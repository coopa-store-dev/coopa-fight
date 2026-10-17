// tests/test_local.c —— 和电脑打:两个 fight_game_t 在内存里接起来(fight_local),电脑(fight_bot)看 cpu.sim、
// 按键走 fight_game_key。打完一场两边比分一致;大招特写期间电脑那边也冻结,按的键不算。
#include <stdio.h>
#include <stdlib.h>

#include "fight_bot.h"
#include "fight_local.h"

#define CHECK(c) do { if (!(c)) { \
    printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); exit(1); } } while (0)

static uint32_t s_x = 5;
static uint32_t rnd(void) {
    s_x = s_x * 1664525u + 1013904223u;
    return s_x >> 8;
}

static fight_local_t L;

static void tick(uint32_t dt) {
    fight_game_tick(&L.me, dt);
    fight_game_tick(&L.cpu, dt);
    fight_local_flow(&L);
}

static void drain(void) {
    uint8_t e;
    fight_ev_t se;
    while (fight_game_event(&L.me, &e, &se)) {}
    while (fight_game_event(&L.cpu, &e, &se)) {}
}

static void begin(uint8_t me, uint8_t cpu) {
    fight_local_init(&L, rnd);
    fight_local_flow(&L);
    CHECK(L.me.phase == GP_PICK && L.cpu.phase == GP_PICK && L.me.match == L.cpu.match);
    for (int t = 0; t < FIGHT_PING_QUIET + 10; t += FIGHT_STEP) tick(FIGHT_STEP);   // 选人页上量一次来回(选人页前 0.4 秒不收 ●)
    CHECK(L.me.rtt_ok && L.me.rtt == 0);
    CHECK(fight_game_pick(&L.me, me) && fight_game_pick(&L.cpu, cpu));
    fight_local_flow(&L);
    CHECK(L.me.phase == GP_COUNT && L.cpu.phase == GP_COUNT);
    for (int t = 0; t < FIGHT_COUNT_MS + 20; t += FIGHT_STEP) tick(FIGHT_STEP);
    CHECK(L.me.phase == GP_PLAY && L.cpu.phase == GP_PLAY);
    drain();
}

// 一个人按一个键(fight_game_key),再推 ms
static void press_me(uint8_t key, uint32_t ms) {
    fight_game_key(&L.me, key);
    fight_local_flow(&L);
    for (uint32_t t = 0; t < ms; t += FIGHT_STEP) tick(FIGHT_STEP);
}

// 我(主动方)有气、● ▼ ● 打中电脑:特写期间电脑那边也冻结(画面的时间不走),电脑的脑子照常跑、按的键都不算
static void test_cpu_frozen_by_super(void) {
    begin(FC_DAD, FC_KID);
    L.me.sim.f[0].gauge = FIGHT_GAUGE_BAR;
    press_me(FK_OK, 200);
    press_me(FK_DOWN, 200);
    press_me(FK_OK, 0);
    for (int t = 0; t < 200 && !L.me.sim.freeze; t += FIGHT_STEP) tick(FIGHT_STEP);
    CHECK(L.me.sim.freeze && L.me.sim.f[1].st == FS_SUPER_IN);
    CHECK(L.cpu.sim.freeze && L.cpu.sim.f[1].st == FS_SUPER_IN && L.cpu.sim.f[0].st == FS_SUPER_OUT);
    fight_bot_t bot;
    fight_bot_init(&bot, &fight_bot_hard, rnd);
    uint32_t clock = L.cpu.sim.clock, host_clock = L.me.sim.clock;
    int16_t hp = L.me.sim.f[0].hp;
    int presses = 0;
    while (L.me.sim.freeze > FIGHT_STEP) {
        fight_seen_t v;
        fight_seen(&L.cpu.sim, L.cpu.me, &v);
        CHECK(v.st[0] == FS_SUPER_IN && v.st[1] == FS_SUPER_OUT);   // 电脑看到的是自己挨大招
        int k = fight_bot_step(&bot, &v, FIGHT_STEP);
        if (k >= 0) fight_game_key(&L.cpu, (uint8_t)k), presses++;
        if (L.me.sim.freeze % 200 == 0) fight_game_key(&L.cpu, FK_UP);   // 特写里乱按(一秒 5 下)也不算
        tick(FIGHT_STEP);
        CHECK(L.cpu.sim.clock == clock && L.me.sim.clock == host_clock);
        CHECK(L.me.sim.f[1].st == FS_SUPER_IN && L.me.sim.f[1].qn == 0 && L.me.sim.f[0].hp == hp);
    }
    CHECK(presses > 0);   // 电脑知道要晕,反应过来就开始狂按(特写里的不算)
    for (int t = 0; t < 100; t += FIGHT_STEP) tick(FIGHT_STEP);
    CHECK(L.me.sim.f[1].st == FS_DIZZY && L.cpu.sim.f[1].st == FS_DIZZY);
    CHECK(L.me.sim.f[1].mash_n == 0);   // 特写里按的没算进狂按
}

// 电脑打我放大招也一样:两边都冻结
static void test_me_frozen_by_cpu_super(void) {
    begin(FC_KID, FC_GRANNY);
    L.me.sim.f[1].gauge = FIGHT_GAUGE_BAR;
    fight_game_key(&L.cpu, FK_OK);
    fight_local_flow(&L);
    for (int t = 0; t < 200; t += FIGHT_STEP) tick(FIGHT_STEP);
    fight_game_key(&L.cpu, FK_DOWN);
    fight_local_flow(&L);
    for (int t = 0; t < 200; t += FIGHT_STEP) tick(FIGHT_STEP);
    fight_game_key(&L.cpu, FK_OK);
    fight_local_flow(&L);
    for (int t = 0; t < 200 && !L.me.sim.freeze; t += FIGHT_STEP) tick(FIGHT_STEP);
    CHECK(L.me.sim.freeze && L.me.sim.f[0].st == FS_SUPER_IN && L.cpu.sim.freeze);
    uint32_t clock = L.cpu.sim.clock;
    while (L.me.sim.freeze > FIGHT_STEP) {
        fight_game_key(&L.me, FK_OK);
        tick(FIGHT_STEP);
        CHECK(L.cpu.sim.clock == clock && L.me.sim.f[0].st == FS_SUPER_IN);
    }
}

// 我是「大人」模型、电脑是三档之一:打完一场,两边比分一致,有一边赢够 FIGHT_WIN
static void test_bots_finish(void) {
    const fight_bot_cfg_t *lv[3] = { &fight_bot_easy, &fight_bot_normal, &fight_bot_hard };
    for (int i = 0; i < 3; i++) {
        begin((uint8_t)(i % FC_COUNT), (uint8_t)((i + 1) % FC_COUNT));
        fight_bot_t a, b;
        fight_bot_init(&a, &fight_human_adult, rnd);
        fight_bot_init(&b, lv[i], rnd);
        uint32_t supers = 0;
        for (int t = 0; t < 600000 && !(L.me.phase == GP_OVER && L.cpu.phase == GP_OVER); t += FIGHT_STEP) {
            fight_seen_t v;
            fight_seen(&L.me.sim, L.me.me, &v);
            int k = fight_bot_step(&a, &v, FIGHT_STEP);
            if (k >= 0) fight_game_key(&L.me, (uint8_t)k);
            fight_seen(&L.cpu.sim, L.cpu.me, &v);
            k = fight_bot_step(&b, &v, FIGHT_STEP);
            if (k >= 0) fight_game_key(&L.cpu, (uint8_t)k);
            tick(FIGHT_STEP);
            uint8_t e;
            fight_ev_t se;
            while (fight_game_event(&L.me, &e, &se)) supers += e == GE_SIM && se.kind == FE_SUPER;
            while (fight_game_event(&L.cpu, &e, &se)) {}
        }
        CHECK(L.me.phase == GP_OVER && L.cpu.phase == GP_OVER);
        CHECK(L.me.score_me == L.cpu.score_peer && L.me.score_peer == L.cpu.score_me);
        CHECK(L.me.score_me == FIGHT_WIN || L.me.score_peer == FIGHT_WIN);
        CHECK(L.me.dropped == 0 && L.cpu.dropped == 0);
        printf("  大人 对 %s:%d : %d,大招 %u 次\n", i == 0 ? "简单" : i == 1 ? "普通" : "厉害", L.me.score_me,
               L.me.score_peer, supers);
    }
}

int main(void) {
    test_cpu_frozen_by_super();
    test_me_frozen_by_cpu_super();
    test_bots_finish();
    printf("test_local: PASS\n");
    return 0;
}
