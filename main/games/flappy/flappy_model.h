// main/flappy_model.h —— 飞吧小鸟的纯游戏模型:物理、管道、计分、碰撞。
//
// 不依赖 ESP-IDF / LVGL,可在主机上测试。坐标单位为屏幕像素,内部用 Q8 定点
// (1 像素 = 256)。模型以固定 60Hz 节拍推进(fb_model_step 一次 = 1 tick),
// 由调用方根据真实时间补足节拍,保证手感与刷新率无关。
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define FB_Q                 8
#define FB_ONE               (1 << FB_Q)
#define FB_TICK_HZ           60
#define FB_TICK_US           (1000000 / FB_TICK_HZ)

// 游戏场地(与屏幕同宽,地面以上为飞行区)
#define FB_FIELD_W           240
#define FB_GROUND_Y          272

// 小鸟:水平位置固定,碰撞盒比贴图略小,擦边不算撞
#define FB_BIRD_X            72
#define FB_BIRD_HALF_W       11
#define FB_BIRD_HALF_H       8
#define FB_BIRD_START_Y      150

// 管道
#define FB_PIPE_W            46
#define FB_PIPE_COUNT        3
#define FB_PIPE_SPACING      146   // 相邻管道左边缘的水平间距
#define FB_PIPE_FIRST_X      (FB_FIELD_W + 70)
#define FB_GAP_MARGIN        34    // 缺口与天花板/地面至少留出的距离

// 物理参数(Q8,每 tick)
#define FB_GRAVITY           67    // 约 0.26 px/tick²
#define FB_FLAP_VELOCITY     (-1203) // 约 -4.7 px/tick
#define FB_MAX_FALL          1792  // 7 px/tick
#define FB_HIT_BOUNCE        (-512) // 撞管后轻微上弹再坠落

// 奖牌门槛
#define FB_MEDAL_BRONZE_AT   10
#define FB_MEDAL_SILVER_AT   20
#define FB_MEDAL_GOLD_AT     30
#define FB_MEDAL_PLATINUM_AT 40

typedef enum {
    FB_DIFF_EASY = 0,
    FB_DIFF_NORMAL,
    FB_DIFF_HARD,
    FB_DIFF_COUNT,
} fb_difficulty_t;

typedef enum {
    FB_STATE_READY = 0,  // 等待第一次扇翅:小鸟上下浮动,管道静止
    FB_STATE_PLAYING,
    FB_STATE_DYING,      // 撞管后坠落,管道冻结,不再接受操作
    FB_STATE_OVER,       // 已落地
} fb_state_t;

typedef enum {
    FB_MEDAL_NONE = 0,
    FB_MEDAL_BRONZE,
    FB_MEDAL_SILVER,
    FB_MEDAL_GOLD,
    FB_MEDAL_PLATINUM,
} fb_medal_t;

// fb_model_flap / fb_model_step 返回的事件位
#define FB_EV_FLAP   (1u << 0)
#define FB_EV_SCORE  (1u << 1)
#define FB_EV_HIT    (1u << 2)   // 撞到管道或地面,本局结束的那一刻
#define FB_EV_LAND   (1u << 3)   // 落地,进入 OVER

typedef struct {
    int16_t gap_h;       // 缺口高度(px)
    int16_t speed_q8;    // 初始水平速度(Q8 px/tick)
    int16_t max_shift;   // 相邻缺口中心最大垂直落差(px)
} fb_params_t;

typedef struct {
    int32_t x_q8;        // 管道左边缘
    int16_t gap_y;       // 缺口中心
    bool scored;
} fb_pipe_t;

typedef struct {
    fb_state_t state;
    fb_difficulty_t difficulty;
    int32_t bird_y_q8;   // 小鸟中心
    int32_t vel_q8;
    fb_pipe_t pipes[FB_PIPE_COUNT];
    uint32_t score;
    uint32_t ticks;      // 本局累计节拍,用于浮动与扇翅动画
    uint32_t ground_q8;  // 地面滚动偏移(只增不减,由 UI 取模)
    uint32_t rng;
} fb_model_t;

const fb_params_t *fb_params(fb_difficulty_t difficulty);

void fb_model_init(fb_model_t *m, fb_difficulty_t difficulty, uint32_t seed);
uint32_t fb_model_flap(fb_model_t *m);
uint32_t fb_model_step(fb_model_t *m);

// 当前水平速度:每 10 分提速 4%,最多提 20%。
int32_t fb_model_speed_q8(const fb_model_t *m);
// 缺口上下沿(px)。
int fb_pipe_gap_top(const fb_model_t *m, const fb_pipe_t *p);
int fb_pipe_gap_bottom(const fb_model_t *m, const fb_pipe_t *p);

// 小鸟俯仰角(度,负值抬头):上升时 -25°,下落越快越低头,最低 90°。
int fb_bird_angle_deg(int32_t vel_q8);
// 扇翅动画帧 0..2;坠落/结束时固定为中间帧。
int fb_bird_wing_frame(const fb_model_t *m);

fb_medal_t fb_medal_for(uint32_t score);

// 伪随机数(LCG),暴露出来便于测试可复现。
uint32_t fb_rng_next(uint32_t *state);
