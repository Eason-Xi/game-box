// main/crane_flow.h —— 页面流程与按键映射的纯状态机(无 ESP-IDF / LVGL 依赖)。
//
// 所有页面只响应 PRESS(按下瞬间):松钩延迟最低,也避免同一次按键的
// CLICK 在切换页面后被新页面再次消费。
//
//   标题页   上/下=移动焦点  OK=开始游戏 / 切换音效
//   游戏中   OK 或 下=松钩    上=暂停
//   暂停     上/下=移动焦点  OK=继续 / 重新开始 / 返回标题
//   结算     上/下=移动焦点  OK=再来一局 / 返回标题(结束后短暂锁定防误触)
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define CF_OVER_LOCK_MS  800

typedef enum { CF_KEY_UP = 0, CF_KEY_DOWN, CF_KEY_OK } cf_key_t;

typedef enum {
    CF_TITLE = 0,
    CF_PLAYING,
    CF_PAUSED,
    CF_OVER,
} cf_state_t;

typedef enum {
    CF_ACT_NONE = 0,
    CF_ACT_FOCUS,         // 焦点移动,仅刷新高亮
    CF_ACT_TOGGLE_SOUND,  // 音效已切换,需保存设置
    CF_ACT_START,         // 新开一局
    CF_ACT_DROP,          // 松钩
    CF_ACT_PAUSE,
    CF_ACT_RESUME,
    CF_ACT_TO_TITLE,
} cf_action_t;

enum { CF_TITLE_ITEM_START = 0, CF_TITLE_ITEM_SOUND, CF_TITLE_ITEMS };
enum { CF_PAUSE_ITEM_RESUME = 0, CF_PAUSE_ITEM_RESTART, CF_PAUSE_ITEM_TITLE, CF_PAUSE_ITEMS };
enum { CF_OVER_ITEM_AGAIN = 0, CF_OVER_ITEM_TITLE, CF_OVER_ITEMS };

#define CF_SETTINGS_VERSION 1

// 持久化到 NVS 的设置与纪录。
typedef struct {
    uint8_t version;
    uint8_t sound_on;
    uint16_t best_score;
    uint16_t best_height;
    uint16_t reserved;
} cf_settings_t;

typedef struct {
    cf_state_t state;
    uint8_t focus;
    cf_settings_t settings;
    uint32_t over_at_ms;
} cf_flow_t;

void cf_settings_default(cf_settings_t *s);
// 校验从存储读回的设置;非法时恢复默认并返回 false。
bool cf_settings_sanitize(cf_settings_t *s);

void cf_flow_init(cf_flow_t *f, const cf_settings_t *settings);
cf_action_t cf_flow_key(cf_flow_t *f, cf_key_t key, uint32_t now_ms);

// 本局结束:更新纪录,返回 true 表示刷新了任意一项纪录(需保存)。
// *new_score 为 true 表示最高分被刷新(结算页显示"新纪录")。
bool cf_flow_on_over(cf_flow_t *f, uint32_t score, uint32_t height, uint32_t now_ms,
                     bool *new_score);
