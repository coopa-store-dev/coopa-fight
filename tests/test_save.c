// tests/test_save.c —— 存档(纯数据部分;kit_sv_* 读写在 fight_app.c):归零、校验、读回来规整、一场只记一次、
// 闯关只记一次、里程碑。
#include "fight_save.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_reset_and_valid(void) {
    fight_save_t s;
    memset(&s, 0xAA, sizeof(s));
    fight_save_reset(&s);
    fight_save_t z;
    memset(&z, 0, sizeof(z));
    z.ver = 1;
    assert(memcmp(&s, &z, sizeof(s)) == 0 && fight_save_milestones(&s) == 0);
    assert(fight_save_valid(&s));
    s.ver = 9;  // 版本不对:kit_sv_blob_load 不收
    assert(!fight_save_valid(&s));
}

static void test_clean(void) {
    fight_save_t s;
    fight_save_reset(&s);
    s.last_char = 2;
    s.crowns = 5;
    s.seen_tutorial = 1;
    s.wins = 3;
    s.matches = 7;
    s.supers = 2;
    fight_save_t t = s;
    fight_save_clean(&t);  // 好的值不动
    assert(memcmp(&s, &t, sizeof(s)) == 0);

    s.last_char = 4;  // 坏的值:人归 0,冠只留低 4 位,教学只能是 0 / 1,赢的不比打的多
    s.crowns = 0xF3;
    s.seen_tutorial = 7;
    s.wins = 9;
    fight_save_clean(&s);
    assert(s.last_char == 0 && s.crowns == 3 && s.seen_tutorial == 1 && s.wins == 7 && s.matches == 7);
}

static void test_over_once(void) {
    fight_save_t s;
    fight_save_reset(&s);
    assert(!fight_save_over(&s, 0, true));  // 没有场号不记
    assert(fight_save_over(&s, 0x11, true) && s.wins == 1 && s.matches == 1);
    assert(!fight_save_over(&s, 0x11, true) && s.wins == 1 && s.matches == 1);  // 同一场(连回来又到结束页)
    assert(fight_save_over(&s, 0x13, false) && s.wins == 1 && s.matches == 2);
}

static void test_crown_once(void) {
    fight_save_t s;
    fight_save_reset(&s);
    assert(!fight_save_crown(&s, 0) && !fight_save_crown(&s, 3));
    assert(fight_save_set_crown(&s, 3) && !fight_save_set_crown(&s, 3));
    assert(fight_save_crown(&s, 3) && !fight_save_crown(&s, 2) && !fight_save_crown(&s, 7));
    assert(!fight_save_set_crown(&s, 4) && !fight_save_set_crown(&s, 255));  // 越界不记
    assert(s.crowns == 8);
}

static void test_milestones(void) {
    fight_save_t s;
    fight_save_reset(&s);
    assert(fight_save_milestones(&s) == 0);
    s.supers = 1;
    assert(fight_save_milestones(&s) == 1);
    s.wins = 1;
    assert(fight_save_milestones(&s) == 3);
    s.wins = 9;
    assert(fight_save_milestones(&s) == 3);
    s.wins = 10;
    assert(fight_save_milestones(&s) == 7);
    s.supers = 0;
    assert(fight_save_milestones(&s) == 6);
}

int main(void) {
    test_reset_and_valid();
    test_clean();
    test_over_once();
    test_crown_once();
    test_milestones();
    puts("test_save: ok");
    return 0;
}
