// src/fight_msg.h —— 联机消息的打包和拆包(消息格式见 fight_game.h):小端读写、快照 'F'、按键 'K'。
// 纯 C,只管字节,不管一场比赛的状态。
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "fight_sim.h"

#define FIGHT_SNAP_LEN 16
#define FIGHT_K_MAX 7     // 一条 K 最多装几个键
#define FIGHT_K_GAP_MAX 255  // K 里「距上一键几个 10 ms」封顶

void fight_put16(uint8_t *p, uint16_t v);
void fight_put32(uint8_t *p, uint32_t v);
uint16_t fight_get16(const uint8_t *p);
uint32_t fight_get32(const uint8_t *p);

// 快照里的一个人(拆出来的样子);t、left 是毫秒(10 ms 精度,封顶 2550)
typedef struct {
    uint8_t st, move, chain;
    bool protect;
    int16_t hp;
    uint16_t gauge;
    uint16_t t;     // 这个状态过了多久
    uint16_t left;  // 还剩多久:晕 = 狂按还剩多少;特写 = 冻结还剩多少;出招 / 盾 / 僵直 = dur − t;别的 0
} fight_snap_one_t;
typedef struct {
    uint8_t ack;    // 主动方收到的最后一条 'K' 的序号
    uint32_t clock;
    fight_snap_one_t f[2];
} fight_snap_t;

// 把主动方的 fight_sim_t 打成 'F'(FIGHT_SNAP_LEN 字节),返回长度
size_t fight_snap_pack(uint8_t *m, const fight_sim_t *s, uint8_t ack);
// 拆 'F';长度不对、取值越界返回 false(血还要由调用方按角色的满血再查)
bool fight_snap_unpack(fight_snap_t *out, const uint8_t *m, size_t n);

typedef struct { uint8_t key, gap; } fight_kpair_t;  // 键(fight_key_t)、距上一键几个 10 ms
size_t fight_k_pack(uint8_t *m, uint8_t seq, const fight_kpair_t *p, uint8_t n);  // n ≤ FIGHT_K_MAX
// 拆 'K':返回几个键(1..FIGHT_K_MAX);格式不对、键越界返回 0
uint8_t fight_k_unpack(const uint8_t *m, size_t n, uint8_t *seq, fight_kpair_t *out);
