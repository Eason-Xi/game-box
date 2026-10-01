// main/launcher/gb_launcher.c —— 游戏选择页（列表）。
//
// 布局（240x320，四角 30px 圆角）：
//   y 9..31    左「游戏盒」，右电量
//   y 44..284  游戏列表：4 行，每行 54px、行距 8px
//              行内：左侧 40x40 图标；第一行游戏名（左，最多 4 字）+「★最高分」（右）；
//              第二行一句话介绍（约 9 字，超出显示省略号）
//   x 224      游戏超过 4 个时显示滚动条
//   y 290      操作提示
// 选中行用游戏自己的主题色填充并加高亮边框。切换选中项时整个列表重建：
// 最多 4 行、几十个对象，重建比逐个改样式简单，耗时也可以忽略。
#include "gb_launcher.h"

#include "gb_launcher_layout.h"
#include "gb_launcher_text.h"
#include "gb_registry.h"
#include "gb_score.h"
#include "gb_ui.h"

#define LIST_X        24
#define LIST_Y        44
#define LIST_W        192
#define ROW_H         54
#define ROW_GAP       8
#define LIST_H        (GB_LIST_VISIBLE * ROW_H + (GB_LIST_VISIBLE - 1) * ROW_GAP)
#define ICON_X        6
#define TEXT_X        (ICON_X + GB_ICON_SIZE + 8)
#define TITLE_W       84     // 4 个 20px 汉字（80px）留余量；更长的游戏名显示省略号
#define RIGHT_PAD     7
#define BAR_X         224
#define BAR_W         3
#define BAR_MIN_H     24
#define HINT_Y        290

static lv_obj_t *s_scr;
static lv_obj_t *s_list;     // 所有行的容器，切换选中项时清空重建
static size_t s_first;       // 第一个可见行的游戏下标

// 把颜色调暗 pct%，用于图标底色。
static uint32_t shade(uint32_t rgb, int pct) {
    uint32_t r = (rgb >> 16) & 0xFF, g = (rgb >> 8) & 0xFF, b = rgb & 0xFF;
    r = r * (uint32_t)(100 - pct) / 100;
    g = g * (uint32_t)(100 - pct) / 100;
    b = b * (uint32_t)(100 - pct) / 100;
    return (r << 16) | (g << 8) | b;
}

// 没有图标的游戏：显示游戏名的第一个字。
static void fallback_icon(lv_obj_t *box, const char *title) {
    char first[5] = { 0 };
    size_t len = 1;
    const unsigned char lead = (unsigned char)title[0];
    if (lead >= 0xF0) len = 4;
    else if (lead >= 0xE0) len = 3;
    else if (lead >= 0xC0) len = 2;
    for (size_t i = 0; i < len && title[i]; i++) first[i] = title[i];
    lv_obj_t *label = gb_ui_label(box, &gb_font_20, GB_COLOR_TEXT, first);
    lv_obj_center(label);
}

static void build_row(size_t index, int y, bool selected) {
    const gb_game_t *game = gb_registry_get(index);
    if (!game) return;

    lv_obj_t *row = gb_ui_rect(s_list, 0, y, LIST_W, ROW_H,
                               selected ? game->accent : GB_COLOR_PANEL, 12);
    if (selected) {
        lv_obj_set_style_border_color(row, lv_color_hex(GB_COLOR_HILITE), 0);
        lv_obj_set_style_border_width(row, 2, 0);
    }

    lv_obj_t *icon = gb_ui_rect(row, ICON_X, (ROW_H - GB_ICON_SIZE) / 2, GB_ICON_SIZE, GB_ICON_SIZE,
                                shade(game->accent, selected ? 30 : 45), 9);
    if (game->icon) game->icon(icon);
    else fallback_icon(icon, game->title);

    lv_obj_t *title = gb_ui_label(row, &gb_font_20, GB_COLOR_TEXT, game->title);
    lv_obj_set_pos(title, TEXT_X, 5);
    lv_obj_set_width(title, TITLE_W);
    lv_label_set_long_mode(title, LV_LABEL_LONG_MODE_DOTS);

    const uint32_t score = gb_score_best(game->id);
    if (score > 0) {
        lv_obj_t *best = gb_ui_label(row, &gb_font_14, selected ? GB_COLOR_TEXT : GB_COLOR_HILITE, "");
        lv_label_set_text_fmt(best, "%s%lu", GB_TXT_BEST, (unsigned long)score);
        lv_obj_align(best, LV_ALIGN_TOP_RIGHT, -RIGHT_PAD, 9);
    }

    lv_obj_t *tagline = gb_ui_label(row, &gb_font_14, selected ? GB_COLOR_TEXT : GB_COLOR_MUTED,
                                    game->tagline);
    lv_obj_set_pos(tagline, TEXT_X, 31);
    lv_obj_set_width(tagline, LIST_W - TEXT_X - RIGHT_PAD);
    lv_label_set_long_mode(tagline, LV_LABEL_LONG_MODE_DOTS);   // 介绍太长时用省略号收尾
}

static void build_scrollbar(size_t count) {
    const gb_list_thumb_t thumb = gb_list_thumb(s_first, count, GB_LIST_VISIBLE,
                                                LIST_Y, LIST_H, BAR_MIN_H);
    if (thumb.h == 0) return;
    gb_ui_rect(s_list, BAR_X - LIST_X, 0, BAR_W, LIST_H, GB_COLOR_PANEL, 1);
    gb_ui_rect(s_list, BAR_X - LIST_X, thumb.y - LIST_Y, BAR_W, thumb.h, GB_COLOR_MUTED, 1);
}

static void build_list(size_t selected) {
    const size_t count = gb_registry_count();
    s_first = gb_list_first_visible(s_first, selected, count, GB_LIST_VISIBLE);
    lv_obj_clean(s_list);
    for (size_t i = 0; i < GB_LIST_VISIBLE && s_first + i < count; i++) {
        build_row(s_first + i, (int)i * (ROW_H + ROW_GAP), s_first + i == selected);
    }
    build_scrollbar(count);
}

void gb_launcher_show(size_t selected) {
    gb_launcher_destroy();
    s_scr = gb_ui_screen_create(GB_COLOR_BG);

    lv_obj_t *name = gb_ui_label(s_scr, &gb_font_20, GB_COLOR_TEXT, GB_TXT_APP_NAME);
    lv_obj_set_pos(name, GB_UI_EDGE + 2, 9);
    gb_ui_battery_create(s_scr, GB_COLOR_MUTED);

    if (gb_registry_count() == 0) {
        lv_obj_t *empty = gb_ui_label(s_scr, &gb_font_20, GB_COLOR_MUTED, GB_TXT_EMPTY);
        lv_obj_center(empty);
    } else {
        s_list = gb_ui_box(s_scr, BAR_X + BAR_W - LIST_X, LIST_H);
        lv_obj_set_pos(s_list, LIST_X, LIST_Y);
        build_list(selected);
        lv_obj_t *hint = gb_ui_label(s_scr, &gb_font_14, GB_COLOR_MUTED, GB_TXT_HINT);
        lv_obj_align(hint, LV_ALIGN_TOP_MID, 0, HINT_Y);
    }
    lv_screen_load(s_scr);
}

void gb_launcher_select(size_t selected) {
    if (s_scr && s_list) build_list(selected);
}

void gb_launcher_destroy(void) {
    if (!s_scr) return;
    lv_obj_delete(s_scr);
    s_scr = NULL;
    s_list = NULL;
}
