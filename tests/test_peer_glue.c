// tests/test_peer_glue.c —— Mac 对端的 C 接口(peer_glue.c)当主动方,和 duo.c 的 b(被连方,当卡)打完一场。
// 照 link_peer.py fight 那样用它:peer_tick 的 dt 不齐(Python 那边 10 ms 左右睡一次)、peer_take 取到没有为止、
// 卡发来的逐条 peer_msg。卡这边由「真人模型」(大人)按键。打完两边都在结束页、场号一样、比分互为镜像。
#include <stdio.h>
#include <stdlib.h>

#include "duo.h"
#include "fight_bot.h"

#define CHECK(c) do { if (!(c)) { \
    printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); exit(1); } } while (0)

void peer_init(uint32_t seed);
void peer_connected(void);
void peer_msg(const uint8_t *m, size_t len);
void peer_tick(uint32_t dt);
int peer_press(void);
int peer_holding(void);
int peer_over(void);
size_t peer_take(uint8_t *buf);
int peer_status(char *buf, size_t len);

static const uint32_t DT[] = { 7, 13, 10, 16, 9, 11 };  // Python 那边每圈过了多久(合计是 10 的倍数)

int main(void) {
    duo_init();
    D.ba.up = true;
    peer_init(7);
    peer_connected();
    fight_game_connected(&D.b, false);
    fight_bot_t card;
    fight_bot_init(&card, &fight_human_adult, test_rnd);
    uint32_t acc = 0;
    int step;
    for (step = 0; step < 200000 && !(peer_over() && D.b.phase == GP_OVER); step++) {
        uint32_t dt = DT[step % 6];
        peer_tick(dt);
        if (peer_holding()) peer_press();  // fight 模式里一直是 0;照 link_peer.py 的样子留着
        if (D.b.phase == GP_PICK && !D.b.me_picked) CHECK(fight_game_pick(&D.b, FC_GRANNY));
        for (acc += dt; acc >= FIGHT_STEP; acc -= FIGHT_STEP) {
            fight_game_tick(&D.b, FIGHT_STEP);
            if (D.b.phase != GP_PLAY) continue;
            fight_seen_t v;
            fight_seen(&D.b.sim, D.b.me, &v);
            int k = fight_bot_step(&card, &v, FIGHT_STEP);
            if (k >= 0) fight_game_key(&D.b, (uint8_t)k);
        }
        uint8_t m[FIGHT_MSG_MAX];
        size_t n;
        while ((n = peer_take(m)) > 0) fight_game_on_msg(&D.b, m, n);
        while (D.ba.n) {
            int i = D.ba.head;
            D.ba.head = (D.ba.head + 1) % PIPE_N;
            D.ba.n--;
            peer_msg(D.ba.q[i], D.ba.len[i]);
        }
        drain_events();
        CHECK(D.b.dropped == 0);
    }
    char st[200];
    peer_status(st, sizeof(st));
    printf("  %s(卡这边 %u:%u,%d 圈)\n", st, (unsigned)D.b.score_me, (unsigned)D.b.score_peer, step);
    unsigned ph, round, me, peer, ch_mac, ch_card;
    unsigned long match;
    CHECK(sscanf(st, "phase=%u round=%u me=%u peer=%u match=%lx char=%u/%u", &ph, &round, &me, &peer, &match, &ch_mac,
                 &ch_card) == 7);
    CHECK(peer_over() && D.b.phase == GP_OVER && ph == GP_OVER);
    CHECK(match == D.b.match && me == D.b.score_peer && peer == D.b.score_me);
    CHECK(me == FIGHT_WIN || peer == FIGHT_WIN);
    CHECK(ch_card == FC_GRANNY && D.b.peer_char == ch_mac);
    puts("test_peer_glue: ok");
    return 0;
}
