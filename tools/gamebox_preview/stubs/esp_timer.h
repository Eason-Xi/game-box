// tools/gamebox_preview/stubs/esp_timer.h —— 主机渲染预览用的最小桩：时间由 harness 推进。
#pragma once
#include <stdint.h>
extern int64_t g_now_us;
static inline int64_t esp_timer_get_time(void) { return g_now_us; }
