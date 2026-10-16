// tests/test_game.c —— 一场比赛:选人、倒数 → 打 → KO → 下一回合 → 先赢 2、气带到下一回合、再来一场、
// 版本不同、乱消息。管道没有延迟(联机的延迟、卡顿、断线在 test_link.c)。
#include "duo.h"

#include <stdio.h>
#include <stdlib.h>

#define CHECK(c) do { if (!(c)) { \
    printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); exit(1); } } while (0)

static int EV[2][GE_BAD_VER + 1];

static void pump_ev(void) {
    for (int s = 0; s < 2; s++) {
        uint8_t e;
        fight_ev_t se;
        while (fight_game_event(side(s), &e, &se)) EV[s][e]++;
    }
}
static void clear_ev(void) {
    drain_events();
    for (int s = 0; s < 2; s++)
        for (int e = 0; e <= GE_BAD_VER; e++) EV[s][e] = 0;
}

static void start(void) {
    duo_begin();
    run(FIGHT_COUNT_MS + 20);
    CHECK(D.a.phase == GP_PLAY && D.b.phase == GP_PLAY);
}

// loser 只剩 1 点血,另一边出一拳:这一回合 KO
static void ko(int loser) {
    D.a.sim.f[loser].hp = 1;
    press(!loser, FK_OK);
    for (int t = 0; D.a.phase == GP_PLAY; t += 10) {
        CHECK(t < 2000);
        duo_step();
    }
    CHECK(D.a.phase == GP_END && D.b.phase == GP_END);
}

static void test_round_flow(void) {
    duo_begin();
    clear_ev();
    CHECK(D.a.round == 1 && D.b.round == 1 && D.a.match == D.b.match);
    CHECK(D.b.sim.f[0].ch == FC_KID && D.b.sim.f[1].ch == FC_DAD && D.a.sim.f[1].ch == FC_DAD);
    CHECK(D.a.me == 0 && D.b.me == 1);
    run(FIGHT_COUNT_MS - 20);
    CHECK(D.a.phase == GP_COUNT && D.b.phase == GP_COUNT);
    press(0, FK_OK);   // 倒数时按的不算
    press(1, FK_OK);
    run(40);
    pump_ev();
    CHECK(D.a.phase == GP_PLAY && D.b.phase == GP_PLAY && EV[0][GE_FIGHT] == 1 && EV[1][GE_FIGHT] == 1);
    run(500);
    CHECK(D.a.sim.f[0].st == FS_IDLE && D.a.sim.f[1].st == FS_IDLE);
    ko(1);
    pump_ev();
    CHECK(EV[0][GE_ROUND_END] == 1 && EV[1][GE_ROUND_END] == 1);
    CHECK(D.a.score_me == 1 && D.a.score_peer == 0 && D.b.sim.f[1].st == FS_KO && D.b.sim.winner == 0);
    mirror();
    run(FIGHT_NEXT_MS + 20);
    CHECK(D.a.round == 2 && D.b.round == 2 && D.a.phase == GP_COUNT && D.b.phase == GP_COUNT);
    CHECK(D.b.sim.f[1].hp == fight_char(FC_DAD)->hp && D.b.sim.f[1].st == FS_IDLE);
    run(FIGHT_COUNT_MS + 20);
    ko(0);   // 被连方这一拳赢一回合
    mirror();
    CHECK(D.a.score_me == 1 && D.a.score_peer == 1);
    run(FIGHT_NEXT_MS + FIGHT_COUNT_MS + 40);
    CHECK(D.a.round == 3 && D.b.phase == GP_PLAY);
    ko(1);
    run(FIGHT_NEXT_MS + 20);
    pump_ev();
    CHECK(D.a.phase == GP_OVER && D.b.phase == GP_OVER && D.a.score_me == 2 && D.b.score_peer == 2);
    CHECK(EV[0][GE_OVER] == 1 && EV[1][GE_OVER] == 1 && EV[0][GE_ROUND] == 2 && EV[1][GE_ROUND] == 2);   // 第 1 回合的在 clear_ev 之前
    mirror();
}

// 时间到血一样多:平,重打这一回合(不加分)
static void test_draw_replays(void) {
    start();
    run(FIGHT_ROUND_MS + 20);
    CHECK(D.a.phase == GP_END && D.b.phase == GP_END && D.a.sim.winner == FIGHT_DRAW && D.b.sim.winner == FIGHT_DRAW);
    run(FIGHT_NEXT_MS + 20);
    CHECK(D.a.round == 1 && D.b.round == 1 && D.a.score_me == 0 && D.a.score_peer == 0 && D.b.phase == GP_COUNT);
}

static void test_gauge_carries(void) {
    start();
    D.a.sim.f[0].gauge = 1500;
    D.a.sim.f[1].gauge = 700;
    ko(1);
    uint16_t g0 = D.a.sim.f[0].gauge, g1 = D.a.sim.f[1].gauge;
    CHECK(g0 >= 1500 && g1 >= 700);
    CHECK(D.b.sim.f[0].gauge == g0 && D.b.sim.f[1].gauge == g1);   // 最后一条快照
    run(FIGHT_NEXT_MS + 20);
    CHECK(D.a.round == 2 && D.a.sim.f[0].gauge == g0 && D.a.sim.f[1].gauge == g1);
    CHECK(D.b.sim.f[0].gauge == g0 && D.b.sim.f[1].gauge == g1 && D.b.gauge[0] == g0 && D.b.gauge[1] == g1);
    // 这一回合里涨的气,断线作废后不算:重开用回合开始时的
    run(FIGHT_COUNT_MS + 20);
    D.a.sim.f[0].gauge = 2900;
    run(200);
    duo_drop();
    duo_connect();
    flow();
    CHECK(D.a.round == 2 && D.a.sim.f[0].gauge == g0 && D.b.sim.f[0].gauge == g0 && D.b.sim.f[1].gauge == g1);
}

static void test_again(void) {
    start();
    ko(1);
    run(FIGHT_NEXT_MS + FIGHT_COUNT_MS + 40);
    ko(1);
    run(FIGHT_NEXT_MS + 20);
    CHECK(D.a.phase == GP_OVER && D.b.phase == GP_OVER);
    uint32_t old = D.a.match;
    fight_game_again(&D.b);
    flow();
    CHECK(D.a.phase == GP_OVER && D.a.peer_again);   // a 还没按
    fight_game_again(&D.a);
    flow();
    // 两边都按了:回选人页,新的场号,比分、气清零;再选一次才开打
    CHECK(D.a.phase == GP_PICK && D.b.phase == GP_PICK && D.a.match != old && D.a.match == D.b.match);
    CHECK(D.a.score_me == 0 && D.a.score_peer == 0 && D.b.score_me == 0 && D.a.gauge[0] == 0 && D.b.gauge[1] == 0);
    CHECK(!D.a.me_picked && !D.b.me_picked && !D.a.peer_picked);
    duo_pick(FC_GRANNY, FC_GRANDPA);
    CHECK(D.a.round == 1 && D.b.phase == GP_COUNT && D.b.sim.f[0].ch == FC_GRANNY && D.a.sim.f[1].ch == FC_GRANDPA);
    CHECK(D.b.sim.f[0].gauge == 0 && D.b.sim.f[1].gauge == 0);
}

static void test_over_survives_reconnect(void) {
    start();
    ko(1);
    run(FIGHT_NEXT_MS + FIGHT_COUNT_MS + 40);
    ko(1);
    run(FIGHT_NEXT_MS + 20);
    duo_drop();
    fight_game_init(&D.b, duo_send, &D.ba, duo_rnd);   // 被连方连重启都没关系
    duo_connect();
    flow();
    CHECK(D.a.phase == GP_OVER && D.b.phase == GP_OVER && D.b.peer_char == FC_KID && D.b.me_char == FC_DAD);
    mirror();
}

static void test_initiator_restart_new_match(void) {
    start();
    ko(1);
    uint32_t old = D.a.match;
    duo_drop();
    fight_game_init(&D.a, duo_send, &D.ab, duo_rnd);   // 主动方重启:比分没了,开新的一场
    duo_connect();
    flow();
    CHECK(D.a.match != old && D.b.match == D.a.match && D.b.score_me == 0 && D.b.score_peer == 0);
    CHECK(D.a.phase == GP_PICK && D.b.phase == GP_PICK && !D.b.me_picked);
}

// 'N' / 'O' 排满了也不丢:链路堵死、发送队列只剩两个空时回合打完('E'、最后的快照进去了,'O' 进不去),
// 下一回合的 'N' 也进不去;通了以后被连方照样按顺序收到,没丢任何消息
static void test_round_msgs_kept(void) {
    start();
    D.ab.cap = 0;   // 还连着,但一条也发不出去
    while (D.a.out_n < FIGHT_OUTQ - 2) {   // 塞几条无害的 'H'(被连方收到不做事)
        uint8_t i = (uint8_t)((D.a.out_head + D.a.out_n) % FIGHT_OUTQ);
        D.a.out[i][0] = 'H';
        D.a.out[i][1] = FIGHT_PROTO;
        D.a.out_len[i] = 2;
        D.a.out_n++;
    }
    D.a.sim.f[1].hp = 1;
    press(0, FK_OK);
    for (int t = 0; D.a.phase == GP_PLAY; t += 10) {
        CHECK(t < 2000);
        duo_step();
    }
    CHECK(D.a.phase == GP_END && D.b.phase == GP_PLAY);   // 被连方还不知道
    CHECK(D.a.out_n == FIGHT_OUTQ && D.a.keep_n == 1 && D.a.dropped == 0);
    run(FIGHT_NEXT_MS + 20);
    CHECK(D.a.round == 2 && D.a.keep_n == 2 && D.a.dropped == 0 && D.b.phase == GP_PLAY);
    D.ab.cap = 8;
    run(500);
    CHECK(D.b.phase == GP_COUNT && D.b.round == 2 && D.b.score_peer == 1 && D.a.keep_n == 0);
    mirror();
}

static void test_bad_ver(void) {
    duo_init();
    duo_connect();
    uint8_t h[2] = { 'H', FIGHT_PROTO + 1 };
    fight_game_on_msg(&D.b, h, sizeof h);
    uint8_t e = 0, last = 0;
    fight_ev_t se;
    while (fight_game_event(&D.b, &e, &se)) last = e;
    CHECK(D.b.bad_ver && last == GE_BAD_VER);
    fight_game_connected(&D.b, false);
    CHECK(!D.b.bad_ver);
    // 主动方收到不认识的版本:不进选人页
    duo_init();
    D.ab.up = D.ba.up = true;
    fight_game_connected(&D.a, true);
    fight_game_on_msg(&D.a, h, sizeof h);
    CHECK(D.a.bad_ver && D.a.phase == GP_WAIT && !D.a.match);
}

static void check_sane(const fight_game_t *g) {
    CHECK(g->score_me <= FIGHT_WIN && g->score_peer <= FIGHT_WIN && g->phase <= GP_OVER);
    CHECK(g->me_char < FC_COUNT || g->me_char == FIGHT_NOCHAR);
    CHECK(g->peer_char < FC_COUNT || g->peer_char == FIGHT_NOCHAR);
    CHECK(g->gauge[0] <= FIGHT_GAUGE_MAX && g->gauge[1] <= FIGHT_GAUGE_MAX);
    for (int i = 0; i < 2; i++) {
        const fight_fighter_t *f = &g->sim.f[i];
        CHECK(f->ch < FC_COUNT && f->st <= FS_KO && f->move < FM_COUNT);
        CHECK(f->hp >= 0 && f->hp <= (int16_t)fight_char(f->ch)->hp && f->gauge <= FIGHT_GAUGE_MAX);
    }
}

static void test_garbage_ignored(void) {
    static const uint8_t TYPES[] = { 'H', 'S', 'P', 'N', 'O', 'R', 'K', 'M', 'F', 'E', 'Q', 'A', 0, 0xFF };
    uint8_t m[FIGHT_MSG_MAX];
    uint32_t x = 9;
    for (int to = 0; to < 2; to++) {
        start();
        run(300);
        for (size_t t = 0; t < sizeof TYPES; t++) {
            for (size_t len = 0; len <= FIGHT_MSG_MAX; len++) {
                for (int r = 0; r < 30; r++) {
                    for (size_t k = 0; k < sizeof m; k++) {
                        x = x * 1103515245u + 12345u;
                        m[k] = (uint8_t)(x >> 16);
                    }
                    m[0] = TYPES[t];
                    if (r < 10 && len > 5) m[5] = (uint8_t)(r % 2 ? FC_COUNT : 0xFE);   // 越界角色
                    if (r >= 10 && r < 20 && len >= 5) fight_put32(m + 1, side(to)->match);  // 场号对得上
                    fight_game_on_msg(side(to), m, len);
                    check_sane(side(to));
                    duo_step();   // 两边接着跑,不崩
                    check_sane(&D.a);
                    check_sane(&D.b);
                }
            }
        }
        duo_drop();
        duo_connect();
        flow();
        mirror();   // 连回来以主动方为准
        check_sane(&D.b);
    }
}

static void test_pick_flow(void) {
    duo_init();
    duo_connect();
    flow();
    CHECK(D.a.phase == GP_PICK && D.b.phase == GP_PICK && D.a.match && D.a.match == D.b.match);
    CHECK(!fight_game_pick(&D.a, FC_COUNT) && !fight_game_pick(&D.a, FIGHT_NOCHAR));
    CHECK(fight_game_pick(&D.a, FC_GRANNY));
    flow();
    CHECK(D.a.phase == GP_PICK && D.b.peer_picked && D.b.peer_char == FC_GRANNY);   // 只一边选了:不开
    CHECK(fight_game_pick(&D.b, FC_GRANNY));                                       // 可以选同一个人
    flow();
    CHECK(D.a.phase == GP_COUNT && D.b.phase == GP_COUNT);
    CHECK(D.a.me_char == FC_GRANNY && D.a.peer_char == FC_GRANNY && D.b.me_char == FC_GRANNY);
    CHECK(!fight_game_pick(&D.a, FC_KID));   // 开打了不能再选
}

static void test_unpick(void) {
    duo_init();
    duo_connect();
    flow();
    CHECK(!fight_game_unpick(&D.b));
    CHECK(fight_game_pick(&D.b, FC_KID));
    flow();
    CHECK(D.a.peer_picked);
    CHECK(fight_game_unpick(&D.b));
    flow();
    CHECK(!D.a.peer_picked && D.a.peer_char == FIGHT_NOCHAR);
    CHECK(fight_game_pick(&D.a, FC_DAD));
    flow();
    CHECK(D.a.phase == GP_PICK);   // 对方撤回了:不开
    CHECK(fight_game_pick(&D.b, FC_GRANDPA));
    flow();
    CHECK(D.a.phase == GP_COUNT && D.a.peer_char == FC_GRANDPA);
}

static void test_pick_survives_drop(void) {
    duo_init();
    duo_connect();
    flow();
    uint32_t m = D.a.match;
    CHECK(fight_game_pick(&D.a, FC_DAD) && fight_game_pick(&D.b, FC_KID));   // 都按了,但消息没送到
    duo_drop();
    CHECK(D.a.phase == GP_WAIT && D.b.phase == GP_WAIT);
    duo_connect();
    flow();
    // 连回来:同一个场号,两边重发自己的选择,直接开打
    CHECK(D.a.match == m && D.b.match == m && D.a.phase == GP_COUNT && D.b.phase == GP_COUNT);
    CHECK(D.a.peer_char == FC_KID && D.b.peer_char == FC_DAD);
}

static void test_peer_swapped_while_picking(void) {   // 选人页断线,对方重启(或换了张卡):不能沿用它上次的选择
    duo_init();
    duo_connect();
    flow();
    CHECK(fight_game_pick(&D.b, FC_DAD));
    flow();
    CHECK(D.a.peer_picked);
    duo_drop();
    fight_game_init(&D.b, duo_send, &D.ba, duo_rnd);
    duo_connect();
    flow();
    CHECK(D.a.phase == GP_PICK && !D.a.peer_picked && D.a.peer_char == FIGHT_NOCHAR);
    CHECK(fight_game_pick(&D.a, FC_KID));
    flow();
    CHECK(D.a.phase == GP_PICK);   // 对方还没选:不开打
}

static void test_stale_pick_ignored(void) {   // 比赛中、或者场号不对的 'P' 不换人
    start();
    uint8_t m[6] = { 'P' };
    fight_put32(m + 1, D.b.match);
    m[5] = FC_GRANDPA;
    fight_game_on_msg(&D.b, m, sizeof m);
    CHECK(D.b.peer_char == FC_KID && D.b.phase == GP_PLAY);
}

static void test_responder_restart_gets_chars(void) {   // 被连方重启丢了角色:'N' 里带着
    start();
    duo_drop();
    fight_game_init(&D.b, duo_send, &D.ba, duo_rnd);
    duo_connect();
    flow();
    CHECK(D.b.phase == GP_COUNT && D.b.me_char == FC_DAD && D.b.peer_char == FC_KID);
    CHECK(D.b.sim.f[0].ch == FC_KID && D.b.sim.f[1].ch == FC_DAD);
}

int main(void) {
    test_pick_flow();
    test_unpick();
    test_pick_survives_drop();
    test_peer_swapped_while_picking();
    test_stale_pick_ignored();
    test_responder_restart_gets_chars();
    test_round_flow();
    test_draw_replays();
    test_gauge_carries();
    test_again();
    test_over_survives_reconnect();
    test_initiator_restart_new_match();
    test_round_msgs_kept();
    test_bad_ver();
    test_garbage_ignored();
    printf("test_game: PASS\n");
    return 0;
}
