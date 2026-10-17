// tests/test_fair.c —— 联机时坐哪张卡都一样(设计 §2「网一卡,一方明显吃亏」就是失败):两个 fight_game_t 经 duo 的
// 假链路(单程 0 / 35 / 70 ms,抖动 ±1/3)打完整场,两边各一个真人模型(fight_bot 的大人 / 小孩)。
// 主动方的人看主动方的 sim(真的),被连方的人看被连方自己的 sim(照快照画的)——就是两个人各自屏幕上看到的。
//   - 大人对大人(同一个人对打):主动方胜率 40%–60%;
//   - 小孩对大人:小孩坐主动方和坐被连方的胜率差 ≤ FAIR_KID_GAP。
// 每种 FAIR_N 场(角色轮着来),一共几千场:开优化、不开 sanitizer(同 test_balance)。
#include <stdio.h>
#include <stdlib.h>

#include "duo.h"
#include "fight_bot.h"

#define FAIR_N 1600
#define FAIR_KID_GAP 15
#define MATCH_MAX_MS 400000

#define CHECK(c) do { if (!(c)) { \
    printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); exit(1); } } while (0)

static uint32_t s_bot = 99;
static uint32_t bot_rnd(void) {
    s_bot = s_bot * 1664525u + 1013904223u;
    return s_bot >> 8;
}

// 一场:a(主动方)角色 ca、模型 cfa;b 同理。返回赢的一方(0 = 主动方)
static int play_match(uint8_t ca, uint8_t cb, uint32_t one_way, const fight_bot_cfg_t *cfa,
                      const fight_bot_cfg_t *cfb) {
    duo_init();
    duo_connect();
    flow();
    duo_delay(one_way, one_way / 3);
    run(2500);  // 选人页上量来回
    duo_pick(ca, cb);
    fight_bot_t ba, bb;
    fight_bot_init(&ba, cfa, bot_rnd);
    fight_bot_init(&bb, cfb, bot_rnd);
    for (uint32_t t = 0; t < MATCH_MAX_MS; t += FIGHT_STEP) {
        duo_step();
        drain_events();
        if (D.a.phase == GP_OVER && D.b.phase == GP_OVER) return D.a.score_me >= FIGHT_WIN ? 0 : 1;
        for (int s = 0; s < 2; s++) {
            fight_game_t *g = side(s);
            if (g->phase != GP_PLAY) continue;
            fight_seen_t v;
            fight_seen(&g->sim, g->me, &v);
            int k = fight_bot_step(s ? &bb : &ba, &v, FIGHT_STEP);
            if (k >= 0) press(s, (uint8_t)k);
        }
    }
    CHECK(!"match did not finish");
    return -1;
}

int main(void) {
    static const uint32_t ONE_WAY[] = { 0, 35, 70 };
    for (unsigned d = 0; d < sizeof ONE_WAY / sizeof ONE_WAY[0]; d++) {
        uint32_t ow = ONE_WAY[d];
        int host = 0;
        for (int i = 0; i < FAIR_N; i++) {
            uint8_t c = (uint8_t)(i % FC_COUNT);
            host += play_match(c, c, ow, &fight_human_adult, &fight_human_adult) == 0;
        }
        int kid[2] = { 0, 0 };  // [0] 小孩坐主动方、[1] 坐被连方:小孩赢了几场
        for (int seat = 0; seat < 2; seat++) {
            for (int i = 0; i < FAIR_N; i++) {
                uint8_t ck = (uint8_t)(i % FC_COUNT), ca = (uint8_t)(i / FC_COUNT % FC_COUNT);
                int w = seat ? play_match(ca, ck, ow, &fight_human_adult, &fight_human_kid)
                             : play_match(ck, ca, ow, &fight_human_kid, &fight_human_adult);
                kid[seat] += w == seat;
            }
        }
        int hp = host * 100 / FAIR_N, k0 = kid[0] * 100 / FAIR_N, k1 = kid[1] * 100 / FAIR_N;
        printf("  单程 %2u±%-2u ms:大人对大人 主动方赢 %d%%;小孩对大人 小孩坐主动方赢 %d%%、坐被连方赢 %d%%(%d 场一种)\n",
               (unsigned)ow, (unsigned)(ow / 3), hp, k0, k1, FAIR_N);
        CHECK(hp >= 40 && hp <= 60);
        CHECK(abs(k0 - k1) <= FAIR_KID_GAP);
    }
    printf("test_fair: PASS\n");
    return 0;
}
