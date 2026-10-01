// main/games/dodge/dodge_model.h —— 躲避方块的游戏规则（纯逻辑，可主机测试）。
//
// 玩法：方块从右往左飞，玩家在左侧用 ▲▼ 换道躲开。每躲过一个得 1 分，
// 分数越高方块越快、出现越密；20 分以后偶尔一次来两个（一定留有空道）。
//
// 坐标单位是画布像素，方块横坐标用 1/16 像素的定点数保存，低帧率下也能匀速移动。
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define DODGE_W            200
#define DODGE_H            240
#define DODGE_LANES        6
#define DODGE_LANE_H       (DODGE_H / DODGE_LANES)
#define DODGE_PLAYER_X     14
#define DODGE_PLAYER_SIZE  18
#define DODGE_BLOCK_W      22
#define DODGE_BLOCK_H      26
#define DODGE_MAX_BLOCKS   8
#define DODGE_MAX_STEP_MS  50      // 单步最长时间：卡顿后不会一下子「瞬移」穿过玩家

#define DODGE_SPEED_START  90      // 像素/秒
#define DODGE_SPEED_STEP   5
#define DODGE_SPEED_MAX    240
#define DODGE_SPAWN_START  950     // 毫秒
#define DODGE_SPAWN_STEP   25
#define DODGE_SPAWN_MIN    380
#define DODGE_DOUBLE_FROM  20      // 从这个分数起可能一次来两个

typedef enum {
    DODGE_READY = 0,
    DODGE_RUN,
    DODGE_OVER,
} dodge_state_t;

typedef struct {
    bool used;
    uint8_t lane;
    int32_t x_q4;      // 左边缘横坐标 × 16
} dodge_block_t;

typedef struct {
    dodge_state_t state;
    uint8_t lane;              // 玩家所在车道 0..DODGE_LANES-1
    uint32_t score;
    uint32_t spawn_left_ms;    // 距下一次出方块
    uint32_t rng;
    dodge_block_t blocks[DODGE_MAX_BLOCKS];
} dodge_t;

// step() 返回的事件位
#define DODGE_EV_PASS  0x1u    // 本步至少躲过一个方块（得分）
#define DODGE_EV_CRASH 0x2u    // 本步撞上了，状态已变为 OVER

void dodge_init(dodge_t *g);                   // READY，玩家在中间车道
void dodge_start(dodge_t *g, uint32_t seed);   // 开一局（seed 为 0 时使用固定值）
bool dodge_move(dodge_t *g, int dir);          // -1 上 / +1 下；READY 与 RUN 中有效，到边返回 false
uint32_t dodge_step(dodge_t *g, uint32_t dt_ms);

uint32_t dodge_speed(const dodge_t *g);            // 当前速度，像素/秒
uint32_t dodge_spawn_interval(const dodge_t *g);   // 当前出块间隔，毫秒
int dodge_lane_top(uint8_t lane);                  // 车道顶边 y
int dodge_block_x(const dodge_block_t *b);         // 方块左边缘的整数像素 x
