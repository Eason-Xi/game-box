// main/launcher/gb_launcher_layout.h —— 游戏列表的滚动计算（纯逻辑，可主机测试）。
#pragma once

#include <stddef.h>

typedef struct {
    int y;
    int h;
} gb_list_thumb_t;

// 让选中项保持可见：返回新的「第一个可见行」下标。
// 选中项在窗口上方时窗口上移、在下方时下移；游戏数不超过 visible 时恒为 0。
size_t gb_list_first_visible(size_t first, size_t selected, size_t count, size_t visible);

// 滚动条滑块的位置与高度。count <= visible 时 h 为 0（不需要滚动条）。
gb_list_thumb_t gb_list_thumb(size_t first, size_t count, size_t visible,
                              int track_y, int track_h, int min_h);
