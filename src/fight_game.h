// src/fight_game.h —— 库巴格斗的一场比赛和两张卡联机:选人、回合、倒数、比分、气带到下一回合、断线续上,
// 和对方之间的消息。纯 C:发送和随机数走回调(卡上是 kit_link_send / esp_random),不直接调 kit_*;
// 宿主测试把两个实例接在一起(tests/duo.c)。
//
// 谁说了算:只有主动方跑真的 fight_sim(判打中、扣血、气、KO、时间到、比分、开回合)。被连方只把自己的按键发过去,
// 照快照 'F' 和 'E' 填自己的 sim 来画;自己按下的招先画出来(只画不判,▼ 前缀照规则判),等收到算过这个键的快照
// 再以快照为准。sim.f[0] 永远是主动方、f[1] 是被连方;me 是自己在里面的下标。
//
// 两边公平(坐哪张卡都一样):主动方看自己是马上看到的,自己的键延后 delay(≈ 单程)才生效。被连方收到的快照
// 已经旧了一个单程,所以每条快照往前推 ahead(主动方在 'N' 里给:2 × 量到的单程 − delay,约等于单程)再画;
// 'E' 的大招特写也少定住 ahead。出招、盾、僵直到时间、晕减到 0、特写冻结完,就在这边结束(放大招的人能动、
// 挨的人开始晕)——自己从挨打、被挡、晕里缓过来,不用等下一条快照。这边已经结束掉的出招 / 被挡僵直 / 晕 / 特写,
// 晚到的快照里还在(推得不够)也不再画回来。
// 开打:被连方从收到 'N' 起倒数,主动方多等「'N' 在路上的时间」(链路还忙着的那段、排在它前面的消息、再一个单程)。
//
// 消息(第一字节是类型,多字节小端,每条 ≤ FIGHT_MSG_MAX):
//   'H' 版本                                                         连上后双方各发一次
//   'S' 场号(4)                                                      主动方:进选人页(新的一场的场号)
//   'P' 场号(4) 角色(FIGHT_NOCHAR = 撤回)                             选人页确定 / 撤回;连回来重发
//   'N' 场号(4) 回合 主动方比分 被连方比分 主动方角色 被连方角色 主动方气(2) 被连方气(2) 快照往前推几 ms
//        主动方:开一回合(倒数 FIGHT_COUNT_MS);回合 = 0:这场打完了(打完时、连回来时告诉被连方)
//   'O' 场号(4) 回合 赢家(0 主动方 / 1 被连方 / FIGHT_DRAW 平:重打这一回合) 主动方比分 被连方比分
//        主动方:回合结束
//   'R' 场号(4)                                                      结束页「再来一场」
//   'K' 序号 然后 1..7 对(键 fight_key_t、距上一键几个 10 ms,封顶 255)  被连方 → 主动方
//   'M' 这次晕已经狂按了几下(封顶 255)                                被连方 → 主动方(晕着时代替 K)
//   'F' 快照(下面)                                                   主动方 → 被连方
//   'E' 回合 种类(FE_SUPER / FE_GUARD_BREAK / FE_KO / FE_TIMEUP) who move 角色  主动方 → 被连方:不能丢的事件
//   'Q' 编号 / 'A' 编号                                              主动方 ping、被连方回应(开打时不发)
//
// 快照 'F' 正好 16 字节:[0] 'F' [1] ack = 主动方收到的最后一条 'K' 的序号 [2..3] 回合计时 / 10 ms
//   [4..9] 主动方 [10..15] 被连方。每人 48 位(小端,从低到高):状态 4、招式 3、这套连招打中几下 2(封顶 3)、
//   保护 1、血 10、气 12(全精度)、这个状态过了多久 / 10 ms 8(封顶 2550 ms)、还剩多久 / 10 ms 8
//   (晕 = 狂按还剩多少,特写 = 冻结还剩多少,出招 / 盾 / 僵直 = dur − t;封顶 2550 ms)。
//   没带的(画面用不着,规则只在主动方):排着的键、▼ 前缀、连招窗口、冷却、刚醒吃键、狂按记录(星星用 left 画)、
//   锅盖在飞、赢家(E 带)。
//
// 节奏:
// - 被连方的键:一串(和上一键隔 < FIGHT_BURST_MS)里第一下按到达时刻算,后面的按「上一下 + 间隔」排到那一刻喂给
//   fight_sim_key_at;已经过了那一刻的(重发晚到)马上喂,late_ms = 晚了多少(封顶 FIGHT_LATE_MAX)。
//   (下面「往前挪」以后排的时刻都不会晚于到达,实际上都是马上喂。)
//   被连方最多 FIGHT_K_INFLIGHT 条 K 还没被快照的 ack 确认,再按的键排着,合并进下一条。
// - 主动方自己的键延后 delay 再喂 f[0]:delay = 平滑来回的一半(新的一次占 1/4),限制在 30–150 ms,
//   还没量到时 FIGHT_LOCAL_DELAY。'Q' 在开打以外(选人、倒数、回合之间、打完)、链路静了 FIGHT_PING_QUIET 以后
//   每 FIGHT_PING_MS 一次;开打时不 ping,因为 'Q' / 'A' 各占链路一片,会把快照和被连方的键挤后一个来回。
// - 链路是停等的:一片的确认回来才发下一片,交出去太快就全排在链路里(快照越积越旧)。主动方每交出去一条,
//   就估计链路要忙一片的时间(量到的平滑来回 × 9/8);快照和 'Q' 只在估计空下来时交出去(快照有事件尽快,
//   没事件约每 FIGHT_SNAP_MS),快照交出去的时候现打包,所以排着的旧快照不会堆在链路里。
//   回合里链路变慢了:send 被拒(链路排满)就把一片的估计加到 3/2(最多 FIGHT_BUSY_MAX)、快照停到排着的送完,
//   之后很慢地收回;下次 ping 重新量。
// - 被连方的键:一条 'K' 的最后一个键排在它到达以后,说明这一串开头那下晚到了,整条往前挪到最后一个 = 到达时刻
//   (键不可能比它被发出去还晚按),免得一下卡顿拖慢这一串后面所有的键。
// - 'N' / 'O' 排满了也不丢:先留着,有空再排。
// - 断线:这一回合作废,比分和回合开始时的气留着;连回来主动方重开这一回合(判完了就开下一回合)。
//   被连方重启过也没关系:'N' 里带着比分、气和角色。
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "fight_msg.h"
#include "fight_sim.h"

#define FIGHT_PROTO 2
#define FIGHT_MSG_MAX 16
#define FIGHT_WIN 2
#define FIGHT_COUNT_MS 3000
#define FIGHT_NEXT_MS 2500
#define FIGHT_LOCAL_DELAY 60     // 主动方自己的按键延后多久生效(被连方按键路上的估计);还没量到来回时也当单程用
#define FIGHT_SNAP_MS 100        // 快照最多多久发一次(有事件时马上发)
#define FIGHT_PING_MS 2000
#define FIGHT_PING_QUIET 300     // 主动方这么久没交出去任何消息才 ping
#define FIGHT_DELAY_MIN 30
#define FIGHT_DELAY_MAX 150
#define FIGHT_BURST_MS 1000      // 被连方的键和上一键隔这么久以上:新的一串,按到达时刻算
#define FIGHT_MASH_MSG_MS 200    // 被连方晕着:最多这么久发一条 'M'
#define FIGHT_K_INFLIGHT 4       // 被连方最多几条 'K' 没被快照确认(加上偶尔的 'M' / 'A',不超过链路的 8 条)
#define FIGHT_NOCHAR 0xFF        // 还没选人 / 'P' 里的撤回
#define FIGHT_LINK_Q 8           // kit_link 最多排几片(send 被拒 = 排满了)
#define FIGHT_BUSY_MAX 600       // send 被拒(拥堵)后一片的估计最多加到这么久
#define FIGHT_BUSY_RUN 64        // 连着交出去这么多条才把拥堵的估计往量到的来回收 1 ms
#define FIGHT_OUTQ 8
#define FIGHT_GEVQ 32
#define FIGHT_KEYQ 32
#define FIGHT_GONE_TOL 300       // 被连方:快照里的出招 / 被挡僵直 / 晕 / 特写和这边已经结束掉的那一下开始时刻差这么多以内,算同一下

typedef enum { GP_WAIT = 0, GP_PICK, GP_COUNT, GP_PLAY, GP_END, GP_OVER } fight_phase_t;
typedef enum { GE_PICK = 1, GE_PEER_PICK, GE_ROUND, GE_FIGHT, GE_SIM, GE_ROUND_END, GE_OVER, GE_BAD_VER } fight_gev_t;
typedef bool (*fight_send_fn)(void *ctx, const void *msg, size_t len);  // false = 现在发不出去,过会儿再发
typedef uint32_t (*fight_rnd_fn)(void);

typedef struct { uint8_t key; uint32_t at; } fight_tkey_t;  // 一个键和它该喂给规则的时刻(now)

typedef struct {
    fight_send_fn send; void *ctx; fight_rnd_fn rnd;
    bool initiator, up, bad_ver;
    uint8_t phase;            // fight_phase_t
    uint32_t match; uint8_t round, score_me, score_peer;
    uint8_t me_char, peer_char; bool me_picked, peer_picked;
    fight_sim_t sim;          // 主动方:真的;被连方:照快照填的
    uint8_t me;               // sim.f[] 里哪个是自己(主动方 0、被连方 1)

    bool selecting;           // 在选人(主动方断线重连时据此重发 'S')
    bool me_again, peer_again;
    bool round_done;          // 主动方:这一回合判完了(比分算过),连回来开 next_round
    uint8_t next_round;
    uint16_t gauge[2];        // 这一回合开始时两人的气(sim 的下标);作废重开用它,回合打完换成那时的气
    uint32_t now;             // 毫秒,10 ms 一步
    uint32_t acc;             // tick 里还不够一步的毫秒
    uint32_t phase_ms;        // 倒数 / 回合结束后还要等多久

    uint8_t out[FIGHT_OUTQ][FIGHT_MSG_MAX];  // 排着要交给 send 的消息(快照、'K'、'Q' 不进这里:发的时候现打包)
    uint8_t out_len[FIGHT_OUTQ], out_head, out_n;
    uint8_t keep[2][FIGHT_MSG_MAX], keep_len[2], keep_n;  // 排满时留着的 'N' / 'O'(不能丢)
    uint16_t dropped;         // 排满丢掉的消息数(测试里应该一直是 0)
    uint32_t link_free;       // 估计链路什么时候空下来
    uint32_t hand_at;         // 最近一次交给 send 的时刻
    uint16_t busy;            // 估计链路一片要多久(量到的来回 × 9/8,拥堵时加大);0 = 还没定
    uint8_t busy_run;         // 连着交出去了几条(收回拥堵的估计用)
    bool snap_due;            // 主动方:有事件,快照尽快发
    uint32_t snap_at;         // 主动方:上一条快照交出去的时刻

    uint8_t ping_id; bool ping_wait; uint32_t ping_at, ping_next;
    uint16_t rtt; bool rtt_ok;  // 平滑来回(毫秒,新的一次占 1/4)
    uint16_t delay;           // 主动方自己的键延后多久

    fight_tkey_t own[FIGHT_KEYQ]; uint8_t own_n;        // 主动方:自己的键,到时刻喂 f[0]
    fight_tkey_t remote[FIGHT_KEYQ]; uint8_t remote_n;  // 主动方:被连方的键,排到本来的时刻喂 f[1]
    uint8_t rx_seq; uint32_t rx_last;                   // 主动方:收到的最后一条 'K' 的序号、上一键的生效时刻
    uint8_t mash_got;                                   // 主动方:被连方这次晕已经算进去几下

    fight_kpair_t kq[FIGHT_KEYQ]; uint8_t kq_n;         // 被连方:还没发出去的键
    uint8_t tx_seq, acked;                              // 被连方:发出去的最后一条 'K'、快照确认到哪条
    bool key_any; uint32_t key_at;                      // 被连方:上一键的时刻
    bool down_last; uint32_t down_at;                   // 被连方:上一个键(不算长按)是 ▼、最近一次 ▼ 的时刻(先画小招用)
    uint8_t mash_n, mash_sent; uint16_t mash_cool;      // 被连方:这次晕狂按几下、发出去几下、多久后能再发 'M'
    bool predict; uint8_t predict_left, predict_seq;    // 被连方:自己先画的招还没被快照算到(在 kq 第几个 / 哪条 'K')
    fight_snap_t snap; bool snap_ok;                    // 被连方:这一回合最近收到的快照(原样)
    uint8_t ahead;                                      // 被连方:快照往前推几 ms('N' 带来的)
    uint8_t broke;                                      // 被连方:'E' 说盾被砸开了、掉血还没在快照里看到的人(位 = sim 下标)
    struct { uint8_t st, move; uint32_t at; } gone[2];  // 被连方:这边结束掉的最近一下出招 / 被挡僵直 / 晕 / 特写(开始时刻,now)

    struct { uint8_t gev; fight_ev_t ev; } evq[FIGHT_GEVQ];
    uint8_t ev_head, ev_n;
} fight_game_t;

void fight_game_init(fight_game_t *g, fight_send_fn send, void *ctx, fight_rnd_fn rnd);
void fight_game_connected(fight_game_t *g, bool initiator);  // 新的一次连接:发 'H'
void fight_game_lost(fight_game_t *g);                       // 断了:这一回合作废,比分和气留着
void fight_game_on_msg(fight_game_t *g, const uint8_t *m, size_t n);
void fight_game_tick(fight_game_t *g, uint32_t dt);          // 推时间(10 ms 一步),把排着的消息交给 send
void fight_game_key(fight_game_t *g, uint8_t key);           // fight_key_t,自己的
bool fight_game_pick(fight_game_t *g, uint8_t ch);           // 选人页确定;返回 true = 收下
bool fight_game_unpick(fight_game_t *g);                     // 确定后撤回
void fight_game_again(fight_game_t *g);                      // 结束页「再来一场」
bool fight_game_event(fight_game_t *g, uint8_t *gev, fight_ev_t *sim_ev);  // GE_SIM 时 sim_ev 有内容
