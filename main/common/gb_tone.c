// main/common/gb_tone.c —— 纯 PCM 生成，见 gb_tone.h。
#include "gb_tone.h"

size_t gb_tone_samples_for(uint32_t ms, const gb_tone_config_t *config) {
    if (!config || config->sample_rate == 0) return 0;
    return (size_t)((uint64_t)config->sample_rate * ms / 1000U);
}

size_t gb_tone_render_chunk(int16_t *out, size_t count, size_t first, size_t total,
                            uint32_t freq_hz, const gb_tone_config_t *config) {
    if (!out || !config || config->sample_rate == 0) return 0;

    // 周期用整数除法，16000/880 = 18 → 实际约 888Hz，听感上可忽略。
    const uint32_t period = freq_hz ? config->sample_rate / freq_hz : 0;
    const uint32_t half = period / 2;

    // 音很短时淡入淡出各占一半，保证首尾一定归零。
    size_t fade = config->fade_samples;
    if (fade * 2 > total) fade = total / 2;

    for (size_t i = 0; i < count; i++) {
        const size_t index = first + i;
        int32_t sample = 0;
        if (index < total) {
            if (period > 1) {
                const uint32_t phase = (uint32_t)(index % period);
                sample = (phase < half) ? config->amplitude : -(int32_t)config->amplitude;
            }
            if (fade > 0) {
                if (index < fade) {
                    sample = sample * (int32_t)index / (int32_t)fade;
                } else if (index >= total - fade) {
                    sample = sample * (int32_t)(total - index - 1) / (int32_t)fade;
                }
            }
        }
        out[i] = (int16_t)sample;
    }
    return count;
}

void gb_tune_player_start(gb_tune_player_t *p, const gb_tune_t *tune) {
    if (!p) return;
    p->tune = tune;
    p->note = 0;
    p->pos = 0;
}

size_t gb_tune_player_render(gb_tune_player_t *p, const gb_tone_config_t *config,
                             int16_t *out, size_t n) {
    if (!p || !p->tune || !out) return 0;
    size_t written = 0;
    while (written < n && p->note < p->tune->count) {
        const gb_note_t *note = &p->tune->notes[p->note];
        const size_t total = gb_tone_samples_for(note->ms, config);
        if (p->pos >= total) {      // 本音符结束（含 0 时长音符），进入下一个
            p->note++;
            p->pos = 0;
            continue;
        }
        size_t chunk = total - p->pos;
        if (chunk > n - written) chunk = n - written;
        gb_tone_render_chunk(out + written, chunk, p->pos, total, note->freq_hz, config);
        p->pos += chunk;
        written += chunk;
    }
    return written;
}
