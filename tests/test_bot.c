// tests/test_bot.c —— 电脑和真人模型:按真人的节奏按(两下之间 ≥ gap_ms,看到变化先反应),只看 fight_seen_t;
// 大人会统计对方的习惯并克制;有气时 ● ▼ ● 按得进窗口;晕了狂按。
#include <stdio.h>
#include <stdlib.h>

#include "fight_bot.h"

#define CHECK(c) do { if (!(c)) { \
    printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); exit(1); } } while (0)

static uint32_t s_x = 1;
static uint32_t rnd(void) {
    s_x = s_x * 1664525u + 1013904223u;
    return s_x >> 8;
}

static const fight_bot_cfg_t *ALL[5] = { &FIGHT_BOT_EASY, &FIGHT_BOT_NORMAL, &FIGHT_BOT_HARD, &FIGHT_HUMAN_ADULT, &FIGHT_HUMAN_KID };

static void test_cfg_sums(void) {
    for (int i = 0; i < 5; i++) {
        const fight_bot_cfg_t *c = ALL[i];
        CHECK(c->p_jab + c->p_heavy + c->p_guard + c->p_special == 100);
        CHECK(c->react_ms > c->react_jit && c->gap_ms >= 1000 / 5);  // 谁都不超过一秒 5 下
    }
    // 真人模型(设计 §10、计划 Task 5)
    const fight_bot_cfg_t *a = &FIGHT_HUMAN_ADULT, *k = &FIGHT_HUMAN_KID;
    CHECK(a->react_ms == 650 && a->react_jit == 150 && a->gap_ms == 200 && a->reads_habits && a->mash);
    CHECK(a->p_jab == 40 && a->p_heavy == 25 && a->p_guard == 25 && a->p_special == 10);
    CHECK(a->p_combo == 85 && a->p_super == 90 && a->p_wrong == 3 && a->p_pause == 0);
    CHECK(k->react_ms == 900 && k->react_jit == 250 && k->gap_ms == 286 && !k->reads_habits && k->mash);
    CHECK(k->p_jab == 60 && k->p_heavy == 15 && k->p_guard == 20 && k->p_special == 5);
    CHECK(k->p_combo == 60 && k->p_super == 60 && k->p_wrong == 12 && k->p_pause == 15);
    CHECK(FIGHT_BOT_HARD.react_ms == 500);
}

// 两个模型对打 60 秒(一回合打完就再开一回合):每个人两下按下之间都 ≥ gap_ms(长按不是新的按下)
static void test_cadence(void) {
    for (int i = 0; i < 5; i++) {
        for (int j = 0; j < 5; j++) {
            fight_sim_t s;
            fight_sim_init(&s, (uint8_t)(i % FC_COUNT), (uint8_t)(j % FC_COUNT), 2000, 2000);
            fight_bot_t b[2];
            fight_bot_init(&b[0], ALL[i], rnd);
            fight_bot_init(&b[1], ALL[j], rnd);
            uint32_t last[2] = { 0, 0 }, presses[2] = { 0, 0 }, wall = 0;   // wall:特写时 clock 不走,这里按真的时间比
            bool any[2] = { false, false };
            for (; wall < 60000; wall += FIGHT_STEP) {
                if (s.winner != FIGHT_UNDECIDED) fight_sim_init(&s, (uint8_t)(i % FC_COUNT), (uint8_t)(j % FC_COUNT), 0, 0);
                for (uint8_t w = 0; w < 2; w++) {
                    fight_seen_t v;
                    fight_seen(&s, w, &v);
                    int k = fight_bot_step(&b[w], &v, FIGHT_STEP);
                    if (k < 0) continue;
                    CHECK(k <= FK_DOWN_LONG);
                    fight_sim_key(&s, w, (uint8_t)k);
                    if (k == FK_DOWN_LONG) continue;
                    if (any[w]) CHECK(wall - last[w] >= b[w].cfg->gap_ms);
                    last[w] = wall;
                    any[w] = true;
                    presses[w]++;
                }
                fight_sim_step(&s);
            }
            CHECK(presses[0] > 60 && presses[1] > 60);   // 真的在打
        }
    }
}

// 自己挨了大招:特写一开始看见,反应过来才开始狂按(特写里按的不算);晕着一下一下按,间隔 ≥ gap_ms
static void test_mash_after_react(void) {
    for (int i = 0; i < 5; i++) {
        const fight_bot_cfg_t *c = ALL[i];
        for (int trial = 0; trial < 30; trial++) {
            fight_bot_t b;
            fight_bot_init(&b, c, rnd);
            fight_seen_t v = { .st = { FS_SUPER_IN, FS_SUPER_OUT }, .hp = { 600, 900 } };
            int first = -1, n = 0;
            uint32_t prev = 0;
            for (uint32_t t = 0; t < 4000; t += FIGHT_STEP) {
                if (t == 1500) v.st[0] = FS_DIZZY, v.st[1] = FS_IDLE;
                int k = fight_bot_step(&b, &v, FIGHT_STEP);
                if (k < 0) continue;
                CHECK(k != FK_DOWN_LONG);
                if (first < 0) first = (int)t;
                else CHECK(t - prev >= c->gap_ms);
                prev = t;
                n++;
            }
            CHECK(first >= c->react_ms - c->react_jit - FIGHT_STEP);
            CHECK(n >= (int)(2500 / (c->gap_ms + 200)));   // 一直在按
        }
    }
}

// bot 打一个只会每 800 ms 按一下 opp_key(-1 = 不按)的对手,数 bot 自己在规则里起了几次手(按状态的变化数)
typedef struct { int jab, heavy, guard, special, total; } openers_t;
static openers_t face(const fight_bot_cfg_t *c, int opp_key) {
    fight_bot_t b;
    fight_bot_init(&b, c, rnd);
    openers_t o = { 0 };
    fight_sim_t s;
    fight_sim_init(&s, FC_DAD, FC_DAD, 0, 0);
    uint8_t pst = FS_IDLE, pmv = 0;
    uint16_t pt = 0;
    for (uint32_t wall = 0; o.total < 400; wall += FIGHT_STEP) {
        if (s.winner != FIGHT_UNDECIDED) fight_sim_init(&s, FC_DAD, FC_DAD, 0, 0);
        fight_seen_t v;
        fight_seen(&s, 0, &v);
        int k = fight_bot_step(&b, &v, FIGHT_STEP);
        if (k >= 0) fight_sim_key(&s, 0, (uint8_t)k);
        if (opp_key >= 0 && wall % 800 == 0) fight_sim_key(&s, 1, (uint8_t)opp_key);
        fight_sim_step(&s);
        const fight_fighter_t *f = &s.f[0];
        bool fresh = f->st != pst || (f->st == FS_MOVE && (f->move != pmv || f->t < pt));
        if (fresh && f->st == FS_GUARD) o.guard++, o.total++;
        if (fresh && f->st == FS_STANCE) o.special++, o.total++;
        if (fresh && f->st == FS_MOVE && f->move == FM_JAB) o.jab++, o.total++;
        if (fresh && f->st == FS_MOVE && f->move == FM_HEAVY) o.heavy++, o.total++;
        if (fresh && f->st == FS_MOVE && f->move == FM_SPECIAL && !f->chain) o.special++, o.total++;
        pst = f->st, pmv = f->move, pt = f->t;
    }
    return o;
}

static int pct(int n, const openers_t *o) { return 100 * n / o->total; }

// 大人统计对方的习惯:对方老出拳 → 多防;老防 → 多抡重击;老抡 → 早出拳。小孩不读。
static void test_reads_habits(void) {
    openers_t base = face(&FIGHT_HUMAN_ADULT, -1);
    openers_t vs_jab = face(&FIGHT_HUMAN_ADULT, FK_OK);
    openers_t vs_guard = face(&FIGHT_HUMAN_ADULT, FK_DOWN);
    openers_t vs_heavy = face(&FIGHT_HUMAN_ADULT, FK_UP);
    CHECK(pct(vs_jab.guard, &vs_jab) > pct(base.guard, &base) + 10);
    CHECK(pct(vs_guard.heavy, &vs_guard) > pct(base.heavy, &base) + 10);
    CHECK(pct(vs_heavy.jab, &vs_heavy) > pct(base.jab, &base) + 10);
    openers_t kid_base = face(&FIGHT_HUMAN_KID, -1), kid_jab = face(&FIGHT_HUMAN_KID, FK_OK);
    CHECK(abs(pct(kid_jab.guard, &kid_jab) - pct(kid_base.guard, &kid_base)) < 8);
}

// 有气、● 起手接大招:● ▼ ● 三下的间隔都 ≥ gap_ms,按完赶得上连招窗口(打到 + 僵 600)
static void test_super_input(void) {
    const fight_bot_cfg_t *c = &FIGHT_HUMAN_ADULT;
    int seen = 0;
    for (int trial = 0; trial < 300 && seen < 30; trial++) {
        fight_bot_t b;
        fight_bot_init(&b, c, rnd);
        fight_seen_t v = { .st = { FS_IDLE, FS_IDLE }, .hp = { 900, 900 }, .gauge = { 1500, 0 } };
        int keys[3], n = 0;
        uint32_t at[3];
        for (uint32_t t = 0; t < 3000 && n < 3; t += FIGHT_STEP) {
            int k = fight_bot_step(&b, &v, FIGHT_STEP);
            if (k < 0) continue;
            if (n == 0 && k != FK_OK) break;   // 这次起手不是 ●
            v.st[0] = FS_MOVE;                // 出招中(后面的键提前按)
            keys[n] = k, at[n++] = t;
        }
        if (n < 3 || keys[1] != FK_DOWN || keys[2] != FK_OK) continue;
        seen++;
        CHECK(at[1] - at[0] >= c->gap_ms && at[2] - at[1] >= c->gap_ms);
        CHECK(at[2] - at[1] <= FIGHT_PREFIX_MS);
        CHECK(at[2] - at[0] < 150 + 600);   // 最快的轻拳 150 打到,僵 600
    }
    CHECK(seen >= 20);
}

int main(void) {
    test_cfg_sums();
    test_cadence();
    test_mash_after_react();
    test_reads_habits();
    test_super_input();
    printf("test_bot: PASS\n");
    return 0;
}
