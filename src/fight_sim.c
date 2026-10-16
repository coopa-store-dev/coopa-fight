// src/fight_sim.c —— 见 fight_sim.h。
#include "fight_sim.h"

#include <string.h>

#define NO_DOWN UINT32_MAX   // down_at:这回合还没按过 ▼

static const fight_move_def_t *mv(const fight_fighter_t *f, uint8_t m) { return &fight_char(f->ch)->mv[m]; }

// 事件满了先丢最旧的打中 / 挡住(只影响声音和闪光),再丢别的;KO、时间到不丢。
static void event(fight_sim_t *s, uint8_t kind, uint8_t who, uint8_t move) {
    if (s->evn == FIGHT_EVQ) {
        int drop = -1;
        for (int i = 0; i < s->evn && drop < 0; i++)
            if (s->ev[i].kind == FE_HIT || s->ev[i].kind == FE_BLOCK) drop = i;
        for (int i = 0; i < s->evn && drop < 0; i++)
            if (s->ev[i].kind != FE_KO && s->ev[i].kind != FE_TIMEUP) drop = i;
        if (drop < 0) return;  // 全是 KO / 时间到:一回合最多两条,满不了
        memmove(&s->ev[drop], &s->ev[drop + 1], (size_t)(s->evn - drop - 1) * sizeof s->ev[0]);
        s->evn--;
    }
    s->ev[s->evn++] = (fight_ev_t){ kind, who, move };
}

static void add_gauge(fight_sim_t *s, uint8_t who, uint16_t n) {
    fight_fighter_t *f = &s->f[who];
    if (f->st == FS_DIZZY || s->freeze) return;  // 晕着、特写时不涨
    f->gauge = f->gauge + n > FIGHT_GAUGE_MAX ? FIGHT_GAUGE_MAX : (uint16_t)(f->gauge + n);
}

static int32_t per_mille(const fight_fighter_t *f) { return (int32_t)f->hp * 1000 / fight_char(f->ch)->hp; }

static void take_dmg(fight_fighter_t *f, uint16_t dmg) { f->hp = (int16_t)(f->hp > dmg ? f->hp - dmg : 0); }

// 挨打涨气(扣完血再比:落后的一方多涨)
static void hurt_gauge(fight_sim_t *s, uint8_t who) {
    uint16_t n = FIGHT_GAUGE_HURT;
    if (per_mille(&s->f[!who]) - per_mille(&s->f[who]) >= FIGHT_BEHIND) n += FIGHT_GAUGE_BEHIND;
    add_gauge(s, who, n);
}

// 换状态。放下盾(到时间、出招、被砸开都算)就进冷却。
static void enter(fight_fighter_t *f, uint8_t st, uint16_t dur) {
    if (f->st == FS_GUARD && st != FS_GUARD) {
        f->guard_cool = FIGHT_GUARD_COOL;
        f->long_guard = false;
    }
    f->st = st;
    f->t = 0;
    f->dur = dur;
}

static void end_chain(fight_fighter_t *f) {
    f->chain = 0;
    f->chain_open = false;
    f->chain_until = 0;
}

// 挨了一下:僵住,提前按的键和自己的连招作废
static void stun(fight_fighter_t *f, uint16_t dur) {
    enter(f, FS_HITSTUN, dur);
    f->qn = 0;
    end_chain(f);
}

static void push(fight_fighter_t *f, uint8_t e) {
    if (f->qn < sizeof f->queue) f->queue[f->qn++] = e;  // 多的丢
}

static uint8_t pop(fight_fighter_t *f) {
    uint8_t e = f->queue[0];
    memmove(f->queue, f->queue + 1, --f->qn);
    return e;
}

static void start_move(fight_sim_t *s, uint8_t who, uint8_t m) {
    fight_fighter_t *f = &s->f[who];
    const fight_move_def_t *d = mv(f, m);
    enter(f, FS_MOVE, (uint16_t)(d->startup + d->active + d->recovery));
    f->move = m;
    event(s, FE_START, who, m);
}

// 连招的下一下:对方一直僵到这一下打到(连上)
static void start_chain(fight_sim_t *s, uint8_t who, uint8_t m) {
    fight_fighter_t *f = &s->f[who], *o = &s->f[!who];
    start_move(s, who, m);
    f->chain_open = false;
    f->chain_until = 0;
    uint16_t need = (uint16_t)(o->t + mv(f, m)->startup);
    if (o->st == FS_HITSTUN && o->dur < need) o->dur = need;
}

// ● / ▲ 按下时(本来的时刻 te)在不在 ▼ 前缀里
static bool prefixed(const fight_fighter_t *f, uint8_t key, uint32_t te) {
    if (key == FK_UP && f->st == FS_GUARD && f->long_guard) return true;
    return f->down_at != NO_DOWN && te >= f->down_at && te - f->down_at <= FIGHT_PREFIX_MS;
}

// 这个键(本来的时刻 te)接不接连招:上一下连招打中了,对方那时还僵着,自己不在下一下的出手里
// (chain_until 只在连招的一下打中时设,出下一下就清掉,所以 FS_MOVE 一定是打中以后的收招)。
static bool chain_ok(const fight_fighter_t *f, uint32_t te) {
    return f->chain_until && te < f->chain_until && (f->st == FS_MOVE || f->st == FS_IDLE);
}

// 不在连招里的一个键(f 在 FS_IDLE 或 FS_GUARD)
static void apply_key(fight_sim_t *s, uint8_t who, uint8_t e) {
    fight_fighter_t *f = &s->f[who];
    uint8_t key = e & (uint8_t)~FIGHT_Q_PREFIX;
    if (key == FK_DOWN) {
        if (f->st == FS_GUARD || f->guard_cool) return;  // 举着不重新举;冷却里丢掉
        end_chain(f);
        enter(f, FS_GUARD, fight_char(f->ch)->guard_ms);
        return;
    }
    end_chain(f);
    // ▲ 带前缀是小招;小招还没做,先当重击
    start_move(s, who, key == FK_UP ? FM_HEAVY : FM_JAB);
}

static void drain_idle(fight_sim_t *s, uint8_t who) {
    fight_fighter_t *f = &s->f[who];
    while (f->qn && (f->st == FS_IDLE || f->st == FS_GUARD)) apply_key(s, who, pop(f));
}

// 连招里排着的键:● 再一拳(第三拳收尾),▲ 连段重击(收尾);
// ▼ 先等着:后面跟着前缀里的 ● / ▲ 就只是前缀,FIGHT_PREFIX_MS 过了没跟键就举盾(连招到此为止)。
static void drain_chain(fight_sim_t *s, uint8_t who) {
    fight_fighter_t *f = &s->f[who];
    while (f->qn) {
        uint8_t e = f->queue[0], key = e & (uint8_t)~FIGHT_Q_PREFIX;
        if (key == FK_DOWN) {
            if (f->qn > 1 && (f->queue[1] & FIGHT_Q_PREFIX)) {
                pop(f);
                continue;
            }
            if (f->qn == 1 && s->clock - f->down_at <= FIGHT_PREFIX_MS) return;
            apply_key(s, who, pop(f));
            drain_idle(s, who);
            return;
        }
        pop(f);
        // 带前缀的 ● 是大招、▲ 是小招;还没做,先当普通的
        start_chain(s, who, key == FK_UP ? FM_HEAVY_LINK : f->chain >= 2 ? FM_JAB3 : FM_JAB2);
        return;
    }
}

static bool armored(const fight_fighter_t *f) {
    const fight_move_def_t *d = mv(f, f->move);
    return f->st == FS_MOVE && d->armor_from && f->t >= d->armor_from && f->t < d->startup;
}

// 打中以后自己的连招:轻拳、第二拳打中开窗口;第三拳、连段重击收尾;别的招不开连招。
static void landed(fight_sim_t *s, uint8_t who, uint16_t hitstun) {
    fight_fighter_t *f = &s->f[who];
    switch (f->move) {
    case FM_JAB:
    case FM_JAB2:
        f->chain = f->move == FM_JAB ? 1 : (uint8_t)(f->chain + 1);
        f->chain_open = true;
        f->chain_until = s->clock + hitstun;
        break;
    case FM_JAB3:
    case FM_HEAVY_LINK:
        f->chain++;
        f->chain_open = false;
        f->chain_until = 0;
        break;
    default:
        end_chain(f);
        break;
    }
}

// who 的招这一步打到对方
static void resolve_contact(fight_sim_t *s, uint8_t who) {
    fight_fighter_t *a = &s->f[who], *d = &s->f[!who];
    const fight_move_def_t *md = mv(a, a->move);
    if (d->st == FS_GUARD && !md->breaks_guard) {
        event(s, FE_BLOCK, who, a->move);
        add_gauge(s, who, FIGHT_GAUGE_BLOCK);
        add_gauge(s, !who, FIGHT_GAUGE_BLOCK);
        end_chain(a);
        enter(a, FS_BLOCKSTUN, (uint16_t)(a->dur - a->t + FIGHT_BLOCKSTUN));
        return;
    }
    bool brk = d->st == FS_GUARD, armor = armored(d);
    event(s, brk ? FE_GUARD_BREAK : FE_HIT, who, a->move);
    take_dmg(d, md->dmg);
    add_gauge(s, who, FIGHT_GAUGE_HIT);
    hurt_gauge(s, !who);
    if (armor) {  // 重击后半段:只扣血,不打断
        end_chain(a);
        return;
    }
    stun(d, md->hitstun);  // 砸开防御以后要晕;晕还没做,先当僵住
    landed(s, who, md->hitstun);
}

// 同一步两边都打到:各吃一下、都僵住,连招都作废
static void trade(fight_sim_t *s) {
    fight_fighter_t *f0 = &s->f[0], *f1 = &s->f[1];
    const fight_move_def_t *m0 = mv(f0, f0->move), *m1 = mv(f1, f1->move);
    event(s, FE_TRADE, 0, f0->move);
    take_dmg(f0, m1->dmg);
    take_dmg(f1, m0->dmg);
    for (uint8_t i = 0; i < 2; i++) {
        add_gauge(s, i, FIGHT_GAUGE_HIT);
        hurt_gauge(s, i);
    }
    stun(f0, m1->hitstun);
    stun(f1, m0->hitstun);
}

static bool contact_now(const fight_fighter_t *f) {
    return f->st == FS_MOVE && f->t == mv(f, f->move)->startup;
}

static void tick(fight_fighter_t *f) {
    if (f->t <= UINT16_MAX - FIGHT_STEP) f->t += FIGHT_STEP;
    f->guard_cool = f->guard_cool > FIGHT_STEP ? (uint16_t)(f->guard_cool - FIGHT_STEP) : 0;
}

// 到时间的状态回 FS_IDLE;连招窗口关掉(晚到的键还能按本来的时刻补 FIGHT_LATE_MAX)
static void advance_state(fight_sim_t *s, uint8_t who) {
    fight_fighter_t *f = &s->f[who];
    switch (f->st) {
    case FS_MOVE:
    case FS_GUARD:
    case FS_HITSTUN:
    case FS_BLOCKSTUN:
        if (f->t >= f->dur) enter(f, FS_IDLE, 0);
        break;
    default:
        break;
    }
    if (f->chain_until && s->clock >= f->chain_until) {
        f->chain_open = false;
        if (s->clock >= f->chain_until + FIGHT_LATE_MAX) end_chain(f);
    }
}

static bool check_ko(fight_sim_t *s) {
    bool ko0 = s->f[0].hp <= 0, ko1 = s->f[1].hp <= 0;
    if (!ko0 && !ko1) return false;
    s->winner = ko0 && ko1 ? FIGHT_DRAW : ko0 ? 1 : 0;
    for (uint8_t i = 0; i < 2; i++) {
        if (s->f[i].hp > 0) continue;
        enter(&s->f[i], FS_KO, 0);
        s->f[i].qn = 0;
        end_chain(&s->f[i]);
        event(s, FE_KO, i, s->f[!i].move);
    }
    return true;
}

// 时间到:按血占满血的比例比(交叉相乘,不丢精度)
static void time_up(fight_sim_t *s) {
    int32_t a = (int32_t)s->f[0].hp * fight_char(s->f[1].ch)->hp;
    int32_t b = (int32_t)s->f[1].hp * fight_char(s->f[0].ch)->hp;
    s->winner = a > b ? 0 : b > a ? 1 : FIGHT_DRAW;
    event(s, FE_TIMEUP, s->winner, 0);
}

void fight_sim_init(fight_sim_t *s, uint8_t ch0, uint8_t ch1, uint16_t gauge0, uint16_t gauge1) {
    memset(s, 0, sizeof *s);
    uint8_t ch[2] = { ch0 < FC_COUNT ? ch0 : FC_KID, ch1 < FC_COUNT ? ch1 : FC_KID };
    uint16_t g[2] = { gauge0, gauge1 };
    for (int i = 0; i < 2; i++) {
        fight_fighter_t *f = &s->f[i];
        f->ch = ch[i];
        f->st = FS_IDLE;
        f->hp = (int16_t)fight_char(ch[i])->hp;
        f->gauge = g[i] > FIGHT_GAUGE_MAX ? FIGHT_GAUGE_MAX : g[i];
        f->down_at = NO_DOWN;
    }
    s->winner = FIGHT_UNDECIDED;
}

void fight_sim_key(fight_sim_t *s, uint8_t who, uint8_t key) { fight_sim_key_at(s, who, key, 0); }

void fight_sim_key_at(fight_sim_t *s, uint8_t who, uint8_t key, uint16_t late_ms) {
    if (who > 1 || key > FK_DOWN_LONG || s->winner != FIGHT_UNDECIDED) return;
    fight_fighter_t *f = &s->f[who];
    if (late_ms > FIGHT_LATE_MAX) late_ms = FIGHT_LATE_MAX;
    uint32_t te = s->clock > late_ms ? s->clock - late_ms : 0;
    if (key == FK_DOWN_LONG) {  // 长按只管撑盾:盾还举着就撑到 guard_long_ms
        if (f->st == FS_GUARD) {
            f->dur = fight_char(f->ch)->guard_long_ms;
            f->long_guard = true;
        }
        return;
    }
    uint8_t e = key;
    if (key == FK_DOWN) f->down_at = te;  // 每次 ▼ 都记(前缀),举不举盾另算
    else if (prefixed(f, key, te)) e |= FIGHT_Q_PREFIX;
    if (f->st == FS_HITSTUN || f->st == FS_KO) return;  // 挨打僵着按的不算
    push(f, e);  // 出招中 / 收招中 / 被挡僵着:排着;能动时马上出
    if (chain_ok(f, te)) drain_chain(s, who);
    else drain_idle(s, who);
}

void fight_sim_step(fight_sim_t *s) {
    if (s->winner != FIGHT_UNDECIDED) return;
    s->clock += FIGHT_STEP;
    tick(&s->f[0]);
    tick(&s->f[1]);
    bool c0 = contact_now(&s->f[0]), c1 = contact_now(&s->f[1]);
    if (c0 && c1) trade(s);
    else if (c0) resolve_contact(s, 0);
    else if (c1) resolve_contact(s, 1);
    if (check_ko(s)) return;
    for (uint8_t i = 0; i < 2; i++) advance_state(s, i);
    for (uint8_t i = 0; i < 2; i++) {
        if (chain_ok(&s->f[i], s->clock)) drain_chain(s, i);
        else drain_idle(s, i);
    }
    if (s->clock >= FIGHT_ROUND_MS) time_up(s);
}

bool fight_sim_event(fight_sim_t *s, fight_ev_t *out) {
    if (!s->evn) return false;
    *out = s->ev[0];
    memmove(s->ev, s->ev + 1, --s->evn * sizeof s->ev[0]);
    return true;
}
