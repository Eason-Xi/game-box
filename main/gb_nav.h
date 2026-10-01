// main/gb_nav.h —— 游戏盒的导航状态机（纯逻辑，可主机测试）。
//
// 三种模式：
//   LAUNCHER  启动页：上/下单击切换游戏，确定单击启动选中的游戏；
//   PLAYING   游戏中：除「确定长按」外，所有按键原样转发给游戏；
//   CONFIRM   退出确认：确定单击 = 退出到启动页，上/下单击 = 取消、继续玩。
//
// 「确定长按」只弹出确认框而不直接退出：本板长按判定只有 500ms，游戏里按住
// 确定键很常见，直接退出很容易误触丢掉一局。
//
// 启动与退出都是两段式：handle() 只给出动作，真正完成（页面建好 / 删掉）后
// 再由调用方调用 gb_nav_launched() / gb_nav_exited() 切换模式。这样拿不到 LVGL
// 锁或游戏停止失败时，状态机不会和屏幕上的实际页面对不上。
#pragma once

#include <stdbool.h>
#include <stddef.h>

// 与 bsp_btn_t / bsp_btn_ev_t 取值一一对应（gb_app.c 用静态断言保证），
// 本模块因此不依赖 BSP 头文件，可在主机上编译。
typedef enum {
    GB_KEY_UP = 0,
    GB_KEY_DOWN,
    GB_KEY_OK,
} gb_key_t;

typedef enum {
    GB_KEY_EV_PRESS = 0,
    GB_KEY_EV_CLICK,
    GB_KEY_EV_DOUBLE,
    GB_KEY_EV_LONG,
} gb_key_ev_t;

typedef enum {
    GB_MODE_LAUNCHER = 0,
    GB_MODE_PLAYING,
    GB_MODE_CONFIRM,
} gb_mode_t;

typedef enum {
    GB_ACT_NONE = 0,
    GB_ACT_SELECT,        // 选中项变了（dir 为 -1 / +1），刷新启动页
    GB_ACT_LAUNCH,        // 启动 index 指向的游戏，完成后调用 gb_nav_launched()
    GB_ACT_FORWARD,       // 把按键转发给当前游戏
    GB_ACT_ASK_EXIT,      // 弹出退出确认框（并让游戏暂停）
    GB_ACT_CANCEL_EXIT,   // 关闭确认框，继续游戏
    GB_ACT_EXIT,          // 退出当前游戏，完成后调用 gb_nav_exited()
} gb_action_t;

typedef struct {
    size_t count;      // 游戏数量
    size_t selected;   // 启动页选中项；游戏中即为正在玩的那一个
    gb_mode_t mode;
} gb_nav_t;

typedef struct {
    gb_action_t action;
    size_t index;      // 动作针对的游戏下标
    int dir;           // GB_ACT_SELECT 时的移动方向：-1 向上，+1 向下
} gb_nav_result_t;

void gb_nav_init(gb_nav_t *nav, size_t count);
gb_nav_result_t gb_nav_handle(gb_nav_t *nav, gb_key_t key, gb_key_ev_t ev);

// 游戏页面已建好：进入 PLAYING。
void gb_nav_launched(gb_nav_t *nav);
// 游戏已退出、启动页已重建：回到 LAUNCHER，选中项保持为刚玩过的游戏。
void gb_nav_exited(gb_nav_t *nav);
