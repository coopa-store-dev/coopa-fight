// src/fight_app.c —— 库巴格斗的入口表和流程(照 coopa-bomb 的 bomb_app.c):
//   第一次进游戏先看出招表 → 首页「和电脑打 / 两人联机 / 出招表 / 回菜单」→ 选人 → 倒数、对打、回合结束 → 结束页。
// 按键:● 拳 / 确定 / 再来;▲ 重击;▼ 防(结束页 ▼ 回首页,选人页 ▲▼ 选)。长按只认 ▼(撑盾,FK_DOWN_LONG),● 的长按丢掉。
// 一场比赛和联机在 fight_game.c;联机走 kit_link(频道 FIGHT_CH);和电脑打是 fight_local.c 的两个 fight_game_t
// + fight_bot.c(闯关:另外三个人随机排,简单 → 普通 → 厉害;不开蓝牙)。画面在 fight_ui.c。
// 存档:saves 分区 "fight"/"save"(fight_save.h 只管数据,这里 kit_sv_* 读写)。
#include <stdio.h>
#include <string.h>

#include "esp_random.h"
#include "fight_art.h"
#include "fight_bot.h"
#include "fight_game.h"
#include "fight_lines.h"
#include "fight_local.h"
#include "fight_save.h"
#include "fight_texts.h"
#include "fight_ui.h"
#include "kit_app.h"
#include "kit_art.h"
#include "kit_link.h"
#include "kit_sfx.h"
#include "kit_store.h"
#include "ui_kit.h"

#define FIGHT_CH 0x7AC3u   // 联机频道(库巴大作战 0x7AC1、传炸弹 0x7AC2)
#define OVER_DEAF_MS 1000  // 结束页前 1 秒不收键(狂按的那一下别把人带回首页)
#define PICK_DEAF_MS 400   // 刚进选人页不收 ●(结束页连按的第二下别直接选了人)
#define STAGES 3
#define SAY_MS 700         // 招式名、「破防！」这类字闪多久
#define FIGHT_SHOW_MS 700  // 「开打！」
#define FLASH_MS 140       // 挨打闪白
#define SPARK_MS 160
#define SHIELD_MS 300
#define SHAKE_MS 450       // 大招砸下来震屏
#define RESULT_MS 900      // 回合结束这么久以后再说谁拿下这一回合、赢家举手
#define STARS 5

_Static_assert(FIGHT_CUT_MS == FIGHT_SUPER_FREEZE, "the cut-in must last exactly the super freeze");

typedef enum { PG_TITLE = 0, PG_LINK, PG_GAME, PG_HELP } page_t;
typedef enum { MODE_LINK = 0, MODE_CPU } play_mode_t;
typedef enum { SCR_OTHER = 0, SCR_PICK, SCR_GAME } screen_t;

static const char *const NAMES[FC_COUNT] = { TX_KID, TX_DAD, TX_GRANNY, TX_GRANDPA };
static const char *const SPECIAL[FC_COUNT] = { TX_SP_KID, TX_SP_DAD, TX_SP_GRANNY, TX_SP_GRANDPA };
static const char *const SUPER[FC_COUNT] = { TX_SU_KID, TX_SU_DAD, TX_SU_GRANNY, TX_SU_GRANDPA };
static const fight_bot_cfg_t *const LEVEL[STAGES] = { &fight_bot_easy, &fight_bot_normal, &fight_bot_hard };

static fight_save_t s_save;
static fight_game_t s_link;      // 联机时的这一边
static fight_local_t s_loc;      // 和电脑打:me(主动方)+ cpu
static fight_bot_t s_bot;        // 电脑的脑子
static fight_game_t *G = &s_link;  // 当前这一边
static uint8_t s_mode;           // play_mode_t
static uint8_t s_stage;          // 闯关第几关(1..3);联机时 0
static uint8_t s_order[STAGES];  // 闯关要打的三个人
static bool s_auto;              // 和电脑打:进选人页时自动选上一关的人(下一关 / 再打一次)
static bool s_cleared;           // 三关都赢了
static page_t s_page;
static screen_t s_screen;
static uint8_t s_sel;            // 首页光标
static uint8_t s_cursor;         // 选人页光标(0..3 角色,4 回首页)
static uint8_t s_game_me = 0xFF, s_game_peer = 0xFF;  // 对打页按哪两个人建的
static uint32_t s_session;
static bool s_up;
static char s_note[192];
static uint32_t s_clock, s_acc, s_quiet, s_over_ms, s_pick_deaf, s_fight_ms, s_say_ms, s_shake_ms;
static const char *s_say;
static uint32_t s_say_color;
static uint8_t s_count_shown;    // 倒数上次响到几
// 画面上的一边:[0] 自己、[1] 对方
static uint32_t s_flash[2], s_spark[2], s_shield[2];
static uint16_t s_trail[2];      // 血条上刚掉的那段(千分比),慢慢往下收
static bool s_cage[2];           // 被爸爸的大招打晕:关在铁笼里狂按
static bool s_cut;
static uint8_t s_cut_char, s_cut_side;
static uint32_t s_cut_ms;
static fight_view_t s_view;

static bool link_send(void *ctx, const void *m, size_t n) {
    (void)ctx;
    return kit_link_send(m, n);
}

static uint32_t rnd(void) { return esp_random(); }

static void store(void) { kit_sv_blob_store(FIGHT_NS, FIGHT_KEY, &s_save, sizeof(s_save)); }

// 内容没变就不重画(key 里放着画这一页用到的所有东西)
static bool changed(const char *key) {
    if (!strcmp(key, s_note)) return false;
    snprintf(s_note, sizeof(s_note), "%s", key);
    return true;
}

static void note(const char *big, uint32_t color, const char *l1, const char *l2, const char *l3) {
    char key[sizeof(s_note)];
    snprintf(key, sizeof(key), "N%s|%s|%s|%s|%lx", big, l1, l2, l3, (unsigned long)color);
    if (!changed(key)) return;
    s_screen = SCR_OTHER;
    fight_ui_note(big, color, l1, l2, l3);
}

static void say(const char *text, uint32_t color) {
    s_say = text;
    s_say_color = color;
    s_say_ms = SAY_MS;
}

static void voice(const kit_line_t *line, sfx_id_t prefix) {
    sfx_play_line(line, prefix, false);
    s_quiet = line->samples / 16 + 200;  // 16 kHz:说话的这段时间不放「啪」「嘀」(会切断语音)
}

static void sound(sfx_id_t id) {
    if (!s_quiet) sfx_play(id);
}

static bool match_over(const fight_game_t *g) { return g->score_me >= FIGHT_WIN || g->score_peer >= FIGHT_WIN; }

// 画面的一边(0 自己、1 对方)在 sim.f[] 里的下标,和反过来
static uint8_t sim_of(int side) { return (uint8_t)(side ? !G->me : G->me); }
static int side_of(uint8_t who) { return who == G->me ? 0 : 1; }
static uint8_t char_of(uint8_t who) { return G->sim.f[who & 1].ch < FC_COUNT ? G->sim.f[who & 1].ch : FC_KID; }

static void clear_fx(void) {
    for (int i = 0; i < 2; i++) {
        s_flash[i] = s_spark[i] = s_shield[i] = 0;
        s_cage[i] = false;
        s_trail[i] = 1000;
    }
    s_cut = false;
    s_say_ms = s_fight_ms = s_shake_ms = 0;
}

static void show_title(void) {
    s_page = PG_TITLE;
    s_screen = SCR_OTHER;
    s_note[0] = 0;
    fight_ui_title(s_sel, (unsigned)s_save.wins);
}

static void to_title(void) {
    if (s_mode == MODE_LINK) kit_link_stop();
    fight_game_lost(&s_link);
    s_up = false;
    clear_fx();
    show_title();
}

static void to_help(void) {
    s_page = PG_HELP;
    s_screen = SCR_OTHER;
    s_note[0] = 0;
    fight_ui_help(s_save.last_char);
}

static void to_link(void) {
    s_mode = MODE_LINK;
    fight_game_init(&s_link, link_send, NULL, rnd);  // 从首页进来是新的一场(断线连回来才接着打)
    G = &s_link;
    s_stage = 0;
    s_page = PG_LINK;
    s_up = false;
    s_note[0] = 0;
    kit_link_start(FIGHT_CH);
}

static void to_cpu(void) {
    s_mode = MODE_CPU;
    fight_local_init(&s_loc, rnd);
    fight_local_flow(&s_loc);
    G = &s_loc.me;
    s_stage = 1;
    s_auto = s_cleared = false;
    s_acc = 0;
    s_page = PG_GAME;
    s_screen = SCR_OTHER;
    s_note[0] = 0;
}

// 闯关:除了自己的人,另外三个随机排
static void make_order(uint8_t me) {
    uint8_t n = 0;
    for (uint8_t f = 0; f < FC_COUNT; f++)
        if (f != me) s_order[n++] = f;
    for (int i = STAGES - 1; i > 0; i--) {
        int j = (int)(rnd() % (uint32_t)(i + 1));
        uint8_t t = s_order[i];
        s_order[i] = s_order[j];
        s_order[j] = t;
    }
}

static void pick(uint8_t f) {
    if (!fight_game_pick(G, f)) return;
    voice(&fight_lines_pick[f], SFX_NONE);
    if (s_save.last_char != f) {
        s_save.last_char = f;
        store();
    }
    if (s_mode == MODE_CPU && s_stage == 1 && !s_auto) make_order(f);
}

// 大招特写:放招的人的头像和招式名,喊他自己的招式语音(不点对方是谁)
static void start_cut(uint8_t who) {
    s_cut = true;
    s_cut_ms = 0;
    s_cut_char = char_of(who);
    s_cut_side = (uint8_t)side_of(who);
    voice(&fight_lines_super[s_cut_char], SFX_CHAIN);
}

static void hit_fx(int side) {
    s_flash[side] = FLASH_MS;
    s_spark[side] = SPARK_MS;
}

static void on_sim(const fight_ev_t *e) {
    int sd = side_of(e->who);
    switch (e->kind) {
        case FE_START:
            if (e->move == FM_SPECIAL) {  // 小招:只闪招式名 + 语音
                say(SPECIAL[char_of(e->who)], UK_BLUE);
                voice(&fight_lines_special[char_of(e->who)], SFX_NONE);
            } else if (e->move == FM_HEAVY) {
                sfx_play(SFX_WINDUP);  // 次要音效:说话时外壳自己跳过
            }
            break;
        case FE_HIT:
            hit_fx(!sd);
            if (e->move != FM_SUPER) sound(SFX_HIT);
            break;
        case FE_GUARD_BREAK:
            hit_fx(!sd);
            say(TX_BREAK, UK_RED);
            sound(SFX_HIT);
            break;
        case FE_BLOCK:  // who = 被挡住的人:盾出在对面
            s_shield[!sd] = SHIELD_MS;
            sfx_play(SFX_DODGE);
            break;
        case FE_TRADE:
            hit_fx(0);
            hit_fx(1);
            sound(SFX_HIT);
            break;
        case FE_COUNTER:  // 爷爷接住:打过来的人挨一下
            hit_fx(!sd);
            say(TX_COUNTER, UK_GREEN);
            sound(SFX_HIT);
            break;
        case FE_SUPER:
            start_cut(e->who);
            s_cage[!sd] = char_of(e->who) == FC_DAD;
            if (sd == 0) {
                s_save.supers++;
                store();
            }
            break;
        case FE_WAKE: s_cage[sd] = false; break;
        case FE_KO:
            hit_fx(sd);
            s_shake_ms = SHAKE_MS;
            sound(SFX_HIT);
            break;
        default: break;
    }
}

static void on_event(uint8_t ev, const fight_ev_t *se) {
    switch (ev) {
        case GE_ROUND:
            clear_fx();
            s_count_shown = 0;
            break;
        case GE_FIGHT:
            s_fight_ms = FIGHT_SHOW_MS;
            sound(SFX_START);
            break;
        case GE_SIM: on_sim(se); break;
        case GE_OVER: {
            bool won = G->score_me >= FIGHT_WIN;
            uint8_t w = won ? G->me_char : G->peer_char;
            voice(&fight_lines_win[w < FC_COUNT ? w : 0], SFX_GAME_OVER);  // 两张卡都放赢家的
            s_over_ms = OVER_DEAF_MS;
            bool dirty = fight_save_over(&s_save, G->match, won);
            if (s_mode == MODE_CPU && won && s_stage == STAGES) {
                s_cleared = true;
                dirty |= fight_save_set_crown(&s_save, G->me_char);
            }
            if (dirty) store();
            break;
        }
        case GE_PICK:
            s_cursor = s_save.last_char;
            s_pick_deaf = PICK_DEAF_MS;
            break;
        default: break;
    }
}

static void fight_init_app(void) {
    static fight_save_t scratch;  // 不放在任务栈上
    if (!kit_sv_blob_load(FIGHT_NS, FIGHT_KEY, &s_save, sizeof(s_save), fight_save_valid, &scratch)) {
        fight_save_reset(&s_save);
    }
    fight_save_clean(&s_save);
    fight_game_init(&s_link, link_send, NULL, rnd);
}

static void fight_enter(const kit_links_t *links) {
    (void)links;
    s_sel = 0;
    s_mode = MODE_LINK;
    G = &s_link;
    clear_fx();
    if (s_save.seen_tutorial) show_title();
    else to_help();  // 第一次玩先看出招表
}

// 和电脑打的结束页按 ●:下一关 / 再打一次 / 通关后重新闯
static void cpu_again(void) {
    if (s_cleared) {
        s_stage = 1;
        s_cleared = s_auto = false;
    } else {
        if (G->score_me >= FIGHT_WIN && s_stage < STAGES) s_stage++;
        s_auto = true;
    }
    fight_game_again(&s_loc.me);
    fight_game_again(&s_loc.cpu);
    fight_local_flow(&s_loc);
}

// kit_input_t → fight_key_t:▼ 的长按 = 撑盾;● 的长按丢掉(按下那一下已经来过了)。-1 = 不算
static int key_of(const kit_input_t *in) {
    if (in->long_press) return in->key == KIT_KEY_DOWN ? FK_DOWN_LONG : -1;
    return in->key == KIT_KEY_UP ? FK_UP : in->key == KIT_KEY_DOWN ? FK_DOWN : FK_OK;
}

static bool menu_input(const kit_input_t *in) {
    if (s_page == PG_HELP) {  // 随便按一下:看过了
        if (!s_save.seen_tutorial) {
            s_save.seen_tutorial = 1;
            store();
        }
        show_title();
        return false;
    }
    if (s_page == PG_TITLE) {
        if (in->key == KIT_KEY_OK) {
            if (s_sel == 3) return true;  // 回菜单
            if (s_sel == 0) to_cpu();
            else if (s_sel == 1) to_link();
            else to_help();
            return false;
        }
        s_sel = (uint8_t)((s_sel + (in->key == KIT_KEY_UP ? 3 : 1)) % 4);
        fight_ui_title(s_sel, (unsigned)s_save.wins);
        return false;
    }
    if (in->key == KIT_KEY_OK) to_title();  // 找对方 / 断线页:● 回首页
    return false;
}

static bool fight_input(const kit_input_t *in) {
    if (s_page != PG_GAME) return in->long_press ? false : menu_input(in);
    if (G->phase == GP_PICK) {
        if (s_pick_deaf || in->long_press) return false;
        if (G->me_picked) {
            if (in->key == KIT_KEY_DOWN) fight_game_unpick(G);
        } else if (in->key == KIT_KEY_OK) {
            if (s_cursor >= FC_COUNT) to_title();
            else pick(s_cursor);
        } else {
            s_cursor = (uint8_t)((s_cursor + (in->key == KIT_KEY_UP ? FC_COUNT : 1)) % (FC_COUNT + 1));
        }
        return false;
    }
    if (G->phase == GP_OVER) {
        if (s_over_ms || in->long_press) return false;
        if (in->key == KIT_KEY_DOWN) to_title();
        else if (in->key == KIT_KEY_OK) {
            if (s_mode == MODE_CPU) cpu_again();
            else fight_game_again(G);
        }
        return false;
    }
    int k = key_of(in);
    if (k < 0) return false;
    if (G->phase == GP_PLAY && G->sim.f[G->me].st == FS_DIZZY && k != FK_DOWN_LONG) sfx_play(SFX_TAP);
    fight_game_key(G, (uint8_t)k);
    return false;
}

static void show_link(kit_link_state_t st) {
    if (s_link.bad_ver) note(TX_BAD_VER, UK_RED, TX_BAD_VER2, "", TX_HOME_HINT);
    else if (st == KIT_LINK_NO_MEMORY) note(TX_NO_MEM, UK_RED, TX_NO_MEM2, "", TX_HOME_HINT);
    else if (st == KIT_LINK_LOST) note(TX_LOST, UK_INK, TX_LOST2, "", TX_HOME_HINT);
    else note(TX_SEARCH, UK_INK, TX_SEARCH2, TX_SEARCH3, TX_HOME_HINT);
}

static void show_pick(void) {
    char peer[64] = "";
    if (s_mode == MODE_LINK) {
        if (G->peer_picked && G->peer_char < FC_COUNT) snprintf(peer, sizeof(peer), TX_PEER_PICKED, NAMES[G->peer_char]);
        else snprintf(peer, sizeof(peer), "%s", TX_PEER_PICKING);
    }
    const char *hint = G->me_picked ? TX_PICK_WAIT : TX_PICK_HINT;
    uint8_t sel = G->me_picked ? G->me_char : s_cursor;
    uint8_t crowns = s_save.crowns;
    char key[sizeof(s_note)];
    snprintf(key, sizeof(key), "P%u|%u|%u|%s|%s", sel, G->me_picked, crowns, hint, peer);
    if (!changed(key)) return;
    s_screen = SCR_PICK;
    fight_ui_pick(sel, G->me_picked, crowns, TX_PICK, hint, peer);
}

// ---- 对打页:从 G->sim(主动方是真的,被连方照快照填、自己的招先画)选动作图和特效 ----

// 晕的星星:每狂按两下少一颗(至少留一颗)。被连方不知道对方按了几下,按还剩多久算。
static uint8_t stars_of(int side, const fight_fighter_t *f) {
    if (f->st != FS_DIZZY) return 0;
    int n;
    if (G->initiator) n = STARS - fight_mash_taken(&G->sim, sim_of(side)) / 2;
    else if (side == 0) n = STARS - G->mash_n / 2;
    else n = (f->mash_left * STARS + FIGHT_DIZZY_SUPER - 1) / FIGHT_DIZZY_SUPER;
    return (uint8_t)(n < 1 ? 1 : n > STARS ? STARS : n);
}

// 回合 / 一场的赢家在画面的哪一边;-1 = 还没有或平
static int winner_side(void) {
    if (G->phase == GP_OVER) return G->score_me >= FIGHT_WIN ? 0 : 1;
    if (G->phase != GP_END || G->sim.winner > 1) return -1;
    return side_of(G->sim.winner);
}

static void side_view(fight_side_view_t *v, int side, int won) {
    const fight_fighter_t *f = &G->sim.f[sim_of(side)];
    fight_ui_pose(f, v);
    bool done = G->phase == GP_OVER || (G->phase == GP_END && G->phase_ms + RESULT_MS <= FIGHT_NEXT_MS);
    if (done && won >= 0) {
        v->pose = won == side ? FA_WIN : FA_KO;
        v->dx = won == side ? 0 : -10;
    }
    int max = fight_char(f->ch)->hp, hp = f->hp < 0 ? 0 : f->hp;
    v->hp = (uint16_t)(hp * 1000 / max);
    v->trail = s_trail[side] > v->hp ? s_trail[side] : v->hp;
    v->gauge = f->gauge;
    v->gauge_blink = (s_clock / 250) & 1;
    v->flash = s_flash[side] > 0;
    v->spark = s_spark[side] > 0;
    v->shield = s_shield[side] > 0;
    v->stars = stars_of(side, f);
    if (f->st == FS_DIZZY && s_cage[side]) {  // 被爸爸的大招打晕:关在铁笼里
        v->prop = FX_CAGE + 1;
        v->prop_x = 0;
    }
}

static void keys_hint(fight_view_t *v) {
    const fight_fighter_t *me = &G->sim.f[G->me];
    v->hint_color = UK_INK;
    if (me->st == FS_DIZZY) {
        snprintf(v->hint, sizeof(v->hint), "%s", TX_MASH);
        v->hint_color = (s_clock / 200) & 1 ? UK_RED : UK_ORANGE;
    } else if (me->gauge >= FIGHT_GAUGE_BAR) {  // 有气:换成大招提示,一闪一闪
        snprintf(v->hint, sizeof(v->hint), "%s", TX_KEYS_SUPER);
        v->hint_color = (s_clock / 300) & 1 ? UK_RED : UK_GOLD;
    } else {
        snprintf(v->hint, sizeof(v->hint), "%s", TX_KEYS);
    }
}

static void show_game(void) {
    if (s_screen != SCR_GAME || s_game_me != G->me_char || s_game_peer != G->peer_char) {
        s_game_me = G->me_char;
        s_game_peer = G->peer_char;
        s_screen = SCR_GAME;
        s_note[0] = 0;
        fight_ui_game(s_game_me, s_game_peer);
    }
    fight_view_t *v = &s_view;
    memset(v, 0, sizeof(*v));
    const fight_game_t *g = G;
    int won = winner_side();
    for (int i = 0; i < 2; i++) side_view(&v->s[i], i, won);
    snprintf(v->score, sizeof(v->score), TX_SCORE, (unsigned)g->score_me, (unsigned)g->score_peer);
    uint32_t left = g->sim.clock < FIGHT_ROUND_MS ? FIGHT_ROUND_MS - g->sim.clock : 0;
    if (g->phase != GP_OVER) snprintf(v->timer, sizeof(v->timer), "%lu", (unsigned long)((left + 999) / 1000));
    v->big_color = v->sub_color = UK_INK;
    v->mash = -1;
    v->clock = s_clock;
    v->shake = (int8_t)(s_shake_ms ? ((s_clock / 40) & 1 ? 4 : -4) : 0);
    switch (g->phase) {
        case GP_COUNT:
            snprintf(v->big, sizeof(v->big), "%u", (unsigned)(g->phase_ms / 1000 + 1));
            snprintf(v->sub, sizeof(v->sub), TX_ROUND, (unsigned)g->round);
            if (s_mode == MODE_CPU) snprintf(v->hint, sizeof(v->hint), TX_STAGE, (unsigned)s_stage);
            else snprintf(v->hint, sizeof(v->hint), "%s", TX_KEYS);
            v->hint_color = UK_INK;
            break;
        case GP_PLAY: {
            const fight_fighter_t *me = &g->sim.f[g->me];
            if (s_fight_ms) {
                snprintf(v->big, sizeof(v->big), "%s", TX_FIGHT);
                v->big_color = UK_RED;
            }
            if (s_say_ms) {
                snprintf(v->sub, sizeof(v->sub), "%s", s_say);
                v->sub_color = s_say_color;
            }
            if (me->st == FS_DIZZY && me->dur) v->mash = (int16_t)(1000 - (uint32_t)me->mash_left * 1000 / me->dur);
            keys_hint(v);
            break;
        }
        case GP_END: {
            bool ko = g->sim.f[0].st == FS_KO || g->sim.f[1].st == FS_KO;
            snprintf(v->big, sizeof(v->big), "%s", ko ? TX_KO : TX_TIMEUP);
            v->big_color = UK_RED;
            if (g->phase_ms + RESULT_MS <= FIGHT_NEXT_MS) {
                snprintf(v->sub, sizeof(v->sub), "%s", won < 0 ? TX_ROUND_DRAW : won == 0 ? TX_ROUND_ME : TX_ROUND_PEER);
                v->sub_color = won == 0 ? UK_RED : UK_INK;
            }
            break;
        }
        case GP_OVER: {
            bool w = won == 0;
            const char *big = w ? TX_WIN : TX_LOSE, *hint = g->me_again ? TX_WAIT_AGAIN : TX_AGAIN;
            if (s_mode == MODE_CPU) {
                big = s_cleared ? TX_CLEAR : big;
                hint = s_cleared ? TX_CLEAR_HINT : w ? TX_NEXT : TX_RETRY;
                snprintf(v->sub, sizeof(v->sub), TX_STAGE, (unsigned)s_stage);
            }
            snprintf(v->big, sizeof(v->big), "%s", big);
            v->big_color = w ? UK_RED : UK_INK;
            snprintf(v->hint, sizeof(v->hint), "%s", s_over_ms ? "" : hint);
            v->hint_color = UK_INK;
            break;
        }
        default: break;
    }
    if (s_cut) {
        v->cut = true;
        v->cut_char = s_cut_char;
        v->cut_name = SUPER[s_cut_char];
        v->cut_t = s_cut_ms;
    }
    fight_ui_game_set(v);
}

// 联机:收发消息、断线;返回现在能不能显示游戏页
static bool link_frame(uint32_t dt) {
    kit_link_state_t st = kit_link_state();
    if (st == KIT_LINK_CONNECTED) {
        if (!s_up || kit_link_session() != s_session) {
            s_up = true;
            s_session = kit_link_session();
            fight_game_connected(&s_link, kit_link_initiator());
        }
        uint8_t msg[KIT_LINK_MSG_MAX];
        size_t n;
        while ((n = kit_link_recv(msg, sizeof(msg))) > 0) fight_game_on_msg(&s_link, msg, n);
    } else if (s_up) {
        s_up = false;
        fight_game_lost(&s_link);
    }
    fight_game_tick(&s_link, dt);
    bool game = st == KIT_LINK_CONNECTED && s_link.phase != GP_WAIT;
    if (!game) show_link(st);
    return game;
}

// 和电脑打:两边 10 ms 一步一起走,电脑按自己的脑子按键;进选人页时电脑(和「下一关」时的自己)自动选人
static void cpu_frame(uint32_t dt) {
    fight_game_t *me = &s_loc.me, *cpu = &s_loc.cpu;
    if (me->phase == GP_PICK && !me->me_picked && s_auto) pick(s_save.last_char);
    if (cpu->phase == GP_PICK && !cpu->me_picked && me->me_picked) {
        fight_bot_init(&s_bot, LEVEL[s_stage - 1], rnd);
        fight_game_pick(cpu, s_order[s_stage - 1]);
    }
    for (s_acc += dt; s_acc >= FIGHT_STEP; s_acc -= FIGHT_STEP) {
        fight_game_tick(me, FIGHT_STEP);
        fight_game_tick(cpu, FIGHT_STEP);
        fight_local_flow(&s_loc);
        if (cpu->phase != GP_PLAY) continue;  // 电脑的脑子在选人时才建好;只有开打时按的键才算
        fight_seen_t v;
        fight_seen(&cpu->sim, cpu->me, &v);
        int k = fight_bot_step(&s_bot, &v, FIGHT_STEP);
        if (k >= 0) fight_game_key(cpu, (uint8_t)k);
        fight_local_flow(&s_loc);
    }
    uint8_t ev;
    while (fight_game_event(cpu, &ev, NULL)) {}
}

static void fx_frame(uint32_t dt) {
    uint32_t *t[] = { &s_quiet, &s_over_ms, &s_pick_deaf, &s_fight_ms, &s_say_ms, &s_shake_ms,
                      &s_flash[0], &s_flash[1], &s_spark[0], &s_spark[1], &s_shield[0], &s_shield[1] };
    for (size_t i = 0; i < sizeof t / sizeof t[0]; i++) uk_countdown(t[i], dt);
    for (int i = 0; i < 2; i++) {  // 刚掉的血慢慢收(约 0.4 秒收 100‰);新回合满血
        const fight_fighter_t *f = &G->sim.f[sim_of(i)];
        int hp = (f->hp < 0 ? 0 : f->hp) * 1000 / fight_char(f->ch)->hp, step = (int)dt / 4 + 1;
        s_trail[i] = (uint16_t)(s_trail[i] > hp + step ? s_trail[i] - step : hp);
    }
    if (s_cut) {
        s_cut_ms += dt;
        if (s_cut_ms >= FIGHT_CUT_MS) {  // 特写完了:大招砸下来,震屏
            s_cut = false;
            s_shake_ms = SHAKE_MS;
            hit_fx(!s_cut_side);
            sound(SFX_HIT);
        }
    }
    if (G->phase == GP_COUNT) {  // 倒数 3、2、1 每个数「嘀」一下
        uint8_t n = (uint8_t)(G->phase_ms / 1000 + 1);
        if (n != s_count_shown) {
            s_count_shown = n;
            sound(SFX_TICK);
        }
    }
}

static void fight_frame(uint32_t dt) {
    if (s_page == PG_TITLE || s_page == PG_HELP) return;
    bool game = true;
    if (s_mode == MODE_LINK) game = link_frame(dt);
    else cpu_frame(dt);
    uint8_t ev;
    fight_ev_t se;
    while (fight_game_event(G, &ev, &se)) on_event(ev, &se);
    fx_frame(dt);
    s_clock += dt;
    s_page = game ? PG_GAME : PG_LINK;
    if (!game) {
        s_screen = SCR_OTHER;
        return;
    }
    if (G->phase == GP_PICK) show_pick();
    else if (G->phase != GP_WAIT) show_game();
}

// 一场没打完(包括联机选人、断线等对方回来)都不让屏幕熄。和电脑打时停在选人页可以熄(没人等着,同 coopa-bomb)。
static unsigned fight_busy(void) {
    bool mid = s_page == PG_GAME ? G->phase != GP_OVER && !(s_mode == MODE_CPU && G->phase == GP_PICK)
                                 : s_page == PG_LINK && s_link.match && !match_over(&s_link);
    return mid ? KIT_BUSY_TIMING : 0;
}

static bool fight_won(void) { return s_save.crowns != 0; }

static uint8_t fight_milestones(void) { return fight_save_milestones(&s_save); }

// 调试通道状态行(格式冻结,tests/sim_cpu.py、sim_duo.py 在读;全是整数,空格分开):
// FT <页> <阶段> <模式> <我的人> <对方的人> <回合> <我的分> <对方分> <血0> <血1> <气0> <气1> <状态0> <状态1>
//    <状态过了多久0> <状态过了多久1> <连招窗口开着> <第几关>
// 0 = 自己、1 = 对方(不是 sim.f[] 的下标);页 = page_t,阶段 = fight_phase_t,状态 = fight_state_t;人 255 = 还没选。
static size_t fight_status(char *buf, size_t len) {
    const fight_game_t *g = G;
    const fight_fighter_t *a = &g->sim.f[g->me], *b = &g->sim.f[!g->me];
    fight_seen_t v;
    fight_seen(&g->sim, g->me, &v);
    int n = snprintf(buf, len, "FT %d %d %u %u %u %u %u %u %d %d %u %u %u %u %u %u %d %u\n", (int)s_page,
                     (int)g->phase, (unsigned)s_mode, (unsigned)g->me_char, (unsigned)g->peer_char, (unsigned)g->round,
                     (unsigned)g->score_me, (unsigned)g->score_peer, (int)a->hp, (int)b->hp, (unsigned)a->gauge,
                     (unsigned)b->gauge, (unsigned)a->st, (unsigned)b->st, (unsigned)a->t, (unsigned)b->t,
                     (int)v.chain_open, (unsigned)s_stage);
    return n < 0 ? 0 : ((size_t)n < len ? (size_t)n : len - 1);
}

static void fight_save_now(void) { store(); }

static const kit_menu_card_t FIGHT_CARD = {
    .title = TX_TITLE,
    .sub = TX_CARD_SUB,
    .art = { SPR_KID, SPR_DAD },
};

const kit_app_t fight_app = {
    .id = "fight",
    .sprite_budget = 0,  // 图都是游戏块里的常量图(fight_art.h),不用精灵会话
    .init = fight_init_app,
    .enter = fight_enter,
    .input = fight_input,
    .frame = fight_frame,
    .busy = fight_busy,
    .won = fight_won,
    .milestones = fight_milestones,
    .status = fight_status,
    .save_now = fight_save_now,
    .card = &FIGHT_CARD,
};
