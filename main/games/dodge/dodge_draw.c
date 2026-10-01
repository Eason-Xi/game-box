// main/games/dodge/dodge_draw.c —— 见 dodge_draw.h。
#include "dodge_draw.h"

static void draw_lanes(const gb_i4_t *img) {
    // 车道分隔用虚线，比实线轻，不和方块抢视线。
    for (int lane = 1; lane < DODGE_LANES; lane++) {
        const int y = dodge_lane_top((uint8_t)lane);
        for (int x = 4; x < DODGE_W; x += 12) gb_i4_fill_rect(img, x, y, 6, 1, DODGE_PAL_LANE);
    }
}

static void draw_block(const gb_i4_t *img, const dodge_block_t *b) {
    const int x = dodge_block_x(b);
    const int y = dodge_lane_top(b->lane) + (DODGE_LANE_H - DODGE_BLOCK_H) / 2;
    gb_i4_fill_rect(img, x, y, DODGE_BLOCK_W, DODGE_BLOCK_H, DODGE_PAL_BLOCK);
    gb_i4_fill_rect(img, x, y, DODGE_BLOCK_W, 3, DODGE_PAL_BLOCK_LIGHT);   // 顶部高光
    gb_i4_fill_rect(img, x, y, 3, DODGE_BLOCK_H, DODGE_PAL_BLOCK_LIGHT);
}

static void draw_player(const dodge_t *g, const gb_i4_t *img) {
    const int x = DODGE_PLAYER_X;
    const int y = dodge_lane_top(g->lane) + (DODGE_LANE_H - DODGE_PLAYER_SIZE) / 2;
    const bool crashed = g->state == DODGE_OVER;
    const uint8_t body = crashed ? DODGE_PAL_CRASH : DODGE_PAL_PLAYER;
    gb_i4_fill_rect(img, x, y, DODGE_PLAYER_SIZE, DODGE_PLAYER_SIZE, body);
    gb_i4_fill_rect(img, x, y + DODGE_PLAYER_SIZE - 4, DODGE_PLAYER_SIZE, 4, DODGE_PAL_PLAYER_DARK);
    // 两只眼睛朝向右边（方块来的方向）。
    gb_i4_fill_rect(img, x + 10, y + 5, 3, 4, DODGE_PAL_BG);
    gb_i4_fill_rect(img, x + 14, y + 5, 3, 4, DODGE_PAL_BG);
}

void dodge_draw(const dodge_t *g, const gb_i4_t *img) {
    if (!g || !img) return;
    gb_i4_clear(img, DODGE_PAL_BG);
    draw_lanes(img);
    for (int i = 0; i < DODGE_MAX_BLOCKS; i++) {
        if (g->blocks[i].used) draw_block(img, &g->blocks[i]);
    }
    draw_player(g, img);
}
