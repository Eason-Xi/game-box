// tests/test_crane_layout.c —— 布局常量检查:圆角避让、元素不重叠、与模型几何一致。
#include "bsp_display.h"
#include "bsp_display_rounding.h"
#include "crane_theme.h"

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
        if (!bsp_display_rounded_row_span(y, CT_SCREEN_W, CT_SCREEN_H,
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

static rect_t box_at(int cx, int top) {
    return (rect_t){ cx - CS_BOX_W / 2, top, cx + CS_BOX_W / 2 - 1, top + CS_BOX_H - 1 };
}

int main(void) {
    // 顶部状态栏:命数、分数、层数、电量互不重叠且在圆角内
    const rect_t lives = { CT_LIFE_X, CT_LIFE_Y,
                           CT_LIFE_X + CS_MAX_LIVES * (CT_LIFE_W + CT_LIFE_GAP) - CT_LIFE_GAP - 1,
                           CT_LIFE_Y + CT_LIFE_H - 1 };
    const rect_t battery = { CT_BATT_X, CT_BATT_Y,
                             CT_BATT_X + CT_BATT_W + CT_BATT_NUB_W - 1, CT_BATT_Y + CT_BATT_H - 1 };
    const rect_t battery_text = { CT_BATT_TEXT_R - CT_BATT_TEXT_W, CT_BATT_TEXT_Y,
                                  CT_BATT_TEXT_R - 1, CT_BATT_TEXT_Y + 21 };
    const rect_t score = { CT_SCREEN_W / 2 - CT_SCORE_HALF_W, CT_SCORE_Y,
                           CT_SCREEN_W / 2 + CT_SCORE_HALF_W - 1, CT_SCORE_Y + 42 };
    const rect_t height = { CT_SCREEN_W / 2 - 30, CT_HEIGHT_Y, CT_SCREEN_W / 2 + 29, CT_HEIGHT_Y + 21 };
    CHECK(rect_visible(lives));
    CHECK(rect_visible(battery));
    CHECK(rect_visible(battery_text));
    CHECK(!overlap(battery_text, battery));
    CHECK(!overlap(score, lives));
    CHECK(!overlap(score, battery_text));
    CHECK(!overlap(score, battery));
    CHECK(!overlap(height, score));

    // 吊着的箱子(就位后摆到最远端也)不挡状态栏,且整箱在屏内
    const int hang_top = CS_PIVOT_Y + CS_ROPE_LEN;           // 摆到正中时的顶边
    const int amp = 78;                                      // cs_swing_amp_px 上限
    const rect_t hang_center = box_at(CS_CENTER_X, hang_top);
    const rect_t hang_left = box_at(CS_CENTER_X - amp, hang_top - 14);   // 摆到端点时抬高约 13px
    const rect_t hang_right = box_at(CS_CENTER_X + amp, hang_top - 14);
    CHECK(!overlap(hang_center, height));
    CHECK(!overlap(hang_left, height) && !overlap(hang_right, height));
    CHECK(hang_left.y1 - CT_HOOK_H > height.y2);
    CHECK(rect_visible(hang_left) && rect_visible(hang_right));

    // 落点提示在吊着的箱子和塔顶之间
    const rect_t popup = { 0, CT_POPUP_Y - 18, CT_SCREEN_W - 1, CT_POPUP_Y + 33 };
    CHECK(popup.y1 > hang_center.y2);
    CHECK(popup.y2 < CS_TOP_SCREEN_Y);

    // 标题页:Logo 在电量下方,菜单卡片不挡展示小塔,卡片在屏内
    const rect_t logo_en = { 40, CT_LOGO_EN_Y, 199, CT_LOGO_EN_Y + 21 };
    const rect_t menu = { CT_MENU_X, CT_MENU_Y, CT_MENU_X + CT_MENU_W - 1, CT_MENU_Y + CT_MENU_H - 1 };
    CHECK(CT_LOGO_Y > CT_BATT_Y + CT_BATT_H);
    CHECK(CT_LOGO_Y + 40 <= CT_LOGO_EN_Y + 4);
    CHECK(!overlap(logo_en, menu));
    CHECK(rect_visible(menu));
    const int demo_top = CS_GROUND_SCREEN_Y - 3 * CS_BOX_H;   // cs_model_init_demo 的 3 层
    CHECK(menu.y2 < demo_top);

    // 沙土上的提示(最长一句约 170px,按 190px、行高 20px 校验)在底部圆角内完整可见
    const rect_t hint = { (CT_SCREEN_W - 190) / 2, CT_HINT_Y, (CT_SCREEN_W + 190) / 2 - 1, CT_HINT_Y + 19 };
    CHECK(hint.y1 >= CT_DIRT_Y);
    CHECK(rect_visible(hint));

    // 暂停/结算卡片在屏内,不挡电量
    const rect_t pause = { CT_PAUSE_X, CT_PAUSE_Y, CT_PAUSE_X + CT_PAUSE_W - 1, CT_PAUSE_Y + CT_PAUSE_H - 1 };
    const rect_t over = { CT_OVER_X, CT_OVER_Y, CT_OVER_X + CT_OVER_W - 1, CT_OVER_Y + CT_OVER_H - 1 };
    CHECK(rect_visible(pause));
    CHECK(rect_visible(over));
    CHECK(!overlap(over, battery) && !overlap(over, battery_text));
    CHECK(CT_PAUSE_H >= 40 + 3 * (CT_MENU_ROW_H + 2));
    CHECK(CT_OVER_ROWS_Y + 2 * CT_MENU_ROW_H <= CT_OVER_H - 6);
    CHECK(CT_OVER_BADGE_Y >= CT_OVER_STAT_Y + 4 * CT_OVER_STAT_H - 4);
    CHECK(CT_OVER_ROWS_Y >= CT_OVER_BADGE_Y + 20);

    // 场景几何:地基、草皮、沙土自上而下;塔镜头位置在塔顶留出下落距离
    CHECK(CT_GRASS_Y > CS_GROUND_SCREEN_Y && CT_GRASS_Y < CS_GROUND_SCREEN_Y + CT_BASE_H);
    CHECK(CS_TOP_SCREEN_Y - hang_center.y2 >= 60);
    CHECK(CS_BASE_W > CS_BOX_W && CS_BOX_W > CT_TAPE_W);
    CHECK(CT_TOWER_POOL * CS_BOX_H > CT_SCREEN_H - CS_TOP_SCREEN_Y + CS_BOX_H);

    if (s_failures) {
        fprintf(stderr, "test_crane_layout: %d failure(s)\n", s_failures);
        return 1;
    }
    puts("test_crane_layout: OK");
    return 0;
}
