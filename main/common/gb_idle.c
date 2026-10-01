// main/common/gb_idle.c —— 空闲调暗 / 熄屏，见 gb_idle.h。
#include "gb_idle.h"

void gb_idle_init(gb_idle_t *idle, uint32_t now_ms, uint32_t dim_after_ms, uint32_t off_after_ms) {
    if (!idle) return;
    idle->last_activity_ms = now_ms;
    idle->dim_after_ms = dim_after_ms;
    idle->off_after_ms = off_after_ms > dim_after_ms ? off_after_ms : dim_after_ms;
    idle->state = GB_POWER_ACTIVE;
    idle->swallowing = false;
    idle->swallow_key = GB_KEY_UP;
    idle->swallow_since_ms = 0;
}

static bool ends_press(gb_key_ev_t ev) {
    return ev == GB_KEY_EV_CLICK || ev == GB_KEY_EV_LONG || ev == GB_KEY_EV_DOUBLE;
}

bool gb_idle_input(gb_idle_t *idle, uint32_t now_ms, gb_key_t key, gb_key_ev_t ev) {
    if (!idle) return false;
    idle->last_activity_ms = now_ms;

    if (idle->state == GB_POWER_OFF) {
        idle->state = GB_POWER_ACTIVE;
        idle->swallowing = !ends_press(ev);   // 唤醒事件本身已是一次完整按键时无需再吞
        idle->swallow_key = key;
        idle->swallow_since_ms = now_ms;
        return true;
    }
    idle->state = GB_POWER_ACTIVE;

    if (idle->swallowing) {
        if (now_ms - idle->swallow_since_ms > GB_IDLE_WAKE_SWALLOW_MS || key != idle->swallow_key) {
            idle->swallowing = false;          // 超时或换了键：恢复正常处理
        } else {
            if (ends_press(ev)) idle->swallowing = false;
            return true;
        }
    }
    return false;
}

gb_power_t gb_idle_update(gb_idle_t *idle, uint32_t now_ms, bool busy) {
    if (!idle) return GB_POWER_ACTIVE;
    if (busy) {
        idle->last_activity_ms = now_ms;
        idle->state = GB_POWER_ACTIVE;
        return idle->state;
    }
    const uint32_t idle_ms = now_ms - idle->last_activity_ms;
    if (idle_ms >= idle->off_after_ms) {
        idle->state = GB_POWER_OFF;
    } else if (idle_ms >= idle->dim_after_ms) {
        idle->state = GB_POWER_DIM;
    } else {
        idle->state = GB_POWER_ACTIVE;
    }
    return idle->state;
}
