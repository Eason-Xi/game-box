// main/games/dodge/dodge_draw.h —— 把躲避方块的状态画进 I4 画布（纯逻辑，可主机测试）。
#pragma once

#include "dodge_model.h"
#include "gb_i4.h"

// 调色板下标。颜色值在 dodge_game.c 里设置。
enum {
    DODGE_PAL_BG = 0,
    DODGE_PAL_LANE,
    DODGE_PAL_BLOCK,
    DODGE_PAL_BLOCK_LIGHT,
    DODGE_PAL_PLAYER,
    DODGE_PAL_PLAYER_DARK,
    DODGE_PAL_CRASH,
    DODGE_PAL_COUNT,
};

// 整帧重画。img 尺寸应为 DODGE_W x DODGE_H。
void dodge_draw(const dodge_t *g, const gb_i4_t *img);
