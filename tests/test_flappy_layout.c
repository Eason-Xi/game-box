// tests/test_flappy_layout.c —— 布局常量检查:圆角避让、元素不重叠、不压地面。
#include "bsp_display.h"
#include "bsp_display_rounding.h"
#include "flappy_theme.h"

#include <stdbool.h>
#include <stdio.h>

static int s_failures;

#define CHECK(cond) do { \
    if (!(cond)) { \
        fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
        s_failures++; \
    } \
} while (0)

typedef struct { int x1, y1, x2, y2; } rect_t;   // 闭区间

static bool rect_visible(rect_t r) {
    for (int y = r.y1; y <= r.y2; y++) {
        int32_t vx1, vx2;
        if (!bsp_display_rounded_row_span(y, FT_SCREEN_W, FT_SCREEN_H,
                                          BSP_LVGL_SCREEN_RADIUS, &vx1, &vx2)) {
            return false;
        }
        if (r.x1 < vx1 || r.x2 > vx2) return false;
    }
    return true;
}

static bool overlap(rect_t a, rect_t b) {
    return a.x1 <= b.x2 && b.x1 <= a.x2 && a.y1 <= b.y2 && b.y1 <= a.y2;
}

int main(void) {
    const rect_t battery = { FT_BATT_X, FT_BATT_Y,
                             FT_BATT_X + FT_BATT_W + FT_BATT_NUB_W - 1, FT_BATT_Y + FT_BATT_H - 1 };
    const rect_t battery_text = { FT_BATT_TEXT_R - FT_BATT_TEXT_W, FT_BATT_TEXT_Y,
                                  FT_BATT_TEXT_R - 1, FT_BATT_TEXT_Y + 21 };
    const rect_t score = { FT_SCREEN_W / 2 - FT_SCORE_HALF_W, FT_SCORE_Y,
                           FT_SCREEN_W / 2 + FT_SCORE_HALF_W - 1, FT_SCORE_Y + 40 };
    CHECK(rect_visible(battery));
    CHECK(rect_visible(battery_text));
    CHECK(!overlap(battery_text, battery));
    CHECK(!overlap(score, battery_text));
    CHECK(!overlap(score, battery));

    // 标题页:Logo、小鸟、菜单卡片自上而下不重叠,卡片不压地面
    const rect_t title_bird = { FT_TITLE_BIRD_X - FT_BIRD_SPRITE / 2, FT_TITLE_BIRD_Y - FT_BIRD_SPRITE / 2,
                                FT_TITLE_BIRD_X + FT_BIRD_SPRITE / 2 - 1, FT_TITLE_BIRD_Y + FT_BIRD_SPRITE / 2 - 1 };
    const rect_t logo_en = { 40, FT_LOGO_EN_Y, 199, FT_LOGO_EN_Y + 21 };
    const rect_t menu = { FT_MENU_X, FT_MENU_Y, FT_MENU_X + FT_MENU_W - 1, FT_MENU_Y + FT_MENU_H - 1 };
    CHECK(FT_LOGO_Y + 38 <= FT_LOGO_EN_Y + 4);
    CHECK(!overlap(logo_en, title_bird));
    CHECK(!overlap(title_bird, menu));
    CHECK(menu.y2 < FT_GROUND_Y);
    CHECK(rect_visible(menu));
    CHECK(FT_LOGO_Y > FT_BATT_Y + FT_BATT_H);   // Logo 在电量下方

    // 沙土上的操作提示(最长一句实测约 160px,按 180px、16px 字库行高 20px 校验)
    // 在底部圆角内完整可见
    const rect_t hint = { (FT_SCREEN_W - 180) / 2, FT_HINT_Y, (FT_SCREEN_W + 180) / 2 - 1, FT_HINT_Y + 19 };
    CHECK(hint.y1 >= FT_GROUND_Y + FT_GROUND_BAND_H);   // 在滚动草皮带之下
    CHECK(rect_visible(hint));

    // 准备提示不挡小鸟(准备阶段小鸟在 FB_BIRD_START_Y 附近浮动 ±5)
    const rect_t ready_bird = { FB_BIRD_X - 20, FB_BIRD_START_Y - 25, FB_BIRD_X + 19, FB_BIRD_START_Y + 24 };
    const rect_t ready_title = { 60, FT_READY_TITLE_Y, 179, FT_READY_TITLE_Y + 31 };
    const rect_t ready_hint = { 20, FT_READY_HINT_Y, 219, FT_READY_HINT_Y + 21 };
    CHECK(!overlap(ready_bird, ready_title));
    CHECK(!overlap(ready_bird, ready_hint));
    CHECK(ready_hint.y2 < FT_HILLS_Y + FT_HILLS_H);

    // 暂停/结算卡片在屏内且不压地面、不挡电量
    const rect_t pause = { FT_PAUSE_X, FT_PAUSE_Y, FT_PAUSE_X + FT_PAUSE_W - 1, FT_PAUSE_Y + FT_PAUSE_H - 1 };
    const rect_t over = { FT_OVER_X, FT_OVER_Y, FT_OVER_X + FT_OVER_W - 1, FT_OVER_Y + FT_OVER_H - 1 };
    CHECK(rect_visible(pause) && pause.y2 < FT_GROUND_Y);
    CHECK(rect_visible(over) && over.y2 < FT_GROUND_Y);
    CHECK(!overlap(over, battery) && !overlap(over, battery_text));

    // 场景几何与模型一致
    CHECK(FT_PIPE_CAP_W > FB_PIPE_W && (FT_PIPE_CAP_W - FB_PIPE_W) % 2 == 0);
    CHECK(FT_GROUND_Y == FB_GROUND_Y && FT_SCREEN_W == FB_FIELD_W);
    CHECK(FB_BIRD_HALF_W * 2 < FT_BIRD_SPRITE && FB_BIRD_HALF_H * 2 < FT_BIRD_SPRITE);

    if (s_failures) {
        fprintf(stderr, "test_flappy_layout: %d failure(s)\n", s_failures);
        return 1;
    }
    puts("test_flappy_layout: PASS");
    return 0;
}
