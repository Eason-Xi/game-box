// main/games/tap/tap_model.h —— 连点挑战的规则（纯逻辑，可主机测试）。
//
// 10 秒内按 OK 的次数就是分数。第一次按下即开始计时并计 1 次。
// 时间一律是 uint32_t 毫秒并用减法比较，计数回绕后仍然正确。
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define TAP_DURATION_MS          10000u
#define TAP_RESTART_COOLDOWN_MS  800u    // 结束后短暂忽略「再来」，防止连按直接开新局

typedef enum {
    TAP_READY = 0,
    TAP_RUN,
    TAP_OVER,
} tap_state_t;

typedef struct {
    tap_state_t state;
    uint32_t start_ms;
    uint32_t count;
} tap_t;

void tap_init(tap_t *t);

// 一次按下。READY 时开始一局；RUN 时计数。返回是否计入了这一次。
// 时间已到的按下不计入，并把状态推进到 OVER。
bool tap_press(tap_t *t, uint32_t now_ms);

// 周期调用。本次调用使状态变成 OVER 时返回 true。
bool tap_update(tap_t *t, uint32_t now_ms);

// RUN 中剩余毫秒；READY 为整段时长，OVER 为 0。
uint32_t tap_remaining_ms(const tap_t *t, uint32_t now_ms);

// OVER 状态下是否已过冷却、可以重来。
bool tap_can_restart(const tap_t *t, uint32_t now_ms);
