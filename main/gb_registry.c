// main/gb_registry.c —— 游戏清单：启动页按这里的顺序显示。
//
// 加一个游戏：在下面声明它的 gb_game_t，再把地址加进 GAMES[]，
// 并把源文件加进 main/CMakeLists.txt。
#include "gb_registry.h"

extern const gb_game_t GB_GAME_CRANE;
extern const gb_game_t GB_GAME_DODGE;
extern const gb_game_t GB_GAME_FLAPPY;
extern const gb_game_t GB_GAME_TAP;

static const gb_game_t *const GAMES[] = {
    &GB_GAME_CRANE,
    &GB_GAME_FLAPPY,
    &GB_GAME_DODGE,
    &GB_GAME_TAP,
};

size_t gb_registry_count(void) {
    return sizeof(GAMES) / sizeof(GAMES[0]);
}

const gb_game_t *gb_registry_get(size_t index) {
    return index < gb_registry_count() ? GAMES[index] : NULL;
}
