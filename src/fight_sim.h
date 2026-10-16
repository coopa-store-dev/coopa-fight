// src/fight_sim.h —— 库巴格斗的一回合规则:两个人的动作、打中、防御、连招、互相打中、气、KO、时间到。
// 纯 C、确定性:整数毫秒,固定 FIGHT_STEP 一步;不用随机数。卡上和宿主测试跑的是同一份。
// 数值:招式在 fight_moves.c 的表里,规则的数在这里的宏里,调数值只改这两处。
//
// 一步的顺序(两人固定,先算判定再统一结算):
//   1. clock、两人的 t 往前走,冷却、刚醒吃键、晕往下减;
//   2. 两人这一步谁打到判定(出招的 t 正好到 startup;锅盖飞回来);两边出招都打到 = 互相打中;
//   3. 结算打到:打空(刚醒;对方挨打僵着、这一下又不是连招里的)/ 太极接住 / 挡住 / 砸开防御 / 打中 / 重击后半段只扣轻拳的血;扣血、涨气、进僵直或晕;
//      锅盖第二下在这之后算;
//   4. 有人血空 → KO,这一回合结束;
//   5. 到时间的状态回 FS_IDLE(晕到 0 醒、太极收招、保护消失);排着的键按顺序出(连招里按连招解释);
//   6. clock 到 FIGHT_ROUND_MS → 时间到。
// 大招特写(freeze)期间:clock 不走、两边按键丢掉、不涨气;冻结完才扣大招血,对方进 FS_DIZZY。
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "fight_moves.h"

#define FIGHT_STEP 10
#define FIGHT_GAUGE_BAR 1000
#define FIGHT_GAUGE_MAX 3000
#define FIGHT_GUARD_COOL 400
#define FIGHT_BLOCKSTUN 500   // 被挡住的轻拳:出拳的人多僵这么久
#define FIGHT_PREFIX_MS 350   // ▼ 前缀:按 ▼ 后这么久内的 ▲ / ● 算小招 / 大招
#define FIGHT_ROUND_MS 60000
#define FIGHT_LATE_MAX 400    // 联机晚到的键最多按本来的时刻补算这么久
#define FIGHT_GAUGE_HIT 6     // 打中
#define FIGHT_GAUGE_HURT 15   // 挨打
#define FIGHT_GAUGE_BEHIND 50 // 挨打时血比对方少 FIGHT_BEHIND 以上再加
#define FIGHT_BEHIND 200      // 满血 1000 制
#define FIGHT_GAUGE_BLOCK 5   // 挡住、被挡都加
#define FIGHT_EVQ 16
#define FIGHT_UNDECIDED 2     // winner:还没分出
#define FIGHT_DRAW 3          // winner:平
#define FIGHT_Q_PREFIX 0x80   // queue 里的 ● / ▲:紧跟在 ▼ 后面、FIGHT_PREFIX_MS 内按的(或 ▼ 长按撑着盾时按的 ▲)
#define FIGHT_Q_NOSUPER 0x40  // queue 里的 ● / ▲:按的时候(按本来的时刻)对方在保护里:▼ ● 只出拳,拿它起手的这一套不能有大招
#define FIGHT_SUPER_FREEZE 1500
#define FIGHT_DIZZY_SUPER 2000
#define FIGHT_DIZZY_BREAK 1000
#define FIGHT_MASH_STEP 160    // 每按一下少这么久
#define FIGHT_MASH_PER_SEC 6   // 一秒最多算几下
#define FIGHT_MASH_WINDOW 1000 // 「一秒」:滚动这么久里最多算 FIGHT_MASH_PER_SEC 下
#define FIGHT_WAKE_EAT 300     // 醒后这么久不收键,也不挨打
#define FIGHT_STANCE_MS 1000
#define FIGHT_SPECIAL_COOL 1000 // 小招出完这么久内不能再出

typedef enum { FK_UP = 0, FK_DOWN, FK_OK, FK_DOWN_LONG } fight_key_t;   // 和 kit_key_t 对应;长按只认 ▼
typedef enum {
    FS_IDLE = 0, FS_MOVE,      // 出招中(move、t)
    FS_GUARD, FS_HITSTUN, FS_BLOCKSTUN, FS_DIZZY, FS_SUPER_IN, FS_SUPER_OUT, FS_STANCE, FS_KO
} fight_state_t;
typedef struct {
    uint8_t ch;          // fight_char_t
    uint8_t st;          // fight_state_t
    uint8_t move;        // FS_MOVE 时:fight_move_t
    uint16_t t;          // 这个状态过了多久
    uint16_t dur;        // 这个状态一共多久(僵直 / 晕 / 举盾;出招 = startup + active + recovery)
    int16_t hp;
    uint16_t gauge;
    uint8_t chain;       // 这套连招打中了几下(0 = 不在连招里)
    bool chain_open;     // 对方正僵着,下一个键接连招
    uint32_t chain_until; // 连招窗口在这个 clock 关(对方僵到这一刻);晚到的键按本来按下的时刻和它比。0 = 没有窗口
    bool chain_nosuper;  // 这一套连招不能有大招:起手出的时候、或者这一套哪一下打到的时候对方在保护里
    uint32_t down_at;    // 最近一次按 ▼ 的 clock(前缀);UINT32_MAX = 这回合还没按过
    uint16_t down_grace; // 这次 ▼ 晚到了(联机补算)= FIGHT_LATE_MAX,本地 0:连招里排着等下一个键时,按真时钟多等这么久
    bool down_last;      // 上一个按下的键是 ▼(前缀只管紧跟在它后面的那一个键)
    bool long_guard;     // ▼ 长按撑起来的盾还在
    bool protect;        // 保护:被打晕起,到醒后第一次能动且没在挨打为止(不再晕、不吃大招)
    uint32_t protect_end; // 最近一次保护消失的 clock(0 = 这回合还没有过):晚到的起手按本来的时刻和它比
    uint16_t special_cool; // 小招冷却(小招出完才开始减)
    uint16_t eat_keys;   // 刚醒:还剩多久不收键、也不挨打
    uint8_t queue[4];    // 提前按的键(出招还没打到时按的):fight_key_t,● / ▲ 可能带 FIGHT_Q_PREFIX、FIGHT_Q_NOSUPER
    uint8_t qn;
    uint16_t guard_cool;
    uint16_t mash_left;  // 晕:还剩多久(狂按减)
    uint16_t mash_n;     // 这次晕已经算了几下
    uint32_t mash_at[FIGHT_MASH_PER_SEC]; // 最近算进去的几下的时刻(环,下标 mash_n % FIGHT_MASH_PER_SEC)
    uint16_t hit2_left;  // 锅盖飞回来打第二下还要多久(0 = 没有在飞的锅盖)
} fight_fighter_t;
// 事件里的 who(move 是相关的招,fight_move_t):
//   FE_START       出招的人
//   FE_HIT         打中的人(出招方);包括重击后半段只扣血没打断、大招冻结完扣血(move = FM_SUPER)
//   FE_BLOCK       被挡住的人(出招方)
//   FE_WHIFF       打空的人(出招方):打到刚醒 FIGHT_WAKE_EAT 里的人、不在连招里的一下打到挨打僵着的人;爷爷太极没人打过来
//   FE_GUARD_BREAK 砸开防御的人(出招方)
//   FE_TRADE       两边同一步打中对方:who 固定 0,move 是 0 号的招(1 号的招还在 f[1].move)
//   FE_SUPER       放大招的人(特写开始)
//   FE_DIZZY / FE_WAKE  晕的人(狂按到 0、或晕着挨打都算醒)
//   FE_COUNTER     接住反打的人(爷爷)
//   FE_KO          倒下的人(move = 打倒他的那一招);两人一起倒就两条
//   FE_TIMEUP      赢的人(0/1;FIGHT_DRAW = 平)
typedef enum {
    FE_NONE = 0, FE_START, FE_HIT, FE_BLOCK, FE_WHIFF, FE_GUARD_BREAK, FE_TRADE, FE_SUPER, FE_DIZZY, FE_WAKE,
    FE_COUNTER, FE_KO, FE_TIMEUP
} fight_ev_kind_t;
typedef struct { uint8_t kind, who, move; } fight_ev_t;
typedef struct {
    fight_fighter_t f[2];
    uint32_t clock;       // 这一回合过了多久(不含特写冻结)
    uint16_t freeze;      // 特写冻结还剩多久
    uint8_t winner;       // 0/1,FIGHT_UNDECIDED = 还没分出,FIGHT_DRAW = 平
    fight_ev_t ev[FIGHT_EVQ]; uint8_t evn;  // 满了先丢最旧的 FE_HIT / FE_BLOCK,FE_KO / FE_TIMEUP 不丢
} fight_sim_t;
void fight_sim_init(fight_sim_t *s, uint8_t ch0, uint8_t ch1, uint16_t gauge0, uint16_t gauge1);
void fight_sim_key(fight_sim_t *s, uint8_t who, uint8_t key);   // fight_key_t
// 这个键其实是 late_ms 之前按的(本地 0;联机补算,封顶 FIGHT_LATE_MAX):▼ 前缀、连招窗口、起手时对方在不在保护里
// 按 clock − late_ms 判,动作从现在开始。晚到的 ▼ 在连招里等下一个键时,按真时钟多等 FIGHT_LATE_MAX
// (下一个键也晚到,排在它后面常常更晚)。
void fight_sim_key_at(fight_sim_t *s, uint8_t who, uint8_t key, uint16_t late_ms);
void fight_sim_step(fight_sim_t *s);                            // 推 FIGHT_STEP 毫秒
bool fight_sim_event(fight_sim_t *s, fight_ev_t *out);          // 取一条事件,没有返回 false
uint16_t fight_mash_taken(const fight_sim_t *s, uint8_t who);  // 这次晕已经算了几下(给画面画星星)
