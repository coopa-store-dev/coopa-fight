// tests/test_sim.c —— 一回合的规则:轻拳、重击、防御、连招、提前按的键、晚到的键、互相打中、气、KO、时间到。
#include <stdio.h>
#include <stdlib.h>

#include "fight_sim.h"

#define CHECK(c) do { if (!(c)) { \
    printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); exit(1); } } while (0)

static fight_sim_t S;
static int EV[FE_KINDS][2];   // 每种事件、每个 who 来了几条(who = FIGHT_DRAW 记在 [0])
static fight_ev_t LAST[FE_KINDS];

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
    for (int k = 0; k < FE_KINDS; k++) EV[k][0] = EV[k][1] = 0;
}
static void run_to(uint32_t clock) { while (S.clock < clock && S.winner == FIGHT_UNDECIDED) fight_sim_step(&S); }
static void key(int who, uint8_t k) { fight_sim_key(&S, (uint8_t)who, k); }
static int hp_max(int who) { return fight_char(S.f[who].ch)->hp; }
static int dmg(uint8_t c, uint8_t m) { return fight_char(c)->mv[m].dmg; }   // 伤害读出招表(数值会调)
static uint32_t su(uint8_t c, uint8_t m) { return fight_char(c)->mv[m].startup; }   // 出手也读出招表

// 奶奶的轻拳 200 ms 打到,扣轻拳的血,对方僵 600
static void test_jab_hits_idle(void) {
    start(FC_GRANNY, FC_KID, 0, 0);
    key(0, FK_OK);
    CHECK(S.f[0].st == FS_MOVE && S.f[0].move == FM_JAB);
    run_to(190);
    CHECK(S.f[1].hp == hp_max(1) && S.f[1].st == FS_IDLE);
    run_to(200);
    CHECK(S.f[1].hp == hp_max(1) - dmg(FC_GRANNY, FM_JAB));
    CHECK(S.f[1].st == FS_HITSTUN && S.f[1].dur == 600);
    CHECK(S.f[0].chain == 1 && S.f[0].chain_open);
    CHECK(S.f[0].gauge == FIGHT_GAUGE_HIT && S.f[1].gauge == FIGHT_GAUGE_HURT);
    CHECK(ev_by(FE_START, 0) == 1 && ev_by(FE_HIT, 0) == 1 && LAST[FE_HIT].move == FM_JAB);
    run_to(790);
    CHECK(S.f[1].st == FS_HITSTUN && S.f[0].chain_open);
    run_to(800);
    CHECK(S.f[1].st == FS_IDLE && !S.f[0].chain_open);
    CHECK(S.f[0].st == FS_IDLE);
}

// 盾举起来过了 FIGHT_PARRY_MS 才打到:挡住(不是弹开)
static void test_guard_blocks_jab(void) {
    start(FC_GRANNY, FC_KID, 0, 0);
    key(1, FK_DOWN);
    CHECK(S.f[1].st == FS_GUARD && S.f[1].dur == fight_char(FC_KID)->guard_ms);
    run_to(100);
    key(0, FK_OK);
    run_to(300);
    CHECK(S.f[1].hp == hp_max(1));
    CHECK(S.f[0].st == FS_BLOCKSTUN && S.f[0].dur == 100 + 250 + FIGHT_BLOCKSTUN);
    CHECK(S.f[0].gauge == FIGHT_GAUGE_BLOCK && S.f[1].gauge == FIGHT_GAUGE_BLOCK);
    CHECK(S.f[0].chain == 0 && !S.f[0].chain_open);
    CHECK(ev_by(FE_BLOCK, 0) == 1 && ev(FE_HIT) == 0 && ev(FE_PARRY) == 0);
    run_to(300 + 100 + 250 + FIGHT_BLOCKSTUN - 10);
    CHECK(S.f[0].st == FS_BLOCKSTUN);
    run_to(300 + 100 + 250 + FIGHT_BLOCKSTUN);
    CHECK(S.f[0].st == FS_IDLE);
}

// 盾刚举起来(FIGHT_PARRY_MS 内)打到:弹开。出拳的人僵住,弹开的人马上能动、盾不进冷却、白拿气,下一拳接得上
static void test_parry_jab(void) {
    start(FC_GRANNY, FC_KID, 0, 0);
    key(0, FK_OK);
    run_to(200 - FIGHT_PARRY_MS + 10);
    key(1, FK_DOWN);   // 拳 200 打到:盾举了 FIGHT_PARRY_MS − 10
    run_to(200);
    CHECK(ev_by(FE_PARRY, 1) == 1 && LAST[FE_PARRY].move == FM_JAB && ev(FE_BLOCK) == 0 && ev(FE_HIT) == 0);
    CHECK(S.f[1].hp == hp_max(1) && S.f[1].st == FS_IDLE && S.f[1].guard_cool == 0);
    CHECK(S.f[1].gauge == FIGHT_GAUGE_PARRY && S.f[0].gauge == 0);
    CHECK(S.f[0].st == FS_HITSTUN && S.f[0].dur == FIGHT_PARRY_STUN && S.f[0].qn == 0);
    CHECK(S.f[1].chain == 1 && S.f[1].chain_open);
    key(1, FK_OK);   // 反打:连招的第二拳,打得中僵着的人
    CHECK(S.f[1].move == FM_JAB2);
    run_to(200 + su(FC_KID, FM_JAB2));
    CHECK(S.f[0].hp == hp_max(0) - dmg(FC_KID, FM_JAB2) && S.f[1].chain == 2);
}

// 盾举了正好 FIGHT_PARRY_MS 才打到:只是挡住
static void test_parry_window_edge(void) {
    start(FC_GRANNY, FC_KID, 0, 0);
    key(1, FK_DOWN);
    run_to(FIGHT_PARRY_MS - 200);
    key(0, FK_OK);   // 200 后打到:盾那时正好举了 FIGHT_PARRY_MS
    run_to(FIGHT_PARRY_MS);
    CHECK(ev(FE_PARRY) == 0 && ev_by(FE_BLOCK, 0) == 1 && S.f[0].st == FS_BLOCKSTUN);
}

// 重击也弹得开(不砸开);弹开以后 ▼ ● 能出大招
static void test_parry_heavy_then_super(void) {
    start(FC_KID, FC_DAD, 0, FIGHT_GAUGE_BAR);
    key(0, FK_UP);
    run_to(su(FC_KID, FM_HEAVY) - 100);
    key(1, FK_DOWN);
    run_to(su(FC_KID, FM_HEAVY));
    CHECK(ev_by(FE_PARRY, 1) == 1 && ev(FE_GUARD_BREAK) == 0 && S.f[1].hp == hp_max(1));
    CHECK(S.f[0].st == FS_HITSTUN);
    key(1, FK_DOWN);
    key(1, FK_OK);
    CHECK(S.f[1].st == FS_SUPER_OUT && ev_by(FE_SUPER, 1) == 1);
}

// 晚到的 ▼(联机补算)按本来按下的时刻算盾举了多久:本来举了 FIGHT_PARRY_MS 以上就只是挡住
static void test_parry_late_down(void) {
    start(FC_GRANNY, FC_KID, 0, 0);
    run_to(100);
    key(0, FK_OK);   // 300 打到
    run_to(250);
    fight_sim_key_at(&S, 1, FK_DOWN, 150);   // 本来 100 就按了:打到时盾举了 200,只是挡住
    CHECK(S.f[1].st == FS_GUARD && S.f[1].t == 150);
    run_to(300);
    CHECK(ev(FE_PARRY) == 0 && ev_by(FE_BLOCK, 0) == 1);
    // 对照:同一时刻不晚到的 ▼ 是弹开
    start(FC_GRANNY, FC_KID, 0, 0);
    run_to(100);
    key(0, FK_OK);
    run_to(250);
    key(1, FK_DOWN);
    run_to(300);
    CHECK(ev_by(FE_PARRY, 1) == 1);
}

// 奶奶锅盖的第二下不算弹开(照常挡住 / 打中)
static void test_parry_not_second_hit(void) {
    start(FC_GRANNY, FC_KID, 0, 0);
    key(0, FK_DOWN);
    key(0, FK_UP);   // 锅盖:250 打到、900 飞回来
    run_to(400);
    CHECK(S.f[1].hp < hp_max(1));
    run_to(860);   // 第一下的僵直 850 完
    CHECK(S.f[1].st == FS_IDLE);
    key(1, FK_DOWN);
    run_to(900);
    CHECK(ev(FE_PARRY) == 0 && ev_by(FE_BLOCK, 0) == 1);
}

static void test_heavy_breaks_guard(void) {
    start(FC_KID, FC_DAD, 0, 0);
    key(0, FK_UP);
    CHECK(S.f[0].move == FM_HEAVY);
    run_to(200);
    key(1, FK_DOWN);   // 爸爸的盾举到 800
    run_to(590);
    CHECK(S.f[1].st == FS_GUARD && S.f[1].hp == hp_max(1));
    run_to(600);
    CHECK(ev_by(FE_GUARD_BREAK, 0) == 1 && ev(FE_BLOCK) == 0);
    CHECK(S.f[1].hp == hp_max(1) - dmg(FC_KID, FM_HEAVY));
    CHECK(S.f[1].st == FS_DIZZY && S.f[1].mash_left == FIGHT_DIZZY_BREAK && S.f[1].protect);   // 砸开防御:晕
    CHECK(S.f[1].guard_cool == FIGHT_GUARD_COOL);
    CHECK(S.f[0].chain == 0);   // 重击不开连招
}

// 重击抡到 200 ms 被轻拳打中:打断,重击作废
static void test_jab_interrupts_heavy_early(void) {
    start(FC_GRANNY, FC_KID, 0, 0);
    key(1, FK_UP);
    key(0, FK_OK);
    run_to(200);
    CHECK(S.f[1].st == FS_HITSTUN && S.f[1].hp == hp_max(1) - dmg(FC_GRANNY, FM_JAB));
    run_to(1500);
    CHECK(S.f[0].hp == hp_max(0));
    CHECK(ev(FE_HIT) == 1 && ev(FE_GUARD_BREAK) == 0);
}

// 重击抡到 400 ms(过了 armor_from)被轻拳打中:扣血,不打断,重击照样打到
static void test_heavy_armor_late(void) {
    start(FC_GRANNY, FC_KID, 0, 0);
    key(1, FK_UP);
    run_to(200);
    key(0, FK_OK);
    run_to(400);
    CHECK(S.f[1].hp == hp_max(1) - dmg(FC_GRANNY, FM_JAB));
    CHECK(S.f[1].st == FS_MOVE && S.f[1].move == FM_HEAVY);
    CHECK(S.f[0].chain == 0 && !S.f[0].chain_open);
    run_to(600);
    CHECK(S.f[0].hp == hp_max(0) - dmg(FC_KID, FM_HEAVY));
    CHECK(S.f[0].st == FS_HITSTUN && S.f[0].dur == 500);
    CHECK(ev_by(FE_HIT, 0) == 1 && ev_by(FE_HIT, 1) == 1);
    CHECK(S.f[0].gauge == FIGHT_GAUGE_HIT + FIGHT_GAUGE_HURT);
    CHECK(S.f[1].gauge == FIGHT_GAUGE_HIT + FIGHT_GAUGE_HURT);
}

// 重击后半段只扛得住轻拳:重击、小招打到照样打断
static void test_heavy_armor_jabs_only(void) {
    // 爸爸重击 550 打到,库巴的重击抡到 550(过了 armor_from 300、还没到 600)也被打断
    start(FC_KID, FC_DAD, 0, 0);
    key(0, FK_UP);
    key(1, FK_UP);
    run_to(550);
    CHECK(S.f[0].st == FS_HITSTUN && S.f[0].hp == hp_max(0) - dmg(FC_DAD, FM_HEAVY));
    run_to(2000);
    CHECK(S.f[1].hp == hp_max(1) && ev_by(FE_HIT, 0) == 0);

    // 爸爸肩撞(▼ ▲)360 打到,库巴的重击抡到 360 也被打断
    start(FC_KID, FC_DAD, 0, 0);
    key(0, FK_UP);
    key(1, FK_DOWN);
    run_to(10);
    key(1, FK_UP);
    CHECK(S.f[1].move == FM_SPECIAL);
    run_to(360);
    CHECK(S.f[0].st == FS_HITSTUN && S.f[0].hp == hp_max(0) - dmg(FC_DAD, FM_SPECIAL));
    run_to(2000);
    CHECK(S.f[1].hp == hp_max(1));
}

// 提前按 ● ▲:轻拳打中后都连上,对方一直僵着,伤害 轻拳 + 第二拳 + 连段重击
static void test_chain_jab_jab_heavy(void) {
    start(FC_GRANNY, FC_KID, 0, 0);
    key(0, FK_OK);
    run_to(50);
    key(0, FK_OK);
    run_to(100);
    key(0, FK_UP);
    CHECK(S.f[0].qn == 2 && S.f[0].move == FM_JAB);
    run_to(200);
    CHECK(S.f[0].st == FS_MOVE && S.f[0].move == FM_JAB2 && S.f[0].qn == 1);
    while (S.clock < 550) {
        CHECK(S.f[1].st == FS_HITSTUN);
        fight_sim_step(&S);
    }
    int jjh = dmg(FC_GRANNY, FM_JAB) + dmg(FC_GRANNY, FM_JAB2) + dmg(FC_GRANNY, FM_HEAVY_LINK);
    CHECK(S.f[0].move == FM_HEAVY_LINK);
    CHECK(S.f[1].hp == hp_max(1) - jjh);
    CHECK(S.f[0].chain == 3 && !S.f[0].chain_open);
    run_to(3000);
    CHECK(S.f[1].hp == hp_max(1) - jjh);
    CHECK(ev_by(FE_HIT, 0) == 3 && ev_by(FE_START, 0) == 3);
    CHECK(S.f[0].st == FS_IDLE && S.f[1].st == FS_IDLE && S.f[0].qn == 0);

    // 窗口最后才按 ●:对方照样僵到这一拳打到,僵着按的 ▼ 不算
    start(FC_GRANNY, FC_KID, 0, 0);
    key(0, FK_OK);
    run_to(790);
    key(0, FK_OK);
    CHECK(S.f[0].move == FM_JAB2);
    run_to(800);
    key(1, FK_DOWN);
    CHECK(S.f[1].st == FS_HITSTUN);
    run_to(990);
    CHECK(S.f[1].hp == hp_max(1) - dmg(FC_GRANNY, FM_JAB) - dmg(FC_GRANNY, FM_JAB2) && ev(FE_BLOCK) == 0);
}

// ● 被挡:提前按的 ▼ ● 收招完才变成举盾 + 一拳
static void test_buffered_on_block(void) {
    start(FC_GRANNY, FC_KID, 0, 0);
    key(1, FK_DOWN);
    key(0, FK_OK);
    run_to(60);
    key(0, FK_DOWN);
    run_to(120);
    key(0, FK_OK);
    run_to(200);
    CHECK(S.f[0].st == FS_BLOCKSTUN && S.f[0].qn == 2);
    uint32_t free_at = 200 + 100 + 250 + FIGHT_BLOCKSTUN;   // 被挡:active + recovery + FIGHT_BLOCKSTUN
    run_to(free_at - 10);
    CHECK(S.f[0].st == FS_BLOCKSTUN && S.f[0].guard_cool == 0);
    run_to(free_at);
    CHECK(S.f[0].st == FS_MOVE && S.f[0].move == FM_JAB && S.f[0].qn == 0);
    CHECK(S.f[0].guard_cool == FIGHT_GUARD_COOL);   // 盾举起来又被这一拳放下了
    CHECK(ev_by(FE_START, 0) == 2);
    run_to(free_at + 200);
    CHECK(S.f[1].hp == hp_max(1) - dmg(FC_GRANNY, FM_JAB));   // 对方的盾早放下了

    // 挡住的一方在收招完之前反打:打中,提前按的键作废
    start(FC_GRANNY, FC_KID, 0, 0);
    key(1, FK_DOWN);
    key(0, FK_OK);
    run_to(60);
    key(0, FK_DOWN);
    run_to(120);
    key(0, FK_OK);
    run_to(400);
    key(1, FK_OK);   // 放下盾出拳,400 + 库巴轻拳出手 打到
    run_to(400 + su(FC_KID, FM_JAB));
    CHECK(S.f[0].hp == hp_max(0) - dmg(FC_KID, FM_JAB) && S.f[0].st == FS_HITSTUN && S.f[0].qn == 0);
    run_to(2000);
    CHECK(ev_by(FE_START, 0) == 1);
}

static void test_guard_then_jab(void) {
    start(FC_KID, FC_DAD, 0, 0);
    key(0, FK_DOWN);
    run_to(100);
    key(0, FK_OK);
    CHECK(S.f[0].st == FS_MOVE && S.f[0].move == FM_JAB);
    CHECK(S.f[0].guard_cool == FIGHT_GUARD_COOL && !S.f[0].long_guard);
    run_to(100 + su(FC_KID, FM_JAB));
    CHECK(S.f[1].hp == hp_max(1) - dmg(FC_KID, FM_JAB));
}

// 冷却里按 ▼ 不举盾,但时刻照记(0.35 秒内的 ▲ 仍是小招)
static void test_down_in_cool_prefix_kept(void) {
    start(FC_KID, FC_DAD, 0, 0);
    CHECK(S.f[0].down_at == UINT32_MAX);
    key(0, FK_DOWN);
    CHECK(S.f[0].down_at == 0);
    run_to(600);
    CHECK(S.f[0].st == FS_IDLE && S.f[0].guard_cool == FIGHT_GUARD_COOL);
    run_to(700);
    key(0, FK_DOWN);
    CHECK(S.f[0].st == FS_IDLE && S.f[0].down_at == 700);
    run_to(800);
    key(0, FK_UP);
    CHECK(S.f[0].st == FS_MOVE && S.f[0].move == FM_SPECIAL);
}

// 晚到的键按本来按下的时刻判连招窗口
static void test_late_key_window(void) {
    start(FC_GRANNY, FC_KID, 0, 0);
    key(0, FK_OK);
    run_to(200);
    CHECK(S.f[0].chain_until == 800);
    run_to(1000);
    CHECK(S.f[0].st == FS_IDLE && S.f[1].st == FS_IDLE && !S.f[0].chain_open);
    fight_sim_key_at(&S, 0, FK_OK, 300);   // 本来 700 按的:对方还僵着
    CHECK(S.f[0].move == FM_JAB2);
    run_to(1200);
    CHECK(S.f[1].hp == hp_max(1) - dmg(FC_GRANNY, FM_JAB) - dmg(FC_GRANNY, FM_JAB2) && S.f[0].chain == 2);

    // 同样的时刻按、不晚:窗口早关了,是一记新的轻拳
    start(FC_GRANNY, FC_KID, 0, 0);
    key(0, FK_OK);
    run_to(1000);
    key(0, FK_OK);
    CHECK(S.f[0].move == FM_JAB);
    run_to(1200);
    CHECK(S.f[0].chain == 1);

    // 晚得不够:本来 850 按的,窗口 800 就关了
    start(FC_GRANNY, FC_KID, 0, 0);
    key(0, FK_OK);
    run_to(1000);
    fight_sim_key_at(&S, 0, FK_OK, 150);
    CHECK(S.f[0].move == FM_JAB);

    // 晚的比回合过去的还多:按 0 算;超过 FIGHT_LATE_MAX 的按 FIGHT_LATE_MAX 算
    start(FC_KID, FC_DAD, 0, 0);
    run_to(100);
    fight_sim_key_at(&S, 0, FK_DOWN, 300);
    CHECK(S.f[0].down_at == 0);
    run_to(1000);
    fight_sim_key_at(&S, 1, FK_DOWN, 1000);
    CHECK(S.f[1].down_at == 1000 - FIGHT_LATE_MAX);
}

static void test_guard_repress_ignored(void) {
    start(FC_KID, FC_DAD, 0, 0);
    key(0, FK_DOWN);
    run_to(300);
    key(0, FK_DOWN);
    CHECK(S.f[0].st == FS_GUARD && S.f[0].t == 300 && S.f[0].dur == 600);
    CHECK(S.f[0].down_at == 300);
    run_to(600);
    CHECK(S.f[0].st == FS_IDLE);
}

static void test_guard_long(void) {
    start(FC_KID, FC_DAD, 0, 0);
    key(0, FK_DOWN_LONG);   // 没举盾:长按不算
    CHECK(S.f[0].st == FS_IDLE && !S.f[0].long_guard);
    key(0, FK_DOWN);
    run_to(500);
    key(0, FK_DOWN_LONG);
    CHECK(S.f[0].st == FS_GUARD && S.f[0].long_guard);
    CHECK(S.f[0].dur == fight_char(FC_KID)->guard_long_ms);
    CHECK(S.f[0].down_at == 0);   // 长按不是新的 ▼
    run_to(1190);
    CHECK(S.f[0].st == FS_GUARD);
    run_to(1200);
    CHECK(S.f[0].st == FS_IDLE && !S.f[0].long_guard && S.f[0].guard_cool == FIGHT_GUARD_COOL);
}

static void test_guard_cool(void) {
    start(FC_KID, FC_DAD, 0, 0);
    key(0, FK_DOWN);
    run_to(600);
    CHECK(S.f[0].st == FS_IDLE && S.f[0].guard_cool == FIGHT_GUARD_COOL);
    run_to(700);
    key(0, FK_DOWN);
    CHECK(S.f[0].st == FS_IDLE);
    run_to(990);
    key(0, FK_DOWN);
    CHECK(S.f[0].st == FS_IDLE && S.f[0].guard_cool == 10);
    run_to(1000);
    key(0, FK_DOWN);
    CHECK(S.f[0].st == FS_GUARD);
}

// 同一步两边都打到:各吃一下,都僵住,连招和提前按的键都作废
static void test_trade(void) {
    start(FC_GRANNY, FC_GRANNY, 0, 0);
    key(0, FK_OK);
    key(1, FK_OK);
    run_to(50);
    key(0, FK_OK);
    key(1, FK_UP);
    run_to(200);
    CHECK(ev(FE_TRADE) == 1 && ev(FE_HIT) == 0);
    for (int i = 0; i < 2; i++) {
        CHECK(S.f[i].hp == hp_max(i) - dmg(FC_GRANNY, FM_JAB));
        CHECK(S.f[i].st == FS_HITSTUN && S.f[i].dur == 600);
        CHECK(S.f[i].qn == 0 && S.f[i].chain == 0 && !S.f[i].chain_open);
        CHECK(S.f[i].gauge == FIGHT_GAUGE_HIT + FIGHT_GAUGE_HURT);
    }
    run_to(3000);
    CHECK(S.f[0].hp == hp_max(0) - dmg(FC_GRANNY, FM_JAB) && S.f[1].hp == hp_max(1) - dmg(FC_GRANNY, FM_JAB));
    CHECK(S.f[0].st == FS_IDLE && S.f[1].st == FS_IDLE && ev(FE_START) == 2);
}

// 叠着按冒出的「幻影」●:连按 ● ▼ ● 再多一个 ●,不崩,最多多一拳
static void test_overlap_extra_jab(void) {
    // 打中、没气:▼ ● 是举盾 + 一拳(新的一拳不是连招,对方还僵着 → 打空),多的 ● 接在它后面(对方缓过来了,打中)
    start(FC_GRANNY, FC_KID, 0, 0);
    key(0, FK_OK);
    run_to(60);
    key(0, FK_DOWN);
    run_to(120);
    key(0, FK_OK);
    run_to(180);
    key(0, FK_OK);
    run_to(3000);
    CHECK(ev_by(FE_START, 0) == 3 && ev_by(FE_HIT, 0) == 2 && ev_by(FE_WHIFF, 0) == 1);
    CHECK(S.f[1].hp == hp_max(1) - 2 * dmg(FC_GRANNY, FM_JAB));
    CHECK(S.f[0].st == FS_IDLE && S.f[0].qn == 0 && S.f[1].st == FS_IDLE);

    // 被挡:收招完举盾、出拳,多的那个 ● 再一拳(这一拳打中,接成连招)
    start(FC_GRANNY, FC_KID, 0, 0);
    key(1, FK_DOWN);
    key(0, FK_OK);
    run_to(60);
    key(0, FK_DOWN);
    run_to(120);
    key(0, FK_OK);
    run_to(180);
    key(0, FK_OK);
    run_to(3000);
    CHECK(ev_by(FE_START, 0) == 3 && ev_by(FE_BLOCK, 0) == 1);
    CHECK(S.f[1].hp >= hp_max(1) - dmg(FC_GRANNY, FM_JAB) - dmg(FC_GRANNY, FM_JAB2));
    CHECK(S.f[0].st == FS_IDLE && S.f[0].qn == 0);

    // 打中、有两格气:● ▼ ● 出一次大招;多的 ● 前面不是 ▼,不会再试一次大招(特写前排着的键作废)
    start(FC_GRANNY, FC_KID, 2000, 0);
    key(0, FK_OK);
    run_to(60);
    key(0, FK_DOWN);
    run_to(120);
    key(0, FK_OK);
    run_to(180);
    key(0, FK_OK);
    run_to(200);
    CHECK(ev_by(FE_SUPER, 0) == 1 && S.f[0].qn == 0);
    run_to(1500);
    CHECK(ev_by(FE_SUPER, 0) == 1 && ev_by(FE_START, 0) == 1);
    CHECK(S.f[0].gauge == 2000 + FIGHT_GAUGE_HIT - FIGHT_GAUGE_BAR);
    CHECK(S.f[1].st == FS_DIZZY && S.f[1].hp == hp_max(1) - dmg(FC_GRANNY, FM_JAB) - dmg(FC_GRANNY, FM_SUPER));
}

// ▼ 只管紧跟在它后面的那个键:▼ ● ● 连按,第二个 ● 离 ▼ 不到 0.35 秒也只是连招第二拳
static void test_prefix_binds_next_key(void) {
    start(FC_GRANNY, FC_KID, 1000, 0);
    key(0, FK_DOWN);
    run_to(100);
    key(0, FK_OK);   // 放下盾出拳,300 打到
    run_to(200);
    key(0, FK_OK);
    run_to(300);
    CHECK(S.f[0].move == FM_JAB2 && ev(FE_SUPER) == 0);
    run_to(3000);
    CHECK(ev(FE_SUPER) == 0 && S.f[1].hp == hp_max(1) - dmg(FC_GRANNY, FM_JAB) - dmg(FC_GRANNY, FM_JAB2) &&
          S.f[0].gauge >= FIGHT_GAUGE_BAR);

    // ▼ ● ▲:▲ 前面是 ●,不是小招(● 被挡,收招完 ▲ 是重击)
    start(FC_KID, FC_DAD, 0, 0);
    key(1, FK_DOWN);
    key(0, FK_DOWN);
    run_to(100);
    key(0, FK_OK);
    run_to(200);
    key(0, FK_UP);
    run_to(100 + su(FC_KID, FM_JAB));   // ● 被挡
    CHECK(S.f[0].st == FS_BLOCKSTUN && S.f[0].qn == 1);
    while (S.f[0].st == FS_BLOCKSTUN) fight_sim_step(&S);
    CHECK(S.f[0].st == FS_MOVE && S.f[0].move == FM_HEAVY);
}

static void test_ko(void) {
    start(FC_GRANNY, FC_KID, 0, 0);
    S.f[1].hp = 10;   // 比一记轻拳少
    key(0, FK_OK);
    run_to(200);
    CHECK(S.f[1].hp == 0 && S.f[1].st == FS_KO && S.winner == 0);
    CHECK(ev_by(FE_KO, 1) == 1 && LAST[FE_KO].move == FM_JAB);
    uint32_t c = S.clock;
    key(0, FK_OK);
    key(1, FK_OK);
    for (int i = 0; i < 100; i++) fight_sim_step(&S);
    CHECK(S.clock == c && ev(FE_START) == 1 && S.f[1].st == FS_KO);

    // 互相打中两个一起倒:平
    start(FC_GRANNY, FC_GRANNY, 0, 0);
    S.f[0].hp = S.f[1].hp = 10;
    key(0, FK_OK);
    key(1, FK_OK);
    run_to(200);
    CHECK(S.winner == FIGHT_DRAW && ev(FE_KO) == 2);
    CHECK(S.f[0].st == FS_KO && S.f[1].st == FS_KO);
}

static void test_timeup(void) {
    // 按血占满血的比例比:500/950 > 520/1000
    start(FC_KID, FC_DAD, 0, 0);
    S.f[0].hp = 500;
    S.f[1].hp = 520;
    run_to(FIGHT_ROUND_MS - FIGHT_STEP);
    CHECK(S.winner == FIGHT_UNDECIDED);
    fight_sim_step(&S);
    CHECK(S.winner == 0 && ev(FE_TIMEUP) == 1 && LAST[FE_TIMEUP].who == 0);
    fight_sim_step(&S);
    CHECK(S.clock == FIGHT_ROUND_MS && ev(FE_TIMEUP) == 1);

    start(FC_KID, FC_DAD, 0, 0);   // 475/950 = 500/1000
    S.f[0].hp = 475;
    S.f[1].hp = 500;
    run_to(FIGHT_ROUND_MS);
    CHECK(S.winner == FIGHT_DRAW && ev(FE_TIMEUP) == 1 && LAST[FE_TIMEUP].who == FIGHT_DRAW);

    start(FC_KID, FC_DAD, 0, 0);
    S.f[0].hp = 495;
    S.f[1].hp = 551;
    run_to(FIGHT_ROUND_MS);
    CHECK(S.winner == 1);
}

static void test_gauge_caps(void) {
    start(FC_GRANNY, FC_KID, 5000, 0);
    CHECK(S.f[0].gauge == FIGHT_GAUGE_MAX);

    start(FC_GRANNY, FC_KID, FIGHT_GAUGE_MAX - FIGHT_GAUGE_HIT + 5, FIGHT_GAUGE_MAX - FIGHT_GAUGE_HURT + 5);   // 涨了就超
    key(0, FK_OK);
    run_to(200);
    CHECK(S.f[0].gauge == FIGHT_GAUGE_MAX && S.f[1].gauge == FIGHT_GAUGE_MAX);

    // 挨打时血比对方少 200 以上(满血 1000 制)多涨
    start(FC_GRANNY, FC_DAD, 0, 0);
    S.f[1].hp = 790;
    key(0, FK_OK);
    run_to(200);
    CHECK(S.f[1].gauge == FIGHT_GAUGE_HURT + FIGHT_GAUGE_BEHIND);
    start(FC_GRANNY, FC_DAD, 0, 0);
    S.f[1].hp = (int16_t)(810 + dmg(FC_GRANNY, FM_JAB));   // 挨完 810,只少 190
    key(0, FK_OK);
    run_to(200);
    CHECK(S.f[1].gauge == FIGHT_GAUGE_HURT);
}

// 事件队列满了:丢最旧的打中,KO 不丢
static void test_events_full(void) {
    start(FC_GRANNY, FC_KID, 0, 0);
    S.f[1].hp = (int16_t)(10 * dmg(FC_GRANNY, FM_JAB) - 5);   // 第 10 拳打倒
    for (int i = 0; i < 10; i++) {
        key(0, FK_OK);
        run_to(S.clock + 1500);
    }
    CHECK(S.winner == 0 && S.evn == FIGHT_EVQ);
    fight_ev_t e = { 0 };
    int hits = 0, starts = 0;
    while (fight_sim_event(&S, &e)) {
        hits += e.kind == FE_HIT;
        starts += e.kind == FE_START;
    }
    CHECK(e.kind == FE_KO && e.who == 1);
    CHECK(starts == 10 && hits == FIGHT_EVQ - 1 - 10);
}

// 防无限连:● 打中一次以后一秒 5 下轮着按 ▼ ●(没气,▼ ● 是举盾 + 新的一拳)。新的一拳不在连招里,
// 打到还僵着的人是打空;挨打的人(每 200 ms 按一下 ▼ / ●,四种相位)第一下挨打后 1 秒内能动。
static void test_no_infinite_lock(void) {
    for (uint8_t c0 = 0; c0 < FC_COUNT; c0++) {
        for (uint32_t ph = 0; ph < 200; ph += 50) {
            start(c0, FC_DAD, 0, 0);
            key(0, FK_OK);
            uint32_t hit_at = 0, n = 0;
            bool acted = false;
            for (uint32_t t = 0; t < 5000 && !acted; t += FIGHT_STEP) {
                if (t >= 200 && t % 200 == 0) key(0, (t / 200) % 2 ? FK_DOWN : FK_OK);
                if (t >= 300 && t % 200 == ph) key(1, (n++ % 2) ? FK_DOWN : FK_OK);   // 第一拳打中以后才开始按
                fight_sim_step(&S);
                uint8_t st = S.f[1].st;
                if (!hit_at && st == FS_HITSTUN) hit_at = S.clock;
                if (hit_at && (st == FS_IDLE || st == FS_GUARD || st == FS_MOVE)) {
                    CHECK(S.clock - hit_at <= 1000);
                    acted = true;
                }
            }
            CHECK(hit_at && acted);
        }
    }
}

// 规则改了以后真的连招照样连上:● ● ● 三连、● ▼ ●(有气)大招、● ▲ 连段重击、库巴弹弓接 ●、爷爷太极接住接 ●
static void test_chains_still_connect(void) {
    for (uint8_t c0 = 0; c0 < FC_COUNT; c0++) {
        start(c0, FC_KID, 0, 0);
        key(0, FK_OK);
        run_to(200);
        key(0, FK_OK);
        run_to(400);
        key(0, FK_OK);
        run_to(3000);
        const fight_char_def_t *d = fight_char(c0);
        CHECK(ev_by(FE_HIT, 0) == 3 && ev(FE_WHIFF) == 0);
        CHECK(S.f[1].hp == hp_max(1) - (d->mv[FM_JAB].dmg + d->mv[FM_JAB2].dmg + d->mv[FM_JAB3].dmg));

        start(c0, FC_KID, FIGHT_GAUGE_BAR, 0);
        key(0, FK_OK);
        run_to(200);
        key(0, FK_DOWN);
        run_to(400);
        key(0, FK_OK);
        CHECK(S.freeze && S.f[0].st == FS_SUPER_OUT);
        run_to(3000);
        CHECK(ev_by(FE_SUPER, 0) == 1 && S.f[1].hp == hp_max(1) - (d->mv[FM_JAB].dmg + d->mv[FM_SUPER].dmg));

        start(c0, FC_KID, 0, 0);   // ● ▲:连段重击
        key(0, FK_OK);
        run_to(200);
        key(0, FK_UP);
        run_to(3000);
        CHECK(ev_by(FE_HIT, 0) == 2 && ev(FE_WHIFF) == 0);
        CHECK(S.f[1].hp == hp_max(1) - (d->mv[FM_JAB].dmg + d->mv[FM_HEAVY_LINK].dmg));
    }

    // 库巴弹弓打中开连招,接 ●(第二拳)
    start(FC_KID, FC_DAD, 0, 0);
    key(0, FK_DOWN);
    run_to(100);
    key(0, FK_UP);
    run_to(100 + su(FC_KID, FM_SPECIAL));
    CHECK(S.f[1].st == FS_HITSTUN && S.f[0].chain_open);
    key(0, FK_OK);
    run_to(3000);
    CHECK(ev_by(FE_HIT, 0) == 2 && ev(FE_WHIFF) == 0);
    CHECK(S.f[1].hp == hp_max(1) - dmg(FC_KID, FM_SPECIAL) - dmg(FC_KID, FM_JAB2));

    // 爷爷太极接住(对方僵着),接 ●(第二拳)
    start(FC_GRANDPA, FC_GRANNY, 0, 0);
    key(0, FK_DOWN);
    run_to(50);
    key(0, FK_UP);
    run_to(100);
    key(1, FK_OK);
    run_to(100 + su(FC_GRANNY, FM_JAB));
    CHECK(ev_by(FE_COUNTER, 0) == 1 && S.f[1].st == FS_HITSTUN && S.f[0].chain_open);
    key(0, FK_OK);
    run_to(3000);
    CHECK(ev_by(FE_HIT, 0) == 1 && ev_by(FE_WHIFF, 0) == 0);
    CHECK(S.f[1].hp == hp_max(1) - dmg(FC_GRANDPA, FM_SPECIAL) - dmg(FC_GRANDPA, FM_JAB2));
}

int main(void) {
    test_jab_hits_idle();
    test_guard_blocks_jab();
    test_parry_jab();
    test_parry_window_edge();
    test_parry_heavy_then_super();
    test_parry_not_second_hit();
    test_parry_late_down();
    test_heavy_breaks_guard();
    test_jab_interrupts_heavy_early();
    test_heavy_armor_late();
    test_heavy_armor_jabs_only();
    test_chain_jab_jab_heavy();
    test_buffered_on_block();
    test_guard_then_jab();
    test_down_in_cool_prefix_kept();
    test_late_key_window();
    test_guard_repress_ignored();
    test_guard_long();
    test_guard_cool();
    test_trade();
    test_overlap_extra_jab();
    test_prefix_binds_next_key();
    test_ko();
    test_timeup();
    test_gauge_caps();
    test_events_full();
    test_no_infinite_lock();
    test_chains_still_connect();
    printf("test_sim: PASS\n");
    return 0;
}
