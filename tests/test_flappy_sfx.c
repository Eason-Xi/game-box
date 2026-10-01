// tests/test_flappy_sfx.c —— 音效合成的主机测试:时长、分块一致性、幅度与首尾静音。
#include "flappy_sfx.h"

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

#define MAX_SAMPLES (FB_SFX_RATE * 2)

static size_t render_all(fb_sfx_t id, int16_t *out, size_t chunk) {
    fb_sfx_player_t p;
    fb_sfx_start(&p, id);
    size_t total = 0;
    for (;;) {
        size_t n = fb_sfx_render(&p, out + total, chunk);
        if (n == 0) break;
        total += n;
        if (total + chunk > MAX_SAMPLES) break;
    }
    CHECK(fb_sfx_done(&p));
    return total;
}

static void test_each_effect(void) {
    static int16_t a[MAX_SAMPLES];
    static int16_t b[MAX_SAMPLES];
    for (int id = 0; id < FB_SFX_COUNT; id++) {
        const uint32_t expected = fb_sfx_total_samples((fb_sfx_t)id);
        CHECK(expected > 0 && expected < FB_SFX_RATE);     // 都是 1 秒以内的短音效
        memset(a, 0x55, sizeof(a));
        memset(b, 0x55, sizeof(b));
        const size_t na = render_all((fb_sfx_t)id, a, 240);
        const size_t nb = render_all((fb_sfx_t)id, b, 7);   // 奇怪的块大小结果必须一致
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
    fb_sfx_player_t p;
    int16_t buf[16];
    fb_sfx_start(&p, (fb_sfx_t)99);
    CHECK(fb_sfx_done(&p));
    CHECK(fb_sfx_render(&p, buf, 16) == 0);
    CHECK(fb_sfx_total_samples((fb_sfx_t)99) == 0);

    fb_sfx_start(&p, FB_SFX_MENU);
    CHECK(!fb_sfx_done(&p));
    CHECK(fb_sfx_render(&p, buf, 0) == 0);                 // 0 长度请求不推进
    CHECK(!fb_sfx_done(&p));
}

static void test_feedback_classes(void) {
    CHECK(fb_sfx_is_feedback(FB_SFX_FLAP));
    CHECK(fb_sfx_is_feedback(FB_SFX_MENU));
    CHECK(!fb_sfx_is_feedback(FB_SFX_SCORE));
    CHECK(!fb_sfx_is_feedback(FB_SFX_DIE));
    CHECK(!fb_sfx_is_feedback(FB_SFX_RECORD));
    // 撞管音效包含撞击段,比单纯撞地更长
    CHECK(fb_sfx_total_samples(FB_SFX_DIE) > fb_sfx_total_samples(FB_SFX_HIT));
}

static void test_flap_is_short(void) {
    // 扇翅音效必须短于两次扇翅的最小间隔感知,否则会堆积延迟
    CHECK(fb_sfx_total_samples(FB_SFX_FLAP) <= FB_SFX_RATE / 10);
}

int main(void) {
    test_each_effect();
    test_invalid_and_done();
    test_flap_is_short();
    test_feedback_classes();
    if (s_failures) {
        fprintf(stderr, "test_flappy_sfx: %d failure(s)\n", s_failures);
        return 1;
    }
    puts("test_flappy_sfx: PASS");
    return 0;
}
