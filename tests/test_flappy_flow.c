// tests/test_flappy_flow.c —— 页面流程与按键映射的主机测试。
#include "flappy_flow.h"

#include <stdio.h>
#include <string.h>

static int s_failures;

#define CHECK(cond) do { \
    if (!(cond)) { \
        fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
        s_failures++; \
    } \
} while (0)

static void fresh(ff_flow_t *f) {
    ff_settings_t s;
    ff_settings_default(&s);
    ff_flow_init(f, &s);
}

static void test_defaults_and_sanitize(void) {
    ff_settings_t s;
    ff_settings_default(&s);
    CHECK(s.version == FF_SETTINGS_VERSION);
    CHECK(s.difficulty == FB_DIFF_NORMAL);
    CHECK(s.sound_on == 1);
    CHECK(s.best[0] == 0 && s.best[1] == 0 && s.best[2] == 0);
    CHECK(ff_settings_sanitize(&s));

    s.best[2] = 77;
    s.reserved = 9;
    CHECK(ff_settings_sanitize(&s));
    CHECK(s.best[2] == 77 && s.reserved == 0);   // 合法数据保留纪录

    ff_settings_t bad = s;
    bad.difficulty = 3;
    CHECK(!ff_settings_sanitize(&bad));
    CHECK(bad.difficulty == FB_DIFF_NORMAL && bad.best[2] == 0);
    bad = s;
    bad.version = 0;
    CHECK(!ff_settings_sanitize(&bad));
    bad = s;
    bad.sound_on = 2;
    CHECK(!ff_settings_sanitize(&bad));
    CHECK(bad.sound_on == 1);
}

static void test_title_menu(void) {
    ff_flow_t f;
    fresh(&f);
    CHECK(f.state == FF_TITLE && f.focus == FF_TITLE_ITEM_START);
    CHECK(ff_flow_key(&f, FF_KEY_UP, 0) == FF_ACT_FOCUS);
    CHECK(f.focus == FF_TITLE_ITEM_SOUND);                 // 上键在顶部回绕
    CHECK(ff_flow_key(&f, FF_KEY_DOWN, 0) == FF_ACT_FOCUS);
    CHECK(f.focus == FF_TITLE_ITEM_START);
    CHECK(ff_flow_key(&f, FF_KEY_DOWN, 0) == FF_ACT_FOCUS);
    CHECK(f.focus == FF_TITLE_ITEM_DIFF);

    // 难度循环:普通 → 困难 → 简单 → 普通
    CHECK(ff_flow_key(&f, FF_KEY_OK, 0) == FF_ACT_CYCLE_DIFF);
    CHECK(f.settings.difficulty == FB_DIFF_HARD);
    CHECK(ff_flow_key(&f, FF_KEY_OK, 0) == FF_ACT_CYCLE_DIFF);
    CHECK(f.settings.difficulty == FB_DIFF_EASY);
    CHECK(ff_flow_key(&f, FF_KEY_OK, 0) == FF_ACT_CYCLE_DIFF);
    CHECK(f.settings.difficulty == FB_DIFF_NORMAL);
    CHECK(f.state == FF_TITLE);

    ff_flow_key(&f, FF_KEY_DOWN, 0);
    CHECK(f.focus == FF_TITLE_ITEM_SOUND);
    CHECK(ff_flow_key(&f, FF_KEY_OK, 0) == FF_ACT_TOGGLE_SOUND);
    CHECK(f.settings.sound_on == 0);
    CHECK(ff_flow_key(&f, FF_KEY_OK, 0) == FF_ACT_TOGGLE_SOUND);
    CHECK(f.settings.sound_on == 1);

    ff_flow_key(&f, FF_KEY_DOWN, 0);
    CHECK(f.focus == FF_TITLE_ITEM_START);
    CHECK(ff_flow_key(&f, FF_KEY_OK, 0) == FF_ACT_START);
    CHECK(f.state == FF_READY);
}

static void test_ready_and_play(void) {
    ff_flow_t f;
    fresh(&f);
    ff_flow_key(&f, FF_KEY_OK, 0);
    CHECK(f.state == FF_READY);
    CHECK(ff_flow_key(&f, FF_KEY_DOWN, 0) == FF_ACT_TO_TITLE);
    CHECK(f.state == FF_TITLE && f.focus == FF_TITLE_ITEM_START);

    ff_flow_key(&f, FF_KEY_OK, 0);
    CHECK(ff_flow_key(&f, FF_KEY_UP, 0) == FF_ACT_FLAP);   // 上键也能起飞
    CHECK(f.state == FF_PLAYING);
    CHECK(ff_flow_key(&f, FF_KEY_OK, 0) == FF_ACT_FLAP);
    CHECK(ff_flow_key(&f, FF_KEY_UP, 0) == FF_ACT_FLAP);
    CHECK(f.state == FF_PLAYING);
}

static void test_pause_menu(void) {
    ff_flow_t f;
    fresh(&f);
    ff_flow_key(&f, FF_KEY_OK, 0);
    ff_flow_key(&f, FF_KEY_OK, 0);
    CHECK(ff_flow_key(&f, FF_KEY_DOWN, 0) == FF_ACT_PAUSE);
    CHECK(f.state == FF_PAUSED && f.focus == FF_PAUSE_ITEM_RESUME);
    CHECK(ff_flow_key(&f, FF_KEY_OK, 0) == FF_ACT_RESUME);
    CHECK(f.state == FF_PLAYING);

    ff_flow_key(&f, FF_KEY_DOWN, 0);
    CHECK(ff_flow_key(&f, FF_KEY_DOWN, 0) == FF_ACT_FOCUS);
    CHECK(f.focus == FF_PAUSE_ITEM_RESTART);
    CHECK(ff_flow_key(&f, FF_KEY_OK, 0) == FF_ACT_START);
    CHECK(f.state == FF_READY);

    ff_flow_key(&f, FF_KEY_OK, 0);
    ff_flow_key(&f, FF_KEY_DOWN, 0);
    CHECK(ff_flow_key(&f, FF_KEY_UP, 0) == FF_ACT_FOCUS);
    CHECK(f.focus == FF_PAUSE_ITEM_TITLE);
    CHECK(ff_flow_key(&f, FF_KEY_OK, 0) == FF_ACT_TO_TITLE);
    CHECK(f.state == FF_TITLE);
}

static void test_dying_and_over(void) {
    ff_flow_t f;
    fresh(&f);
    ff_flow_key(&f, FF_KEY_OK, 0);
    ff_flow_key(&f, FF_KEY_OK, 0);
    ff_flow_on_hit(&f);
    CHECK(f.state == FF_DYING);
    CHECK(ff_flow_key(&f, FF_KEY_OK, 100) == FF_ACT_NONE);
    CHECK(ff_flow_key(&f, FF_KEY_DOWN, 100) == FF_ACT_NONE);

    CHECK(ff_flow_on_over(&f, 12, 1000));                  // 首次得分即新纪录
    CHECK(f.state == FF_OVER && f.focus == FF_OVER_ITEM_AGAIN);
    CHECK(ff_flow_best(&f) == 12);
    CHECK(!ff_flow_on_over(&f, 30, 1001));                 // 重复结算被忽略

    // 锁定期内所有按键无效,包括回绕的时间戳
    CHECK(ff_flow_key(&f, FF_KEY_OK, 1000 + FF_OVER_LOCK_MS - 1) == FF_ACT_NONE);
    CHECK(f.state == FF_OVER);
    CHECK(ff_flow_key(&f, FF_KEY_DOWN, 1000 + FF_OVER_LOCK_MS) == FF_ACT_FOCUS);
    CHECK(f.focus == FF_OVER_ITEM_TITLE);
    CHECK(ff_flow_key(&f, FF_KEY_DOWN, 2000) == FF_ACT_FOCUS);
    CHECK(f.focus == FF_OVER_ITEM_AGAIN);
    CHECK(ff_flow_key(&f, FF_KEY_OK, 2000) == FF_ACT_START);
    CHECK(f.state == FF_READY);

    // 直接落地(未经 DYING)同样结算;低于纪录不刷新
    ff_flow_key(&f, FF_KEY_OK, 3000);
    CHECK(!ff_flow_on_over(&f, 5, 3000));
    CHECK(ff_flow_best(&f) == 12);
    CHECK(ff_flow_key(&f, FF_KEY_UP, 3000 + FF_OVER_LOCK_MS) == FF_ACT_FOCUS);
    CHECK(ff_flow_key(&f, FF_KEY_OK, 3000 + FF_OVER_LOCK_MS) == FF_ACT_TO_TITLE);
    CHECK(f.state == FF_TITLE);

    // 时间戳回绕时锁定依旧有效
    fresh(&f);
    ff_flow_key(&f, FF_KEY_OK, 0);
    ff_flow_key(&f, FF_KEY_OK, 0);
    ff_flow_on_over(&f, 0, 0xFFFFFF00u);
    CHECK(ff_flow_key(&f, FF_KEY_OK, 0x00000010u) == FF_ACT_NONE);
    CHECK(ff_flow_key(&f, FF_KEY_OK, 0xFFFFFF00u + FF_OVER_LOCK_MS) == FF_ACT_START);
}

static void test_best_is_per_difficulty(void) {
    ff_flow_t f;
    fresh(&f);
    f.settings.difficulty = FB_DIFF_HARD;
    ff_flow_key(&f, FF_KEY_OK, 0);
    ff_flow_key(&f, FF_KEY_OK, 0);
    CHECK(ff_flow_on_over(&f, 70000, 0));                  // 超出 16 位时饱和
    CHECK(f.settings.best[FB_DIFF_HARD] == 0xFFFF);
    CHECK(f.settings.best[FB_DIFF_NORMAL] == 0);
    CHECK(ff_flow_on_over(&f, 1, 0) == false);             // 已在 OVER,不重复结算
}

static void test_init_sanitizes(void) {
    ff_settings_t s;
    memset(&s, 0xFF, sizeof(s));
    ff_flow_t f;
    ff_flow_init(&f, &s);
    CHECK(f.settings.version == FF_SETTINGS_VERSION);
    CHECK(f.settings.difficulty == FB_DIFF_NORMAL);
    CHECK(f.state == FF_TITLE);
}

int main(void) {
    test_defaults_and_sanitize();
    test_title_menu();
    test_ready_and_play();
    test_pause_menu();
    test_dying_and_over();
    test_best_is_per_difficulty();
    test_init_sanitizes();
    if (s_failures) {
        fprintf(stderr, "test_flappy_flow: %d failure(s)\n", s_failures);
        return 1;
    }
    puts("test_flappy_flow: PASS");
    return 0;
}
