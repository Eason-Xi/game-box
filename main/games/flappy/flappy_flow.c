// main/flappy_flow.c —— 页面流程状态机实现,见 flappy_flow.h。
#include "flappy_flow.h"

#include <stddef.h>

void ff_settings_default(ff_settings_t *s) {
    *s = (ff_settings_t){
        .version = FF_SETTINGS_VERSION,
        .difficulty = FB_DIFF_NORMAL,
        .sound_on = 1,
    };
}

bool ff_settings_sanitize(ff_settings_t *s) {
    if (s->version != FF_SETTINGS_VERSION || s->difficulty >= FB_DIFF_COUNT ||
        s->sound_on > 1) {
        ff_settings_default(s);
        return false;
    }
    s->reserved = 0;
    return true;
}

void ff_flow_init(ff_flow_t *f, const ff_settings_t *settings) {
    *f = (ff_flow_t){ .state = FF_TITLE, .focus = FF_TITLE_ITEM_START };
    f->settings = *settings;
    ff_settings_sanitize(&f->settings);
}

static ff_action_t move_focus(ff_flow_t *f, ff_key_t key, uint8_t count) {
    if (key == FF_KEY_UP) f->focus = (uint8_t)((f->focus + count - 1u) % count);
    else f->focus = (uint8_t)((f->focus + 1u) % count);
    return FF_ACT_FOCUS;
}

static ff_action_t start_round(ff_flow_t *f) {
    f->state = FF_READY;
    f->focus = 0;
    return FF_ACT_START;
}

static ff_action_t to_title(ff_flow_t *f) {
    f->state = FF_TITLE;
    f->focus = FF_TITLE_ITEM_START;
    return FF_ACT_TO_TITLE;
}

ff_action_t ff_flow_key(ff_flow_t *f, ff_key_t key, uint32_t now_ms) {
    switch (f->state) {
    case FF_TITLE:
        if (key != FF_KEY_OK) return move_focus(f, key, FF_TITLE_ITEMS);
        if (f->focus == FF_TITLE_ITEM_DIFF) {
            f->settings.difficulty = (uint8_t)((f->settings.difficulty + 1u) % FB_DIFF_COUNT);
            return FF_ACT_CYCLE_DIFF;
        }
        if (f->focus == FF_TITLE_ITEM_SOUND) {
            f->settings.sound_on = f->settings.sound_on ? 0 : 1;
            return FF_ACT_TOGGLE_SOUND;
        }
        return start_round(f);

    case FF_READY:
        if (key == FF_KEY_DOWN) return to_title(f);
        f->state = FF_PLAYING;
        return FF_ACT_FLAP;

    case FF_PLAYING:
        if (key == FF_KEY_DOWN) {
            f->state = FF_PAUSED;
            f->focus = FF_PAUSE_ITEM_RESUME;
            return FF_ACT_PAUSE;
        }
        return FF_ACT_FLAP;

    case FF_DYING:
        return FF_ACT_NONE;

    case FF_PAUSED:
        if (key != FF_KEY_OK) return move_focus(f, key, FF_PAUSE_ITEMS);
        if (f->focus == FF_PAUSE_ITEM_RESTART) return start_round(f);
        if (f->focus == FF_PAUSE_ITEM_TITLE) return to_title(f);
        f->state = FF_PLAYING;
        return FF_ACT_RESUME;

    case FF_OVER:
        // 坠地前后玩家往往还在连按扇翅,短暂锁定避免误触"再来一局"。
        if ((uint32_t)(now_ms - f->over_at_ms) < FF_OVER_LOCK_MS) return FF_ACT_NONE;
        if (key != FF_KEY_OK) return move_focus(f, key, FF_OVER_ITEMS);
        if (f->focus == FF_OVER_ITEM_TITLE) return to_title(f);
        return start_round(f);
    }
    return FF_ACT_NONE;
}

void ff_flow_on_hit(ff_flow_t *f) {
    if (f->state == FF_PLAYING) f->state = FF_DYING;
}

bool ff_flow_on_over(ff_flow_t *f, uint32_t score, uint32_t now_ms) {
    if (f->state != FF_PLAYING && f->state != FF_DYING) return false;
    f->state = FF_OVER;
    f->focus = FF_OVER_ITEM_AGAIN;
    f->over_at_ms = now_ms;
    uint16_t *best = &f->settings.best[f->settings.difficulty];
    if (score > 0xFFFFu) score = 0xFFFFu;
    if (score <= *best) return false;
    *best = (uint16_t)score;
    return true;
}

uint16_t ff_flow_best(const ff_flow_t *f) {
    return f->settings.best[f->settings.difficulty];
}
