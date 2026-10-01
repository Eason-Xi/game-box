// tools/gamebox_preview/stubs/esp_err.h —— 主机渲染预览用的最小桩。
#pragma once
#include <stdint.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_NO_MEM 0x101
static inline const char *esp_err_to_name(esp_err_t e) { (void)e; return "err"; }
