// tests/test_crane_sfx.c —— 音效合成的主机测试:时长、分块一致性、幅度与首尾静音。
#include "crane_sfx.h"

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

#define MAX_SAMPLES (CS_SFX_RATE * 2)

static size_t render_all(cs_sfx_t id, int16_t *out, size_t chunk) {
    cs_sfx_player_t p;
    cs_sfx_start(&p, id);
    size_t total = 0;
    for (;;) {
        size_t n = cs_sfx_render(&p, out + total, chunk);
        if (n == 0) break;
        total += n;
        if (total + chunk > MAX_SAMPLES) break;
    }
    CHECK(cs_sfx_done(&p));
    return total;
}

static void test_each_effect(void) {
    static int16_t a[MAX_SAMPLES];
    static int16_t b[MAX_SAMPLES];
    for (int id = 0; id < CS_SFX_COUNT; id++) {
        const uint32_t expected = cs_sfx_total_samples((cs_sfx_t)id);
        CHECK(expected > 0 && expected < CS_SFX_RATE * 3 / 2);   // 都在 1.5 秒以内
        memset(a, 0x55, sizeof(a));
        memset(b, 0x55, sizeof(b));
        const size_t na = render_all((cs_sfx_t)id, a, 240);
        const size_t nb = render_all((cs_sfx_t)id, b, 7);   // 奇怪的块大小结果必须一致
        CHECK(na == expected && nb == expected);
        CHECK(memcmp(a, b, na * sizeof(int16_t)) == 0);

        int peak = 0;
        for (size_t i = 0; i < na; i++) {
            int v = abs(a[i]);
            if (v > peak) peak = v;
        }
        CHECK(peak > 1000);         // 听得见
        CHECK(peak <= 12000);       // 留出余量,不削波
        // 首尾接近 0,避免"啪"声
        CHECK(abs(a[0]) < 200);
        CHECK(abs(a[na - 1]) < 400);
    }
}

static void test_invalid_and_done(void) {
    cs_sfx_player_t p;
    int16_t buf[16];
    cs_sfx_start(&p, (cs_sfx_t)99);
    CHECK(cs_sfx_done(&p));
    CHECK(cs_sfx_render(&p, buf, 16) == 0);
    CHECK(cs_sfx_total_samples((cs_sfx_t)99) == 0);

    cs_sfx_start(&p, CS_SFX_MENU);
    CHECK(!cs_sfx_done(&p));
    CHECK(cs_sfx_render(&p, buf, 0) == 0);                 // 0 长度请求不推进
    CHECK(!cs_sfx_done(&p));
}

static void test_classes_and_combo(void) {
    CHECK(cs_sfx_is_feedback(CS_SFX_DROP));
    CHECK(cs_sfx_is_feedback(CS_SFX_MENU));
    CHECK(!cs_sfx_is_feedback(CS_SFX_LAND));
    CHECK(!cs_sfx_is_feedback(CS_SFX_PERFECT_3));
    CHECK(!cs_sfx_is_feedback(CS_SFX_COLLAPSE));
    // 松钩声很短:下落约 0.3 秒,落地声来时它早已结束
    CHECK(cs_sfx_total_samples(CS_SFX_DROP) <= CS_SFX_RATE / 10);
    // 连击音阶:1 起,5 以上封顶
    CHECK(cs_sfx_for_perfect(0) == CS_SFX_PERFECT_1);
    CHECK(cs_sfx_for_perfect(1) == CS_SFX_PERFECT_1);
    CHECK(cs_sfx_for_perfect(3) == CS_SFX_PERFECT_3);
    CHECK(cs_sfx_for_perfect(5) == CS_SFX_PERFECT_5);
    CHECK(cs_sfx_for_perfect(99) == CS_SFX_PERFECT_5);
}

// 连击音效的亮音一级比一级高:比较亮音段的过零次数
static void test_perfect_pitch_rises(void) {
    static int16_t buf[MAX_SAMPLES];
    int prev = 0;
    for (int id = CS_SFX_PERFECT_1; id <= CS_SFX_PERFECT_5; id++) {
        const size_t n = render_all((cs_sfx_t)id, buf, 160);
        // 前两段是落地声(20+50ms),之后 70ms 是亮音
        const size_t from = CS_SFX_RATE * 70 / 1000, to = CS_SFX_RATE * 140 / 1000;
        CHECK(n > to);
        int crossings = 0;
        for (size_t i = from + 1; i < to; i++) {
            if ((buf[i - 1] < 0) != (buf[i] < 0)) crossings++;
        }
        CHECK(crossings > prev);
        prev = crossings;
    }
}

int main(void) {
    test_each_effect();
    test_invalid_and_done();
    test_classes_and_combo();
    test_perfect_pitch_rises();
    if (s_failures) {
        fprintf(stderr, "test_crane_sfx: %d failure(s)\n", s_failures);
        return 1;
    }
    puts("test_crane_sfx: OK");
    return 0;
}
