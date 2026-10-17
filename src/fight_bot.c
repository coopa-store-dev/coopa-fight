// src/fight_bot.c —— 见 fight_bot.h。
//
// 一步里做的事:
//   1. 看对方:起了什么手(拳 / 抡 / 防 / 小招,读习惯的记下来);晕了、被我挡住在僵着、摆太极架势了
//      → 过 react 才反应过来(晕了、僵着就去打;架势就先不打)。
//   2. 自己晕了(或者挨了大招、知道要晕):反应过来就一下一下狂按 ●。醒了先回过神(react)再出招。
//   3. 按着 ▼ 撑盾:0.5 秒时来一次长按,盾放下才松开,按着时别的键不按。
//   4. 手指按自己的节拍动(每 gap_ms 一次机会,小孩偶尔多停一下;每回合起点随机):只有节拍到点的那一步能按。
//      挨打僵着、特写时到点的那一下白按了(规则不收);出招、收招、被挡僵着时按的规则会排着。
//   5. 到点了:先按提前定好的一串键(● ▼ ●、● ● ●、▼ ● …);没有就按出招习惯挑下一个起手,定好后面接什么。
//      平时就这样跟着节拍一直按,不另外等反应;反应只花在看到变化以后(1、2)。
//   6. 弹开:看见对方起了来得及看清的招(离打到还有 PARRY_SEE 以上),按 p_parry 踩着点按 ▼(不等节拍);
//      自己的连招窗口开了(弹开、或者碰巧举早了弹开)而手上没有定好的键,反应过来就反打(有气就 ▼ ●)。
#include "fight_bot.h"

#include <string.h>

#define HABIT_N 20        // 记对方最近几次起手
#define HABIT_MIN 4       // 记够这么多次才开始克制
#define HABIT_BIAS 60     // 克制的分量(加到出招权重上,权重原本合计 100)
#define PLAN_MAX 4
#define PAUSE_MIN 100     // 小孩偶尔多停 100–200 ms
#define PAUSE_SPAN 100
#define HOLD_MS 500       // 按着 ▼ 这么久来一次长按(和按键组件一样)
#define COUNTER_MS 400    // 防住拳以后反打:举盾后这么久按 ●
#define STANCE_EARLY 100  // 对方的太极架势还剩这么久就出拳(拳打到时架势已经收了,打在收招上)
#define SUPER_WATCH 400   // ● ▼ ● 按完后再看这么久有没有出大招(统计)
#define PARRY_SEE 300     // 对方的招离打到还有这么久才来得及看清、踩点按 ▼
#define PARRY_AIM (FIGHT_PARRY_MS / 2)  // 踩点:打到前这么久按 ▼
#define PARRY_JABBER 500  // 对方最近的起手里拳占这么多(‰)以上:读得出他的节奏,连完三拳后踩下一拳的点

enum { H_JAB = 0, H_HEAVY, H_GUARD, H_SPECIAL, H_KINDS };  // 对方的起手(看得见的样子)
enum { R_NONE = 0, R_DIZZY, R_STANCE, R_BLOCKED, R_OPEN };  // 看到了、正在反应的事(R_BLOCKED:挡住了对方;R_OPEN:弹开了)

typedef struct {
    uint32_t since;        // 上一下按下到现在
    int32_t beat;          // 节拍:离下一次「能按一下」还有多久(一直在走,每 gap_ms 一次,小孩偶尔多停一会儿)
    int16_t hp_prev[2];    // 上一步看到的血:变多了 = 新的一回合
    int32_t wait;          // 还要等多久才出下一个起手(回过神 / 晕了开始狂按前的反应)
    int32_t react;         // 正在反应 react_kind:还剩多久反应过来
    uint8_t react_kind;
    uint8_t plan[PLAN_MAX];      // 定好的一串键
    uint16_t plan_gap[PLAN_MAX]; // 每一下离上一下至少多久(0 = 下一拍就按)
    uint8_t plan_n, plan_i;
    bool plan_hold;        // 这一串的 ▼ 要按着(撑盾)
    bool plan_super, super_hit;  // 这一串是 ● ▼ ●;● 打中了
    bool chain_prev;       // 上一步看到的 chain_open
    int16_t super_watch;
    bool holding;          // 正按着 ▼
    int16_t hold;          // 按着 ▼:还剩多久到长按(≤ 0 = 已经来过了)
    bool mashing;          // 在狂按(晕着 / 挨了大招)
    bool avoid;            // 对方在摆太极架势:先不打
    int16_t stance_early;  // 架势还剩这么久就出拳(负数 = 架势收了以后再等这么久)
    uint8_t opp_st, opp_move;  // 上一步看到的对方
    uint16_t opp_t;
    uint8_t habit[HABIT_N], habit_n, habit_i;
    int16_t parry;         // 还剩多久踩点按 ▼(0 = 没打算)
} brain_t;

_Static_assert(sizeof(brain_t) <= sizeof(((fight_bot_t *)0)->pad), "brain_t must fit in fight_bot_t.pad");

static brain_t *brain(fight_bot_t *b) { return (brain_t *)(void *)b->pad; }

// 真人模型(设计 §10、计划 Task 5):大人一秒 5 下、反应 0.65 秒;小孩一秒 3.5 下、反应 0.9 秒、按错多、偶尔停顿
const fight_bot_cfg_t fight_human_adult = { 650, 150, 200, 40, 25, 25, 10, 85, 90, 3, true, true, 0, 20, 50 };
const fight_bot_cfg_t fight_human_kid = { 900, 250, 286, 60, 15, 20, 5, 60, 60, 12, false, true, 15, 50, 20 };
// 电脑三档:简单 ≈ 小孩再慢一点(反应 1.1 秒、一秒 3 下、少接连招和大招);普通介于两者(不读习惯);
// 厉害 ≈ 大人但反应 0.5 秒。厉害打简单要赢 75%–95%(test_balance)
const fight_bot_cfg_t fight_bot_easy = { 1100, 300, 333, 60, 15, 20, 5, 50, 50, 15, false, true, 20, 50, 10 };
const fight_bot_cfg_t fight_bot_normal = { 775, 200, 243, 50, 20, 22, 8, 72, 75, 7, false, true, 7, 35, 30 };
const fight_bot_cfg_t fight_bot_hard = { 500, 150, 200, 40, 25, 25, 10, 85, 90, 3, true, true, 0, 20, 60 };

void fight_seen(const fight_sim_t *s, uint8_t me, fight_seen_t *out) {
    me &= 1;
    const fight_fighter_t *f[2] = { &s->f[me], &s->f[!me] };
    for (int i = 0; i < 2; i++) {
        const fight_fighter_t *x = f[i];
        out->ch[i] = x->ch;
        out->st[i] = x->st;
        out->move[i] = x->move;
        out->t[i] = x->t;
        out->hp[i] = x->hp;
        out->gauge[i] = x->gauge;
        if (x->st == FS_DIZZY) out->left[i] = x->mash_left;
        else if (x->st == FS_SUPER_IN || x->st == FS_SUPER_OUT) out->left[i] = s->freeze;
        else out->left[i] = x->st != FS_IDLE && x->st != FS_KO && x->dur > x->t ? (uint16_t)(x->dur - x->t) : 0;
    }
    // 被连方的画面里没有连招窗口(快照只带打中几下):自己这套打中过、对方正僵着,就是看得见的「打中了」
    out->chain_open = f[0]->chain_open || (f[0]->chain && f[1]->st == FS_HITSTUN);
    out->protect_peer = f[1]->protect;
}

static uint32_t roll(fight_bot_t *b, uint32_t n) { return n ? b->rnd() % n : 0; }

static bool chance(fight_bot_t *b, uint8_t pct) { return roll(b, 100) < pct; }

static int32_t react_time(fight_bot_t *b) {
    const fight_bot_cfg_t *c = b->cfg;
    return (int32_t)c->react_ms - c->react_jit + (int32_t)roll(b, 2u * c->react_jit + 1);
}

void fight_bot_init(fight_bot_t *b, const fight_bot_cfg_t *cfg, uint32_t (*rnd)(void)) {
    memset(b, 0, sizeof *b);
    b->cfg = cfg;
    b->rnd = rnd;
    brain_t *m = brain(b);
    m->since = UINT16_MAX;
    m->beat = (int32_t)roll(b, cfg->gap_ms) + 1;  // 节拍的起点随机
    m->opp_st = FS_IDLE;
}

// 按下一个键:可能按错(换成另外两个里的一个)
static int press(fight_bot_t *b, uint8_t key, bool can_wrong) {
    brain_t *m = brain(b);
    const fight_bot_cfg_t *c = b->cfg;
    if (can_wrong && chance(b, c->p_wrong)) key = (uint8_t)((key + 1 + roll(b, 2)) % 3);  // FK_UP / FK_DOWN / FK_OK
    m->since = 0;
    return key;
}

// 节拍往前走 dt:到点了 = 这一步有一次按的机会(按不按、能不能按另说),下一次在 gap_ms 后(小孩偶尔多停 100–200 ms)。
// 真人的手指是按自己的节奏动的:挨打僵着、特写时到点的那一下白按了(规则不收),不会正好在能动的第一刻补上。
static bool beat(fight_bot_t *b, uint32_t dt) {
    brain_t *m = brain(b);
    const fight_bot_cfg_t *c = b->cfg;
    m->beat -= (int32_t)dt;
    if (m->beat > 0) return false;
    m->beat = c->gap_ms + (chance(b, c->p_pause) ? PAUSE_MIN + (int32_t)roll(b, PAUSE_SPAN + 1) : 0);  // 不带零头:两下 ≥ gap_ms
    return true;
}

static void plan_clear(brain_t *m) {
    m->plan_n = m->plan_i = 0;
    m->plan_hold = false;
}

static void plan_add(brain_t *m, uint8_t key, uint16_t gap) {
    if (m->plan_n < PLAN_MAX) {
        m->plan[m->plan_n] = key;
        m->plan_gap[m->plan_n++] = gap;
    }
}

// ● 起手:站定了(不在出招 / 收招里、上一套没在连)、有气、对方不在保护里 → ● ▼ ●(大招);
// 不然可能接 ● ● 或者 ▲;都是提前按的。收招里提前按的 ● 可能接成上一套的第二、三拳,那时 ● ▼ ● 出不了大招,人也不会那样起大招。
static void plan_jab(fight_bot_t *b, const fight_seen_t *s) {
    brain_t *m = brain(b);
    const fight_bot_cfg_t *c = b->cfg;
    plan_clear(m);
    m->plan_super = false;
    plan_add(m, FK_OK, 0);
    bool settled = (s->st[0] == FS_IDLE || s->st[0] == FS_GUARD) && !s->chain_open;  // 站定了才起 ● ▼ ● 的节奏
    if (settled && s->gauge[0] >= FIGHT_GAUGE_BAR && !s->protect_peer && chance(b, c->p_super)) {
        plan_add(m, FK_DOWN, 0);
        plan_add(m, FK_OK, 0);
        m->plan_super = true;
        m->super_hit = false;
    } else if (chance(b, c->p_combo)) {
        if (chance(b, 50)) {
            plan_add(m, FK_OK, 0);
            plan_add(m, FK_OK, 0);
        } else {
            plan_add(m, FK_UP, 0);
        }
    }
}

// 弹开了(自己的连招窗口开着、对方僵着):有气、对方不在保护里就 ▼ ● 大招,不然 ● ● 或者 ▲
static void plan_open(fight_bot_t *b, const fight_seen_t *s) {
    brain_t *m = brain(b);
    plan_clear(m);
    m->plan_super = false;
    if (s->gauge[0] >= FIGHT_GAUGE_BAR && !s->protect_peer && chance(b, b->cfg->p_super)) {
        plan_add(m, FK_DOWN, 0);
        plan_add(m, FK_OK, 0);
    } else if (chance(b, 50)) {
        plan_add(m, FK_OK, 0);
        plan_add(m, FK_OK, 0);
    } else {
        plan_add(m, FK_UP, 0);
    }
}

// 对方晕着:抢着补一套 ● ● ●(他在保护里,出不了大招)
static void plan_punish(brain_t *m) {
    plan_clear(m);
    m->plan_super = false;
    for (int i = 0; i < 3; i++) plan_add(m, FK_OK, 0);
}

// 对方最近的起手里各种占多少(‰);记得太少返回 false
static bool habits(const brain_t *m, uint32_t per[H_KINDS]) {
    if (m->habit_n < HABIT_MIN) return false;
    uint32_t n[H_KINDS] = { 0 };
    for (int i = 0; i < m->habit_n; i++) n[m->habit[i]]++;
    for (int k = 0; k < H_KINDS; k++) per[k] = n[k] * 1000u / m->habit_n;
    return true;
}

// 挑一个起手。读习惯的:对方老出拳 → 多防(防住了反打);老防 → 多抡重击;老抡 → 早出拳
static void plan_opener(fight_bot_t *b, const fight_seen_t *s) {
    brain_t *m = brain(b);
    const fight_bot_cfg_t *c = b->cfg;
    uint32_t w[4] = { c->p_jab, c->p_heavy, c->p_guard, c->p_special }, per[H_KINDS] = { 0 };
    if (c->reads_habits && habits(m, per)) {
        w[0] += HABIT_BIAS * per[H_HEAVY] / 1000;
        w[1] += HABIT_BIAS * per[H_GUARD] / 1000;
        w[2] += HABIT_BIAS * per[H_JAB] / 1000;
    }
    uint32_t r = roll(b, w[0] + w[1] + w[2] + w[3]);
    if (r < w[0]) {
        plan_jab(b, s);
        return;
    }
    plan_clear(m);
    m->plan_super = false;
    r -= w[0];
    if (r < w[1]) {
        plan_add(m, FK_UP, 0);
    } else if (r < w[1] + w[2]) {
        plan_add(m, FK_DOWN, 0);
        if (chance(b, c->p_hold)) m->plan_hold = true;
        else if (c->reads_habits && roll(b, 1000) < per[H_JAB]) plan_add(m, FK_OK, COUNTER_MS);
    } else {
        plan_add(m, FK_DOWN, 0);
        plan_add(m, FK_UP, 0);
    }
}

// 看对方这一步起了什么手、晕没晕、摆没摆架势
static void watch(fight_bot_t *b, const fight_seen_t *s) {
    brain_t *m = brain(b);
    const fight_bot_cfg_t *c = b->cfg;
    uint8_t st = s->st[1], mv = s->move[1];
    bool fresh = st != m->opp_st || (st == FS_MOVE && (mv != m->opp_move || s->t[1] < m->opp_t));
    if (fresh) {
        int h = -1;
        if (st == FS_GUARD) h = H_GUARD;
        else if (st == FS_STANCE) h = H_SPECIAL;
        else if (st == FS_MOVE && mv == FM_JAB) h = H_JAB;
        else if (st == FS_MOVE && mv == FM_HEAVY) h = H_HEAVY;
        else if (st == FS_MOVE && mv == FM_SPECIAL && m->opp_st != FS_STANCE) h = H_SPECIAL;  // 架势收招不算新的
        if (h >= 0 && c->reads_habits) {
            m->habit[m->habit_i] = (uint8_t)h;
            m->habit_i = (uint8_t)((m->habit_i + 1) % HABIT_N);
            if (m->habit_n < HABIT_N) m->habit_n++;
        }
        bool seeable = st == FS_MOVE && (mv == FM_HEAVY || mv == FM_SPECIAL);
        int32_t until = seeable ? (int32_t)fight_char(s->ch[1])->mv[mv].startup - s->t[1] : 0;  // 离打到还有多久
        uint32_t per[H_KINDS];
        if (st == FS_MOVE && mv == FM_JAB3 && c->reads_habits && habits(m, per) && per[H_JAB] >= PARRY_JABBER) {
            until = s->left[1] + fight_char(s->ch[1])->mv[FM_JAB].startup;  // 老出拳的:第三拳收完马上又是一拳,踩那一拳的点
        }
        if (until >= PARRY_SEE && chance(b, c->p_parry)) {  // 新看到的招重新踩点
            int32_t jit = c->react_jit / 2;
            int32_t at = until - PARRY_AIM - jit + (int32_t)roll(b, 2u * (uint32_t)jit + 1);
            m->parry = (int16_t)(at > 1 ? at : 1);
        }
        if (st == FS_DIZZY) {
            m->react_kind = R_DIZZY;
            m->react = react_time(b);
        } else if (st == FS_BLOCKSTUN) {
            m->react_kind = R_BLOCKED;
            m->react = react_time(b);
        } else if (st == FS_STANCE && c->reads_habits) {
            m->react_kind = R_STANCE;
            m->react = react_time(b);
        }
    }
    m->opp_st = st;
    m->opp_move = mv;
    m->opp_t = s->t[1];
}

// 统计 ● ▼ ●:起手的 ● 打中了(按了 ● 以后、最后一个 ● 之前,对方开始晃)算一次;之后看到自己放出大招算成了。
// 起手前对方就在晃(上一套还没完)、或者是最后那个 ● 打中的(前面打空了),都不算。
static void count_super(fight_bot_t *b, const fight_seen_t *s, uint32_t dt) {
    brain_t *m = brain(b);
    bool rise = s->chain_open && !m->chain_prev;
    m->chain_prev = s->chain_open;
    if (rise && s->st[1] == FS_HITSTUN && s->left[1] + 2 * FIGHT_STEP >= FIGHT_PARRY_STUN) rise = false;  // 弹开开的窗口不算 ● 打中
    if (!m->plan_super) return;
    if (rise && !m->super_hit && m->plan_i >= 1 && m->plan_i < m->plan_n) {
        m->super_hit = true;
        b->super_try++;
    }
    if (s->st[0] == FS_SUPER_OUT) {
        if (m->super_hit) b->super_ok++;
        m->plan_super = false;
    } else if (m->plan_i == m->plan_n && (m->super_watch -= (int16_t)dt) <= 0) {
        m->plan_super = false;
    }
}

// 定好的一串里的下一下(只在节拍到点时调):这一下要求离上一下更久(防住后反打)就等下一拍
static int press_plan(fight_bot_t *b) {
    brain_t *m = brain(b);
    if (m->since < m->plan_gap[m->plan_i]) return -1;
    int k = press(b, m->plan[m->plan_i++], true);
    if (k == FK_DOWN && m->plan_hold) {
        m->holding = true;
        m->hold = HOLD_MS;
    }
    if (m->plan_i == m->plan_n) m->super_watch = SUPER_WATCH;
    return k;
}

int fight_bot_step(fight_bot_t *b, const fight_seen_t *s, uint32_t dt) {
    brain_t *m = brain(b);
    const fight_bot_cfg_t *c = b->cfg;
    if (m->since < UINT16_MAX) m->since += dt;
    if (m->wait > 0) m->wait -= (int32_t)dt;
    if (s->hp[0] > m->hp_prev[0] || s->hp[1] > m->hp_prev[1]) {  // 血变多了:新的一回合,节拍重新随机起点
        m->beat = (int32_t)roll(b, c->gap_ms) + 1;
        plan_clear(m);
        m->react_kind = R_NONE;
        m->holding = m->avoid = m->mashing = false;
        m->wait = 0;
        m->parry = 0;
    }
    m->hp_prev[0] = s->hp[0];
    m->hp_prev[1] = s->hp[1];
    bool open_rise = s->chain_open && !m->chain_prev;
    watch(b, s);
    count_super(b, s, dt);
    bool tap = beat(b, dt);
    uint8_t me = s->st[0];
    if (me == FS_KO) {
        plan_clear(m);
        return -1;
    }
    if (me == FS_DIZZY || me == FS_SUPER_IN) {  // 晕了(挨了大招就知道要晕):反应过来就跟着节拍狂按
        plan_clear(m);
        m->parry = 0;
        m->holding = m->avoid = false;
        m->react_kind = R_NONE;
        if (!m->mashing) {
            m->mashing = true;
            m->wait = react_time(b);
        }
        if (!c->mash || m->wait > 0 || !tap) return -1;
        return press(b, FK_OK, false);
    }
    if (m->mashing) {  // 醒了:先回过神
        m->mashing = false;
        m->wait = react_time(b);
    }
    if (m->holding) {
        if (me != FS_GUARD) {
            m->holding = false;  // 盾放下了:松开
        } else {
            if (m->hold > 0 && (m->hold -= (int16_t)dt) <= 0) return FK_DOWN_LONG;
            return -1;  // 按着 ▼ 时别的键不按
        }
    }
    if (m->parry > 0 && (m->parry -= (int16_t)dt) <= 0) {  // 踩点按 ▼(不等节拍,但手指快不过 gap_ms);定好的键不按了
        if (m->since < c->gap_ms) {
            m->parry = 1;
        } else {
            m->parry = 0;
            if (me == FS_IDLE || me == FS_GUARD) {
                plan_clear(m);
                m->beat = c->gap_ms;  // 节拍从这一下重新数
                return press(b, FK_DOWN, false);
            }
        }
    }
    if (open_rise && me == FS_IDLE && m->plan_i == m->plan_n && s->st[1] == FS_HITSTUN) {  // 弹开了:反应一下就反打
        m->react_kind = R_OPEN;
        m->react = react_time(b) / 4;  // 自己踩的点:按下去就等着反打,「弹开!」又响又闪
    }
    if (m->react_kind && (m->react -= (int32_t)dt) <= 0) {  // 反应过来了
        uint8_t k = m->react_kind;
        m->react_kind = R_NONE;
        if (k == R_DIZZY && s->st[1] == FS_DIZZY) {
            plan_punish(m);
            m->wait = 0;
        } else if (k == R_BLOCKED && s->st[1] == FS_BLOCKSTUN && m->plan_i == m->plan_n) {
            plan_jab(b, s);  // 挡住了:对方还僵着,反打(有气就 ● ▼ ●)
            m->wait = 0;
        } else if (k == R_OPEN && s->chain_open && m->plan_i == m->plan_n) {
            plan_open(b, s);
            m->wait = 0;
        } else if (k == R_STANCE && s->st[1] == FS_STANCE) {
            if (m->plan_i == 0) plan_clear(m);  // 还没开始按的不按了
            m->avoid = true;
            m->stance_early = (int16_t)(STANCE_EARLY - c->react_jit + (int32_t)roll(b, 2u * c->react_jit + 1));
        }
    }
    if (m->avoid) {  // 等架势快收了再出拳,打在收招上(什么时候算「快收了」也有反应的误差)
        bool waiting = s->st[1] == FS_STANCE ? s->left[1] > m->stance_early
                                             : s->st[1] == FS_MOVE && s->move[1] == FM_SPECIAL && m->stance_early < 0 &&
                                                   s->t[1] < FIGHT_STANCE_MS - m->stance_early;
        if (waiting) return -1;
        m->avoid = false;
        if (m->plan_i == m->plan_n) {
            plan_jab(b, s);
            m->wait = 0;
        }
    }
    // 只在节拍到点时按;僵着、特写里到点的这一下白按了
    if (!tap || me == FS_HITSTUN || me == FS_SUPER_OUT) return -1;
    if (m->plan_i < m->plan_n) return press_plan(b);
    if (m->wait > 0 || m->parry > 0) return -1;  // 等着踩点按 ▼:先不起新的手
    // 能动、或者自己出招 / 收招 / 被挡僵着(按了规则会排着,收完再出)
    if (me != FS_IDLE && me != FS_GUARD && me != FS_MOVE && me != FS_BLOCKSTUN && me != FS_STANCE) return -1;
    plan_opener(b, s);
    return press_plan(b);
}
