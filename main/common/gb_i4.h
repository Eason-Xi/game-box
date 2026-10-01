// main/common/gb_i4.h —— I4（16 色调色板）像素缓冲上的绘制原语（纯逻辑，可主机测试）。
//
// 游戏画面大多是方块：蛇身、管道、箱子、障碍物。用 LV_COLOR_FORMAT_I4 画布，
// 200x240 只要 24KB，而同尺寸 RGB565 要 94KB —— 在没有 PSRAM 的 C3 上差别很大。
//
// 像素排布与 LVGL 9.5 的 lv_canvas_set_px() 一致：一个字节放两个像素，
// 偶数列在高 4 位。base 指向第 (0,0) 个像素（即 lv_draw_buf_goto_xy(buf, 0, 0)，
// 已跳过开头的调色板），stride 为每行字节数（draw_buf->header.stride）。
#pragma once

#include <stdint.h>

typedef struct {
    uint8_t *base;
    uint32_t stride;
    int w;
    int h;
} gb_i4_t;

// 写一个像素；越界忽略。
void gb_i4_px(const gb_i4_t *img, int x, int y, uint8_t color);

// 读一个像素；越界返回 0。
uint8_t gb_i4_get(const gb_i4_t *img, int x, int y);

// 填充矩形，自动裁剪到画布范围内；w/h <= 0 时什么也不做。
void gb_i4_fill_rect(const gb_i4_t *img, int x, int y, int w, int h, uint8_t color);

// 整块清成同一颜色。
void gb_i4_clear(const gb_i4_t *img, uint8_t color);
