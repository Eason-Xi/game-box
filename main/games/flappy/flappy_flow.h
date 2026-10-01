// main/flappy_flow.h —— 页面流程与按键映射的纯状态机(无 ESP-IDF / LVGL 依赖)。
//
// 所有页面只响应 PRESS(按下瞬间),保证扇翅低延迟,也避免同一次按键的
// CLICK 在切换页面后被新页面再次消费。
//
//   标题页   上/下=移动焦点  OK=执行焦点项(开始 / 切换难度 / 切换音效)
//   准备     OK 或 上=扇翅并开始  下=返回标题
//   游戏中   OK 或 上=扇翅        下=暂停
//   坠落中   忽略按键
//   暂停     上/下=移动焦点  OK=继续 / 重新开始 / 返回标题
//   结算     上/下=移动焦点  OK=再来一局 / 返回标题(结束后短暂锁定防误触)
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "flappy_model.h"

#define FF_OVER_LOCK_MS  700

typedef enum { FF_KEY_UP = 0, FF_KEY_DOWN, FF_KEY_OK } ff_key_t;

typedef enum {
    FF_TITLE = 0,
    FF_READY,
    FF_PLAYING,
    FF_DYING,
    FF_PAUSED,
    FF_OVER,
} ff_state_t;

typedef enum {
    FF_ACT_NONE = 0,
    FF_ACT_FOCUS,         // 焦点移动,仅刷新高亮
    FF_ACT_CYCLE_DIFF,    // 难度已切换,需保存设置
    FF_ACT_TOGGLE_SOUND,  // 音效已切换,需保存设置
    FF_ACT_START,         // 进入准备状态(新开一局)
    FF_ACT_FLAP,
    FF_ACT_PAUSE,
    FF_ACT_RESUME,
    FF_ACT_TO_TITLE,
} ff_action_t;

enum { FF_TITLE_ITEM_START = 0, FF_TITLE_ITEM_DIFF, FF_TITLE_ITEM_SOUND, FF_TITLE_ITEMS };
enum { FF_PAUSE_ITEM_RESUME = 0, FF_PAUSE_ITEM_RESTART, FF_PAUSE_ITEM_TITLE, FF_PAUSE_ITEMS };
enum { FF_OVER_ITEM_AGAIN = 0, FF_OVER_ITEM_TITLE, FF_OVER_ITEMS };

#define FF_SETTINGS_VERSION 1

// 持久化到 NVS 的设置与纪录。
typedef struct {
    uint8_t version;
    uint8_t difficulty;
    uint8_t sound_on;
    uint8_t reserved;
    uint16_t best[FB_DIFF_COUNT];
} ff_settings_t;

typedef struct {
    ff_state_t state;
    uint8_t focus;
    ff_settings_t settings;
    uint32_t over_at_ms;
} ff_flow_t;

void ff_settings_default(ff_settings_t *s);
// 校验从存储读回的设置;非法时恢复默认并返回 false。
bool ff_settings_sanitize(ff_settings_t *s);

void ff_flow_init(ff_flow_t *f, const ff_settings_t *settings);
ff_action_t ff_flow_key(ff_flow_t *f, ff_key_t key, uint32_t now_ms);

// 由游戏驱动在模型状态变化时通知。
void ff_flow_on_hit(ff_flow_t *f);
// 落地结算:更新纪录,返回 true 表示刷新了本难度最高分(需保存)。
bool ff_flow_on_over(ff_flow_t *f, uint32_t score, uint32_t now_ms);

uint16_t ff_flow_best(const ff_flow_t *f);
