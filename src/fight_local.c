// src/fight_local.c —— 见 fight_local.h。
#include "fight_local.h"

#include <string.h>

static bool pipe_send(void *ctx, const void *m, size_t len) {
    fight_pipe_t *p = ctx;
    if (p->n == FIGHT_LOCAL_Q || len > sizeof p->q[0]) return false;  // 满了:fight_game 下一帧再发
    uint8_t i = (uint8_t)((p->head + p->n) % FIGHT_LOCAL_Q);
    memcpy(p->q[i], m, len);
    p->len[i] = (uint8_t)len;
    p->n++;
    return true;
}

static bool deliver(fight_pipe_t *p, fight_game_t *to) {
    if (!p->n) return false;
    uint8_t m[FIGHT_MSG_MAX], len = p->len[p->head];
    memcpy(m, p->q[p->head], len);
    p->head = (uint8_t)((p->head + 1) % FIGHT_LOCAL_Q);
    p->n--;
    fight_game_on_msg(to, m, len);
    return true;
}

void fight_local_init(fight_local_t *l, uint32_t (*rnd)(void)) {
    memset(l, 0, sizeof *l);
    fight_game_init(&l->me, pipe_send, &l->to_cpu, rnd);
    fight_game_init(&l->cpu, pipe_send, &l->to_me, rnd);
    fight_game_connected(&l->me, true);
    fight_game_connected(&l->cpu, false);
}

void fight_local_flow(fight_local_t *l) {
    for (bool any = true; any;) {
        any = false;
        fight_game_tick(&l->me, 0);   // 不推时间,只把排着的消息交出去
        fight_game_tick(&l->cpu, 0);
        while (deliver(&l->to_cpu, &l->cpu)) any = true;
        while (deliver(&l->to_me, &l->me)) any = true;
    }
}
