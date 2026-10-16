// src/fight_ui.h —— 库巴格斗的画面:首页、提示页(找对方 / 断线)、选人页、出招表、对打页(血、计时、气、两个人、
// 特效、提示行、大招特写)。只画,不管流程;对打页每帧照 fight_view_t 改(fight_app.c 填)。
// 图都是游戏块里的常量图(fight_art.h),LVGL 直接从 flash 画,不用精灵会话。
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "fight_sim.h"

#define FIGHT_UI_TEXT 64
#define FIGHT_CUT_MS 1500   // 大招特写多久(= FIGHT_SUPER_FREEZE,fight_app.c 里查)

// 对打页上的一个人:[0] 自己(左边,朝右)、[1] 对方(右边,朝左)
typedef struct {
    uint8_t pose;        // fight_pose_t
    int8_t dx;           // 往对方那边挪多少像素(出拳往前、挨打往后是负的、抖)
    bool flash;          // 挨打闪白
    bool spark;          // 打中火花(画在这个人身上)
    bool shield;         // 挡住了:身前出盾
    uint8_t prop;        // 道具:FX_* + 1(0 = 没有):弹珠、锅盖、太极、铁笼
    int16_t prop_x;      // 道具在身前多远(像素,朝对方)
    uint8_t stars;       // 晕的星星(0..5)
    uint16_t hp, trail;  // 血条:剩多少、刚掉的那段到哪(千分比)
    uint16_t gauge;      // 气 0..3000
    bool gauge_blink;    // 满格的气闪(这一帧亮不亮)
} fight_side_view_t;

typedef struct {
    fight_side_view_t s[2];
    char timer[8];
    char score[16];
    char big[FIGHT_UI_TEXT];   // 中间大字(倒数、开打、KO、结束)
    uint32_t big_color;
    char sub[FIGHT_UI_TEXT];   // 大字下面一行(第几回合、招式名、谁拿下这一回合)
    uint32_t sub_color;
    char hint[FIGHT_UI_TEXT];  // 底下提示行
    uint32_t hint_color;
    int16_t mash;              // 狂按进度条(千分比);-1 = 不画
    int8_t shake;              // 整个台子左右抖多少像素
    uint32_t clock;            // 画面时钟(毫秒):星星转、闪
    // 特写
    bool cut;
    uint8_t cut_char;
    const char *cut_name;
    uint32_t cut_t;            // 特写开始后多久(毫秒)
} fight_view_t;

void fight_ui_title(uint8_t sel, unsigned wins);
void fight_ui_note(const char *big, uint32_t color, const char *l1, const char *l2, const char *l3);
// 选人页:sel 0..3 = 角色、4 = 回首页;crowns 位 = 通关过的角色;title / hint / peer 是三行字。
void fight_ui_pick(uint8_t sel, bool picked, uint8_t crowns, const char *title, const char *hint, const char *peer);
void fight_ui_help(uint8_t ch);   // 出招表(拿 ch 这个人摆动作)
void fight_ui_game(uint8_t me_char, uint8_t peer_char);
void fight_ui_game_set(const fight_view_t *v);
// 照一个人在规则里的状态(fight_sim_t 的,被连方是照快照填的)选动作图、往前挪多少、小招的道具:填 v 的 pose / dx / prop / prop_x
void fight_ui_pose(const fight_fighter_t *f, fight_side_view_t *v);
