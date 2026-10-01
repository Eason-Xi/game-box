// main/games/tap/tap_game.c —— 连点挑战：页面与按键。
//
// 最简单的接入样板：纯 LVGL 控件、无画布、无后台任务。
#include "esp_timer.h"
#include "gb_app.h"
#include "gb_game.h"
#include "gb_score.h"
#include "gb_sfx.h"
#include "gb_ui.h"
#include "tap_model.h"
#include "tap_text.h"

#define GAME_ID       "tap"
#define TICK_MS       40
#define BAR_X         32
#define BAR_Y         204
#define BAR_W         176
#define BAR_H         10
#define COLOR_BG      0x33182A
#define COLOR_ACCENT  0xFF7A59
#define COLOR_SOFT    0xE7B7C2

static const gb_note_t TAP_NOTES[]  = { { 1047, 15 } };
static const gb_note_t END_NOTES[]  = { { 880, 80 }, { 659, 80 }, { 523, 160 } };
static const gb_note_t BEST_NOTES[] = { { 784, 70 }, { 988, 70 }, { 1319, 160 } };
static const gb_tune_t TUNE_TAP  = GB_TUNE(TAP_NOTES);
static const gb_tune_t TUNE_END  = GB_TUNE(END_NOTES);
static const gb_tune_t TUNE_BEST = GB_TUNE(BEST_NOTES);

static lv_obj_t *s_scr;
static lv_obj_t *s_count;
static lv_obj_t *s_bar;
static lv_obj_t *s_time;
static lv_obj_t *s_result;
static lv_obj_t *s_hint;
static lv_timer_t *s_timer;
static tap_t s_game;
static bool s_paused;
static uint32_t s_paused_at;

static uint32_t now_ms(void) {
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static void refresh(uint32_t now) {
    lv_label_set_text_fmt(s_count, "%lu", (unsigned long)s_game.count);
    const uint32_t left = tap_remaining_ms(&s_game, now);
    lv_obj_set_width(s_bar, (int32_t)(BAR_W * left / TAP_DURATION_MS));
    lv_label_set_text_fmt(s_time, "%lu.%lu %s", (unsigned long)(left / 1000),
                          (unsigned long)(left % 1000 / 100), TAP_TXT_SECONDS);
    lv_label_set_text(s_hint, s_game.state == TAP_READY ? TAP_TXT_READY
                            : s_game.state == TAP_RUN   ? TAP_TXT_RUN : TAP_TXT_OVER);
}

static void finish(void) {
    gb_app_set_playing(false);
    const bool record = gb_score_submit(GAME_ID, s_game.count);
    gb_sfx_play(record ? &TUNE_BEST : &TUNE_END);
    if (record) {
        lv_label_set_text(s_result, TAP_TXT_RECORD);
    } else {
        lv_label_set_text_fmt(s_result, "%s %lu", TAP_TXT_BEST, (unsigned long)gb_score_best(GAME_ID));
    }
    lv_obj_remove_flag(s_result, LV_OBJ_FLAG_HIDDEN);
}

static void tick(lv_timer_t *t) {
    (void)t;
    if (s_paused) return;
    const uint32_t now = now_ms();
    if (tap_update(&s_game, now)) finish();
    refresh(now);
}

static void tap_icon(lv_obj_t *box) {
    // 一个按钮 + 两圈波纹（40x40）。
    for (int i = 0; i < 2; i++) {
        lv_obj_t *ring = gb_ui_box(box, 32 - i * 10, 32 - i * 10);
        lv_obj_center(ring);
        lv_obj_set_style_radius(ring, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_color(ring, lv_color_hex(COLOR_SOFT), 0);
        lv_obj_set_style_border_width(ring, 2, 0);
        lv_obj_set_style_border_opa(ring, i ? LV_OPA_COVER : LV_OPA_50, 0);
    }
    lv_obj_t *dot = gb_ui_rect(box, 0, 0, 10, 10, 0xFFFFFF, LV_RADIUS_CIRCLE);
    lv_obj_center(dot);
}

static void tap_enter(void) {
    tap_init(&s_game);
    s_paused = false;
    s_scr = gb_ui_screen_create(COLOR_BG);

    lv_obj_t *title = gb_ui_label(s_scr, &gb_font_20, 0xFFFFFF, TAP_TXT_TITLE);
    lv_obj_set_pos(title, GB_UI_EDGE + 2, 9);
    gb_ui_battery_create(s_scr, COLOR_SOFT);

    s_count = gb_ui_label(s_scr, &gb_num_48, 0xFFFFFF, "0");
    lv_obj_align(s_count, LV_ALIGN_TOP_MID, 0, 90);
    lv_obj_t *unit = gb_ui_label(s_scr, &gb_font_14, COLOR_SOFT, TAP_TXT_UNIT);
    lv_obj_align(unit, LV_ALIGN_TOP_MID, 0, 150);

    gb_ui_rect(s_scr, BAR_X, BAR_Y, BAR_W, BAR_H, 0x4F2A40, BAR_H / 2);
    s_bar = gb_ui_rect(s_scr, BAR_X, BAR_Y, BAR_W, BAR_H, COLOR_ACCENT, BAR_H / 2);
    s_time = gb_ui_label(s_scr, &gb_font_14, COLOR_SOFT, "");
    lv_obj_align(s_time, LV_ALIGN_TOP_MID, 0, BAR_Y + 16);

    s_result = gb_ui_label(s_scr, &gb_font_20, GB_COLOR_HILITE, "");
    lv_obj_align(s_result, LV_ALIGN_TOP_MID, 0, 50);
    lv_obj_add_flag(s_result, LV_OBJ_FLAG_HIDDEN);

    s_hint = gb_ui_label(s_scr, &gb_font_20, 0xFFFFFF, "");
    lv_obj_align(s_hint, LV_ALIGN_TOP_MID, 0, 262);

    refresh(now_ms());
    s_timer = lv_timer_create(tick, TICK_MS, NULL);
    lv_screen_load(s_scr);
}

static void tap_exit(void) {
    gb_app_set_playing(false);
    if (s_timer) {
        lv_timer_delete(s_timer);
        s_timer = NULL;
    }
    if (s_scr) {
        lv_obj_delete(s_scr);
        s_scr = NULL;
    }
    s_count = s_bar = s_time = s_result = s_hint = NULL;
}

static void tap_key(bsp_btn_t btn, bsp_btn_ev_t ev) {
    if (btn != BSP_BTN_OK) return;
    const uint32_t now = now_ms();
    if (ev == BSP_BTN_PRESS) {
        // 计数用 PRESS：按下即算，连点时不受单击 / 双击判定窗口影响。
        const tap_state_t before = s_game.state;
        if (tap_press(&s_game, now)) {
            if (before == TAP_READY) gb_app_set_playing(true);
            gb_sfx_play(&TUNE_TAP);
        } else if (before == TAP_RUN && s_game.state == TAP_OVER) {
            finish();   // 时间恰好在这次按下时耗尽，tick 还没来得及结算
        }
        refresh(now);
    } else if (ev == BSP_BTN_CLICK && tap_can_restart(&s_game, now)) {
        tap_init(&s_game);
        lv_obj_add_flag(s_result, LV_OBJ_FLAG_HIDDEN);
        refresh(now);
    }
}

static void tap_pause(bool paused) {
    const uint32_t now = now_ms();
    if (paused) {
        s_paused_at = now;
    } else if (s_paused && s_game.state == TAP_RUN) {
        s_game.start_ms += now - s_paused_at;   // 暂停的时间不算进 10 秒
    }
    s_paused = paused;
    gb_app_set_playing(!paused && s_game.state == TAP_RUN);
}

const gb_game_t GB_GAME_TAP = {
    .id = GAME_ID,
    .title = TAP_TXT_TITLE,
    .tagline = TAP_TXT_TAGLINE,
    .accent = COLOR_ACCENT,
    .icon = tap_icon,
    .enter = tap_enter,
    .exit = tap_exit,
    .key = tap_key,
    .pause = tap_pause,
};
