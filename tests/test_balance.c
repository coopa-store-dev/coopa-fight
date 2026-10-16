// tests/test_balance.c —— 调数值:按真人的手速(大人 / 小孩模型,不按电脑的)打很多场,打印统计并检查计划 Task 5 的目标。
// 直接驱动 fight_sim(两边的 bot 每 10 ms 看一眼、按键走 fight_sim_key),一场先赢 FIGHT_WIN 回合、气带到下一回合、
// 平局重打这一回合;一场的时长按真的时间算(含特写、倒数、回合之间)。种子固定,结果可重现。
// 场数可以用第一个参数改(默认 200;大人两两每对 (A,B)+(B,A) 合起来、小孩对大人每格,都是 5 倍,默认 1000)。
#include <stdio.h>
#include <stdlib.h>

#include "fight_bot.h"
#include "fight_game.h"

static const char *NAME[FC_COUNT] = { "kid", "dad", "granny", "grandpa" };

#define KID_WIN_MIN 25   // 小孩对大人 16 格的平均胜率下限(计划 Task 5)
#define KID_CELL_MIN 20  // 小孩对大人每一格的下限(控制方裁定)

static uint32_t s_x = 1;
static uint32_t rnd(void) {
    s_x = s_x * 1664525u + 1013904223u;
    return s_x >> 8;
}

// ---- 死打法:按大人的手速(一秒 5 下)一直按同一套,不看场上 ----

enum { DS_JAB = 0, DS_GUARD, DS_HEAVY, DS_SPECIAL, DS_SUPER, DS_COUNT };
static const char *DS_NAME[DS_COUNT] = { "一直●", "一直▼(按着)", "一直▲", "一直▼▲", "每拳●▼●" };
#define DS_GAP 200

typedef struct {
    int dead;           // -1 = fight_bot;否则 DS_*
    fight_bot_t bot;
    uint32_t t;         // 死打法:这一轮过了多久
} player_t;

static void player_bot(player_t *p, const fight_bot_cfg_t *c) {
    p->dead = -1;
    fight_bot_init(&p->bot, c, rnd);
}

static void player_dead(player_t *p, int ds) {
    p->dead = ds;
    p->t = 0;
}

static int dead_step(player_t *p, uint32_t dt) {
    uint32_t t = p->t;
    p->t += dt;
    switch (p->dead) {
    case DS_GUARD:   // ▼ 按下,0.5 秒长按撑盾(撑到 1.2 秒),松开;冷却 0.4 秒完了再按
        if (p->t >= 1600) p->t = 0;
        return t == 0 ? FK_DOWN : t == 500 ? FK_DOWN_LONG : -1;
    case DS_SPECIAL:  // ▼ ▲ ▼ ▲ …
        if (p->t >= 2 * DS_GAP) p->t = 0;
        return t == 0 ? FK_DOWN : t == DS_GAP ? FK_UP : -1;
    case DS_SUPER:    // ● ▼ ●,停一下,再来
        if (p->t >= 4 * DS_GAP) p->t = 0;
        return t == 0 ? FK_OK : t == DS_GAP ? FK_DOWN : t == 2 * DS_GAP ? FK_OK : -1;
    default:          // 一直 ● / 一直 ▲
        if (p->t >= DS_GAP) p->t = 0;
        return t ? -1 : p->dead == DS_HEAVY ? FK_UP : FK_OK;
    }
}

static int player_step(player_t *p, const fight_seen_t *v, uint32_t dt) {
    return p->dead < 0 ? fight_bot_step(&p->bot, v, dt) : dead_step(p, dt);
}

// ---- 一场 ----

typedef struct {
    int winner;           // 0 / 1
    uint32_t ms;          // 一场多久(真的时间)
    uint32_t supers[2];   // 打中几次大招
    uint32_t rounds, kos; // 打了几回合、其中 KO 结束的
    uint32_t ev[FE_TIMEUP + 1][2];  // 每种事件、每个 who 几条(FE_TRADE 记在 [0])
} result_t;

static result_t match(player_t *p0, uint8_t c0, player_t *p1, uint8_t c1) {
    player_t *p[2] = { p0, p1 };
    result_t r = { 0 };
    uint8_t score[2] = { 0, 0 };
    uint16_t gauge[2] = { 0, 0 };
    fight_sim_t s;
    while (score[0] < FIGHT_WIN && score[1] < FIGHT_WIN) {
        fight_sim_init(&s, c0, c1, gauge[0], gauge[1]);
        r.ms += FIGHT_COUNT_MS;
        while (s.winner == FIGHT_UNDECIDED) {
            for (uint8_t w = 0; w < 2; w++) {
                fight_seen_t v;
                fight_seen(&s, w, &v);
                int k = player_step(p[w], &v, FIGHT_STEP);
                if (k >= 0) fight_sim_key(&s, w, (uint8_t)k);
            }
            fight_sim_step(&s);
            r.ms += FIGHT_STEP;
            fight_ev_t e;
            while (fight_sim_event(&s, &e)) {
                if (e.kind == FE_SUPER) r.supers[e.who]++;
                if (e.kind <= FE_TIMEUP && e.who < 2) r.ev[e.kind][e.who]++;
            }
        }
        r.rounds++;
        r.kos += s.f[0].hp <= 0 || s.f[1].hp <= 0;
        if (s.winner < 2) score[s.winner]++;
        gauge[0] = s.f[0].gauge;
        gauge[1] = s.f[1].gauge;
        r.ms += FIGHT_NEXT_MS;
    }
    r.winner = score[1] >= FIGHT_WIN;
    return r;
}

// a 用角色 ca 打 b 用角色 cb 一场;第 i 场 a 坐 0 号或 1 号(轮着,抵掉先算的一点点偏差)。返回 a 赢没赢
static bool play(player_t *a, uint8_t ca, player_t *b, uint8_t cb, int i, result_t *out) {
    result_t r;
    bool a_first = !(i & 1);
    if (a_first) {
        r = match(a, ca, b, cb);
    } else {
        r = match(b, cb, a, ca);
        uint32_t t = r.supers[0];
        r.supers[0] = r.supers[1];
        r.supers[1] = t;
        for (int k = 0; k <= FE_TIMEUP; k++) {
            t = r.ev[k][0];
            r.ev[k][0] = r.ev[k][1];
            r.ev[k][1] = t;
        }
        r.winner = !r.winner;
    }
    if (out) *out = r;
    return r.winner == 0;
}

// 没达标的记下来,表格打完再一起列(表格里没达标的格子后面打 *)
static const char *s_fail[32];
static int s_fail_n[32], s_fails;
static bool expect(bool c, const char *what) {
    if (c) return true;
    int i = 0;
    while (i < s_fails && s_fail[i] != what) i++;
    if (i == s_fails && s_fails < 32) s_fail[s_fails++] = what;
    if (i < 32) s_fail_n[i]++;
    return false;
}

// 一批场的统计(每人每场平均)
typedef struct { double n, ms, rounds, kos, ev[FE_TIMEUP + 1][2]; } tally_t;
static void tally(tally_t *t, const result_t *r) {
    t->n++;
    t->ms += r->ms;
    t->rounds += r->rounds;
    t->kos += r->kos;
    for (int k = 0; k <= FE_TIMEUP; k++)
        for (int w = 0; w < 2; w++) t->ev[k][w] += r->ev[k][w];
}
static void tally_print(const tally_t *t, const char *a, const char *b) {
    printf("  一场 %.0f 秒、%.1f 回合(KO %.0f%%);每场(%s / %s):大招 %.2f / %.2f、打中 %.1f / %.1f、被挡 %.1f / %.1f、"
           "砸开 %.2f / %.2f、太极接住 %.2f / %.2f、互相打中 %.1f\n",
           t->ms / t->n / 1000, t->rounds / t->n, 100 * t->kos / t->rounds, a, b, t->ev[FE_SUPER][0] / t->n,
           t->ev[FE_SUPER][1] / t->n, t->ev[FE_HIT][0] / t->n, t->ev[FE_HIT][1] / t->n, t->ev[FE_BLOCK][0] / t->n,
           t->ev[FE_BLOCK][1] / t->n, t->ev[FE_GUARD_BREAK][0] / t->n, t->ev[FE_GUARD_BREAK][1] / t->n,
           t->ev[FE_COUNTER][0] / t->n, t->ev[FE_COUNTER][1] / t->n, t->ev[FE_TRADE][0] / t->n);
}

static double pc(uint32_t a, uint32_t b) { return b ? 100.0 * a / b : 0; }

// ---- 狂按:挨了大招(放的人之后不动)到醒,平均多久 ----

static double mash_ms(const fight_bot_cfg_t *c, int n) {
    double sum = 0;
    for (int i = 0; i < n; i++) {
        fight_sim_t s;
        fight_sim_init(&s, (uint8_t)(i % FC_COUNT), (uint8_t)(i / FC_COUNT % FC_COUNT), FIGHT_GAUGE_BAR, 0);
        fight_bot_t b;
        fight_bot_init(&b, c, rnd);
        uint32_t wall = 0, dizzy_at = 0;
        bool dizzy = false, lit = false;
        fight_sim_key(&s, 0, FK_OK);
        for (; wall < 10000; wall += FIGHT_STEP) {
            lit = lit || s.freeze;
            if (wall == 100) fight_sim_key(&s, 0, FK_DOWN);
            if (wall == 200) fight_sim_key(&s, 0, FK_OK);
            if (lit) {   // 挨打的这边:特写开始以后才动脑子(之前站着挨 ●)
                fight_seen_t v;
                fight_seen(&s, 1, &v);
                int k = fight_bot_step(&b, &v, FIGHT_STEP);
                if (k >= 0) fight_sim_key(&s, 1, (uint8_t)k);
            }
            fight_sim_step(&s);
            if (!dizzy && s.f[1].st == FS_DIZZY) dizzy = true, dizzy_at = wall;
            if (dizzy && s.f[1].st != FS_DIZZY) break;
        }
        if (!dizzy) {
            printf("    !! 狂按测量:大招没放出来(第 %d 次)\n", i);
            exit(1);
        }
        sum += wall - dizzy_at;
    }
    return sum / n / 1000;
}

static const char *mark(bool good) { return good ? " " : "*"; }

int main(int argc, char **argv) {
    int n = argc > 1 ? atoi(argv[1]) : 200, pair_n = 5 * n;   // 两两胜率:每对 5n 场(默认 1000)
    player_t A, B;

    // 1. 大人对大人:6 对不同的角色,每对 (A,B)+(B,A) 合起来 pair_n 场(两边轮着坐 0 号),判 A 的胜率
    printf("== 大人对大人:每对 %d 场,前一个角色的胜率 ==\n ", pair_n);
    tally_t ta = { 0 };
    uint32_t aa_try = 0, aa_ok = 0;
    for (uint8_t ca = 0; ca < FC_COUNT; ca++) {
        for (uint8_t cb = ca + 1; cb < FC_COUNT; cb++) {
            s_x = 1000u * ca + cb + 1;
            uint32_t won = 0;
            for (int i = 0; i < pair_n; i++) {
                player_bot(&A, &FIGHT_HUMAN_ADULT);
                player_bot(&B, &FIGHT_HUMAN_ADULT);
                result_t r;
                won += play(&A, ca, &B, cb, i, &r);
                tally(&ta, &r);
                aa_try += A.bot.super_try + B.bot.super_try;
                aa_ok += A.bot.super_ok + B.bot.super_ok;
            }
            double w = pc(won, (uint32_t)pair_n);
            printf(" %s-%s %3.0f%%%s", NAME[ca], NAME[cb], w, mark(expect(w >= 45 && w <= 55, "大人对大人每对 45%–55%")));
        }
    }
    printf("\n");
    tally_print(&ta, "大人", "大人");
    double avg_s = ta.ms / ta.n / 1000;
    expect(avg_s >= 90 && avg_s <= 210, "一场平均 90–210 秒");

    // 2. 小孩对大人:16 种组合(小孩的角色 × 大人的角色)各 pair_n 场(两边轮着坐 0 号);每格 ≥ KID_CELL_MIN,16 格平均 ≥ KID_WIN_MIN
    printf("== 小孩(行)对大人(列):每格 %d 场,小孩胜率 / 小孩打中过大招的场 ==\n", pair_n);
    tally_t tk = { 0 };
    uint32_t kid_try = 0, kid_ok = 0, ad_try = aa_try, ad_ok = aa_ok;
    double kid_min = 100, kid_sum = 0;
    for (uint8_t ck = 0; ck < FC_COUNT; ck++) {
        printf("  %-8s", NAME[ck]);
        for (uint8_t cd = 0; cd < FC_COUNT; cd++) {
            s_x = 2000u + 10u * ck + cd;
            uint32_t won = 0, sup = 0;
            for (int i = 0; i < pair_n; i++) {
                player_bot(&A, &FIGHT_HUMAN_KID);
                player_bot(&B, &FIGHT_HUMAN_ADULT);
                result_t r;
                won += play(&A, ck, &B, cd, i, &r);
                tally(&tk, &r);
                sup += r.supers[0] > 0;
                kid_try += A.bot.super_try;
                kid_ok += A.bot.super_ok;
                ad_try += B.bot.super_try;
                ad_ok += B.bot.super_ok;
            }
            double w = pc(won, (uint32_t)pair_n), sp = pc(sup, (uint32_t)pair_n);
            kid_sum += w;
            if (w < kid_min) kid_min = w;
            bool good = expect(w >= KID_CELL_MIN, "小孩对大人每格胜率 ≥ KID_CELL_MIN");
            good = expect(sp >= 50, "小孩每场打中过大招的场 ≥ 50%") && good;
            printf("  %-7s %2.0f%%/%2.0f%%%s", NAME[cd], w, sp, mark(good));
        }
        printf("\n");
    }
    tally_print(&tk, "小孩", "大人");
    double kid_mean = kid_sum / 16, kid_sup = tk.ev[FE_SUPER][0] / tk.n, kms = tk.ms / tk.n / 1000;
    bool gm = expect(kid_mean >= KID_WIN_MIN, "小孩对大人 16 格平均 ≥ KID_WIN_MIN");
    bool gs = expect(kid_sup <= 2.0, "小孩每场大招 ≤ 2.0(设计 §4.3「一两次」)");
    bool gl = expect(kms >= 90 && kms <= 240, "小孩对大人一场平均 90–240 秒");
    printf("  小孩胜率:最低 %.0f%%(每格 ≥ %d%%)、平均 %.1f%%%s(≥ %d%%);小孩每场大招 %.2f%s(≤ 2.0);一场 %.0f 秒%s\n", kid_min,
           KID_CELL_MIN, kid_mean, mark(gm), KID_WIN_MIN, kid_sup, mark(gs), kms, mark(gl));

    // 3. ● ▼ ● 在 ● 打中的前提下出了大招
    double sa = pc(ad_ok, ad_try), sk = pc(kid_ok, kid_try);
    bool ga = expect(sa >= 85, "大人 ●▼● 成功率 ≥ 85%"), gk = expect(sk >= 55, "小孩 ●▼● 成功率 ≥ 55%");
    printf("== ●▼● 打中后出了大招:大人 %.0f%%%s(%u 次)、小孩 %.0f%%%s(%u 次)==\n", sa, mark(ga), ad_try, sk, mark(gk),
           kid_try);

    // 4. 狂按
    s_x = 3000;
    double ma = mash_ms(&FIGHT_HUMAN_ADULT, n), mk = mash_ms(&FIGHT_HUMAN_KID, n);
    ga = expect(ma >= 1.0 && ma <= 1.3, "大人狂按醒 1.0–1.3 秒");
    gk = expect(mk >= 1.2 && mk <= 1.5, "小孩狂按醒 1.2–1.5 秒");
    printf("== 大招后狂按到醒:大人 %.2f 秒%s、小孩 %.2f 秒%s(不按 %.1f 秒)==\n", ma, mark(ga), mk, mark(gk),
           FIGHT_DIZZY_SUPER / 1000.0);

    // 5. test_dead_strategies:死打法 × 四个角色,各打大人模型 n 场(大人轮着用四个角色)
    puts("== test_dead_strategies:死打法打大人(死打法的胜率)==");
    printf("  %-14s", "");
    for (uint8_t c = 0; c < FC_COUNT; c++) printf(" %8s", NAME[c]);
    printf("\n");
    for (int ds = 0; ds < DS_COUNT; ds++) {
        printf("  %-14s", DS_NAME[ds]);
        for (uint8_t c = 0; c < FC_COUNT; c++) {
            s_x = 4000u + 10u * (uint32_t)ds + c;
            uint32_t won = 0;
            for (int i = 0; i < n; i++) {
                player_dead(&A, ds);
                player_bot(&B, &FIGHT_HUMAN_ADULT);
                won += play(&A, c, &B, (uint8_t)(i / 2 % FC_COUNT), i, NULL);
            }
            double w = pc(won, (uint32_t)n);
            printf("   %4.0f%%%s", w, mark(expect(w <= 60, "死打法打大人 ≤ 60%")));
        }
        printf("\n");
    }
    for (uint8_t c = FC_DAD; c <= FC_GRANDPA; c += FC_GRANDPA - FC_DAD) {
        s_x = 5000u + c;
        uint32_t won = 0;
        for (int i = 0; i < n; i++) {
            player_dead(&A, DS_SPECIAL);
            player_bot(&B, &FIGHT_HUMAN_KID);
            won += play(&A, c, &B, (uint8_t)(i / 2 % FC_COUNT), i, NULL);
        }
        double w = pc(won, (uint32_t)n);
        printf("  %s 一直▼▲ 打小孩:%.0f%%%s\n", NAME[c], w, mark(expect(w <= 80, "一直▼▲ 打小孩 ≤ 80%")));
    }

    // 6. 厉害对简单
    s_x = 6000;
    uint32_t hard = 0, m = 300;
    for (uint32_t i = 0; i < m; i++) {
        player_bot(&A, &FIGHT_BOT_HARD);
        player_bot(&B, &FIGHT_BOT_EASY);
        hard += play(&A, (uint8_t)(i % FC_COUNT), &B, (uint8_t)(i / FC_COUNT % FC_COUNT), (int)(i / 16), NULL);
    }
    double hw = pc(hard, m);
    printf("== 厉害对简单 %u 场:厉害赢 %.0f%%%s ==\n", m, hw, mark(expect(hw >= 75 && hw <= 95, "厉害对简单 75%–95%")));

    if (s_fails) {
        for (int i = 0; i < s_fails; i++) printf("  没达标:%s(%d 项)\n", s_fail[i], s_fail_n[i]);
        puts("test_balance: FAIL(数值没达到计划 Task 5 的目标)");
        return 1;
    }
    puts("test_balance: PASS");
    return 0;
}
