// src/fight_msg.c —— 见 fight_msg.h。
#include "fight_msg.h"

void fight_put16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

void fight_put32(uint8_t *p, uint32_t v) {
    for (int i = 0; i < 4; i++) p[i] = (uint8_t)(v >> (8 * i));
}

uint16_t fight_get16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }

uint32_t fight_get32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

// 每人 48 位,从低到高:状态 4、招式 3、连招 2、保护 1、血 10、气 12、t/10 8、left/10 8
#define SH_MOVE 4
#define SH_CHAIN 7
#define SH_PROT 9
#define SH_HP 10
#define SH_GAUGE 20
#define SH_T 32
#define SH_LEFT 40

_Static_assert(FIGHT_GAUGE_MAX <= 0xFFF, "gauge must fit the 12 snapshot bits");  // 快照里气占 12 位

static uint64_t tens(uint32_t ms) { return ms / 10 > 255 ? 255 : ms / 10; }

static uint16_t left_of(const fight_sim_t *s, const fight_fighter_t *f) {
    switch (f->st) {
    case FS_DIZZY: return f->mash_left;
    case FS_SUPER_IN:
    case FS_SUPER_OUT: return s->freeze;  // 特写时 t 不走,冻结还剩多少在 s->freeze
    case FS_IDLE:
    case FS_KO: return 0;
    default: return f->dur > f->t ? (uint16_t)(f->dur - f->t) : 0;
    }
}

size_t fight_snap_pack(uint8_t *m, const fight_sim_t *s, uint8_t ack) {
    m[0] = 'F';
    m[1] = ack;
    fight_put16(m + 2, (uint16_t)(s->clock / 10));
    for (int i = 0; i < 2; i++) {
        const fight_fighter_t *f = &s->f[i];
        uint64_t v = (uint64_t)(f->st & 0xF) | (uint64_t)(f->move & 0x7) << SH_MOVE |
                     (uint64_t)(f->chain > 3 ? 3 : f->chain) << SH_CHAIN | (uint64_t)(f->protect ? 1 : 0) << SH_PROT |
                     (uint64_t)((uint16_t)(f->hp > 0 ? f->hp : 0) & 0x3FF) << SH_HP |
                     (uint64_t)(f->gauge & 0xFFF) << SH_GAUGE | tens(f->t) << SH_T | tens(left_of(s, f)) << SH_LEFT;
        for (int b = 0; b < 6; b++) m[4 + 6 * i + b] = (uint8_t)(v >> (8 * b));
    }
    return FIGHT_SNAP_LEN;
}

bool fight_snap_unpack(fight_snap_t *out, const uint8_t *m, size_t n) {
    if (n < FIGHT_SNAP_LEN) return false;
    fight_snap_t s;
    s.ack = m[1];
    s.clock = fight_get16(m + 2) * 10u;
    if (s.clock > FIGHT_ROUND_MS) return false;
    for (int i = 0; i < 2; i++) {
        uint64_t v = 0;
        for (int b = 0; b < 6; b++) v |= (uint64_t)m[4 + 6 * i + b] << (8 * b);
        fight_snap_one_t *f = &s.f[i];
        f->st = (uint8_t)(v & 0xF);
        f->move = (uint8_t)(v >> SH_MOVE & 0x7);
        f->chain = (uint8_t)(v >> SH_CHAIN & 0x3);
        f->protect = v >> SH_PROT & 1;
        f->hp = (int16_t)(v >> SH_HP & 0x3FF);
        f->gauge = (uint16_t)(v >> SH_GAUGE & 0xFFF);
        f->t = (uint16_t)((v >> SH_T & 0xFF) * 10);
        f->left = (uint16_t)((v >> SH_LEFT & 0xFF) * 10);
        if (f->st > FS_KO || f->move >= FM_COUNT || f->gauge > FIGHT_GAUGE_MAX) return false;
    }
    *out = s;
    return true;
}

size_t fight_k_pack(uint8_t *m, uint8_t seq, const fight_kpair_t *p, uint8_t n) {
    m[0] = 'K';
    m[1] = seq;
    for (uint8_t i = 0; i < n; i++) {
        m[2 + 2 * i] = p[i].key;
        m[3 + 2 * i] = p[i].gap;
    }
    return 2 + 2 * (size_t)n;
}

uint8_t fight_k_unpack(const uint8_t *m, size_t n, uint8_t *seq, fight_kpair_t *out) {
    if (n < 4 || (n - 2) % 2 || (n - 2) / 2 > FIGHT_K_MAX) return 0;
    uint8_t k = (uint8_t)((n - 2) / 2);
    for (uint8_t i = 0; i < k; i++) {
        out[i].key = m[2 + 2 * i];
        out[i].gap = m[3 + 2 * i];
        if (out[i].key > FK_DOWN_LONG) return 0;
    }
    *seq = m[1];
    return k;
}
