// tools/gamebox_preview/flappy_probe.c —— 让预览 harness 读到飞吧小鸟的内部状态。
//
// 与堆箱子一样直接包含真实的 flappy_game.c；单独成一个编译单元，是因为两个游戏
// 的文件内静态变量同名（s_model、s_flow …），不能放进同一个 .c。
#include "../../main/games/flappy/flappy_game.c"

const fb_model_t *preview_flappy_model(void) { return &s_model; }
int preview_flappy_state(void) { return (int)s_flow.state; }
