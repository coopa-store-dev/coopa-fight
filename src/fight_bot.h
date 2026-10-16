// src/fight_bot.h —— 电脑对手和「真人模型」的脑子:只看 fight_seen_t(玩家看得见的),不看对方排着的键。
// 按真人的节奏按:两下之间至少 gap_ms;看到变化再按要先反应 react_ms ± react_jit。
// 平时按出招习惯出招;● 起手时就定好后面接什么(真人看不清打没打中,0.6 秒的窗口来不及看了再按,
// 所以连招、大招都是提前按的,打空了后面的键照样出)。
// 和电脑打时它驱动对面那个 fight_game_t(fight_local);宿主测试拿它(大人 / 小孩模型)调数值。
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "fight_sim.h"

// 玩家看得见的:两边状态、招式、过了多久、还剩多久、血、气;[0] 是自己,[1] 是对方。没有对方排着的键。
typedef struct {
    uint8_t st[2], move[2];  // fight_state_t、fight_move_t
    uint16_t t[2];           // 这个状态过了多久
    uint16_t left[2];        // 还剩多久:晕 = 狂按还剩多少(星星);出招 / 盾 / 僵直 = dur − t;别的 0
    int16_t hp[2];
    uint16_t gauge[2];
    bool chain_open;         // 自己打中了、对方正僵着(看得见对方在晃)
    bool protect_peer;       // 对方在保护里(刚晕过,头上的光圈)
} fight_seen_t;
void fight_seen(const fight_sim_t *s, uint8_t me, fight_seen_t *out);

typedef struct {
    uint16_t react_ms, react_jit;   // 看到变化到按键
    uint16_t gap_ms;                // 两下之间最短间隔(1000/每秒几下)
    uint8_t p_jab, p_heavy, p_guard, p_special;  // 平时出什么(百分比,合计 100)
    uint8_t p_combo;                // ● 起手时接连招的概率(提前按)
    uint8_t p_super;                // 有气时 ● 起手接 ▼ ● 大招的概率
    uint8_t p_wrong;                // 按错键的概率
    bool reads_habits;              // 统计对方最常出的招并克制(大人)
    bool mash;                      // 晕了会狂按
    uint8_t p_pause;                // 两下之间多停 100–200 ms 的概率(小孩)
    uint8_t p_hold;                 // 防的时候按着 ▼ 撑盾的概率(小孩爱按着 ▼)
} fight_bot_cfg_t;
extern const fight_bot_cfg_t fight_bot_easy, fight_bot_normal, fight_bot_hard, fight_human_adult, fight_human_kid;

typedef struct {
    const fight_bot_cfg_t *cfg;
    uint32_t (*rnd)(void);
    uint32_t super_try, super_ok;   // 统计(测试用):提前按了 ● ▼ ● 而且 ● 打中了几次 / 其中出了大招几次
    uint32_t pad[24];               // 内部状态由实现定
} fight_bot_t;
void fight_bot_init(fight_bot_t *b, const fight_bot_cfg_t *cfg, uint32_t (*rnd)(void));
int fight_bot_step(fight_bot_t *b, const fight_seen_t *s, uint32_t dt);   // 每 10 ms 一次;返回 fight_key_t,-1 = 不按
