// tests/test_save.c —— 存档:读不到 / 坏的归零、闯关只记一次、里程碑、读回来规整。
#include "fight_save.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "kit_store.h"

// 假的 saves 分区:一个键。blob_len 与 size 不等当读不到(和真的一样)。
static unsigned char g_blob[64];
static size_t g_len;
static bool g_has;

bool kit_sv_blob_load(const char *ns, const char *key, void *out, size_t size, kit_valid_fn valid, void *scratch) {
    if (strcmp(ns, "fight") || strcmp(key, "save") || !g_has || g_len != size) return false;
    memcpy(scratch, g_blob, size);
    if (!valid(scratch)) return false;
    if (scratch != out) memcpy(out, scratch, size);
    return true;
}

bool kit_sv_blob_store(const char *ns, const char *key, const void *blob, size_t size) {
    if (strcmp(ns, "fight") || strcmp(key, "save") || size > sizeof(g_blob)) return false;
    memcpy(g_blob, blob, size);
    g_len = size;
    g_has = true;
    return true;
}

bool kit_sv_ns_erase(const char *ns) {
    (void)ns;
    g_has = false;
    return true;
}

static void test_old_or_bad_save_zeroed(void) {
    fight_save_t s;
    memset(&s, 0xAA, sizeof(s));
    kit_sv_ns_erase("fight");
    fight_save_load(&s);  // 读不到
    fight_save_t z;
    memset(&z, 0, sizeof(z));
    z.ver = 1;
    assert(memcmp(&s, &z, sizeof(s)) == 0 && fight_save_milestones(&s) == 0);

    memset(&s, 0, sizeof(s));
    s.ver = 9;  // 版本不对
    s.wins = 5;
    fight_save_store(&s);
    memset(&s, 0xAA, sizeof(s));
    fight_save_load(&s);
    assert(memcmp(&s, &z, sizeof(s)) == 0);

    g_len = 3;  // 长度不对
    fight_save_load(&s);
    assert(memcmp(&s, &z, sizeof(s)) == 0);
}

static void test_store_load_roundtrip_and_clean(void) {
    fight_save_t s;
    memset(&s, 0, sizeof(s));
    s.ver = 1;
    s.last_char = 2;
    s.crowns = 5;
    s.seen_tutorial = 1;
    s.wins = 3;
    s.matches = 7;
    s.supers = 2;
    fight_save_store(&s);
    fight_save_t t;
    fight_save_load(&t);
    assert(memcmp(&s, &t, sizeof(s)) == 0);

    s.last_char = 4;  // 坏的值:人归 0,冠只留低 4 位,教学只能是 0 / 1
    s.crowns = 0xF3;
    s.seen_tutorial = 7;
    fight_save_store(&s);
    fight_save_load(&t);
    assert(t.last_char == 0 && t.crowns == 3 && t.seen_tutorial == 1 && t.wins == 3);
}

static void test_crown_once(void) {
    fight_save_t s;
    memset(&s, 0, sizeof(s));
    s.ver = 1;
    assert(!fight_save_crown(&s, 0) && !fight_save_crown(&s, 3));
    assert(fight_save_set_crown(&s, 3) && !fight_save_set_crown(&s, 3));
    assert(fight_save_crown(&s, 3) && !fight_save_crown(&s, 2) && !fight_save_crown(&s, 7));
    assert(!fight_save_set_crown(&s, 4) && !fight_save_set_crown(&s, 255));  // 越界不记
    assert(s.crowns == 8);
}

static void test_milestones(void) {
    fight_save_t s;
    memset(&s, 0, sizeof(s));
    s.ver = 1;
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
    test_old_or_bad_save_zeroed();
    test_store_load_roundtrip_and_clean();
    test_crown_once();
    test_milestones();
    puts("test_save: ok");
    return 0;
}
