// main/launcher/gb_launcher.h —— 游戏选择页（列表，一屏 GB_LIST_VISIBLE 行）。
//
// 所有函数都必须在持有 LVGL 锁的上下文中调用。
#pragma once

#include <stddef.h>

#define GB_LIST_VISIBLE 4

// 建立并载入启动页，选中第 selected 个游戏。
void gb_launcher_show(size_t selected);

// 选中第 selected 个游戏；必要时滚动列表让它可见。
void gb_launcher_select(size_t selected);

// 删除启动页（进入游戏前调用）。未建立时什么也不做。
void gb_launcher_destroy(void);
