// tests/stub/lvgl.h —— 宿主测试用的 lvgl.h 替身:只有 fight_art.c 用到的(LVGL 9.5 的 lv_image_dsc_t,小端布局)。
// 字段名、常量值和真的一样(src/draw/lv_image_dsc.h、src/misc/lv_color.h);真头文件的对不对由网页版 / 卡上版编译把关。
#pragma once

#include <stdint.h>

#define LV_IMAGE_HEADER_MAGIC (0x19)

typedef enum {
    LV_COLOR_FORMAT_RGB565A8 = 0x14,
} lv_color_format_t;

typedef struct {
    uint32_t magic: 8;
    uint32_t cf : 8;
    uint32_t flags: 16;

    uint32_t w: 16;
    uint32_t h: 16;
    uint32_t stride: 16;
    uint32_t reserved_2: 16;
} lv_image_header_t;

typedef struct {
    lv_image_header_t header;
    uint32_t data_size;
    const uint8_t *data;
    const void *reserved;
    const void *reserved_2;
} lv_image_dsc_t;
