// main/crane_sfx.c —— 音效合成实现,见 crane_sfx.h。
#include "crane_sfx.h"

#define COUNT_OF(a) (sizeof(a) / sizeof((a)[0]))

#define PEAK        12000   // 满音量峰值,给 ES8311 输出留余量
#define ATTACK_MS   3
#define RELEASE_MIN 8       // 释音至少 8ms,且不短于段长的 30%

static const cs_sfx_seg_t DROP[] = {
    { 700, 320, 60, CS_WAVE_TRIANGLE, 55 },
};
// 落地"咚":短噪声 + 低频三角波下滑
static const cs_sfx_seg_t LAND[] = {
    { 0, 0, 25, CS_WAVE_NOISE, 55 },
    { 190, 80, 110, CS_WAVE_TRIANGLE, 95 },
};
// 完美:落地声后接一个上行音阶的亮音(C6 D6 E6 G6 A6),再叠高八度余音
#define PERFECT(note) { \
    { 0, 0, 20, CS_WAVE_NOISE, 45 }, \
    { 220, 120, 50, CS_WAVE_TRIANGLE, 90 }, \
    { note, note, 70, CS_WAVE_SQUARE, 40 }, \
    { note * 2, note * 2, 110, CS_WAVE_SQUARE, 30 }, \
}
static const cs_sfx_seg_t PERFECT_1[] = PERFECT(1047);
static const cs_sfx_seg_t PERFECT_2[] = PERFECT(1175);
static const cs_sfx_seg_t PERFECT_3[] = PERFECT(1319);
static const cs_sfx_seg_t PERFECT_4[] = PERFECT(1568);
static const cs_sfx_seg_t PERFECT_5[] = PERFECT(1760);
static const cs_sfx_seg_t MISS[] = {
    { 0, 0, 30, CS_WAVE_NOISE, 50 },
    { 620, 160, 420, CS_WAVE_TRIANGLE, 75 },
};
static const cs_sfx_seg_t LIFE[] = {
    { 784, 784, 70, CS_WAVE_SQUARE, 40 },
    { 988, 988, 70, CS_WAVE_SQUARE, 40 },
    { 1175, 1175, 70, CS_WAVE_SQUARE, 40 },
    { 1568, 1568, 160, CS_WAVE_SQUARE, 40 },
};
// 倒塌:一串噪声撞击 + 长下滑
static const cs_sfx_seg_t COLLAPSE[] = {
    { 0, 0, 90, CS_WAVE_NOISE, 80 },
    { 160, 70, 120, CS_WAVE_TRIANGLE, 90 },
    { 0, 0, 70, CS_WAVE_NOISE, 65 },
    { 140, 60, 120, CS_WAVE_TRIANGLE, 80 },
    { 0, 0, 60, CS_WAVE_NOISE, 50 },
    { 520, 90, 600, CS_WAVE_TRIANGLE, 70 },
};
static const cs_sfx_seg_t OVER[] = {
    { 523, 523, 140, CS_WAVE_SQUARE, 35 },
    { 466, 466, 140, CS_WAVE_SQUARE, 35 },
    { 392, 392, 140, CS_WAVE_SQUARE, 35 },
    { 330, 300, 360, CS_WAVE_SQUARE, 35 },
};
static const cs_sfx_seg_t RECORD[] = {
    { 523, 523, 90, CS_WAVE_SQUARE, 40 },
    { 659, 659, 90, CS_WAVE_SQUARE, 40 },
    { 784, 784, 90, CS_WAVE_SQUARE, 40 },
    { 1047, 1047, 260, CS_WAVE_SQUARE, 40 },
};
static const cs_sfx_seg_t MENU[] = {
    { 660, 660, 35, CS_WAVE_SQUARE, 35 },
};
static const cs_sfx_seg_t CONFIRM[] = {
    { 660, 660, 45, CS_WAVE_SQUARE, 40 },
    { 990, 990, 70, CS_WAVE_SQUARE, 40 },
};

typedef struct {
    const cs_sfx_seg_t *segs;
    uint8_t count;
} sfx_def_t;

static const sfx_def_t DEFS[CS_SFX_COUNT] = {
    [CS_SFX_DROP]      = { DROP, COUNT_OF(DROP) },
    [CS_SFX_LAND]      = { LAND, COUNT_OF(LAND) },
    [CS_SFX_PERFECT_1] = { PERFECT_1, COUNT_OF(PERFECT_1) },
    [CS_SFX_PERFECT_2] = { PERFECT_2, COUNT_OF(PERFECT_2) },
    [CS_SFX_PERFECT_3] = { PERFECT_3, COUNT_OF(PERFECT_3) },
    [CS_SFX_PERFECT_4] = { PERFECT_4, COUNT_OF(PERFECT_4) },
    [CS_SFX_PERFECT_5] = { PERFECT_5, COUNT_OF(PERFECT_5) },
    [CS_SFX_MISS]      = { MISS, COUNT_OF(MISS) },
    [CS_SFX_LIFE]      = { LIFE, COUNT_OF(LIFE) },
    [CS_SFX_COLLAPSE]  = { COLLAPSE, COUNT_OF(COLLAPSE) },
    [CS_SFX_OVER]      = { OVER, COUNT_OF(OVER) },
    [CS_SFX_RECORD]    = { RECORD, COUNT_OF(RECORD) },
    [CS_SFX_MENU]      = { MENU, COUNT_OF(MENU) },
    [CS_SFX_CONFIRM]   = { CONFIRM, COUNT_OF(CONFIRM) },
};

static uint32_t seg_samples(const cs_sfx_seg_t *s) {
    return (uint32_t)s->ms * CS_SFX_RATE / 1000u;
}

void cs_sfx_start(cs_sfx_player_t *p, cs_sfx_t id) {
    *p = (cs_sfx_player_t){ .noise = 0xACE1u };
    if ((unsigned)id >= CS_SFX_COUNT) return;
    p->segs = DEFS[id].segs;
    p->seg_count = DEFS[id].count;
}

bool cs_sfx_is_feedback(cs_sfx_t id) {
    return id == CS_SFX_DROP || id == CS_SFX_MENU;
}

cs_sfx_t cs_sfx_for_perfect(uint32_t combo) {
    if (combo <= 1) return CS_SFX_PERFECT_1;
    if (combo >= 5) return CS_SFX_PERFECT_5;
    return (cs_sfx_t)(CS_SFX_PERFECT_1 + combo - 1);
}

bool cs_sfx_done(const cs_sfx_player_t *p) {
    return p->segs == NULL || p->seg >= p->seg_count;
}

uint32_t cs_sfx_total_samples(cs_sfx_t id) {
    if ((unsigned)id >= CS_SFX_COUNT) return 0;
    uint32_t total = 0;
    for (uint8_t i = 0; i < DEFS[id].count; i++) total += seg_samples(&DEFS[id].segs[i]);
    return total;
}

// 包络 0..256:线性起音,线性释音。
static int32_t envelope(uint32_t pos, uint32_t len) {
    const uint32_t attack = ATTACK_MS * CS_SFX_RATE / 1000u;
    uint32_t release = len * 3u / 10u;
    if (release < RELEASE_MIN * CS_SFX_RATE / 1000u) release = RELEASE_MIN * CS_SFX_RATE / 1000u;
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

static int32_t oscillator(cs_sfx_player_t *p, const cs_sfx_seg_t *s, uint32_t len) {
    if (s->wave == CS_WAVE_REST) return 0;
    if (s->wave == CS_WAVE_NOISE) {
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
    p->phase += (uint32_t)(((uint64_t)f << 32) / CS_SFX_RATE);
    if (s->wave == CS_WAVE_SQUARE) return (p->phase & 0x80000000u) ? 32767 : -32767;
    // 三角波:相位高 16 位折线映射到 -32767..32767
    const uint32_t ph = p->phase >> 16;
    const int32_t tri = ph < 32768u ? (int32_t)ph * 2 - 32767 : (int32_t)(65535u - ph) * 2 - 32767;
    return tri;
}

size_t cs_sfx_render(cs_sfx_player_t *p, int16_t *out, size_t n) {
    size_t written = 0;
    while (written < n && !cs_sfx_done(p)) {
        const cs_sfx_seg_t *s = &p->segs[p->seg];
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
