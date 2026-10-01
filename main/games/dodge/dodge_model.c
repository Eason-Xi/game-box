// main/games/dodge/dodge_model.c —— 躲避方块规则，见 dodge_model.h。
#include "dodge_model.h"

#include <string.h>

static uint32_t next_rand(dodge_t *g) {
    // xorshift32：够用、无分配、结果可复现，便于主机测试。
    uint32_t x = g->rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    g->rng = x;
    return x;
}

void dodge_init(dodge_t *g) {
    if (!g) return;
    memset(g, 0, sizeof(*g));
    g->state = DODGE_READY;
    g->lane = DODGE_LANES / 2;
    g->rng = 0x9E3779B9u;
}

void dodge_start(dodge_t *g, uint32_t seed) {
    if (!g) return;
    const uint8_t lane = g->lane;
    dodge_init(g);
    g->lane = lane;            // 保留开局前选好的车道
    g->rng = seed ? seed : 0x9E3779B9u;
    g->state = DODGE_RUN;
    g->spawn_left_ms = 400;    // 开局给一点反应时间
}

bool dodge_move(dodge_t *g, int dir) {
    if (!g || g->state == DODGE_OVER) return false;
    const int lane = (int)g->lane + (dir < 0 ? -1 : 1);
    if (lane < 0 || lane >= DODGE_LANES) return false;
    g->lane = (uint8_t)lane;
    return true;
}

uint32_t dodge_speed(const dodge_t *g) {
    const uint32_t v = DODGE_SPEED_START + g->score * DODGE_SPEED_STEP;
    return v > DODGE_SPEED_MAX ? DODGE_SPEED_MAX : v;
}

uint32_t dodge_spawn_interval(const dodge_t *g) {
    const uint32_t cut = g->score * DODGE_SPAWN_STEP;
    if (cut >= DODGE_SPAWN_START - DODGE_SPAWN_MIN) return DODGE_SPAWN_MIN;
    return DODGE_SPAWN_START - cut;
}

int dodge_lane_top(uint8_t lane) {
    return (int)lane * DODGE_LANE_H;
}

int dodge_block_x(const dodge_block_t *b) {
    // 算术右移对负数向下取整，方块在左边缘外也能正确裁剪。
    return (int)(b->x_q4 >> 4);
}

static void spawn_one(dodge_t *g, uint8_t lane) {
    for (int i = 0; i < DODGE_MAX_BLOCKS; i++) {
        if (!g->blocks[i].used) {
            g->blocks[i].used = true;
            g->blocks[i].lane = lane;
            g->blocks[i].x_q4 = (int32_t)DODGE_W << 4;
            return;
        }
    }
}

static void spawn(dodge_t *g) {
    const uint8_t first = (uint8_t)(next_rand(g) % DODGE_LANES);
    spawn_one(g, first);
    if (g->score >= DODGE_DOUBLE_FROM && next_rand(g) % 3 == 0) {
        // 第二个放在另一条车道：最多占两条，至少还剩四条可走。
        const uint8_t offset = (uint8_t)(1 + next_rand(g) % (DODGE_LANES - 1));
        spawn_one(g, (uint8_t)((first + offset) % DODGE_LANES));
    }
}

static bool hits_player(const dodge_t *g, const dodge_block_t *b) {
    if (b->lane != g->lane) return false;
    const int x = dodge_block_x(b);
    return x < DODGE_PLAYER_X + DODGE_PLAYER_SIZE && x + DODGE_BLOCK_W > DODGE_PLAYER_X;
}

uint32_t dodge_step(dodge_t *g, uint32_t dt_ms) {
    if (!g || g->state != DODGE_RUN) return 0;
    if (dt_ms > DODGE_MAX_STEP_MS) dt_ms = DODGE_MAX_STEP_MS;

    uint32_t events = 0;
    const int32_t dx_q4 = (int32_t)(dodge_speed(g) * dt_ms * 16u / 1000u);
    for (int i = 0; i < DODGE_MAX_BLOCKS; i++) {
        dodge_block_t *b = &g->blocks[i];
        if (!b->used) continue;
        b->x_q4 -= dx_q4;
        if (dodge_block_x(b) + DODGE_BLOCK_W <= 0) {
            b->used = false;
            g->score++;
            events |= DODGE_EV_PASS;
        }
    }
    for (int i = 0; i < DODGE_MAX_BLOCKS; i++) {
        if (g->blocks[i].used && hits_player(g, &g->blocks[i])) {
            g->state = DODGE_OVER;
            return events | DODGE_EV_CRASH;
        }
    }

    if (dt_ms >= g->spawn_left_ms) {
        spawn(g);
        g->spawn_left_ms = dodge_spawn_interval(g);
    } else {
        g->spawn_left_ms -= dt_ms;
    }
    return events;
}
