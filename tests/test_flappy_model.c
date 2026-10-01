// tests/test_flappy_model.c —— 飞吧小鸟纯模型的主机测试。
#include "flappy_model.h"

#include <stdio.h>
#include <stdlib.h>

static int s_failures;

#define CHECK(cond) do { \
    if (!(cond)) { \
        fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
        s_failures++; \
    } \
} while (0)

static int bird_y(const fb_model_t *m) { return (int)(m->bird_y_q8 >> FB_Q); }

static void check_gap_bounds(const fb_model_t *m, const fb_pipe_t *p) {
    const int top = fb_pipe_gap_top(m, p);
    const int bottom = fb_pipe_gap_bottom(m, p);
    CHECK(top >= FB_GAP_MARGIN);
    CHECK(bottom <= FB_GROUND_Y - FB_GAP_MARGIN);
    CHECK(bottom - top == fb_params(m->difficulty)->gap_h);
}

static void test_init(void) {
    fb_model_t m;
    fb_model_init(&m, FB_DIFF_NORMAL, 42);
    CHECK(m.state == FB_STATE_READY);
    CHECK(bird_y(&m) == FB_BIRD_START_Y);
    CHECK(m.score == 0);
    for (int i = 0; i < FB_PIPE_COUNT; i++) {
        CHECK((m.pipes[i].x_q8 >> FB_Q) >= FB_FIELD_W);   // 开局管道都在屏幕外
        check_gap_bounds(&m, &m.pipes[i]);
        if (i > 0) CHECK(m.pipes[i].x_q8 - m.pipes[i - 1].x_q8 == FB_PIPE_SPACING * FB_ONE);
    }
    // 非法难度回落到普通
    fb_model_init(&m, (fb_difficulty_t)9, 1);
    CHECK(m.difficulty == FB_DIFF_NORMAL);
}

static void test_ready_bob_and_start(void) {
    fb_model_t m;
    fb_model_init(&m, FB_DIFF_EASY, 7);
    const int32_t x0 = m.pipes[0].x_q8;
    int lo = 1000, hi = -1000;
    for (int i = 0; i < 200; i++) {
        CHECK(fb_model_step(&m) == 0);
        if (bird_y(&m) < lo) lo = bird_y(&m);
        if (bird_y(&m) > hi) hi = bird_y(&m);
    }
    CHECK(m.state == FB_STATE_READY);
    CHECK(m.pipes[0].x_q8 == x0);          // 准备阶段管道不动
    CHECK(m.ground_q8 > 0);                // 地面仍在滚动
    CHECK(lo >= FB_BIRD_START_Y - 5 && hi <= FB_BIRD_START_Y + 5 && hi > lo);

    CHECK(fb_model_flap(&m) == FB_EV_FLAP);
    CHECK(m.state == FB_STATE_PLAYING);
    CHECK(m.vel_q8 == FB_FLAP_VELOCITY);
}

static void test_flap_arc(void) {
    fb_model_t m;
    fb_model_init(&m, FB_DIFF_NORMAL, 3);
    fb_model_flap(&m);
    const int start = bird_y(&m);
    int apex = start;
    int ticks_to_apex = 0;
    for (int i = 0; i < 40; i++) {
        fb_model_step(&m);
        if (bird_y(&m) < apex) { apex = bird_y(&m); ticks_to_apex = i + 1; }
    }
    // 一次扇翅约升高 40px,约 0.3 秒到顶
    CHECK(start - apex >= 36 && start - apex <= 48);
    CHECK(ticks_to_apex >= 15 && ticks_to_apex <= 21);
}

static void test_fall_to_ground(void) {
    fb_model_t m;
    fb_model_init(&m, FB_DIFF_NORMAL, 3);
    fb_model_flap(&m);
    uint32_t events = 0;
    int ticks = 0;
    while (m.state == FB_STATE_PLAYING && ticks < 1000) {
        events = fb_model_step(&m);
        ticks++;
        CHECK(m.vel_q8 <= FB_MAX_FALL);
    }
    CHECK(m.state == FB_STATE_OVER);
    CHECK(events == (FB_EV_HIT | FB_EV_LAND));
    CHECK(bird_y(&m) == FB_GROUND_Y - FB_BIRD_HALF_H);
    CHECK(ticks < 120);                     // 两秒内落地
    CHECK(fb_model_flap(&m) == 0);          // 结束后不能再扇翅
    CHECK(fb_model_step(&m) == 0);
}

static void test_ceiling_clamp(void) {
    fb_model_t m;
    fb_model_init(&m, FB_DIFF_EASY, 3);
    for (int i = 0; i < 90; i++) {
        if (i % 4 == 0) fb_model_flap(&m);
        fb_model_step(&m);
        CHECK(bird_y(&m) >= FB_BIRD_HALF_H);
    }
    // 管道还没到达小鸟,顶到天花板不应致死
    CHECK(m.state == FB_STATE_PLAYING);
}

static void test_pipe_hit_then_land(void) {
    fb_model_t m;
    fb_model_init(&m, FB_DIFF_NORMAL, 5);
    fb_model_flap(&m);
    // 把一根管道直接放到小鸟面前,缺口在最底部
    m.pipes[0].x_q8 = (FB_BIRD_X + FB_BIRD_HALF_W) * FB_ONE;
    m.pipes[0].gap_y = (int16_t)(FB_GROUND_Y - FB_GAP_MARGIN - fb_params(m.difficulty)->gap_h / 2);
    m.bird_y_q8 = 60 * FB_ONE;
    uint32_t ev = fb_model_step(&m);
    CHECK(ev == FB_EV_HIT);
    CHECK(m.state == FB_STATE_DYING);
    CHECK(m.vel_q8 <= FB_HIT_BOUNCE);
    const int32_t frozen_x = m.pipes[0].x_q8;
    const uint32_t frozen_ground = m.ground_q8;
    CHECK(fb_model_flap(&m) == 0);
    uint32_t all = 0;
    for (int i = 0; i < 200 && m.state == FB_STATE_DYING; i++) all |= fb_model_step(&m);
    CHECK(m.state == FB_STATE_OVER);
    CHECK(all == FB_EV_LAND);
    CHECK(m.pipes[0].x_q8 == frozen_x);     // 坠落时管道与地面冻结
    CHECK(m.ground_q8 == frozen_ground);
    CHECK(fb_bird_wing_frame(&m) == 1);
}

static void test_graze_is_safe(void) {
    // 碰撞盒贴着缺口上下沿时不算撞
    fb_model_t m;
    fb_model_init(&m, FB_DIFF_NORMAL, 5);
    fb_model_flap(&m);
    fb_pipe_t *p = &m.pipes[0];
    p->x_q8 = (FB_BIRD_X - FB_PIPE_W / 2) * FB_ONE;
    p->gap_y = 150;
    m.vel_q8 = -FB_GRAVITY;   // 本 tick 结束时速度为 0,位置不变
    m.bird_y_q8 = (fb_pipe_gap_top(&m, p) + FB_BIRD_HALF_H) * FB_ONE;
    CHECK((fb_model_step(&m) & FB_EV_HIT) == 0);
    CHECK(m.state == FB_STATE_PLAYING);
    m.vel_q8 = -FB_GRAVITY;
    m.bird_y_q8 = (fb_pipe_gap_top(&m, p) + FB_BIRD_HALF_H - 1) * FB_ONE;
    CHECK(fb_model_step(&m) == FB_EV_HIT);
}

// 简单自动驾驶:快要低于目标高度时扇翅。验证每个难度的缺口都能飞过,
// 同时验证计分、回收后的间距与缺口约束。
static uint32_t autopilot(fb_difficulty_t diff, uint32_t seed, int max_ticks) {
    fb_model_t m;
    fb_model_init(&m, diff, seed);
    fb_model_flap(&m);
    uint32_t scored_events = 0;
    for (int t = 0; t < max_ticks && m.state == FB_STATE_PLAYING; t++) {
        const fb_pipe_t *next = NULL;
        for (int i = 0; i < FB_PIPE_COUNT; i++) {
            const fb_pipe_t *p = &m.pipes[i];
            if ((p->x_q8 >> FB_Q) + FB_PIPE_W < FB_BIRD_X - FB_BIRD_HALF_W) continue;
            if (!next || p->x_q8 < next->x_q8) next = p;
        }
        // 预判下一 tick 的位置,离缺口下沿不足 12px 就扇翅(允许连续扇翅爬升)
        const int target = fb_pipe_gap_bottom(&m, next) - 12;
        const int predicted = bird_y(&m) + (int)(m.vel_q8 >> FB_Q);
        if (predicted > target) fb_model_flap(&m);
        uint32_t ev = fb_model_step(&m);
        if (ev & FB_EV_SCORE) scored_events++;
        for (int i = 0; i < FB_PIPE_COUNT; i++) check_gap_bounds(&m, &m.pipes[i]);
    }
    CHECK(scored_events == m.score);
    // 回收后三根管道仍保持等间距
    int32_t xs[FB_PIPE_COUNT];
    for (int i = 0; i < FB_PIPE_COUNT; i++) xs[i] = m.pipes[i].x_q8;
    for (int i = 0; i < FB_PIPE_COUNT; i++)
        for (int j = i + 1; j < FB_PIPE_COUNT; j++)
            if (xs[j] < xs[i]) { int32_t t = xs[i]; xs[i] = xs[j]; xs[j] = t; }
    CHECK(xs[1] - xs[0] == FB_PIPE_SPACING * FB_ONE);
    CHECK(xs[2] - xs[1] == FB_PIPE_SPACING * FB_ONE);
    return m.score;
}

static void test_autopilot_all_difficulties(void) {
    for (int d = 0; d < FB_DIFF_COUNT; d++) {
        for (uint32_t seed = 1; seed <= 20; seed++) {
            uint32_t score = autopilot((fb_difficulty_t)d, seed * 7919u, 60 * 120);
            if (score < 50) {
                fprintf(stderr, "autopilot diff=%d seed=%u score=%u\n", d, (unsigned)seed, (unsigned)score);
            }
            CHECK(score >= 50);
        }
    }
}

static void test_adjacent_gap_shift(void) {
    for (int d = 0; d < FB_DIFF_COUNT; d++) {
        fb_model_t m;
        fb_model_init(&m, (fb_difficulty_t)d, 99);
        int prev = m.pipes[FB_PIPE_COUNT - 1].gap_y;
        for (int n = 0; n < 500; n++) {
            // 手动推动回收:把最左的管道推出屏幕
            fb_pipe_t *left = &m.pipes[0];
            for (int i = 1; i < FB_PIPE_COUNT; i++) if (m.pipes[i].x_q8 < left->x_q8) left = &m.pipes[i];
            m.state = FB_STATE_PLAYING;
            m.bird_y_q8 = 20 * FB_ONE;   // 远离管道,避免碰撞
            m.vel_q8 = -FB_GRAVITY;
            left->x_q8 = -FB_PIPE_W * FB_ONE;
            left->scored = true;
            fb_model_step(&m);
            CHECK(abs(left->gap_y - prev) <= fb_params(m.difficulty)->max_shift);
            check_gap_bounds(&m, left);
            prev = left->gap_y;
        }
    }
}

static void test_determinism(void) {
    fb_model_t a, b;
    fb_model_init(&a, FB_DIFF_HARD, 1234);
    fb_model_init(&b, FB_DIFF_HARD, 1234);
    for (int i = 0; i < FB_PIPE_COUNT; i++) CHECK(a.pipes[i].gap_y == b.pipes[i].gap_y);
    fb_model_init(&b, FB_DIFF_HARD, 4321);
    int same = 0;
    for (int i = 0; i < FB_PIPE_COUNT; i++) same += a.pipes[i].gap_y == b.pipes[i].gap_y;
    CHECK(same < FB_PIPE_COUNT);
}

static void test_speed_ramp(void) {
    fb_model_t m;
    fb_model_init(&m, FB_DIFF_NORMAL, 1);
    const int32_t base = fb_params(FB_DIFF_NORMAL)->speed_q8;
    CHECK(fb_model_speed_q8(&m) == base);
    m.score = 25;
    CHECK(fb_model_speed_q8(&m) == base + base * 8 / 100);
    m.score = 1000;
    CHECK(fb_model_speed_q8(&m) == base + base * 20 / 100);
    CHECK(fb_params(FB_DIFF_EASY)->speed_q8 < fb_params(FB_DIFF_HARD)->speed_q8);
    CHECK(fb_params(FB_DIFF_EASY)->gap_h > fb_params(FB_DIFF_HARD)->gap_h);
}

static void test_angle_and_wings(void) {
    CHECK(fb_bird_angle_deg(FB_FLAP_VELOCITY) == -25);
    CHECK(fb_bird_angle_deg(FB_MAX_FALL) == 90);
    int prev = -100;
    for (int32_t v = -2000; v <= 2000; v += 16) {
        int a = fb_bird_angle_deg(v);
        CHECK(a >= prev);
        CHECK(a >= -25 && a <= 90);
        prev = a;
    }
    fb_model_t m;
    fb_model_init(&m, FB_DIFF_NORMAL, 1);
    int seen[3] = { 0 };
    for (int i = 0; i < 40; i++) { seen[fb_bird_wing_frame(&m)] = 1; fb_model_step(&m); }
    CHECK(seen[0] && seen[1] && seen[2]);
}

static void test_medals(void) {
    CHECK(fb_medal_for(0) == FB_MEDAL_NONE);
    CHECK(fb_medal_for(9) == FB_MEDAL_NONE);
    CHECK(fb_medal_for(10) == FB_MEDAL_BRONZE);
    CHECK(fb_medal_for(20) == FB_MEDAL_SILVER);
    CHECK(fb_medal_for(39) == FB_MEDAL_GOLD);
    CHECK(fb_medal_for(40) == FB_MEDAL_PLATINUM);
}

int main(void) {
    test_init();
    test_ready_bob_and_start();
    test_flap_arc();
    test_fall_to_ground();
    test_ceiling_clamp();
    test_pipe_hit_then_land();
    test_graze_is_safe();
    test_autopilot_all_difficulties();
    test_adjacent_gap_shift();
    test_determinism();
    test_speed_ramp();
    test_angle_and_wings();
    test_medals();
    if (s_failures) {
        fprintf(stderr, "test_flappy_model: %d failure(s)\n", s_failures);
        return 1;
    }
    puts("test_flappy_model: PASS");
    return 0;
}
