// tools/gamebox_preview/harness.c —— 游戏盒主机无头渲染预览。
//
// 用与固件同版本的 LVGL 9.5 在主机上渲染真实的游戏列表与各游戏页面，模拟时间
// 与按键走一遍：列表 → 堆箱子（标题、开局、完美、暂停、结算）→ 飞吧小鸟（标题、
// 准备、飞行、暂停、结算）→ 躲避方块 → 连点挑战 → 带纪录的列表。每个画面（含 BSP 同款 30px 圆角遮罩）写成 PPM。
//
// 另外检查生命周期：每个游戏反复进入 / 退出 8 次，退出后 LVGL 内存池里仍被占用的
// 块数必须不变，否则说明 exit() 漏删了对象、样式或定时器。泄漏时以非零码退出。
//
// 平台服务（音效、存档、电量、调暗）在这里换成只记录调用的替身；退出确认框属于
// gb_app.c（依赖 FreeRTOS 任务），不在预览范围内。它不替代真机测试：SPI 刷屏速度、
// 帧率、按键手感与音效都只能在设备上确认。由 tools/render_gamebox_preview.sh 编译运行。
#include "../../main/games/crane/crane_game.c"

#include <signal.h>
#include <string.h>
#include <unistd.h>

#include "bsp_display_rounding.h"
#include "flappy_flow.h"
#include "flappy_model.h"
#include "gb_launcher.h"
#include "gb_registry.h"

// flappy_probe.c 提供
extern const gb_game_t GB_GAME_FLAPPY;
const fb_model_t *preview_flappy_model(void);
int preview_flappy_state(void);

int64_t g_now_us = 1000000;
static uint16_t s_fb[CT_SCREEN_W * CT_SCREEN_H];
static uint8_t s_buf[CT_SCREEN_W * 40 * 2];
static int s_battery_soc = 76;
static const char *s_out;
static cs_model_t s_probe;
static int s_failures;
// 主机是 64 位指针，LVGL 对象比设备上大得多；主机上仍留得出这么多空闲，设备上就更宽裕。
#define MIN_POOL_FREE (12 * 1024)
static volatile const char *s_stage = "start";   // 看门狗超时时打印当前步骤

// 看门狗：任何一步卡住（例如自动驾驶等不到状态、LVGL 断言死循环）都直接报错退出，
// 而不是让调用方无限等待。
static void on_alarm(int sig) {
    (void)sig;
    static const char msg[] = "preview: TIMEOUT at stage ";
    write(2, msg, sizeof(msg) - 1);
    write(2, (const char *)s_stage, strlen((const char *)s_stage));
    write(2, "\n", 1);
    _exit(2);
}

// ---------------------------------------------------------------- 平台替身

int bsp_battery_soc(void) { return s_battery_soc; }
void gb_app_set_playing(bool playing) { (void)playing; }

static struct { char id[16]; uint32_t best; uint8_t save[GB_SAVE_MAX]; size_t save_len; } s_store[8];

static int store_slot(const char *id) {
    for (int i = 0; i < 8; i++) if (strcmp(s_store[i].id, id) == 0) return i;
    for (int i = 0; i < 8; i++) if (!s_store[i].id[0]) { snprintf(s_store[i].id, 16, "%s", id); return i; }
    return 0;
}
uint32_t gb_score_best(const char *id) { return s_store[store_slot(id)].best; }
bool gb_score_submit(const char *id, uint32_t score) {
    const int i = store_slot(id);
    if (score <= s_store[i].best) return false;
    s_store[i].best = score;
    printf("  score %s -> %u\n", id, (unsigned)score);
    return true;
}
bool gb_save_load(const char *id, void *buf, size_t len) {
    const int i = store_slot(id);
    if (s_store[i].save_len != len) return false;
    memcpy(buf, s_store[i].save, len);
    return true;
}
bool gb_save_store(const char *id, const void *buf, size_t len) {
    const int i = store_slot(id);
    memcpy(s_store[i].save, buf, len);
    s_store[i].save_len = len;
    return true;
}

void gb_sfx_play(const gb_tune_t *tune) { (void)tune; }
void gb_sfx_play_feedback(const gb_tune_t *tune) { (void)tune; }
void gb_sfx_play_synth(const gb_synth_t *synth, uintptr_t sound) { (void)synth; printf("  sfx %d\n", (int)sound); }
void gb_sfx_cancel(void) {}
bool gb_sfx_run_when_idle(void (*fn)(void)) { (void)fn; return false; }
static const gb_note_t NOTE[] = { { 880, 10 } };
const gb_tune_t GB_TUNE_MOVE = GB_TUNE(NOTE);
const gb_tune_t GB_TUNE_CONFIRM = GB_TUNE(NOTE);
const gb_tune_t GB_TUNE_BACK = GB_TUNE(NOTE);
const gb_tune_t GB_TUNE_ASK = GB_TUNE(NOTE);

// ---------------------------------------------------------------- 渲染与时间

static uint32_t tick_cb(void) { return (uint32_t)(g_now_us / 1000); }

static void flush_cb(lv_display_t *d, const lv_area_t *a, uint8_t *pxs) {
    const int w = lv_area_get_width(a);
    for (int y = a->y1; y <= a->y2; y++) {
        memcpy(&s_fb[y * CT_SCREEN_W + a->x1], pxs + (size_t)(y - a->y1) * w * 2, (size_t)w * 2);
    }
    lv_display_flush_ready(d);
}

static void run_ms(int ms) {
    for (int t = 0; t < ms; t += 4) {
        g_now_us += 4000;
        lv_timer_handler();
    }
}

static void shot(const char *name) {
    lv_refr_now(NULL);
    char path[512];
    snprintf(path, sizeof path, "%s/%s.ppm", s_out, name);
    FILE *f = fopen(path, "wb");
    if (!f) { perror(path); exit(1); }
    fprintf(f, "P6\n%d %d\n255\n", CT_SCREEN_W, CT_SCREEN_H);
    for (int y = 0; y < CT_SCREEN_H; y++) {
        for (int x = 0; x < CT_SCREEN_W; x++) {
            uint16_t c = s_fb[y * CT_SCREEN_W + x];
            if (bsp_display_pixel_outside_rounded_rect(x, y, CT_SCREEN_W, CT_SCREEN_H, 30)) c = 0;
            const unsigned char rgb[3] = {
                (unsigned char)((c >> 11) << 3), (unsigned char)(((c >> 5) & 63) << 2),
                (unsigned char)((c & 31) << 3),
            };
            fwrite(rgb, 1, 3, f);
        }
    }
    fclose(f);
    printf("shot %s\n", name);
}

// 内存池里仍被占用的分配块个数。漏删对象 / 样式 / 定时器会让它一轮比一轮多；
// 空闲字节数则会因碎片（块头开销）而有几个字节的波动，不适合用来判断泄漏。
static size_t pool_blocks(void) {
    lv_mem_monitor_t mon;
    lv_mem_monitor(&mon);
    return mon.used_cnt;
}

// 每次按键依次送 PRESS 与 CLICK，与真机按键组件的顺序一致。
static void key(const gb_game_t *g, bsp_btn_t b) {
    g->key(b, BSP_BTN_PRESS);
    g->key(b, BSP_BTN_CLICK);
    run_ms(40);
}

// ---------------------------------------------------------------- 堆箱子自动驾驶

// 在模型副本上模拟「现在松钩」，返回落点误差（Q8）。
static int32_t predict_offset(void) {
    s_probe = s_model;
    if (!cs_model_release(&s_probe)) return INT32_MAX;
    for (int i = 0; i < 2000; i++) {
        if (cs_model_step(&s_probe) & (CS_EV_LAND | CS_EV_MISS)) return s_probe.last_offset;
    }
    return INT32_MAX;
}

// 等到预测误差落在 [lo, hi] 像素时按下 OK；fall_ms 后返回。
static void drop(int lo, int hi, int fall_ms) {
    for (int t = 0; t < 20000 && s_model.state != CS_ST_HANG; t += 4) run_ms(4);
    for (int t = 0; t < 20000; t += 2) {
        const int32_t d = predict_offset();
        if (d >= lo * CS_ONE && d <= hi * CS_ONE) {
            GB_GAME_CRANE.key(BSP_BTN_OK, BSP_BTN_PRESS);
            break;
        }
        g_now_us += 2000;
        if (t % 16 == 0) lv_timer_handler();
    }
    run_ms(fall_ms);
}

static void perfect(int fall_ms) { drop(-1, 1, fall_ms); }

// ---------------------------------------------------------------- 飞吧小鸟自动驾驶

// 与 FlappyBird 仓库预览相同的思路：小鸟快掉到下一个缺口下沿以下时扇一次翅。
static void flappy_autopilot(uint32_t score, int max_ms) {
    const fb_model_t *m = preview_flappy_model();
    for (int t = 0; t < max_ms && m->state == FB_STATE_PLAYING && m->score < score; t += 16) {
        const fb_pipe_t *next = NULL;
        for (int i = 0; i < FB_PIPE_COUNT; i++) {
            const fb_pipe_t *p = &m->pipes[i];
            if ((p->x_q8 >> FB_Q) + FB_PIPE_W < FB_BIRD_X - FB_BIRD_HALF_W) continue;
            if (!next || p->x_q8 < next->x_q8) next = p;
        }
        const int target = fb_pipe_gap_bottom(m, next) - 12;
        if ((int)(m->bird_y_q8 >> FB_Q) + (int)((m->vel_q8 * 2) >> FB_Q) > target) {
            GB_GAME_FLAPPY.key(BSP_BTN_OK, BSP_BTN_PRESS);
        }
        run_ms(16);
    }
}

static void check_headroom(const char *game) {
    lv_mem_monitor_t mon;
    lv_mem_monitor(&mon);
    printf("LVGL pool during %s (host): free=%u biggest=%u\n", game, (unsigned)mon.free_size,
           (unsigned)mon.free_biggest_size);
    if (mon.free_size < MIN_POOL_FREE) {
        printf("FAIL: LVGL pool headroom %u < %u during %s\n", (unsigned)mon.free_size,
               (unsigned)MIN_POOL_FREE, game);
        s_failures++;
    }
}

// ---------------------------------------------------------------- 场景

static void enter(const gb_game_t *g) {
    gb_launcher_destroy();
    g->enter();
}

static void leave(const gb_game_t *g, size_t selected) {
    g->exit();
    gb_launcher_show(selected);
}

// 反复进出同一个游戏（每轮都开一局），退出后内存池的占用块数必须不变。
static void check_lifecycle(const gb_game_t *g, size_t index) {
    size_t baseline = 0;
    for (int round = 0; round < 8; round++) {
        enter(g);
        run_ms(300);
        g->key(BSP_BTN_OK, BSP_BTN_PRESS);
        g->key(BSP_BTN_OK, BSP_BTN_CLICK);
        run_ms(600);
        leave(g, index);
        run_ms(50);
        const size_t blocks = pool_blocks();
        if (round == 0) baseline = blocks;
        else if (blocks != baseline) {
            printf("LEAK %s: round %d blocks=%u baseline=%u\n", g->id, round, (unsigned)blocks,
                   (unsigned)baseline);
            s_failures++;
        }
    }
    printf("lifecycle %s: 8 rounds, %u live LVGL blocks after each exit (stable)\n", g->id,
           (unsigned)baseline);
}

int main(int argc, char **argv) {
    s_out = argc > 1 ? argv[1] : ".";
    setvbuf(stdout, NULL, _IONBF, 0);   // 看门狗 _exit 时不丢输出
    signal(SIGALRM, on_alarm);
    alarm(120);
    lv_init();
    lv_tick_set_cb(tick_cb);
    lv_display_t *d = lv_display_create(CT_SCREEN_W, CT_SCREEN_H);
    lv_display_set_color_format(d, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(d, s_buf, NULL, sizeof s_buf, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(d, flush_cb);

    const gb_game_t *crane = gb_registry_get(0);
    const gb_game_t *flappy = gb_registry_get(1);
    const gb_game_t *dodge = gb_registry_get(2);
    const gb_game_t *tap = gb_registry_get(3);
    if (crane != &GB_GAME_CRANE || flappy != &GB_GAME_FLAPPY || !dodge || !tap) {
        printf("registry order changed; update the harness\n");
        return 1;
    }

    // 列表
    gb_launcher_show(0);
    run_ms(200); shot("01_list_first");
    gb_launcher_select(3);
    run_ms(200); shot("02_list_fourth");

    // 堆箱子
    s_stage = "crane";
    enter(crane);
    run_ms(600); shot("10_crane_title");
    key(crane, BSP_BTN_OK); run_ms(600); shot("11_crane_hang");
    perfect(450); perfect(450); shot("12_crane_combo");
    drop(9, 12, 500);
    for (int i = 0; i < 4; i++) perfect(400);
    run_ms(600); shot("13_crane_tower");
    crane->pause(true); run_ms(1000);                       // 平台确认框打开：冻结
    const uint16_t frozen_count = s_model.count;
    crane->pause(false);
    key(crane, BSP_BTN_UP); shot("14_crane_paused");
    key(crane, BSP_BTN_OK);                                  // 继续
    s_stage = "crane_tower_30";
    for (int i = 0; i < 60 && s_model.count < 30 && s_model.state != CS_ST_OVER; i++) perfect(350);
    run_ms(700); shot("15_crane_tower_30");
    check_headroom("crane");
    for (int i = 0; i < 6 && s_model.state != CS_ST_COLLAPSE; i++) drop(17, 20, 400);
    run_ms(2300); shot("16_crane_over");
    if (frozen_count == 0) { printf("FAIL: crane tower empty before pause\n"); s_failures++; }
    leave(crane, 0);
    run_ms(200); shot("17_list_after_crane");

    // 再进一次：所有「已绘制」缓存都应重置，标题页完整显示。
    enter(crane);
    run_ms(600); shot("18_crane_reenter");
    leave(crane, 0);

    // 飞吧小鸟
    s_stage = "flappy";
    enter(flappy);
    run_ms(600); shot("20_flappy_title");
    key(flappy, BSP_BTN_OK); run_ms(400); shot("21_flappy_ready");
    key(flappy, BSP_BTN_OK);
    flappy_autopilot(6, 60000); run_ms(16); shot("22_flappy_playing");
    check_headroom("flappy");
    flappy->pause(true); run_ms(1000);                      // 平台确认框打开：冻结
    const uint32_t frozen_ground = preview_flappy_model()->ground_q8;
    run_ms(500);
    if (preview_flappy_model()->ground_q8 != frozen_ground) {
        printf("FAIL: flappy kept moving while frozen\n");
        s_failures++;
    }
    flappy->pause(false);
    key(flappy, BSP_BTN_DOWN); shot("23_flappy_paused");
    key(flappy, BSP_BTN_OK);                                 // 继续
    flappy_autopilot(12, 120000);
    for (int i = 0; i < 400 && preview_flappy_state() == FF_PLAYING; i++) run_ms(16);   // 停手
    run_ms(2500); shot("24_flappy_over");
    leave(flappy, 1);
    enter(flappy);
    run_ms(600); shot("25_flappy_reenter");
    leave(flappy, 1);

    // 躲避方块
    s_stage = "dodge";
    enter(dodge);
    run_ms(200); shot("30_dodge_ready");
    key(dodge, BSP_BTN_OK); run_ms(1500); shot("31_dodge_run");
    leave(dodge, 2);

    // 连点挑战
    s_stage = "tap";
    enter(tap);
    run_ms(200); shot("40_tap_ready");
    for (int i = 0; i < 12; i++) { tap->key(BSP_BTN_OK, BSP_BTN_PRESS); run_ms(120); }
    shot("41_tap_run");
    run_ms(10000); shot("42_tap_over");
    leave(tap, 3);
    run_ms(200); shot("43_list_with_records");

    // 低电量 / 电量不可用
    s_battery_soc = 12; gb_launcher_show(0); run_ms(200); shot("50_list_battery_low");
    s_battery_soc = -1; gb_launcher_show(0); run_ms(200); shot("51_list_battery_unknown");
    s_battery_soc = 76;

    // 生命周期：反复进出
    s_stage = "lifecycle";
    check_lifecycle(crane, 0);
    check_lifecycle(flappy, 1);
    check_lifecycle(dodge, 2);
    check_lifecycle(tap, 3);

    lv_mem_monitor_t mon;
    lv_mem_monitor(&mon);
    printf("LVGL pool (host, 64-bit pointers): max_used=%u of %u\n", (unsigned)mon.max_used,
           (unsigned)mon.total_size);
    printf(s_failures ? "preview: FAIL\n" : "preview: PASS\n");
    return s_failures ? 1 : 0;
}
