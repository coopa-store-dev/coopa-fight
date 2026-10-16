// src/fight_moves.h —— 四个角色的出招表(纯数据,宿主测试)。
// 数值全在 fight_moves.c 的表里,调数值只改那里(单位:毫秒 / 血,满血 1000 制)。
#pragma once

#include <stdint.h>

typedef enum { FC_KID = 0, FC_DAD, FC_GRANNY, FC_GRANDPA, FC_COUNT } fight_char_t;

typedef enum {
    FM_JAB = 0,     // ● 轻拳
    FM_JAB2,        // 连招第二拳
    FM_JAB3,        // 连招第三拳(打退,连招结束)
    FM_HEAVY,       // ▲ 重击
    FM_HEAVY_LINK,  // 轻拳打中后 ▲:连段重击,不用抡
    FM_SPECIAL,     // ▼ ▲ 小招(各角色不同)
    FM_SUPER,       // ● 打中后 ▼ ●:大招
    FM_COUNT
} fight_move_t;

typedef struct {
    uint16_t startup;    // 按下到打到(ms)
    uint16_t active;     // 打到判定持续
    uint16_t recovery;   // 打完(没打中)收招
    uint16_t dmg;        // 血(满血 1000 制)
    uint16_t hitstun;    // 打中后对方僵多久(连招窗口)
    uint16_t armor_from; // 重击:从这一刻起轻拳打不断(0 = 没有)
    uint8_t  breaks_guard;
    uint8_t  hits;       // 打几下(奶奶锅盖 2)
    uint16_t hit2_at;    // 第二下的时刻
} fight_move_def_t;

typedef struct {
    uint16_t hp;          // 满血
    uint16_t guard_ms;    // 举盾多久(长按撑到 guard_long_ms)
    uint16_t guard_long_ms;
    fight_move_def_t mv[FM_COUNT];
} fight_char_def_t;

const fight_char_def_t *fight_char(uint8_t c);  // 越界返回库巴
