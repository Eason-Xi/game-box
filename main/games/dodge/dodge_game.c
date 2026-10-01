// main/games/dodge/dodge_game.c —— 躲避方块：页面、帧循环与按键。
//
// 规则在 dodge_model.c，画法在 dodge_draw.c，本文件只做 LVGL 胶水。
// 它同时是「画布类游戏」的接入样板：enter() 申请画布、exit() 释放，
// 帧循环用 lv_timer（在 LVGL 任务里运行，已持锁）。
#include "dodge_draw.h"
#include "dodge_model.h"
#include "dodge_text.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "gb_app.h"
#include "gb_canvas.h"
#include "gb_game.h"
#include "gb_score.h"
#include "gb_sfx.h"
#include "gb_text.h"
#include "gb_ui.h"

#define GAME_ID       "dodge"
#define BOARD_X       20
#define BOARD_Y       48
#define FRAME_MS      30
#define RESTART_COOLDOWN_MS 600   // 撞上后短暂忽略「再来」，防止手快直接开下一局
#define COLOR_BG      0x0E2233
#define COLOR_ACCENT  0x2EC4B6

static const uint32_t PALETTE[DODGE_PAL_COUNT] = {
    [DODGE_PAL_BG] = 0x0F2A3D,
    [DODGE_PAL_LANE] = 0x24506E,
    [DODGE_PAL_BLOCK] = 0xFF6B6B,
    [DODGE_PAL_BLOCK_LIGHT] = 0xFFA8A8,
    [DODGE_PAL_PLAYER] = 0xFFD166,
    [DODGE_PAL_PLAYER_DARK] = 0xE09F00,
    [DODGE_PAL_CRASH] = 0xFFFFFF,
};

static const gb_note_t MOVE_NOTES[]  = { { 988, 18 } };
static const gb_note_t PASS_NOTES[]  = { { 1175, 30 } };
static const gb_note_t START_NOTES[] = { { 659, 60 }, { 880, 60 }, { 1175, 90 } };
static const gb_note_t CRASH_NOTES[] = { { 392, 110 }, { 294, 110 }, { 220, 220 } };
static const gb_note_t BEST_NOTES[]  = { { 784, 70 }, { 988, 70 }, { 1319, 160 } };
static const gb_tune_t TUNE_MOVE  = GB_TUNE(MOVE_NOTES);
static const gb_tune_t TUNE_PASS  = GB_TUNE(PASS_NOTES);
static const gb_tune_t TUNE_START = GB_TUNE(START_NOTES);
static const gb_tune_t TUNE_CRASH = GB_TUNE(CRASH_NOTES);
static const gb_tune_t TUNE_BEST  = GB_TUNE(BEST_NOTES);

static lv_obj_t *s_scr;
static lv_obj_t *s_canvas;
static lv_obj_t *s_score;
static lv_obj_t *s_hint;
static lv_obj_t *s_banner;
static lv_draw_buf_t *s_buf;
static lv_timer_t *s_timer;
static dodge_t s_game;
static uint32_t s_last_ms;
static uint32_t s_over_at_ms;
static bool s_paused;

static uint32_t now_ms(void) {
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static void redraw(void) {
    if (!s_buf) return;
    const gb_i4_t img = gb_canvas_i4(s_buf);
    dodge_draw(&s_game, &img);
    lv_obj_invalidate(s_canvas);
}

static void show_state(bool new_record) {
    lv_label_set_text_fmt(s_score, "%s %lu", DODGE_TXT_SCORE, (unsigned long)s_game.score);
    switch (s_game.state) {
    case DODGE_READY: lv_label_set_text(s_hint, DODGE_TXT_READY); break;
    case DODGE_RUN:   lv_label_set_text(s_hint, DODGE_TXT_RUN); break;
    case DODGE_OVER:  lv_label_set_text(s_hint, DODGE_TXT_OVER); break;
    }
    if (new_record) lv_obj_remove_flag(s_banner, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(s_banner, LV_OBJ_FLAG_HIDDEN);
}

static void game_over(void) {
    gb_app_set_playing(false);
    s_over_at_ms = now_ms();
    const bool record = gb_score_submit(GAME_ID, s_game.score);
    gb_sfx_play(record ? &TUNE_BEST : &TUNE_CRASH);
    show_state(record);
}

static void frame(lv_timer_t *t) {
    (void)t;
    const uint32_t now = now_ms();
    const uint32_t dt = now - s_last_ms;
    s_last_ms = now;
    if (s_paused || s_game.state != DODGE_RUN) return;

    const uint32_t ev = dodge_step(&s_game, dt);
    if (ev & DODGE_EV_PASS) {
        gb_sfx_play(&TUNE_PASS);
        show_state(false);
    }
    if (ev & DODGE_EV_CRASH) game_over();
    redraw();
}

static void start_round(void) {
    dodge_start(&s_game, esp_random());
    s_last_ms = now_ms();
    gb_app_set_playing(true);
    gb_sfx_play(&TUNE_START);
    show_state(false);
    redraw();
}

static void dodge_icon(lv_obj_t *box) {
    // 缩小版的游戏画面（40x40）：两道虚线、两个红方块、一个黄色小人。
    for (int i = 1; i < 3; i++) {
        for (int x = 4; x < 36; x += 7) gb_ui_rect(box, x, i * 13, 4, 1, PALETTE[DODGE_PAL_LANE], 0);
    }
    gb_ui_rect(box, 24, 3, 9, 9, PALETTE[DODGE_PAL_BLOCK], 2);
    gb_ui_rect(box, 19, 29, 9, 9, PALETTE[DODGE_PAL_BLOCK], 2);
    gb_ui_rect(box, 6, 16, 9, 9, PALETTE[DODGE_PAL_PLAYER], 2);
}

static void dodge_enter(void) {
    dodge_init(&s_game);
    s_paused = false;
    s_scr = gb_ui_screen_create(COLOR_BG);

    s_score = gb_ui_label(s_scr, &gb_font_20, 0xFFFFFF, "");
    lv_obj_set_pos(s_score, GB_UI_EDGE + 2, 11);
    gb_ui_battery_create(s_scr, 0x9FC5D8);

    s_buf = gb_canvas_create(DODGE_W, DODGE_H, LV_COLOR_FORMAT_I4);
    if (s_buf) {
        s_canvas = lv_canvas_create(s_scr);
        lv_canvas_set_draw_buf(s_canvas, s_buf);
        for (int i = 0; i < DODGE_PAL_COUNT; i++) {
            lv_canvas_set_palette(s_canvas, (uint8_t)i, lv_color_to_32(lv_color_hex(PALETTE[i]), LV_OPA_COVER));
        }
        lv_obj_set_pos(s_canvas, BOARD_X, BOARD_Y);
    } else {
        lv_obj_t *err = gb_ui_label(s_scr, &gb_font_14, GB_COLOR_DANGER, GB_TXT_NO_MEMORY);
        lv_obj_center(err);
    }

    s_banner = gb_ui_label(s_scr, &gb_font_28, GB_COLOR_HILITE, DODGE_TXT_RECORD);
    lv_obj_align(s_banner, LV_ALIGN_TOP_MID, 0, BOARD_Y + 96);
    lv_obj_add_flag(s_banner, LV_OBJ_FLAG_HIDDEN);

    s_hint = gb_ui_label(s_scr, &gb_font_14, 0x9FC5D8, "");
    lv_obj_align(s_hint, LV_ALIGN_TOP_MID, 0, BOARD_Y + DODGE_H + 7);

    show_state(false);
    redraw();
    s_last_ms = now_ms();
    if (s_buf) s_timer = lv_timer_create(frame, FRAME_MS, NULL);
    lv_screen_load(s_scr);
}

static void dodge_exit(void) {
    gb_app_set_playing(false);
    if (s_timer) {
        lv_timer_delete(s_timer);
        s_timer = NULL;
    }
    if (s_scr) {
        lv_obj_delete(s_scr);   // 先删引用画布的对象，再释放画布内存
        s_scr = NULL;
    }
    gb_canvas_destroy(s_buf);
    s_buf = NULL;
    s_canvas = s_score = s_hint = s_banner = NULL;
}

static void dodge_key(bsp_btn_t btn, bsp_btn_ev_t ev) {
    if (!s_buf) return;
    if ((btn == BSP_BTN_UP || btn == BSP_BTN_DOWN) && ev == BSP_BTN_PRESS) {
        // 换道用 PRESS：按下就动，不等抬手，躲避才跟手。
        if (dodge_move(&s_game, btn == BSP_BTN_UP ? -1 : +1)) {
            gb_sfx_play(&TUNE_MOVE);
            redraw();
        }
        return;
    }
    if (btn != BSP_BTN_OK || ev != BSP_BTN_CLICK) return;
    if (s_game.state == DODGE_READY) {
        start_round();
    } else if (s_game.state == DODGE_OVER && now_ms() - s_over_at_ms >= RESTART_COOLDOWN_MS) {
        dodge_init(&s_game);
        show_state(false);
        redraw();
    }
}

static void dodge_pause(bool paused) {
    s_paused = paused;
    gb_app_set_playing(!paused && s_game.state == DODGE_RUN);
    s_last_ms = now_ms();   // 恢复时不把暂停时长算进这一帧
}

const gb_game_t GB_GAME_DODGE = {
    .id = GAME_ID,
    .title = DODGE_TXT_TITLE,
    .tagline = DODGE_TXT_TAGLINE,
    .accent = COLOR_ACCENT,
    .icon = dodge_icon,
    .enter = dodge_enter,
    .exit = dodge_exit,
    .key = dodge_key,
    .pause = dodge_pause,
};
