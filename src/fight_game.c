// src/fight_game.c —— 见 fight_game.h。
#include "fight_game.h"

#include <string.h>

static bool over(const fight_game_t *g) { return g->score_me >= FIGHT_WIN || g->score_peer >= FIGHT_WIN; }

// ---- 事件 ----

static void gev(fight_game_t *g, uint8_t e, const fight_ev_t *se) {
    if (g->ev_n == FIGHT_GEVQ) {  // 满了丢最旧的:只影响声音和动画
        g->ev_head = (uint8_t)((g->ev_head + 1) % FIGHT_GEVQ);
        g->ev_n--;
    }
    uint8_t i = (uint8_t)((g->ev_head + g->ev_n) % FIGHT_GEVQ);
    g->evq[i].gev = e;
    g->evq[i].ev = se ? *se : (fight_ev_t){ 0, 0, 0 };
    g->ev_n++;
}

static void sim_gev(fight_game_t *g, uint8_t kind, uint8_t who, uint8_t move) {
    fight_ev_t e = { kind, who, move };
    gev(g, GE_SIM, &e);
}

// ---- 发送 ----

static void enqueue(fight_game_t *g, const uint8_t *m, size_t len) {
    if (!g->up) return;
    if (g->out_n == FIGHT_OUTQ) {  // 满了(正常打不满):丢掉并记数,断线重连时以主动方为准
        g->dropped++;
        return;
    }
    uint8_t i = (uint8_t)((g->out_head + g->out_n) % FIGHT_OUTQ);
    memcpy(g->out[i], m, len);
    g->out_len[i] = (uint8_t)len;
    g->out_n++;
}

// 'N' / 'O' 不能丢(丢了被连方会停在回合结束):排满了就先留着,pump 腾出地方再排
static void enqueue_keep(fight_game_t *g, const uint8_t *m, size_t len) {
    if (!g->up) return;
    if (g->out_n < FIGHT_OUTQ && !g->keep_n) {
        enqueue(g, m, len);
    } else if (g->keep_n < 2) {
        memcpy(g->keep[g->keep_n], m, len);
        g->keep_len[g->keep_n++] = (uint8_t)len;
    } else {
        g->dropped++;  // 一回合最多留着一条 'O' 一条 'N',到不了这里
    }
}

static void flush_keep(fight_game_t *g) {
    uint8_t i = 0;
    for (; i < g->keep_n && g->out_n < FIGHT_OUTQ; i++) enqueue(g, g->keep[i], g->keep_len[i]);
    g->keep_n = (uint8_t)(g->keep_n - i);
    if (i && g->keep_n) {  // 只排进去了第一条:第二条挪到前面
        memcpy(g->keep[0], g->keep[1], sizeof g->keep[0]);
        g->keep_len[0] = g->keep_len[1];
    }
}

// 链路一片要多久的基数:量到的平滑来回再多 1/8。量到的每一次都不会比那一片真用的短(排队只会更长),
// 平均起来不会低估;多 1/8 免得正好卡在满负荷上越排越长。
static uint16_t busy_base(const fight_game_t *g) {
    return (uint16_t)(g->rtt_ok ? g->rtt + g->rtt / 8u : 2u * FIGHT_LOCAL_DELAY);
}

// 交给 send。成功了就当链路又要忙一片的时间(停等:一片的确认回来才发下一片)。
// 回合里不 ping,链路变慢了量不到。send 被拒 = 链路里已经排满 FIGHT_LINK_Q 片(里面的快照都旧了):
// 一片的估计加到 3/2(这一次拥堵只加一次),快照先停到排着的都送完;之后连着成功 FIGHT_BUSY_RUN 次
// 才往量到的基数收回 1 ms(很慢:收快了又会排满),下次 ping 重新量。
static bool hand(fight_game_t *g, const uint8_t *m, size_t n) {
    if (!g->busy) g->busy = busy_base(g);
    if (!g->send(g->ctx, m, n)) {
        if (g->now >= g->link_free) {  // 新的一次拥堵(还在等排着的送完时再被拒不算)
            uint32_t up = g->busy * 3u / 2u;
            g->busy = (uint16_t)(up > FIGHT_BUSY_MAX ? FIGHT_BUSY_MAX : up);
            g->link_free = g->now + (uint32_t)FIGHT_LINK_Q * g->busy;
        }
        g->busy_run = 0;
        return false;
    }
    g->hand_at = g->now;
    uint32_t from = g->link_free > g->now ? g->link_free : g->now;
    g->link_free = from + g->busy;
    if (++g->busy_run >= FIGHT_BUSY_RUN) {
        g->busy_run = 0;
        if (g->busy > busy_base(g)) g->busy--;
    }
    return true;
}

static void send_snap(fight_game_t *g) {
    uint8_t m[FIGHT_SNAP_LEN];
    if (!hand(g, m, fight_snap_pack(m, &g->sim, g->rx_seq))) return;
    g->snap_due = false;
    g->snap_at = g->now;
}

// 平滑来回(新的一次占 1/4),主动方自己的键延后它的一半
static void rtt_sample(fight_game_t *g, uint32_t ms) {
    int32_t sample = ms > 2000 ? 2000 : (int32_t)ms;
    g->rtt = (uint16_t)(g->rtt_ok ? g->rtt + (sample - (int32_t)g->rtt) / 4 : sample);
    g->rtt_ok = true;
    g->busy = busy_base(g);  // 量到新的了:拥堵的估计作废
    uint16_t d = g->rtt / 2;
    g->delay = d < FIGHT_DELAY_MIN ? FIGHT_DELAY_MIN : d > FIGHT_DELAY_MAX ? FIGHT_DELAY_MAX : d;
}

// 估计的单程(平滑来回的一半;还没量到时 FIGHT_LOCAL_DELAY)
static uint32_t one_way(const fight_game_t *g) { return g->rtt_ok ? g->rtt / 2u : FIGHT_LOCAL_DELAY; }

// 被连方的快照往前推多少:推到「被连方现在按的键到主动方时,主动方看自己」的那一刻。被连方的键在路上一个单程,
// 主动方自己的键延后 delay:推 2 × 单程 − delay(delay 没被 30–150 限住时就是一个单程)。
static uint8_t ahead_of(const fight_game_t *g) {
    int32_t a = 2 * (int32_t)one_way(g) - (int32_t)g->delay;
    return (uint8_t)(a < 0 ? 0 : a > UINT8_MAX ? UINT8_MAX : a);
}

// 被连方:排着的键合并成 'K'(没被确认的不超过 FIGHT_K_INFLIGHT 条)
static void send_keys(fight_game_t *g) {
    while (g->kq_n && (uint8_t)(g->tx_seq - g->acked) < FIGHT_K_INFLIGHT) {
        uint8_t n = g->kq_n < FIGHT_K_MAX ? g->kq_n : FIGHT_K_MAX, m[FIGHT_MSG_MAX];
        if (!hand(g, m, fight_k_pack(m, (uint8_t)(g->tx_seq + 1), g->kq, n))) return;
        g->tx_seq++;
        g->kq_n = (uint8_t)(g->kq_n - n);
        memmove(g->kq, g->kq + n, g->kq_n * sizeof g->kq[0]);
        if (g->predict_left) {  // 先画的那个键在这一条里了
            if (g->predict_left <= n) g->predict_seq = g->tx_seq;
            g->predict_left = g->predict_left > n ? (uint8_t)(g->predict_left - n) : 0;
        }
    }
}

static void pump(fight_game_t *g) {
    if (!g->up) return;
    for (bool more = true; more;) {
        while (g->out_n && hand(g, g->out[g->out_head], g->out_len[g->out_head])) {
            g->out_head = (uint8_t)((g->out_head + 1) % FIGHT_OUTQ);
            g->out_n--;
        }
        more = g->keep_n && g->out_n < FIGHT_OUTQ;
        flush_keep(g);
    }
    if (g->out_n) return;  // 链路排满了:下一帧再发
    if (!g->initiator) {
        send_keys(g);
        return;
    }
    if (g->now < g->link_free) return;  // 快照只在估计链路空下来时交出去
    // 'Q':开打时不发('Q' 和回的 'A' 各占一片,会把快照和被连方的键挤后一个来回);别的时候等链路静下来
    // FIGHT_PING_QUIET 再发,量到的来回才不带排队(估计偏小时「估计空了」并不可靠)。回合之间也量(下一回合的 ahead
    // 早一回合跟上);'N' 排在路上的 'Q' 后面没关系,start_round 把链路还忙着的那段算进去了
    if (g->phase != GP_PLAY && !g->bad_ver && !g->ping_wait && g->now >= g->ping_next &&
        g->now - g->hand_at >= FIGHT_PING_QUIET) {
        uint8_t m[2] = { 'Q', (uint8_t)(g->ping_id + 1) };
        if (!hand(g, m, sizeof m)) return;
        g->ping_id++;
        g->ping_wait = true;
        g->ping_at = g->now;
        g->ping_next = g->now + FIGHT_PING_MS;
        return;  // 这一片占着链路,快照等下一次空
    }
    if (g->phase == GP_PLAY && (g->snap_due || g->now - g->snap_at >= FIGHT_SNAP_MS)) send_snap(g);
}

// ---- 选人、开回合、比分(照 coopa-bomb) ----

static void send_select(fight_game_t *g) {
    uint8_t m[5] = { 'S' };
    fight_put32(m + 1, g->match);
    enqueue(g, m, sizeof m);
}

static void send_pick(fight_game_t *g) {
    uint8_t m[6] = { 'P' };
    fight_put32(m + 1, g->match);
    m[5] = g->me_picked ? g->me_char : FIGHT_NOCHAR;
    enqueue(g, m, sizeof m);
}

// 'N':round = 0 表示这场打完了
static void send_round(fight_game_t *g, uint8_t round) {
    uint8_t m[15] = { 'N' };
    fight_put32(m + 1, g->match);
    m[5] = round;
    m[6] = g->score_me;
    m[7] = g->score_peer;
    m[8] = g->me_char;
    m[9] = g->peer_char;
    fight_put16(m + 10, g->gauge[0]);
    fight_put16(m + 12, g->gauge[1]);
    m[14] = ahead_of(g);
    enqueue_keep(g, m, sizeof m);
}

static void clear_keys(fight_game_t *g) {
    g->own_n = g->remote_n = g->kq_n = 0;
    g->mash_got = g->mash_n = g->mash_sent = 0;
    g->mash_cool = 0;
    g->predict = false;
    g->predict_left = 0;
    g->acked = g->tx_seq;
    g->down_last = false;
    g->broke = 0;
    memset(g->gone, 0, sizeof g->gone);
}

// 两边开一回合的同一段
static void begin(fight_game_t *g, uint8_t round) {
    g->round = round;
    g->round_done = false;
    uint8_t c0 = g->initiator ? g->me_char : g->peer_char, c1 = g->initiator ? g->peer_char : g->me_char;
    fight_sim_init(&g->sim, c0, c1, g->gauge[0], g->gauge[1]);
    clear_keys(g);
    g->snap_ok = false;
    g->snap_due = true;
    g->phase = GP_COUNT;
    g->phase_ms = FIGHT_COUNT_MS;
    gev(g, GE_ROUND, NULL);
}

// 主动方开一回合。被连方从收到 'N' 起倒数:主动方多等 'N' 在路上的时间——链路还忙着的那段、排在它前面的
// 每条各占一个来回(停等)、再一个单程——两边一起开打。
static void start_round(fight_game_t *g, uint8_t round) {
    uint32_t piece = g->rtt_ok ? g->rtt : busy_base(g);
    uint32_t wait = (g->link_free > g->now ? g->link_free - g->now : 0) + (uint32_t)(g->out_n + g->keep_n) * piece;
    send_round(g, round);
    begin(g, round);
    g->phase_ms += wait + one_way(g);
}

static void finish(fight_game_t *g) {
    g->phase = GP_OVER;
    gev(g, GE_OVER, NULL);
}

static void reset_match(fight_game_t *g, uint32_t match) {
    g->match = match;
    g->score_me = g->score_peer = 0;
    g->gauge[0] = g->gauge[1] = 0;
    g->round = 0;
    g->round_done = false;
    g->me_picked = g->peer_picked = false;
    g->peer_char = FIGHT_NOCHAR;
    g->me_again = g->peer_again = false;
}

static void to_pick(fight_game_t *g) {
    g->selecting = true;
    g->phase = GP_PICK;
    gev(g, GE_PICK, NULL);
}

// 主动方:定下一场的场号,进选人页
static void enter_select(fight_game_t *g) {
    uint32_t old = g->match, m;
    do m = g->rnd() | 1u;
    while (m == old);
    reset_match(g, m);
    to_pick(g);
    send_select(g);
}

static void maybe_start(fight_game_t *g) {
    if (!g->initiator || g->phase != GP_PICK || !g->me_picked || !g->peer_picked) return;
    g->selecting = false;
    start_round(g, 1);
}

static void maybe_again(fight_game_t *g) {
    if (g->initiator && g->phase == GP_OVER && g->me_again && g->peer_again) enter_select(g);
}

// 主动方:对方连上来了。没开过就开新的一场;选人选到一半就接着选;打完了告诉对方;否则重开这一回合(判完了开下一回合)。
static void resume(fight_game_t *g) {
    if (!g->match) {
        enter_select(g);
    } else if (g->selecting) {  // 对方可能换了人(重启 / 换卡),等它重发
        g->peer_picked = false;
        g->peer_char = FIGHT_NOCHAR;
        to_pick(g);
        send_select(g);
        if (g->me_picked) send_pick(g);
    } else if (over(g)) {
        send_round(g, 0);
        finish(g);
    } else {
        start_round(g, g->round_done ? g->next_round : g->round);
    }
}

// 主动方:这一回合分出来了(KO / 时间到)。最后一条快照和 'O' 跟在 'E' 后面发。
static void round_over(fight_game_t *g) {
    uint8_t w = g->sim.winner;
    if (w == 0 && g->score_me < FIGHT_WIN) g->score_me++;
    if (w == 1 && g->score_peer < FIGHT_WIN) g->score_peer++;
    g->gauge[0] = g->sim.f[0].gauge;
    g->gauge[1] = g->sim.f[1].gauge;
    g->round_done = true;
    g->next_round = w == FIGHT_DRAW ? g->round : (uint8_t)(g->round + 1);
    uint8_t f[FIGHT_SNAP_LEN];
    enqueue(g, f, fight_snap_pack(f, &g->sim, g->rx_seq));
    uint8_t m[9] = { 'O' };
    fight_put32(m + 1, g->match);
    m[5] = g->round;
    m[6] = w;
    m[7] = g->score_me;
    m[8] = g->score_peer;
    enqueue_keep(g, m, sizeof m);
    g->phase = GP_END;
    g->phase_ms = FIGHT_NEXT_MS;
    gev(g, GE_ROUND_END, NULL);
}

// ---- 主动方:喂键、推规则 ----

static void push_key(fight_tkey_t *q, uint8_t *n, uint8_t key, uint32_t at) {
    if (*n < FIGHT_KEYQ) q[(*n)++] = (fight_tkey_t){ key, at };
}

static void sim_events(fight_game_t *g) {
    fight_ev_t e;
    while (fight_sim_event(&g->sim, &e)) {
        g->snap_due = true;
        if (e.kind == FE_SUPER || e.kind == FE_KO || e.kind == FE_TIMEUP || e.kind == FE_GUARD_BREAK) {
            uint8_t m[6] = { 'E', g->round, e.kind, e.who, e.move, e.who < 2 ? g->sim.f[e.who].ch : 0 };
            enqueue(g, m, sizeof m);
        }
        if (e.kind == FE_DIZZY && e.who == 1) g->mash_got = 0;
        gev(g, GE_SIM, &e);
    }
}

// 到了时刻的键喂给规则:own 喂 f[0](延后的本地键,不算晚),remote 喂 f[1](晚了多少按本来的时刻补算)
static void feed(fight_game_t *g, fight_tkey_t *q, uint8_t *n, uint8_t who) {
    uint8_t i = 0;
    while (i < *n && q[i].at <= g->now) {
        uint32_t late = who ? g->now - q[i].at : 0;
        fight_sim_key_at(&g->sim, who, q[i].key, (uint16_t)(late > FIGHT_LATE_MAX ? FIGHT_LATE_MAX : late));
        i++;
    }
    *n = (uint8_t)(*n - i);
    memmove(q, q + i, *n * sizeof q[0]);
}

static void host_step(fight_game_t *g) {
    feed(g, g->own, &g->own_n, 0);
    feed(g, g->remote, &g->remote_n, 1);
    fight_sim_step(&g->sim);
    sim_events(g);
    if (g->sim.winner != FIGHT_UNDECIDED) round_over(g);
}

static void on_keys(fight_game_t *g, const uint8_t *m, size_t n) {
    fight_kpair_t p[FIGHT_K_MAX];
    uint8_t seq, k = fight_k_unpack(m, n, &seq, p);
    if (!g->initiator || !k) return;
    bool cont = seq == (uint8_t)(g->rx_seq + 1);  // 接得上:第一个键的间隔是对着上一条最后一个键的
    g->rx_seq = seq;
    if (g->phase != GP_PLAY) return;
    uint32_t at[FIGHT_K_MAX], prev = g->rx_last;
    for (uint8_t i = 0; i < k; i++) {
        uint32_t gap = p[i].gap * 10u;
        if ((i == 0 && !cont) || gap >= FIGHT_BURST_MS) at[i] = g->now > prev ? g->now : prev;  // 新的一串
        else at[i] = prev + gap;
        prev = at[i];
    }
    // 一条 'K' 里的键都是发出去之前按的,不可能比它到得还晚:最后一个排在到达以后,说明这一串开头那下晚到了
    // (后面的都跟着晚)。整条往前挪到最后一个 = 现在,不早于上一条最后一个键。
    if (at[k - 1] > g->now) {
        uint32_t shift = at[k - 1] - g->now;
        for (uint8_t i = 0; i < k; i++) {
            at[i] = at[i] > shift ? at[i] - shift : 0;
            uint32_t floor = i ? at[i - 1] : g->rx_last;
            if (at[i] < floor) at[i] = floor;
        }
    }
    for (uint8_t i = 0; i < k; i++) push_key(g->remote, &g->remote_n, p[i].key, at[i]);
    g->rx_last = at[k - 1];
    feed(g, g->remote, &g->remote_n, 1);
    sim_events(g);
}

static void on_mash(fight_game_t *g, const uint8_t *m, size_t n) {
    if (!g->initiator || n < 2 || g->phase != GP_PLAY) return;
    while (g->mash_got < m[1] && g->sim.f[1].st == FS_DIZZY) {  // 一秒最多算几下由规则管
        fight_sim_key_at(&g->sim, 1, FK_OK, 0);
        g->mash_got++;
    }
    sim_events(g);
}

static void on_ack(fight_game_t *g, const uint8_t *m, size_t n) {
    if (!g->initiator || n < 2 || !g->ping_wait || m[1] != g->ping_id) return;
    g->ping_wait = false;
    rtt_sample(g, g->now - g->ping_at);
    if (g->hand_at == g->ping_at && g->link_free > g->now) g->link_free = g->now;  // 'Q' 是最后交出去的一片:回答到了,链路空了
}

// ---- 被连方:照快照画 ----

// 主动方开打了(发来快照 / 事件),这边的倒数可能慢一点:直接开打
static bool playing(fight_game_t *g) {
    if (g->phase == GP_COUNT) {
        g->phase_ms = 0;
        g->phase = GP_PLAY;
        gev(g, GE_FIGHT, NULL);
    }
    return g->phase == GP_PLAY;
}

// 被连方晕着(或者特写演完、快照还没说他晕):按键算狂按,发 'M' 不发 'K'
static bool mashing(const fight_game_t *g) {
    const fight_fighter_t *f = &g->sim.f[g->me];
    return f->st == FS_DIZZY || (f->st == FS_SUPER_IN && !g->sim.freeze);
}

static void send_mash(fight_game_t *g) {
    if (g->mash_n == g->mash_sent || g->mash_cool) return;
    uint8_t m[2] = { 'M', g->mash_n };
    enqueue(g, m, sizeof m);
    g->mash_sent = g->mash_n;
    g->mash_cool = FIGHT_MASH_MSG_MS;
}

// 快照前后比一比,给画面出事件(出招、打中、太极接住、挡住的特效、弹开、晕、醒);大招、砸开防御、KO、时间到由 'E' 出。
// 弹开:打的人僵住了、两边都没掉血、对方的气一下涨了 FIGHT_GAUGE_PARRY 以上(别的事一次涨不了这么多);对方气满了就看他刚才是不是举着盾。
// 弹开后一个快照间隔里就反打的认不出(只少个声音)。
// 爷爷的太极:摆架势算出招(FE_START 小招),架势收招(FS_STANCE → FS_MOVE 小招)不算;
// 架势里的爷爷不再摆了、对方掉了血 = 接住(FE_COUNTER)。'E' 说了砸开防御的那一下掉血不再算打中。
static void diff_events(fight_game_t *g, const fight_fighter_t *old) {
    for (uint8_t i = 0; i < 2; i++) {
        const fight_fighter_t *o = &old[i], *f = &g->sim.f[i], *po = &old[!i], *p = &g->sim.f[!i];
        bool stance_end = o->st == FS_STANCE && f->st == FS_MOVE && f->move == FM_SPECIAL;
        if ((f->st == FS_MOVE && !stance_end && (o->st != FS_MOVE || o->move != f->move || f->t + 200 < o->t)) ||
            (f->st == FS_STANCE && o->st != FS_STANCE))
            sim_gev(g, FE_START, i, f->move);
        if (f->hp < o->hp) {
            bool countered = po->st == FS_STANCE && p->st != FS_STANCE && !(p->st == FS_MOVE && p->move == FM_SPECIAL);
            if (countered) sim_gev(g, FE_COUNTER, !i, FM_SPECIAL);
            else if (g->broke & 1u << i) g->broke &= (uint8_t)~(1u << i);
            else sim_gev(g, FE_HIT, !i, p->move);
        }
        if (f->st == FS_BLOCKSTUN && o->st != FS_BLOCKSTUN) sim_gev(g, FE_BLOCK, i, f->move);
        if (f->st == FS_HITSTUN && o->st != FS_HITSTUN && f->hp == o->hp && p->hp == po->hp &&
            (p->gauge >= po->gauge + FIGHT_GAUGE_PARRY || (p->gauge == FIGHT_GAUGE_MAX && po->st == FS_GUARD)))
            sim_gev(g, FE_PARRY, !i, o->move);
        if (f->st == FS_DIZZY && o->st != FS_DIZZY) sim_gev(g, FE_DIZZY, i, 0);
        if (o->st == FS_DIZZY && f->st != FS_DIZZY) sim_gev(g, FE_WAKE, i, 0);
    }
}

static bool in_super(uint8_t st) { return st == FS_SUPER_IN || st == FS_SUPER_OUT; }

// 有事件的状态(出招、被挡僵直、晕、大招特写):这边结束掉以后,晚到的快照别再画回来(会重出一次事件、重新定住)。
// 盾、挨打僵直不算:主动方会把它们拉长(长按撑盾、连招的下一下),快照说还在就还在。
static bool evented(uint8_t st) { return st == FS_MOVE || st == FS_BLOCKSTUN || st == FS_DIZZY || in_super(st); }

// 这个状态开始了多久(特写时 t 不走:按冻结过了多久算)
static uint16_t elapsed(uint8_t st, uint16_t t, uint16_t freeze) {
    return in_super(st) ? (uint16_t)(FIGHT_SUPER_FREEZE - (freeze < FIGHT_SUPER_FREEZE ? freeze : FIGHT_SUPER_FREEZE)) : t;
}

// 被连方:到时间的出招、盾、僵直,晕减到 0(只按时间减,自己狂按的主动方算了才知道),特写冻结完(放招的人能动、
// 挨的人开始晕),这边就结束、不等快照;结束掉的有事件的状态记下开始时刻。返回结束掉的状态(FS_IDLE = 没有)
static uint8_t expire(fight_game_t *g, uint8_t i) {
    fight_fighter_t *f = &g->sim.f[i];
    uint8_t st = f->st;
    bool due = st == FS_DIZZY  ? !f->mash_left
               : in_super(st) ? !g->sim.freeze
                              : (st == FS_MOVE || st == FS_GUARD || st == FS_HITSTUN || st == FS_BLOCKSTUN) && f->t >= f->dur;
    if (!due) return FS_IDLE;
    if (evented(st)) {
        g->gone[i].st = st;
        g->gone[i].move = f->move;
        g->gone[i].at = g->now - elapsed(st, f->t, 0);
    }
    f->t = 0;
    if (st == FS_SUPER_IN) {
        f->st = FS_DIZZY;
        f->dur = f->mash_left = FIGHT_DIZZY_SUPER;
    } else {
        f->st = FS_IDLE;
        f->dur = 0;
    }
    return st;
}

// 快照里的这一下是不是这边已经结束掉的那一下(状态一样、出招的话招也一样,开始时刻差 FIGHT_GONE_TOL 以内)
static bool is_gone(const fight_game_t *g, uint8_t i, uint8_t st, uint8_t move, uint16_t since) {
    int32_t d = (int32_t)(g->now - since - g->gone[i].at);
    return evented(st) && g->gone[i].st == st && (st != FS_MOVE || g->gone[i].move == move) && d >= -FIGHT_GONE_TOL &&
           d <= FIGHT_GONE_TOL;
}

static void apply_snap(fight_game_t *g, const fight_snap_t *s) {
    fight_fighter_t old[2] = { g->sim.f[0], g->sim.f[1] };
    // 自己先画的招:快照还没算到那个键(没发出去,或者 ack 还没到)、也没出别的事,就先留着画的样子
    bool keep = g->predict && (g->predict_left || (int8_t)(s->ack - g->predict_seq) < 0);
    g->sim.freeze = 0;
    bool set[2];
    for (uint8_t i = 0; i < 2; i++) {
        fight_fighter_t *f = &g->sim.f[i];
        const fight_snap_one_t *n = &s->f[i];
        // 快照旧了约一个单程:有时长的状态往前推 ahead(特写时主动方的 t 不走,只推冻结还剩多少)
        uint16_t a = n->st != FS_IDLE && n->st != FS_KO ? g->ahead : 0;
        uint16_t t = (uint16_t)(n->t + (in_super(n->st) ? 0 : a)), left = n->left > a ? (uint16_t)(n->left - a) : 0;
        bool kept = i == g->me && keep && (n->st == FS_IDLE || n->st == FS_GUARD);
        set[i] = !kept && !is_gone(g, i, n->st, n->move, elapsed(n->st, t, left));
        if (set[i]) {
            f->st = n->st;
            f->move = n->move;
            f->t = t;
            f->dur = (uint16_t)(t + left);
            f->mash_left = n->st == FS_DIZZY ? left : 0;
            if (n->st == FS_SUPER_OUT) g->sim.freeze = left;
        }
        if (i == g->me && !kept) g->predict = false;
        f->chain = n->chain;
        f->protect = n->protect;
        f->hp = n->hp;
        f->gauge = n->gauge;
    }
    for (uint8_t i = 0; i < 2; i++)
        if (set[i]) expire(g, i);
    g->sim.clock = s->clock;
    diff_events(g, old);
}

static void on_snap(fight_game_t *g, const uint8_t *m, size_t n) {
    fight_snap_t s;
    if (g->initiator || !fight_snap_unpack(&s, m, n)) return;
    if ((int8_t)(s.ack - g->acked) > 0 && (int8_t)(g->tx_seq - s.ack) >= 0) g->acked = s.ack;
    for (int i = 0; i < 2; i++)
        if (s.f[i].hp > (int16_t)fight_char(g->sim.f[i].ch)->hp) return;
    if (!playing(g)) return;
    g->snap = s;
    g->snap_ok = true;
    apply_snap(g, &s);
}

static void on_event(fight_game_t *g, const uint8_t *m, size_t n) {
    if (g->initiator || n < 6 || m[1] != g->round) return;
    uint8_t kind = m[2], who = m[3], move = m[4];
    bool ok = kind == FE_TIMEUP ? who <= 1 || who == FIGHT_DRAW
                                : (kind == FE_SUPER || kind == FE_KO || kind == FE_GUARD_BREAK) && who <= 1;
    if (!ok || move >= FM_COUNT || !playing(g)) return;
    fight_sim_t *s = &g->sim;
    if (kind == FE_SUPER) {  // 特写:两边定住(晚到了约一个单程,冻结少等 ahead),冻结完这边就接着画
        s->f[who].st = FS_SUPER_OUT;
        s->f[who].move = FM_SUPER;
        s->f[!who].st = FS_SUPER_IN;
        for (int i = 0; i < 2; i++) s->f[i].t = 0;
        s->freeze = (uint16_t)(FIGHT_SUPER_FREEZE - g->ahead);
        if (who == g->me) g->predict = false;
    } else if (kind == FE_GUARD_BREAK) {  // 主动方那边真的砸开了盾(这边画的盾可能是自己先画的):掉血等快照
        g->broke |= (uint8_t)(1u << !who);
    } else if (kind == FE_KO) {
        s->f[who].st = FS_KO;
        s->f[who].hp = 0;
    } else {
        s->winner = who;
    }
    sim_gev(g, kind, who, move);
}

static uint16_t dec(uint16_t v) { return v > FIGHT_STEP ? (uint16_t)(v - FIGHT_STEP) : 0; }

// 被连方这一步:画面上的时间接着走,到时间的出招、盾、僵直结束,晕接着减(下一条快照来了以快照为准);
// 自己的小招冷却照规则估(出完小招才开始减),先画小招用;狂按的数攒着发
static void view_step(fight_game_t *g) {
    fight_sim_t *s = &g->sim;
    if (s->freeze) {
        s->freeze = dec(s->freeze);
        if (!s->freeze)
            for (uint8_t i = 0; i < 2; i++) expire(g, i);
    } else {
        if (s->clock < FIGHT_ROUND_MS) s->clock += FIGHT_STEP;
        for (uint8_t i = 0; i < 2; i++) {
            fight_fighter_t *f = &s->f[i];
            if (f->t <= UINT16_MAX - FIGHT_STEP) f->t += FIGHT_STEP;
            if (f->st == FS_DIZZY) f->mash_left = dec(f->mash_left);
            if (expire(g, i) == FS_DIZZY) sim_gev(g, FE_WAKE, i, 0);
        }
        fight_fighter_t *me = &s->f[g->me];
        bool special = me->st == FS_STANCE || (me->st == FS_MOVE && me->move == FM_SPECIAL);
        me->special_cool = special ? FIGHT_SPECIAL_COOL : dec(me->special_cool);
    }
    g->mash_cool = g->mash_cool > FIGHT_STEP ? (uint16_t)(g->mash_cool - FIGHT_STEP) : 0;
    if (mashing(g)) send_mash(g);
    else if (s->f[g->me].st != FS_SUPER_IN) g->mash_n = g->mash_sent = 0;  // 醒了:下次晕从 0 数
}

// 被连方按下自己的键:先画成出招 / 举盾 / 小招的样子(只画,规则在主动方)。▼ ▲ 照规则(fight_sim 的 ▼ 前缀):
// 上一个键是 ▼、在 FIGHT_PREFIX_MS 内或者盾还举着 → 小招(爷爷摆太极架势);小招冷却里 ▲ 不出。长按撑着盾。
static void predict(fight_game_t *g, uint8_t key) {
    fight_fighter_t *f = &g->sim.f[g->me];
    bool prefix = key == FK_UP && g->down_last && (g->now - g->down_at <= FIGHT_PREFIX_MS || f->st == FS_GUARD);
    if (key != FK_DOWN_LONG) g->down_last = key == FK_DOWN;
    if (key == FK_DOWN) g->down_at = g->now;
    if (g->sim.freeze || (f->st != FS_IDLE && f->st != FS_GUARD)) return;
    const fight_char_def_t *c = fight_char(f->ch);
    if (key == FK_DOWN_LONG) {
        if (f->st != FS_GUARD) return;
        f->dur = c->guard_long_ms;
    } else if (key == FK_DOWN) {
        if (f->st == FS_GUARD) return;
        f->st = FS_GUARD;
        f->t = 0;
        f->dur = c->guard_ms;
    } else if (prefix && f->special_cool) {
        return;
    } else {
        f->move = prefix ? FM_SPECIAL : key == FK_UP ? FM_HEAVY : FM_JAB;
        const fight_move_def_t *d = &c->mv[f->move];
        f->st = prefix && f->ch == FC_GRANDPA ? FS_STANCE : FS_MOVE;
        f->t = 0;
        f->dur = f->st == FS_STANCE ? FIGHT_STANCE_MS : (uint16_t)(d->startup + d->active + d->recovery);
        sim_gev(g, FE_START, g->me, f->move);
    }
    g->predict = true;
    g->predict_left = g->kq_n;
}

// ---- 对外 ----

static void on_hello(fight_game_t *g, const uint8_t *m, size_t n) {
    if (n < 2) return;
    if (m[1] != FIGHT_PROTO) {
        g->bad_ver = true;
        gev(g, GE_BAD_VER, NULL);
        return;
    }
    if (g->initiator) resume(g);
}

static void on_select(fight_game_t *g, const uint8_t *m, size_t n) {
    if (n < 5 || g->initiator) return;
    uint32_t match = fight_get32(m + 1);
    if (!match) return;
    if (match != g->match) reset_match(g, match);
    to_pick(g);
    if (g->me_picked) send_pick(g);  // 同一场断线重连:再告诉一次
}

static void on_pick(fight_game_t *g, const uint8_t *m, size_t n) {
    if (n < 6 || g->phase != GP_PICK || fight_get32(m + 1) != g->match) return;
    uint8_t c = m[5];
    if (c >= FC_COUNT && c != FIGHT_NOCHAR) return;
    g->peer_char = c;
    g->peer_picked = c != FIGHT_NOCHAR;
    gev(g, GE_PEER_PICK, NULL);
    maybe_start(g);
}

static void on_round(fight_game_t *g, const uint8_t *m, size_t n) {
    if (n < 15 || g->initiator) return;
    uint32_t match = fight_get32(m + 1);
    uint16_t g0 = fight_get16(m + 10), g1 = fight_get16(m + 12);
    if (!match || m[6] > FIGHT_WIN || m[7] > FIGHT_WIN || m[8] >= FC_COUNT || m[9] >= FC_COUNT) return;
    if (g0 > FIGHT_GAUGE_MAX || g1 > FIGHT_GAUGE_MAX) return;
    if (match != g->match) reset_match(g, match);
    g->selecting = false;
    g->peer_char = m[8];
    g->me_char = m[9];
    g->me_picked = g->peer_picked = true;
    g->score_me = m[7];
    g->score_peer = m[6];
    g->gauge[0] = g0;
    g->gauge[1] = g1;
    g->ahead = m[14];
    if (m[5]) {
        begin(g, m[5]);
    } else if (over(g)) {
        if (g->phase != GP_OVER) finish(g);
    } else {
        g->phase = GP_WAIT;
    }
}

static void on_over(fight_game_t *g, const uint8_t *m, size_t n) {
    if (n < 9 || g->initiator || fight_get32(m + 1) != g->match || m[5] != g->round) return;
    uint8_t w = m[6];
    if ((w > 1 && w != FIGHT_DRAW) || m[7] > FIGHT_WIN || m[8] > FIGHT_WIN) return;
    if (g->phase != GP_COUNT && g->phase != GP_PLAY) return;  // 一回合只结束一次
    g->score_me = m[8];
    g->score_peer = m[7];
    g->sim.winner = w;
    g->phase = GP_END;
    g->phase_ms = FIGHT_NEXT_MS;
    gev(g, GE_ROUND_END, NULL);
}

static void on_again(fight_game_t *g, const uint8_t *m, size_t n) {
    if (n < 5 || fight_get32(m + 1) != g->match) return;
    g->peer_again = true;
    maybe_again(g);
}

static void on_ping(fight_game_t *g, const uint8_t *m, size_t n) {
    if (g->initiator || n < 2) return;
    uint8_t a[2] = { 'A', m[1] };
    enqueue(g, a, sizeof a);
}

void fight_game_init(fight_game_t *g, fight_send_fn send, void *ctx, fight_rnd_fn rnd) {
    memset(g, 0, sizeof *g);
    g->send = send;
    g->ctx = ctx;
    g->rnd = rnd;
    g->me_char = g->peer_char = FIGHT_NOCHAR;
    g->delay = FIGHT_LOCAL_DELAY;
    fight_sim_init(&g->sim, FC_KID, FC_KID, 0, 0);
}

static void clear_link(fight_game_t *g) {
    if (g->phase != GP_OVER) g->phase = GP_WAIT;
    g->me_again = g->peer_again = false;
    g->out_n = g->out_head = 0;
    g->keep_n = 0;
    clear_keys(g);
    g->snap_ok = false;
}

void fight_game_connected(fight_game_t *g, bool initiator) {
    clear_link(g);
    g->phase = GP_WAIT;
    g->initiator = initiator;
    g->me = initiator ? 0 : 1;
    g->up = true;
    g->bad_ver = false;
    g->rx_seq = g->tx_seq = g->acked = 0;
    g->link_free = g->hand_at = g->now;
    g->key_any = false;
    g->ping_wait = false;
    g->ping_next = g->now;
    g->rtt_ok = false;
    g->busy = 0;
    g->busy_run = 0;
    g->delay = FIGHT_LOCAL_DELAY;
    uint8_t m[2] = { 'H', FIGHT_PROTO };
    enqueue(g, m, sizeof m);
    pump(g);
}

void fight_game_lost(fight_game_t *g) {
    clear_link(g);
    g->up = false;
}

void fight_game_on_msg(fight_game_t *g, const uint8_t *m, size_t n) {
    if (!n) return;
    switch (m[0]) {
    case 'H': on_hello(g, m, n); break;
    case 'S': on_select(g, m, n); break;
    case 'P': on_pick(g, m, n); break;
    case 'N': on_round(g, m, n); break;
    case 'O': on_over(g, m, n); break;
    case 'R': on_again(g, m, n); break;
    case 'K': on_keys(g, m, n); break;
    case 'M': on_mash(g, m, n); break;
    case 'F': on_snap(g, m, n); break;
    case 'E': on_event(g, m, n); break;
    case 'Q': on_ping(g, m, n); break;
    case 'A': on_ack(g, m, n); break;
    default: break;
    }
    pump(g);
}

static void step(fight_game_t *g) {
    switch (g->phase) {
    case GP_COUNT:
        if (g->phase_ms > FIGHT_STEP) {
            g->phase_ms -= FIGHT_STEP;
        } else {
            g->phase_ms = 0;
            g->phase = GP_PLAY;
            gev(g, GE_FIGHT, NULL);
        }
        break;
    case GP_PLAY:
        if (g->initiator) host_step(g);
        else view_step(g);
        break;
    case GP_END:
        if (!g->phase_ms) break;  // 被连方:等主动方的 'N'
        g->phase_ms = g->phase_ms > FIGHT_STEP ? g->phase_ms - FIGHT_STEP : 0;
        if (g->phase_ms || !g->initiator) break;
        if (over(g)) {  // 打完了也由主动方说('O' 可能没送到)
            send_round(g, 0);
            finish(g);
        } else {
            start_round(g, g->next_round);
        }
        break;
    default:
        break;
    }
}

void fight_game_tick(fight_game_t *g, uint32_t dt) {
    g->acc += dt;
    while (g->acc >= FIGHT_STEP) {
        g->acc -= FIGHT_STEP;
        g->now += FIGHT_STEP;
        step(g);
    }
    pump(g);
}

void fight_game_key(fight_game_t *g, uint8_t key) {
    if (g->phase != GP_PLAY || key > FK_DOWN_LONG) return;
    if (g->initiator) {
        push_key(g->own, &g->own_n, key, g->now + g->delay);
        return;
    }
    if (mashing(g)) {
        if (key != FK_DOWN_LONG && g->mash_n < UINT8_MAX) g->mash_n++;
        send_mash(g);
    } else if (g->kq_n < FIGHT_KEYQ) {
        uint32_t gap = g->key_any ? (g->now - g->key_at) / 10 : FIGHT_K_GAP_MAX;
        g->kq[g->kq_n++] = (fight_kpair_t){ key, (uint8_t)(gap > FIGHT_K_GAP_MAX ? FIGHT_K_GAP_MAX : gap) };
        g->key_any = true;
        g->key_at = g->now;
        predict(g, key);
    }
    pump(g);
}

bool fight_game_pick(fight_game_t *g, uint8_t ch) {
    if (g->phase != GP_PICK || g->me_picked || ch >= FC_COUNT) return false;
    g->me_char = ch;
    g->me_picked = true;
    send_pick(g);
    maybe_start(g);
    pump(g);
    return true;
}

bool fight_game_unpick(fight_game_t *g) {
    if (g->phase != GP_PICK || !g->me_picked) return false;
    g->me_picked = false;
    send_pick(g);
    pump(g);
    return true;
}

void fight_game_again(fight_game_t *g) {
    if (g->phase != GP_OVER || g->me_again) return;
    g->me_again = true;
    uint8_t m[5] = { 'R' };
    fight_put32(m + 1, g->match);
    enqueue(g, m, sizeof m);
    maybe_again(g);
    pump(g);
}

bool fight_game_event(fight_game_t *g, uint8_t *e, fight_ev_t *sim_ev) {
    if (!g->ev_n) return false;
    *e = g->evq[g->ev_head].gev;
    if (sim_ev) *sim_ev = g->evq[g->ev_head].ev;
    g->ev_head = (uint8_t)((g->ev_head + 1) % FIGHT_GEVQ);
    g->ev_n--;
    return true;
}
