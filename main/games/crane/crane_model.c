// main/crane_model.c —— 吊车堆箱子纯游戏模型实现,见 crane_model.h。
#include "crane_model.h"

#include <stddef.h>

#define TWO_POW_32       4294967296ULL
#define QUARTER_TURN     0x40000000u

#define GRAVITY          12              // Q8 像素/节拍²,约 1875 px/s²
#define KEEP_NUM         1               // 松钩后保留 1/4 的横向速度
#define KEEP_DEN         4
#define TIP_SPEED        (CS_ONE * 6 / 5) // 压在边上没压住时向外滑出的速度
#define SWAY_PERIOD_MS   2400
#define SWAY_MAX         (16 * CS_ONE)
#define SWAY_EASE        400             // 晃动幅度向基线收敛的时间常数(节拍)
#define CAM_EASE         12
#define COLLAPSE_FALL_AT 100             // 倒塌开始后多少节拍整体下坠

// 四分之一周期正弦表,Q15,65 项(含端点)
static const int16_t SIN_QUARTER[65] = {
    0, 804, 1608, 2410, 3212, 4011, 4808, 5602,
    6393, 7179, 7962, 8739, 9512, 10278, 11039, 11793,
    12539, 13279, 14010, 14732, 15446, 16151, 16846, 17530,
    18204, 18868, 19519, 20159, 20787, 21403, 22005, 22594,
    23170, 23731, 24279, 24811, 25329, 25832, 26319, 26790,
    27245, 27683, 28105, 28510, 28898, 29268, 29621, 29956,
    30273, 30571, 30852, 31113, 31356, 31580, 31785, 31971,
    32137, 32285, 32412, 32521, 32609, 32678, 32728, 32757,
    32767,
};

int32_t cs_sin_q15(uint32_t phase) {
    const uint32_t quadrant = phase >> 30;
    uint32_t p = phase & (QUARTER_TURN - 1u);   // 0..2^30-1
    if (quadrant & 1u) p = QUARTER_TURN - p;     // 第 2、4 象限镜像
    const uint32_t idx = p >> 24;                // 0..64
    const uint32_t frac = (p >> 8) & 0xFFFFu;
    int32_t v = SIN_QUARTER[idx];
    if (idx < 64) v += (int32_t)(((int64_t)(SIN_QUARTER[idx + 1] - SIN_QUARTER[idx]) * frac) >> 16);
    return (quadrant & 2u) ? -v : v;
}

static uint32_t isqrt32(uint32_t n) {
    uint32_t root = 0;
    uint32_t bit = 1u << 30;
    while (bit > n) bit >>= 2;
    while (bit) {
        if (n >= root + bit) {
            n -= root + bit;
            root = (root >> 1) + bit;
        } else {
            root >>= 1;
        }
        bit >>= 2;
    }
    return root;
}

static int32_t iabs(int32_t v) { return v < 0 ? -v : v; }

static uint32_t next_rand(cs_model_t *m) {
    uint32_t x = m->rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    m->rng = x;
    return x;
}

// ------------------------------------------------------------------ 难度

uint32_t cs_swing_period_ms(uint16_t count) {
    const uint32_t c = count > 45 ? 45 : count;
    return 2600u - 20u * c;
}

int cs_swing_amp_px(uint16_t count) {
    const int a = 56 + count / 2;
    return a > 78 ? 78 : a;
}

static uint32_t phase_step(uint32_t period_ms) {
    return (uint32_t)(TWO_POW_32 * CS_TICK_MS / period_ms);
}

// ------------------------------------------------------------------ 查询

int32_t cs_world_to_screen(const cs_model_t *m, int32_t world_y) {
    return CS_GROUND_SCREEN_Y * CS_ONE - world_y + m->cam;
}

int32_t cs_sway(const cs_model_t *m) {
    return (int32_t)(((int64_t)m->sway_amp * cs_sin_q15(m->sway_phase)) >> 15);
}

int32_t cs_layer_x(const cs_model_t *m, int i) {
    if (i < 0 || i >= m->count) return CS_CENTER_X * CS_ONE;
    // 剪切晃动:越高的层位移越大,塔顶位移为 cs_sway()
    return m->layer_x[i] + cs_sway(m) * (i + 1) / m->count;
}

int32_t cs_top_x(const cs_model_t *m) {
    return m->count ? cs_layer_x(m, m->count - 1) : CS_CENTER_X * CS_ONE;
}

bool cs_box_hooked(const cs_model_t *m) {
    return m->state == CS_ST_SPAWN || m->state == CS_ST_HANG;
}

bool cs_box_visible(const cs_model_t *m) {
    return m->state == CS_ST_SPAWN || m->state == CS_ST_HANG || m->state == CS_ST_FALL ||
           m->state == CS_ST_MISS;
}

void cs_collapse_offset(const cs_model_t *m, int i, int *dx, int *dy) {
    *dx = 0;
    *dy = 0;
    if (m->state != CS_ST_COLLAPSE && m->state != CS_ST_OVER) return;
    if (!m->over_by_collapse || i < m->collapse_from) return;
    const uint32_t t = m->timer > CS_COLLAPSE_TICKS ? CS_COLLAPSE_TICKS : m->timer;
    // 以倒塌层底边为支点转动:角度按 t² 加速,到结束时约 108°
    const uint64_t a = (uint64_t)t * t * (QUARTER_TURN / 10u * 12u) / ((uint64_t)CS_COLLAPSE_TICKS * CS_COLLAPSE_TICKS);
    const uint32_t angle = (uint32_t)a;
    const int32_t h = (i - m->collapse_from) * CS_BOX_H + CS_BOX_H / 2;
    const int32_t s = cs_sin_q15(angle);
    const int32_t c = cs_sin_q15(angle + QUARTER_TURN);
    int32_t x = (h * s) >> 15;
    int32_t y = h - ((h * c + (1 << 14)) >> 15);
    if (t > COLLAPSE_FALL_AT) {
        const int32_t ft = (int32_t)t - COLLAPSE_FALL_AT;
        y += (ft * ft * GRAVITY) >> CS_Q;
        x += ft * (i - m->collapse_from + 1) / 4;   // 越高的箱子甩得越远
    }
    *dx = m->collapse_dir * x;
    *dy = y;
}

// ------------------------------------------------------------------ 内部

static void update_hook(cs_model_t *m) {
    const int32_t dx = (int32_t)(((int64_t)m->swing_amp * cs_sin_q15(m->swing_phase)) >> 15);
    const uint32_t rope = (uint32_t)CS_ROPE_LEN * CS_ONE;
    const uint32_t rope_y = isqrt32(rope * rope - (uint32_t)(dx * dx));   // Q8
    int32_t top = CS_PIVOT_Y * CS_ONE + (int32_t)rope_y;
    if (m->state == CS_ST_SPAWN) {
        // 新箱子从高处放下,先快后慢
        const int32_t left = CS_SPAWN_TICKS - (m->timer > CS_SPAWN_TICKS ? CS_SPAWN_TICKS : m->timer);
        top -= CS_SPAWN_LIFT * CS_ONE * left / CS_SPAWN_TICKS * left / CS_SPAWN_TICKS;
    }
    const int32_t prev_x = m->box_x;
    m->box_x = CS_CENTER_X * CS_ONE + dx;
    m->box_y = CS_GROUND_SCREEN_Y * CS_ONE - (top + CS_BOX_H * CS_ONE) + m->cam;
    m->vx = m->box_x - prev_x;
}

static void spawn(cs_model_t *m) {
    m->state = CS_ST_SPAWN;
    m->timer = 0;
    m->swing_amp = cs_swing_amp_px(m->count) * CS_ONE;
    m->swing_step = phase_step(cs_swing_period_ms(m->count));
    // 从左或右的最远端开始摆,避免一出现就在正中
    m->swing_phase = (next_rand(m) & 1u) ? QUARTER_TURN : 3u * QUARTER_TURN;
    m->vy = 0;
    update_hook(m);
    m->vx = 0;
}

static int32_t cam_target(const cs_model_t *m) {
    const int32_t t = m->count * CS_BOX_H - (CS_GROUND_SCREEN_Y - CS_TOP_SCREEN_Y);
    return t > 0 ? t * CS_ONE : 0;
}

static int32_t sway_floor(const cs_model_t *m) {
    if (m->count == 0) return 0;
    int32_t sum = 0;
    for (int i = 0; i < m->count; i++) sum += m->layer_x[i] - CS_CENTER_X * CS_ONE;
    const int32_t lean = iabs(sum / m->count) / 8;          // 重心每偏 8px,基线 +1px
    const int32_t height = (m->count / 5) * CS_ONE;          // 每 5 层,基线 +1px
    return (height > 8 * CS_ONE ? 8 * CS_ONE : height) + (lean > 6 * CS_ONE ? 6 * CS_ONE : lean);
}

// 找出最低的失稳层;返回 true 表示要倒塌。
static bool check_collapse(cs_model_t *m) {
    int32_t sum = 0;
    int fail_support = -2;
    int32_t fail_dir = 0;
    for (int j = m->count - 1; j >= 0; j--) {
        sum += m->layer_x[j];
        const int32_t com = sum / (m->count - j);
        const int32_t support = j > 0 ? m->layer_x[j - 1] : CS_CENTER_X * CS_ONE;
        const int32_t half = (j > 0 ? CS_BOX_W : CS_BASE_W) * CS_ONE / 2;
        if (iabs(com - support) > half) {
            fail_support = j - 1;
            fail_dir = com > support ? 1 : -1;
        }
    }
    if (fail_support < -1) return false;
    m->collapse_from = (int16_t)(fail_support + 1);
    m->collapse_dir = (int8_t)fail_dir;
    return true;
}

static uint32_t finish(cs_model_t *m, bool collapse) {
    m->state = collapse ? CS_ST_COLLAPSE : CS_ST_OVER;
    m->timer = 0;
    m->over_by_collapse = collapse;
    return collapse ? CS_EV_COLLAPSE : CS_EV_OVER;
}

static uint32_t resolve_landing(cs_model_t *m) {
    const int32_t top_x = cs_top_x(m);
    const int32_t half = (m->count ? CS_BOX_W : CS_BASE_W) * CS_ONE / 2;
    const int32_t d = m->box_x - top_x;
    m->last_offset = d;
    m->box_y = m->count * CS_BOX_H * CS_ONE;

    if (iabs(d) >= half) {
        m->last_land = CS_LAND_MISS;
        m->combo = 0;
        m->state = CS_ST_MISS;
        m->timer = 0;
        // 压在边上的箱子翻出去;完全没碰到的保持原速继续往下掉
        if (iabs(d) < (m->count ? CS_BOX_W : (CS_BOX_W + CS_BASE_W) / 2) * CS_ONE) {
            m->vx = d > 0 ? TIP_SPEED : -TIP_SPEED;
        }
        return CS_EV_MISS;
    }

    if (m->count >= CS_MAX_LAYERS) return finish(m, false);

    uint32_t ev = CS_EV_LAND;
    int32_t x;
    if (iabs(d) <= CS_PERFECT_PX * CS_ONE) {
        x = m->count ? m->layer_x[m->count - 1] : CS_CENTER_X * CS_ONE;
        m->last_land = CS_LAND_PERFECT;
        m->perfects++;
        m->combo++;
        if (m->combo > m->best_combo) m->best_combo = m->combo;
        m->score += m->combo >= 3 ? 3u : 2u;
        m->sway_amp /= 2;
        ev |= CS_EV_PERFECT;
        if (m->combo % CS_LIFE_COMBO == 0 && m->lives < CS_MAX_LIVES) {
            m->lives++;
            ev |= CS_EV_LIFE_GAIN;
        }
    } else {
        // 记录为不含晃动的位置,晃动只是整体剪切
        x = m->box_x - (m->count ? cs_sway(m) : 0);
        m->last_land = CS_LAND_OK;
        m->combo = 0;
        m->score += 1;
        m->sway_amp = m->sway_amp * 3 / 4 + iabs(d) / 2;
    }
    if (m->sway_amp > SWAY_MAX) m->sway_amp = SWAY_MAX;
    m->layer_x[m->count++] = x;

    if (check_collapse(m)) return ev | finish(m, true);
    spawn(m);
    return ev;
}

// ------------------------------------------------------------------ 对外接口

void cs_model_init(cs_model_t *m, uint32_t seed) {
    *m = (cs_model_t){ .rng = seed ? seed : 0x9E3779B9u, .lives = CS_MAX_LIVES };
    spawn(m);
}

void cs_model_init_demo(cs_model_t *m, uint32_t seed) {
    static const int8_t OFFSETS[] = { 0, 6, -3 };
    cs_model_init(m, seed);
    for (unsigned i = 0; i < sizeof(OFFSETS); i++) {
        m->layer_x[m->count++] = (CS_CENTER_X + OFFSETS[i]) * CS_ONE;
    }
    m->sway_amp = 3 * CS_ONE;
    spawn(m);
}

uint32_t cs_model_release(cs_model_t *m) {
    if (m->state != CS_ST_HANG) return 0;
    m->state = CS_ST_FALL;
    m->timer = 0;
    m->vx = m->vx * KEEP_NUM / KEEP_DEN;
    m->vy = 0;
    return CS_EV_RELEASE;
}

uint32_t cs_model_step(cs_model_t *m) {
    if (m->timer < UINT16_MAX) m->timer++;

    const int32_t diff = cam_target(m) - m->cam;
    const int32_t dc = diff / CAM_EASE;
    m->cam = dc ? m->cam + dc : cam_target(m);

    if (m->state != CS_ST_COLLAPSE && m->state != CS_ST_OVER) {
        m->sway_phase += phase_step(SWAY_PERIOD_MS);
        m->sway_amp += (sway_floor(m) - m->sway_amp) / SWAY_EASE;
    }

    uint32_t ev = 0;
    switch (m->state) {
    case CS_ST_SPAWN:
        m->swing_phase += m->swing_step;
        update_hook(m);
        if (m->timer >= CS_SPAWN_TICKS) {
            m->state = CS_ST_HANG;
            m->timer = 0;
            ev |= CS_EV_HANG;
        }
        break;
    case CS_ST_HANG:
        m->swing_phase += m->swing_step;
        update_hook(m);
        break;
    case CS_ST_FALL:
        m->vy -= GRAVITY;
        m->box_x += m->vx;
        m->box_y += m->vy;
        if (m->box_y <= m->count * CS_BOX_H * CS_ONE) ev |= resolve_landing(m);
        break;
    case CS_ST_MISS:
        m->vy -= GRAVITY;
        m->box_x += m->vx;
        m->box_y += m->vy;
        if (m->timer >= CS_MISS_TICKS) {
            m->lives--;
            ev |= CS_EV_LIFE_LOST;
            if (m->lives == 0) ev |= finish(m, false);
            else spawn(m);
        }
        break;
    case CS_ST_COLLAPSE:
        if (m->timer >= CS_COLLAPSE_TICKS) {
            m->state = CS_ST_OVER;
            ev |= CS_EV_OVER;
        }
        break;
    case CS_ST_OVER:
        break;
    }
    return ev;
}
