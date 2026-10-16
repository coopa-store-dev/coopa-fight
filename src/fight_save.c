// src/fight_save.c —— 见 fight_save.h。
#include "fight_save.h"

#include <string.h>

#include "kit_store.h"

_Static_assert(sizeof(fight_save_t) <= 2048, "save blob is at most 2 KB");

static bool valid(const void *blob) { return ((const fight_save_t *)blob)->ver == FIGHT_SAVE_VER; }

void fight_save_load(fight_save_t *s) {
    static fight_save_t scratch;  // 不放在任务栈上
    if (!kit_sv_blob_load(FIGHT_NS, FIGHT_KEY, s, sizeof(*s), valid, &scratch)) {
        memset(s, 0, sizeof(*s));
        s->ver = FIGHT_SAVE_VER;
    }
    if (s->last_char >= FIGHT_SAVE_CHARS) s->last_char = 0;
    s->crowns &= 0x0F;
    if (s->seen_tutorial > 1) s->seen_tutorial = 1;
}

void fight_save_store(const fight_save_t *s) { kit_sv_blob_store(FIGHT_NS, FIGHT_KEY, s, sizeof(*s)); }

uint8_t fight_save_milestones(const fight_save_t *s) {
    return (uint8_t)((s->supers >= 1 ? 1u : 0u) | (s->wins >= 1 ? 2u : 0u) | (s->wins >= 10 ? 4u : 0u));
}

bool fight_save_crown(const fight_save_t *s, uint8_t c) { return c < FIGHT_SAVE_CHARS && (s->crowns >> c & 1); }

bool fight_save_set_crown(fight_save_t *s, uint8_t c) {
    if (c >= FIGHT_SAVE_CHARS || fight_save_crown(s, c)) return false;
    s->crowns = (uint8_t)(s->crowns | 1u << c);
    return true;
}
