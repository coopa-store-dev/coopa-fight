// tests/test_link.c —— 联机:快照跟得上主动方、被连方的键晚到 / 卡一下 / 排满、断线续上、被连方重启、
// 被连方狂按解晕、被连方的 ● ▼ ● 卡一下照样是大招、两边出大招一样难、快照不堆积;
// 被连方的画面跟上主动方的现在(僵直、特写、晕按时结束,不重出事件)、被连方卡上的小招 / 太极 / 破防 / 接住、两边一起开打。
#include "duo.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(c) do { if (!(c)) { \
    printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); exit(1); } } while (0)

// 链路先设好延迟再连上(量来回从一开始就是真的),选人,打到两边都开打
static void begin_as(uint8_t c0, uint8_t c1, uint32_t delay, uint32_t jitter) {
    duo_init();
    duo_delay(delay, jitter);
    duo_connect();
    for (int t = 0; D.a.phase != GP_PICK || D.b.phase != GP_PICK; t += 10) {
        CHECK(t < 2000);
        duo_step();
    }
    CHECK(fight_game_pick(&D.a, c0) && fight_game_pick(&D.b, c1));
    for (int t = 0; D.a.phase != GP_PLAY || D.b.phase != GP_PLAY; t += 10) {
        CHECK(t < FIGHT_COUNT_MS + 2000);
        duo_step();
    }
}

// 打完一场就两边都按「再来一场」、随便选人,接着打
static void keep_going(void) {
    for (int s = 0; s < 2; s++) {
        fight_game_t *g = side(s);
        if (g->phase == GP_OVER && !g->me_again) fight_game_again(g);
        if (g->phase == GP_PICK && !g->me_picked) fight_game_pick(g, (uint8_t)(test_rnd() % FC_COUNT));
    }
    flow();
}

static const uint8_t KEYS[] = { FK_OK, FK_OK, FK_OK, FK_UP, FK_DOWN, FK_DOWN, FK_DOWN_LONG };

// 两边每 10 ms 各有 pct% 的机会随便按一下
static void random_keys(int pct) {
    for (int s = 0; s < 2; s++)
        if ((int)(test_rnd() % 100) < pct) press(s, KEYS[test_rnd() % sizeof KEYS]);
}

static int count_sim(fight_game_t *g, uint8_t kind, int who) {
    int n = 0;
    uint8_t e;
    fight_ev_t se;
    while (fight_game_event(g, &e, &se)) n += e == GE_SIM && se.kind == kind && (who < 0 || se.who == who);
    return n;
}

// ---- 快照 = 主动方 ----

typedef struct { uint32_t clock; uint8_t st[2]; int16_t hp[2]; uint16_t gauge[2]; } hist_t;
#define HN 64
static hist_t HIST[HN];
static int hist_n;

static void record(void) {
    hist_t *h = &HIST[hist_n++ % HN];
    h->clock = D.a.sim.clock;
    for (int i = 0; i < 2; i++) {
        h->st[i] = D.a.sim.f[i].st;
        h->hp[i] = D.a.sim.f[i].hp;
        h->gauge[i] = D.a.sim.f[i].gauge;
    }
}

// 被连方收到的最近一条快照,和主动方最近 back 步里的某一步一模一样
static bool snap_matches(int back) {
    const fight_snap_t *s = &D.b.snap;
    for (int k = 0; k < back && k < hist_n; k++) {
        const hist_t *h = &HIST[(hist_n - 1 - k) % HN];
        bool eq = h->clock == s->clock;
        for (int i = 0; i < 2; i++)
            eq = eq && h->st[i] == s->f[i].st && h->hp[i] == s->f[i].hp && h->gauge[i] == s->f[i].gauge;
        if (eq) return true;
    }
    return false;
}

static void test_mirror(void) {
    test_seed(11);
    begin_as(FC_KID, FC_DAD, 0, 0);
    int rounds = 0, checked = 0;
    uint8_t last = D.a.phase;
    while (rounds < 50) {
        // 同一个 clock 主动方可能有几个样子:被连方按键送到以后、tick 完(这时打的快照)、收完消息以后:都记下
        random_keys(8);
        if (D.a.phase == GP_PLAY) record();
        D.now += 10;
        fight_game_tick(&D.a, 10);
        if (D.a.phase == GP_PLAY) record();
        fight_game_tick(&D.b, 10);
        flow();
        keep_going();
        if (D.a.phase == GP_PLAY) record();
        else hist_n = 0;
        if (D.a.phase == GP_END && last == GP_PLAY) rounds++;
        last = D.a.phase;
        if (D.a.phase == GP_PLAY && D.b.phase == GP_PLAY && D.b.snap_ok && D.a.round == D.b.round &&
            hist_n >= 3 * (FIGHT_SNAP_MS / 10 + 1)) {
            CHECK(snap_matches(3 * (FIGHT_SNAP_MS / 10 + 1)));
            checked++;
        }
        if (D.a.phase == GP_END && D.b.phase == GP_END) {
            mirror();
            for (int i = 0; i < 2; i++)   // 回合结束的最后一条快照:两边一样
                CHECK(D.b.sim.f[i].hp == D.a.sim.f[i].hp && D.b.sim.f[i].gauge == D.a.sim.f[i].gauge);
        }
    }
    printf("test_mirror: 50 rounds, %d steps matched\n", checked);
    CHECK(checked > 50 * 300);   // 平均每回合至少对了 3 秒
    drain_events();
}

// ---- 被连方自己先画 ----

// 被连方按下去先画成出招 / 举盾的样子;还没算到这个键的快照不把它改回去,算到了以后以快照为准
static void test_predict(void) {
    begin_as(FC_KID, FC_DAD, 70, 0);
    run(500);
    uint32_t f0 = D.ab.msgs['F'];
    press(1, FK_OK);
    CHECK(D.b.sim.f[1].st == FS_MOVE && D.b.sim.f[1].move == FM_JAB && D.b.predict && D.a.sim.f[1].st == FS_IDLE);
    for (int t = 0; D.b.predict; t += 10) {   // 中间来的快照(主动方还没收到这个键)不改回去
        CHECK(t < 500);
        duo_step();
        CHECK(D.b.sim.f[1].st == FS_MOVE && D.b.sim.f[1].move == FM_JAB);
    }
    CHECK(D.ab.msgs['F'] > f0 + 1 && D.a.sim.f[1].st == FS_MOVE);

    // 主动方不收的键:盾刚放下、冷却里又按 ▼。先画成举盾,算过这个键的快照来了就改回去
    run(1500);
    press(1, FK_DOWN);
    const fight_fighter_t *real = &D.a.sim.f[1], *seen = &D.b.sim.f[1];
    for (int t = 0; !(real->st == FS_IDLE && seen->st == FS_IDLE && real->guard_cool > 250); t += 10) {
        CHECK(t < 2000);   // 等到盾放下(两边都看见了)、冷却还剩不少
        duo_step();
    }
    press(1, FK_DOWN);
    CHECK(D.b.sim.f[1].st == FS_GUARD && D.b.predict);
    run(400);
    CHECK(D.a.sim.f[1].st == FS_IDLE && D.b.sim.f[1].st == FS_IDLE && !D.b.predict);
}

// ---- 晚到、卡一下、排满 ----

// 随便打到一场打完(timeout 毫秒内),每步之后 between()
static void play_match(uint32_t timeout, void (*between)(uint32_t t)) {
    uint32_t t = 0;
    while (!(D.a.phase == GP_OVER && D.b.phase == GP_OVER)) {
        CHECK(t < timeout);
        random_keys(8);
        duo_step();
        if (between) between(t);
        drain_events();
        t += 10;
    }
    mirror();
    CHECK(D.a.score_me == FIGHT_WIN || D.a.score_peer == FIGHT_WIN);
}

static void test_late_keys(void) {
    test_seed(21);
    begin_as(FC_GRANNY, FC_GRANDPA, 140, 60);
    play_match(600000, NULL);
}

static void stall_every_second(uint32_t t) {
    if (t % 1000 == 500) duo_stall(test_rnd() & 1 ? &D.ab : &D.ba);
}

static void test_stall_400(void) {
    test_seed(31);
    begin_as(FC_DAD, FC_KID, 70, 20);
    play_match(600000, stall_every_second);
}

static void stall_both(uint32_t t) {
    if (t % 1000 == 0) {
        duo_stall(&D.ab);
        duo_stall(&D.ba);
    }
}

static void test_queue_full(void) {
    // 一帧按 20 下:被连方的键合并成几条 'K',一个都不丢
    test_seed(41);
    begin_as(FC_KID, FC_KID, 70, 0);
    run(500);
    uint32_t before = D.ba.keys;
    for (int i = 0; i < 20; i++) {
        fight_game_key(&D.a, KEYS[i % 3]);
        fight_game_key(&D.b, KEYS[i % 3]);
    }
    flow();
    CHECK(D.a.own_n == 20 && D.b.kq_n <= 20);
    run(3000);
    CHECK(D.ba.keys - before == 20 && D.ba.msgs['K'] >= 3 && D.b.kq_n == 0 && D.a.remote_n == 0);

    // 链路时不时卡一下、只能排 1 条:send 返回 false 就下一帧再发,照样打完、两边一致
    test_seed(42);
    begin_as(FC_GRANNY, FC_DAD, 70, 0);
    D.ab.cap = D.ba.cap = 1;
    for (uint32_t t = 0; t < 20000; t += 10) {
        for (int k = 0; k < 3; k++) random_keys(30);
        duo_step();
        stall_both(t);
        drain_events();
        keep_going();
    }
    CHECK(D.ab.refused > 0 && D.ba.refused > 0);
    play_match(600000, NULL);
}

// ---- 断线、重启 ----

static void test_drop_resume(void) {
    test_seed(51);
    begin_as(FC_KID, FC_DAD, 70, 0);
    D.a.sim.f[1].hp = 1;   // 第一回合主动方赢
    press(0, FK_OK);
    run(FIGHT_NEXT_MS + FIGHT_COUNT_MS + 500);
    CHECK(D.a.round == 2 && D.a.phase == GP_PLAY && D.b.phase == GP_PLAY && D.a.score_me == 1);
    uint16_t g0 = D.a.gauge[0], g1 = D.a.gauge[1];
    for (int i = 0; i < 6; i++) {   // 打到一半:被连方挨了几拳
        press(0, FK_OK);
        run(700);
    }
    int hp_max = fight_char(FC_DAD)->hp;
    CHECK(D.a.sim.f[1].hp < hp_max && D.b.sim.f[1].hp < hp_max);
    duo_drop();
    CHECK(D.a.phase == GP_WAIT && D.b.phase == GP_WAIT);
    run(3000);
    duo_connect();
    run(500);
    // 同一个回合重开:比分不变,血满,气是回合开始时的;被连方挨的那几拳不算
    CHECK(D.a.round == 2 && D.b.round == 2 && D.a.phase == GP_COUNT && D.b.phase == GP_COUNT);
    mirror();
    CHECK(D.a.score_me == 1 && D.a.score_peer == 0);
    CHECK(D.a.sim.f[1].hp == hp_max && D.b.sim.f[1].hp == hp_max && D.b.sim.f[0].hp == fight_char(FC_KID)->hp);
    CHECK(D.a.sim.f[0].gauge == g0 && D.a.sim.f[1].gauge == g1 && D.b.sim.f[0].gauge == g0 && D.b.sim.f[1].gauge == g1);
    run(FIGHT_COUNT_MS + 1000);
    CHECK(D.a.sim.f[1].hp == hp_max && D.b.sim.f[1].hp == hp_max);   // 断线前排着的键也没补上

    // 回合判完、下一回合还没开就断了:连回来开下一回合
    D.a.sim.f[0].hp = 1;
    press(1, FK_OK);
    run(500);
    CHECK(D.a.phase == GP_END && D.a.score_me == 1 && D.a.score_peer == 1);
    duo_drop();
    duo_connect();
    run(500);
    CHECK(D.a.round == 3 && D.b.round == 3 && D.b.phase == GP_COUNT);
    mirror();
}

static void test_peer_restart(void) {
    test_seed(61);
    begin_as(FC_GRANNY, FC_GRANDPA, 70, 10);
    D.a.sim.f[0].hp = 1;   // 被连方赢第一回合
    press(1, FK_OK);
    run(FIGHT_NEXT_MS + FIGHT_COUNT_MS + 800);
    CHECK(D.a.round == 2 && D.a.phase == GP_PLAY && D.a.score_peer == 1);
    D.a.sim.f[0].gauge = 1234;
    duo_drop();
    fight_game_init(&D.b, duo_send, &D.ba, duo_rnd);   // 被连方重启:比分、气、角色全忘了
    duo_connect();
    run(500);
    CHECK(D.b.phase == GP_COUNT && D.b.round == 2 && D.b.score_me == 1 && D.b.score_peer == 0);
    CHECK(D.b.me_char == FC_GRANDPA && D.b.peer_char == FC_GRANNY);
    CHECK(D.b.gauge[0] == D.a.gauge[0] && D.b.gauge[1] == D.a.gauge[1] && D.a.sim.f[0].gauge == D.a.gauge[0]);
    CHECK(D.b.sim.f[0].gauge == D.a.gauge[0] && D.b.sim.f[1].gauge == D.a.gauge[1]);
    test_seed(62);
    play_match(600000, NULL);
}

// ---- 狂按解晕 ----

// attacker 用 ● ▼ ● 放大招,victim 晕了以后自己看见晕了就一秒按 5 下;返回主动方规则里晕了多久
static uint32_t super_then_mash(int attacker) {
    int victim = !attacker;
    begin_as(FC_KID, FC_KID, 70, 0);
    run(2500);   // 量过来回
    D.a.sim.f[attacker].gauge = FIGHT_GAUGE_BAR;
    drain_events();
    press(attacker, FK_OK);
    run(100);
    press(attacker, FK_DOWN);
    run(100);
    press(attacker, FK_OK);
    uint32_t dizzy_at = 0, next = 0;
    for (int t = 0;; t += 10) {
        CHECK(t < 6000);
        duo_step();
        const fight_fighter_t *real = &D.a.sim.f[victim];
        if (!dizzy_at && real->st == FS_DIZZY) dizzy_at = D.now;
        if (dizzy_at && real->st != FS_DIZZY) return D.now - dizzy_at;
        const fight_game_t *v = side(victim);
        const fight_fighter_t *seen = &v->sim.f[v->me];
        bool sees = seen->st == FS_DIZZY || (seen->st == FS_SUPER_IN && !v->sim.freeze);
        if (sees && !next) next = D.now + 100;
        if (next && D.now >= next) {
            press(victim, KEYS[(next / 200) % 3]);
            next += 200;
        }
    }
}

static void test_mash_remote(void) {
    uint32_t remote = super_then_mash(0), local = super_then_mash(1);
    printf("test_mash_remote: responder dizzy %u ms, initiator dizzy %u ms\n", (unsigned)remote, (unsigned)local);
    CHECK(remote < FIGHT_DIZZY_SUPER && local < FIGHT_DIZZY_SUPER);
    int32_t diff = (int32_t)remote - (int32_t)local;
    CHECK(diff < 200 && diff > -200);
}

// ---- 被连方的大招 ----

static void test_remote_super_survives_stall(void) {
    begin_as(FC_KID, FC_GRANNY, 70, 0);
    run(2500);
    D.a.sim.f[1].gauge = FIGHT_GAUGE_BAR;
    drain_events();
    press(1, FK_OK);
    uint32_t first = 0;   // 第一下(一串的开头)到主动方时规则的 clock
    for (int t = 0; t < 150; t += 10) {
        duo_step();
        if (!first && D.ba.keys) first = D.a.sim.clock;
    }
    CHECK(first);
    press(1, FK_DOWN);
    CHECK(D.ba.n >= 1);
    duo_stall(&D.ba);   // ▼ 那一片卡 400 ms
    run(150);
    press(1, FK_OK);
    for (int t = 0; D.ba.keys < 2; t += 10) {
        CHECK(t < 1000);
        duo_step();
    }
    CHECK(D.a.sim.f[1].down_at == first + 150);   // 按本来的时刻判:第一下到的时刻 + 间隔,不是晚到的时刻
    run(2500);
    CHECK(count_sim(&D.a, FE_SUPER, 1) == 1);
    CHECK(D.a.sim.f[0].hp < fight_char(FC_KID)->hp - 300);
}

// attacker 按 ● 、隔 g1 按 ▼、再隔 g2 按 ●;返回大招出没出
static bool super_try(int attacker, uint32_t g1, uint32_t g2) {
    begin_as(FC_GRANNY, FC_GRANNY, 100, 50);
    run(2500);
    D.a.sim.f[attacker].gauge = FIGHT_GAUGE_BAR;
    drain_events();
    press(attacker, FK_OK);
    run(g1);
    press(attacker, FK_DOWN);
    run(g2);
    press(attacker, FK_OK);
    run(2000);
    return count_sim(&D.a, FE_SUPER, attacker) > 0;
}

static void test_remote_super_fair(void) {
    test_seed(71);
    int n = 80, ok[2] = { 0, 0 };
    for (int i = 0; i < n; i++) {
        uint32_t g1 = 120 + test_rnd() % 27 * 10, g2 = 120 + test_rnd() % 27 * 10;   // 120..380
        for (int s = 0; s < 2; s++) ok[s] += super_try(s, g1, g2);
    }
    printf("test_remote_super_fair: initiator %d/%d responder %d/%d\n", ok[0], n, ok[1], n);
    CHECK(ok[0] > n / 2 && ok[0] < n);   // 有成有不成,这个比较才有意义
    CHECK(abs(ok[0] - ok[1]) * 100 <= 10 * n);
}

// ---- 快照不堆积 ----

static void test_snapshot_no_backlog(void) {
    test_seed(81);
    begin_as(FC_DAD, FC_GRANDPA, 70, 0);
    uint32_t worst = 0;
    for (uint32_t t = 0; t < 60000; t += 10) {
        random_keys(8);
        duo_step();
        drain_events();
        keep_going();
        if (D.a.phase == GP_PLAY && D.b.phase == GP_PLAY && D.b.snap_ok && D.a.round == D.b.round &&
            D.a.match == D.b.match) {
            uint32_t lag = D.a.sim.clock - D.b.snap.clock;
            if (lag > worst) worst = lag;
        }
    }
    printf("test_snapshot_no_backlog: worst lag %u ms\n", (unsigned)worst);
    CHECK(worst <= 300);
}

// ---- 回合里链路变慢 ----

// 主动方规则里出了 kind(who),到被连方那边出同一个事件:隔了多久
static uint32_t e_latency(uint8_t kind, int who) {
    int32_t at_a = -1;
    for (uint32_t t = 0; t < 3000; t += 10) {
        duo_step();
        uint8_t e;
        fight_ev_t se;
        while (fight_game_event(&D.a, &e, &se))
            if (e == GE_SIM && se.kind == kind && se.who == who && at_a < 0) at_a = (int32_t)D.now;
        while (fight_game_event(&D.b, &e, &se))
            if (e == GE_SIM && se.kind == kind && se.who == who && at_a >= 0) return D.now - (uint32_t)at_a;
    }
    CHECK(0);
    return 0;
}

// 回合开始时量到的来回是 70 ms 单程的;1 秒后链路变慢(variant 0:单程 110 ms;variant 1:每 2 秒卡 400 ms)。
// 回合里不 ping,只能靠 send 被拒知道拥堵:等它稳下来以后,快照不能越积越旧(variant 0 每一步都算;
// variant 1 卡的那 400 ms 本身就超过界线,按每条快照到的时候算),大招、KO 的 'E' 照样及时到。
static void slow_round(int variant) {
    begin_as(FC_KID, FC_DAD, 70, 0);   // 不按键:这一回合 60 秒内打不完
    run(1000);
    if (variant == 0) duo_delay(110, 0);
    uint32_t worst = 0, worst_arrive = 0;
    // 稳下来要多久:拥堵只能从「链路排满、send 被拒」知道;每 2 秒卡一下时链路一次多排一点,十几秒才排满
    for (uint32_t t = 0; t < 40000; t += 10) {
        if (variant == 1 && t % 2000 == 0) duo_stall(&D.ab);
        uint32_t nf = D.ab.msgs['F'];
        duo_step();
        drain_events();
        CHECK(D.a.phase == GP_PLAY && D.b.phase == GP_PLAY);
        if (t < 30000) continue;   // 稳下来(每 2 秒卡一下时约 25 秒才第一次排满、被拒)
        uint32_t lag = D.a.sim.clock - D.b.snap.clock;
        if (lag > worst) worst = lag;
        if (D.ab.msgs['F'] != nf && lag > worst_arrive) worst_arrive = lag;
    }
    // 大招(避开卡顿那一刻):主动方 ● ▼ ●
    while (variant == 1 && D.now % 2000 != 1000) duo_step();
    D.a.sim.f[0].gauge = FIGHT_GAUGE_BAR;
    drain_events();
    press(0, FK_OK);
    run(100);
    press(0, FK_DOWN);
    run(100);
    press(0, FK_OK);
    uint32_t e_super = e_latency(FE_SUPER, 0);
    const fight_fighter_t *v = &D.a.sim.f[1];
    while (v->st != FS_IDLE || v->protect || v->eat_keys) duo_step();   // 晕完、醒了、保护没了
    while (variant == 1 && D.now % 2000 != 1000) duo_step();
    D.a.sim.f[1].hp = 1;
    drain_events();
    press(0, FK_OK);
    uint32_t e_ko = e_latency(FE_KO, 1);
    printf("test_slowdown %d: worst lag %u ms (on arrival %u), super E %u ms, KO E %u ms, refused %u\n", variant,
           (unsigned)worst, (unsigned)worst_arrive, (unsigned)e_super, (unsigned)e_ko, (unsigned)D.ab.refused);
    CHECK((variant == 0 ? worst : worst_arrive) <= 500 && e_super <= 500 && e_ko <= 500);
}

static void test_slowdown(void) {
    slow_round(0);
    slow_round(1);
}

// 一串的第一下卡了 400 ms:后面的键不能都跟着晚 400 ms(往前挪),卡过以后照链路的延迟生效
static void test_burst_reanchor(void) {
    begin_as(FC_KID, FC_DAD, 70, 0);
    run(1000);
    drain_events();
    uint32_t pressed[12], n = 0, starts = 0, worst = 0;
    press(1, FK_OK);
    pressed[n++] = D.now;
    duo_stall(&D.ba);   // 第一下那一片卡 400 ms
    for (uint32_t t = 10; n < 12 || t < 12 * 600 + 500; t += 10) {
        if (t % 600 == 0 && n < 12) {   // 隔 0.6 秒一下(一串,不断开),每下都是从站着出的一拳
            press(1, FK_OK);
            pressed[n++] = D.now;
        }
        duo_step();
        uint8_t e;
        fight_ev_t se;
        while (fight_game_event(&D.a, &e, &se)) {
            if (e != GE_SIM || se.kind != FE_START || se.who != 1) continue;
            CHECK(starts < n);
            uint32_t lat = D.now - pressed[starts];
            if (starts >= 3 && lat > worst) worst = lat;   // 卡过以后
            starts++;
        }
        drain_events();
    }
    printf("test_burst_reanchor: worst key latency after the stall %u ms\n", (unsigned)worst);
    CHECK(starts == 12 && worst <= 150);
}


// ---- 被连方的画面跟上主动方的现在(坐哪张卡都一样)----

static int EVS[2][FE_TIMEUP + 1][2][FM_COUNT];   // [哪张卡][种类][who][招]:跑的时候收到的 GE_SIM

static void count_events(void) {
    for (int c = 0; c < 2; c++) {
        uint8_t e;
        fight_ev_t se;
        while (fight_game_event(side(c), &e, &se))
            if (e == GE_SIM && se.kind <= FE_TIMEUP && se.who < 2 && se.move < FM_COUNT) EVS[c][se.kind][se.who][se.move]++;
    }
}

static void clear_events(void) {
    drain_events();
    memset(EVS, 0, sizeof EVS);
}

static void run_count(uint32_t ms) {
    for (uint32_t t = 0; t < ms; t += 10) {
        duo_step();
        count_events();
    }
}

// 这张卡上 who 的某种事件一共几条(所有招加起来)
static int evs(int card, uint8_t kind, int who) {
    int n = 0;
    for (int m = 0; m < FM_COUNT; m++) n += EVS[card][kind][who][m];
    return n;
}

// 跑到主动方的 f[who] 和被连方画面上的 f[who] 都进过 st 又离开(st 在 5 秒内开始);返回离开的时刻差(被连方 − 主动方)
static int32_t view_lag(int who, uint8_t st) {
    uint32_t real = 0, seen = 0;
    bool in_r = false, in_s = false;
    for (int t = 0; !real || !seen; t += 10) {
        CHECK(t < 5000);
        if (D.a.sim.f[who].st == st) in_r = true;
        else if (in_r && !real) real = D.now;
        if (D.b.sim.f[who].st == st) in_s = true;
        else if (in_s && !seen) seen = D.now;
        if (real && seen) break;
        duo_step();
        count_events();
    }
    return (int32_t)seen - (int32_t)real;
}

// 被连方自己的挨打僵直、被挡僵直、大招特写、晕(不按,自己醒)在这张卡上结束的时刻和主动方的规则一样(差一步以内),
// 不等下一条快照;放大招的人也同时能动
static void test_view_catches_up(void) {
    test_seed(71);
    begin_as(FC_KID, FC_DAD, 70, 0);
    run(2500);   // 量过来回
    CHECK(D.a.rtt_ok && D.b.ahead >= 60 && D.b.ahead <= 80);
    press(0, FK_OK);
    int32_t lag = view_lag(1, FS_HITSTUN);
    printf("test_view_catches_up: hitstun ends %d ms after the host", (int)lag);
    CHECK(lag >= -20 && lag <= 20);
    run(1000);
    press(0, FK_DOWN);   // 主动方举盾(70 ms 后生效),被连方的拳打在盾上
    run(100);
    press(1, FK_OK);
    lag = view_lag(1, FS_BLOCKSTUN);
    printf(", blockstun %d", (int)lag);
    CHECK(lag >= -20 && lag <= 20);
    run(1500);
    D.a.sim.f[0].gauge = FIGHT_GAUGE_BAR;
    press(0, FK_OK);
    run(100);
    press(0, FK_DOWN);
    run(100);
    press(0, FK_OK);
    lag = view_lag(1, FS_SUPER_IN);
    printf(", super freeze %d", (int)lag);
    CHECK(lag >= -20 && lag <= 20);
    CHECK(D.b.sim.f[1].st == FS_DIZZY && D.b.sim.f[0].st == FS_IDLE);
    lag = view_lag(1, FS_DIZZY);
    printf(", dizzy %d ms\n", (int)lag);
    CHECK(lag >= -20 && lag <= 20);
}

// 这边已经结束掉的出招、被挡僵直,晚到的快照(抖动)不再画回来:被连方卡上的出招、挡住事件和主动方的一样多
static void test_view_no_double_events(void) {
    test_seed(72);
    begin_as(FC_KID, FC_KID, 70, 23);
    run(2500);
    clear_events();
    for (int i = 0; i < 9; i++) {   // 一回合 60 秒里打得完
        press(1, FK_OK);
        run_count(1200);
        press(0, FK_OK);
        run_count(1200);
        press(1, FK_DOWN);
        run_count(100);
        press(1, FK_UP);
        run_count(1600);
        press(0, FK_DOWN);   // 主动方举盾,被连方的拳被挡
        run_count(100);
        press(1, FK_OK);
        run_count(1600);
    }
    for (int who = 0; who < 2; who++)
        for (int m = 0; m < FM_COUNT; m++) CHECK(EVS[1][FE_START][who][m] == EVS[0][FE_START][who][m]);
    CHECK(D.a.round == 1 && EVS[0][FE_START][1][FM_SPECIAL] == 9 && EVS[0][FE_START][1][FM_JAB] == 18);
    CHECK(evs(1, FE_BLOCK, 1) == evs(0, FE_BLOCK, 1) && evs(0, FE_BLOCK, 1) == 9);
    CHECK(evs(1, FE_WAKE, 0) == 0 && evs(1, FE_WAKE, 1) == 0);
}

// ---- 被连方卡上的小招、太极、破防、接住 ----

static void test_passive_feedback(void) {
    // ▼ ▲(隔 100 ms,或者按着 ▼ 400 ms 再按 ▲):这边先画的就是小招,不先抡重击
    for (int k = 0; k < 2; k++) {
        test_seed(73);
        begin_as(FC_KID, FC_DAD, 35, 0);
        run(2500);
        clear_events();
        press(1, FK_DOWN);
        run_count(k ? 400 : 100);
        press(1, FK_UP);
        count_events();
        CHECK(EVS[1][FE_START][1][FM_SPECIAL] == 1 && D.b.sim.f[1].st == FS_MOVE && D.b.sim.f[1].move == FM_SPECIAL);
        run_count(1500);
        CHECK(EVS[1][FE_START][1][FM_HEAVY] == 0 && EVS[1][FE_START][1][FM_SPECIAL] == 1);
        CHECK(EVS[0][FE_START][1][FM_SPECIAL] == 1);
        // 小招冷却里再 ▼ ▲:主动方不出,这边也不画
        press(1, FK_DOWN);
        run_count(100);
        press(1, FK_UP);
        run_count(1500);
        CHECK(EVS[0][FE_START][1][FM_SPECIAL] == 1 && EVS[1][FE_START][1][FM_SPECIAL] == 1);
        CHECK(EVS[1][FE_START][1][FM_HEAVY] == 0 && EVS[0][FE_START][1][FM_HEAVY] == 0);
    }
    // 爷爷的太极推手:按下就出招式名(摆架势),没人打过来收招时不再出一次;对面卡上看到的也一样
    for (int me = 0; me < 2; me++) {
        test_seed(74);
        begin_as(me ? FC_KID : FC_GRANDPA, me ? FC_GRANDPA : FC_KID, 35, 0);
        run(2500);
        clear_events();
        press(me, FK_DOWN);
        run_count(100);
        press(me, FK_UP);
        if (me) {
            count_events();
            CHECK(EVS[1][FE_START][1][FM_SPECIAL] == 1 && D.b.sim.f[1].st == FS_STANCE);
        }
        run_count(300);   // 架势还摆着:两张卡都已经出了招式名
        CHECK(EVS[0][FE_START][me][FM_SPECIAL] == 1 && EVS[1][FE_START][me][FM_SPECIAL] == 1);
        CHECK(D.b.sim.f[me].st == FS_STANCE);
        run_count(2200);
        CHECK(EVS[0][FE_START][me][FM_SPECIAL] == 1 && EVS[1][FE_START][me][FM_SPECIAL] == 1);
        // 接住:对面一拳打进架势
        press(me, FK_DOWN);
        run_count(100);
        press(me, FK_UP);
        run_count(100);
        press(!me, FK_OK);
        run_count(1500);
        CHECK(evs(0, FE_COUNTER, me) == 1 && evs(1, FE_COUNTER, me) == 1);
        CHECK(evs(1, FE_HIT, !me) == 0 && evs(0, FE_HIT, !me) == 0);
    }
    // 破防:爸爸的重击砸在举着的盾上,两张卡都是「破防！」不是普通的打中
    for (int me = 0; me < 2; me++) {
        test_seed(75);
        begin_as(me ? FC_KID : FC_DAD, me ? FC_DAD : FC_KID, 35, 0);
        run(2500);
        clear_events();
        press(!me, FK_DOWN);   // 按着 ▼:0.5 秒来长按,盾撑到重击砸下来
        run_count(100);
        press(me, FK_UP);
        run_count(400);
        press(!me, FK_DOWN_LONG);
        run_count(1200);
        CHECK(evs(0, FE_GUARD_BREAK, me) == 1 && evs(1, FE_GUARD_BREAK, me) == 1);
        CHECK(evs(0, FE_HIT, me) == 0 && evs(1, FE_HIT, me) == 0);
    }
}

// ---- 两边一起开打 ----

// 记下两张卡各自「开打」的时刻;跑到两边都开打了
static int32_t fight_gap(void) {
    uint32_t at[2] = { 0, 0 };
    for (int t = 0; !at[0] || !at[1]; t += 10) {
        CHECK(t < FIGHT_NEXT_MS + FIGHT_COUNT_MS + 2000);
        CHECK(!(D.a.phase == GP_END && D.a.ping_wait));   // 回合之间不 ping
        duo_step();
        for (int c = 0; c < 2; c++) {
            uint8_t e;
            while (fight_game_event(side(c), &e, NULL))
                if (e == GE_FIGHT && !at[c]) at[c] = D.now;
        }
    }
    return (int32_t)at[1] - (int32_t)at[0];
}

static void test_fight_aligned(void) {
    for (int busy = 0; busy < 2; busy++) {
        test_seed(76);
        duo_init();
        duo_delay(70, 0);
        duo_connect();
        run(1000);   // 选人页上量过来回
        CHECK(D.a.phase == GP_PICK && D.a.rtt_ok);
        CHECK(fight_game_pick(&D.b, FC_KID));
        run(200);
        if (busy)   // 'Q' 正在路上时开回合:'N' 排在它后面
            for (int t = 0; !D.a.ping_wait; t += 10) {
                CHECK(t < 3000);
                duo_step();
            }
        drain_events();
        CHECK(fight_game_pick(&D.a, FC_DAD));
        flow();
        int32_t gap = fight_gap();
        printf("test_fight_aligned: round 1%s passive starts %d ms after the host", busy ? " (ping in flight)" : "", (int)gap);
        CHECK(gap >= -20 && gap <= 30);
        D.a.sim.f[1].hp = 1;
        press(0, FK_OK);
        for (int t = 0; D.a.phase == GP_PLAY; t += 10) {
            CHECK(t < 2000);
            duo_step();
        }
        drain_events();
        gap = fight_gap();
        printf(", round 2 %d ms\n", (int)gap);
        CHECK(D.a.round == 2 && gap >= -20 && gap <= 20);
    }
}

int main(void) {
    test_mirror();
    test_predict();
    test_late_keys();
    test_stall_400();
    test_queue_full();
    test_drop_resume();
    test_peer_restart();
    test_mash_remote();
    test_remote_super_survives_stall();
    test_remote_super_fair();
    test_snapshot_no_backlog();
    test_slowdown();
    test_burst_reanchor();
    test_view_catches_up();
    test_view_no_double_events();
    test_passive_feedback();
    test_fight_aligned();
    printf("test_link: PASS\n");
    return 0;
}
