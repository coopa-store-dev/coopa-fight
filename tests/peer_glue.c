// tests/peer_glue.c —— Mac 冒充对手(外壳的 tools/link_peer.py fight)用的 C 接口:一个 fight_game_t + 一个电脑(普通)。
// 接口和 coopa-bomb 的 tests/peer_glue.c 一样(peer_init / peer_connected / peer_lost / peer_msg / peer_tick /
// peer_press / peer_holding / peer_over / peer_take / peer_status),link_peer.py 照同一个样子加载。
// Mac 永远是主动连的一方,所以 Mac 这边跑真的规则(开回合、判打中、扣血),卡当被连方。时间由 Python 推(peer_tick)。
// 选人、出招、狂按、「再来一场」都由电脑在 peer_tick 里自己做,所以 peer_holding() 一直是 0(link_peer.py 不用自己按)。
// link_peer.py 只编 src/fight_game.c 和这个文件,所以其余的规则源文件在这里直接包含进来
// (tests/Makefile 编 test_peer_glue 时就不再单独列它们)。发出去的消息先进这里的队列,由 Python 取走。
#include <stdio.h>
#include <string.h>

#include "fight_bot.h"
#include "fight_game.h"

#include "../src/fight_bot.c"
#include "../src/fight_moves.c"
#include "../src/fight_msg.c"
#include "../src/fight_sim.c"

// 放得下一帧里最多交出去的几条('N' / 'O' + 'E' + 'F')。Python 那边 link_proto 排满(8 条,和卡上 kit_link 一样)
// 就不再取,这里很快也满,send 返回 false:fight_game 照卡上 send 被拒的样子处理拥堵
#define PEER_Q 4
static fight_game_t s_g;
static fight_bot_t s_bot;
static uint8_t s_q[PEER_Q][FIGHT_MSG_MAX];
static size_t s_len[PEER_Q];
static int s_head, s_cnt;
static uint32_t s_rng = 4242, s_acc;
static unsigned s_supers[2];  // 放出的大招:[0] Mac、[1] 卡

static bool to_py(void *ctx, const void *m, size_t len) {
    (void)ctx;
    if (s_cnt == PEER_Q) return false;
    int i = (s_head + s_cnt) % PEER_Q;
    memcpy(s_q[i], m, len);
    s_len[i] = len;
    s_cnt++;
    return true;
}

static uint32_t glue_rnd(void) {
    s_rng = s_rng * 1664525u + 1013904223u;
    return s_rng >> 8;
}

void peer_init(uint32_t seed) {
    s_rng = seed ? seed : 4242;
    s_acc = 0;
    s_supers[0] = s_supers[1] = 0;
    fight_game_init(&s_g, to_py, NULL, glue_rnd);
    fight_bot_init(&s_bot, &fight_bot_normal, glue_rnd);
}

void peer_connected(void) {
    s_head = s_cnt = 0;
    fight_game_connected(&s_g, true);
}

void peer_lost(void) { fight_game_lost(&s_g); }
void peer_msg(const uint8_t *m, size_t len) { fight_game_on_msg(&s_g, m, len); }

// 照卡上和电脑打(fight_app.c cpu_frame):规则和电脑的脑子都 10 ms 一步,不够一步的留到下次
void peer_tick(uint32_t dt) {
    if (s_g.phase == GP_PICK && !s_g.me_picked && fight_game_pick(&s_g, (uint8_t)(glue_rnd() % FC_COUNT))) {
        fight_bot_init(&s_bot, &fight_bot_normal, glue_rnd);
    }
    if (s_g.phase == GP_OVER && !s_g.me_again) fight_game_again(&s_g);  // 打完了:直接要「再来一场」
    for (s_acc += dt; s_acc >= FIGHT_STEP; s_acc -= FIGHT_STEP) {
        fight_game_tick(&s_g, FIGHT_STEP);
        if (s_g.phase != GP_PLAY) continue;
        fight_seen_t v;
        fight_seen(&s_g.sim, s_g.me, &v);
        int k = fight_bot_step(&s_bot, &v, FIGHT_STEP);
        if (k >= 0) fight_game_key(&s_g, (uint8_t)k);
    }
    uint8_t e;
    fight_ev_t se;
    while (fight_game_event(&s_g, &e, &se)) {
        if (e == GE_SIM && se.kind == FE_SUPER) s_supers[se.who == s_g.me ? 0 : 1]++;
    }
}
int peer_press(void) { return 0; }  // 电脑自己按(见 peer_tick)
int peer_holding(void) { return 0; }
int peer_over(void) { return s_g.phase == GP_OVER; }

size_t peer_take(uint8_t *buf) {
    fight_game_tick(&s_g, 0);  // 不推时间,只把排着的(刚才被拒的)交出去
    if (!s_cnt) return 0;
    size_t len = s_len[s_head];
    memcpy(buf, s_q[s_head], len);
    s_head = (s_head + 1) % PEER_Q;
    s_cnt--;
    return len;
}

// 比分、血、气都是 Mac 这边(主动方)的说法:me = Mac、peer = 卡,和卡上 STAT 的「我 / 对方」正好反过来
int peer_status(char *buf, size_t len) {
    const fight_fighter_t *a = &s_g.sim.f[s_g.me], *b = &s_g.sim.f[!s_g.me];
    return snprintf(buf, len, "phase=%u round=%u me=%u peer=%u match=%08lx char=%u/%u hp=%d/%d g=%u/%u super=%u/%u rtt=%u",
                    (unsigned)s_g.phase, (unsigned)s_g.round, (unsigned)s_g.score_me, (unsigned)s_g.score_peer,
                    (unsigned long)s_g.match, (unsigned)s_g.me_char, (unsigned)s_g.peer_char, (int)a->hp, (int)b->hp,
                    (unsigned)a->gauge, (unsigned)b->gauge, s_supers[0], s_supers[1], (unsigned)s_g.rtt);
}
