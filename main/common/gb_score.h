// main/common/gb_score.h —— 各游戏的最高分与自定义存档（NVS 命名空间 "gamebox"）。
//
// 最高分约定「越大越好」。需要「越小越好」的游戏（如计时类）请自行换算后再提交。
// 自定义存档是每个游戏一块不超过 GB_SAVE_MAX 字节的数据（设置、额外纪录等），
// 建议在结构体里带版本号，读回后先校验。
//
// 写入是异步的：提交后立即在内存里生效（随后的读取能读到新值），真正写 flash
// 由音效任务在两段音效之间执行 —— 写 flash 会暂停取指缓存，正在播放的音效会爆音。
// 只有内容变化才写 flash。
// 内部有互斥锁，可在任意任务（含 LVGL 定时器）中调用；NVS 不可用时读到 0 /
// 读取失败、写入返回 false，游戏照常进行。
#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define GB_SAVE_MAX 32

// 开机调用一次：初始化 NVS 并打开命名空间。
// 刻意不在失败时擦除 NVS：整片擦除会清掉设备上的其他数据，不能当作「修复」。
esp_err_t gb_score_init(void);

// 读取某游戏的最高分；没有记录或不可用时返回 0。
uint32_t gb_score_best(const char *game_id);

// 提交一局分数；高于纪录时记下并返回 true（随后异步写入 flash）。
bool gb_score_submit(const char *game_id, uint32_t score);

// 读取自定义存档。没有存档、长度不符或不可用时返回 false，buf 内容不变。
bool gb_save_load(const char *game_id, void *buf, size_t len);

// 保存自定义存档（len 不超过 GB_SAVE_MAX）。内容未变化时不写 flash。
bool gb_save_store(const char *game_id, const void *buf, size_t len);
