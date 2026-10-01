// main/crane_sfx.h —— 音效合成(纯计算,无 ESP-IDF 依赖)。
//
// 每个音效由若干段组成,每段是方波/三角波/噪声的线性扫频音,带起音与释音
// 包络,首尾样本都接近 0,避免喇叭爆音。播放器可按任意块大小分段渲染,
// 结果与一次渲染完全一致(测试保证),便于音频任务流式写入 I2S。
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define CS_SFX_RATE 16000

typedef enum {
    CS_SFX_DROP = 0,     // 松钩
    CS_SFX_LAND,         // 落稳
    CS_SFX_PERFECT_1,    // 完美落下,连击越高音越高
    CS_SFX_PERFECT_2,
    CS_SFX_PERFECT_3,
    CS_SFX_PERFECT_4,
    CS_SFX_PERFECT_5,
    CS_SFX_MISS,         // 箱子掉下去
    CS_SFX_LIFE,         // 奖励一条命
    CS_SFX_COLLAPSE,     // 塔倒了
    CS_SFX_OVER,         // 机会用完
    CS_SFX_RECORD,       // 新纪录
    CS_SFX_MENU,
    CS_SFX_CONFIRM,
    CS_SFX_COUNT,
} cs_sfx_t;

typedef enum { CS_WAVE_SQUARE = 0, CS_WAVE_TRIANGLE, CS_WAVE_NOISE, CS_WAVE_REST } cs_wave_t;

typedef struct {
    uint16_t f0_hz;      // 起始频率
    uint16_t f1_hz;      // 结束频率(线性扫频)
    uint16_t ms;         // 时长
    uint8_t wave;        // cs_wave_t
    uint8_t volume;      // 0..100
} cs_sfx_seg_t;

typedef struct {
    const cs_sfx_seg_t *segs;
    uint8_t seg_count;
    uint8_t seg;
    uint32_t pos;        // 当前段内样本序号
    uint32_t phase;      // 32 位相位累加器
    uint32_t noise;      // 噪声 LFSR 状态
} cs_sfx_player_t;

void cs_sfx_start(cs_sfx_player_t *p, cs_sfx_t id);
// 渲染至多 n 个样本,返回实际写出的数量;返回 0 表示音效结束。
size_t cs_sfx_render(cs_sfx_player_t *p, int16_t *out, size_t n);
bool cs_sfx_done(const cs_sfx_player_t *p);
// 短促的操作反馈(松钩、菜单):可以被下一个音效打断,也可以在其他音效播放
// 时直接丢弃,避免连按时声音排队滞后。
bool cs_sfx_is_feedback(cs_sfx_t id);
// 音效总样本数(用于测试与估算时长)。
uint32_t cs_sfx_total_samples(cs_sfx_t id);
// 第 combo 次连续完美对应的音效(1 起,5 以上保持最高音)。
cs_sfx_t cs_sfx_for_perfect(uint32_t combo);
