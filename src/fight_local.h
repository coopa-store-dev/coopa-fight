// src/fight_local.h —— 和电脑打:卡上开两个 fight_game_t(me 是主动方、cpu 是被连方),用内存里的管道接起来。
// 规则、回合、比分、特写冻结和联机完全是同一套代码,只是消息不走蓝牙(照 coopa-bomb 的 bomb_local)。
// 管道和 kit_link 一样不丢、不乱;电脑(fight_bot)看 cpu.sim、按键走 fight_game_key(&cpu, …)。
#pragma once

#include "fight_game.h"

#define FIGHT_LOCAL_Q 16

typedef struct {
    uint8_t q[FIGHT_LOCAL_Q][FIGHT_MSG_MAX];
    uint8_t len[FIGHT_LOCAL_Q];
    uint8_t head, n;
} fight_pipe_t;

typedef struct {
    fight_game_t me, cpu;
    fight_pipe_t to_cpu, to_me;   // 两个方向的内存管道
} fight_local_t;

void fight_local_init(fight_local_t *l, uint32_t (*rnd)(void));  // 两个 fight_game_t 接好、连上(还要 flow 一下)
void fight_local_flow(fight_local_t *l);                          // 把两边排着的消息互相送到,直到没有
