// main/common/gb_sfx.c —— 音效播放任务，见 gb_sfx.h。
//
// 取消用「代号」实现：每次 gb_sfx_cancel() 把代号加一，队列里带旧代号的请求
// 直接跳过，正在播放的音效也会在下一个分片（10ms）检查到代号变化后收尾。
// 方波音效表也走合成器接口（TUNE_SYNTH），播放循环只有一份。
#include "gb_sfx.h"

#include "bsp_audio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

static const char *TAG = "gb_sfx";

#define SFX_CHUNK_SAMPLES 160      // 10ms @16kHz：分片小，切换与取消响应快
#define SFX_AMPLITUDE     6500     // 约 -14dBFS，给 codec 留余量避免削顶
#define SFX_VOLUME        70
#define SFX_QUEUE_DEPTH   8
#define SFX_TASK_STACK    4096
// 比 LVGL 任务（4）高：PCM 写入会阻塞让出 CPU，但到点必须能及时喂数据。
#define SFX_TASK_PRIORITY 5
#define SFX_FADE_SAMPLES  (GB_SFX_RATE * 3 / 1000)
// I2S DMA 约 90ms 的缓冲。写 flash 前等这段余音放完，避免关闭缓存时打断 DMA 而出杂音。
#define SFX_DRAIN_MS      120
#define SFX_IDLE_SLOTS    4

static const gb_tone_config_t TONE_CONFIG = {
    .sample_rate = GB_SFX_RATE,
    .amplitude = SFX_AMPLITUDE,
    .fade_samples = SFX_FADE_SAMPLES,
};

typedef struct {
    const gb_synth_t *synth;   // NULL = 仅用于唤醒任务处理空闲回调
    uintptr_t sound;
    uint32_t generation;
    bool feedback;
} sfx_request_t;

static QueueHandle_t s_queue;
static volatile uint32_t s_generation;
static bool s_ready;
static int16_t s_pcm[SFX_CHUNK_SAMPLES];
static union {
    uint8_t bytes[GB_SYNTH_PLAYER_MAX];
    uint64_t align;
} s_player;

static portMUX_TYPE s_idle_lock = portMUX_INITIALIZER_UNLOCKED;
static void (*s_idle_fns[SFX_IDLE_SLOTS])(void);

// ---------------------------------------------------------------- 方波音效表

static void tune_start(void *player, uintptr_t sound) {
    gb_tune_player_start(player, (const gb_tune_t *)sound);
}

static size_t tune_render(void *player, int16_t *out, size_t n) {
    return gb_tune_player_render(player, &TONE_CONFIG, out, n);
}

static const gb_synth_t TUNE_SYNTH = { .start = tune_start, .render = tune_render };
_Static_assert(sizeof(gb_tune_player_t) <= GB_SYNTH_PLAYER_MAX, "tune player too large");

static const gb_note_t MOVE_NOTES[]    = { { 880, 22 } };
static const gb_note_t CONFIRM_NOTES[] = { { 659, 50 }, { 988, 80 } };
static const gb_note_t BACK_NOTES[]    = { { 784, 50 }, { 523, 80 } };
static const gb_note_t ASK_NOTES[]     = { { 587, 60 } };
const gb_tune_t GB_TUNE_MOVE    = GB_TUNE(MOVE_NOTES);
const gb_tune_t GB_TUNE_CONFIRM = GB_TUNE(CONFIRM_NOTES);
const gb_tune_t GB_TUNE_BACK    = GB_TUNE(BACK_NOTES);
const gb_tune_t GB_TUNE_ASK     = GB_TUNE(ASK_NOTES);

// ---------------------------------------------------------------- 播放

static void play(sfx_request_t req) {
    req.synth->start(s_player.bytes, req.sound);
    for (;;) {
        if (req.generation != s_generation) return;
        const size_t n = req.synth->render(s_player.bytes, s_pcm, SFX_CHUNK_SAMPLES);
        if (n == 0) return;
        if (bsp_audio_write(s_pcm, n * sizeof(int16_t)) != ESP_OK) {
            ESP_LOGW(TAG, "音频写入失败，放弃本段音效");
            return;
        }
        sfx_request_t next;
        if (xQueuePeek(s_queue, &next, 0) != pdTRUE) continue;
        if (!next.synth) {
            // 唤醒标记：吃掉它，免得挡住后面的音效；空闲回调仍由 s_idle_fns 记着。
            xQueueReceive(s_queue, &next, 0);
            continue;
        }
        if (req.feedback) {                  // 反馈音可被下一个音效立即打断
            xQueueReceive(s_queue, &next, 0);
            if (next.generation != s_generation) continue;
            req = next;
            req.synth->start(s_player.bytes, req.sound);
        } else if (next.feedback) {          // 普通音效播放中，到来的反馈音直接丢弃
            xQueueReceive(s_queue, &next, 0);
        }
    }
}

static bool idle_pending(void) {
    bool any = false;
    portENTER_CRITICAL(&s_idle_lock);
    for (int i = 0; i < SFX_IDLE_SLOTS; i++) any = any || s_idle_fns[i] != NULL;
    portEXIT_CRITICAL(&s_idle_lock);
    return any;
}

static void run_idle_fns(void) {
    for (int i = 0; i < SFX_IDLE_SLOTS; i++) {
        void (*fn)(void);
        portENTER_CRITICAL(&s_idle_lock);
        fn = s_idle_fns[i];
        s_idle_fns[i] = NULL;
        portEXIT_CRITICAL(&s_idle_lock);
        if (fn) fn();
    }
}

static void sfx_task(void *arg) {
    (void)arg;
    TickType_t last_play_end = xTaskGetTickCount() - pdMS_TO_TICKS(SFX_DRAIN_MS);
    for (;;) {
        // 有待执行的空闲回调时，只等到余音放完为止；否则一直等新请求。
        TickType_t wait = portMAX_DELAY;
        if (idle_pending()) {
            const TickType_t since = xTaskGetTickCount() - last_play_end;
            wait = since >= pdMS_TO_TICKS(SFX_DRAIN_MS) ? 0 : pdMS_TO_TICKS(SFX_DRAIN_MS) - since;
        }
        sfx_request_t req;
        if (xQueueReceive(s_queue, &req, wait) == pdTRUE) {
            if (req.synth && req.generation == s_generation) {
                play(req);
                last_play_end = xTaskGetTickCount();
            }
            continue;   // 先把排队的音效放完，再考虑空闲回调
        }
        run_idle_fns();
    }
}

esp_err_t gb_sfx_init(void) {
    if (s_ready) return ESP_OK;
    esp_err_t err = bsp_audio_set_format(GB_SFX_RATE, 16, 1);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "音频格式设置失败: %s，静音运行", esp_err_to_name(err));
        return err;
    }
    bsp_audio_set_volume(SFX_VOLUME);

    s_queue = xQueueCreate(SFX_QUEUE_DEPTH, sizeof(sfx_request_t));
    if (!s_queue) return ESP_ERR_NO_MEM;
    if (xTaskCreate(sfx_task, "gb_sfx", SFX_TASK_STACK, NULL, SFX_TASK_PRIORITY, NULL) != pdPASS) {
        vQueueDelete(s_queue);
        s_queue = NULL;
        return ESP_ERR_NO_MEM;
    }
    s_ready = true;
    return ESP_OK;
}

static void enqueue(const gb_synth_t *synth, uintptr_t sound, bool feedback) {
    if (!s_ready) return;
    const sfx_request_t req = {
        .synth = synth, .sound = sound, .generation = s_generation, .feedback = feedback,
    };
    (void)xQueueSend(s_queue, &req, 0);   // 满了就丢：音效不值得阻塞游戏
}

void gb_sfx_play(const gb_tune_t *tune) {
    if (tune && tune->count) enqueue(&TUNE_SYNTH, (uintptr_t)tune, false);
}

void gb_sfx_play_feedback(const gb_tune_t *tune) {
    if (tune && tune->count) enqueue(&TUNE_SYNTH, (uintptr_t)tune, true);
}

void gb_sfx_play_synth(const gb_synth_t *synth, uintptr_t sound) {
    if (!synth || !synth->start || !synth->render) return;
    enqueue(synth, sound, synth->is_feedback && synth->is_feedback(sound));
}

void gb_sfx_cancel(void) {
    if (!s_ready) return;
    s_generation++;
    xQueueReset(s_queue);
}

bool gb_sfx_run_when_idle(void (*fn)(void)) {
    if (!s_ready || !fn) return false;
    bool queued = false;
    portENTER_CRITICAL(&s_idle_lock);
    for (int i = 0; i < SFX_IDLE_SLOTS && !queued; i++) queued = s_idle_fns[i] == fn;
    for (int i = 0; i < SFX_IDLE_SLOTS && !queued; i++) {
        if (!s_idle_fns[i]) {
            s_idle_fns[i] = fn;
            queued = true;
        }
    }
    portEXIT_CRITICAL(&s_idle_lock);
    if (!queued) return false;   // 槽位满（不应发生）：让调用方立即执行
    const sfx_request_t wake = { .synth = NULL };
    (void)xQueueSend(s_queue, &wake, 0);   // 队列满也没关系：任务放完音效后会检查
    return true;
}
