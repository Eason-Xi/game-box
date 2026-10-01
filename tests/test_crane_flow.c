// tests/test_crane_flow.c —— 页面流程状态机与设置校验的主机测试。
#include "crane_flow.h"

#include <stdio.h>

static int s_failures;

#define CHECK(cond) do { \
    if (!(cond)) { \
        fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
        s_failures++; \
    } \
} while (0)

static void test_settings(void) {
    cf_settings_t s;
    cf_settings_default(&s);
    CHECK(s.version == CF_SETTINGS_VERSION && s.sound_on == 1);
    CHECK(s.best_score == 0 && s.best_height == 0);
    s.best_score = 42;
    s.reserved = 7;
    CHECK(cf_settings_sanitize(&s));
    CHECK(s.best_score == 42 && s.reserved == 0);
    s.sound_on = 5;
    CHECK(!cf_settings_sanitize(&s));
    CHECK(s.sound_on == 1 && s.best_score == 0);   // 非法时整体恢复默认
    s.version = 99;
    CHECK(!cf_settings_sanitize(&s));
}

static void test_title(void) {
    cf_settings_t s;
    cf_settings_default(&s);
    cf_flow_t f;
    cf_flow_init(&f, &s);
    CHECK(f.state == CF_TITLE && f.focus == CF_TITLE_ITEM_START);
    // 焦点循环
    CHECK(cf_flow_key(&f, CF_KEY_UP, 0) == CF_ACT_FOCUS);
    CHECK(f.focus == CF_TITLE_ITEMS - 1);
    CHECK(cf_flow_key(&f, CF_KEY_DOWN, 0) == CF_ACT_FOCUS);
    CHECK(f.focus == CF_TITLE_ITEM_START);
    // 切换音效
    cf_flow_key(&f, CF_KEY_DOWN, 0);
    CHECK(cf_flow_key(&f, CF_KEY_OK, 0) == CF_ACT_TOGGLE_SOUND);
    CHECK(f.settings.sound_on == 0);
    CHECK(cf_flow_key(&f, CF_KEY_OK, 0) == CF_ACT_TOGGLE_SOUND);
    CHECK(f.settings.sound_on == 1);
    CHECK(f.state == CF_TITLE);
    // 开始
    cf_flow_key(&f, CF_KEY_UP, 0);
    CHECK(cf_flow_key(&f, CF_KEY_OK, 0) == CF_ACT_START);
    CHECK(f.state == CF_PLAYING);
}

static void test_playing_and_pause(void) {
    cf_settings_t s;
    cf_settings_default(&s);
    cf_flow_t f;
    cf_flow_init(&f, &s);
    cf_flow_key(&f, CF_KEY_OK, 0);
    // 游戏中:OK 与 下 都是松钩,上是暂停
    CHECK(cf_flow_key(&f, CF_KEY_OK, 0) == CF_ACT_DROP);
    CHECK(cf_flow_key(&f, CF_KEY_DOWN, 0) == CF_ACT_DROP);
    CHECK(f.state == CF_PLAYING);
    CHECK(cf_flow_key(&f, CF_KEY_UP, 0) == CF_ACT_PAUSE);
    CHECK(f.state == CF_PAUSED && f.focus == CF_PAUSE_ITEM_RESUME);
    // 暂停页:上/下移动焦点,不会松钩
    CHECK(cf_flow_key(&f, CF_KEY_DOWN, 0) == CF_ACT_FOCUS);
    CHECK(f.focus == CF_PAUSE_ITEM_RESTART);
    CHECK(cf_flow_key(&f, CF_KEY_UP, 0) == CF_ACT_FOCUS);
    CHECK(cf_flow_key(&f, CF_KEY_OK, 0) == CF_ACT_RESUME);
    CHECK(f.state == CF_PLAYING);
    // 暂停 → 重新开始
    cf_flow_key(&f, CF_KEY_UP, 0);
    cf_flow_key(&f, CF_KEY_DOWN, 0);
    CHECK(cf_flow_key(&f, CF_KEY_OK, 0) == CF_ACT_START);
    CHECK(f.state == CF_PLAYING);
    // 暂停 → 返回标题
    cf_flow_key(&f, CF_KEY_UP, 0);
    cf_flow_key(&f, CF_KEY_UP, 0);
    CHECK(f.focus == CF_PAUSE_ITEM_TITLE);
    CHECK(cf_flow_key(&f, CF_KEY_OK, 0) == CF_ACT_TO_TITLE);
    CHECK(f.state == CF_TITLE && f.focus == CF_TITLE_ITEM_START);
}

static void test_over_and_records(void) {
    cf_settings_t s;
    cf_settings_default(&s);
    s.best_score = 30;
    s.best_height = 20;
    cf_flow_t f;
    cf_flow_init(&f, &s);
    bool new_score = true;
    // 标题页不会结算
    CHECK(!cf_flow_on_over(&f, 99, 99, 0, &new_score));
    CHECK(!new_score && f.state == CF_TITLE);

    cf_flow_key(&f, CF_KEY_OK, 0);
    // 分数没破纪录,层数破了:需要保存,但不显示"新纪录"
    CHECK(cf_flow_on_over(&f, 25, 22, 1000, &new_score));
    CHECK(!new_score);
    CHECK(f.state == CF_OVER && f.focus == CF_OVER_ITEM_AGAIN);
    CHECK(f.settings.best_score == 30 && f.settings.best_height == 22);
    // 结束后短暂锁定
    CHECK(cf_flow_key(&f, CF_KEY_OK, 1000 + CF_OVER_LOCK_MS - 1) == CF_ACT_NONE);
    CHECK(cf_flow_key(&f, CF_KEY_DOWN, 1000 + CF_OVER_LOCK_MS) == CF_ACT_FOCUS);
    CHECK(f.focus == CF_OVER_ITEM_TITLE);
    CHECK(cf_flow_key(&f, CF_KEY_UP, 1000 + CF_OVER_LOCK_MS) == CF_ACT_FOCUS);
    CHECK(cf_flow_key(&f, CF_KEY_OK, 1000 + CF_OVER_LOCK_MS) == CF_ACT_START);
    CHECK(f.state == CF_PLAYING);

    // 破分数纪录
    CHECK(cf_flow_on_over(&f, 31, 5, 5000, &new_score));
    CHECK(new_score && f.settings.best_score == 31 && f.settings.best_height == 22);
    // 两项都没破:不保存
    cf_flow_key(&f, CF_KEY_OK, 9000);
    CHECK(f.state == CF_PLAYING);
    CHECK(!cf_flow_on_over(&f, 3, 1, 9500, &new_score));
    CHECK(!new_score);
    // 超大分数截断到 16 位
    cf_flow_key(&f, CF_KEY_OK, 20000);
    CHECK(cf_flow_on_over(&f, 100000, 70000, 20000, &new_score));
    CHECK(f.settings.best_score == 0xFFFF && f.settings.best_height == 0xFFFF);
    // 结算 → 返回标题
    cf_flow_key(&f, CF_KEY_DOWN, 30000);
    CHECK(cf_flow_key(&f, CF_KEY_OK, 30000) == CF_ACT_TO_TITLE);
    CHECK(f.state == CF_TITLE);
}

int main(void) {
    test_settings();
    test_title();
    test_playing_and_pause();
    test_over_and_records();
    if (s_failures) {
        fprintf(stderr, "test_crane_flow: %d failure(s)\n", s_failures);
        return 1;
    }
    puts("test_crane_flow: OK");
    return 0;
}
