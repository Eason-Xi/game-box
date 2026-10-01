// main/crane_flow.c —— 页面流程状态机实现,见 crane_flow.h。
#include "crane_flow.h"

#include <stddef.h>

void cf_settings_default(cf_settings_t *s) {
    *s = (cf_settings_t){ .version = CF_SETTINGS_VERSION, .sound_on = 1 };
}

bool cf_settings_sanitize(cf_settings_t *s) {
    if (s->version != CF_SETTINGS_VERSION || s->sound_on > 1) {
        cf_settings_default(s);
        return false;
    }
    s->reserved = 0;
    return true;
}

void cf_flow_init(cf_flow_t *f, const cf_settings_t *settings) {
    *f = (cf_flow_t){ .state = CF_TITLE, .focus = CF_TITLE_ITEM_START };
    f->settings = *settings;
    cf_settings_sanitize(&f->settings);
}

static cf_action_t move_focus(cf_flow_t *f, cf_key_t key, uint8_t count) {
    if (key == CF_KEY_UP) f->focus = (uint8_t)((f->focus + count - 1u) % count);
    else f->focus = (uint8_t)((f->focus + 1u) % count);
    return CF_ACT_FOCUS;
}

static cf_action_t start_round(cf_flow_t *f) {
    f->state = CF_PLAYING;
    f->focus = 0;
    return CF_ACT_START;
}

static cf_action_t to_title(cf_flow_t *f) {
    f->state = CF_TITLE;
    f->focus = CF_TITLE_ITEM_START;
    return CF_ACT_TO_TITLE;
}

cf_action_t cf_flow_key(cf_flow_t *f, cf_key_t key, uint32_t now_ms) {
    switch (f->state) {
    case CF_TITLE:
        if (key != CF_KEY_OK) return move_focus(f, key, CF_TITLE_ITEMS);
        if (f->focus == CF_TITLE_ITEM_SOUND) {
            f->settings.sound_on = f->settings.sound_on ? 0 : 1;
            return CF_ACT_TOGGLE_SOUND;
        }
        return start_round(f);

    case CF_PLAYING:
        if (key == CF_KEY_UP) {
            f->state = CF_PAUSED;
            f->focus = CF_PAUSE_ITEM_RESUME;
            return CF_ACT_PAUSE;
        }
        return CF_ACT_DROP;

    case CF_PAUSED:
        if (key != CF_KEY_OK) return move_focus(f, key, CF_PAUSE_ITEMS);
        if (f->focus == CF_PAUSE_ITEM_RESTART) return start_round(f);
        if (f->focus == CF_PAUSE_ITEM_TITLE) return to_title(f);
        f->state = CF_PLAYING;
        return CF_ACT_RESUME;

    case CF_OVER:
        // 结束瞬间玩家往往还在连按松钩,短暂锁定避免误触"再来一局"。
        if ((uint32_t)(now_ms - f->over_at_ms) < CF_OVER_LOCK_MS) return CF_ACT_NONE;
        if (key != CF_KEY_OK) return move_focus(f, key, CF_OVER_ITEMS);
        if (f->focus == CF_OVER_ITEM_TITLE) return to_title(f);
        return start_round(f);
    }
    return CF_ACT_NONE;
}

static uint16_t clamp16(uint32_t v) {
    return v > 0xFFFFu ? 0xFFFFu : (uint16_t)v;
}

bool cf_flow_on_over(cf_flow_t *f, uint32_t score, uint32_t height, uint32_t now_ms,
                     bool *new_score) {
    *new_score = false;
    if (f->state != CF_PLAYING && f->state != CF_PAUSED) return false;
    f->state = CF_OVER;
    f->focus = CF_OVER_ITEM_AGAIN;
    f->over_at_ms = now_ms;
    bool changed = false;
    if (clamp16(score) > f->settings.best_score) {
        f->settings.best_score = clamp16(score);
        *new_score = true;
        changed = true;
    }
    if (clamp16(height) > f->settings.best_height) {
        f->settings.best_height = clamp16(height);
        changed = true;
    }
    return changed;
}
