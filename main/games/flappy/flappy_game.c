// main/games/flappy/flappy_game.c —— 飞吧小鸟:界面、游戏循环与游戏盒接入。
//
// 移植自独立固件 FlappyBird 仓库的 main/flappy_ui.c。规则(flappy_model)、页面流程
// (flappy_flow)、音效合成(flappy_sfx)与精灵图原样保留;本文件的改动只有接入游戏盒:
//   * 一次性创建的界面改成 enter()/exit():每次进入重建界面、重置所有「已绘制」
//     缓存,退出时删除定时器和屏幕。
//   * 原来的后台服务任务(音效、存档、电量)换成平台的 gb_sfx / gb_score:
//     音效经合成器接口交给 gb_sfx 播放;每局得分都提交给 gb_score(平台只保留
//     最大值,游戏列表显示的是各难度中的最高分);难度、音效开关与三个难度各自的
//     最高分存进本游戏的自定义存档。
//   * 平台的退出确认框弹出时 pause(true) 冻结模拟,关闭后从当前时刻继续。
#include <stdio.h>
#include <stdlib.h>

#include "bsp_battery.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "flappy_assets.h"
#include "flappy_flow.h"
#include "flappy_model.h"
#include "flappy_sfx.h"
#include "flappy_text.h"
#include "flappy_theme.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "gb_app.h"
#include "gb_game.h"
#include "gb_score.h"
#include "gb_sfx.h"
#include "gb_ui.h"
#include "lvgl.h"

static const char *TAG = "flappy";

#define GAME_ID          "flappy"

#define LOOP_PERIOD_MS   16
#define MAX_CATCHUP      5      // 一帧最多补 5 个节拍,卡顿时宁可放慢也不瞬移
#define KEY_QUEUE_DEPTH  8
#define FLASH_FRAMES     3      // 撞击白闪持续的帧数
#define BATTERY_UI_MS    5000   // 读电量走 I2C,每 5 秒一次
#define CLOUD_COUNT      3
#define STATS_PERIOD_MS  10000  // 游戏中每 10 秒打印一次帧率,便于上机核对流畅度

typedef struct {
    lv_obj_t *box;          // 管道整体(水平移动)
    lv_obj_t *top_body;
    lv_obj_t *top_cap;
    lv_obj_t *bottom_cap;
    lv_obj_t *bottom_body;
    int x;                  // 已应用的屏幕坐标,避免重复失效
    int gap_y;
} pipe_view_t;

typedef struct {
    lv_obj_t *img;
    int32_t x_q8;
    int32_t speed_q8;
    int x;
} cloud_view_t;

typedef struct {
    lv_obj_t *row;
    lv_obj_t *left;
    lv_obj_t *right;
} menu_row_t;

static QueueHandle_t s_keys;
static ff_flow_t s_flow;
static fb_model_t s_model;
static int64_t s_last_us;
static int64_t s_acc_us;
static bool s_frozen;                // 平台退出确认框打开期间冻结
static uint32_t s_stat_frames, s_stat_lag, s_stat_ms;

static lv_obj_t *s_scr;
static cloud_view_t s_clouds[CLOUD_COUNT];
static pipe_view_t s_pipes[FB_PIPE_COUNT];
static lv_obj_t *s_ground_band;
static lv_obj_t *s_bird;
static lv_obj_t *s_hint;
static lv_obj_t *s_flash;
static int s_flash_frames;

static lv_obj_t *s_score[2];        // [0]=投影 [1]=正文
static lv_obj_t *s_batt_text;
static lv_obj_t *s_batt_frame;
static lv_obj_t *s_batt_nub;
static lv_obj_t *s_batt_fill;
static int s_batt_shown = -2;
static uint32_t s_batt_ms;

static lv_obj_t *s_title;
static menu_row_t s_menu[FF_TITLE_ITEMS];
static lv_obj_t *s_menu_best;

static lv_obj_t *s_ready;

static lv_obj_t *s_dim;
static lv_obj_t *s_pause;
static menu_row_t s_pause_rows[FF_PAUSE_ITEMS];

static lv_obj_t *s_over;
static menu_row_t s_over_rows[FF_OVER_ITEMS];
static lv_obj_t *s_over_score;
static lv_obj_t *s_over_best;
static lv_obj_t *s_medal;
static lv_obj_t *s_medal_label;
static lv_obj_t *s_record;

static int s_bird_x = -1000, s_bird_y = -1000;
static const lv_image_dsc_t *s_bird_src;
static int s_ground_x = 1;
static uint32_t s_score_shown = UINT32_MAX;

// ------------------------------------------------------------------ 小工具

static lv_obj_t *plain(lv_obj_t *parent) {
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    return o;
}

static lv_obj_t *image(lv_obj_t *parent, const lv_image_dsc_t *src, int x, int y) {
    lv_obj_t *img = lv_image_create(parent);
    lv_image_set_src(img, src);
    lv_obj_set_pos(img, x, y);
    return img;
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

static lv_obj_t *card(lv_obj_t *parent, int x, int y, int w, int h) {
    lv_obj_t *c = plain(parent);
    lv_obj_set_pos(c, x, y);
    lv_obj_set_size(c, w, h);
    lv_obj_set_style_bg_color(c, lv_color_hex(FT_COLOR_CARD), 0);
    lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(c, lv_color_hex(FT_COLOR_CARD_EDGE), 0);
    lv_obj_set_style_border_width(c, 3, 0);
    lv_obj_set_style_radius(c, 12, 0);
    return c;
}

static menu_row_t menu_row(lv_obj_t *parent, int x, int y, int w, const char *left) {
    menu_row_t r;
    r.row = plain(parent);
    lv_obj_set_pos(r.row, x, y);
    lv_obj_set_size(r.row, w, FT_MENU_ROW_H);
    lv_obj_set_style_radius(r.row, FT_MENU_ROW_H / 2, 0);
    lv_obj_set_style_bg_color(r.row, lv_color_hex(FT_COLOR_FOCUS), 0);
    r.left = label(r.row, &gb_font_16, FT_COLOR_INK, left);
    r.right = NULL;
    lv_obj_align(r.left, LV_ALIGN_CENTER, 0, 0);
    return r;
}

static void menu_row_split(menu_row_t *r, const char *right) {
    lv_obj_align(r->left, LV_ALIGN_LEFT_MID, 16, 0);
    r->right = label(r->row, &gb_font_16, FT_COLOR_INK, right);
    lv_obj_align(r->right, LV_ALIGN_RIGHT_MID, -16, 0);
}

static void menu_row_focus(menu_row_t *r, bool focused) {
    const uint32_t color = focused ? FT_COLOR_ON_FOCUS : FT_COLOR_INK;
    lv_obj_set_style_bg_opa(r->row, focused ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    lv_obj_set_style_text_color(r->left, lv_color_hex(color), 0);
    if (r->right) lv_obj_set_style_text_color(r->right, lv_color_hex(color), 0);
}

static void show(lv_obj_t *o, bool visible) {
    if (visible) lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
}

static const char *difficulty_name(uint8_t d) {
    static const char *const NAMES[FB_DIFF_COUNT] = { FT_DIFF_EASY, FT_DIFF_NORMAL, FT_DIFF_HARD };
    return d < FB_DIFF_COUNT ? NAMES[d] : FT_DIFF_NORMAL;
}

static void synth_start(void *player, uintptr_t sound) {
    fb_sfx_start(player, (fb_sfx_t)sound);
}

static size_t synth_render(void *player, int16_t *out, size_t n) {
    return fb_sfx_render(player, out, n);
}

static bool synth_is_feedback(uintptr_t sound) {
    return fb_sfx_is_feedback((fb_sfx_t)sound);
}

_Static_assert(sizeof(fb_sfx_player_t) <= GB_SYNTH_PLAYER_MAX, "fb_sfx_player_t too large");
_Static_assert(FB_SFX_RATE == GB_SFX_RATE, "flappy sfx sample rate must match the platform");

static const gb_synth_t FLAPPY_SYNTH = {
    .start = synth_start, .render = synth_render, .is_feedback = synth_is_feedback,
};

static void play(fb_sfx_t id) {
    if (s_flow.settings.sound_on) gb_sfx_play_synth(&FLAPPY_SYNTH, (uintptr_t)id);
}

static void save_settings(void) {
    (void)gb_save_store(GAME_ID, &s_flow.settings, sizeof(s_flow.settings));
}

// ------------------------------------------------------------------ 场景

static void build_scene(void) {
    image(s_scr, &flappy_img_hills, 0, FT_HILLS_Y);

    static const struct { const lv_image_dsc_t *src; int x, y, speed_q8; } CLOUDS[CLOUD_COUNT] = {
        { &flappy_img_cloud_big, 18, 48, 56 },
        { &flappy_img_cloud_small, 150, 104, 80 },
        { &flappy_img_cloud_big, 196, 176, 64 },
    };
    for (int i = 0; i < CLOUD_COUNT; i++) {
        s_clouds[i].img = image(s_scr, CLOUDS[i].src, CLOUDS[i].x, CLOUDS[i].y);
        s_clouds[i].x_q8 = CLOUDS[i].x * FB_ONE;
        s_clouds[i].speed_q8 = CLOUDS[i].speed_q8;
        s_clouds[i].x = CLOUDS[i].x;
    }

    for (int i = 0; i < FB_PIPE_COUNT; i++) {
        pipe_view_t *p = &s_pipes[i];
        p->box = plain(s_scr);
        lv_obj_set_size(p->box, FT_PIPE_CAP_W, FT_GROUND_Y);
        lv_obj_set_pos(p->box, FT_SCREEN_W, 0);
        p->x = FT_SCREEN_W;
        p->gap_y = -1;
        p->top_body = image(p->box, &flappy_img_pipe_body, FT_PIPE_CAP_INSET, 0);
        p->bottom_body = image(p->box, &flappy_img_pipe_body, FT_PIPE_CAP_INSET, 0);
        lv_image_set_inner_align(p->top_body, LV_IMAGE_ALIGN_TILE);
        lv_image_set_inner_align(p->bottom_body, LV_IMAGE_ALIGN_TILE);
        p->top_cap = image(p->box, &flappy_img_pipe_cap, 0, 0);
        p->bottom_cap = image(p->box, &flappy_img_pipe_cap, 0, 0);
    }

    // 地面:上部草皮带循环平移,下部沙土静止
    lv_obj_t *ground = plain(s_scr);
    lv_obj_set_pos(ground, 0, FT_GROUND_Y);
    lv_obj_set_size(ground, FT_SCREEN_W, FT_GROUND_BAND_H);
    s_ground_band = image(ground, &flappy_img_ground_band, 0, 0);
    image(s_scr, &flappy_img_dirt, 0, FT_GROUND_Y + FT_GROUND_BAND_H);

    s_bird = lv_image_create(s_scr);

    s_hint = center_label(s_scr, &gb_font_16, FT_COLOR_HINT, "", FT_HINT_Y);
}

static void build_hud(void) {
    // 分数:深色投影 + 白色正文
    for (int i = 0; i < 2; i++) {
        s_score[i] = center_label(s_scr, &gb_font_40, i ? FT_COLOR_LOGO : FT_COLOR_SHADOW,
                                  "0", FT_SCORE_Y + (i ? 0 : 3));
        if (!i) lv_obj_set_x(s_score[i], 2);
    }

    // 电量:数字 + 电池图标,电量不可用时只显示暗色空框
    s_batt_text = label(s_scr, &gb_font_16, FT_COLOR_BATT, "");
    lv_obj_set_width(s_batt_text, FT_BATT_TEXT_W);
    lv_obj_set_style_text_align(s_batt_text, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_pos(s_batt_text, FT_BATT_TEXT_R - FT_BATT_TEXT_W, FT_BATT_TEXT_Y);
    s_batt_frame = plain(s_scr);
    lv_obj_set_pos(s_batt_frame, FT_BATT_X, FT_BATT_Y);
    lv_obj_set_size(s_batt_frame, FT_BATT_W, FT_BATT_H);
    lv_obj_set_style_border_width(s_batt_frame, 2, 0);
    lv_obj_set_style_border_color(s_batt_frame, lv_color_hex(FT_COLOR_BATT), 0);
    lv_obj_set_style_radius(s_batt_frame, 2, 0);
    s_batt_nub = plain(s_scr);
    lv_obj_set_pos(s_batt_nub, FT_BATT_X + FT_BATT_W, FT_BATT_Y + 3);
    lv_obj_set_size(s_batt_nub, FT_BATT_NUB_W, FT_BATT_H - 6);
    lv_obj_set_style_bg_color(s_batt_nub, lv_color_hex(FT_COLOR_BATT), 0);
    lv_obj_set_style_bg_opa(s_batt_nub, LV_OPA_COVER, 0);
    s_batt_fill = plain(s_batt_frame);
    lv_obj_set_pos(s_batt_fill, 3, 3);
    lv_obj_set_size(s_batt_fill, 0, FT_BATT_H - 6);
    lv_obj_set_style_bg_opa(s_batt_fill, LV_OPA_COVER, 0);
}

static void build_title(void) {
    s_title = plain(s_scr);
    lv_obj_set_size(s_title, FT_SCREEN_W, FT_GROUND_Y);

    // Logo:深色与橙色两层投影,正文重复绘制 1px 模拟粗体
    static const struct { int dx, dy; uint32_t color; } LOGO[] = {
        { 2, 5, FT_COLOR_SHADOW }, { 2, 3, FT_COLOR_LOGO_DEEP },
        { 0, 0, FT_COLOR_LOGO }, { 1, 0, FT_COLOR_LOGO },
    };
    for (size_t i = 0; i < sizeof(LOGO) / sizeof(LOGO[0]); i++) {
        lv_obj_t *l = center_label(s_title, &gb_font_40, LOGO[i].color, FT_LOGO, FT_LOGO_Y + LOGO[i].dy);
        lv_obj_set_x(l, LOGO[i].dx);
    }
    lv_obj_t *en = center_label(s_title, &gb_font_16, FT_COLOR_SHADOW, FT_LOGO_EN, FT_LOGO_EN_Y);
    lv_obj_set_style_text_letter_space(en, 3, 0);

    lv_obj_t *menu = card(s_title, FT_MENU_X, FT_MENU_Y, FT_MENU_W, FT_MENU_H);
    const int row_w = FT_MENU_W - 2 * FT_MENU_PAD - 6;
    const int row_x = FT_MENU_PAD;
    s_menu[FF_TITLE_ITEM_START] = menu_row(menu, row_x, FT_MENU_PAD - 3, row_w, FT_MENU_START);
    s_menu[FF_TITLE_ITEM_DIFF] = menu_row(menu, row_x, FT_MENU_PAD - 3 + FT_MENU_ROW_H, row_w, FT_MENU_DIFF);
    menu_row_split(&s_menu[FF_TITLE_ITEM_DIFF], "");
    s_menu[FF_TITLE_ITEM_SOUND] = menu_row(menu, row_x, FT_MENU_PAD - 3 + 2 * FT_MENU_ROW_H, row_w, FT_MENU_SOUND);
    menu_row_split(&s_menu[FF_TITLE_ITEM_SOUND], "");
    s_menu_best = center_label(menu, &gb_font_16, FT_COLOR_INK_SOFT, "",
                               FT_MENU_PAD - 3 + 3 * FT_MENU_ROW_H + 1);

}

static void build_ready(void) {
    s_ready = plain(s_scr);
    lv_obj_set_size(s_ready, FT_SCREEN_W, FT_GROUND_Y);
    lv_obj_t *t0 = center_label(s_ready, &gb_font_24, FT_COLOR_SHADOW, FT_READY, FT_READY_TITLE_Y + 2);
    lv_obj_set_x(t0, 2);
    center_label(s_ready, &gb_font_24, FT_COLOR_LOGO, FT_READY, FT_READY_TITLE_Y);
    lv_obj_t *h0 = center_label(s_ready, &gb_font_16, FT_COLOR_SHADOW, FT_READY_HINT, FT_READY_HINT_Y + 1);
    lv_obj_set_x(h0, 1);
    center_label(s_ready, &gb_font_16, FT_COLOR_LOGO, FT_READY_HINT, FT_READY_HINT_Y);
}

static void build_overlays(void) {
    s_dim = plain(s_scr);
    lv_obj_set_size(s_dim, FT_SCREEN_W, FT_SCREEN_H);
    lv_obj_set_style_bg_color(s_dim, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_dim, LV_OPA_30, 0);

    // 暂停卡片
    s_pause = card(s_scr, FT_PAUSE_X, FT_PAUSE_Y, FT_PAUSE_W, FT_PAUSE_H);
    center_label(s_pause, &gb_font_24, FT_COLOR_INK, FT_PAUSED, 6);
    static const char *const PAUSE_TEXT[FF_PAUSE_ITEMS] = { FT_RESUME, FT_RESTART, FT_TO_TITLE };
    for (int i = 0; i < FF_PAUSE_ITEMS; i++) {
        s_pause_rows[i] = menu_row(s_pause, 12, 40 + i * (FT_MENU_ROW_H + 2), FT_PAUSE_W - 30, PAUSE_TEXT[i]);
    }

    // 结算卡片:左侧奖牌,右侧得分与最高分,底部两个选项
    s_over = card(s_scr, FT_OVER_X, FT_OVER_Y, FT_OVER_W, FT_OVER_H);
    center_label(s_over, &gb_font_24, FT_COLOR_INK, FT_OVER, 6);

    s_medal = plain(s_over);
    lv_obj_set_pos(s_medal, 20, 40);
    lv_obj_set_size(s_medal, FT_MEDAL_D, FT_MEDAL_D);
    lv_obj_set_style_radius(s_medal, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(s_medal, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_medal, 3, 0);
    lv_obj_set_style_border_color(s_medal, lv_color_hex(FT_COLOR_CARD_EDGE), 0);
    lv_obj_t *shine = plain(s_medal);       // 内圈高光,让奖牌有立体感
    lv_obj_set_size(shine, FT_MEDAL_D - 18, FT_MEDAL_D - 18);
    lv_obj_center(shine);
    lv_obj_set_style_radius(shine, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(shine, 2, 0);
    lv_obj_set_style_border_color(shine, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_opa(shine, LV_OPA_60, 0);
    s_medal_label = label(s_over, &gb_font_16, FT_COLOR_INK, "");
    lv_obj_set_width(s_medal_label, FT_MEDAL_D + 24);
    lv_obj_set_style_text_align(s_medal_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(s_medal_label, 8, 90);
    s_record = label(s_over, &gb_font_16, FT_COLOR_ON_FOCUS, FT_NEW_RECORD);
    lv_obj_set_style_bg_color(s_record, lv_color_hex(FT_COLOR_RECORD), 0);
    lv_obj_set_style_bg_opa(s_record, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_record, 8, 0);
    lv_obj_set_style_pad_hor(s_record, 6, 0);
    lv_obj_set_width(s_record, LV_SIZE_CONTENT);
    lv_obj_align(s_record, LV_ALIGN_TOP_LEFT, 6, 112);

    const int col = 100;
    lv_obj_t *score_cap = label(s_over, &gb_font_16, FT_COLOR_INK_SOFT, FT_SCORE);
    lv_obj_set_pos(score_cap, col, 42);
    s_over_score = label(s_over, &gb_font_24, FT_COLOR_INK, "0");
    lv_obj_set_pos(s_over_score, col, 60);
    lv_obj_t *best_cap = label(s_over, &gb_font_16, FT_COLOR_INK_SOFT, FT_BEST);
    lv_obj_set_pos(best_cap, col, 90);
    s_over_best = label(s_over, &gb_font_24, FT_COLOR_INK, "0");
    lv_obj_set_pos(s_over_best, col, 108);

    static const char *const OVER_TEXT[FF_OVER_ITEMS] = { FT_AGAIN, FT_TO_TITLE };
    for (int i = 0; i < FF_OVER_ITEMS; i++) {
        s_over_rows[i] = menu_row(s_over, 16, 140 + i * FT_MENU_ROW_H, FT_OVER_W - 38, OVER_TEXT[i]);
    }

    // 撞击白闪,覆盖全屏
    s_flash = plain(s_scr);
    lv_obj_set_size(s_flash, FT_SCREEN_W, FT_SCREEN_H);
    lv_obj_set_style_bg_color(s_flash, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(s_flash, LV_OPA_60, 0);
}

// ------------------------------------------------------------------ 刷新

static void refresh_title(void) {
    for (int i = 0; i < FF_TITLE_ITEMS; i++) menu_row_focus(&s_menu[i], s_flow.state == FF_TITLE && s_flow.focus == i);
    lv_label_set_text_fmt(s_menu[FF_TITLE_ITEM_DIFF].right, "< %s >", difficulty_name(s_flow.settings.difficulty));
    lv_label_set_text(s_menu[FF_TITLE_ITEM_SOUND].right, s_flow.settings.sound_on ? FT_SOUND_ON : FT_SOUND_OFF);
    lv_label_set_text_fmt(s_menu_best, "%s %s %u", difficulty_name(s_flow.settings.difficulty), FT_BEST,
                          (unsigned)ff_flow_best(&s_flow));
}

static void refresh_menus(void) {
    for (int i = 0; i < FF_PAUSE_ITEMS; i++) menu_row_focus(&s_pause_rows[i], s_flow.focus == i);
    for (int i = 0; i < FF_OVER_ITEMS; i++) menu_row_focus(&s_over_rows[i], s_flow.focus == i);
}

static void refresh_score(void) {
    if (s_model.score == s_score_shown) return;
    s_score_shown = s_model.score;
    for (int i = 0; i < 2; i++) lv_label_set_text_fmt(s_score[i], "%u", (unsigned)s_model.score);
}

static void fill_over_card(bool new_record) {
    static const uint32_t MEDAL_COLOR[] = {
        FT_COLOR_NO_MEDAL, FT_COLOR_BRONZE, FT_COLOR_SILVER, FT_COLOR_GOLD, FT_COLOR_PLATINUM,
    };
    static const char *const MEDAL_TEXT[] = {
        FT_MEDAL_NONE, FT_MEDAL_BRONZE, FT_MEDAL_SILVER, FT_MEDAL_GOLD, FT_MEDAL_PLATINUM,
    };
    const fb_medal_t medal = fb_medal_for(s_model.score);
    lv_obj_set_style_bg_color(s_medal, lv_color_hex(MEDAL_COLOR[medal]), 0);
    lv_label_set_text(s_medal_label, MEDAL_TEXT[medal]);
    lv_label_set_text_fmt(s_over_score, "%u", (unsigned)s_model.score);
    lv_label_set_text_fmt(s_over_best, "%u", (unsigned)ff_flow_best(&s_flow));
    show(s_record, new_record);
}

// 按页面状态切换图层与地面提示
static void apply_state(void) {
    const ff_state_t st = s_flow.state;
    show(s_title, st == FF_TITLE);
    show(s_ready, st == FF_READY);
    show(s_score[0], st == FF_PLAYING || st == FF_DYING || st == FF_PAUSED);
    show(s_score[1], st == FF_PLAYING || st == FF_DYING || st == FF_PAUSED);
    show(s_dim, st == FF_PAUSED || st == FF_OVER);
    show(s_pause, st == FF_PAUSED);
    show(s_over, st == FF_OVER);
    for (int i = 0; i < FB_PIPE_COUNT; i++) show(s_pipes[i].box, st != FF_TITLE);

    const char *hint = "";
    if (st == FF_TITLE || st == FF_PAUSED || st == FF_OVER) hint = FT_TITLE_HINT;
    else if (st == FF_READY) hint = FT_READY_BACK;
    else if (st == FF_PLAYING) hint = FT_PLAY_HINT;
    if (st != FF_DYING) lv_label_set_text_static(s_hint, hint);
    gb_app_set_playing(st == FF_PLAYING || st == FF_DYING);

    refresh_title();
    refresh_menus();
}

static void place_pipe(pipe_view_t *v, const fb_pipe_t *p) {
    const int x = (int)(p->x_q8 >> FB_Q) - FT_PIPE_CAP_INSET;
    if (x != v->x) {
        lv_obj_set_x(v->box, x);
        v->x = x;
    }
    if (p->gap_y == v->gap_y) return;
    v->gap_y = p->gap_y;
    const int top = fb_pipe_gap_top(&s_model, p);
    const int bottom = fb_pipe_gap_bottom(&s_model, p);
    lv_obj_set_height(v->top_body, top - FT_PIPE_CAP_H > 0 ? top - FT_PIPE_CAP_H : 0);
    lv_obj_set_y(v->top_cap, top - FT_PIPE_CAP_H);
    lv_obj_set_y(v->bottom_cap, bottom);
    lv_obj_set_y(v->bottom_body, bottom + FT_PIPE_CAP_H);
    const int body_h = FT_GROUND_Y - bottom - FT_PIPE_CAP_H;
    lv_obj_set_height(v->bottom_body, body_h > 0 ? body_h : 0);
}

static int nearest_angle_index(int deg) {
    int best = 0;
    for (int i = 1; i < FLAPPY_BIRD_ANGLES; i++) {
        if (abs(flappy_bird_angles[i] - deg) < abs(flappy_bird_angles[best] - deg)) best = i;
    }
    return best;
}

static void render(void) {
    // 小鸟:标题页显示在中央,游戏中固定在 FB_BIRD_X
    int cx = FB_BIRD_X;
    int cy = (int)(s_model.bird_y_q8 >> FB_Q);
    int angle = 0;
    if (s_flow.state == FF_TITLE) {
        cx = FT_TITLE_BIRD_X;
        cy = FT_TITLE_BIRD_Y + cy - FB_BIRD_START_Y;
    } else if (s_model.state != FB_STATE_READY) {
        angle = fb_bird_angle_deg(s_model.vel_q8);
    }
    const lv_image_dsc_t *src = flappy_img_bird[nearest_angle_index(angle)][fb_bird_wing_frame(&s_model)];
    if (src != s_bird_src) {
        lv_image_set_src(s_bird, src);
        s_bird_src = src;
    }
    if (cx != s_bird_x || cy != s_bird_y) {
        lv_obj_set_pos(s_bird, cx - FT_BIRD_SPRITE / 2, cy - FT_BIRD_SPRITE / 2);
        s_bird_x = cx;
        s_bird_y = cy;
    }

    for (int i = 0; i < FB_PIPE_COUNT; i++) place_pipe(&s_pipes[i], &s_model.pipes[i]);

    const int gx = -(int)((s_model.ground_q8 >> FB_Q) % FT_GROUND_PERIOD);
    if (gx != s_ground_x) {
        lv_obj_set_x(s_ground_band, gx);
        s_ground_x = gx;
    }

    for (int i = 0; i < CLOUD_COUNT; i++) {
        const int x = (int)(s_clouds[i].x_q8 >> FB_Q);
        if (x != s_clouds[i].x) {
            lv_obj_set_x(s_clouds[i].img, x);
            s_clouds[i].x = x;
        }
    }

    refresh_score();

    if (s_flash_frames > 0 && --s_flash_frames == 0) show(s_flash, false);
}

static void refresh_battery(void) {
    const int soc = bsp_battery_soc();
    if (soc == s_batt_shown) return;
    s_batt_shown = soc;
    const bool ok = soc >= 0;
    const uint32_t color = ok && soc < 20 ? FT_COLOR_BATT_LOW : FT_COLOR_BATT;
    lv_obj_set_style_border_color(s_batt_frame, lv_color_hex(color), 0);
    lv_obj_set_style_bg_color(s_batt_nub, lv_color_hex(color), 0);
    lv_obj_set_style_bg_color(s_batt_fill, lv_color_hex(color), 0);
    lv_obj_set_style_opa(s_batt_frame, ok ? LV_OPA_COVER : LV_OPA_40, 0);
    lv_obj_set_style_opa(s_batt_nub, ok ? LV_OPA_COVER : LV_OPA_40, 0);
    lv_obj_set_size(s_batt_fill, ok ? (FT_BATT_W - 6) * soc / 100 : 0, FT_BATT_H - 6);
    if (ok) lv_label_set_text_fmt(s_batt_text, "%d", soc);
    else lv_label_set_text_static(s_batt_text, "");   // 读不到电量时不显示数字
}

// ------------------------------------------------------------------ 逻辑

static void new_round(void) {
    fb_model_init(&s_model, (fb_difficulty_t)s_flow.settings.difficulty, esp_random());
    s_acc_us = 0;
}

static void step_clouds(void) {
    for (int i = 0; i < CLOUD_COUNT; i++) {
        cloud_view_t *c = &s_clouds[i];
        c->x_q8 -= c->speed_q8;
        const int w = (int)lv_obj_get_width(c->img);
        if ((c->x_q8 >> FB_Q) < -w) c->x_q8 = (FT_SCREEN_W + 8) * FB_ONE;
    }
}

static void on_events(uint32_t ev, uint32_t now_ms) {
    if (ev & FB_EV_SCORE) play(FB_SFX_SCORE);
    if (ev & FB_EV_HIT) {
        play((ev & FB_EV_LAND) ? FB_SFX_HIT : FB_SFX_DIE);
        ff_flow_on_hit(&s_flow);
        show(s_flash, true);
        s_flash_frames = FLASH_FRAMES;
    }
    if (ev & FB_EV_LAND) {
        const bool record = ff_flow_on_over(&s_flow, s_model.score, now_ms);
        // 平台只保留各难度中的最大值(游戏列表显示它);本难度纪录存进自定义存档。
        (void)gb_score_submit(GAME_ID, s_model.score);
        if (record) {
            save_settings();
            play(FB_SFX_RECORD);
        }
        ESP_LOGI(TAG, "本局结束: 难度=%u 得分=%u 最高=%u%s", s_flow.settings.difficulty,
                 (unsigned)s_model.score, (unsigned)ff_flow_best(&s_flow), record ? " (新纪录)" : "");
        fill_over_card(record);
        apply_state();
    }
}

static void handle_key(ff_key_t key, uint32_t now_ms) {
    const ff_action_t act = ff_flow_key(&s_flow, key, now_ms);
    switch (act) {
    case FF_ACT_NONE:
        return;
    case FF_ACT_FOCUS:
        play(FB_SFX_MENU);
        refresh_title();
        refresh_menus();
        return;
    case FF_ACT_CYCLE_DIFF:
        new_round();     // 标题页背景随难度重置
        /* fall through */
    case FF_ACT_TOGGLE_SOUND:
        save_settings();
        play(FB_SFX_CONFIRM);
        refresh_title();
        return;
    case FF_ACT_START:
        play(FB_SFX_CONFIRM);
        new_round();
        break;
    case FF_ACT_FLAP:
        if (fb_model_flap(&s_model)) play(FB_SFX_FLAP);
        break;
    case FF_ACT_PAUSE:
        play(FB_SFX_MENU);
        break;
    case FF_ACT_RESUME:
        s_acc_us = 0;
        s_last_us = esp_timer_get_time();
        break;
    case FF_ACT_TO_TITLE:
        play(FB_SFX_MENU);
        new_round();
        break;
    }
    apply_state();
}

static bool simulating(void) {
    const ff_state_t st = s_flow.state;
    return st == FF_TITLE || st == FF_READY || st == FF_PLAYING || st == FF_DYING;
}

static void loop_cb(lv_timer_t *timer) {
    (void)timer;
    const int64_t now_us = esp_timer_get_time();
    const uint32_t now_ms = (uint32_t)(now_us / 1000);
    if (s_frozen) {            // 确认框打开期间不推进模拟,恢复时也不补帧
        s_last_us = now_us;
        return;
    }

    uint8_t key;
    while (xQueueReceive(s_keys, &key, 0) == pdTRUE) handle_key((ff_key_t)key, now_ms);

    if (simulating()) {
        s_acc_us += now_us - s_last_us;
        if (s_acc_us > (int64_t)FB_TICK_US * MAX_CATCHUP) {
            s_acc_us = (int64_t)FB_TICK_US * MAX_CATCHUP;
            s_stat_lag++;
        }
        while (s_acc_us >= FB_TICK_US) {
            s_acc_us -= FB_TICK_US;
            const ff_state_t st = s_flow.state;
            if (st != FF_DYING) step_clouds();
            const uint32_t ev = fb_model_step(&s_model);
            if (ev) on_events(ev, now_ms);
            if (!simulating()) break;
        }
    }
    s_last_us = now_us;

    render();

    s_stat_frames++;
    if ((uint32_t)(now_ms - s_stat_ms) >= STATS_PERIOD_MS) {
        if (s_flow.state == FF_PLAYING) {
            ESP_LOGI(TAG, "游戏循环 %u 次/%u ms, 卡顿截断 %u 次, 得分 %u",
                     (unsigned)s_stat_frames, (unsigned)(now_ms - s_stat_ms),
                     (unsigned)s_stat_lag, (unsigned)s_model.score);
        }
        s_stat_ms = now_ms;
        s_stat_frames = 0;
        s_stat_lag = 0;
    }

    if ((uint32_t)(now_ms - s_batt_ms) >= BATTERY_UI_MS) {
        s_batt_ms = now_ms;
        refresh_battery();
    }
}

// ------------------------------------------------------------------ 游戏盒接口

static lv_timer_t *s_loop;

// 「已绘制」缓存全部回到初始值:否则第二次进入时,与上次相同的值会被当成
// 已经画过而跳过,界面停在空白状态。
static void reset_caches(void) {
    s_batt_shown = -2;
    s_batt_ms = 0;
    s_bird_x = s_bird_y = -1000;
    s_bird_src = NULL;
    s_ground_x = 1;
    s_score_shown = UINT32_MAX;
    s_flash_frames = 0;
    s_frozen = false;
    s_acc_us = 0;
    s_stat_frames = s_stat_lag = 0;
    s_stat_ms = (uint32_t)(esp_timer_get_time() / 1000);
}

static void flappy_enter(void) {
    if (!s_keys) s_keys = xQueueCreate(KEY_QUEUE_DEPTH, sizeof(uint8_t));
    if (s_keys) xQueueReset(s_keys);

    ff_settings_t settings;
    ff_settings_default(&settings);
    if (!gb_save_load(GAME_ID, &settings, sizeof(settings))) ff_settings_default(&settings);
    ff_flow_init(&s_flow, &settings);   // 内部会校验,非法存档恢复默认

    reset_caches();
    s_scr = lv_obj_create(NULL);
    lv_obj_remove_style_all(s_scr);
    lv_obj_remove_flag(s_scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(s_scr, lv_color_hex(FT_COLOR_SKY), 0);
    lv_obj_set_style_bg_opa(s_scr, LV_OPA_COVER, 0);

    build_scene();
    build_hud();
    build_title();
    build_ready();
    build_overlays();
    show(s_flash, false);

    new_round();
    apply_state();
    render();
    refresh_battery();
    lv_screen_load(s_scr);

    lv_mem_monitor_t mon;
    lv_mem_monitor(&mon);
    ESP_LOGI(TAG, "LVGL 内存池: 已用 %u%%, 空闲 %u 字节, 最大空闲块 %u 字节",
             (unsigned)mon.used_pct, (unsigned)mon.free_size, (unsigned)mon.free_biggest_size);

    s_last_us = esp_timer_get_time();
    s_loop = lv_timer_create(loop_cb, LOOP_PERIOD_MS, NULL);
    if (!s_loop) ESP_LOGE(TAG, "游戏循环定时器创建失败");
}

static void flappy_exit(void) {
    gb_app_set_playing(false);
    if (s_loop) {
        lv_timer_delete(s_loop);
        s_loop = NULL;
    }
    if (s_scr) {
        lv_obj_delete(s_scr);
        s_scr = NULL;
    }
    if (s_keys) xQueueReset(s_keys);
}

static void flappy_key(bsp_btn_t btn, bsp_btn_ev_t ev) {
    // 只响应按下瞬间:扇翅最低延迟,也避免同一次按键的 CLICK 被下一个页面消费。
    if (ev != BSP_BTN_PRESS || !s_keys) return;
    uint8_t key;
    switch (btn) {
    case BSP_BTN_UP: key = FF_KEY_UP; break;
    case BSP_BTN_DOWN: key = FF_KEY_DOWN; break;
    case BSP_BTN_OK: key = FF_KEY_OK; break;
    default: return;
    }
    (void)xQueueSend(s_keys, &key, 0);
}

static void flappy_pause(bool paused) {
    s_frozen = paused;
    s_last_us = esp_timer_get_time();
    s_acc_us = 0;
    gb_app_set_playing(!paused && (s_flow.state == FF_PLAYING || s_flow.state == FF_DYING));
}

static void flappy_icon(lv_obj_t *box) {
    // 直接用游戏里的小鸟精灵(40x40,平飞、翅膀居中)。
    lv_obj_t *bird = lv_image_create(box);
    lv_image_set_src(bird, flappy_img_bird[nearest_angle_index(0)][1]);
    lv_obj_center(bird);
}

const gb_game_t GB_GAME_FLAPPY = {
    .id = GAME_ID,
    .title = FT_GAME_TITLE,
    .tagline = FT_GAME_TAGLINE,
    .accent = 0x3FA34D,
    .icon = flappy_icon,
    .enter = flappy_enter,
    .exit = flappy_exit,
    .key = flappy_key,
    .pause = flappy_pause,
};
