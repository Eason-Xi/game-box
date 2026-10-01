// main/common/gb_tone.h —— 方波音效的 PCM 生成（纯逻辑，可主机测试）。
//
// 从贪吃蛇的 snake_sfx_render 提取而来。音效写错（爆音、音高不对、整段静音）
// 在编译期看不出来，所以把「采样怎么算」与 ESP-IDF 解耦，在主机上逐样本断言。
#pragma once

#include <stddef.h>
#include <stdint.h>

// 一个音符。freq_hz 为 0 表示休止。
typedef struct {
    uint16_t freq_hz;
    uint16_t ms;
} gb_note_t;

// 一段音效 = 若干音符。用 GB_TUNE() 从数组生成，免得手写长度写错。
typedef struct {
    const gb_note_t *notes;
    uint8_t count;
} gb_tune_t;

#define GB_TUNE(notes_array) { (notes_array), (uint8_t)(sizeof(notes_array) / sizeof((notes_array)[0])) }

typedef struct {
    uint32_t sample_rate;    // 例如 16000
    int16_t amplitude;       // 方波峰值
    uint32_t fade_samples;   // 音符首尾的线性淡入淡出长度，消除起停「咔」声
} gb_tone_config_t;

// 一个音符的采样总数（时长向下取整）。
size_t gb_tone_samples_for(uint32_t ms, const gb_tone_config_t *config);

// 生成一个音符的第 [first, first + count) 个采样写入 out。
// 包络按绝对位置计算，所以任意分块生成的结果与整段生成逐样本一致。
// freq_hz 为 0 或过高（周期不足 2 个采样）时输出静音；index >= total 的位置写 0。
size_t gb_tone_render_chunk(int16_t *out, size_t count, size_t first, size_t total,
                            uint32_t freq_hz, const gb_tone_config_t *config);

// 按顺序播放一段音效的流式播放器：可按任意块大小反复调用 render()，
// 结果与一次渲染整段逐样本一致。
typedef struct {
    const gb_tune_t *tune;
    uint8_t note;      // 当前音符
    size_t pos;        // 当前音符内的采样序号
} gb_tune_player_t;

void gb_tune_player_start(gb_tune_player_t *p, const gb_tune_t *tune);

// 渲染至多 n 个采样，返回实际写出的数量；返回 0 表示播完。
size_t gb_tune_player_render(gb_tune_player_t *p, const gb_tone_config_t *config,
                             int16_t *out, size_t n);
