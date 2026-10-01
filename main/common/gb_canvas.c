// main/common/gb_canvas.c —— 见 gb_canvas.h。
#include "gb_canvas.h"

#include "esp_heap_caps.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "gb_canvas";

// draw_buf 结构体与像素数据一次申请，raw 记下原始指针用于释放。
typedef struct {
    lv_draw_buf_t buf;   // 必须是第一个成员：对外返回的就是它的地址
    void *raw;
} canvas_mem_t;

lv_draw_buf_t *gb_canvas_create(uint32_t w, uint32_t h, lv_color_format_t cf) {
    const uint32_t stride = lv_draw_buf_width_to_stride(w, cf);
    const uint32_t palette = LV_COLOR_INDEXED_PALETTE_SIZE(cf) * sizeof(lv_color32_t);
    const uint32_t data_size = stride * h + palette;

    canvas_mem_t *mem = heap_caps_calloc(1, sizeof(canvas_mem_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!mem) return NULL;
    mem->raw = heap_caps_calloc(1, data_size + LV_DRAW_BUF_ALIGN, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!mem->raw) {
        ESP_LOGW(TAG, "画布 %lux%lu 申请 %lu 字节失败，最大连续块 %u",
                 (unsigned long)w, (unsigned long)h, (unsigned long)data_size,
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
        heap_caps_free(mem);
        return NULL;
    }
    void *data = lv_draw_buf_align(mem->raw, cf);
    if (lv_draw_buf_init(&mem->buf, w, h, cf, stride, data, data_size) != LV_RESULT_OK) {
        heap_caps_free(mem->raw);
        heap_caps_free(mem);
        return NULL;
    }
    // lv_draw_buf_init() 会清掉标志位；游戏要直接改像素，补上「可修改」。
    mem->buf.header.flags |= LV_IMAGE_FLAGS_MODIFIABLE;
    return &mem->buf;
}

void gb_canvas_destroy(lv_draw_buf_t *buf) {
    if (!buf) return;
    canvas_mem_t *mem = (canvas_mem_t *)buf;
    heap_caps_free(mem->raw);
    heap_caps_free(mem);
}

gb_i4_t gb_canvas_i4(lv_draw_buf_t *buf) {
    gb_i4_t img = { 0 };
    if (!buf || buf->header.cf != LV_COLOR_FORMAT_I4) return img;
    img.base = lv_draw_buf_goto_xy(buf, 0, 0);   // 已跳过开头的调色板
    img.stride = buf->header.stride;
    img.w = (int)buf->header.w;
    img.h = (int)buf->header.h;
    return img;
}
