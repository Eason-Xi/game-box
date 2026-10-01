// main/games/tap/tap_model.c —— 连点挑战规则，见 tap_model.h。
#include "tap_model.h"

void tap_init(tap_t *t) {
    if (!t) return;
    t->state = TAP_READY;
    t->start_ms = 0;
    t->count = 0;
}

bool tap_update(tap_t *t, uint32_t now_ms) {
    if (!t || t->state != TAP_RUN) return false;
    if (now_ms - t->start_ms < TAP_DURATION_MS) return false;
    t->state = TAP_OVER;
    return true;
}

bool tap_press(tap_t *t, uint32_t now_ms) {
    if (!t) return false;
    if (t->state == TAP_READY) {
        t->state = TAP_RUN;
        t->start_ms = now_ms;
        t->count = 1;
        return true;
    }
    if (t->state != TAP_RUN || tap_update(t, now_ms)) return false;
    t->count++;
    return true;
}

uint32_t tap_remaining_ms(const tap_t *t, uint32_t now_ms) {
    if (!t || t->state == TAP_OVER) return 0;
    if (t->state == TAP_READY) return TAP_DURATION_MS;
    const uint32_t used = now_ms - t->start_ms;
    return used >= TAP_DURATION_MS ? 0 : TAP_DURATION_MS - used;
}

bool tap_can_restart(const tap_t *t, uint32_t now_ms) {
    if (!t || t->state != TAP_OVER) return false;
    return now_ms - t->start_ms >= TAP_DURATION_MS + TAP_RESTART_COOLDOWN_MS;
}
