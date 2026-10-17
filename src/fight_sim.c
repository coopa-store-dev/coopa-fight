// src/fight_sim.c —— 见 fight_sim.h。
#include "fight_sim.h"

#include <string.h>

#define NO_DOWN UINT32_MAX   // down_at:这回合还没按过 ▼

static const fight_move_def_t *mv(const fight_fighter_t *f, uint8_t m) { return &fight_char(f->ch)->mv[m]; }

static uint8_t qkey(uint8_t e) { return e & (uint8_t)~(FIGHT_Q_PREFIX | FIGHT_Q_NOSUPER | FIGHT_Q_LONG); }

static bool is_jab(uint8_t m) { return m == FM_JAB || m == FM_JAB2 || m == FM_JAB3; }

// 正在出小招(爷爷太极的架势也算):小招冷却这时不减,出完才开始减
static bool in_special(const fight_fighter_t *f) {
    return f->st == FS_STANCE || (f->st == FS_MOVE && f->move == FM_SPECIAL);
}

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
    if (s->f[0].st == FS_DIZZY || s->f[1].st == FS_DIZZY || s->freeze) return;  // 有人晕着、特写时两边都不涨
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

// 晕:保护开始(这次晕到醒后第一次能动为止不再晕、不吃大招)
static void dizzy(fight_sim_t *s, uint8_t who, uint16_t dur) {
    fight_fighter_t *f = &s->f[who];
    enter(f, FS_DIZZY, dur);
    f->mash_left = dur;
    f->mash_n = 0;
    f->protect = true;
    f->qn = 0;
    end_chain(f);
    event(s, FE_DIZZY, who, 0);
}

// 狂按到 0 或自己晕完:醒,FIGHT_WAKE_EAT 内不收键也不挨打
static void wake(fight_sim_t *s, uint8_t who) {
    fight_fighter_t *f = &s->f[who];
    enter(f, FS_IDLE, 0);
    f->mash_left = 0;
    f->eat_keys = FIGHT_WAKE_EAT;
    event(s, FE_WAKE, who, 0);
}

// 晕着按一下(三个键都算):滚动 FIGHT_MASH_WINDOW 里最多算 FIGHT_MASH_PER_SEC 下
static void mash(fight_sim_t *s, uint8_t who, uint32_t te) {
    fight_fighter_t *f = &s->f[who];
    uint8_t i = (uint8_t)(f->mash_n % FIGHT_MASH_PER_SEC);  // 环里最旧的一下(满了以后)
    if (f->mash_n >= FIGHT_MASH_PER_SEC && te < f->mash_at[i] + FIGHT_MASH_WINDOW) return;
    f->mash_at[i] = te;
    f->mash_n++;
    f->mash_left = f->mash_left > FIGHT_MASH_STEP ? (uint16_t)(f->mash_left - FIGHT_MASH_STEP) : 0;
    if (!f->mash_left) wake(s, who);
}

// 对方进 st(僵直 / 大招特写)。不在僵直里说明是晚到的连招键:他其实还没缓过来,
// 这段时间里起的动作作废,举过的盾、出过的小招也不进冷却。
static void pull_back(fight_fighter_t *o, uint8_t st, uint16_t dur) {
    bool guard = o->st == FS_GUARD, special = in_special(o);
    enter(o, st, dur);
    o->qn = 0;
    end_chain(o);
    if (guard) o->guard_cool = 0;
    if (special) o->special_cool = 0;
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

// 连招的下一下:对方一直僵到这一下打到(连上);晚到的键对方已经缓过来了就拉回僵直
static void start_chain(fight_sim_t *s, uint8_t who, uint8_t m) {
    fight_fighter_t *f = &s->f[who], *o = &s->f[!who];
    start_move(s, who, m);
    f->chain_open = false;
    f->chain_until = 0;
    uint16_t startup = mv(f, m)->startup;
    if (o->st != FS_HITSTUN) pull_back(o, FS_HITSTUN, startup);
    else if (o->dur < o->t + startup) o->dur = (uint16_t)(o->t + startup);
}

// 小招。爷爷的太极是架势(FS_STANCE),连招里出也一样(不连上,等对方打过来)。
// nosuper = 拿它起手的这一套不能有大招(对方在保护里、或者按的时候在保护里)。
static void start_special(fight_sim_t *s, uint8_t who, bool in_chain, bool nosuper) {
    fight_fighter_t *f = &s->f[who];
    f->special_cool = FIGHT_SPECIAL_COOL;
    if (f->ch == FC_GRANDPA) {
        end_chain(f);
        f->chain_nosuper = nosuper;  // 架势是新的起手(接住后才开连招)
        enter(f, FS_STANCE, FIGHT_STANCE_MS);
        f->move = FM_SPECIAL;
        event(s, FE_START, who, FM_SPECIAL);
    } else if (in_chain) {
        start_chain(s, who, FM_SPECIAL);
    } else {
        start_move(s, who, FM_SPECIAL);
    }
}

// 大招:扣 1 格,特写冻结;冻结完(super_land)才扣血、对方晕。特写前排着的键两边都作废。
static void start_super(fight_sim_t *s, uint8_t who) {
    fight_fighter_t *f = &s->f[who];
    f->gauge = (uint16_t)(f->gauge - FIGHT_GAUGE_BAR);
    end_chain(f);
    f->qn = 0;
    enter(f, FS_SUPER_OUT, FIGHT_SUPER_FREEZE);
    f->move = FM_SUPER;
    pull_back(&s->f[!who], FS_SUPER_IN, FIGHT_SUPER_FREEZE);
    s->freeze = FIGHT_SUPER_FREEZE;
    event(s, FE_SUPER, who, FM_SUPER);
}

static void super_land(fight_sim_t *s) {
    uint8_t who = s->f[0].st == FS_SUPER_OUT ? 0 : 1;
    fight_fighter_t *f = &s->f[who], *o = &s->f[!who];
    take_dmg(o, mv(f, FM_SUPER)->dmg);  // 大招这一下不涨气
    event(s, FE_HIT, who, FM_SUPER);
    enter(f, FS_IDLE, 0);
    if (o->hp > 0) dizzy(s, !who, FIGHT_DIZZY_SUPER);
}

// ● / ▲ 按下时(本来的时刻 te)在不在 ▼ 前缀里:▼ 是紧挨着的上一个键、而且在 FIGHT_PREFIX_MS 内。
// ▲ 还有一种:按着 ▼ 再按 ▲(设计 §4)——上一个键是 ▼、盾还举着(长按撑着的盾也算)。按着和点一下在 0.5 秒长按之前分不出来。
static bool prefixed(const fight_fighter_t *f, uint8_t key, uint32_t te) {
    if (key == FK_UP && f->st == FS_GUARD && (f->long_guard || f->down_last)) return true;
    return f->down_last && te >= f->down_at && te - f->down_at <= FIGHT_PREFIX_MS;
}

// 这个键(本来的时刻 te)接不接连招:上一下连招打中了,对方那时还僵着,自己不在下一下的出手里
// (chain_until 只在连招的一下打中时设,出下一下就清掉,所以 FS_MOVE 一定是打中以后的收招)。
static bool chain_ok(const fight_fighter_t *f, uint32_t te) {
    return f->chain_until && te < f->chain_until && (f->st == FS_MOVE || f->st == FS_IDLE);
}

// 不在连招里的一个键(f 在 FS_IDLE 或 FS_GUARD)
static void apply_key(fight_sim_t *s, uint8_t who, uint8_t e) {
    fight_fighter_t *f = &s->f[who];
    uint8_t key = qkey(e);
    if (key == FK_DOWN) {
        if (f->st == FS_GUARD || f->guard_cool) return;  // 举着不重新举;冷却里丢掉
        end_chain(f);
        bool hold = e & FIGHT_Q_LONG;  // 排着的时候已经按成长按了
        enter(f, FS_GUARD, hold ? fight_char(f->ch)->guard_long_ms : fight_char(f->ch)->guard_ms);
        f->long_guard = hold;
        return;
    }
    end_chain(f);
    // 起手:对方这时在保护里、或者按的时候(本来的时刻)在保护里,这一套就不能有大招
    f->chain_nosuper = s->f[!who].protect || (e & FIGHT_Q_NOSUPER);
    if (key == FK_UP && (e & FIGHT_Q_PREFIX)) {  // ▼ ▲ 小招;冷却里 ▲ 不出
        if (!f->special_cool) start_special(s, who, false, f->chain_nosuper);
        return;
    }
    start_move(s, who, key == FK_UP ? FM_HEAVY : FM_JAB);
}

static void drain_idle(fight_sim_t *s, uint8_t who) {
    fight_fighter_t *f = &s->f[who];
    while (f->qn && (f->st == FS_IDLE || f->st == FS_GUARD)) apply_key(s, who, pop(f));
}

static uint8_t next_jab(const fight_fighter_t *f) { return f->chain >= 2 ? FM_JAB3 : FM_JAB2; }

// 连招里只排着一个 ▼,还在等后面前缀里的 ● / ▲:按 ▼ 后 FIGHT_PREFIX_MS 内、连招窗口没关。
// 晚到的 ▼(联机补算)两个期限都按真时钟多等 FIGHT_LATE_MAX:同一串里的下一个键也晚到,而且排在它后面,常常更晚。
static bool down_waits(const fight_sim_t *s, const fight_fighter_t *f) {
    return f->qn == 1 && qkey(f->queue[0]) == FK_DOWN && f->chain_until &&
           s->clock < f->chain_until + f->down_grace && s->clock <= f->down_at + FIGHT_PREFIX_MS + f->down_grace;
}

// 连招里 ▼ 后面跟着前缀里的 ● / ▲(▼ 已经拿掉,这里拿掉跟着的那个键):
//   ▼ ● → 大招(有气、这一套不是对着保护里的人打的、● 也不是对着保护按的);不能大招就只出拳;没气就是举盾、一拳。
//   ▼ ▲ → 小招连上;小招冷却里 ▼ 只管举盾、▲ 不出。
static void chain_after_down(fight_sim_t *s, uint8_t who) {
    fight_fighter_t *f = &s->f[who];
    uint8_t e = pop(f);
    bool nosuper = f->chain_nosuper || (e & FIGHT_Q_NOSUPER);
    if (qkey(e) == FK_OK && nosuper) {
        start_chain(s, who, next_jab(f));
    } else if (qkey(e) == FK_OK && f->gauge >= FIGHT_GAUGE_BAR) {
        start_super(s, who);
    } else if (qkey(e) == FK_UP && !f->special_cool) {
        start_special(s, who, true, s->f[!who].protect || (e & FIGHT_Q_NOSUPER));
    } else {
        apply_key(s, who, FK_DOWN);
        if (qkey(e) == FK_OK) apply_key(s, who, e);
        drain_idle(s, who);
    }
}

// 连招里排着的键:● 再一拳(第三拳收尾),▲ 连段重击(收尾);
// ▼ 先等着:后面跟着前缀里的 ● / ▲ 见 chain_after_down,FIGHT_PREFIX_MS 过了没跟键就举盾(连招到此为止)。
static void drain_chain(fight_sim_t *s, uint8_t who) {
    fight_fighter_t *f = &s->f[who];
    if (!f->qn) return;
    uint8_t e = f->queue[0], key = qkey(e);
    if (key == FK_DOWN) {
        if (f->qn > 1 && (f->queue[1] & FIGHT_Q_PREFIX)) {
            pop(f);
            chain_after_down(s, who);
            return;
        }
        if (down_waits(s, f)) return;
        apply_key(s, who, pop(f));
        drain_idle(s, who);
        return;
    }
    pop(f);
    start_chain(s, who, key == FK_UP ? FM_HEAVY_LINK : next_jab(f));
}

static bool armored(const fight_fighter_t *f) {
    const fight_move_def_t *d = mv(f, f->move);
    return f->st == FS_MOVE && d->armor_from && f->t >= d->armor_from && f->t < d->startup;
}

static void open_chain(fight_sim_t *s, fight_fighter_t *f, uint8_t chain, uint16_t hitstun) {
    f->chain = chain;
    f->chain_open = true;
    f->chain_until = s->clock + hitstun;
}

// 打中以后自己的连招:轻拳、第二拳、库巴的弹弓(不在连招里时)打中开窗口;
// 第三拳、连段重击、连招里的小招收尾;别的招不开连招。
static void landed(fight_sim_t *s, uint8_t who, uint16_t hitstun) {
    fight_fighter_t *f = &s->f[who];
    if (s->f[!who].protect) f->chain_nosuper = true;  // 打到保护里的人(比如晕着打醒):这一套不能有大招
    switch (f->move) {
    case FM_JAB:
    case FM_JAB2:
        open_chain(s, f, f->move == FM_JAB ? 1 : (uint8_t)(f->chain + 1), hitstun);
        break;
    case FM_SPECIAL:
        if (!f->chain && f->ch == FC_KID) open_chain(s, f, 1, hitstun);
        else if (f->chain) f->chain++;
        else end_chain(f);
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

// 爷爷架势里被打到:不扣血,打的人扣血僵住(不晕),爷爷的连招窗口打开
static void counter(fight_sim_t *s, uint8_t who) {
    fight_fighter_t *g = &s->f[who], *a = &s->f[!who];
    const fight_move_def_t *d = mv(g, FM_SPECIAL);
    event(s, FE_COUNTER, who, FM_SPECIAL);
    take_dmg(a, d->dmg);
    add_gauge(s, who, FIGHT_GAUGE_HIT);
    hurt_gauge(s, !who);
    stun(a, d->hitstun);
    a->hit2_left = 0;  // 锅盖被接住了,不再飞回来
    enter(g, FS_IDLE, 0);
    if (a->protect) g->chain_nosuper = true;
    open_chain(s, g, 1, d->hitstun);
}

// 盾刚举起来就挨到:弹开。打过来的人僵住(锅盖不再飞回来),弹开的人马上能动、盾不进冷却、白拿气,
// 连招窗口打开(下一拳接得上、▼ ● 能出大招;对方在保护里就不能大招)
static void parry(fight_sim_t *s, uint8_t who, uint8_t m) {
    fight_fighter_t *g = &s->f[who], *a = &s->f[!who];
    event(s, FE_PARRY, who, m);
    add_gauge(s, who, FIGHT_GAUGE_PARRY);
    stun(a, FIGHT_PARRY_STUN);
    a->hit2_left = 0;
    enter(g, FS_IDLE, 0);
    g->guard_cool = 0;
    g->chain_nosuper = a->protect;
    open_chain(s, g, 1, FIGHT_PARRY_STUN);
}

// who 的招 m 这一步打到对方。second = 奶奶锅盖飞回来的第二下(她自己这时不一定还在出招)。
static void resolve_contact(fight_sim_t *s, uint8_t who, uint8_t m, bool second) {
    fight_fighter_t *a = &s->f[who], *d = &s->f[!who];
    const fight_move_def_t *md = mv(a, m);
    if (!second && md->hits == 2) a->hit2_left = (uint16_t)(md->hit2_at - md->startup);
    if (d->eat_keys) {  // 刚醒:打不中
        event(s, FE_WHIFF, who, m);
        if (!second) end_chain(a);
        return;
    }
    if (d->st == FS_HITSTUN && !a->chain && !second) {  // 挨打僵着的人只吃这一套连招的下一下;另起的招打不中(防无限连)
        event(s, FE_WHIFF, who, m);
        return;
    }
    if (d->st == FS_STANCE) {
        counter(s, !who);
        return;
    }
    if (d->st == FS_GUARD && !second && d->t < FIGHT_PARRY_MS) {
        parry(s, !who, m);
        return;
    }
    if (d->st == FS_GUARD && !md->breaks_guard) {
        event(s, FE_BLOCK, who, m);
        add_gauge(s, who, FIGHT_GAUGE_BLOCK);
        add_gauge(s, !who, FIGHT_GAUGE_BLOCK);
        if (second) return;
        end_chain(a);
        enter(a, FS_BLOCKSTUN, (uint16_t)(a->dur - a->t + FIGHT_BLOCKSTUN));
        return;
    }
    bool brk = d->st == FS_GUARD, armor = armored(d) && is_jab(m);
    event(s, brk ? FE_GUARD_BREAK : FE_HIT, who, m);
    take_dmg(d, md->dmg);
    add_gauge(s, who, FIGHT_GAUGE_HIT);  // 对方晕着就不涨(add_gauge 里判)
    hurt_gauge(s, !who);
    if (armor) {  // 重击后半段扛得住轻拳:只扣血,不打断
        if (!second) end_chain(a);
        return;
    }
    if (d->st == FS_DIZZY) {  // 晕着挨打:立刻醒,照常吃这一下
        d->mash_left = 0;
        event(s, FE_WAKE, !who, 0);
    }
    // 砸开防御:晕;小招砸开、保护里砸开只僵不晕
    if (brk && m != FM_SPECIAL && !d->protect) dizzy(s, !who, FIGHT_DIZZY_BREAK);
    else stun(d, md->hitstun);
    if (!second) landed(s, who, md->hitstun);
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

static uint16_t dec(uint16_t v) { return v > FIGHT_STEP ? (uint16_t)(v - FIGHT_STEP) : 0; }

static void tick(fight_fighter_t *f) {
    if (f->t <= UINT16_MAX - FIGHT_STEP) f->t += FIGHT_STEP;
    f->guard_cool = dec(f->guard_cool);
    f->eat_keys = dec(f->eat_keys);
    if (f->st == FS_DIZZY) f->mash_left = dec(f->mash_left);
    if (!in_special(f)) f->special_cool = dec(f->special_cool);
}

// 锅盖这一步飞回来
static bool hit2_now(fight_fighter_t *f) {
    if (!f->hit2_left) return false;
    f->hit2_left = dec(f->hit2_left);
    return !f->hit2_left;
}

// 到时间的状态回 FS_IDLE;晕减到 0 就醒;太极没人打过来就收招;连招窗口关掉(晚到的键还能按本来的时刻补 FIGHT_LATE_MAX);
// 醒后第一次能动(FS_IDLE、不再吃键)保护消失。
static void advance_state(fight_sim_t *s, uint8_t who) {
    fight_fighter_t *f = &s->f[who];
    switch (f->st) {
    case FS_MOVE:
    case FS_GUARD:
    case FS_HITSTUN:
    case FS_BLOCKSTUN:
        if (f->t >= f->dur) enter(f, FS_IDLE, 0);
        break;
    case FS_DIZZY:
        if (!f->mash_left) wake(s, who);
        break;
    case FS_STANCE:
        if (f->t >= f->dur) {
            const fight_move_def_t *d = mv(f, FM_SPECIAL);
            uint16_t done = (uint16_t)(d->startup + FIGHT_STANCE_MS);
            event(s, FE_WHIFF, who, FM_SPECIAL);
            enter(f, FS_MOVE, (uint16_t)(done + d->recovery));
            f->t = done;
            f->move = FM_SPECIAL;
        }
        break;
    default:
        break;
    }
    if (f->protect && f->st == FS_IDLE && !f->eat_keys) {
        f->protect = false;
        f->protect_end = s->clock;
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
    if (who > 1 || key > FK_DOWN_LONG || s->winner != FIGHT_UNDECIDED || s->freeze) return;  // 特写时丢掉
    fight_fighter_t *f = &s->f[who];
    if (f->st == FS_KO || f->eat_keys) return;  // 倒了、刚醒:不收键(也不记 ▼)
    if (late_ms > FIGHT_LATE_MAX) late_ms = FIGHT_LATE_MAX;
    uint32_t te = s->clock > late_ms ? s->clock - late_ms : 0;
    if (f->st == FS_DIZZY) {  // 晕着:只算狂按
        if (key != FK_DOWN_LONG) mash(s, who, te);
        return;
    }
    if (key == FK_DOWN_LONG) {  // 长按只管撑盾:盾还举着就撑到 guard_long_ms;那个 ▼ 还排着就记在它身上,举盾时撑
        if (f->st == FS_GUARD) {
            f->dur = fight_char(f->ch)->guard_long_ms;
            f->long_guard = true;
        } else if (f->down_last && f->qn && qkey(f->queue[f->qn - 1]) == FK_DOWN) {
            f->queue[f->qn - 1] |= FIGHT_Q_LONG;
        }
        return;
    }
    uint8_t e = key;
    const fight_fighter_t *o = &s->f[!who];
    if (key == FK_DOWN) {
        f->down_at = te;  // 每次 ▼ 都记(前缀),举不举盾另算
        f->down_grace = late_ms ? FIGHT_LATE_MAX : 0;  // 晚到的:后面那个键可能比它还晚(排在它后面),最多晚这么多
    } else {
        if (prefixed(f, key, te)) e |= FIGHT_Q_PREFIX;
        // 按的时候(本来的时刻)对方在保护里:对他按的 ● ▼ ● 只出拳,拿这个键起手的一套也不能有大招
        if (o->protect || te < o->protect_end) e |= FIGHT_Q_NOSUPER;
    }
    f->down_last = key == FK_DOWN;
    if (f->st == FS_HITSTUN) return;  // 挨打僵着按的不算
    push(f, e);  // 出招中 / 收招中 / 被挡僵着:排着;能动时马上出
    if (chain_ok(f, te)) drain_chain(s, who);
    else drain_idle(s, who);
}

void fight_sim_step(fight_sim_t *s) {
    if (s->winner != FIGHT_UNDECIDED) return;
    if (s->freeze) {  // 特写:clock 不走,冻结完扣大招血
        s->freeze = dec(s->freeze);
        if (s->freeze) return;
        super_land(s);
        if (!check_ko(s) && s->clock >= FIGHT_ROUND_MS) time_up(s);
        return;
    }
    s->clock += FIGHT_STEP;
    tick(&s->f[0]);
    tick(&s->f[1]);
    bool c0 = contact_now(&s->f[0]), c1 = contact_now(&s->f[1]);
    bool h0 = hit2_now(&s->f[0]), h1 = hit2_now(&s->f[1]);
    if (c0 && c1) trade(s);
    else if (c0) resolve_contact(s, 0, s->f[0].move, false);
    else if (c1) resolve_contact(s, 1, s->f[1].move, false);
    if (h0) resolve_contact(s, 0, FM_SPECIAL, true);  // 锅盖第二下在这一步其他打到之后算
    if (h1) resolve_contact(s, 1, FM_SPECIAL, true);
    if (check_ko(s)) return;
    for (uint8_t i = 0; i < 2; i++) advance_state(s, i);
    for (uint8_t i = 0; i < 2; i++) {
        if (chain_ok(&s->f[i], s->clock) || down_waits(s, &s->f[i])) drain_chain(s, i);
        else drain_idle(s, i);
    }
    if (s->clock >= FIGHT_ROUND_MS && !s->freeze) time_up(s);
}

bool fight_sim_event(fight_sim_t *s, fight_ev_t *out) {
    if (!s->evn) return false;
    *out = s->ev[0];
    memmove(s->ev, s->ev + 1, --s->evn * sizeof s->ev[0]);
    return true;
}

uint16_t fight_mash_taken(const fight_sim_t *s, uint8_t who) { return who > 1 ? 0 : s->f[who].mash_n; }
