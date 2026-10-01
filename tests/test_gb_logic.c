// tests/test_gb_logic.c —— 游戏盒纯逻辑的主机测试。
//
// 覆盖：导航状态机、空闲调暗与唤醒吞键、最高分键名、方波合成、I4 绘制、
// 连点挑战与躲避方块的规则和绘制。不依赖 ESP-IDF / LVGL。
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dodge_draw.h"
#include "dodge_model.h"
#include "gb_i4.h"
#include "gb_idle.h"
#include "gb_launcher_layout.h"
#include "gb_nav.h"
#include "gb_score_key.h"
#include "gb_tone.h"
#include "tap_model.h"

static int s_failures;

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #cond); \
            s_failures++;                                                    \
        }                                                                    \
    } while (0)

// ---------------------------------------------------------------- 导航

static void test_nav(void) {
    gb_nav_t nav;
    gb_nav_init(&nav, 3);

    // 启动页：只认单击，上下循环。
    CHECK(gb_nav_handle(&nav, GB_KEY_UP, GB_KEY_EV_PRESS).action == GB_ACT_NONE);
    gb_nav_result_t r = gb_nav_handle(&nav, GB_KEY_UP, GB_KEY_EV_CLICK);
    CHECK(r.action == GB_ACT_SELECT && r.index == 2 && r.dir == -1);
    r = gb_nav_handle(&nav, GB_KEY_DOWN, GB_KEY_EV_CLICK);
    CHECK(r.action == GB_ACT_SELECT && r.index == 0 && r.dir == 1);
    r = gb_nav_handle(&nav, GB_KEY_DOWN, GB_KEY_EV_CLICK);
    CHECK(r.index == 1);
    CHECK(gb_nav_handle(&nav, GB_KEY_OK, GB_KEY_EV_LONG).action == GB_ACT_NONE);

    // 启动是两段式：完成前仍在启动页。
    r = gb_nav_handle(&nav, GB_KEY_OK, GB_KEY_EV_CLICK);
    CHECK(r.action == GB_ACT_LAUNCH && r.index == 1);
    CHECK(nav.mode == GB_MODE_LAUNCHER);
    gb_nav_launched(&nav);
    CHECK(nav.mode == GB_MODE_PLAYING);

    // 游戏中：除确定长按外全部转发。
    CHECK(gb_nav_handle(&nav, GB_KEY_UP, GB_KEY_EV_PRESS).action == GB_ACT_FORWARD);
    CHECK(gb_nav_handle(&nav, GB_KEY_OK, GB_KEY_EV_CLICK).action == GB_ACT_FORWARD);
    CHECK(gb_nav_handle(&nav, GB_KEY_UP, GB_KEY_EV_LONG).action == GB_ACT_FORWARD);
    r = gb_nav_handle(&nav, GB_KEY_OK, GB_KEY_EV_LONG);
    CHECK(r.action == GB_ACT_ASK_EXIT && nav.mode == GB_MODE_CONFIRM);

    // 确认框：非单击忽略，上下取消，确定退出。
    CHECK(gb_nav_handle(&nav, GB_KEY_OK, GB_KEY_EV_PRESS).action == GB_ACT_NONE);
    r = gb_nav_handle(&nav, GB_KEY_DOWN, GB_KEY_EV_CLICK);
    CHECK(r.action == GB_ACT_CANCEL_EXIT && nav.mode == GB_MODE_PLAYING);
    gb_nav_handle(&nav, GB_KEY_OK, GB_KEY_EV_LONG);
    r = gb_nav_handle(&nav, GB_KEY_OK, GB_KEY_EV_CLICK);
    CHECK(r.action == GB_ACT_EXIT && r.index == 1);
    CHECK(nav.mode == GB_MODE_CONFIRM);   // 退出未完成前保持确认状态
    gb_nav_exited(&nav);
    CHECK(nav.mode == GB_MODE_LAUNCHER && nav.selected == 1);

    // 没有游戏时什么也不做。
    gb_nav_init(&nav, 0);
    CHECK(gb_nav_handle(&nav, GB_KEY_OK, GB_KEY_EV_CLICK).action == GB_ACT_NONE);
    gb_nav_launched(&nav);
    CHECK(nav.mode == GB_MODE_LAUNCHER);
}

// ---------------------------------------------------------------- 游戏列表滚动

static void test_list_layout(void) {
    // 不超过一屏：永远从第 0 行开始，没有滚动条。
    CHECK(gb_list_first_visible(0, 2, 3, 4) == 0);
    CHECK(gb_list_first_visible(5, 3, 4, 4) == 0);
    CHECK(gb_list_thumb(0, 4, 4, 44, 240, 24).h == 0);

    // 10 个游戏、一屏 4 行：往下走到第 4 个时窗口下移一行。
    size_t first = 0;
    first = gb_list_first_visible(first, 3, 10, 4);
    CHECK(first == 0);
    first = gb_list_first_visible(first, 4, 10, 4);
    CHECK(first == 1);
    first = gb_list_first_visible(first, 2, 10, 4);   // 往回走、仍可见：窗口不动
    CHECK(first == 1);
    first = gb_list_first_visible(first, 0, 10, 4);
    CHECK(first == 0);
    first = gb_list_first_visible(first, 9, 10, 4);   // 从第一个绕到最后一个
    CHECK(first == 6);
    first = gb_list_first_visible(first, 0, 10, 4);   // 从最后一个绕回第一个
    CHECK(first == 0);
    CHECK(gb_list_first_visible(8, 9, 10, 4) == 6);   // 窗口不会越过末尾
    CHECK(gb_list_first_visible(0, 50, 10, 4) == 6);  // 越界选中项被夹紧

    // 滑块：高度按比例、有下限，顶端/底端对齐轨道两端。
    gb_list_thumb_t t = gb_list_thumb(0, 10, 4, 44, 240, 24);
    CHECK(t.h == 96 && t.y == 44);
    t = gb_list_thumb(6, 10, 4, 44, 240, 24);
    CHECK(t.y + t.h == 44 + 240);
    t = gb_list_thumb(3, 10, 4, 44, 240, 24);
    CHECK(t.y == 44 + (240 - 96) * 3 / 6);
    t = gb_list_thumb(0, 100, 4, 44, 240, 24);
    CHECK(t.h == 24);
}

// ---------------------------------------------------------------- 空闲

static void test_idle(void) {
    gb_idle_t idle;
    gb_idle_init(&idle, 1000, 30000, 90000);
    CHECK(gb_idle_update(&idle, 30999, false) == GB_POWER_ACTIVE);
    CHECK(gb_idle_update(&idle, 31000, false) == GB_POWER_DIM);
    // 调暗时按键照常生效。
    CHECK(!gb_idle_input(&idle, 32000, GB_KEY_UP, GB_KEY_EV_PRESS));
    CHECK(idle.state == GB_POWER_ACTIVE);
    CHECK(gb_idle_update(&idle, 32000 + 90000, false) == GB_POWER_OFF);

    // 熄屏后唤醒：整组按键被吞掉，下一次按键正常。
    const uint32_t t = 200000;
    CHECK(gb_idle_input(&idle, t, GB_KEY_OK, GB_KEY_EV_PRESS));
    CHECK(idle.state == GB_POWER_ACTIVE);
    CHECK(gb_idle_input(&idle, t + 150, GB_KEY_OK, GB_KEY_EV_CLICK));
    CHECK(!gb_idle_input(&idle, t + 600, GB_KEY_OK, GB_KEY_EV_PRESS));

    // 唤醒后按住不放：长按也被吞掉（否则游戏里会弹出退出确认）。
    gb_idle_update(&idle, t + 600 + 90000, false);
    CHECK(gb_idle_input(&idle, t + 100000, GB_KEY_OK, GB_KEY_EV_PRESS));
    CHECK(gb_idle_input(&idle, t + 100500, GB_KEY_OK, GB_KEY_EV_LONG));
    CHECK(!gb_idle_input(&idle, t + 101000, GB_KEY_OK, GB_KEY_EV_PRESS));

    // 换了键或超时：不再吞。
    gb_idle_update(&idle, t + 101000 + 90000, false);
    CHECK(gb_idle_input(&idle, t + 200000, GB_KEY_UP, GB_KEY_EV_PRESS));
    CHECK(!gb_idle_input(&idle, t + 200100, GB_KEY_DOWN, GB_KEY_EV_PRESS));
    gb_idle_update(&idle, t + 200100 + 90000, false);
    CHECK(gb_idle_input(&idle, t + 300000, GB_KEY_UP, GB_KEY_EV_PRESS));
    CHECK(!gb_idle_input(&idle, t + 300000 + GB_IDLE_WAKE_SWALLOW_MS + 1, GB_KEY_UP, GB_KEY_EV_CLICK));

    // 游戏进行中不调暗。
    gb_idle_init(&idle, 0, 1000, 2000);
    CHECK(gb_idle_update(&idle, 5000, true) == GB_POWER_ACTIVE);
    CHECK(gb_idle_update(&idle, 5999, false) == GB_POWER_ACTIVE);
    CHECK(gb_idle_update(&idle, 6000, false) == GB_POWER_DIM);

    // 计时器回绕。
    gb_idle_init(&idle, UINT32_MAX - 500, 1000, 2000);
    CHECK(gb_idle_update(&idle, 400, false) == GB_POWER_ACTIVE);
    CHECK(gb_idle_update(&idle, 600, false) == GB_POWER_DIM);
}

// ---------------------------------------------------------------- 最高分键名

static void test_score_key(void) {
    char key[GB_SCORE_KEY_LEN];
    CHECK(gb_score_make_key("snake", key, sizeof(key)) && strcmp(key, "b_snake") == 0);
    CHECK(gb_score_make_key("abcdefghijk_9", key, sizeof(key)) && strlen(key) == 15);
    CHECK(!gb_score_make_key("abcdefghijk_90", key, sizeof(key)) && key[0] == '\0');
    CHECK(!gb_game_id_valid(""));
    CHECK(!gb_game_id_valid(NULL));
    CHECK(!gb_game_id_valid("Snake"));
    CHECK(!gb_game_id_valid("fl-bird"));
    CHECK(!gb_score_make_key("tap", key, 8));
    CHECK(gb_store_make_key('s', "crane", key, sizeof(key)) && strcmp(key, "s_crane") == 0);
    CHECK(!gb_store_make_key('S', "crane", key, sizeof(key)));
    CHECK(!gb_store_make_key('_', "crane", key, sizeof(key)));
}

// ---------------------------------------------------------------- 方波

static void test_tone(void) {
    const gb_tone_config_t cfg = { .sample_rate = 16000, .amplitude = 6000, .fade_samples = 48 };
    const size_t total = gb_tone_samples_for(25, &cfg);
    CHECK(total == 400);

    int16_t whole[400];
    int16_t chunked[400];
    gb_tone_render_chunk(whole, total, 0, total, 880, &cfg);
    for (size_t first = 0; first < total; first += 37) {
        size_t n = total - first < 37 ? total - first : 37;
        gb_tone_render_chunk(chunked + first, n, first, total, 880, &cfg);
    }
    CHECK(memcmp(whole, chunked, sizeof(whole)) == 0);
    CHECK(whole[0] == 0 && whole[total - 1] == 0);   // 首尾归零，无爆音
    int peak = 0;
    for (size_t i = 0; i < total; i++) peak = abs(whole[i]) > peak ? abs(whole[i]) : peak;
    CHECK(peak == 6000);

    gb_tone_render_chunk(whole, total, 0, total, 0, &cfg);
    for (size_t i = 0; i < total; i++) CHECK(whole[i] == 0);
    gb_tone_render_chunk(whole, 4, total, total, 880, &cfg);   // 越过结尾写 0
    CHECK(whole[0] == 0 && whole[3] == 0);

    // 流式播放器：任意块大小与一次渲染整段逐样本一致；0 时长音符被跳过。
    static const gb_note_t NOTES[] = { { 784, 10 }, { 0, 0 }, { 0, 5 }, { 1175, 12 } };
    static const gb_tune_t TUNE = GB_TUNE(NOTES);
    CHECK(TUNE.count == 4);
    int16_t ref[1000], got[1000];
    gb_tune_player_t p;
    gb_tune_player_start(&p, &TUNE);
    const size_t ref_n = gb_tune_player_render(&p, &cfg, ref, 1000);
    CHECK(ref_n == 160 + 0 + 80 + 192);
    CHECK(gb_tune_player_render(&p, &cfg, ref + ref_n, 10) == 0);   // 播完
    int16_t note0[160];
    gb_tone_render_chunk(note0, 160, 0, 160, 784, &cfg);
    CHECK(memcmp(ref, note0, sizeof(note0)) == 0);
    gb_tune_player_start(&p, &TUNE);
    size_t got_n = 0, n;
    while ((n = gb_tune_player_render(&p, &cfg, got + got_n, 33)) > 0) got_n += n;
    CHECK(got_n == ref_n && memcmp(ref, got, ref_n * sizeof(int16_t)) == 0);
    gb_tune_player_start(&p, NULL);
    CHECK(gb_tune_player_render(&p, &cfg, got, 10) == 0);
}

// ---------------------------------------------------------------- I4

static void test_i4(void) {
    uint8_t buf[5 * 4];   // 9x4 像素，stride 5
    memset(buf, 0, sizeof(buf));
    const gb_i4_t img = { .base = buf, .stride = 5, .w = 9, .h = 4 };

    gb_i4_px(&img, 0, 0, 0xA);
    gb_i4_px(&img, 1, 0, 0x3);
    CHECK(buf[0] == 0xA3);   // 偶数列在高 4 位
    gb_i4_px(&img, 9, 0, 0xF);   // 越界忽略
    gb_i4_px(&img, -1, 0, 0xF);
    CHECK(buf[4] == 0);

    gb_i4_fill_rect(&img, 1, 1, 6, 2, 0x7);   // 奇数起点、偶数宽度
    for (int y = 0; y < 4; y++) {
        for (int x = 0; x < 9; x++) {
            const bool inside = x >= 1 && x < 7 && y >= 1 && y < 3;
            if (y == 0) continue;
            CHECK(gb_i4_get(&img, x, y) == (inside ? 0x7 : 0));
        }
    }
    gb_i4_fill_rect(&img, -5, -5, 100, 100, 0x2);   // 裁剪到整块
    for (int y = 0; y < 4; y++)
        for (int x = 0; x < 9; x++) CHECK(gb_i4_get(&img, x, y) == 0x2);
    gb_i4_fill_rect(&img, 2, 2, 0, 5, 0x9);   // 空矩形不画
    CHECK(gb_i4_get(&img, 2, 2) == 0x2);
}

// ---------------------------------------------------------------- 连点挑战

static void test_tap(void) {
    tap_t t;
    tap_init(&t);
    CHECK(tap_remaining_ms(&t, 0) == TAP_DURATION_MS);
    CHECK(!tap_update(&t, 99999));
    CHECK(tap_press(&t, 1000) && t.state == TAP_RUN && t.count == 1);
    CHECK(tap_press(&t, 1100) && t.count == 2);
    CHECK(tap_remaining_ms(&t, 6000) == 5000);
    CHECK(!tap_update(&t, 1000 + TAP_DURATION_MS - 1));
    CHECK(tap_press(&t, 1000 + TAP_DURATION_MS - 1) && t.count == 3);
    CHECK(!tap_press(&t, 1000 + TAP_DURATION_MS));   // 时间到的按下不算
    CHECK(t.state == TAP_OVER && t.count == 3);
    CHECK(!tap_update(&t, 1000 + TAP_DURATION_MS + 10));   // 只报告一次结束
    CHECK(!tap_can_restart(&t, 1000 + TAP_DURATION_MS + TAP_RESTART_COOLDOWN_MS - 1));
    CHECK(tap_can_restart(&t, 1000 + TAP_DURATION_MS + TAP_RESTART_COOLDOWN_MS));
    CHECK(tap_remaining_ms(&t, 0) == 0);

    tap_init(&t);
    tap_press(&t, UINT32_MAX - 100);   // 回绕
    CHECK(tap_remaining_ms(&t, 899) == TAP_DURATION_MS - 1000);
}

// ---------------------------------------------------------------- 躲避方块

static void test_dodge(void) {
    dodge_t g;
    dodge_init(&g);
    CHECK(g.state == DODGE_READY && g.lane == DODGE_LANES / 2);
    CHECK(dodge_step(&g, 30) == 0);   // 未开局不动
    CHECK(dodge_move(&g, -1) && g.lane == DODGE_LANES / 2 - 1);   // 开局前可选车道

    dodge_start(&g, 12345);
    CHECK(g.state == DODGE_RUN && g.lane == DODGE_LANES / 2 - 1);
    g.lane = 0;
    CHECK(!dodge_move(&g, -1) && g.lane == 0);
    g.lane = DODGE_LANES - 1;
    CHECK(!dodge_move(&g, +1));

    // 难度随分数上升并封顶。
    CHECK(dodge_speed(&g) == DODGE_SPEED_START && dodge_spawn_interval(&g) == DODGE_SPAWN_START);
    g.score = 1000;
    CHECK(dodge_speed(&g) == DODGE_SPEED_MAX && dodge_spawn_interval(&g) == DODGE_SPAWN_MIN);
    g.score = 0;

    // 别的车道的方块飞过去 = 得分。
    dodge_start(&g, 7);
    g.lane = 0;
    memset(g.blocks, 0, sizeof(g.blocks));
    g.blocks[0] = (dodge_block_t){ .used = true, .lane = 3, .x_q4 = 2 << 4 };
    g.spawn_left_ms = 100000;
    uint32_t ev = dodge_step(&g, 50);   // 90px/s * 50ms = 4.5px，越过左边缘
    CHECK(ev == 0);   // 2 - 4.5 = -2.5，右边缘仍可见
    for (int i = 0; i < 10 && !(ev & DODGE_EV_PASS); i++) ev = dodge_step(&g, 50);
    CHECK((ev & DODGE_EV_PASS) && g.score == 1 && !g.blocks[0].used);

    // 同车道方块碰到玩家 = 结束。
    g.blocks[1] = (dodge_block_t){ .used = true, .lane = 0,
                                   .x_q4 = (DODGE_PLAYER_X + DODGE_PLAYER_SIZE + 1) << 4 };
    ev = dodge_step(&g, 50);
    CHECK((ev & DODGE_EV_CRASH) && g.state == DODGE_OVER);
    CHECK(dodge_step(&g, 50) == 0);
    CHECK(!dodge_move(&g, 1));

    // 单步时长被限制，卡顿后不会「瞬移」穿过玩家。
    dodge_start(&g, 7);
    memset(g.blocks, 0, sizeof(g.blocks));
    g.lane = 2;
    g.spawn_left_ms = 100000;
    g.blocks[0] = (dodge_block_t){ .used = true, .lane = 2,
                                   .x_q4 = (DODGE_PLAYER_X + DODGE_PLAYER_SIZE + 2) << 4 };
    CHECK(dodge_step(&g, 5000) & DODGE_EV_CRASH);

    // 同一种子 → 同样的方块序列；双块一定在不同车道。
    dodge_t a, b;
    dodge_init(&a);
    dodge_init(&b);
    dodge_start(&a, 99);
    dodge_start(&b, 99);
    a.score = b.score = DODGE_DOUBLE_FROM;
    a.lane = b.lane = 0;
    for (int i = 0; i < 400; i++) {
        a.lane = b.lane = (uint8_t)(i % DODGE_LANES);   // 避免撞车结束
        a.state = b.state = DODGE_RUN;
        dodge_step(&a, 20);
        dodge_step(&b, 20);
        for (int k = 0; k < DODGE_MAX_BLOCKS; k++) {
            CHECK(a.blocks[k].used == b.blocks[k].used && a.blocks[k].lane == b.blocks[k].lane);
            CHECK(a.blocks[k].lane < DODGE_LANES);
        }
    }
}

static void test_dodge_draw(void) {
    static uint8_t buf[DODGE_W / 2 * DODGE_H];
    const gb_i4_t img = { .base = buf, .stride = DODGE_W / 2, .w = DODGE_W, .h = DODGE_H };
    dodge_t g;
    dodge_init(&g);
    g.lane = 1;
    g.blocks[0] = (dodge_block_t){ .used = true, .lane = 4, .x_q4 = 100 << 4 };
    g.blocks[1] = (dodge_block_t){ .used = true, .lane = 0, .x_q4 = -10 * 16 };   // 部分出界
    dodge_draw(&g, &img);

    const int py = dodge_lane_top(1) + (DODGE_LANE_H - DODGE_PLAYER_SIZE) / 2;
    CHECK(gb_i4_get(&img, DODGE_PLAYER_X + 1, py + 1) == DODGE_PAL_PLAYER);
    const int by = dodge_lane_top(4) + (DODGE_LANE_H - DODGE_BLOCK_H) / 2;
    CHECK(gb_i4_get(&img, 110, by + 10) == DODGE_PAL_BLOCK);
    CHECK(gb_i4_get(&img, 100, by) == DODGE_PAL_BLOCK_LIGHT);
    CHECK(gb_i4_get(&img, 5, dodge_lane_top(0) + 10) == DODGE_PAL_BLOCK);
    CHECK(gb_i4_get(&img, 190, 5) == DODGE_PAL_BG);

    g.state = DODGE_OVER;
    dodge_draw(&g, &img);
    CHECK(gb_i4_get(&img, DODGE_PLAYER_X + 1, py + 1) == DODGE_PAL_CRASH);
}

int main(void) {
    test_nav();
    test_list_layout();
    test_idle();
    test_score_key();
    test_tone();
    test_i4();
    test_tap();
    test_dodge();
    test_dodge_draw();
    if (s_failures) {
        fprintf(stderr, "test_gb_logic: %d failure(s)\n", s_failures);
        return 1;
    }
    printf("test_gb_logic: PASS\n");
    return 0;
}
