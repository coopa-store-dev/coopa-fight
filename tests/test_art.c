// tests/test_art.c —— 常量图:人物每张 16×16 的 RGB565A8、数据长度 16×16×3,朝左那张是朝右的镜像;
// 每个人十个动作各不相同;越界给库巴站立;特效、热血头像的头和数据长度对得上。
#include "fight_art.h"
#include "fight_moves.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

// RGB565A8:先 w*h 个 RGB565(每个 2 字节),再 w*h 个透明度
static void check_head(const lv_image_dsc_t *d) {
    assert(d && d->data);
    assert(d->header.magic == LV_IMAGE_HEADER_MAGIC && d->header.cf == LV_COLOR_FORMAT_RGB565A8);
    assert(d->header.w > 0 && d->header.h > 0 && d->header.stride == d->header.w * 2u);
    assert(d->data_size == (uint32_t)d->header.w * d->header.h * 3u);
    unsigned opaque = 0;
    const uint8_t *alpha = d->data + d->header.w * d->header.h * 2u;
    for (unsigned i = 0; i < (unsigned)d->header.w * d->header.h; i++) {
        assert(alpha[i] == 0 || alpha[i] == 0xFF);
        opaque += alpha[i] != 0;
    }
    assert(opaque > 0);
}

static void check_mirror(const lv_image_dsc_t *r, const lv_image_dsc_t *l) {
    const unsigned w = r->header.w, h = r->header.h;
    assert(l->header.w == w && l->header.h == h);
    for (unsigned y = 0; y < h; y++) {
        for (unsigned x = 0; x < w; x++) {
            const unsigned a = y * w + x, b = y * w + (w - 1 - x);
            assert(r->data[a * 2] == l->data[b * 2] && r->data[a * 2 + 1] == l->data[b * 2 + 1]);
            assert(r->data[w * h * 2 + a] == l->data[w * h * 2 + b]);
        }
    }
}

static void test_poses(void) {
    for (uint8_t c = 0; c < FC_COUNT; c++) {
        for (uint8_t p = 0; p < FA_POSES; p++) {
            const lv_image_dsc_t *r = fight_art_pose(c, p, false), *l = fight_art_pose(c, p, true);
            check_head(r);
            check_head(l);
            assert(r->header.w == 16 && r->header.h == 16 && r->data_size == 16 * 16 * 3);
            assert(r != l && r->data != l->data);
            check_mirror(r, l);
            for (uint8_t q = 0; q < p; q++) {  // 每个动作都画了(不是拿别的动作充数)
                assert(memcmp(r->data, fight_art_pose(c, q, false)->data, 16 * 16 * 3) != 0);
            }
        }
        // 人是侧身的:站着那张和它的镜像不一样
        assert(memcmp(fight_art_pose(c, FA_IDLE, false)->data, fight_art_pose(c, FA_IDLE, true)->data, 16 * 16 * 3));
    }
}

static void test_out_of_range(void) {
    assert(fight_art_pose(FC_COUNT, FA_JAB, false) == fight_art_pose(FC_KID, FA_IDLE, false));
    assert(fight_art_pose(FC_DAD, FA_POSES, true) == fight_art_pose(FC_KID, FA_IDLE, true));
    assert(fight_art_pose(255, 255, false) == fight_art_pose(FC_KID, FA_IDLE, false));
    assert(fight_art_fx(FX_COUNT) == NULL);
    assert(fight_art_hot(FC_COUNT) == fight_art_hot(FC_KID));
}

static void test_fx_and_hot(void) {
    for (uint8_t f = 0; f < FX_COUNT; f++) {
        const lv_image_dsc_t *d = fight_art_fx(f);
        check_head(d);
        assert(d->header.w <= 16 && d->header.h <= 16);
    }
    for (uint8_t c = 0; c < FC_COUNT; c++) {
        const lv_image_dsc_t *d = fight_art_hot(c);
        check_head(d);
        assert(d->header.w == 16 && d->header.h == 16);
        for (uint8_t o = 0; o < c; o++) assert(memcmp(d->data, fight_art_hot(o)->data, d->data_size));
    }
}

int main(void) {
    test_poses();
    test_out_of_range();
    test_fx_and_hot();
    puts("test_art: ok");
    return 0;
}
