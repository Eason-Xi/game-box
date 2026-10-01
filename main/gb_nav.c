// main/gb_nav.c —— 导航状态机实现，见 gb_nav.h。
#include "gb_nav.h"

void gb_nav_init(gb_nav_t *nav, size_t count) {
    if (!nav) return;
    nav->count = count;
    nav->selected = 0;
    nav->mode = GB_MODE_LAUNCHER;
}

static gb_nav_result_t result(gb_action_t action, size_t index, int dir) {
    gb_nav_result_t r = { .action = action, .index = index, .dir = dir };
    return r;
}

gb_nav_result_t gb_nav_handle(gb_nav_t *nav, gb_key_t key, gb_key_ev_t ev) {
    if (!nav || nav->count == 0) return result(GB_ACT_NONE, 0, 0);

    switch (nav->mode) {
    case GB_MODE_PLAYING:
        if (key == GB_KEY_OK && ev == GB_KEY_EV_LONG) {
            nav->mode = GB_MODE_CONFIRM;
            return result(GB_ACT_ASK_EXIT, nav->selected, 0);
        }
        return result(GB_ACT_FORWARD, nav->selected, 0);

    case GB_MODE_CONFIRM:
        if (ev != GB_KEY_EV_CLICK) return result(GB_ACT_NONE, nav->selected, 0);
        if (key == GB_KEY_OK) return result(GB_ACT_EXIT, nav->selected, 0);
        nav->mode = GB_MODE_PLAYING;
        return result(GB_ACT_CANCEL_EXIT, nav->selected, 0);

    case GB_MODE_LAUNCHER:
    default:
        if (ev != GB_KEY_EV_CLICK) return result(GB_ACT_NONE, nav->selected, 0);
        if (key == GB_KEY_UP) {
            nav->selected = (nav->selected + nav->count - 1) % nav->count;
            return result(GB_ACT_SELECT, nav->selected, -1);
        }
        if (key == GB_KEY_DOWN) {
            nav->selected = (nav->selected + 1) % nav->count;
            return result(GB_ACT_SELECT, nav->selected, +1);
        }
        return result(GB_ACT_LAUNCH, nav->selected, 0);
    }
}

void gb_nav_launched(gb_nav_t *nav) {
    if (nav && nav->count > 0) nav->mode = GB_MODE_PLAYING;
}

void gb_nav_exited(gb_nav_t *nav) {
    if (nav) nav->mode = GB_MODE_LAUNCHER;
}
