// main/common/gb_ui.c —— 见 gb_ui.h。
#include "gb_ui.h"

#include "bsp_battery.h"
#include "gb_text.h"

#define BATTERY_REFRESH_MS 5000
#define BATTERY_W 20
#define BATTERY_H 10

lv_obj_t *gb_ui_screen_create(uint32_t bg) {
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(scr, lv_color_hex(bg), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(scr, 0, 0);
    lv_obj_set_style_pad_all(scr, 0, 0);
    return scr;
}

lv_obj_t *gb_ui_label(lv_obj_t *parent, const lv_font_t *font, uint32_t color, const char *text) {
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_label_set_text(label, text ? text : "");
    return label;
}

lv_obj_t *gb_ui_box(lv_obj_t *parent, int32_t w, int32_t h) {
    lv_obj_t *box = lv_obj_create(parent);
    lv_obj_remove_style_all(box);
    lv_obj_remove_flag(box, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(box, w, h);
    return box;
}

lv_obj_t *gb_ui_rect(lv_obj_t *parent, int32_t x, int32_t y, int32_t w, int32_t h,
                     uint32_t color, int32_t radius) {
    lv_obj_t *rect = gb_ui_box(parent, w, h);
    lv_obj_set_pos(rect, x, y);
    lv_obj_set_style_bg_color(rect, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(rect, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(rect, radius, 0);
    return rect;
}

// ---------------------------------------------------------------- 电量

typedef struct {
    lv_obj_t *fill;
    lv_obj_t *label;
    lv_timer_t *timer;
} battery_widget_t;

static void battery_refresh(battery_widget_t *w) {
    const int soc = bsp_battery_soc();
    if (soc < 0) {
        lv_label_set_text(w->label, GB_TXT_BATTERY_NA);
        lv_obj_set_width(w->fill, 0);
        return;
    }
    const int pct = soc > 100 ? 100 : soc;
    lv_label_set_text_fmt(w->label, "%d%%", pct);
    lv_obj_set_width(w->fill, (BATTERY_W - 4) * pct / 100);
    lv_obj_set_style_bg_color(w->fill, lv_color_hex(pct <= 15 ? GB_COLOR_DANGER : 0x7BE495), 0);
}

static void battery_tick(lv_timer_t *t) {
    battery_refresh(lv_timer_get_user_data(t));
}

static void battery_deleted(lv_event_t *e) {
    battery_widget_t *w = lv_event_get_user_data(e);
    if (w->timer) lv_timer_delete(w->timer);
    lv_free(w);
}

lv_obj_t *gb_ui_battery_create(lv_obj_t *parent, uint32_t color) {
    battery_widget_t *w = lv_malloc_zeroed(sizeof(battery_widget_t));
    if (!w) return NULL;

    lv_obj_t *root = gb_ui_box(parent, 64, 16);
    lv_obj_align(root, LV_ALIGN_TOP_RIGHT, -GB_UI_EDGE, 12);

    lv_obj_t *shell = gb_ui_box(root, BATTERY_W, BATTERY_H);
    lv_obj_align(shell, LV_ALIGN_RIGHT_MID, -3, 0);
    lv_obj_set_style_border_color(shell, lv_color_hex(color), 0);
    lv_obj_set_style_border_width(shell, 1, 0);
    lv_obj_set_style_radius(shell, 2, 0);

    lv_obj_t *nub = gb_ui_rect(root, 0, 0, 2, 4, color, 0);
    lv_obj_align(nub, LV_ALIGN_RIGHT_MID, 0, 0);

    w->fill = gb_ui_rect(shell, 2, 2, BATTERY_W - 4, BATTERY_H - 4, 0x7BE495, 1);
    w->label = gb_ui_label(root, &gb_font_14, color, "");
    lv_obj_align(w->label, LV_ALIGN_RIGHT_MID, -(BATTERY_W + 7), 0);

    battery_refresh(w);
    w->timer = lv_timer_create(battery_tick, BATTERY_REFRESH_MS, w);
    lv_obj_add_event_cb(root, battery_deleted, LV_EVENT_DELETE, w);
    return root;
}
