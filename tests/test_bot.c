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

// 两个模型对打 60 秒(一回合打完就再开一回合):每个人一回合里两下按下之间都 ≥ gap_ms(长按不是新的按下)
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
                if (s.winner != FIGHT_UNDECIDED) {   // 下一回合:节拍重新起(真的比赛里两回合之间隔 5 秒多)
                    fight_sim_init(&s, (uint8_t)(i % FC_COUNT), (uint8_t)(j % FC_COUNT), 0, 0);
                    any[0] = any[1] = false;
                }
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

// 挨打僵着时不按(按了也白按);僵直一结束,第一下要等自己的节拍到点,平均约半个 gap_ms,不会总是正好在能动的那一刻
static void test_no_first_frame_after_hitstun(void) {
    for (int i = 0; i < 5; i++) {
        const fight_bot_cfg_t *c = ALL[i];
        fight_bot_t b;
        fight_bot_init(&b, c, rnd);
        uint32_t sum = 0, quick = 0, n = 0;
        for (int trial = 0; trial < 400; trial++) {
            fight_seen_t v = { .st = { FS_HITSTUN, FS_IDLE }, .hp = { 600, 900 } };
            uint32_t stun = 300 + rnd() % 600, t;
            for (t = 0; t < stun; t += FIGHT_STEP) CHECK(fight_bot_step(&b, &v, FIGHT_STEP) < 0);
            v.st[0] = FS_IDLE;
            for (t = 0; t < 2000 && fight_bot_step(&b, &v, FIGHT_STEP) < 0; t += FIGHT_STEP) {}
            CHECK(t < 2000);
            sum += t;
            quick += t <= FIGHT_STEP;
            n++;
            // 按完这一串、等它回到不按的状态(再挨一下)
            v.st[0] = FS_HITSTUN;
            for (t = 0; t < 1500; t += FIGHT_STEP) fight_bot_step(&b, &v, FIGHT_STEP);
        }
        CHECK(sum / n >= c->gap_ms / 4 && sum / n <= c->gap_ms);
        CHECK(quick * 100 < n * 15);
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

// bot 打一个只会每 800 ms 按一下 opp_key(-1 = 不按)的对手(一回合打完再开一回合),数 bot 按的头 2000 下里 ● ▲ ▼ 各几下
// (长按不算)。按了哪个键就是它想出什么:● 拳、▲ 重击、▼ 防(或者小招 / 大招的前缀)。
typedef struct { int ok, up, down, total; } keys_t;
static keys_t face(const fight_bot_cfg_t *c, int opp_key) {
    fight_bot_t b;
    fight_bot_init(&b, c, rnd);
    keys_t o = { 0 };
    fight_sim_t s;
    fight_sim_init(&s, FC_DAD, FC_DAD, 0, 0);
    for (uint32_t wall = 0; o.total < 2000; wall += FIGHT_STEP) {
        if (s.winner != FIGHT_UNDECIDED) fight_sim_init(&s, FC_DAD, FC_DAD, 0, 0);
        fight_seen_t v;
        fight_seen(&s, 0, &v);
        int k = fight_bot_step(&b, &v, FIGHT_STEP);
        if (k >= 0) fight_sim_key(&s, 0, (uint8_t)k);
        if (opp_key >= 0 && wall % 800 == 0) fight_sim_key(&s, 1, (uint8_t)opp_key);
        fight_sim_step(&s);
        if (k == FK_OK) o.ok++, o.total++;
        if (k == FK_UP) o.up++, o.total++;
        if (k == FK_DOWN) o.down++, o.total++;
    }
    return o;
}

static int pct(int n, const keys_t *o) { return 100 * n / o->total; }

// 大人统计对方的习惯:对方老出拳 → 多防(▼);老防 → 多抡重击(▲);老抡 → 早出拳(●)。小孩不读。
static void test_reads_habits(void) {
    keys_t base = face(&FIGHT_HUMAN_ADULT, -1);
    keys_t vs_jab = face(&FIGHT_HUMAN_ADULT, FK_OK);
    keys_t vs_guard = face(&FIGHT_HUMAN_ADULT, FK_DOWN);
    keys_t vs_heavy = face(&FIGHT_HUMAN_ADULT, FK_UP);
    CHECK(pct(vs_jab.down, &vs_jab) > pct(base.down, &base) + 8);
    CHECK(pct(vs_guard.up, &vs_guard) > pct(base.up, &base) + 8);
    CHECK(pct(vs_heavy.ok, &vs_heavy) > pct(base.ok, &base) + 5);
    keys_t kid_base = face(&FIGHT_HUMAN_KID, -1), kid_jab = face(&FIGHT_HUMAN_KID, FK_OK);
    CHECK(pct(kid_jab.down, &kid_jab) <= pct(kid_base.down, &kid_base) + 3);   // 小孩不会因为对方老出拳就多防
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
    test_no_first_frame_after_hitstun();
    test_reads_habits();
    test_super_input();
    printf("test_bot: PASS\n");
    return 0;
}
