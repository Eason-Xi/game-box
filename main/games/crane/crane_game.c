// main/games/crane/crane_game.c —— 堆箱子:界面、游戏循环与游戏盒接入。
//
// 移植自独立固件 crane-stack 的 main/crane_ui.c。规则(crane_model)、页面流程
// (crane_flow)与音效合成(crane_sfx)原样保留;本文件的改动只有接入游戏盒:
//   * 一次性创建的界面改成 enter()/exit():每次进入重建界面、重置所有「已绘制」
//     缓存,退出时删除定时器、屏幕和共享样式。
//   * 原来的后台服务任务(音效、存档、电量)换成平台的 gb_sfx / gb_score:
//     音效经合成器接口交给 gb_sfx 播放;最高分进 gb_score(游戏列表显示它),
//     音效开关与最高层数存进本游戏的自定义存档。平台同样在两段音效之间才写 flash。
//   * 平台的退出确认框弹出时 pause(true) 冻结模拟,关闭后从当前时刻继续。
//
// 本游戏一屏约 120 个 LVGL 对象,依赖 sdkconfig.defaults 里 48KB 的 LVGL 内存池。
#include <stdio.h>
#include <stdlib.h>

#include "bsp_battery.h"
#include "crane_flow.h"
#include "crane_model.h"
#include "crane_sfx.h"
#include "crane_text.h"
#include "crane_theme.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "gb_app.h"
#include "gb_game.h"
#include "gb_score.h"
#include "gb_sfx.h"
#include "gb_ui.h"
#include "lvgl.h"

static const char *TAG = "crane";

#define GAME_ID          "crane"

#define LOOP_PERIOD_MS   16
#define MAX_CATCHUP      8      // 一帧最多补 8 个节拍,卡顿时宁可放慢也不瞬移
#define KEY_QUEUE_DEPTH  8
#define POPUP_MS         900
#define POPUP_RISE_PX    18
#define FLASH_MS         180
#define BATTERY_UI_MS    5000   // 读电量走 I2C,每 5 秒一次
#define STATS_PERIOD_MS  10000  // 游戏中每 10 秒打印一次帧率,便于上机核对流畅度
#define CITY_COUNT       5
#define OFFSCREEN        (-10000)

typedef struct {
    uint8_t key;
    int64_t t_us;               // 按下时刻,用于把松钩落在真实的按键时间
} key_event_t;

typedef struct {
    lv_obj_t *obj;
    int x, y;                   // 已应用的屏幕坐标,避免重复失效
    int layer;
} box_view_t;

typedef struct {
    lv_obj_t *obj;
    int32_t world_y;            // 云的底边世界高度
    int y;
} cloud_view_t;

typedef struct {
    lv_obj_t *row;
    lv_obj_t *left;
    lv_obj_t *right;
} menu_row_t;

typedef enum { POP_NONE = 0, POP_GOOD, POP_BAD } popup_kind_t;

static QueueHandle_t s_keys;
static cf_flow_t s_flow;
static cs_model_t s_model;
static int64_t s_sim_us;
static bool s_frozen;                // 平台退出确认框打开期间冻结
static uint32_t s_stat_frames, s_stat_lag, s_stat_ms;

static lv_style_t s_style_box;
static lv_style_t s_style_tape;

static lv_obj_t *s_scr;
static cloud_view_t s_clouds[CT_CLOUD_COUNT];
static lv_obj_t *s_ground;           // 楼群 + 草皮 + 沙土 + 地基,随镜头整体移动
static int s_ground_y = OFFSCREEN;
static box_view_t s_active;          // 吊着/下落/掉落中的箱子
static box_view_t s_tower[CT_TOWER_POOL];
static lv_obj_t *s_rope;
static lv_point_precise_t s_rope_pts[2];
static lv_obj_t *s_hook;
static int s_hook_x = OFFSCREEN, s_hook_y = OFFSCREEN;
static uint32_t s_sky_shown = 0xFFFFFFFFu;

static lv_obj_t *s_score[2];         // [0]=投影 [1]=正文
static lv_obj_t *s_height[2];
static lv_obj_t *s_lives[CS_MAX_LIVES];
static lv_obj_t *s_batt_text;
static lv_obj_t *s_batt_frame;
static lv_obj_t *s_batt_nub;
static lv_obj_t *s_batt_fill;
static int s_batt_shown = -2;
static uint32_t s_batt_ms;
static uint32_t s_score_shown = UINT32_MAX;
static int s_height_shown = -1;
static int s_lives_shown = -1;

static lv_obj_t *s_popup;
static lv_obj_t *s_popup_text[2];
static uint32_t s_popup_at_ms;
static bool s_popup_on;
static int s_popup_y = OFFSCREEN;

static int s_flash_layer = -1;
static uint32_t s_flash_at_ms;

static lv_obj_t *s_hint;

static lv_obj_t *s_title;
static menu_row_t s_menu[CF_TITLE_ITEMS];
static lv_obj_t *s_menu_best;

static lv_obj_t *s_dim;
static lv_obj_t *s_pause;
static menu_row_t s_pause_rows[CF_PAUSE_ITEMS];

static lv_obj_t *s_over;
static lv_obj_t *s_over_title;
static lv_obj_t *s_over_values[4];
static lv_obj_t *s_record;
static menu_row_t s_over_rows[CF_OVER_ITEMS];

// ------------------------------------------------------------------ 小工具

static int px(int32_t q8) { return (int)(q8 >> CS_Q); }

static uint32_t now_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }

static lv_obj_t *plain(lv_obj_t *parent) {
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    return o;
}

static lv_obj_t *rect(lv_obj_t *parent, int x, int y, int w, int h, uint32_t color) {
    lv_obj_t *o = plain(parent);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_bg_color(o, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    return o;
}

static lv_obj_t *label(lv_obj_t *parent, const lv_font_t *font, uint32_t color, const char *text) {
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    lv_label_set_text(l, text);
    return l;
}

// 居中的单行文字,宽度占满父对象。
static lv_obj_t *center_label(lv_obj_t *parent, const lv_font_t *font, uint32_t color,
                              const char *text, int y) {
    lv_obj_t *l = label(parent, font, color, text);
    lv_obj_set_width(l, lv_pct(100));
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_y(l, y);
    return l;
}

// 带 2px 深色投影的居中文字:pair[0] 为投影,pair[1] 为正文。
static void shadow_label(lv_obj_t *parent, lv_obj_t *pair[2], const lv_font_t *font, uint32_t color,
                         const char *text, int y) {
    pair[0] = center_label(parent, font, CT_COLOR_SHADOW, text, y + 2);
    lv_obj_set_x(pair[0], 2);
    pair[1] = center_label(parent, font, color, text, y);
}

static lv_obj_t *card(lv_obj_t *parent, int x, int y, int w, int h) {
    lv_obj_t *c = plain(parent);
    lv_obj_set_pos(c, x, y);
    lv_obj_set_size(c, w, h);
    lv_obj_set_style_bg_color(c, lv_color_hex(CT_COLOR_CARD), 0);
    lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(c, lv_color_hex(CT_COLOR_CARD_EDGE), 0);
    lv_obj_set_style_border_width(c, 3, 0);
    lv_obj_set_style_radius(c, 12, 0);
    return c;
}

static menu_row_t menu_row(lv_obj_t *parent, int x, int y, int w, const char *left) {
    menu_row_t r;
    r.row = plain(parent);
    lv_obj_set_pos(r.row, x, y);
    lv_obj_set_size(r.row, w, CT_MENU_ROW_H);
    lv_obj_set_style_radius(r.row, CT_MENU_ROW_H / 2, 0);
    lv_obj_set_style_bg_color(r.row, lv_color_hex(CT_COLOR_FOCUS), 0);
    r.left = label(r.row, &gb_font_16, CT_COLOR_INK, left);
    r.right = NULL;
    lv_obj_align(r.left, LV_ALIGN_CENTER, 0, 0);
    return r;
}

static void menu_row_split(menu_row_t *r, const char *right) {
    lv_obj_align(r->left, LV_ALIGN_LEFT_MID, 16, 0);
    r->right = label(r->row, &gb_font_16, CT_COLOR_INK, right);
    lv_obj_align(r->right, LV_ALIGN_RIGHT_MID, -16, 0);
}

static void menu_row_focus(menu_row_t *r, bool focused) {
    const uint32_t color = focused ? CT_COLOR_ON_FOCUS : CT_COLOR_INK;
    lv_obj_set_style_bg_opa(r->row, focused ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    lv_obj_set_style_text_color(r->left, lv_color_hex(color), 0);
    if (r->right) lv_obj_set_style_text_color(r->right, lv_color_hex(color), 0);
}

static void show(lv_obj_t *o, bool visible) {
    if (visible) lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
}

static void synth_start(void *player, uintptr_t sound) {
    cs_sfx_start(player, (cs_sfx_t)sound);
}

static size_t synth_render(void *player, int16_t *out, size_t n) {
    return cs_sfx_render(player, out, n);
}

static bool synth_is_feedback(uintptr_t sound) {
    return cs_sfx_is_feedback((cs_sfx_t)sound);
}

_Static_assert(sizeof(cs_sfx_player_t) <= GB_SYNTH_PLAYER_MAX, "cs_sfx_player_t too large");
_Static_assert(CS_SFX_RATE == GB_SFX_RATE, "crane sfx sample rate must match the platform");

static const gb_synth_t CRANE_SYNTH = {
    .start = synth_start, .render = synth_render, .is_feedback = synth_is_feedback,
};

static void play(cs_sfx_t id) {
    if (s_flow.settings.sound_on) gb_sfx_play_synth(&CRANE_SYNTH, (uintptr_t)id);
}

static void save_settings(void) {
    (void)gb_save_store(GAME_ID, &s_flow.settings, sizeof(s_flow.settings));
}

static uint32_t lerp_color(uint32_t a, uint32_t b, int t, int span) {
    uint32_t out = 0;
    for (int shift = 0; shift <= 16; shift += 8) {
        const int ca = (int)((a >> shift) & 0xFF);
        const int cb = (int)((b >> shift) & 0xFF);
        out |= (uint32_t)(ca + (cb - ca) * t / span) << shift;
    }
    return out;
}

// ------------------------------------------------------------------ 场景

static lv_obj_t *box_create(lv_obj_t *parent) {
    lv_obj_t *o = plain(parent);
    lv_obj_add_style(o, &s_style_box, 0);
    lv_obj_set_size(o, CS_BOX_W, CS_BOX_H);
    lv_obj_t *tape = plain(o);
    lv_obj_add_style(tape, &s_style_tape, 0);
    lv_obj_set_pos(tape, (CS_BOX_W - CT_TAPE_W) / 2 - 2, 0);   // 子对象相对描边内侧定位
    lv_obj_set_size(tape, CT_TAPE_W, CS_BOX_H - 4);
    lv_obj_set_pos(o, OFFSCREEN, OFFSCREEN);
    return o;
}

static void init_styles(void) {
    lv_style_init(&s_style_box);
    lv_style_set_bg_color(&s_style_box, lv_color_hex(CT_COLOR_BOX));
    lv_style_set_bg_opa(&s_style_box, LV_OPA_COVER);
    lv_style_set_border_color(&s_style_box, lv_color_hex(CT_COLOR_BOX_EDGE));
    lv_style_set_border_width(&s_style_box, 2);
    lv_style_set_radius(&s_style_box, 3);
    lv_style_set_pad_all(&s_style_box, 0);

    lv_style_init(&s_style_tape);
    lv_style_set_bg_color(&s_style_tape, lv_color_hex(CT_COLOR_TAPE));
    lv_style_set_bg_opa(&s_style_tape, LV_OPA_COVER);
}

static void build_scene(void) {
    // 云:世界坐标固定,随镜头下移,移出屏幕底部后挪到最上面循环使用
    static const struct { int x, w, h; } CLOUDS[CT_CLOUD_COUNT] = {
        { 18, 70, 22 }, { 150, 56, 18 }, { 80, 84, 24 },
    };
    for (int i = 0; i < CT_CLOUD_COUNT; i++) {
        lv_obj_t *c = rect(s_scr, CLOUDS[i].x, OFFSCREEN, CLOUDS[i].w, CLOUDS[i].h, CT_COLOR_CLOUD);
        lv_obj_set_style_radius(c, CLOUDS[i].h / 2, 0);
        lv_obj_set_style_bg_opa(c, LV_OPA_80, 0);
        s_clouds[i].obj = c;
        s_clouds[i].world_y = 150 + i * CT_CLOUD_SPACING;
        s_clouds[i].y = OFFSCREEN;
    }

    // 地面组:原点在镜头为 0 时的楼群顶部
    static const struct { int x, w, h; } CITY[CITY_COUNT] = {
        { 0, 34, 54 }, { 30, 26, 36 }, { 150, 30, 62 }, { 176, 38, 40 }, { 206, 34, 70 },
    };
    const int city_top = CT_GRASS_Y - 70;
    s_ground = plain(s_scr);
    lv_obj_set_size(s_ground, CT_SCREEN_W, CT_SCREEN_H - city_top);
    for (int i = 0; i < CITY_COUNT; i++) {
        rect(s_ground, CITY[i].x, CT_GRASS_Y - city_top - CITY[i].h, CITY[i].w, CITY[i].h, CT_COLOR_CITY);
    }
    rect(s_ground, 0, CT_GRASS_Y - city_top, CT_SCREEN_W, CT_GRASS_H, CT_COLOR_GRASS);
    rect(s_ground, 0, CT_DIRT_Y - city_top, CT_SCREEN_W, CT_SCREEN_H - CT_DIRT_Y, CT_COLOR_DIRT);
    lv_obj_t *base = rect(s_ground, CS_CENTER_X - CS_BASE_W / 2, CS_GROUND_SCREEN_Y - city_top,
                          CS_BASE_W, CT_BASE_H, CT_COLOR_BASE);
    lv_obj_set_style_border_side(base, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_border_width(base, 3, 0);
    lv_obj_set_style_border_color(base, lv_color_hex(CT_COLOR_BASE_TOP), 0);

    // 吊着的箱子画在塔后面:掉落的箱子从塔身后方滑出去
    s_active.obj = box_create(s_scr);
    s_active.x = s_active.y = OFFSCREEN;
    for (int i = 0; i < CT_TOWER_POOL; i++) {
        s_tower[i].obj = box_create(s_scr);
        s_tower[i].x = s_tower[i].y = OFFSCREEN;
        s_tower[i].layer = -1;
    }

    s_rope = lv_line_create(s_scr);
    lv_obj_set_style_line_width(s_rope, 2, 0);
    lv_obj_set_style_line_color(s_rope, lv_color_hex(CT_COLOR_ROPE), 0);
    s_hook = rect(s_scr, OFFSCREEN, OFFSCREEN, CT_HOOK_W, CT_HOOK_H, CT_COLOR_HOOK);
    lv_obj_set_style_radius(s_hook, 2, 0);
}

static void build_hud(void) {
    shadow_label(s_scr, s_score, &gb_font_40, CT_COLOR_TEXT, "0", CT_SCORE_Y);
    shadow_label(s_scr, s_height, &gb_font_16, CT_COLOR_TEXT, "", CT_HEIGHT_Y);

    for (int i = 0; i < CS_MAX_LIVES; i++) {
        s_lives[i] = plain(s_scr);
        lv_obj_set_pos(s_lives[i], CT_LIFE_X + i * (CT_LIFE_W + CT_LIFE_GAP), CT_LIFE_Y);
        lv_obj_set_size(s_lives[i], CT_LIFE_W, CT_LIFE_H);
        lv_obj_set_style_radius(s_lives[i], 2, 0);
        lv_obj_set_style_border_width(s_lives[i], 2, 0);
        lv_obj_set_style_bg_color(s_lives[i], lv_color_hex(CT_COLOR_BOX), 0);
    }

    // 电量:数字 + 电池图标,电量不可用时只显示暗色空框
    s_batt_text = label(s_scr, &gb_font_16, CT_COLOR_BATT, "");
    lv_obj_set_width(s_batt_text, CT_BATT_TEXT_W);
    lv_obj_set_style_text_align(s_batt_text, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_pos(s_batt_text, CT_BATT_TEXT_R - CT_BATT_TEXT_W, CT_BATT_TEXT_Y);
    s_batt_frame = plain(s_scr);
    lv_obj_set_pos(s_batt_frame, CT_BATT_X, CT_BATT_Y);
    lv_obj_set_size(s_batt_frame, CT_BATT_W, CT_BATT_H);
    lv_obj_set_style_border_width(s_batt_frame, 2, 0);
    lv_obj_set_style_border_color(s_batt_frame, lv_color_hex(CT_COLOR_BATT), 0);
    lv_obj_set_style_radius(s_batt_frame, 2, 0);
    s_batt_nub = plain(s_scr);
    lv_obj_set_pos(s_batt_nub, CT_BATT_X + CT_BATT_W, CT_BATT_Y + 3);
    lv_obj_set_size(s_batt_nub, CT_BATT_NUB_W, CT_BATT_H - 6);
    lv_obj_set_style_bg_color(s_batt_nub, lv_color_hex(CT_COLOR_BATT), 0);
    lv_obj_set_style_bg_opa(s_batt_nub, LV_OPA_COVER, 0);
    s_batt_fill = plain(s_batt_frame);
    lv_obj_set_pos(s_batt_fill, 1, 1);
    lv_obj_set_size(s_batt_fill, 0, CT_BATT_H - 6);
    lv_obj_set_style_bg_opa(s_batt_fill, LV_OPA_COVER, 0);

    // 落点提示:完美 / 没压住 / 奖励一条命,短暂上浮后消失
    s_popup = plain(s_scr);
    lv_obj_set_size(s_popup, CT_SCREEN_W, 34);
    lv_obj_set_pos(s_popup, 0, CT_POPUP_Y);
    for (int i = 0; i < 2; i++) {
        s_popup_text[i] = center_label(s_popup, &gb_font_24, i ? CT_COLOR_PERFECT : CT_COLOR_SHADOW, "", i ? 0 : 2);
        if (!i) lv_obj_set_x(s_popup_text[i], 2);
    }
    show(s_popup, false);

    s_hint = center_label(s_scr, &gb_font_16, CT_COLOR_HINT, "", CT_HINT_Y);
}

static void build_title(void) {
    s_title = plain(s_scr);
    lv_obj_set_size(s_title, CT_SCREEN_W, CT_SCREEN_H);

    // Logo:深色与橙色两层投影,正文重复绘制 1px 模拟粗体
    static const struct { int dx, dy; uint32_t color; } LOGO[] = {
        { 2, 5, CT_COLOR_SHADOW }, { 2, 3, CT_COLOR_BOX_EDGE },
        { 0, 0, CT_COLOR_TEXT }, { 1, 0, CT_COLOR_TEXT },
    };
    for (size_t i = 0; i < sizeof(LOGO) / sizeof(LOGO[0]); i++) {
        lv_obj_t *l = center_label(s_title, &gb_font_40, LOGO[i].color, TX_LOGO, CT_LOGO_Y + LOGO[i].dy);
        lv_obj_set_x(l, LOGO[i].dx);
    }
    lv_obj_t *en = center_label(s_title, &gb_font_16, CT_COLOR_SHADOW, TX_LOGO_EN, CT_LOGO_EN_Y);
    lv_obj_set_style_text_letter_space(en, 3, 0);

    lv_obj_t *menu = card(s_title, CT_MENU_X, CT_MENU_Y, CT_MENU_W, CT_MENU_H);
    const int row_w = CT_MENU_W - 2 * CT_MENU_PAD - 6;
    s_menu[CF_TITLE_ITEM_START] = menu_row(menu, CT_MENU_PAD, CT_MENU_PAD - 3, row_w, TX_MENU_START);
    s_menu[CF_TITLE_ITEM_SOUND] = menu_row(menu, CT_MENU_PAD, CT_MENU_PAD - 3 + CT_MENU_ROW_H, row_w, TX_MENU_SOUND);
    menu_row_split(&s_menu[CF_TITLE_ITEM_SOUND], "");
    s_menu_best = center_label(menu, &gb_font_16, CT_COLOR_INK_SOFT, "",
                               CT_MENU_PAD - 3 + 2 * CT_MENU_ROW_H + 2);

}

static void build_overlays(void) {
    s_dim = plain(s_scr);
    lv_obj_set_size(s_dim, CT_SCREEN_W, CT_SCREEN_H);
    lv_obj_set_style_bg_color(s_dim, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_dim, LV_OPA_40, 0);

    s_pause = card(s_scr, CT_PAUSE_X, CT_PAUSE_Y, CT_PAUSE_W, CT_PAUSE_H);
    center_label(s_pause, &gb_font_24, CT_COLOR_INK, TX_PAUSED, 6);
    static const char *const PAUSE_TEXT[CF_PAUSE_ITEMS] = { TX_RESUME, TX_RESTART, TX_TO_TITLE };
    for (int i = 0; i < CF_PAUSE_ITEMS; i++) {
        s_pause_rows[i] = menu_row(s_pause, 12, 40 + i * (CT_MENU_ROW_H + 2), CT_PAUSE_W - 30, PAUSE_TEXT[i]);
    }

    // 结算卡片:标题(塔倒了 / 机会用完了),四行统计,新纪录徽章,两个选项
    s_over = card(s_scr, CT_OVER_X, CT_OVER_Y, CT_OVER_W, CT_OVER_H);
    s_over_title = center_label(s_over, &gb_font_24, CT_COLOR_INK, "", 6);
    static const char *const STAT_TEXT[4] = { TX_SCORE, TX_HEIGHT, TX_PERFECTS, TX_BEST };
    for (int i = 0; i < 4; i++) {
        const int y = CT_OVER_STAT_Y + i * CT_OVER_STAT_H;
        lv_obj_t *cap = label(s_over, &gb_font_16, CT_COLOR_INK_SOFT, STAT_TEXT[i]);
        lv_obj_set_pos(cap, 22, y + 4);
        s_over_values[i] = label(s_over, &gb_font_24, CT_COLOR_INK, "0");
        lv_obj_set_width(s_over_values[i], 80);
        lv_obj_set_style_text_align(s_over_values[i], LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_set_pos(s_over_values[i], CT_OVER_W - 6 - 22 - 80, y - 2);
    }
    s_record = label(s_over, &gb_font_16, CT_COLOR_ON_FOCUS, TX_NEW_RECORD);
    lv_obj_set_style_bg_color(s_record, lv_color_hex(CT_COLOR_RECORD), 0);
    lv_obj_set_style_bg_opa(s_record, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_record, 8, 0);
    lv_obj_set_style_pad_hor(s_record, 8, 0);
    lv_obj_align(s_record, LV_ALIGN_TOP_MID, 0, CT_OVER_BADGE_Y);

    static const char *const OVER_TEXT[CF_OVER_ITEMS] = { TX_AGAIN, TX_TO_TITLE };
    for (int i = 0; i < CF_OVER_ITEMS; i++) {
        s_over_rows[i] = menu_row(s_over, 16, CT_OVER_ROWS_Y + i * CT_MENU_ROW_H, CT_OVER_W - 38, OVER_TEXT[i]);
    }
}

// ------------------------------------------------------------------ 刷新

static void refresh_title(void) {
    for (int i = 0; i < CF_TITLE_ITEMS; i++) {
        menu_row_focus(&s_menu[i], s_flow.state == CF_TITLE && s_flow.focus == i);
    }
    lv_label_set_text(s_menu[CF_TITLE_ITEM_SOUND].right, s_flow.settings.sound_on ? TX_SOUND_ON : TX_SOUND_OFF);
    lv_label_set_text_fmt(s_menu_best, "%s %u%s%s %u %s", TX_BEST, (unsigned)s_flow.settings.best_score,
                          TX_SEP, TX_BEST_HEIGHT, (unsigned)s_flow.settings.best_height, TX_LAYER);
}

static void refresh_menus(void) {
    for (int i = 0; i < CF_PAUSE_ITEMS; i++) menu_row_focus(&s_pause_rows[i], s_flow.focus == i);
    for (int i = 0; i < CF_OVER_ITEMS; i++) menu_row_focus(&s_over_rows[i], s_flow.focus == i);
}

static void fill_over_card(bool new_record) {
    lv_label_set_text(s_over_title, s_model.over_by_collapse ? TX_OVER_COLLAPSE : TX_OVER_LIVES);
    const unsigned values[4] = {
        (unsigned)s_model.score, (unsigned)s_model.count, (unsigned)s_model.perfects,
        (unsigned)s_flow.settings.best_score,
    };
    for (int i = 0; i < 4; i++) lv_label_set_text_fmt(s_over_values[i], "%u", values[i]);
    show(s_record, new_record);
}

static void popup(const char *text, popup_kind_t kind) {
    for (int i = 0; i < 2; i++) lv_label_set_text(s_popup_text[i], text);
    lv_obj_set_style_text_color(s_popup_text[1],
                                lv_color_hex(kind == POP_BAD ? CT_COLOR_BAD : CT_COLOR_PERFECT), 0);
    s_popup_at_ms = now_ms();
    s_popup_on = true;
    s_popup_y = OFFSCREEN;
    show(s_popup, true);
}

static void hide_popup(void) {
    s_popup_on = false;
    show(s_popup, false);
}

static void set_flash(int layer) {
    if (s_flash_layer >= 0) {
        lv_obj_set_style_border_color(s_tower[s_flash_layer % CT_TOWER_POOL].obj,
                                      lv_color_hex(CT_COLOR_BOX_EDGE), 0);
    }
    s_flash_layer = layer;
    if (layer < 0) return;
    lv_obj_set_style_border_color(s_tower[layer % CT_TOWER_POOL].obj, lv_color_hex(CT_COLOR_FLASH), 0);
    s_flash_at_ms = now_ms();
}

// 按页面状态切换图层与提示
static void apply_state(void) {
    const cf_state_t st = s_flow.state;
    const bool in_game = st != CF_TITLE;
    show(s_title, st == CF_TITLE);
    for (int i = 0; i < 2; i++) {
        show(s_score[i], in_game);
        show(s_height[i], in_game);
    }
    for (int i = 0; i < CS_MAX_LIVES; i++) show(s_lives[i], in_game);
    show(s_dim, st == CF_PAUSED || st == CF_OVER);
    show(s_pause, st == CF_PAUSED);
    show(s_over, st == CF_OVER);
    if (st == CF_TITLE || st == CF_OVER) hide_popup();
    gb_app_set_playing(st == CF_PLAYING);
    refresh_title();
    refresh_menus();
}

static void place(box_view_t *v, int x, int y) {
    if (x == v->x && y == v->y) return;
    const bool visible = y > -CS_BOX_H && y < CT_SCREEN_H && x > -CS_BOX_W && x < CT_SCREEN_W;
    if (!visible) {
        if (v->x != OFFSCREEN) {
            lv_obj_set_pos(v->obj, OFFSCREEN, OFFSCREEN);
            v->x = v->y = OFFSCREEN;
        }
        return;
    }
    lv_obj_set_pos(v->obj, x, y);
    v->x = x;
    v->y = y;
}

static void render_sky(void) {
    static const uint32_t STOPS[] = CT_SKY_STOPS;
    const int n = (int)(sizeof(STOPS) / sizeof(STOPS[0]));
    const int h = px(s_model.cam);
    const int seg = h / CT_SKY_STEP_PX;
    uint32_t color = STOPS[n - 1];
    if (seg < n - 1) color = lerp_color(STOPS[seg], STOPS[seg + 1], h % CT_SKY_STEP_PX, CT_SKY_STEP_PX);
    // 只在 RGB565 可分辨的变化时重设背景,避免无谓的全屏重绘
    const uint32_t c565 = ((color >> 8) & 0xF800) | ((color >> 5) & 0x07E0) | ((color >> 3) & 0x001F);
    if (c565 == s_sky_shown) return;
    s_sky_shown = c565;
    lv_obj_set_style_bg_color(s_scr, lv_color_hex(color), 0);
}

static void render_world(void) {
    const int cam = px(s_model.cam);

    for (int i = 0; i < CT_CLOUD_COUNT; i++) {
        cloud_view_t *c = &s_clouds[i];
        int y = CS_GROUND_SCREEN_Y - c->world_y + cam;
        if (y > CT_SCREEN_H + 20) {
            c->world_y += CT_CLOUD_COUNT * CT_CLOUD_SPACING;
            y = CS_GROUND_SCREEN_Y - c->world_y + cam;
        }
        if (y != c->y) {
            lv_obj_set_y(c->obj, y);
            c->y = y;
        }
    }

    const int gy = CT_GRASS_Y - 70 + cam;
    if (gy != s_ground_y) {
        lv_obj_set_y(s_ground, gy < CT_SCREEN_H ? gy : OFFSCREEN);
        s_ground_y = gy;
    }
}

static void render_tower(void) {
    const int first = s_model.count > CT_TOWER_POOL ? s_model.count - CT_TOWER_POOL : 0;
    for (int j = 0; j < CT_TOWER_POOL; j++) {
        // 层 i 固定使用 s_tower[i % POOL],加层时其他层的对象不需要换位
        int layer = -1;
        for (int i = first; i < s_model.count; i++) {
            if (i % CT_TOWER_POOL == j) {
                layer = i;
                break;
            }
        }
        box_view_t *v = &s_tower[j];
        if (layer != v->layer && v->layer == s_flash_layer) set_flash(-1);
        v->layer = layer;
        if (layer < 0) {
            place(v, OFFSCREEN, OFFSCREEN);
            continue;
        }
        int dx, dy;
        cs_collapse_offset(&s_model, layer, &dx, &dy);
        const int x = px(cs_layer_x(&s_model, layer)) - CS_BOX_W / 2 + dx;
        const int bottom = px(cs_world_to_screen(&s_model, layer * CS_BOX_H * CS_ONE));
        place(v, x, bottom - CS_BOX_H + dy);
    }
}

static void render_box(void) {
    const bool in_game = s_flow.state != CF_TITLE;
    const bool visible = in_game && cs_box_visible(&s_model);
    const int cx = px(s_model.box_x);
    const int top = px(cs_world_to_screen(&s_model, s_model.box_y)) - CS_BOX_H;
    if (visible) place(&s_active, cx - CS_BOX_W / 2, top);
    else place(&s_active, OFFSCREEN, OFFSCREEN);

    const bool hooked = visible && cs_box_hooked(&s_model);
    const int hx = hooked ? cx - CT_HOOK_W / 2 : OFFSCREEN;
    const int hy = hooked ? top - CT_HOOK_H : OFFSCREEN;
    if (hx == s_hook_x && hy == s_hook_y) return;
    s_hook_x = hx;
    s_hook_y = hy;
    lv_obj_set_pos(s_hook, hx, hy);
    if (!hooked) {
        show(s_rope, false);
        return;
    }
    // 绳子沿吊点 → 吊钩的直线,从屏幕顶边画到吊钩
    const int32_t rise = hy - CS_PIVOT_Y;
    s_rope_pts[0].x = CS_CENTER_X + (cx - CS_CENTER_X) * (0 - CS_PIVOT_Y) / (rise > 0 ? rise : 1);
    s_rope_pts[0].y = 0;
    s_rope_pts[1].x = cx;
    s_rope_pts[1].y = hy;
    lv_line_set_points(s_rope, s_rope_pts, 2);
    show(s_rope, true);
}

static void render_hud(uint32_t now) {
    if (s_model.score != s_score_shown) {
        s_score_shown = s_model.score;
        for (int i = 0; i < 2; i++) lv_label_set_text_fmt(s_score[i], "%u", (unsigned)s_model.score);
    }
    if (s_model.count != s_height_shown) {
        s_height_shown = s_model.count;
        for (int i = 0; i < 2; i++) lv_label_set_text_fmt(s_height[i], "%u %s", (unsigned)s_model.count, TX_LAYER);
    }
    if (s_model.lives != s_lives_shown) {
        s_lives_shown = s_model.lives;
        for (int i = 0; i < CS_MAX_LIVES; i++) {
            const bool full = i < s_model.lives;
            lv_obj_set_style_bg_opa(s_lives[i], full ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
            lv_obj_set_style_border_color(s_lives[i], lv_color_hex(full ? CT_COLOR_BOX_EDGE : CT_COLOR_LIFE), 0);
            lv_obj_set_style_border_opa(s_lives[i], full ? LV_OPA_COVER : LV_OPA_60, 0);
        }
    }

    if (s_popup_on) {
        const uint32_t age = now - s_popup_at_ms;
        if (age >= POPUP_MS) {
            hide_popup();
        } else {
            const int y = CT_POPUP_Y - (int)(age * POPUP_RISE_PX / POPUP_MS);
            if (y != s_popup_y) {
                lv_obj_set_y(s_popup, y);
                s_popup_y = y;
            }
        }
    }
    if (s_flash_layer >= 0 && now - s_flash_at_ms >= FLASH_MS) set_flash(-1);

    // 开局第一块之前在沙土上提示操作;标题页提示菜单操作
    const char *hint = "";
    if (s_model.cam == 0) {
        if (s_flow.state == CF_TITLE) hint = TX_TITLE_HINT;
        else if (s_flow.state == CF_PLAYING && s_model.count == 0) hint = TX_PLAY_HINT;
    }
    if (lv_label_get_text(s_hint) != hint) lv_label_set_text_static(s_hint, hint);
}

static void render(uint32_t now) {
    render_sky();
    render_world();
    render_tower();
    render_box();
    render_hud(now);
}

static void refresh_battery(void) {
    const int soc = bsp_battery_soc();
    if (soc == s_batt_shown) return;
    s_batt_shown = soc;
    const bool ok = soc >= 0;
    const uint32_t color = ok && soc < 20 ? CT_COLOR_BATT_LOW : CT_COLOR_BATT;
    lv_obj_set_style_border_color(s_batt_frame, lv_color_hex(color), 0);
    lv_obj_set_style_bg_color(s_batt_nub, lv_color_hex(color), 0);
    lv_obj_set_style_bg_color(s_batt_fill, lv_color_hex(color), 0);
    lv_obj_set_style_opa(s_batt_frame, ok ? LV_OPA_COVER : LV_OPA_40, 0);
    lv_obj_set_style_opa(s_batt_nub, ok ? LV_OPA_COVER : LV_OPA_40, 0);
    lv_obj_set_size(s_batt_fill, ok ? (CT_BATT_W - 6) * soc / 100 : 0, CT_BATT_H - 6);
    if (ok) lv_label_set_text_fmt(s_batt_text, "%d", soc);
    else lv_label_set_text_static(s_batt_text, "");   // 读不到电量时不显示数字
}

// ------------------------------------------------------------------ 逻辑

static void reset_views(void) {
    for (int i = 0; i < CT_TOWER_POOL; i++) s_tower[i].layer = -1;
    set_flash(-1);
    hide_popup();
}

static void new_round(void) {
    reset_views();
    cs_model_init(&s_model, esp_random());
    s_sim_us = esp_timer_get_time();
}

static void title_scene(void) {
    reset_views();
    cs_model_init_demo(&s_model, esp_random());
    s_sim_us = esp_timer_get_time();
}

static void on_events(uint32_t ev) {
    if (ev & CS_EV_PERFECT) {
        play(cs_sfx_for_perfect(s_model.combo));
        if (s_model.combo >= 2) {
            char text[32];
            snprintf(text, sizeof text, "%s%u", TX_POP_COMBO, (unsigned)s_model.combo);
            popup(text, POP_GOOD);
        } else {
            popup(TX_POP_PERFECT, POP_GOOD);
        }
        set_flash(s_model.count - 1);
    } else if (ev & CS_EV_LAND) {
        play(CS_SFX_LAND);
    }
    if (ev & CS_EV_LIFE_GAIN) {
        play(CS_SFX_LIFE);
        popup(TX_POP_LIFE, POP_GOOD);
    }
    if (ev & CS_EV_MISS) {
        play(CS_SFX_MISS);
        popup(TX_POP_MISS, POP_BAD);
    }
    if (ev & CS_EV_COLLAPSE) {
        play(CS_SFX_COLLAPSE);
        popup(TX_OVER_COLLAPSE, POP_BAD);
    }
    if (ev & CS_EV_OVER) {
        bool new_score;
        const bool changed = cf_flow_on_over(&s_flow, s_model.score, s_model.count, now_ms(), &new_score);
        if (changed) save_settings();
        if (new_score) gb_score_submit(GAME_ID, s_model.score);
        if (new_score) play(CS_SFX_RECORD);
        else if (!s_model.over_by_collapse) play(CS_SFX_OVER);
        ESP_LOGI(TAG, "本局结束: 得分=%u 层数=%u 完美=%u 最长连击=%u %s%s", (unsigned)s_model.score,
                 (unsigned)s_model.count, (unsigned)s_model.perfects, (unsigned)s_model.best_combo,
                 s_model.over_by_collapse ? "(倒塌)" : "(机会用完)", new_score ? " 新纪录" : "");
        fill_over_card(new_score);
        apply_state();
    }
}

static void handle_key(cf_key_t key) {
    const cf_action_t act = cf_flow_key(&s_flow, key, now_ms());
    switch (act) {
    case CF_ACT_NONE:
        return;
    case CF_ACT_FOCUS:
        play(CS_SFX_MENU);
        refresh_title();
        refresh_menus();
        return;
    case CF_ACT_TOGGLE_SOUND:
        save_settings();
        play(CS_SFX_CONFIRM);
        refresh_title();
        return;
    case CF_ACT_START:
        play(CS_SFX_CONFIRM);
        new_round();
        break;
    case CF_ACT_DROP:
        if (cs_model_release(&s_model)) {
            play(CS_SFX_DROP);
            ESP_LOGD(TAG, "松钩: 第 %u 层 x=%d", (unsigned)s_model.count + 1, px(s_model.box_x));
        }
        return;
    case CF_ACT_PAUSE:
        play(CS_SFX_MENU);
        break;
    case CF_ACT_RESUME:
        break;
    case CF_ACT_TO_TITLE:
        play(CS_SFX_MENU);
        title_scene();
        break;
    }
    apply_state();
}

static bool simulating(void) {
    return s_flow.state == CF_TITLE || s_flow.state == CF_PLAYING;
}

// 把模拟时钟推进到 t_us(不超过);卡顿过久时丢弃积压,宁可放慢也不瞬移。
static void advance_to(int64_t t_us) {
    if (t_us - s_sim_us > (int64_t)CS_TICK_US * MAX_CATCHUP) {
        s_sim_us = t_us - (int64_t)CS_TICK_US * MAX_CATCHUP;
        s_stat_lag++;
    }
    while (s_sim_us + CS_TICK_US <= t_us) {
        s_sim_us += CS_TICK_US;
        const uint32_t ev = cs_model_step(&s_model);
        if (ev && s_flow.state == CF_PLAYING) on_events(ev);
        if (!simulating()) return;
    }
}

static void loop_cb(lv_timer_t *timer) {
    (void)timer;
    const int64_t now_us = esp_timer_get_time();
    const uint32_t now = (uint32_t)(now_us / 1000);
    if (s_frozen) {            // 确认框打开期间不推进模拟,也不补帧
        s_sim_us = now_us;
        return;
    }

    // 先把模拟推进到每次按键的按下时刻再处理按键:松钩位置与按下瞬间一致,
    // 不受本帧处理时机的影响。
    key_event_t k;
    while (xQueueReceive(s_keys, &k, 0) == pdTRUE) {
        if (simulating()) advance_to(k.t_us < now_us ? k.t_us : now_us);
        handle_key((cf_key_t)k.key);
    }
    if (simulating()) advance_to(now_us);
    else s_sim_us = now_us;   // 暂停/结算期间模拟时钟跟着走,恢复时不补帧

    render(now);

    s_stat_frames++;
    if ((uint32_t)(now - s_stat_ms) >= STATS_PERIOD_MS) {
        if (s_flow.state == CF_PLAYING) {
            ESP_LOGI(TAG, "游戏循环 %u 次/%u ms, 卡顿截断 %u 次, 层数 %u", (unsigned)s_stat_frames,
                     (unsigned)(now - s_stat_ms), (unsigned)s_stat_lag, (unsigned)s_model.count);
        }
        s_stat_ms = now;
        s_stat_frames = 0;
        s_stat_lag = 0;
    }

    if ((uint32_t)(now - s_batt_ms) >= BATTERY_UI_MS) {
        s_batt_ms = now;
        refresh_battery();
    }
}

// ------------------------------------------------------------------ 游戏盒接口

static lv_timer_t *s_loop;

// 「已绘制」缓存全部回到初始值:否则第二次进入时,与上次相同的值会被当成
// 已经画过而跳过,界面停在空白状态。
static void reset_caches(void) {
    s_ground_y = OFFSCREEN;
    s_hook_x = s_hook_y = OFFSCREEN;
    s_sky_shown = 0xFFFFFFFFu;
    s_batt_shown = -2;
    s_batt_ms = 0;
    s_score_shown = UINT32_MAX;
    s_height_shown = -1;
    s_lives_shown = -1;
    s_popup_on = false;
    s_popup_y = OFFSCREEN;
    s_flash_layer = -1;
    s_frozen = false;
    s_stat_frames = s_stat_lag = 0;
    s_stat_ms = now_ms();
}

static void load_settings(cf_settings_t *st) {
    cf_settings_default(st);
    if (!gb_save_load(GAME_ID, st, sizeof(*st))) cf_settings_default(st);
}

static void crane_enter(void) {
    if (!s_keys) s_keys = xQueueCreate(KEY_QUEUE_DEPTH, sizeof(key_event_t));
    if (s_keys) xQueueReset(s_keys);

    cf_settings_t settings;
    load_settings(&settings);
    cf_flow_init(&s_flow, &settings);   // 内部会校验,非法存档恢复默认
    // 最高分以平台纪录为准(游戏列表显示的就是它)。
    const uint32_t best = gb_score_best(GAME_ID);
    if (best > s_flow.settings.best_score) {
        s_flow.settings.best_score = best > 0xFFFFu ? 0xFFFFu : (uint16_t)best;
    }

    reset_caches();
    init_styles();
    s_scr = lv_obj_create(NULL);
    lv_obj_remove_style_all(s_scr);
    lv_obj_remove_flag(s_scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(s_scr, LV_OPA_COVER, 0);

    build_scene();
    build_hud();
    build_title();
    build_overlays();

    title_scene();
    apply_state();
    render(now_ms());
    refresh_battery();
    lv_screen_load(s_scr);

    lv_mem_monitor_t mon;
    lv_mem_monitor(&mon);
    ESP_LOGI(TAG, "LVGL 内存池: 已用 %u%%, 空闲 %u 字节, 最大空闲块 %u 字节",
             (unsigned)mon.used_pct, (unsigned)mon.free_size, (unsigned)mon.free_biggest_size);

    s_loop = lv_timer_create(loop_cb, LOOP_PERIOD_MS, NULL);
    if (!s_loop) ESP_LOGE(TAG, "游戏循环定时器创建失败");
}

static void crane_exit(void) {
    gb_app_set_playing(false);
    if (s_loop) {
        lv_timer_delete(s_loop);
        s_loop = NULL;
    }
    if (s_scr) {
        lv_obj_delete(s_scr);
        s_scr = NULL;
    }
    // 共享样式在对象删除后才能释放
    lv_style_reset(&s_style_box);
    lv_style_reset(&s_style_tape);
    if (s_keys) xQueueReset(s_keys);
}

static void crane_key(bsp_btn_t btn, bsp_btn_ev_t ev) {
    // 只响应按下瞬间:松钩延迟最低,也避免同一次按键的 CLICK 被下一个页面消费。
    // 按下时刻随按键入队,游戏循环先把模拟推进到这一刻再松钩。
    if (ev != BSP_BTN_PRESS || !s_keys) return;
    key_event_t k = { .t_us = esp_timer_get_time() };
    switch (btn) {
    case BSP_BTN_UP: k.key = CF_KEY_UP; break;
    case BSP_BTN_DOWN: k.key = CF_KEY_DOWN; break;
    case BSP_BTN_OK: k.key = CF_KEY_OK; break;
    default: return;
    }
    (void)xQueueSend(s_keys, &k, 0);
}

static void crane_pause(bool paused) {
    s_frozen = paused;
    s_sim_us = esp_timer_get_time();
    gb_app_set_playing(!paused && s_flow.state == CF_PLAYING);
}

static void crane_icon(lv_obj_t *box) {
    // 三只错开叠起的纸箱(40x40)。
    static const struct { int x, y; } BOXES[] = { { 9, 27 }, { 12, 17 }, { 6, 7 } };
    for (size_t i = 0; i < sizeof(BOXES) / sizeof(BOXES[0]); i++) {
        lv_obj_t *b = gb_ui_rect(box, BOXES[i].x, BOXES[i].y, 24, 10, CT_COLOR_BOX, 2);
        lv_obj_set_style_border_color(b, lv_color_hex(CT_COLOR_BOX_EDGE), 0);
        lv_obj_set_style_border_width(b, 1, 0);
        gb_ui_rect(b, 9, 0, 4, 8, CT_COLOR_TAPE, 0);
    }
}

const gb_game_t GB_GAME_CRANE = {
    .id = GAME_ID,
    .title = TX_GAME_TITLE,
    .tagline = TX_GAME_TAGLINE,
    .accent = 0x4C8BF5,
    .icon = crane_icon,
    .enter = crane_enter,
    .exit = crane_exit,
    .key = crane_key,
    .pause = crane_pause,
};
