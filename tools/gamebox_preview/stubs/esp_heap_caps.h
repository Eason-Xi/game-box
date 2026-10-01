// tools/gamebox_preview/stubs/esp_heap_caps.h —— 主机渲染预览用的最小桩。
#pragma once
#include <stdlib.h>
#define MALLOC_CAP_INTERNAL 0
#define MALLOC_CAP_8BIT 0
static inline void *heap_caps_calloc(size_t n, size_t size, unsigned caps) { (void)caps; return calloc(n, size); }
static inline void heap_caps_free(void *p) { free(p); }
static inline size_t heap_caps_get_largest_free_block(unsigned caps) { (void)caps; return 0; }
