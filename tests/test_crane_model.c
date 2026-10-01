// tests/test_crane_model.c —— 吊车堆箱子纯模型的主机测试。
#include "crane_model.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int s_failures;

#define CHECK(cond) do { \
    if (!(cond)) { \
        fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
        s_failures++; \
    } \
} while (0)

static cs_model_t s_m;      // 模型带 400 层数组,放静态区
static cs_model_t s_probe;

static int px(int32_t q8) { return (int)(q8 >> CS_Q); }

static uint32_t step_n(cs_model_t *m, int n) {
    uint32_t ev = 0;
    for (int i = 0; i < n; i++) ev |= cs_model_step(m);
    return ev;
}

static void wait_hang(cs_model_t *m) {
    for (int i = 0; i < 1000 && m->state != CS_ST_HANG; i++) cs_model_step(m);
}

// 在副本上模拟"现在松钩",返回落点误差(Q8);没落到塔顶(或落点之后状态
// 异常)时返回 INT32_MAX。
static int32_t predict_offset(const cs_model_t *m) {
    s_probe = *m;
    if (!cs_model_release(&s_probe)) return INT32_MAX;
    for (int i = 0; i < 2000; i++) {
        const uint32_t ev = cs_model_step(&s_probe);
        if (ev & (CS_EV_LAND | CS_EV_MISS)) return s_probe.last_offset;
    }
    return INT32_MAX;
}

// 自动驾驶:等到预测落点误差落在 [lo, hi](Q8)时松钩,返回本次事件位。
static uint32_t drop_with_offset(cs_model_t *m, int32_t lo, int32_t hi) {
    wait_hang(m);
    for (int i = 0; i < 4000 && m->state == CS_ST_HANG; i++) {
        const int32_t d = predict_offset(m);
        if (d >= lo && d <= hi) {
            CHECK(cs_model_release(m) == CS_EV_RELEASE);
            uint32_t ev = 0;
            for (int k = 0; k < 2000 && !(ev & (CS_EV_LAND | CS_EV_MISS)); k++) ev |= cs_model_step(m);
            return ev;
        }
        cs_model_step(m);
    }
    return 0;
}

static uint32_t drop_perfect(cs_model_t *m) {
    return drop_with_offset(m, -CS_ONE, CS_ONE);
}

static void test_sin(void) {
    CHECK(cs_sin_q15(0) == 0);
    CHECK(cs_sin_q15(0x40000000u) == 32767);
    CHECK(cs_sin_q15(0x80000000u) == 0);
    CHECK(cs_sin_q15(0xC0000000u) == -32767);
    // 30° ≈ 0.5,允许插值误差
    const int32_t s30 = cs_sin_q15(0x40000000u / 3u);
    CHECK(abs(s30 - 16384) < 40);
    // 奇函数与连续性
    for (uint32_t p = 0; p < 0x40000000u; p += 0x00F00000u) {
        CHECK(cs_sin_q15(p) == -cs_sin_q15(0u - p) || p == 0);
        CHECK(abs(cs_sin_q15(p + 0x00100000u) - cs_sin_q15(p)) < 200);
    }
}

static void test_init_and_spawn(void) {
    cs_model_init(&s_m, 42);
    CHECK(s_m.state == CS_ST_SPAWN);
    CHECK(s_m.lives == CS_MAX_LIVES);
    CHECK(s_m.count == 0 && s_m.score == 0 && s_m.cam == 0);
    CHECK(cs_box_hooked(&s_m) && cs_box_visible(&s_m));
    // 下放中不能松钩
    CHECK(cs_model_release(&s_m) == 0);
    const int y0 = px(s_m.box_y);
    uint32_t ev = step_n(&s_m, CS_SPAWN_TICKS - 1);
    CHECK(!(ev & CS_EV_HANG));
    CHECK(px(s_m.box_y) < y0);                // 箱子在往下放
    ev = cs_model_step(&s_m);
    CHECK(ev & CS_EV_HANG);
    CHECK(s_m.state == CS_ST_HANG);
    // 就位后:箱子底边在屏幕上方约 120px 处,离塔顶有足够的下落距离
    const int bottom_screen = px(cs_world_to_screen(&s_m, s_m.box_y));
    CHECK(bottom_screen > 100 && bottom_screen < 130);
}

static void test_swing_bounds(void) {
    for (uint16_t count = 0; count <= 120; count += 5) {
        cs_model_init(&s_m, 7);
        s_m.count = count;
        for (int i = 0; i < count; i++) s_m.layer_x[i] = CS_CENTER_X * CS_ONE;
        // 按这个层数重设摆动参数(与 spawn() 相同的公式)
        s_m.swing_amp = cs_swing_amp_px(count) * CS_ONE;
        s_m.swing_step = (uint32_t)(4294967296ULL * CS_TICK_MS / cs_swing_period_ms(count));
        int lo = 1000, hi = -1000;
        wait_hang(&s_m);
        const int ticks = (int)(cs_swing_period_ms(count) / CS_TICK_MS) + 2;
        for (int i = 0; i < ticks; i++) {
            cs_model_step(&s_m);
            const int x = px(s_m.box_x);
            if (x < lo) lo = x;
            if (x > hi) hi = x;
        }
        const int amp = cs_swing_amp_px(count);
        CHECK(lo - CS_BOX_W / 2 >= 0);
        CHECK(hi + CS_BOX_W / 2 <= CS_SCREEN_W);
        CHECK(abs(lo - (CS_CENTER_X - amp)) <= 2);
        CHECK(abs(hi - (CS_CENTER_X + amp)) <= 2);
    }
}

static void test_difficulty_curve(void) {
    uint32_t prev_period = 0xFFFFFFFFu;
    int prev_amp = 0;
    for (uint16_t c = 0; c < 300; c++) {
        const uint32_t p = cs_swing_period_ms(c);
        const int a = cs_swing_amp_px(c);
        CHECK(p <= prev_period && p >= 1700 && p <= 2600);
        CHECK(a >= prev_amp && a >= 56 && a <= 78);
        prev_period = p;
        prev_amp = a;
    }
    CHECK(cs_swing_period_ms(0) == 2600);
    CHECK(cs_swing_period_ms(1000) == 1700);
}

static void test_release_and_perfect(void) {
    cs_model_init(&s_m, 3);
    const uint32_t ev = drop_perfect(&s_m);
    CHECK(ev & CS_EV_LAND);
    CHECK(ev & CS_EV_PERFECT);
    CHECK(s_m.count == 1);
    CHECK(s_m.layer_x[0] == CS_CENTER_X * CS_ONE);   // 完美落下对齐地基中心
    CHECK(s_m.score == 2);
    CHECK(s_m.combo == 1 && s_m.perfects == 1);
    CHECK(s_m.state == CS_ST_SPAWN);
    // 下落中再按无效
    wait_hang(&s_m);
    CHECK(cs_model_release(&s_m) == CS_EV_RELEASE);
    CHECK(cs_model_release(&s_m) == 0);
}

static void test_momentum(void) {
    // 在摆到正中、速度最大时松钩,箱子要沿摆动方向漂移
    cs_model_init(&s_m, 11);
    wait_hang(&s_m);
    for (int i = 0; i < 2000; i++) {
        if (abs(px(s_m.box_x) - CS_CENTER_X) <= 1 && abs(s_m.vx) > CS_ONE / 2) break;
        cs_model_step(&s_m);
    }
    const int32_t x0 = s_m.box_x;
    const int32_t dir = s_m.vx > 0 ? 1 : -1;
    cs_model_release(&s_m);
    uint32_t ev = 0;
    for (int i = 0; i < 2000 && !(ev & (CS_EV_LAND | CS_EV_MISS)); i++) ev |= cs_model_step(&s_m);
    const int drift = (int)(dir * (s_m.last_offset + CS_CENTER_X * CS_ONE - x0) / CS_ONE);
    CHECK(drift > 5);
    CHECK(drift < 40);     // 只保留一部分速度,不至于飞出塔顶
}

static void test_ok_landing_keeps_offset(void) {
    cs_model_init(&s_m, 5);
    const uint32_t ev = drop_with_offset(&s_m, 10 * CS_ONE, 14 * CS_ONE);
    CHECK(ev & CS_EV_LAND);
    CHECK(!(ev & CS_EV_PERFECT));
    CHECK(s_m.last_land == CS_LAND_OK);
    CHECK(s_m.score == 1 && s_m.combo == 0);
    const int off = px(s_m.layer_x[0]) - CS_CENTER_X;
    CHECK(off >= 10 && off <= 14);
    CHECK(s_m.sway_amp > 0);               // 放歪了塔会晃
}

static void test_miss_and_game_over(void) {
    cs_model_init(&s_m, 9);
    CHECK(drop_perfect(&s_m) & CS_EV_LAND);
    for (int life = CS_MAX_LIVES; life > 0; life--) {
        const uint32_t ev = drop_with_offset(&s_m, 40 * CS_ONE, 60 * CS_ONE);
        CHECK(ev & CS_EV_MISS);
        CHECK(s_m.state == CS_ST_MISS);
        CHECK(s_m.vx > 0);                 // 压在右边缘,向右翻出去
        CHECK(s_m.count == 1);
        const uint32_t ev2 = step_n(&s_m, CS_MISS_TICKS);
        CHECK(ev2 & CS_EV_LIFE_LOST);
        CHECK(s_m.lives == life - 1);
        if (life > 1) {
            CHECK(!(ev2 & CS_EV_OVER));
            CHECK(s_m.state == CS_ST_SPAWN);
        } else {
            CHECK(ev2 & CS_EV_OVER);
            CHECK(s_m.state == CS_ST_OVER);
            CHECK(!s_m.over_by_collapse);
        }
    }
    CHECK(cs_model_release(&s_m) == 0);
    CHECK(step_n(&s_m, 100) == 0);
}

static void test_collapse(void) {
    // 连续三块都往右偏 18px:第三块落下后,最底层以上的合重心超出边缘
    cs_model_init(&s_m, 13);
    CHECK(drop_perfect(&s_m) & CS_EV_LAND);
    uint32_t ev = 0;
    for (int i = 0; i < 3 && !(ev & CS_EV_COLLAPSE); i++) {
        ev = drop_with_offset(&s_m, 17 * CS_ONE, 19 * CS_ONE);
        CHECK(ev & CS_EV_LAND);
    }
    CHECK(ev & CS_EV_COLLAPSE);
    CHECK(s_m.state == CS_ST_COLLAPSE);
    CHECK(s_m.collapse_dir == 1);
    CHECK(s_m.collapse_from >= 1 && s_m.collapse_from < s_m.count);
    CHECK(!cs_box_visible(&s_m));
    int dx, dy;
    cs_collapse_offset(&s_m, s_m.count - 1, &dx, &dy);
    CHECK(dx == 0 && dy == 0);
    step_n(&s_m, CS_COLLAPSE_TICKS / 2);
    cs_collapse_offset(&s_m, s_m.count - 1, &dx, &dy);
    CHECK(dx > 0 && dy > 0);               // 向右倒、往下掉
    cs_collapse_offset(&s_m, 0, &dx, &dy);
    CHECK(dx == 0 && dy == 0);             // 支撑层以下不动
    ev = step_n(&s_m, CS_COLLAPSE_TICKS);
    CHECK(ev & CS_EV_OVER);
    CHECK(s_m.state == CS_ST_OVER && s_m.over_by_collapse);
}

static void test_alternating_offsets_stay_up(void) {
    // 左右交替偏 12px,重心始终回到中间,不应倒塌
    cs_model_init(&s_m, 17);
    for (int i = 0; i < 12; i++) {
        const int32_t sign = (i & 1) ? -1 : 1;
        const int32_t lo = sign > 0 ? 11 * CS_ONE : -13 * CS_ONE;
        const int32_t hi = sign > 0 ? 13 * CS_ONE : -11 * CS_ONE;
        const uint32_t ev = drop_with_offset(&s_m, lo, hi);
        CHECK(ev & CS_EV_LAND);
        CHECK(!(ev & CS_EV_COLLAPSE));
    }
    CHECK(s_m.count == 12);
}

static void test_life_bonus(void) {
    cs_model_init(&s_m, 21);
    // 第一块的支撑面是地基(半宽 48px)
    CHECK(drop_with_offset(&s_m, 52 * CS_ONE, 70 * CS_ONE) & CS_EV_MISS);
    step_n(&s_m, CS_MISS_TICKS);
    CHECK(s_m.lives == CS_MAX_LIVES - 1);
    uint32_t all = 0;
    for (int i = 0; i < CS_LIFE_COMBO; i++) all |= drop_perfect(&s_m);
    CHECK(all & CS_EV_LIFE_GAIN);
    CHECK(s_m.lives == CS_MAX_LIVES);
    CHECK(s_m.combo == CS_LIFE_COMBO);
    // 连击 3 起每块 3 分:2+2+3+3+3
    CHECK(s_m.score == 13);
    // 满命时不再加
    for (int i = 0; i < CS_LIFE_COMBO; i++) CHECK(!(drop_perfect(&s_m) & CS_EV_LIFE_GAIN));
    CHECK(s_m.lives == CS_MAX_LIVES);
}

static void test_camera_and_long_run(void) {
    // 自动驾驶一路完美堆到 80 层:验证最高难度下仍有可达的完美窗口、镜头跟随
    cs_model_init(&s_m, 1234);
    for (int i = 0; i < 80; i++) {
        const uint32_t ev = drop_perfect(&s_m);
        CHECK(ev & CS_EV_PERFECT);
        if (!(ev & CS_EV_PERFECT)) break;
        wait_hang(&s_m);
        // 镜头就位后,塔顶面固定在 CS_TOP_SCREEN_Y(塔矮时停在地面)
        const int top_screen = px(cs_world_to_screen(&s_m, s_m.count * CS_BOX_H * CS_ONE));
        if (s_m.count * CS_BOX_H > CS_GROUND_SCREEN_Y - CS_TOP_SCREEN_Y) {
            CHECK(abs(top_screen - CS_TOP_SCREEN_Y) <= 1);
        } else {
            CHECK(s_m.cam == 0);
        }
        // 箱子底边始终在塔顶上方
        CHECK(px(cs_world_to_screen(&s_m, s_m.box_y)) < top_screen - 40);
    }
    CHECK(s_m.count == 80);
    CHECK(s_m.lives == CS_MAX_LIVES);
}

static void test_sway_shear(void) {
    cs_model_init(&s_m, 31);
    s_m.count = 10;
    for (int i = 0; i < 10; i++) s_m.layer_x[i] = CS_CENTER_X * CS_ONE;
    s_m.sway_amp = 8 * CS_ONE;
    s_m.sway_phase = 0x40000000u;          // 晃到最右
    CHECK(px(cs_sway(&s_m)) == 7 || px(cs_sway(&s_m)) == 8);
    CHECK(cs_top_x(&s_m) == cs_layer_x(&s_m, 9));
    CHECK(cs_layer_x(&s_m, 0) < cs_layer_x(&s_m, 5));
    CHECK(cs_layer_x(&s_m, 5) < cs_layer_x(&s_m, 9));
    // 晃动随时间衰减到基线(10 层、重心居中 → 基线 2px)
    s_m.state = CS_ST_HANG;
    step_n(&s_m, 4000);
    CHECK(s_m.sway_amp <= 4 * CS_ONE);
    CHECK(s_m.sway_amp >= 1 * CS_ONE);
}

static void test_determinism(void) {
    static cs_model_t a, b;
    cs_model_init(&a, 77);
    cs_model_init(&b, 77);
    for (int i = 0; i < 6; i++) {
        drop_with_offset(&a, -6 * CS_ONE, 6 * CS_ONE);
        drop_with_offset(&b, -6 * CS_ONE, 6 * CS_ONE);
    }
    CHECK(memcmp(&a, &b, sizeof a) == 0);
}

int main(void) {
    test_sin();
    test_init_and_spawn();
    test_swing_bounds();
    test_difficulty_curve();
    test_release_and_perfect();
    test_momentum();
    test_ok_landing_keeps_offset();
    test_miss_and_game_over();
    test_collapse();
    test_alternating_offsets_stay_up();
    test_life_bonus();
    test_camera_and_long_run();
    test_sway_shear();
    test_determinism();
    if (s_failures) {
        fprintf(stderr, "test_crane_model: %d failure(s)\n", s_failures);
        return 1;
    }
    printf("test_crane_model: OK\n");
    return 0;
}
