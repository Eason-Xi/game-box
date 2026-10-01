// main/launcher/gb_launcher_layout.c —— 见 gb_launcher_layout.h。
#include "gb_launcher_layout.h"

size_t gb_list_first_visible(size_t first, size_t selected, size_t count, size_t visible) {
    if (visible == 0 || count <= visible) return 0;
    if (selected >= count) selected = count - 1;
    if (selected < first) first = selected;
    else if (selected >= first + visible) first = selected - visible + 1;
    if (first > count - visible) first = count - visible;
    return first;
}

gb_list_thumb_t gb_list_thumb(size_t first, size_t count, size_t visible,
                              int track_y, int track_h, int min_h) {
    gb_list_thumb_t thumb = { .y = track_y, .h = 0 };
    if (visible == 0 || count <= visible || track_h <= 0) return thumb;
    int h = (int)((size_t)track_h * visible / count);
    if (h < min_h) h = min_h;
    if (h > track_h) h = track_h;
    const size_t max_first = count - visible;
    if (first > max_first) first = max_first;
    thumb.h = h;
    thumb.y = track_y + (int)((size_t)(track_h - h) * first / max_first);
    return thumb;
}
