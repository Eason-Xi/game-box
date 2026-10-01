// main/flappy_model.c —— 纯游戏模型实现,见 flappy_model.h。
#include "flappy_model.h"

#include <stddef.h>

static const fb_params_t PARAMS[FB_DIFF_COUNT] = {
    [FB_DIFF_EASY]   = { .gap_h = 104, .speed_q8 = 346, .max_shift = 90 },  // 1.35 px/tick
    [FB_DIFF_NORMAL] = { .gap_h = 92,  .speed_q8 = 410, .max_shift = 90 }, // 1.60 px/tick
    [FB_DIFF_HARD]   = { .gap_h = 84,  .speed_q8 = 486, .max_shift = 70 }, // 1.90 px/tick
};

// 准备阶段的浮动曲线:48 tick 一个周期,振幅约 5px(近似正弦)。
static const int8_t BOB[16] = { 0, 2, 4, 5, 5, 5, 4, 2, 0, -2, -4, -5, -5, -5, -4, -2 };
#define BOB_TICKS_PER_STEP 3

const fb_params_t *fb_params(fb_difficulty_t difficulty) {
    if ((unsigned)difficulty >= FB_DIFF_COUNT) difficulty = FB_DIFF_NORMAL;
    return &PARAMS[difficulty];
}

uint32_t fb_rng_next(uint32_t *state) {
    *state = *state * 1664525u + 1013904223u;
    return *state >> 8;
}

int32_t fb_model_speed_q8(const fb_model_t *m) {
    const int32_t base = fb_params(m->difficulty)->speed_q8;
    uint32_t steps = m->score / 10u;
    if (steps > 5u) steps = 5u;
    return base + base * (int32_t)steps * 4 / 100;
}

int fb_pipe_gap_top(const fb_model_t *m, const fb_pipe_t *p) {
    return p->gap_y - fb_params(m->difficulty)->gap_h / 2;
}

int fb_pipe_gap_bottom(const fb_model_t *m, const fb_pipe_t *p) {
    return fb_pipe_gap_top(m, p) + fb_params(m->difficulty)->gap_h;
}

static int16_t next_gap(fb_model_t *m, int prev_gap_y) {
    const fb_params_t *p = fb_params(m->difficulty);
    const int lo = FB_GAP_MARGIN + p->gap_h / 2;
    const int hi = FB_GROUND_Y - FB_GAP_MARGIN - p->gap_h / 2;
    int y = lo + (int)(fb_rng_next(&m->rng) % (uint32_t)(hi - lo + 1));
    // 限制相邻缺口落差,避免出现来不及飞到的组合。
    if (y > prev_gap_y + p->max_shift) y = prev_gap_y + p->max_shift;
    if (y < prev_gap_y - p->max_shift) y = prev_gap_y - p->max_shift;
    if (y < lo) y = lo;
    if (y > hi) y = hi;
    return (int16_t)y;
}

void fb_model_init(fb_model_t *m, fb_difficulty_t difficulty, uint32_t seed) {
    if ((unsigned)difficulty >= FB_DIFF_COUNT) difficulty = FB_DIFF_NORMAL;
    *m = (fb_model_t){
        .state = FB_STATE_READY,
        .difficulty = difficulty,
        .bird_y_q8 = FB_BIRD_START_Y * FB_ONE,
        .rng = seed ? seed : 0x5EEDu,
    };
    int prev = FB_BIRD_START_Y;
    for (size_t i = 0; i < FB_PIPE_COUNT; i++) {
        m->pipes[i].x_q8 = (FB_PIPE_FIRST_X + (int32_t)i * FB_PIPE_SPACING) * FB_ONE;
        m->pipes[i].gap_y = next_gap(m, prev);
        m->pipes[i].scored = false;
        prev = m->pipes[i].gap_y;
    }
}

uint32_t fb_model_flap(fb_model_t *m) {
    if (m->state == FB_STATE_READY) {
        m->state = FB_STATE_PLAYING;
    } else if (m->state != FB_STATE_PLAYING) {
        return 0;
    }
    m->vel_q8 = FB_FLAP_VELOCITY;
    return FB_EV_FLAP;
}

static void apply_gravity(fb_model_t *m) {
    m->vel_q8 += FB_GRAVITY;
    if (m->vel_q8 > FB_MAX_FALL) m->vel_q8 = FB_MAX_FALL;
    m->bird_y_q8 += m->vel_q8;
    // 天花板只挡住不致死,与经典玩法一致地给玩家留余地。
    if (m->bird_y_q8 < FB_BIRD_HALF_H * FB_ONE) {
        m->bird_y_q8 = FB_BIRD_HALF_H * FB_ONE;
        if (m->vel_q8 < 0) m->vel_q8 = 0;
    }
}

static bool landed(fb_model_t *m) {
    const int32_t floor_q8 = (FB_GROUND_Y - FB_BIRD_HALF_H) * FB_ONE;
    if (m->bird_y_q8 < floor_q8) return false;
    m->bird_y_q8 = floor_q8;
    m->vel_q8 = 0;
    m->state = FB_STATE_OVER;
    return true;
}

static bool hits_pipe(const fb_model_t *m, const fb_pipe_t *p) {
    const int px = (int)(p->x_q8 >> FB_Q);
    const int bx1 = FB_BIRD_X - FB_BIRD_HALF_W;
    const int bx2 = FB_BIRD_X + FB_BIRD_HALF_W;
    if (bx2 <= px || bx1 >= px + FB_PIPE_W) return false;
    const int by = (int)(m->bird_y_q8 >> FB_Q);
    return by - FB_BIRD_HALF_H < fb_pipe_gap_top(m, p) ||
           by + FB_BIRD_HALF_H > fb_pipe_gap_bottom(m, p);
}

static const fb_pipe_t *rightmost_pipe(const fb_model_t *m) {
    const fb_pipe_t *r = &m->pipes[0];
    for (size_t i = 1; i < FB_PIPE_COUNT; i++) {
        if (m->pipes[i].x_q8 > r->x_q8) r = &m->pipes[i];
    }
    return r;
}

static void move_pipes(fb_model_t *m, int32_t speed) {
    for (size_t i = 0; i < FB_PIPE_COUNT; i++) m->pipes[i].x_q8 -= speed;
    for (size_t i = 0; i < FB_PIPE_COUNT; i++) {
        fb_pipe_t *p = &m->pipes[i];
        if (p->x_q8 + FB_PIPE_W * FB_ONE >= 0) continue;
        // 完全移出左侧后回收到最右侧管道之后,保持固定间距。
        const fb_pipe_t *r = rightmost_pipe(m);
        p->x_q8 = r->x_q8 + FB_PIPE_SPACING * FB_ONE;
        p->gap_y = next_gap(m, r->gap_y);
        p->scored = false;
    }
}

uint32_t fb_model_step(fb_model_t *m) {
    uint32_t events = 0;
    switch (m->state) {
    case FB_STATE_READY:
        m->ticks++;
        m->ground_q8 += (uint32_t)fb_model_speed_q8(m);
        m->bird_y_q8 = (FB_BIRD_START_Y + BOB[(m->ticks / BOB_TICKS_PER_STEP) % 16]) * FB_ONE;
        break;

    case FB_STATE_PLAYING: {
        m->ticks++;
        const int32_t speed = fb_model_speed_q8(m);
        m->ground_q8 += (uint32_t)speed;
        move_pipes(m, speed);
        apply_gravity(m);
        if (landed(m)) return FB_EV_HIT | FB_EV_LAND;
        for (size_t i = 0; i < FB_PIPE_COUNT; i++) {
            if (hits_pipe(m, &m->pipes[i])) {
                m->state = FB_STATE_DYING;
                if (m->vel_q8 > FB_HIT_BOUNCE) m->vel_q8 = FB_HIT_BOUNCE;
                return FB_EV_HIT;
            }
        }
        for (size_t i = 0; i < FB_PIPE_COUNT; i++) {
            fb_pipe_t *p = &m->pipes[i];
            if (!p->scored && (p->x_q8 >> FB_Q) + FB_PIPE_W / 2 <= FB_BIRD_X) {
                p->scored = true;
                m->score++;
                events |= FB_EV_SCORE;
            }
        }
        break;
    }

    case FB_STATE_DYING:
        apply_gravity(m);
        if (landed(m)) events |= FB_EV_LAND;
        break;

    case FB_STATE_OVER:
        break;
    }
    return events;
}

int fb_bird_angle_deg(int32_t vel_q8) {
    // 速度 ≤ -1px/tick 时抬头 -25°;之后随下落速度线性低头,7px/tick 时 90°。
    const int32_t lo = -FB_ONE;
    if (vel_q8 <= lo) return -25;
    if (vel_q8 >= FB_MAX_FALL) return 90;
    return -25 + (int)((vel_q8 - lo) * 115 / (FB_MAX_FALL - lo));
}

int fb_bird_wing_frame(const fb_model_t *m) {
    if (m->state == FB_STATE_DYING || m->state == FB_STATE_OVER) return 1;
    static const uint8_t CYCLE[4] = { 0, 1, 2, 1 };
    return CYCLE[(m->ticks / 5u) % 4u];
}

fb_medal_t fb_medal_for(uint32_t score) {
    if (score >= FB_MEDAL_PLATINUM_AT) return FB_MEDAL_PLATINUM;
    if (score >= FB_MEDAL_GOLD_AT) return FB_MEDAL_GOLD;
    if (score >= FB_MEDAL_SILVER_AT) return FB_MEDAL_SILVER;
    if (score >= FB_MEDAL_BRONZE_AT) return FB_MEDAL_BRONZE;
    return FB_MEDAL_NONE;
}
