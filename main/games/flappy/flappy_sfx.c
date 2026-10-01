// main/flappy_sfx.c —— 音效合成实现,见 flappy_sfx.h。
#include "flappy_sfx.h"

#define COUNT_OF(a) (sizeof(a) / sizeof((a)[0]))

#define PEAK        12000   // 满音量峰值,给 ES8311 输出留余量
#define ATTACK_MS   3
#define RELEASE_MIN 8       // 释音至少 8ms,且不短于段长的 30%

static const fb_sfx_seg_t FLAP[] = {
    { 420, 900, 70, FB_WAVE_TRIANGLE, 80 },
};
static const fb_sfx_seg_t SCORE[] = {
    { 988, 988, 60, FB_WAVE_SQUARE, 45 },
    { 1319, 1319, 120, FB_WAVE_SQUARE, 45 },
};
static const fb_sfx_seg_t HIT[] = {
    { 0, 0, 60, FB_WAVE_NOISE, 70 },
    { 220, 90, 90, FB_WAVE_SQUARE, 55 },
};
static const fb_sfx_seg_t DIE[] = {
    { 0, 0, 60, FB_WAVE_NOISE, 70 },
    { 220, 90, 90, FB_WAVE_SQUARE, 55 },
    { 0, 0, 80, FB_WAVE_REST, 0 },
    { 760, 180, 380, FB_WAVE_TRIANGLE, 75 },
};
static const fb_sfx_seg_t MENU[] = {
    { 660, 660, 35, FB_WAVE_SQUARE, 35 },
};
static const fb_sfx_seg_t CONFIRM[] = {
    { 660, 660, 45, FB_WAVE_SQUARE, 40 },
    { 990, 990, 70, FB_WAVE_SQUARE, 40 },
};
static const fb_sfx_seg_t RECORD[] = {
    { 523, 523, 90, FB_WAVE_SQUARE, 40 },
    { 659, 659, 90, FB_WAVE_SQUARE, 40 },
    { 784, 784, 90, FB_WAVE_SQUARE, 40 },
    { 1047, 1047, 220, FB_WAVE_SQUARE, 40 },
};

typedef struct {
    const fb_sfx_seg_t *segs;
    uint8_t count;
} sfx_def_t;

static const sfx_def_t DEFS[FB_SFX_COUNT] = {
    [FB_SFX_FLAP]    = { FLAP, COUNT_OF(FLAP) },
    [FB_SFX_SCORE]   = { SCORE, COUNT_OF(SCORE) },
    [FB_SFX_HIT]     = { HIT, COUNT_OF(HIT) },
    [FB_SFX_DIE]     = { DIE, COUNT_OF(DIE) },
    [FB_SFX_MENU]    = { MENU, COUNT_OF(MENU) },
    [FB_SFX_CONFIRM] = { CONFIRM, COUNT_OF(CONFIRM) },
    [FB_SFX_RECORD]  = { RECORD, COUNT_OF(RECORD) },
};

static uint32_t seg_samples(const fb_sfx_seg_t *s) {
    return (uint32_t)s->ms * FB_SFX_RATE / 1000u;
}

void fb_sfx_start(fb_sfx_player_t *p, fb_sfx_t id) {
    *p = (fb_sfx_player_t){ .noise = 0xACE1u };
    if ((unsigned)id >= FB_SFX_COUNT) return;
    p->segs = DEFS[id].segs;
    p->seg_count = DEFS[id].count;
}

bool fb_sfx_is_feedback(fb_sfx_t id) {
    return id == FB_SFX_FLAP || id == FB_SFX_MENU;
}

bool fb_sfx_done(const fb_sfx_player_t *p) {
    return p->segs == NULL || p->seg >= p->seg_count;
}

uint32_t fb_sfx_total_samples(fb_sfx_t id) {
    if ((unsigned)id >= FB_SFX_COUNT) return 0;
    uint32_t total = 0;
    for (uint8_t i = 0; i < DEFS[id].count; i++) total += seg_samples(&DEFS[id].segs[i]);
    return total;
}

// 包络 0..256:线性起音,线性释音。
static int32_t envelope(uint32_t pos, uint32_t len) {
    const uint32_t attack = ATTACK_MS * FB_SFX_RATE / 1000u;
    uint32_t release = len * 3u / 10u;
    if (release < RELEASE_MIN * FB_SFX_RATE / 1000u) release = RELEASE_MIN * FB_SFX_RATE / 1000u;
    if (release > len) release = len;
    int32_t env = 256;
    if (pos < attack) env = (int32_t)(pos * 256u / attack);
    const uint32_t left = len - pos;   // pos < len
    if (left <= release) {
        int32_t rel = (int32_t)((left - 1u) * 256u / release);
        if (rel < env) env = rel;
    }
    return env;
}

static int32_t oscillator(fb_sfx_player_t *p, const fb_sfx_seg_t *s, uint32_t len) {
    if (s->wave == FB_WAVE_REST) return 0;
    if (s->wave == FB_WAVE_NOISE) {
        // 16 位 Galois LFSR,约每 2 个样本换一次值,听感更"噗"
        if ((p->pos & 1u) == 0) {
            uint32_t lsb = p->noise & 1u;
            p->noise >>= 1;
            if (lsb) p->noise ^= 0xB400u;
        }
        return (p->noise & 1u) ? 32767 : -32767;
    }
    const int32_t df = (int32_t)s->f1_hz - (int32_t)s->f0_hz;
    const uint32_t f = (uint32_t)((int32_t)s->f0_hz + df * (int32_t)p->pos / (int32_t)len);
    p->phase += (uint32_t)(((uint64_t)f << 32) / FB_SFX_RATE);
    if (s->wave == FB_WAVE_SQUARE) return (p->phase & 0x80000000u) ? 32767 : -32767;
    // 三角波:相位高 16 位折线映射到 -32767..32767
    const uint32_t ph = p->phase >> 16;
    const int32_t tri = ph < 32768u ? (int32_t)ph * 2 - 32767 : (int32_t)(65535u - ph) * 2 - 32767;
    return tri;
}

size_t fb_sfx_render(fb_sfx_player_t *p, int16_t *out, size_t n) {
    size_t written = 0;
    while (written < n && !fb_sfx_done(p)) {
        const fb_sfx_seg_t *s = &p->segs[p->seg];
        const uint32_t len = seg_samples(s);
        if (p->pos >= len) {
            p->seg++;
            p->pos = 0;
            p->phase = 0;
            continue;
        }
        const int32_t osc = oscillator(p, s, len);
        const int32_t amp = PEAK * (int32_t)s->volume / 100;
        const int32_t env = envelope(p->pos, len);
        out[written++] = (int16_t)((int64_t)osc * amp / 32767 * env / 256);
        p->pos++;
    }
    return written;
}
