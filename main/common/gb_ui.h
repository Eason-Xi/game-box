// main/common/gb_ui.h —— 游戏盒的配色、字体与少量通用控件。
//
// 这里只放「每个页面都会用到」的东西：屏幕底色、文字标签、右上角电量。
// 游戏画面本身由各游戏自行设计，不要求套用启动页的风格。
//
// 屏幕四角是半径 BSP_LVGL_SCREEN_RADIUS(30px) 的圆角，圆角外的像素不显示。
// 靠近四角放内容时，左右至少留 GB_UI_EDGE 的边距。
#pragma once

#include "lvgl.h"
#include <stdint.h>

#define GB_UI_W     240
#define GB_UI_H     320
#define GB_UI_EDGE  24     // 顶/底部 20px 内的内容，左右至少留这么多

// 平台配色（启动页、确认框）。
#define GB_COLOR_BG        0x15172E   // 夜空蓝底
#define GB_COLOR_PANEL     0x252848
#define GB_COLOR_TEXT      0xF6F2E7   // 暖白
#define GB_COLOR_MUTED     0x8D92BC
#define GB_COLOR_HILITE    0xFFD166   // 高亮黄
#define GB_COLOR_DANGER    0xFF6B6B

// 共用中文字库（tools/gamebox/gen_fonts.py 生成）。
// 14/16/20/24/28/40 收录所有 *_text.h 里的文案 + 可打印 ASCII；num_48 只有数字与少量符号。
LV_FONT_DECLARE(gb_font_14);
LV_FONT_DECLARE(gb_font_16);
LV_FONT_DECLARE(gb_font_20);
LV_FONT_DECLARE(gb_font_24);
LV_FONT_DECLARE(gb_font_28);
LV_FONT_DECLARE(gb_font_40);
LV_FONT_DECLARE(gb_num_48);

// 新建一个不可滚动、无边框、纯色底的屏幕（不自动载入）。
lv_obj_t *gb_ui_screen_create(uint32_t bg);

// 新建文字标签。
lv_obj_t *gb_ui_label(lv_obj_t *parent, const lv_font_t *font, uint32_t color, const char *text);

// 新建一个透明、无边框、不可点击的容器，用来摆放图标等组合图形。
lv_obj_t *gb_ui_box(lv_obj_t *parent, int32_t w, int32_t h);

// 新建一个纯色圆角矩形。
lv_obj_t *gb_ui_rect(lv_obj_t *parent, int32_t x, int32_t y, int32_t w, int32_t h,
                     uint32_t color, int32_t radius);

// 在 parent 右上角放电量指示，每 5 秒刷新；随 parent 删除自动停止刷新。
// 电量计不可用时显示「--」。color 为文字与图标颜色。
lv_obj_t *gb_ui_battery_create(lv_obj_t *parent, uint32_t color);
