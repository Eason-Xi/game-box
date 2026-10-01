// main/crane_theme.h —— 堆箱子的配色与界面布局常量(纯头文件,主机测试可用)。
//
// 屏幕 240x320 竖屏,BSP 会把四角半径 30px 以外涂黑,所以靠边的元素
// (命数、电量、提示文字)都要避开圆角,tests/test_crane_layout.c 负责校验。
// 场景几何(箱子尺寸、地基位置、镜头)由 crane_model.h 定义。
#pragma once

#include "crane_model.h"

#define CT_SCREEN_W        CS_SCREEN_W
#define CT_SCREEN_H        CS_SCREEN_H

// ---- 配色(0xRRGGBB) ----
// 天空随高度变色:白天 → 金色 → 晚霞 → 黄昏 → 夜晚,每段 CT_SKY_STEP_PX 像素
#define CT_SKY_STOPS       { 0x9ADCF5, 0xFFD9A0, 0xF59E8B, 0x6B6FB5, 0x23264F }
#define CT_SKY_STEP_PX     480
#define CT_COLOR_CLOUD     0xFFFFFF
#define CT_COLOR_CITY      0xB9D4E6   // 远处楼群剪影
#define CT_COLOR_GRASS     0x6DBE5C
#define CT_COLOR_DIRT      0xC9935A
#define CT_COLOR_BASE      0x8E9AA6   // 地基
#define CT_COLOR_BASE_TOP  0x5F6B76
#define CT_COLOR_BOX       0xD9A066   // 纸箱
#define CT_COLOR_BOX_EDGE  0x9A6330
#define CT_COLOR_TAPE      0xEDCB92   // 封箱胶带
#define CT_COLOR_ROPE      0x3B3B44
#define CT_COLOR_HOOK      0x2E2E36
#define CT_COLOR_FLASH     0xFFFFFF   // 完美落下时箱子描边闪白

#define CT_COLOR_TEXT      0xFFFFFF   // 场景上的文字
#define CT_COLOR_SHADOW    0x2B2118   // 文字投影
#define CT_COLOR_PERFECT   0xFFD23F
#define CT_COLOR_BAD       0xFF6B5B
#define CT_COLOR_LIFE      0xFFFFFF
#define CT_COLOR_HINT      0x5A3A1C   // 沙土上的提示文字
#define CT_COLOR_INK       0x4A3222   // 卡片正文
#define CT_COLOR_INK_SOFT  0x9C8068   // 次要文字
#define CT_COLOR_CARD      0xFFF7E6
#define CT_COLOR_CARD_EDGE 0x5B3A1E
#define CT_COLOR_FOCUS     0xF08A24   // 焦点胶囊
#define CT_COLOR_ON_FOCUS  0xFFFFFF
#define CT_COLOR_RECORD    0xE8423F
#define CT_COLOR_BATT      0xFFFFFF
#define CT_COLOR_BATT_LOW  0xFF6B5B

// ---- 场景(镜头为 0 时的屏幕坐标) ----
#define CT_BASE_H          12                          // 地基厚度,顶面即 CS_GROUND_SCREEN_Y
#define CT_GRASS_Y         (CS_GROUND_SCREEN_Y + 8)    // 草皮顶边,地基压在草上
#define CT_GRASS_H         6
#define CT_DIRT_Y          (CT_GRASS_Y + CT_GRASS_H)
#define CT_TAPE_W          10
#define CT_HOOK_W          12
#define CT_HOOK_H          6
#define CT_TOWER_POOL      16                          // 同时绘制的塔层数上限
#define CT_CLOUD_COUNT     3
#define CT_CLOUD_SPACING   260                         // 云在世界里的垂直间距

// ---- 顶部状态:命数在左,分数居中,电量在右 ----
#define CT_LIFE_X          22
#define CT_LIFE_Y          16
#define CT_LIFE_W          16
#define CT_LIFE_H          12
#define CT_LIFE_GAP        4
#define CT_SCORE_Y         4
#define CT_SCORE_HALF_W    40            // 三位数分数的半宽上限(40px 数字约 22px/字)
#define CT_HEIGHT_Y        48            // 分数下方的"N 层"
#define CT_BATT_X          194           // 电池外框左上角
#define CT_BATT_Y          16
#define CT_BATT_W          20
#define CT_BATT_H          11
#define CT_BATT_NUB_W      2
#define CT_BATT_TEXT_R     190           // 电量数字右边界(只显示数字,不带 %)
#define CT_BATT_TEXT_W     30
#define CT_BATT_TEXT_Y     11

// ---- 游戏中提示 ----
#define CT_POPUP_Y         140           // 吊着的箱子与塔顶之间
#define CT_HINT_Y          298           // 沙土上的操作提示

// ---- 标题页:Logo、菜单卡片,下方是 3 层的展示小塔(cs_model_init_demo) ----
#define CT_LOGO_Y          30
#define CT_LOGO_EN_Y       76
#define CT_MENU_X          30
#define CT_MENU_Y          108
#define CT_MENU_W          180
#define CT_MENU_ROW_H      28
#define CT_MENU_PAD        6
#define CT_MENU_BEST_H     24
#define CT_MENU_H          (CT_MENU_PAD * 2 + CT_MENU_ROW_H * 2 + CT_MENU_BEST_H)

// ---- 暂停与结算卡片 ----
#define CT_PAUSE_X         30
#define CT_PAUSE_Y         92
#define CT_PAUSE_W         180
#define CT_PAUSE_H         136
#define CT_OVER_X          20
#define CT_OVER_Y          54
#define CT_OVER_W          200
#define CT_OVER_H          226
#define CT_OVER_STAT_Y     40            // 卡片内第一行统计
#define CT_OVER_STAT_H     24
#define CT_OVER_BADGE_Y    138
#define CT_OVER_ROWS_Y     160
