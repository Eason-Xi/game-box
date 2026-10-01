// main/common/gb_canvas.h —— 游戏画布缓冲的申请与释放。
//
// 为什么不用 LV_DRAW_BUF_DEFINE_STATIC：静态缓冲开机就一直占着内存，合集里每个
// 游戏一块，加起来很快把没有 PSRAM 的 C3 吃光。为什么不用 lv_draw_buf_create：
// 它从 LVGL 自己 24KB 的内存池里分配，一块画布就能把池子占满。
// 这里从系统堆申请，在游戏 enter() 里创建、exit() 里销毁，同一时间只有一块。
//
// 用法（在持有 LVGL 锁的上下文中）：
//   s_buf = gb_canvas_create(200, 240, LV_COLOR_FORMAT_I4);
//   if (!s_buf) { 显示「内存不足」并返回; }
//   s_canvas = lv_canvas_create(scr);
//   lv_canvas_set_draw_buf(s_canvas, s_buf);
//   lv_canvas_set_palette(s_canvas, 0, lv_color32_make(...));
//   ...
//   exit(): 先 lv_obj_delete(画布所在 screen)，再 gb_canvas_destroy(s_buf)。
#pragma once

#include "gb_i4.h"
#include "lvgl.h"

// 失败（内存不足）返回 NULL。返回的缓冲已清零、可被直接修改像素。
lv_draw_buf_t *gb_canvas_create(uint32_t w, uint32_t h, lv_color_format_t cf);

// 释放 gb_canvas_create() 的结果；NULL 安全。必须先删除引用它的画布对象。
void gb_canvas_destroy(lv_draw_buf_t *buf);

// 把 I4 画布包装成 gb_i4_t，交给纯逻辑的绘制函数使用。
gb_i4_t gb_canvas_i4(lv_draw_buf_t *buf);
