// main/common/gb_sfx.h —— 所有游戏共用的音效播放器。
//
// 开机由 gb_app 初始化一次，之后一直在后台运行；游戏只管调用播放函数，
// 不需要自己起停任务。音效任务不访问任何 UI，所以游戏退出时也不必等它。
//
// 两种用法：
//   1. 简单方波：用 gb_note_t 数组 + GB_TUNE() 定义音效表，调用 gb_sfx_play()。
//        static const gb_note_t EAT_NOTES[] = { { 784, 45 }, { 1175, 70 } };
//        static const gb_tune_t TUNE_EAT = GB_TUNE(EAT_NOTES);
//        gb_sfx_play(&TUNE_EAT);
//   2. 自带合成器：游戏需要扫频、三角波、噪声等音色时，实现一个 gb_synth_t，
//      调用 gb_sfx_play_synth()。采样率固定为 GB_SFX_RATE（16kHz、16 位单声道）。
// 传入的 gb_tune_t / gb_synth_t 必须是静态存储期（播放时才读取）。
//
// 「反馈音」（按键、移动、松钩这类短促提示）：正在播反馈音时来了新音效，立即
// 切过去；正在播普通音效时来了反馈音，直接丢弃。这样连按时声音不会排队滞后，
// 而得分、失败这类重要音效也不会被按键声打断。
#pragma once

#include "esp_err.h"
#include "gb_tone.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define GB_SFX_RATE         16000
#define GB_SYNTH_PLAYER_MAX 48     // 合成器播放状态的最大字节数

typedef struct {
    // 开始播放 sound（含义由合成器自定），player 指向 GB_SYNTH_PLAYER_MAX 字节的对齐内存。
    void (*start)(void *player, uintptr_t sound);
    // 渲染至多 n 个采样；返回 0 表示播完。
    size_t (*render)(void *player, int16_t *out, size_t n);
    // 可选：sound 是否是反馈音。
    bool (*is_feedback)(uintptr_t sound);
} gb_synth_t;

// 设置音频格式并创建播放任务。失败时返回错误码，之后播放请求静默丢弃。
esp_err_t gb_sfx_init(void);

// 入队一段音效。非阻塞，可在任意任务（含 LVGL 定时器）中调用；队列满时丢弃。
void gb_sfx_play(const gb_tune_t *tune);
void gb_sfx_play_feedback(const gb_tune_t *tune);
void gb_sfx_play_synth(const gb_synth_t *synth, uintptr_t sound);

// 立即停掉正在播的音效并清空队列。
void gb_sfx_cancel(void);

// 等没有音效在播（且 DMA 里的余音放完）后，在音效任务里调用 fn。
// 用于写 flash：写 flash 会暂停取指缓存，正在播的音效会被打断而出现爆音。
// 同一个 fn 重复请求只执行一次。音频不可用时返回 false，调用方应自行立即执行。
bool gb_sfx_run_when_idle(void (*fn)(void));

// 启动页与退出确认框使用的公共音效，游戏也可以直接用。
extern const gb_tune_t GB_TUNE_MOVE;      // 移动选中项（反馈音）
extern const gb_tune_t GB_TUNE_CONFIRM;   // 确认 / 开始
extern const gb_tune_t GB_TUNE_BACK;      // 返回
extern const gb_tune_t GB_TUNE_ASK;       // 弹出确认框
