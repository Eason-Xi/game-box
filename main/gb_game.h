// main/gb_game.h —— 每个游戏都要实现的统一接口。
//
// 接入一个新游戏 = 在 main/games/<id>/ 里实现一个 gb_game_t，
// 再在 main/gb_registry.c 的 GAMES[] 里加一行。详见 docs/development/game-porting.zh_CN.md。
//
// 调用约定（全部由 gb_app 的输入任务发起）：
//   enter()  持 LVGL 锁。建立并 lv_screen_load() 自己的屏幕，申请画布内存，创建 lv_timer。
//   start()  可选，不持锁。启动游戏自己的后台任务（音效不需要，平台已统一提供）。
//   key()    持 LVGL 锁。必须很快返回：不阻塞、不做 I/O，慢操作交给自己的任务。
//            「确定长按」由平台拦截用于退出确认，游戏收不到。
//   pause()  可选，持 LVGL 锁。退出确认框弹出 / 关闭时调用，用于冻结 / 恢复游戏。
//   stop()   可选，不持锁。停止 start() 启动的任务；返回错误时平台不会删除页面。
//   exit()   持 LVGL 锁。删除 lv_timer 与屏幕，释放画布内存，清空所有对象指针。
//
// lv_timer 回调在 LVGL 任务里运行，已持锁，可以直接操作对象。
// 游戏进行中请调用 gb_app_set_playing(true)，避免平台在玩家盯着屏幕时调暗背光；
// 一局结束、暂停或停在说明页时设回 false。
#pragma once

#include "bsp_button.h"
#include "esp_err.h"
#include "lvgl.h"
#include <stdbool.h>
#include <stdint.h>

#define GB_ICON_SIZE 40   // 游戏列表里图标的边长（像素）

typedef struct {
    const char *id;        // 小写字母/数字/下划线，最长 13 字符；用于最高分键名
    const char *title;     // 游戏名，最多 4 个汉字（写在游戏自己的 *_text.h 里）
    const char *tagline;   // 游戏列表里的一句话介绍（约 9 个汉字以内，超出显示省略号）
    uint32_t accent;       // 主题色 0xRRGGBB：选中行底色、图标底色
    void (*icon)(lv_obj_t *box);   // 可选：在 GB_ICON_SIZE 见方的容器里画图标

    void (*enter)(void);
    void (*exit)(void);
    void (*key)(bsp_btn_t btn, bsp_btn_ev_t ev);
    void (*pause)(bool paused);    // 可选
    esp_err_t (*start)(void);      // 可选
    esp_err_t (*stop)(void);       // 可选
} gb_game_t;
