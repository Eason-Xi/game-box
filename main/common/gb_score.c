// main/common/gb_score.c —— 最高分与自定义存档，见 gb_score.h。
//
// 待写入的数据先放进 s_pending（读取时优先查这里），再请音效任务在空闲时
// 调用 flush_pending() 一次性写入并 commit。音频不可用时直接同步写入。
#include "gb_score.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "gb_score_key.h"
#include "gb_sfx.h"
#include "nvs.h"
#include "nvs_flash.h"
#include <string.h>

static const char *TAG = "gb_score";
#define SCORE_NAMESPACE "gamebox"
#define PENDING_SLOTS   8
#define LOCK_TIMEOUT_MS 200

typedef struct {
    bool used;
    char key[GB_SCORE_KEY_LEN];
    uint8_t len;                  // 0 = u32 最高分；否则为存档字节数
    uint32_t value;
    uint8_t blob[GB_SAVE_MAX];
} pending_t;

static nvs_handle_t s_handle;
static SemaphoreHandle_t s_lock;
static bool s_ready;
static pending_t s_pending[PENDING_SLOTS];

esp_err_t gb_score_init(void) {
    if (s_ready) return ESP_OK;
    if (!s_lock) {
        s_lock = xSemaphoreCreateMutex();
        if (!s_lock) return ESP_ERR_NO_MEM;
    }
    esp_err_t err = nvs_flash_init();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "NVS 不可用 (%s)，纪录不会保存", esp_err_to_name(err));
        return err;
    }
    // 只打开一次并常驻：NVS 句柄数量有限，游戏反复进出不能每次都开。
    err = nvs_open(SCORE_NAMESPACE, NVS_READWRITE, &s_handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "nvs_open 失败: %s", esp_err_to_name(err));
        return err;
    }
    s_ready = true;
    return ESP_OK;
}

static bool lock(void) {
    return s_ready && xSemaphoreTake(s_lock, pdMS_TO_TICKS(LOCK_TIMEOUT_MS)) == pdTRUE;
}

static void unlock(void) {
    xSemaphoreGive(s_lock);
}

static pending_t *find_pending(const char *key) {
    for (int i = 0; i < PENDING_SLOTS; i++) {
        if (s_pending[i].used && strcmp(s_pending[i].key, key) == 0) return &s_pending[i];
    }
    return NULL;
}

// 持锁调用：把待写数据写进 NVS 并 commit。
static void flush_locked(void) {
    bool wrote = false;
    for (int i = 0; i < PENDING_SLOTS; i++) {
        pending_t *p = &s_pending[i];
        if (!p->used) continue;
        const esp_err_t err = p->len ? nvs_set_blob(s_handle, p->key, p->blob, p->len)
                                     : nvs_set_u32(s_handle, p->key, p->value);
        if (err != ESP_OK) ESP_LOGW(TAG, "写入 %s 失败: %s", p->key, esp_err_to_name(err));
        p->used = false;
        wrote = true;
    }
    if (!wrote) return;
    const esp_err_t err = nvs_commit(s_handle);
    if (err != ESP_OK) ESP_LOGW(TAG, "nvs_commit 失败: %s", esp_err_to_name(err));
}

static void flush_pending(void) {
    if (!lock()) return;
    flush_locked();
    unlock();
}

// 持锁调用：取一个空槽（同键已有则复用）；槽满时先同步写掉再取。
static pending_t *claim_slot(const char *key) {
    pending_t *p = find_pending(key);
    if (p) return p;
    for (int pass = 0; pass < 2; pass++) {
        for (int i = 0; i < PENDING_SLOTS; i++) {
            if (!s_pending[i].used) {
                memset(&s_pending[i], 0, sizeof(s_pending[i]));
                strcpy(s_pending[i].key, key);
                return &s_pending[i];
            }
        }
        flush_locked();
    }
    return NULL;
}

// 解锁后调用：请音效任务在空闲时写入；音频不可用时立即写。
static void schedule_flush(void) {
    if (!gb_sfx_run_when_idle(flush_pending)) flush_pending();
}

static uint32_t best_locked(const char *key) {
    const pending_t *p = find_pending(key);
    if (p && p->len == 0) return p->value;
    uint32_t value = 0;
    const esp_err_t err = nvs_get_u32(s_handle, key, &value);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "读取 %s 失败: %s", key, esp_err_to_name(err));
    }
    return err == ESP_OK ? value : 0;
}

uint32_t gb_score_best(const char *game_id) {
    char key[GB_SCORE_KEY_LEN];
    if (!gb_score_make_key(game_id, key, sizeof(key)) || !lock()) return 0;
    const uint32_t best = best_locked(key);
    unlock();
    return best;
}

bool gb_score_submit(const char *game_id, uint32_t score) {
    char key[GB_SCORE_KEY_LEN];
    if (!gb_score_make_key(game_id, key, sizeof(key)) || !lock()) return false;
    bool record = false;
    if (score > best_locked(key)) {
        pending_t *p = claim_slot(key);
        if (p) {
            p->used = true;
            p->len = 0;
            p->value = score;
            record = true;
            ESP_LOGI(TAG, "%s 新纪录 %lu", game_id, (unsigned long)score);
        }
    }
    unlock();
    if (record) schedule_flush();
    return record;
}

// 持锁调用：读出当前存档（优先待写数据）。
static bool load_locked(const char *key, void *buf, size_t len) {
    const pending_t *p = find_pending(key);
    if (p && p->len) {
        if (p->len != len) return false;
        memcpy(buf, p->blob, len);
        return true;
    }
    size_t stored = len;
    const esp_err_t err = nvs_get_blob(s_handle, key, buf, &stored);
    if (err == ESP_ERR_NVS_INVALID_LENGTH) return false;
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "读取 %s 失败: %s", key, esp_err_to_name(err));
    }
    return err == ESP_OK && stored == len;
}

bool gb_save_load(const char *game_id, void *buf, size_t len) {
    char key[GB_SCORE_KEY_LEN];
    if (!buf || len == 0 || len > GB_SAVE_MAX) return false;
    if (!gb_store_make_key('s', game_id, key, sizeof(key)) || !lock()) return false;
    // 先读进临时区：长度不符时 nvs_get_blob 可能已部分写入，不能弄脏调用方的缓冲。
    uint8_t tmp[GB_SAVE_MAX];
    const bool ok = load_locked(key, tmp, len);
    unlock();
    if (ok) memcpy(buf, tmp, len);
    return ok;
}

bool gb_save_store(const char *game_id, const void *buf, size_t len) {
    char key[GB_SCORE_KEY_LEN];
    if (!buf || len == 0 || len > GB_SAVE_MAX) return false;
    if (!gb_store_make_key('s', game_id, key, sizeof(key)) || !lock()) return false;
    uint8_t current[GB_SAVE_MAX];
    if (load_locked(key, current, len) && memcmp(current, buf, len) == 0) {
        unlock();
        return true;   // 内容没变，不写 flash
    }
    pending_t *p = claim_slot(key);
    if (p) {
        p->used = true;
        p->len = (uint8_t)len;
        memcpy(p->blob, buf, len);
    }
    unlock();
    if (p) schedule_flush();
    return p != NULL;
}
