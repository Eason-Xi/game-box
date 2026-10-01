// tools/gamebox_preview/stubs/esp_random.h —— 主机渲染预览用的最小桩（固定序列，截图可复现）。
#pragma once
#include <stdint.h>
static inline uint32_t esp_random(void) { static uint32_t s = 20261001u; s = s * 1103515245u + 12345u; return s; }
