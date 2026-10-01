// main/common/gb_i4.c —— I4 绘制原语，见 gb_i4.h。
#include "gb_i4.h"

#include <string.h>

void gb_i4_px(const gb_i4_t *img, int x, int y, uint8_t color) {
    if (!img || !img->base || x < 0 || y < 0 || x >= img->w || y >= img->h) return;
    uint8_t *p = img->base + (uint32_t)y * img->stride + (uint32_t)(x >> 1);
    const uint8_t shift = (uint8_t)(4 - 4 * (x & 1));
    *p = (uint8_t)((*p & ~(0x0Fu << shift)) | ((color & 0x0Fu) << shift));
}

uint8_t gb_i4_get(const gb_i4_t *img, int x, int y) {
    if (!img || !img->base || x < 0 || y < 0 || x >= img->w || y >= img->h) return 0;
    const uint8_t *p = img->base + (uint32_t)y * img->stride + (uint32_t)(x >> 1);
    const uint8_t shift = (uint8_t)(4 - 4 * (x & 1));
    return (uint8_t)((*p >> shift) & 0x0Fu);
}

void gb_i4_fill_rect(const gb_i4_t *img, int x, int y, int w, int h, uint8_t color) {
    if (!img || !img->base || w <= 0 || h <= 0) return;
    int x0 = x < 0 ? 0 : x;
    int y0 = y < 0 ? 0 : y;
    int x1 = x + w > img->w ? img->w : x + w;   // 不含
    int y1 = y + h > img->h ? img->h : y + h;
    if (x0 >= x1 || y0 >= y1) return;

    const uint8_t pair = (uint8_t)(((color & 0x0Fu) << 4) | (color & 0x0Fu));
    for (int row = y0; row < y1; row++) {
        int col = x0;
        if (col & 1) gb_i4_px(img, col++, row, color);          // 左侧落单的奇数列
        int end = x1;
        if (end > col && (end & 1)) gb_i4_px(img, --end, row, color);   // 右侧落单的偶数列
        if (end > col) {
            memset(img->base + (uint32_t)row * img->stride + (uint32_t)(col >> 1), pair,
                   (size_t)((end - col) >> 1));
        }
    }
}

void gb_i4_clear(const gb_i4_t *img, uint8_t color) {
    if (!img) return;
    gb_i4_fill_rect(img, 0, 0, img->w, img->h, color);
}
