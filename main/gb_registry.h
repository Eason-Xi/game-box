// main/gb_registry.h —— 游戏清单。
#pragma once

#include "gb_game.h"
#include <stddef.h>

size_t gb_registry_count(void);
const gb_game_t *gb_registry_get(size_t index);   // 越界返回 NULL
