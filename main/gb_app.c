// main/gb_app.c —— 游戏盒应用核心。
//
// 线程模型：
//   * 按键回调运行在 button 组件的 esp_timer 任务里 —— 只入队，立即返回。
//   * gb_input 任务串行处理按键：导航状态机决定动作，切换页面时持 LVGL 锁。
//     队列空闲时每 200ms 醒来推进一次空闲调暗状态机。
//   * 游戏的 lv_timer 在 LVGL 任务里运行；音效在 gb_sfx 任务里播放。
#include "gb_app.h"

#include "bsp_button.h"
#include "bsp_display.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "gb_idle.h"
#include "gb_launcher.h"
#include "gb_nav.h"
#include "gb_registry.h"
#include "gb_score.h"
#include "gb_sfx.h"
#include "gb_text.h"
#include "gb_ui.h"

static const char *TAG = "gb_app";

// gb_nav 自带一份按键枚举以便主机测试，这里保证它与 BSP 的取值一致。
_Static_assert((int)GB_KEY_UP == (int)BSP_BTN_UP && (int)GB_KEY_DOWN == (int)BSP_BTN_DOWN &&
               (int)GB_KEY_OK == (int)BSP_BTN_OK, "gb_key_t must match bsp_btn_t");
_Static_assert((int)GB_KEY_EV_PRESS == (int)BSP_BTN_PRESS &&
               (int)GB_KEY_EV_CLICK == (int)BSP_BTN_CLICK &&
               (int)GB_KEY_EV_DOUBLE == (int)BSP_BTN_DOUBLE &&
               (int)GB_KEY_EV_LONG == (int)BSP_BTN_LONG, "gb_key_ev_t must match bsp_btn_ev_t");

#define INPUT_QUEUE_DEPTH  8
#define INPUT_TASK_STACK   6144     // 游戏的 enter() 在本任务里建界面，栈给足
#define IDLE_POLL_MS       200
#define LOCK_TIMEOUT_MS    1000
#define DIM_AFTER_MS       30000u
#define OFF_AFTER_MS       90000u
#define BACKLIGHT_ACTIVE   100
#define BACKLIGHT_DIM      15

typedef struct {
    bsp_btn_t btn;
    bsp_btn_ev_t ev;
} input_event_t;

static QueueHandle_t s_queue;
static volatile bool s_input_ready;
static volatile bool s_playing;
static gb_nav_t s_nav;
static gb_idle_t s_idle;
static gb_power_t s_power = GB_POWER_ACTIVE;
static lv_obj_t *s_confirm;   // 退出确认框（建在 lv_layer_top 上）

static uint32_t now_ms(void) {
    return (uint32_t)(esp_timer_get_time() / 1000);
}

void gb_app_set_playing(bool playing) {
    s_playing = playing;
}

static void apply_power(gb_power_t power) {
    if (power == s_power) return;
    s_power = power;
    bsp_display_backlight(power == GB_POWER_ACTIVE ? BACKLIGHT_ACTIVE
                          : power == GB_POWER_DIM  ? BACKLIGHT_DIM : 0);
}

// ---------------------------------------------------------------- 退出确认框

static void confirm_show(void) {
    if (s_confirm) return;
    s_confirm = gb_ui_box(lv_layer_top(), GB_UI_W, GB_UI_H);
    lv_obj_set_style_bg_color(s_confirm, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_confirm, LV_OPA_60, 0);

    lv_obj_t *panel = gb_ui_rect(s_confirm, 0, 0, 196, 132, GB_COLOR_PANEL, 16);
    lv_obj_center(panel);
    lv_obj_set_style_border_color(panel, lv_color_hex(GB_COLOR_HILITE), 0);
    lv_obj_set_style_border_width(panel, 2, 0);

    lv_obj_t *title = gb_ui_label(panel, &gb_font_20, GB_COLOR_TEXT, GB_TXT_EXIT_TITLE);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 22);
    lv_obj_t *ok = gb_ui_label(panel, &gb_font_14, GB_COLOR_HILITE, GB_TXT_EXIT_HINT_OK);
    lv_obj_align(ok, LV_ALIGN_TOP_MID, 0, 66);
    lv_obj_t *back = gb_ui_label(panel, &gb_font_14, GB_COLOR_MUTED, GB_TXT_EXIT_HINT_BACK);
    lv_obj_align(back, LV_ALIGN_TOP_MID, 0, 92);
}

static void confirm_hide(void) {
    if (!s_confirm) return;
    lv_obj_delete(s_confirm);
    s_confirm = NULL;
}

// ---------------------------------------------------------------- 动作

static void do_launch(size_t index) {
    const gb_game_t *game = gb_registry_get(index);
    if (!game || !bsp_lvgl_lock(LOCK_TIMEOUT_MS)) return;
    gb_sfx_play(&GB_TUNE_CONFIRM);
    gb_launcher_destroy();          // 先删启动页，让游戏拿到尽量多的内存
    s_playing = false;
    game->enter();
    gb_nav_launched(&s_nav);
    bsp_lvgl_unlock();

    if (game->start) {
        esp_err_t err = game->start();
        if (err != ESP_OK) ESP_LOGW(TAG, "%s start 失败: %s", game->id, esp_err_to_name(err));
    }
    ESP_LOGI(TAG, "进入 %s，空闲堆 %u", game->id, (unsigned)esp_get_free_heap_size());
}

static void do_exit(size_t index) {
    const gb_game_t *game = gb_registry_get(index);
    if (!game) return;
    if (game->stop) {
        esp_err_t err = game->stop();
        if (err != ESP_OK) {
            // 保留确认框与游戏页面：后台任务可能仍在访问它们，删掉会出事。
            ESP_LOGE(TAG, "%s stop 失败: %s，暂不退出", game->id, esp_err_to_name(err));
            return;
        }
    }
    if (!bsp_lvgl_lock(LOCK_TIMEOUT_MS)) return;
    confirm_hide();
    game->exit();
    s_playing = false;
    gb_nav_exited(&s_nav);
    gb_launcher_show(s_nav.selected);
    bsp_lvgl_unlock();
    gb_sfx_play(&GB_TUNE_BACK);
    ESP_LOGI(TAG, "退出 %s，空闲堆 %u", game->id, (unsigned)esp_get_free_heap_size());
}

static void with_game_locked(size_t index, gb_action_t action, const input_event_t *in) {
    const gb_game_t *game = gb_registry_get(index);
    if (!game || !bsp_lvgl_lock(LOCK_TIMEOUT_MS)) return;
    switch (action) {
    case GB_ACT_FORWARD:
        game->key(in->btn, in->ev);
        break;
    case GB_ACT_ASK_EXIT:
        if (game->pause) game->pause(true);
        confirm_show();
        gb_sfx_play(&GB_TUNE_ASK);
        break;
    case GB_ACT_CANCEL_EXIT:
        confirm_hide();
        if (game->pause) game->pause(false);
        gb_sfx_play(&GB_TUNE_MOVE);
        break;
    default:
        break;
    }
    bsp_lvgl_unlock();
}

static void process(const input_event_t *in) {
    const bool swallowed = gb_idle_input(&s_idle, now_ms(), (gb_key_t)in->btn, (gb_key_ev_t)in->ev);
    apply_power(s_idle.state);
    if (swallowed) return;

    const gb_nav_result_t r = gb_nav_handle(&s_nav, (gb_key_t)in->btn, (gb_key_ev_t)in->ev);
    switch (r.action) {
    case GB_ACT_SELECT:
        if (bsp_lvgl_lock(LOCK_TIMEOUT_MS)) {
            gb_launcher_select(r.index);
            bsp_lvgl_unlock();
        }
        gb_sfx_play(&GB_TUNE_MOVE);
        break;
    case GB_ACT_LAUNCH:
        do_launch(r.index);
        break;
    case GB_ACT_EXIT:
        do_exit(r.index);
        break;
    case GB_ACT_FORWARD:
    case GB_ACT_ASK_EXIT:
    case GB_ACT_CANCEL_EXIT:
        with_game_locked(r.index, r.action, in);
        break;
    case GB_ACT_NONE:
    default:
        break;
    }
}

static void input_task(void *arg) {
    (void)arg;
    input_event_t in;
    for (;;) {
        if (xQueueReceive(s_queue, &in, pdMS_TO_TICKS(IDLE_POLL_MS)) == pdTRUE) {
            process(&in);
        }
        apply_power(gb_idle_update(&s_idle, now_ms(), s_playing));
    }
}

// 运行在 button 组件的 esp_timer 任务里：只入队。
static void on_key(bsp_btn_t btn, bsp_btn_ev_t ev, void *user) {
    (void)user;
    if (!s_input_ready) return;
    const input_event_t in = { .btn = btn, .ev = ev };
    (void)xQueueSend(s_queue, &in, 0);
}

esp_err_t gb_app_start(bool audio_ok) {
    (void)gb_score_init();                 // 失败只是不保存最高分
    if (audio_ok) (void)gb_sfx_init();     // 失败只是静音

    gb_nav_init(&s_nav, gb_registry_count());
    gb_idle_init(&s_idle, now_ms(), DIM_AFTER_MS, OFF_AFTER_MS);

    if (!bsp_lvgl_lock(LOCK_TIMEOUT_MS)) return ESP_ERR_TIMEOUT;
    gb_launcher_show(s_nav.selected);
    bsp_lvgl_unlock();
    bsp_display_backlight(BACKLIGHT_ACTIVE);   // 首页画好后再点亮，避免看到花屏

    s_queue = xQueueCreate(INPUT_QUEUE_DEPTH, sizeof(input_event_t));
    if (!s_queue) return ESP_ERR_NO_MEM;
    if (xTaskCreate(input_task, "gb_input", INPUT_TASK_STACK, NULL, 5, NULL) != pdPASS) {
        vQueueDelete(s_queue);
        s_queue = NULL;
        return ESP_ERR_NO_MEM;
    }
    s_input_ready = true;
    esp_err_t err = bsp_button_init(on_key, NULL);
    if (err != ESP_OK) ESP_LOGE(TAG, "按键初始化失败: %s，无法操作", esp_err_to_name(err));
    return err;
}
