// main/flappy_sfx.h —— 音效合成(纯计算,无 ESP-IDF 依赖)。
//
// 每个音效由若干段组成,每段是一个方波/三角波/噪声的扫频音,带起音与释音
// 包络,首尾样本都接近 0,避免喇叭爆音。播放器可按任意块大小分段渲染,
// 结果与一次渲染完全一致(测试保证),便于音频任务流式写入 I2S。
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define FB_SFX_RATE 16000

typedef enum {
    FB_SFX_FLAP = 0,
    FB_SFX_SCORE,
    FB_SFX_HIT,          // 直接撞地:只有撞击声
    FB_SFX_DIE,          // 撞管:撞击声 + 坠落滑音
    FB_SFX_MENU,
    FB_SFX_CONFIRM,
    FB_SFX_RECORD,
    FB_SFX_COUNT,
} fb_sfx_t;

typedef enum { FB_WAVE_SQUARE = 0, FB_WAVE_TRIANGLE, FB_WAVE_NOISE, FB_WAVE_REST } fb_wave_t;

typedef struct {
    uint16_t f0_hz;      // 起始频率
    uint16_t f1_hz;      // 结束频率(线性扫频)
    uint16_t ms;         // 时长
    uint8_t wave;        // fb_wave_t
    uint8_t volume;      // 0..100
} fb_sfx_seg_t;

typedef struct {
    const fb_sfx_seg_t *segs;
    uint8_t seg_count;
    uint8_t seg;
    uint32_t pos;        // 当前段内样本序号
    uint32_t phase;      // 32 位相位累加器
    uint32_t noise;      // 噪声 LFSR 状态
} fb_sfx_player_t;

void fb_sfx_start(fb_sfx_player_t *p, fb_sfx_t id);
// 渲染至多 n 个样本,返回实际写出的数量;返回 0 表示音效结束。
size_t fb_sfx_render(fb_sfx_player_t *p, int16_t *out, size_t n);
bool fb_sfx_done(const fb_sfx_player_t *p);
// 短促的操作反馈(扇翅、菜单):可以被下一个音效打断,也可以在其他音效播放时
// 直接丢弃,避免连按时声音排队滞后。
bool fb_sfx_is_feedback(fb_sfx_t id);
// 音效总样本数(用于测试与估算时长)。
uint32_t fb_sfx_total_samples(fb_sfx_t id);
