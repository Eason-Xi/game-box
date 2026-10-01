// main/flappy_theme.h —— 飞吧小鸟的配色与界面布局常量(纯头文件,主机测试可用)。
//
// 屏幕 240x320 竖屏,BSP 会把四角半径 30px 以外涂黑,所以靠边的元素
// (电量、提示文字)都要避开圆角,tests/test_flappy_layout.c 负责校验。
#pragma once

#include "flappy_model.h"

#define FT_SCREEN_W        240
#define FT_SCREEN_H        320

// ---- 配色(0xRRGGBB) ----
#define FT_COLOR_SKY       0x78C8E6   // 与 tools/gen_flappy_assets.py 的 SKY 一致
#define FT_COLOR_DIRT      0xE8D08E
#define FT_COLOR_INK       0x4A3222   // 卡片正文
#define FT_COLOR_INK_SOFT  0x9C8068   // 次要文字
#define FT_COLOR_CARD      0xFFF7E6
#define FT_COLOR_CARD_EDGE 0x5B3A1E
#define FT_COLOR_FOCUS     0xFF8C2B   // 焦点胶囊
#define FT_COLOR_ON_FOCUS  0xFFFFFF
#define FT_COLOR_LOGO      0xFFFFFF
#define FT_COLOR_LOGO_DEEP 0xE0662A   // Logo 橙色投影
#define FT_COLOR_SHADOW    0x3A2A1A   // 分数/Logo 深色描边投影
#define FT_COLOR_HINT      0x6B4E2E   // 地面上的提示文字
#define FT_COLOR_RECORD    0xE8423F
#define FT_COLOR_BATT      0x2F4858
#define FT_COLOR_BATT_LOW  0xE8423F

#define FT_COLOR_BRONZE    0xCD7F32
#define FT_COLOR_SILVER    0xC4CCD6
#define FT_COLOR_GOLD      0xFFC940
#define FT_COLOR_PLATINUM  0xE6F6FF
#define FT_COLOR_NO_MEDAL  0xE9DCC4

// ---- 场景 ----
#define FT_GROUND_Y        FB_GROUND_Y   // 272
#define FT_GROUND_H        (FT_SCREEN_H - FT_GROUND_Y)
#define FT_GROUND_PERIOD   24            // 草皮条纹周期
#define FT_GROUND_BAND_H   22            // 滚动的草皮带高度,其下为静态沙土
#define FT_HILLS_H         40
#define FT_HILLS_Y         (FT_GROUND_Y - FT_HILLS_H)
#define FT_PIPE_CAP_W      52
#define FT_PIPE_CAP_H      18
#define FT_PIPE_CAP_INSET  ((FT_PIPE_CAP_W - FB_PIPE_W) / 2)
#define FT_BIRD_SPRITE     40
#define FT_TITLE_BIRD_X    120           // 标题页小鸟中心
#define FT_TITLE_BIRD_Y    122

// ---- 顶部状态:分数居中,电量在右上角 ----
#define FT_SCORE_Y         20
#define FT_SCORE_HALF_W    36            // 三位数分数的半宽上限(40px 数字约 22px/字)
#define FT_BATT_X          194           // 电池外框左上角
#define FT_BATT_Y          14
#define FT_BATT_W          20
#define FT_BATT_H          11
#define FT_BATT_NUB_W      2
#define FT_BATT_TEXT_R     190           // 电量数字右边界(只显示数字,不带 %)
#define FT_BATT_TEXT_W     30
#define FT_BATT_TEXT_Y     9

// ---- 标题页 ----
#define FT_LOGO_Y          30
#define FT_LOGO_EN_Y       76
#define FT_MENU_X          30
#define FT_MENU_Y          150
#define FT_MENU_W          180
#define FT_MENU_ROW_H      28
#define FT_MENU_PAD        6
#define FT_MENU_BEST_H     22
#define FT_MENU_H          (FT_MENU_PAD * 2 + FT_MENU_ROW_H * 3 + FT_MENU_BEST_H)
#define FT_HINT_Y          297           // 沙土上的操作提示(静态区域,不随滚动重绘)

// ---- 准备提示 ----
#define FT_READY_TITLE_Y   74
#define FT_READY_HINT_Y    200

// ---- 暂停与结算卡片 ----
#define FT_PAUSE_X         30
#define FT_PAUSE_Y         92
#define FT_PAUSE_W         180
#define FT_PAUSE_H         142
#define FT_OVER_X          20
#define FT_OVER_Y          58
#define FT_OVER_W          200
#define FT_OVER_H          206
#define FT_MEDAL_D         50
