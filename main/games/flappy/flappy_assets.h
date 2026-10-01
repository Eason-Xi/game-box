// main/games/flappy/flappy_assets.h —— 精灵图的声明(定义见 assets/images/flappy_sprites.c,
// 由 tools/gamebox/gen_flappy_assets.py 生成)。字库改用游戏盒共用字库(gb_ui.h)。
#pragma once

#include "lvgl.h"

#define FLAPPY_BIRD_ANGLES 6
#define FLAPPY_BIRD_WINGS  3

LV_IMAGE_DECLARE(flappy_img_cloud_big);
LV_IMAGE_DECLARE(flappy_img_cloud_small);
LV_IMAGE_DECLARE(flappy_img_hills);
LV_IMAGE_DECLARE(flappy_img_ground_band);
LV_IMAGE_DECLARE(flappy_img_dirt);
LV_IMAGE_DECLARE(flappy_img_pipe_body);
LV_IMAGE_DECLARE(flappy_img_pipe_cap);

// [俯仰角序号][翅膀帧],俯仰角取值见 flappy_bird_angles(度,负值抬头)。
extern const lv_image_dsc_t *const flappy_img_bird[FLAPPY_BIRD_ANGLES][FLAPPY_BIRD_WINGS];
extern const int16_t flappy_bird_angles[FLAPPY_BIRD_ANGLES];
