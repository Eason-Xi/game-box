// tools/gamebox_preview/stubs/freertos/queue.h —— 主机渲染预览用的最小桩：单线程环形队列。
#pragma once
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
typedef struct { size_t item, cap, head, count; unsigned char *buf; } host_queue_t;
typedef host_queue_t *QueueHandle_t;
static inline QueueHandle_t xQueueCreate(size_t cap, size_t item) {
    host_queue_t *q = calloc(1, sizeof(*q)); q->item = item; q->cap = cap; q->buf = calloc(cap, item); return q; }
static inline BaseType_t xQueueSend(QueueHandle_t q, const void *v, int t) { (void)t;
    if (q->count == q->cap) return pdFALSE;
    memcpy(q->buf + ((q->head + q->count) % q->cap) * q->item, v, q->item); q->count++; return pdTRUE; }
static inline BaseType_t xQueueReceive(QueueHandle_t q, void *v, int t) { (void)t;
    if (!q->count) return pdFALSE;
    memcpy(v, q->buf + q->head * q->item, q->item); q->head = (q->head + 1) % q->cap; q->count--; return pdTRUE; }
static inline BaseType_t xQueueReset(QueueHandle_t q) { q->head = q->count = 0; return pdPASS; }
