// tests/test_super.c —— 大招、小招、晕和狂按、保护(防「晕了又晕」)、晚到的连招键。
#include <stdio.h>
#include <stdlib.h>

#include "fight_sim.h"

#define CHECK(c) do { if (!(c)) { \
    printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); exit(1); } } while (0)

static fight_sim_t S;
static int EV[FE_TIMEUP + 1][2];   // 每种事件、每个 who 来了几条(who = FIGHT_DRAW 记在 [0])
static fight_ev_t LAST[FE_TIMEUP + 1];

static void pump(void) {
    fight_ev_t e;
    while (fight_sim_event(&S, &e)) {
        EV[e.kind][e.who == 1]++;
        LAST[e.kind] = e;
    }
}
static int ev(uint8_t kind) { pump(); return EV[kind][0] + EV[kind][1]; }
static int ev_by(uint8_t kind, int who) { pump(); return EV[kind][who]; }

static void start(uint8_t c0, uint8_t c1, uint16_t g0, uint16_t g1) {
    fight_sim_init(&S, c0, c1, g0, g1);
    for (int k = 0; k <= FE_TIMEUP; k++) EV[k][0] = EV[k][1] = 0;
}
static void run_to(uint32_t clock) { while (S.clock < clock && S.winner == FIGHT_UNDECIDED) fight_sim_step(&S); }
static void key(int who, uint8_t k) { fight_sim_key(&S, (uint8_t)who, k); }
static int hp_max(int who) { return fight_char(S.f[who].ch)->hp; }

// c0 提前按 ● ▼ ● 打 c1 放大招,跑完特写:返回 c1 开始晕的 clock
static uint32_t super_dizzy_as(uint8_t c0, uint8_t c1, uint16_t g0) {
    start(c0, c1, g0, 0);
    key(0, FK_OK);
    run_to(50);
    key(0, FK_DOWN);
    run_to(100);
    key(0, FK_OK);
    while (!S.freeze) fight_sim_step(&S);
    while (S.freeze) fight_sim_step(&S);
    CHECK(S.f[1].st == FS_DIZZY && ev(FE_SUPER) == 1);
    return S.clock;
}

// 奶奶打库巴:200 打到、放大招
static uint32_t super_dizzy(uint16_t g0) {
    uint32_t d = super_dizzy_as(FC_GRANNY, FC_KID, g0);
    CHECK(d == 200);
    return d;
}

// 不按,晕到自己醒:返回醒的 clock
static uint32_t wake_natural(void) {
    while (S.f[1].st == FS_DIZZY) fight_sim_step(&S);
    CHECK(S.f[1].st == FS_IDLE && S.f[1].eat_keys == FIGHT_WAKE_EAT);
    return S.clock;
}

// 晕着的 1 号:开始晕后 first ms 按第一下,之后每 every ms 一下(● ▲ ▼ 轮着);返回醒的时候晕了多久
static uint32_t mash(uint32_t first, uint32_t every) {
    static const uint8_t keys[3] = { FK_OK, FK_UP, FK_DOWN };
    uint32_t d = S.clock, next = d + first, n = 0;
    while (S.f[1].st == FS_DIZZY) {
        fight_sim_step(&S);
        if (S.f[1].st == FS_DIZZY && S.clock >= next) {
            key(1, keys[n++ % 3]);
            next += every;
            CHECK(S.f[1].qn == 0 && S.f[1].down_at == UINT32_MAX);   // 狂按不进 queue,也不记 ▼
        }
    }
    return S.clock - d;
}

static void test_jab_down_ok_super(void) {
    start(FC_GRANNY, FC_KID, 1000, 0);
    key(0, FK_OK);
    run_to(60);
    key(0, FK_DOWN);
    run_to(120);
    key(0, FK_OK);
    run_to(200);
    CHECK(ev_by(FE_SUPER, 0) == 1 && LAST[FE_SUPER].move == FM_SUPER);
    CHECK(S.freeze == FIGHT_SUPER_FREEZE && S.f[0].st == FS_SUPER_OUT && S.f[1].st == FS_SUPER_IN);
    CHECK(S.f[0].gauge == 1000 + FIGHT_GAUGE_HIT - FIGHT_GAUGE_BAR && S.f[1].gauge == FIGHT_GAUGE_HURT);
    CHECK(S.f[1].hp == hp_max(1) - 40);
    // 特写里两边按的键都丢掉(不排队、不记 ▼)
    key(1, FK_DOWN);
    key(1, FK_OK);
    key(0, FK_OK);
    CHECK(S.f[0].qn == 0 && S.f[1].qn == 0 && S.f[1].down_at == UINT32_MAX && S.f[1].st == FS_SUPER_IN);
    for (int i = 0; i < FIGHT_SUPER_FREEZE / FIGHT_STEP - 1; i++) fight_sim_step(&S);
    CHECK(S.clock == 200 && S.freeze == FIGHT_STEP && S.f[1].hp == hp_max(1) - 40);
    fight_sim_step(&S);
    CHECK(S.clock == 200 && S.freeze == 0);
    CHECK(S.f[1].hp == hp_max(1) - 40 - 330);
    CHECK(ev_by(FE_HIT, 0) == 2 && LAST[FE_HIT].move == FM_SUPER);
    CHECK(S.f[1].st == FS_DIZZY && S.f[1].dur == FIGHT_DIZZY_SUPER && S.f[1].mash_left == FIGHT_DIZZY_SUPER);
    CHECK(S.f[1].protect && ev_by(FE_DIZZY, 1) == 1);
    CHECK(S.f[0].st == FS_IDLE);
    CHECK(S.f[0].gauge == FIGHT_GAUGE_HIT && S.f[1].gauge == FIGHT_GAUGE_HURT);   // 大招这一下不涨气
    fight_sim_step(&S);
    CHECK(S.clock == 210);
    // 不按:晕 2 秒自己醒
    run_to(200 + FIGHT_DIZZY_SUPER - FIGHT_STEP);
    CHECK(S.f[1].st == FS_DIZZY && ev(FE_WAKE) == 0);
    run_to(200 + FIGHT_DIZZY_SUPER);
    CHECK(S.f[1].st == FS_IDLE && ev_by(FE_WAKE, 1) == 1 && S.f[1].eat_keys == FIGHT_WAKE_EAT);
}

// 没气:● 打中后的 ▼ ● 是举盾、一拳
static void test_super_needs_gauge(void) {
    start(FC_GRANNY, FC_KID, 900, 0);
    key(0, FK_OK);
    run_to(300);
    key(0, FK_DOWN);
    CHECK(S.f[0].qn == 1);   // ▼ 先等着
    run_to(400);
    key(0, FK_OK);
    CHECK(S.f[0].st == FS_MOVE && S.f[0].move == FM_JAB && S.f[0].qn == 0);
    CHECK(S.f[0].guard_cool == FIGHT_GUARD_COOL && S.f[0].chain == 0);   // 盾举起来又被这一拳放下了
    CHECK(ev(FE_SUPER) == 0 && S.f[0].gauge == 900 + FIGHT_GAUGE_HIT);
    run_to(600);
    CHECK(S.f[1].hp == hp_max(1) - 80 && S.f[0].chain == 1);   // 对方还僵着,这一拳也中
}

// 第一拳被挡:提前按的 ▼ ● 收招完是举盾 + 一拳,不出大招、不扣气
static void test_super_on_block_is_guard_jab(void) {
    start(FC_GRANNY, FC_KID, 1000, 0);
    key(1, FK_DOWN);
    key(0, FK_OK);
    run_to(60);
    key(0, FK_DOWN);
    run_to(120);
    key(0, FK_OK);
    run_to(200);
    CHECK(S.f[0].st == FS_BLOCKSTUN && ev_by(FE_BLOCK, 0) == 1);
    run_to(850);
    CHECK(S.f[0].st == FS_MOVE && S.f[0].move == FM_JAB && S.f[0].qn == 0);
    CHECK(S.f[0].guard_cool == FIGHT_GUARD_COOL);
    run_to(3000);
    CHECK(ev(FE_SUPER) == 0 && S.f[0].gauge >= 1000);
}

// 小孩一秒 3.5 下(两下隔 ~290 ms)按 ● ▼ ●:四个角色都来得及
static void test_super_kid_timing(void) {
    for (uint8_t c = 0; c < FC_COUNT; c++) {
        start(c, FC_GRANNY, 1000, 0);
        key(0, FK_OK);
        run_to(290);
        key(0, FK_DOWN);
        run_to(580);
        key(0, FK_OK);
        CHECK(ev_by(FE_SUPER, 0) == 1);
        while (S.freeze) fight_sim_step(&S);
        CHECK(S.f[1].st == FS_DIZZY);
    }
}

// ▼ 在对方僵直结束后才按:普通举盾,再按 ● 是新的一拳
static void test_super_late_down(void) {
    start(FC_GRANNY, FC_KID, 1000, 0);
    key(0, FK_OK);
    run_to(850);
    key(0, FK_DOWN);
    CHECK(S.f[0].st == FS_GUARD);
    run_to(900);
    key(0, FK_OK);
    CHECK(S.f[0].st == FS_MOVE && S.f[0].move == FM_JAB);
    run_to(1500);
    CHECK(ev(FE_SUPER) == 0 && S.f[1].hp == hp_max(1) - 80 && S.f[0].chain == 1);
}

// 大人知道要晕、马上按,一秒 5 下:约 1.1 秒醒
static void test_mash_adult(void) {
    super_dizzy(1000);
    uint32_t t = mash(100, 200);
    CHECK(t >= 1000 && t <= 1200);
    CHECK(ev_by(FE_WAKE, 1) == 1 && fight_mash_taken(&S, 1) == 6);
}

// 小孩一秒 3.5 下:约 1.3 秒醒
static void test_mash_kid(void) {
    super_dizzy(1000);
    uint32_t t = mash(143, 286);
    CHECK(t >= 1200 && t <= 1400);
}

// 一秒按 10 下也只算 6 下;「一秒」是滚动的
static void test_mash_cap(void) {
    super_dizzy(1000);
    uint32_t t = mash(100, 100);
    CHECK(fight_mash_taken(&S, 1) == FIGHT_MASH_PER_SEC);
    CHECK(t == FIGHT_DIZZY_SUPER - (uint32_t)FIGHT_MASH_PER_SEC * FIGHT_MASH_STEP);   // 1040:后面四下没算

    // 晕得够久时(测试直接拉长):100..2500 每 100 一下 → 100–600、1100–1600、2100–2500 算,共 17 下
    // (按整秒切块会算成 18 下)
    uint32_t d = super_dizzy(1000);
    S.f[1].mash_left = 30000;
    for (uint32_t at = d + 100; at <= d + 2500; at += 100) {
        run_to(at);
        key(1, FK_OK);
    }
    CHECK(fight_mash_taken(&S, 1) == 17 && S.f[1].st == FS_DIZZY);
}

// 晕着挨打:立刻醒、照常吃这一下,连招继续;回到能动后保护消失
static void test_hit_wakes(void) {
    uint32_t d = super_dizzy(1000);
    key(0, FK_OK);
    run_to(d + 190);
    CHECK(S.f[1].st == FS_DIZZY);
    run_to(d + 200);
    CHECK(ev_by(FE_WAKE, 1) == 1 && S.f[1].st == FS_HITSTUN && S.f[1].dur == 600);
    CHECK(S.f[1].hp == hp_max(1) - 40 - 330 - 40);
    CHECK(S.f[0].chain == 1 && S.f[0].chain_open);
    CHECK(S.f[1].mash_left == 0 && S.f[1].eat_keys == 0 && S.f[1].protect);
    run_to(d + 790);
    CHECK(S.f[1].protect);
    run_to(d + 800);
    CHECK(S.f[1].st == FS_IDLE && !S.f[1].protect);
}

// 晕着挨 ● ● ▼ ●:第一拳打醒,之后又吃两拳(▼ ● 只出拳),大招不出、气不扣
static void test_no_super_on_dizzy_combo(void) {
    uint32_t d = super_dizzy(2000);
    uint16_t g = S.f[0].gauge;
    CHECK(g == 2000 + FIGHT_GAUGE_HIT - FIGHT_GAUGE_BAR);
    key(0, FK_OK);
    run_to(d + 50);
    key(0, FK_OK);
    run_to(d + 100);
    key(0, FK_DOWN);
    run_to(d + 150);
    key(0, FK_OK);
    run_to(d + 3000);
    CHECK(ev(FE_SUPER) == 1 && ev(FE_DIZZY) == 1 && ev_by(FE_WAKE, 1) == 1);
    CHECK(S.f[1].hp == hp_max(1) - 40 - 330 - 40 - 40 - 70);
    CHECK(S.f[0].gauge == g + 2 * FIGHT_GAUGE_HIT);   // 打醒那一下不涨(对方晕着),后两下照常
}

// 重击砸开防御晕住:晕之前就提前按好的 ● ▼ ●,打醒以后也只出拳(保护在出招时也判)
static void test_no_super_after_break(void) {
    start(FC_KID, FC_DAD, 2000, 0);
    key(0, FK_UP);   // 600 打到
    run_to(100);
    key(1, FK_DOWN);   // 爸爸的盾举到 700
    run_to(500);
    key(0, FK_OK);
    run_to(540);
    key(0, FK_DOWN);
    run_to(580);
    key(0, FK_OK);
    run_to(600);
    CHECK(ev_by(FE_GUARD_BREAK, 0) == 1 && S.f[1].st == FS_DIZZY && S.f[1].dur == FIGHT_DIZZY_BREAK);
    CHECK(S.f[1].protect && S.f[0].qn == 3);
    run_to(3000);
    CHECK(ev(FE_SUPER) == 0 && ev(FE_DIZZY) == 1 && ev_by(FE_WAKE, 1) == 1);
    CHECK(S.f[1].hp == hp_max(1) - 120 - 40 - 40 && S.f[0].gauge >= 2000);
}

// 审核的时间线:醒的那一刻起 ● ● ▼ ● 掐好时间,大招仍不出;醒后 0.3 秒内打不中
static void test_wake_timing_loop(void) {
    // 醒的那一刻就按:第一拳 0.2 秒打到,打空;排着的键收招后照常出,▼ ● 只出拳
    super_dizzy(2000);
    uint32_t w = wake_natural();
    int hp = S.f[1].hp;
    key(0, FK_OK);
    run_to(w + 50);
    key(0, FK_OK);
    run_to(w + 100);
    key(0, FK_DOWN);
    run_to(w + 150);
    key(0, FK_OK);
    run_to(w + 200);
    CHECK(ev_by(FE_WHIFF, 0) == 1 && S.f[1].hp == hp && S.f[0].chain == 0);
    run_to(w + 3000);
    CHECK(ev(FE_SUPER) == 1 && ev(FE_DIZZY) == 1);
    CHECK(S.f[1].hp == hp - 40 - 40);

    // 第一拳掐在醒后 290 ms 打到:还在 0.3 秒里,打空
    super_dizzy(2000);
    w = wake_natural();
    run_to(w + 90);
    key(0, FK_OK);
    run_to(w + 290);
    CHECK(ev_by(FE_WHIFF, 0) == 1 && S.f[1].hp == hp);

    // 第一拳掐在 0.3 秒刚过的那一刻打到:中了,但他还没动过,保护还在 → ● ● ▼ ● 是三拳
    super_dizzy(2000);
    w = wake_natural();
    run_to(w + 100);
    key(0, FK_OK);
    run_to(w + 150);
    key(0, FK_OK);
    run_to(w + 200);
    key(0, FK_DOWN);
    run_to(w + 250);
    key(0, FK_OK);
    run_to(w + 300);
    CHECK(S.f[1].st == FS_HITSTUN && S.f[1].protect && ev(FE_WHIFF) == 0);
    run_to(w + 3000);
    CHECK(ev(FE_SUPER) == 1 && ev(FE_DIZZY) == 1);
    CHECK(S.f[1].hp == hp - 40 - 40 - 70 && !S.f[1].protect);
    CHECK(S.f[0].gauge >= FIGHT_GAUGE_BAR);   // 有气也没出

    // 起手在 0.3 秒里出的(醒后 110 ms),打到时(310)保护已经没了:这套连招仍然不能有大招
    super_dizzy(2000);
    w = wake_natural();
    run_to(w + 110);
    key(0, FK_OK);
    run_to(w + 310);
    CHECK(S.f[1].st == FS_HITSTUN && !S.f[1].protect && S.f[0].chain == 1);
    run_to(w + 320);
    key(0, FK_DOWN);
    run_to(w + 400);
    key(0, FK_OK);
    run_to(w + 3000);
    CHECK(ev(FE_SUPER) == 1 && ev(FE_DIZZY) == 1 && S.f[1].hp == hp - 40 - 40);

    // 库巴弹弓起手(醒后 110 ms 出、510 打到,开连招):同样不能有大招
    super_dizzy_as(FC_KID, FC_DAD, 2000);
    w = wake_natural();
    int dad_hp = S.f[1].hp;
    run_to(w + 60);
    key(0, FK_DOWN);
    run_to(w + 110);
    key(0, FK_UP);
    CHECK(S.f[0].move == FM_SPECIAL);
    run_to(w + 510);
    CHECK(S.f[0].chain == 1 && S.f[0].chain_open && !S.f[1].protect);
    run_to(w + 600);
    key(0, FK_DOWN);
    run_to(w + 650);
    key(0, FK_OK);
    run_to(w + 3000);
    CHECK(ev(FE_SUPER) == 1 && ev(FE_DIZZY) == 1 && S.f[1].hp == dad_hp - 60 - 40);

    // 起手在保护消失以后才出(醒后 300 ms):正常的连招,● ▼ ● 能出大招
    super_dizzy(2000);
    w = wake_natural();
    run_to(w + 300);
    CHECK(!S.f[1].protect);
    key(0, FK_OK);
    run_to(w + 500);
    CHECK(S.f[0].chain == 1);
    key(0, FK_DOWN);
    run_to(w + 560);
    key(0, FK_OK);
    CHECK(ev_by(FE_SUPER, 0) == 2);
}

// 审核的复现:晕着被打醒的这一套,对方缓过来后晚到的 ▼ ●(本来在窗口里按的)也不能是大招
static void test_late_keys_after_wake_hit(void) {
    uint32_t d = super_dizzy(2000);
    key(0, FK_OK);
    run_to(d + 200);
    CHECK(S.f[1].st == FS_HITSTUN && S.f[0].chain_until == d + 800);
    run_to(d + 990);
    CHECK(S.f[1].st == FS_IDLE && !S.f[1].protect);
    fight_sim_key_at(&S, 0, FK_DOWN, 320);   // 本来 d + 670
    fight_sim_key_at(&S, 0, FK_OK, 290);     // 本来 d + 700
    CHECK(ev(FE_SUPER) == 1 && S.f[0].move == FM_JAB2 && S.f[1].st == FS_HITSTUN);   // 只出拳(照样拉回僵直)
    run_to(d + 3000);
    CHECK(ev(FE_SUPER) == 1 && ev(FE_DIZZY) == 1);
    CHECK(S.f[1].hp == hp_max(1) - 40 - 330 - 40 - 40);
}

// 自己醒、过了 0.3 秒回到能动:保护消失,下一次 ● ▼ ● 能出
static void test_protect_clears(void) {
    super_dizzy(2000);
    uint32_t w = wake_natural();
    run_to(w + 290);
    CHECK(S.f[1].protect);
    run_to(w + 300);
    CHECK(!S.f[1].protect && S.f[1].eat_keys == 0);
    run_to(w + 400);
    key(0, FK_OK);
    run_to(w + 460);
    key(0, FK_DOWN);
    run_to(w + 520);
    key(0, FK_OK);
    run_to(w + 600);
    CHECK(ev_by(FE_SUPER, 0) == 2);
}

// 晕着和醒后 0.3 秒里按的键(狂按的最后几下)不变成出招;0.3 秒一过就收键
static void test_wake_keys_eaten(void) {
    super_dizzy(1000);
    mash(100, 150);
    uint32_t w = S.clock;
    CHECK(S.f[1].st == FS_IDLE && S.f[1].eat_keys == FIGHT_WAKE_EAT);
    static const uint8_t keys[4] = { FK_OK, FK_UP, FK_DOWN, FK_DOWN_LONG };
    for (uint32_t at = w; at < w + FIGHT_WAKE_EAT; at += 10) {
        run_to(at);
        key(1, keys[(at / 10) % 4]);
        CHECK(S.f[1].st == FS_IDLE && S.f[1].qn == 0 && S.f[1].down_at == UINT32_MAX);
    }
    CHECK(ev_by(FE_START, 1) == 0);
    run_to(w + FIGHT_WAKE_EAT);
    key(1, FK_UP);   // 0.3 秒里按的 ▼ 不算前缀:这是普通重击
    CHECK(S.f[1].st == FS_MOVE && S.f[1].move == FM_HEAVY && ev_by(FE_START, 1) == 1);
}

// 互相打中:两边提前按的 ● ▼ ● 都作废,不出大招
static void test_trade_no_super(void) {
    start(FC_GRANNY, FC_GRANNY, 1000, 1000);
    for (int i = 0; i < 2; i++) key(i, FK_OK);
    run_to(60);
    for (int i = 0; i < 2; i++) key(i, FK_DOWN);
    run_to(120);
    for (int i = 0; i < 2; i++) key(i, FK_OK);
    run_to(3000);
    CHECK(ev(FE_TRADE) == 1 && ev(FE_SUPER) == 0 && ev(FE_START) == 2);
    for (int i = 0; i < 2; i++) {
        CHECK(S.f[i].gauge == 1000 + FIGHT_GAUGE_HIT + FIGHT_GAUGE_HURT);
        CHECK(S.f[i].st == FS_IDLE && S.f[i].qn == 0);
    }
}

// 从盾后面出小招,四个角色各一个
static void test_special_from_guard(void) {
    // 库巴弹弓:打中能接连招
    start(FC_KID, FC_DAD, 0, 0);
    key(0, FK_DOWN);
    run_to(100);
    key(0, FK_UP);
    CHECK(S.f[0].st == FS_MOVE && S.f[0].move == FM_SPECIAL);
    CHECK(S.f[0].guard_cool == FIGHT_GUARD_COOL && S.f[0].special_cool == FIGHT_SPECIAL_COOL);
    run_to(500);
    CHECK(S.f[1].hp == hp_max(1) - 60 && S.f[1].st == FS_HITSTUN && S.f[0].chain == 1 && S.f[0].chain_open);
    run_to(600);
    key(0, FK_OK);
    CHECK(S.f[0].move == FM_JAB2);
    run_to(750);
    CHECK(S.f[1].hp == hp_max(1) - 60 - 40 && S.f[0].chain == 2);

    // 爸爸肩撞:砸开防御,对方只僵不晕
    start(FC_DAD, FC_KID, 0, 0);
    key(1, FK_DOWN);
    key(0, FK_DOWN);
    run_to(50);
    key(0, FK_UP);
    run_to(400);
    CHECK(ev_by(FE_GUARD_BREAK, 0) == 1 && S.f[1].hp == hp_max(1) - 80);
    CHECK(S.f[1].st == FS_HITSTUN && S.f[1].dur == 600 && !S.f[1].protect && ev(FE_DIZZY) == 0);

    // 奶奶锅盖:第一下被挡,第二下(900 ms)对方盾放下了 → 中
    start(FC_GRANNY, FC_KID, 0, 0);
    key(0, FK_DOWN);
    run_to(100);
    key(1, FK_DOWN);
    key(0, FK_UP);
    CHECK(S.f[0].move == FM_SPECIAL);
    run_to(400);
    CHECK(ev_by(FE_BLOCK, 0) == 1 && S.f[1].hp == hp_max(1));
    run_to(990);
    CHECK(S.f[1].hp == hp_max(1) && S.f[1].st == FS_IDLE);
    run_to(1000);
    CHECK(ev_by(FE_HIT, 0) == 1 && S.f[1].hp == hp_max(1) - 40 && S.f[1].st == FS_HITSTUN);
    // 长按撑盾:两下都挡住
    start(FC_GRANNY, FC_KID, 0, 0);
    key(0, FK_DOWN);
    run_to(100);
    key(1, FK_DOWN);
    key(0, FK_UP);
    run_to(600);
    key(1, FK_DOWN_LONG);
    run_to(1500);
    CHECK(ev_by(FE_BLOCK, 0) == 2 && S.f[1].hp == hp_max(1));
    // 第一下中:对方僵到第二下飞回来,也中
    start(FC_GRANNY, FC_KID, 0, 0);
    key(0, FK_DOWN);
    run_to(100);
    key(0, FK_UP);
    run_to(1500);
    CHECK(ev_by(FE_HIT, 0) == 2 && S.f[1].hp == hp_max(1) - 80);

    // 爷爷太极:对方轻拳打过来 → 接住反打,对方只僵不晕;爷爷的连招窗口打开
    start(FC_GRANDPA, FC_GRANNY, 1000, 0);
    key(0, FK_DOWN);
    run_to(50);
    key(0, FK_UP);
    CHECK(S.f[0].st == FS_STANCE && S.f[0].dur == FIGHT_STANCE_MS);
    run_to(100);
    key(1, FK_OK);
    run_to(300);
    CHECK(ev_by(FE_COUNTER, 0) == 1 && ev(FE_HIT) == 0);
    CHECK(S.f[0].hp == hp_max(0) && S.f[1].hp == hp_max(1) - 60);
    CHECK(S.f[1].st == FS_HITSTUN && S.f[1].dur == 600 && ev(FE_DIZZY) == 0);
    CHECK(S.f[0].st == FS_IDLE && S.f[0].chain == 1 && S.f[0].chain_open);
    key(0, FK_OK);
    CHECK(S.f[0].move == FM_JAB2);
    run_to(550);
    CHECK(S.f[1].hp == hp_max(1) - 60 - 46);
    // 接住以后 ▼ ● 是大招
    start(FC_GRANDPA, FC_GRANNY, 1000, 0);
    key(0, FK_DOWN);
    run_to(50);
    key(0, FK_UP);
    run_to(100);
    key(1, FK_OK);
    run_to(300);
    key(0, FK_DOWN);
    run_to(350);
    key(0, FK_OK);
    CHECK(ev_by(FE_SUPER, 0) == 1);
    // 重击、小招打过来也接住
    for (int k = 0; k < 2; k++) {
        start(FC_GRANDPA, FC_DAD, 0, 0);
        key(0, FK_DOWN);
        run_to(10);
        key(0, FK_UP);
        if (k == 1) key(1, FK_DOWN);
        key(1, FK_UP);   // 爸爸重击 500 / 肩撞 350 打到
        CHECK(S.f[1].move == (k ? FM_SPECIAL : FM_HEAVY));
        run_to(600);
        CHECK(ev_by(FE_COUNTER, 0) == 1 && S.f[0].hp == hp_max(0) && S.f[1].hp == hp_max(1) - 60);
        CHECK(S.f[1].st == FS_HITSTUN);
    }
}

// ▼ 后 290 ms 按 ▲ → 小招;400 ms → 重击
static void test_special_prefix_kid(void) {
    start(FC_KID, FC_DAD, 0, 0);
    key(0, FK_DOWN);
    run_to(290);
    key(0, FK_UP);
    CHECK(S.f[0].move == FM_SPECIAL);
    start(FC_KID, FC_DAD, 0, 0);
    key(0, FK_DOWN);
    run_to(400);
    key(0, FK_UP);
    CHECK(S.f[0].move == FM_HEAVY);
}

// ▼ 长按撑盾时按 ▲ → 小招
static void test_special_long_guard(void) {
    start(FC_KID, FC_DAD, 0, 0);
    key(0, FK_DOWN);
    run_to(500);
    key(0, FK_DOWN_LONG);
    run_to(800);
    key(0, FK_UP);
    CHECK(S.f[0].st == FS_MOVE && S.f[0].move == FM_SPECIAL);
}

// 小招出完 1 秒内:▼ ▲ 只举盾、▲ 不出(连招里也一样)
static void test_special_cool(void) {
    start(FC_KID, FC_DAD, 0, 0);
    key(0, FK_DOWN);
    run_to(10);
    key(0, FK_UP);
    run_to(410);
    CHECK(S.f[0].chain_open);   // 弹弓打中
    run_to(500);
    CHECK(S.f[0].special_cool == FIGHT_SPECIAL_COOL);   // 还在出,冷却没开始减
    key(0, FK_DOWN);
    run_to(550);
    key(0, FK_UP);
    CHECK(S.f[0].st == FS_GUARD && ev_by(FE_START, 0) == 1 && S.f[0].qn == 0);
    run_to(1000);
    CHECK(S.f[0].special_cool == FIGHT_SPECIAL_COOL - 450);
    run_to(1500);
    key(0, FK_DOWN);   // 盾冷却里:不举盾
    run_to(1510);
    key(0, FK_UP);
    CHECK(S.f[0].st == FS_IDLE && ev_by(FE_START, 0) == 1);
    run_to(1600);
    CHECK(S.f[0].special_cool == 0);
    key(0, FK_DOWN);
    run_to(1610);
    key(0, FK_UP);
    CHECK(S.f[0].move == FM_SPECIAL && ev_by(FE_START, 0) == 2);
}

// 太极没人打过来:1 秒后收招 400,这时挨打照常
static void test_stance_whiff(void) {
    start(FC_GRANDPA, FC_GRANNY, 0, 0);
    key(0, FK_DOWN);
    run_to(50);
    key(0, FK_UP);
    run_to(1040);
    CHECK(S.f[0].st == FS_STANCE && ev(FE_WHIFF) == 0);
    run_to(1050);
    CHECK(ev_by(FE_WHIFF, 0) == 1 && S.f[0].st == FS_MOVE && S.f[0].move == FM_SPECIAL);
    run_to(1440);
    CHECK(S.f[0].st == FS_MOVE && S.f[0].special_cool == FIGHT_SPECIAL_COOL);
    run_to(1450);
    CHECK(S.f[0].st == FS_IDLE);

    start(FC_GRANDPA, FC_GRANNY, 0, 0);
    key(0, FK_DOWN);
    run_to(50);
    key(0, FK_UP);
    run_to(1100);
    key(1, FK_OK);
    run_to(1300);
    CHECK(ev_by(FE_HIT, 1) == 1 && ev(FE_COUNTER) == 0);
    CHECK(S.f[0].st == FS_HITSTUN && S.f[0].hp == hp_max(0) - 40);
}

// 晕着、特写时两边都不涨气;大招那一下也不涨
static void test_no_gauge_in_dizzy(void) {
    uint32_t d = super_dizzy(1000);
    CHECK(S.f[0].gauge == FIGHT_GAUGE_HIT && S.f[1].gauge == FIGHT_GAUGE_HURT);
    key(0, FK_OK);
    run_to(d + 50);
    key(0, FK_OK);
    run_to(d + 200);
    CHECK(ev_by(FE_WAKE, 1) == 1);
    CHECK(S.f[0].gauge == FIGHT_GAUGE_HIT && S.f[1].gauge == FIGHT_GAUGE_HURT);
    run_to(d + 400);   // 第二拳:对方已经醒了,照常涨(他落后,多涨)
    CHECK(S.f[0].gauge == 2 * FIGHT_GAUGE_HIT);
    CHECK(S.f[1].gauge == 2 * FIGHT_GAUGE_HURT + FIGHT_GAUGE_BEHIND);
}

// 晚到的连招键(窗口里按的):对方其实还没缓过来,拉回僵直,他这段时间起的动作作废
static void test_late_combo_pulls_back(void) {
    // 对方 200 ms 前缓过来、举了盾;晚到的 ▼ ● 照样是大招,盾作废
    start(FC_GRANNY, FC_KID, 1000, 0);
    key(0, FK_OK);
    run_to(800);
    key(1, FK_DOWN);
    CHECK(S.f[1].st == FS_GUARD);
    run_to(990);
    fight_sim_key_at(&S, 0, FK_DOWN, 320);   // 本来 670
    fight_sim_key_at(&S, 0, FK_OK, 290);     // 本来 700
    CHECK(ev_by(FE_SUPER, 0) == 1 && S.f[1].st == FS_SUPER_IN && S.f[1].guard_cool == 0);
    while (S.freeze) fight_sim_step(&S);
    CHECK(S.f[1].hp == hp_max(1) - 40 - 330 && S.f[1].st == FS_DIZZY);

    // 对方缓过来后出了一拳(还没打到):晚到的 ● 连上,他那一拳作废
    start(FC_GRANNY, FC_KID, 0, 0);
    key(0, FK_OK);
    run_to(850);
    key(1, FK_OK);   // 库巴轻拳 1000 打到
    run_to(990);
    fight_sim_key_at(&S, 0, FK_OK, 290);
    CHECK(S.f[0].move == FM_JAB2 && S.f[1].st == FS_HITSTUN);
    run_to(1190);
    CHECK(S.f[1].hp == hp_max(1) - 80 && S.f[0].chain == 2);
    run_to(3000);
    CHECK(S.f[0].hp == hp_max(0));
}

// 联机:晚到的 ▼(本来在窗口里按的)排着等后面同样晚到的 ●,按真时钟多等它晚到的那么久:还是大招
static void test_late_down_waits(void) {
    start(FC_GRANNY, FC_KID, 1000, 0);
    key(0, FK_OK);                            // 200 打中,窗口到 800
    run_to(700);
    fight_sim_key_at(&S, 0, FK_DOWN, 400);    // 本来 300
    CHECK(S.f[0].st == FS_IDLE && S.f[0].qn == 1);
    run_to(1000);                             // 窗口、本来的 300 + 350 都过了,还在等
    CHECK(S.f[0].st == FS_IDLE && S.f[0].qn == 1 && S.f[0].chain_until == 800);
    fight_sim_key_at(&S, 0, FK_OK, 400);      // 本来 600
    CHECK(ev_by(FE_SUPER, 0) == 1 && S.f[1].st == FS_SUPER_IN);
    while (S.freeze) fight_sim_step(&S);
    CHECK(S.f[1].hp == hp_max(1) - 40 - 330 && S.f[1].st == FS_DIZZY);

    // 晚到的 ▼ 后面没有键:到了真时钟的期限(本来的 300 + 350 + FIGHT_LATE_MAX)就举盾
    start(FC_GRANNY, FC_KID, 1000, 0);
    key(0, FK_OK);
    run_to(700);
    fight_sim_key_at(&S, 0, FK_DOWN, 400);
    run_to(1050);
    CHECK(S.f[0].st == FS_IDLE && S.f[0].qn == 1);
    run_to(1060);
    CHECK(S.f[0].st == FS_GUARD && S.f[0].qn == 0 && ev(FE_SUPER) == 0);

    // 只晚一点的 ▼ 也多等 FIGHT_LATE_MAX(排在它后面的键可能晚得多):▼ 本来 650、晚 50;● 本来 790、晚 310
    start(FC_GRANNY, FC_KID, 1000, 0);
    key(0, FK_OK);
    run_to(700);
    fight_sim_key_at(&S, 0, FK_DOWN, 50);
    run_to(1100);
    CHECK(S.f[0].st == FS_IDLE && S.f[0].qn == 1);
    fight_sim_key_at(&S, 0, FK_OK, 310);
    CHECK(ev_by(FE_SUPER, 0) == 1);

    // 本地(不晚)的 ▼ 照旧:窗口一关就举盾
    start(FC_GRANNY, FC_KID, 1000, 0);
    key(0, FK_OK);
    run_to(700);
    key(0, FK_DOWN);
    run_to(790);
    CHECK(S.f[0].qn == 1);
    run_to(800);
    CHECK(S.f[0].st == FS_GUARD && S.f[0].qn == 0);
}

// 联机:起手本来是在对方保护里按的、晚到时保护已经没了:这一套照样不能有大招
static void test_late_opener_protect(void) {
    super_dizzy(2000);
    uint32_t w = wake_natural();
    int hp = S.f[1].hp;
    run_to(w + 400);
    CHECK(!S.f[1].protect && S.f[1].protect_end == w + 300);
    fight_sim_key_at(&S, 0, FK_OK, 200);      // 本来 w + 200
    run_to(w + 600);
    CHECK(S.f[0].chain == 1 && S.f[1].st == FS_HITSTUN);
    key(0, FK_DOWN);
    run_to(w + 650);
    key(0, FK_OK);
    run_to(w + 3000);
    CHECK(ev(FE_SUPER) == 1 && ev(FE_DIZZY) == 1 && S.f[1].hp == hp - 40 - 40);

    // 对照:本来的时刻在保护消失以后(w + 310):正常起手,能出大招
    super_dizzy(2000);
    w = wake_natural();
    run_to(w + 400);
    fight_sim_key_at(&S, 0, FK_OK, 90);
    run_to(w + 600);
    CHECK(S.f[0].chain == 1);
    key(0, FK_DOWN);
    run_to(w + 650);
    key(0, FK_OK);
    CHECK(ev_by(FE_SUPER, 0) == 2);
}

int main(void) {
    test_jab_down_ok_super();
    test_super_needs_gauge();
    test_super_on_block_is_guard_jab();
    test_super_kid_timing();
    test_super_late_down();
    test_mash_adult();
    test_mash_kid();
    test_mash_cap();
    test_hit_wakes();
    test_no_super_on_dizzy_combo();
    test_no_super_after_break();
    test_wake_timing_loop();
    test_late_keys_after_wake_hit();
    test_protect_clears();
    test_wake_keys_eaten();
    test_trade_no_super();
    test_special_from_guard();
    test_special_prefix_kid();
    test_special_long_guard();
    test_special_cool();
    test_stance_whiff();
    test_no_gauge_in_dizzy();
    test_late_combo_pulls_back();
    test_late_down_waits();
    test_late_opener_protect();
    printf("test_super: PASS\n");
    return 0;
}
