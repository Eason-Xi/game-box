// main/common/gb_idle.h —— 空闲调暗 / 熄屏状态机（纯逻辑，可主机测试）。
//
// 背光是这台电池设备最耗电的部分：
//   无操作 dim_after_ms → 调暗；无操作 off_after_ms → 关背光；任意按键 → 恢复。
//
// 游戏正在进行（busy）时不计空闲：蛇自己会走、方块自己会飞，玩家盯着屏幕
// 却可能好几秒不按键，这时调暗会让人看不清。
//
// 熄屏后的唤醒按键要整组吞掉：一次按键会依次产生 PRESS → CLICK（或 LONG），
// 只吞 PRESS 的话，紧跟着的 CLICK 仍会被当成「开始游戏」之类的操作。
// 因此唤醒后吞掉同一按键的后续事件，直到它的 CLICK / LONG / DOUBLE，
// 或超过 GB_IDLE_WAKE_SWALLOW_MS（防止事件丢失时一直吞）。
// 时间一律是 uint32_t 毫秒并用减法比较，计数回绕后仍然正确。
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "gb_nav.h"

#define GB_IDLE_WAKE_SWALLOW_MS 1500u

typedef enum {
    GB_POWER_ACTIVE = 0,   // 正常亮度
    GB_POWER_DIM,          // 调暗
    GB_POWER_OFF,          // 背光关闭
} gb_power_t;

typedef struct {
    uint32_t last_activity_ms;
    uint32_t dim_after_ms;
    uint32_t off_after_ms;     // 应大于 dim_after_ms
    gb_power_t state;
    bool swallowing;           // 正在吞掉唤醒按键的后续事件
    gb_key_t swallow_key;
    uint32_t swallow_since_ms;
} gb_idle_t;

void gb_idle_init(gb_idle_t *idle, uint32_t now_ms, uint32_t dim_after_ms, uint32_t off_after_ms);

// 处理一个按键事件。返回 true 表示该事件只用于唤醒，调用方应丢弃它。
bool gb_idle_input(gb_idle_t *idle, uint32_t now_ms, gb_key_t key, gb_key_ev_t ev);

// 周期调用：busy 为 true 时视作有活动。返回推进后的状态。
gb_power_t gb_idle_update(gb_idle_t *idle, uint32_t now_ms, bool busy);
