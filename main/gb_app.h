// main/gb_app.h —— 游戏盒应用核心：按键分发、页面切换、退出确认、空闲调暗。
#pragma once

#include "esp_err.h"
#include <stdbool.h>

// 显示与 LVGL 必须已经就绪。audio_ok 为 false 时静音运行。
esp_err_t gb_app_start(bool audio_ok);

// 游戏进行中设为 true：此时不计空闲、不调暗背光。任意任务均可调用。
void gb_app_set_playing(bool playing);
