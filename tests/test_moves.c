// tests/test_moves.c —— 出招表的数值约束(宿主测试)。
#include <stdio.h>
#include <stdlib.h>

#include "fight_moves.h"
#include "fight_msg.h"

#define CHECK(c) do { if (!(c)) { \
    printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); exit(1); } } while (0)

// 每个角色每个招都有出手和有效时间;例外是爷爷的太极(架势,startup = 0)和大招(从连招里直接出,表里只用伤害,时间全 0)
static void test_all_moves_have_timing(void) {
    for (int c = 0; c < FC_COUNT; c++) {
        const fight_char_def_t *d = fight_char(c);
        CHECK(d->hp > 0 && d->guard_ms > 0 && d->guard_long_ms > d->guard_ms);
        for (int m = 0; m < FM_COUNT; m++) {
            const fight_move_def_t *v = &d->mv[m];
            CHECK(v->hits >= 1);
            if (m == FM_SUPER) {
                CHECK(v->startup == 0 && v->active == 0 && v->recovery == 0 && v->hitstun == 0 && v->dmg > 0);
                continue;
            }
            if (!(c == FC_GRANDPA && m == FM_SPECIAL)) CHECK(v->startup > 0);
            CHECK(v->active > 0);
        }
    }
}

// 小孩一秒 3.5 下:间隔 286 ms × 2 = 572,按下 ● 起算要赶在僵直结束前
static void test_combo_fits_kid(void) {
    for (int c = 0; c < FC_COUNT; c++) {
        const fight_move_def_t *j = &fight_char(c)->mv[FM_JAB];
        CHECK(572 <= j->startup + j->hitstun);
    }
}

static void test_heavy_armor(void) {
    for (int c = 0; c < FC_COUNT; c++) {
        const fight_move_def_t *h = &fight_char(c)->mv[FM_HEAVY];
        CHECK(h->armor_from > 0 && h->armor_from < h->startup);
        // 抡的最后 300 ms 轻拳打不断(设计 §4.1);爸爸抡得快,计划里定 250
        CHECK(h->startup - h->armor_from == (c == FC_DAD ? 250 : 300));
    }
}

// 爸爸的重击抡得最快、最痛(设计 §6):四个人里出手最短、伤害最高
static void test_dad_heavy_fastest_hardest(void) {
    const fight_move_def_t *d = &fight_char(FC_DAD)->mv[FM_HEAVY];
    for (int c = 0; c < FC_COUNT; c++) {
        if (c == FC_DAD) continue;
        const fight_move_def_t *h = &fight_char(c)->mv[FM_HEAVY];
        CHECK(d->startup < h->startup && d->dmg > h->dmg);
    }
}

static void test_super_dmg_third(void) {
    for (int c = 0; c < FC_COUNT; c++) {
        const fight_char_def_t *d = fight_char(c);
        CHECK(d->mv[FM_SUPER].dmg * 100 >= d->hp * 30);
        CHECK(d->mv[FM_SUPER].dmg * 100 <= d->hp * 37);
    }
}

static void test_char_out_of_range(void) {
    CHECK(fight_char(9) == fight_char(FC_KID));
    CHECK(fight_char(255) == fight_char(FC_KID));
}

// 快照里血占 10 位:每个角色的满血都放得下
static void test_hp_fits_snapshot(void) {
    for (uint8_t c = 0; c < FC_COUNT; c++) CHECK(fight_char(c)->hp <= FIGHT_SNAP_HP_MAX);
}

int main(void) {
    test_all_moves_have_timing();
    test_combo_fits_kid();
    test_heavy_armor();
    test_dad_heavy_fastest_hardest();
    test_super_dmg_third();
    test_char_out_of_range();
    test_hp_fits_snapshot();
    printf("test_moves: PASS\n");
    return 0;
}
