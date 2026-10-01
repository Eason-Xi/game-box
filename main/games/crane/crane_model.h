// main/crane_model.h —— 吊车堆箱子的纯游戏模型(无 ESP-IDF / LVGL 依赖)。
//
// 以固定 5ms 节拍推进,同一个随机种子下结果完全确定,主机测试可逐拍复现。
//
// 坐标约定:
//   * x 是屏幕横坐标(像素,Q8 定点),塔与吊钩都以 CS_CENTER_X 为中心。
//   * 世界 y 向上为正,地基顶面为 0;第 i 层(0 起)占据 [i*H, (i+1)*H)。
//   * 屏幕 y = CS_GROUND_SCREEN_Y - 世界 y + 镜头高度 cam。镜头由模型
//     按固定节拍缓动,吊着的箱子固定在屏幕上,松钩时才换算成世界坐标。
//
// 玩法:
//   * 箱子挂在屏幕外上方的吊点下摆动;松钩后保留一部分横向速度下落。
//   * 落点误差 d = 箱子中心 - 塔顶中心(含晃动)。|d| ≤ 3px 为完美并对齐;
//     |d| < 支撑半宽(箱子半宽,第一层用地基半宽)为落稳;否则掉落扣一条命。
//   * 落稳后逐层检查:某层以上所有箱子的合重心超出该层边缘,整座塔倒塌。
//   * 塔会整体剪切晃动,幅度随高度、重心偏移和落点误差增大,完美落下减弱。
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define CS_Q           8
#define CS_ONE         (1 << CS_Q)
#define CS_TICK_MS     5
#define CS_TICK_US     (CS_TICK_MS * 1000)

// ---- 场景几何(屏幕像素) ----
#define CS_SCREEN_W        240
#define CS_SCREEN_H        320
#define CS_CENTER_X        120
#define CS_BOX_W           56
#define CS_BOX_H           24
#define CS_BASE_W          96      // 地基宽度,第一层的支撑面
#define CS_GROUND_SCREEN_Y 284     // 镜头为 0 时地基顶面的屏幕 y
#define CS_TOP_SCREEN_Y    196     // 塔变高后,塔顶面保持在这个屏幕 y
#define CS_PIVOT_Y         (-140)  // 吊点在屏幕外上方
#define CS_ROPE_LEN        236     // 吊点到箱子顶边中点的绳长
#define CS_SPAWN_LIFT      120     // 新箱子从这么高处放下来

// ---- 规则 ----
#define CS_MAX_LIVES       3
#define CS_MAX_LAYERS      400     // 足够任何人玩;到顶后不再加层也不再判定倒塌
#define CS_PERFECT_PX      3
#define CS_LIFE_COMBO      5       // 连续完美几次奖励一条命(未满时)

// ---- 节奏(节拍数) ----
#define CS_SPAWN_TICKS     60      // 300ms:镜头上移 + 新箱子放下
#define CS_MISS_TICKS      140     // 700ms:掉落的箱子飞出屏幕
#define CS_COLLAPSE_TICKS  320     // 1.6s:倒塌动画

typedef enum {
    CS_ST_SPAWN = 0,   // 新箱子下放中,不能松钩
    CS_ST_HANG,        // 摆动中,可以松钩
    CS_ST_FALL,        // 已松钩,下落中
    CS_ST_MISS,        // 没压住,箱子掉出屏幕
    CS_ST_COLLAPSE,    // 塔在倒
    CS_ST_OVER,        // 本局结束
} cs_state_t;

typedef enum {
    CS_LAND_NONE = 0,
    CS_LAND_PERFECT,
    CS_LAND_OK,
    CS_LAND_MISS,
} cs_land_t;

// cs_model_step / cs_model_release 返回的事件位
#define CS_EV_RELEASE    (1u << 0)
#define CS_EV_LAND       (1u << 1)   // 落稳(含完美)
#define CS_EV_PERFECT    (1u << 2)
#define CS_EV_MISS       (1u << 3)   // 落点没压住,箱子开始掉落
#define CS_EV_LIFE_LOST  (1u << 4)   // 掉落动画结束,扣命
#define CS_EV_LIFE_GAIN  (1u << 5)
#define CS_EV_COLLAPSE   (1u << 6)
#define CS_EV_OVER       (1u << 7)
#define CS_EV_HANG       (1u << 8)   // 新箱子就位,可以松钩

typedef struct {
    cs_state_t state;
    uint32_t rng;
    uint16_t timer;            // 当前状态已持续的节拍数

    // 塔:每层中心 x(Q8,不含晃动)
    uint16_t count;
    int32_t layer_x[CS_MAX_LAYERS];

    // 当前箱子:中心 x、底边世界 y(Q8),速度为每节拍位移(Q8)
    int32_t box_x;
    int32_t box_y;
    int32_t vx;
    int32_t vy;

    // 摆动:相位一整圈为 2^32
    uint32_t swing_phase;
    uint32_t swing_step;
    int32_t swing_amp;         // Q8 像素

    // 晃动:塔顶位移 = sway_amp * sin(sway_phase)
    uint32_t sway_phase;
    int32_t sway_amp;          // Q8 像素

    int32_t cam;               // 镜头高度,Q8 世界像素

    uint32_t score;
    uint16_t perfects;
    uint16_t combo;
    uint16_t best_combo;
    uint8_t lives;

    cs_land_t last_land;
    int32_t last_offset;       // 最近一次落点误差(Q8)
    int16_t collapse_from;     // 倒塌时第一块倒下的层
    int8_t collapse_dir;       // 倒向:-1 左,+1 右
    bool over_by_collapse;
} cs_model_t;

void cs_model_init(cs_model_t *m, uint32_t seed);
// 标题页展示用:预先摆好 3 层略有错位的小塔,轻微晃动;不计分。
void cs_model_init_demo(cs_model_t *m, uint32_t seed);
// 推进一个节拍,返回事件位。
uint32_t cs_model_step(cs_model_t *m);
// 松钩;只有摆动状态有效,返回 CS_EV_RELEASE,否则返回 0。
uint32_t cs_model_release(cs_model_t *m);

// ---- 难度 ----
uint32_t cs_swing_period_ms(uint16_t count);
int cs_swing_amp_px(uint16_t count);

// ---- 渲染查询 ----
// 屏幕 y(Q8)对应的世界 y,反之亦然。
int32_t cs_world_to_screen(const cs_model_t *m, int32_t world_y);
// 第 i 层当前的中心 x(含晃动,Q8)。
int32_t cs_layer_x(const cs_model_t *m, int i);
// 塔顶中心 x(含晃动,Q8);空塔时为地基中心。
int32_t cs_top_x(const cs_model_t *m);
// 当前塔顶的晃动位移(Q8)。
int32_t cs_sway(const cs_model_t *m);
// 当前箱子是否挂在绳上(下放或摆动中)。
bool cs_box_hooked(const cs_model_t *m);
// 当前箱子是否需要绘制。
bool cs_box_visible(const cs_model_t *m);
// 倒塌动画中第 i 层相对原位置的位移(像素,屏幕方向:dy 向下为正)。
void cs_collapse_offset(const cs_model_t *m, int i, int *dx, int *dy);

// sin,相位一整圈 2^32,返回 Q15(-32767..32767)。
int32_t cs_sin_q15(uint32_t phase);
