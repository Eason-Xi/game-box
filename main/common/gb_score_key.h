// main/common/gb_score_key.h —— 游戏 id 与 NVS 键名的映射（纯逻辑，可主机测试）。
//
// 每个游戏的数据存在同一个 NVS 命名空间里：最高分键名 "b_" + id，
// 自定义存档（设置等）键名 "s_" + id。
// NVS 键名最长 15 个字符，因此 id 最长 GB_GAME_ID_MAX 个字符，
// 且只允许小写字母、数字和下划线 —— 这样 id 也能直接用作文件夹名和 C 符号的一部分。
#pragma once

#include <stdbool.h>
#include <stddef.h>

#define GB_GAME_ID_MAX   13
#define GB_SCORE_KEY_LEN 16   // 含结尾 '\0'

// id 是否合法（非空、不超长、字符集合规）。
bool gb_game_id_valid(const char *id);

// 生成键名 prefix + "_" + id 写入 out（至少 GB_SCORE_KEY_LEN 字节）。
// prefix 必须是小写字母；id 非法时返回 false 且 out 置空串。
bool gb_store_make_key(char prefix, const char *id, char *out, size_t out_size);

// 最高分键名 "b_" + id。
bool gb_score_make_key(const char *id, char *out, size_t out_size);
