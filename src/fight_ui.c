// src/fight_ui.c —— 见 fight_ui.h。对打页的控件在 fight_ui_game 里一次建好,每帧只改图、位置、文字、颜色、显隐。
// 首页、选人页、特写的样子照 coopa-bomb 的 bomb_ui.c。
#include "fight_ui.h"

#include <stdio.h>
#include <string.h>

#include "fight_art.h"
#include "fight_moves.h"
#include "fight_sim.h"
#include "fight_texts.h"
#include "ui_kit.h"

#define S 4            // 对打页的人物、特效放大 4 倍(16 → 64 像素)
#define FIG 16
#define ME_X 24        // 自己(左)放大后的左上角
#define PEER_X 152     // 对方(右)
#define FIG_Y 146
#define FLOOR_Y (FIG_Y + FIG * S)  // 脚踩在地上
#define DIST (PEER_X - ME_X)       // 两个人中心隔多远
#define REACH (FIG * S / 2)        // 弹珠、锅盖从身前(离自己中心这么远)飞出去
#define FRONT (FIG * S / 2 - 4)    // 火花、盾画在身前多远
#define BAR_Y 12       // 血条
#define BAR_W 82
#define BAR_H 14
#define BAR_L 22       // 左边血条的左端;右边的右端在 UK_W - BAR_L
#define PIP_Y 48       // 气
#define PIP_W 26
#define PIP_H 9
#define STARS 5
#define MASH_X 40      // 狂按进度条
#define MASH_Y 224
#define MASH_W 160
#define RAYS 16
#define CUT_FACE 10    // 特写大头像放大 10 倍
#define TWIN 6         // 库巴的影分身
#define TITLE_S 3      // 首页的两个人

static const char *const NAMES[FC_COUNT] = { TX_KID, TX_DAD, TX_GRANNY, TX_GRANDPA };

typedef struct {
    lv_obj_t *fig, *spark, *shield, *prop, *star[STARS];
    lv_obj_t *trail, *hp;
    lv_obj_t *pip[3], *pip_fill[3];
    uint8_t ch;
} side_t;

static lv_obj_t *s_scr, *s_timer, *s_score, *s_big, *s_sub, *s_hint, *s_mash_bg, *s_mash;
static side_t s_side[2];
static lv_obj_t *s_cut_bg, *s_ray[RAYS], *s_cut_face, *s_cut_twin[2], *s_cut_prop, *s_cut_name;
static lv_point_precise_t s_ray_pts[RAYS][2];

static lv_obj_t *centered(lv_obj_t *scr, const lv_font_t *f, uint32_t color, const char *s, int y) {
    lv_obj_t *t = uk_text(scr, f, color, s);
    lv_obj_set_style_text_align(t, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(t, LV_ALIGN_TOP_MID, 0, y);
    return t;
}

// 常量图放大 scale 倍;控件保持原图大小、以中心为轴放大
static lv_obj_t *image(lv_obj_t *scr, const lv_image_dsc_t *d, int scale) {
    lv_obj_t *o = lv_image_create(scr);
    lv_image_set_src(o, d);
    lv_obj_set_size(o, d->header.w, d->header.h);
    lv_image_set_scale(o, (uint32_t)(256 * scale));
    lv_image_set_antialias(o, false);
    return o;
}

// 换图(没变就不动);尺寸跟着图走
static void set_img(lv_obj_t *o, const lv_image_dsc_t *d) {
    if (!d || lv_image_get_src(o) == d) return;
    lv_image_set_src(o, d);
    lv_obj_set_size(o, d->header.w, d->header.h);
}

// 控件上的图的原尺寸(这里的图都是 lv_image_dsc_t;控件尺寸要等 LVGL 排版才更新,不能拿来算)
static const lv_image_header_t *hdr(lv_obj_t *o) { return &((const lv_image_dsc_t *)lv_image_get_src(o))->header; }

// 每帧改的属性:没变就不调 LVGL(set 一次就要重新排版、重画那一块,卡上每帧几十个控件吃不消;uk_show、uk_set_text 自己会比)
static void pos(lv_obj_t *o, int x, int y) {
    if (lv_obj_get_style_x(o, 0) != x || lv_obj_get_style_y(o, 0) != y) lv_obj_set_pos(o, x, y);
}

static void set_x(lv_obj_t *o, int x) {
    if (lv_obj_get_style_x(o, 0) != x) lv_obj_set_x(o, x);
}

static void set_w(lv_obj_t *o, int w) {
    if (lv_obj_get_style_width(o, 0) != w) lv_obj_set_width(o, w);
}

static void bg(lv_obj_t *o, uint32_t c) {
    if (!lv_color_eq(lv_obj_get_style_bg_color(o, 0), lv_color_hex(c))) lv_obj_set_style_bg_color(o, lv_color_hex(c), 0);
}

static void ink(lv_obj_t *o, uint32_t c) {
    if (!lv_color_eq(lv_obj_get_style_text_color(o, 0), lv_color_hex(c))) lv_obj_set_style_text_color(o, lv_color_hex(c), 0);
}

static void tint(lv_obj_t *o, uint32_t c, lv_opa_t opa) {
    if (!lv_color_eq(lv_obj_get_style_image_recolor(o, 0), lv_color_hex(c)))
        lv_obj_set_style_image_recolor(o, lv_color_hex(c), 0);
    if (lv_obj_get_style_image_recolor_opa(o, 0) != opa) lv_obj_set_style_image_recolor_opa(o, opa, 0);
}

// (x, y) = 放大后的左上角
static void place(lv_obj_t *o, int x, int y, int scale) {
    int w = hdr(o)->w, h = hdr(o)->h;
    pos(o, x + (w * scale - w) / 2, y + (h * scale - h) / 2);
}

// 放大后的中心放在 (cx, cy)
static void place_c(lv_obj_t *o, int cx, int cy) { pos(o, cx - hdr(o)->w / 2, cy - hdr(o)->h / 2); }

static void crown(lv_obj_t *scr, int x, int y) {  // 金色小皇冠:底座加三个尖
    uk_rect(scr, x, y + 6, 22, 7, UK_GOLD);
    for (int i = 0; i < 3; i++) uk_rect(scr, x + i * 9, y, 4, 7, UK_GOLD);
}

void fight_ui_title(uint8_t sel, unsigned wins) {
    lv_obj_t *scr = uk_screen_new(UK_WALL);
    centered(scr, UK_F36, UK_RED, TX_TITLE, 20);
    centered(scr, UK_F12, UK_INK, TX_SUB, 64);
    place(image(scr, fight_art_pose(FC_KID, FA_JAB, false), TITLE_S), 40, 82, TITLE_S);
    place(image(scr, fight_art_pose(FC_DAD, FA_GUARD, true), TITLE_S), 152, 82, TITLE_S);
    place(image(scr, fight_art_fx(FX_SPARK), 2), 108, 92, 2);
    char w[32];
    snprintf(w, sizeof(w), TX_WINS, wins);
    centered(scr, UK_F12, UK_INK, w, 134);
    const char *items[4] = { TX_CPU, TX_LINK, TX_HELP, TX_BACK };
    for (int i = 0; i < 4; i++) {
        lv_obj_t *p = uk_pill(scr, UK_F24, sel == i ? UK_RED : UK_LINE, UK_WHITE, items[i]);
        lv_obj_align(p, LV_ALIGN_TOP_MID, 0, 154 + i * 38);
    }
    uk_screen_swap(scr);
}

void fight_ui_note(const char *big, uint32_t color, const char *l1, const char *l2, const char *l3) {
    lv_obj_t *scr = uk_screen_new(UK_WALL);
    place(image(scr, fight_art_pose(FC_KID, FA_IDLE, false), 4), 40, 30, 4);
    place(image(scr, fight_art_pose(FC_DAD, FA_IDLE, true), 4), 136, 30, 4);
    centered(scr, UK_F24, color, big, 120);
    centered(scr, UK_F12, UK_INK, l1, 170);
    centered(scr, UK_F12, UK_INK, l2, 200);
    centered(scr, UK_F12, UK_INK, l3, 270);
    uk_screen_swap(scr);
}

void fight_ui_pick(uint8_t sel, bool picked, uint8_t crowns, const char *title, const char *hint, const char *peer) {
    lv_obj_t *scr = uk_screen_new(UK_WALL);
    centered(scr, UK_F24, UK_INK, title, 6);
    for (int f = 0; f < FC_COUNT; f++) {
        int cx = (f % 2) * 120, cy = 40 + (f / 2) * 104;
        if (sel == f) uk_rect(scr, cx + 6, cy, 108, 100, picked ? UK_GREEN : UK_GOLD);
        uk_rect(scr, cx + 10, cy + 4, 100, 92, UK_WAINSCOT);
        place(image(scr, fight_art_hot((uint8_t)f), 4), cx + 28, cy + 6, 4);
        if (crowns >> f & 1) crown(scr, cx + 82, cy + 8);
        lv_obj_t *n = uk_text(scr, UK_F12, UK_INK, NAMES[f]);
        lv_obj_set_width(n, 100);
        lv_obj_set_style_text_align(n, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_pos(n, cx + 10, cy + 74);
    }
    centered(scr, UK_F12, UK_INK, peer, 252);
    centered(scr, UK_F12, UK_INK, hint, 270);
    lv_obj_t *home = uk_pill(scr, UK_F12, sel == FC_COUNT ? UK_RED : UK_LINE, UK_WHITE, TX_HOME);
    lv_obj_align(home, LV_ALIGN_TOP_MID, 0, 290);
    uk_screen_swap(scr);
}

// 出招表一行:左边一个动作(放大 2 倍),中间按键(大字),右边 / 下面一行小字
static void help_row(lv_obj_t *scr, const lv_image_dsc_t *img, int y, const char *keys, const char *what, uint32_t c) {
    place(image(scr, img, 2), 22, y, 2);
    lv_obj_t *t = uk_text(scr, UK_F24, UK_INK, keys);
    lv_obj_set_pos(t, 62, y + 4);
    t = uk_text(scr, UK_F12, c, what);
    lv_obj_align(t, LV_ALIGN_TOP_RIGHT, -20, y + 10);
}

void fight_ui_help(uint8_t ch) {
    if (ch >= FC_COUNT) ch = FC_KID;
    lv_obj_t *scr = uk_screen_new(UK_WALL);
    centered(scr, UK_F24, UK_RED, TX_HELP, 6);
    help_row(scr, fight_art_pose(ch, FA_JAB, false), 36, TX_HELP_JAB, TX_HELP_JAB_WHAT, UK_FLOOR_D);
    help_row(scr, fight_art_pose(ch, FA_HEAVY, false), 72, TX_HELP_HEAVY, TX_HELP_HEAVY_WHAT, UK_FLOOR_D);
    help_row(scr, fight_art_pose(ch, FA_GUARD, false), 108, TX_HELP_GUARD, TX_HELP_GUARD_WHAT, UK_FLOOR_D);
    help_row(scr, fight_art_pose(ch, FA_SPECIAL, false), 144, TX_HELP_SPECIAL, TX_HELP_SPECIAL_WHAT, UK_GREEN);
    // 大招最要紧:两行大字
    uk_rect(scr, 16, 182, UK_W - 32, 2, UK_LINE);
    place(image(scr, fight_art_hot(ch), 3), 18, 194, 3);
    lv_obj_t *t = uk_text(scr, UK_F24, UK_RED, TX_HELP_SUPER);
    lv_obj_set_pos(t, 72, 190);
    t = uk_text(scr, UK_F24, UK_RED, TX_HELP_SUPER2);
    lv_obj_set_pos(t, 72, 218);
    centered(scr, UK_F12, UK_INK, TX_HELP_TAP, 250);
    centered(scr, UK_F12, UK_INK, TX_HELP_MASH, 266);
    lv_obj_t *ok = uk_pill(scr, UK_F12, UK_RED, UK_WHITE, TX_HELP_OK);
    lv_obj_align(ok, LV_ALIGN_TOP_MID, 0, 288);
    uk_screen_swap(scr);
}

// 一边的顶栏(血条、名字、气)和台上的人。side 0 在左边朝右,1 在右边朝左。
static void make_side(int i, uint8_t ch, bool same) {
    side_t *sd = &s_side[i];
    sd->ch = ch;
    int bx = i ? UK_W - BAR_L - BAR_W : BAR_L;
    uk_rect(s_scr, bx - 2, BAR_Y - 2, BAR_W + 4, BAR_H + 4, UK_INK);
    uk_rect(s_scr, bx, BAR_Y, BAR_W, BAR_H, UK_FLOOR_D);
    sd->trail = uk_rect(s_scr, bx, BAR_Y, BAR_W, BAR_H, UK_WHITE);
    sd->hp = uk_rect(s_scr, bx, BAR_Y, BAR_W, BAR_H, UK_GREEN);
    lv_obj_t *n = uk_text(s_scr, UK_F12, same && i ? UK_BLUE : UK_INK, NAMES[ch]);
    if (i) lv_obj_align(n, LV_ALIGN_TOP_RIGHT, -BAR_L, BAR_Y + BAR_H + 4);
    else lv_obj_set_pos(n, BAR_L, BAR_Y + BAR_H + 4);
    for (int k = 0; k < 3; k++) {
        int px = i ? UK_W - BAR_L - PIP_W - k * (PIP_W + 4) : BAR_L + k * (PIP_W + 4);
        sd->pip[k] = uk_rect(s_scr, px, PIP_Y, PIP_W, PIP_H, UK_LINE);
        sd->pip_fill[k] = uk_rect(s_scr, px, PIP_Y, 0, PIP_H, UK_ORANGE);
    }
    sd->fig = image(s_scr, fight_art_pose(ch, FA_IDLE, i == 1), S);
    // 两边同一个人:对方染一点蓝,分得清
    if (same && i) {
        lv_obj_set_style_image_recolor(sd->fig, lv_color_hex(UK_BLUE), 0);
        lv_obj_set_style_image_recolor_opa(sd->fig, LV_OPA_30, 0);
    }
    sd->prop = image(s_scr, fight_art_fx(FX_PELLET), S);
    sd->shield = image(s_scr, fight_art_fx(FX_SHIELD), S);
    sd->spark = image(s_scr, fight_art_fx(FX_SPARK), S);
    for (int k = 0; k < STARS; k++) sd->star[k] = image(s_scr, fight_art_fx(FX_STAR), 2);
}

void fight_ui_game(uint8_t me_char, uint8_t peer_char) {
    if (me_char >= FC_COUNT) me_char = FC_KID;
    if (peer_char >= FC_COUNT) peer_char = FC_KID;
    s_scr = uk_screen_new(UK_WALL);
    uk_rect(s_scr, 0, FLOOR_Y, UK_W, UK_H - FLOOR_Y, UK_WAINSCOT);
    uk_rect(s_scr, 0, FLOOR_Y, UK_W, 3, UK_FLOOR);
    s_timer = centered(s_scr, UK_F24, UK_INK, "", 6);
    s_score = centered(s_scr, UK_F12, UK_INK, "", 34);
    make_side(0, me_char, false);
    make_side(1, peer_char, me_char == peer_char);
    s_big = centered(s_scr, UK_F36, UK_INK, "", 60);
    s_sub = centered(s_scr, UK_F24, UK_INK, "", 100);  // 下面是晕的星星(FIG_Y 往上 20 像素以内)
    s_mash_bg = uk_rect(s_scr, MASH_X - 2, MASH_Y - 2, MASH_W + 4, 14, UK_INK);
    s_mash = uk_rect(s_scr, MASH_X, MASH_Y, 0, 10, UK_GOLD);
    s_hint = centered(s_scr, UK_F24, UK_INK, "", 250);
    // 特写(最上层):黑底、放射速度线、大头像(库巴的影分身多两个)、道具、招式名
    s_cut_bg = uk_rect(s_scr, 0, 0, UK_W, UK_H, UK_INK);
    for (int i = 0; i < RAYS; i++) {
        s_ray[i] = lv_line_create(s_scr);
        lv_obj_set_style_line_width(s_ray[i], 6, 0);
        lv_obj_set_style_line_rounded(s_ray[i], false, 0);
    }
    s_cut_face = image(s_scr, fight_art_hot(me_char), CUT_FACE);
    for (int i = 0; i < 2; i++) s_cut_twin[i] = image(s_scr, fight_art_hot(FC_KID), TWIN);
    s_cut_prop = image(s_scr, fight_art_fx(FX_CAGE), 5);
    s_cut_name = uk_pill(s_scr, UK_F36, UK_INK, UK_GOLD, "");  // 黑底:速度线压不住字
    lv_obj_set_style_text_align(s_cut_name, LV_TEXT_ALIGN_CENTER, 0);
    uk_screen_swap(s_scr);
}

// ---- 每帧 ----

static uint32_t hp_color(uint16_t hp) { return hp > 500 ? UK_GREEN : hp > 250 ? UK_GOLD : UK_RED; }

static void side_set(int i, const fight_side_view_t *v, int shake, uint32_t clock) {
    side_t *sd = &s_side[i];
    // 血条:左边从左往右排、右边从右往左排,掉血从里侧(靠计时那头)往外缩
    int bx = i ? UK_W - BAR_L - BAR_W : BAR_L;
    int w = BAR_W * (v->hp > 1000 ? 1000 : v->hp) / 1000, tw = BAR_W * (v->trail > 1000 ? 1000 : v->trail) / 1000;
    set_w(sd->hp, w);
    set_x(sd->hp, i ? bx + BAR_W - w : bx);
    bg(sd->hp, hp_color(v->hp));
    set_w(sd->trail, tw);
    set_x(sd->trail, i ? bx + BAR_W - tw : bx);
    for (int k = 0; k < 3; k++) {
        int part = (int)v->gauge - k * 1000;
        part = part < 0 ? 0 : part > 1000 ? 1000 : part;
        set_w(sd->pip_fill[k], PIP_W * part / 1000);
        int px = i ? UK_W - BAR_L - PIP_W - k * (PIP_W + 4) : BAR_L + k * (PIP_W + 4);
        if (i) set_x(sd->pip_fill[k], px + PIP_W - PIP_W * part / 1000);
        bg(sd->pip_fill[k], part == 1000 ? (v->gauge_blink ? UK_WHITE : UK_GOLD) : UK_ORANGE);
    }
    // 人
    int dir = i ? -1 : 1, x = (i ? PEER_X : ME_X) + dir * v->dx + shake;
    set_img(sd->fig, fight_art_pose(sd->ch, v->pose, i == 1));
    place(sd->fig, x, FIG_Y, S);
    if (v->flash) tint(sd->fig, UK_WHITE, LV_OPA_70);
    else tint(sd->fig, UK_BLUE, i && s_side[0].ch == s_side[1].ch ? LV_OPA_30 : LV_OPA_TRANSP);
    int cx = x + FIG * S / 2, cy = FIG_Y + FIG * S / 2, front = cx + dir * FRONT;
    uk_show(sd->spark, v->spark);
    if (v->spark) place_c(sd->spark, front - dir * 8, cy - 10);  // 火花画在挨打的人身前,放大后的中心
    uk_show(sd->shield, v->shield);
    if (v->shield) place_c(sd->shield, front + dir * 4, cy);
    uk_show(sd->prop, v->prop != 0);
    if (v->prop) {
        set_img(sd->prop, fight_art_fx((uint8_t)(v->prop - 1)));
        place_c(sd->prop, cx + dir * v->prop_x, v->prop - 1 == FX_CAGE ? cy : cy - 4);
    }
    for (int k = 0; k < STARS; k++) {  // 星星在头顶排一圈,一上一下地转
        bool on = k < v->stars;
        uk_show(sd->star[k], on);
        if (!on) continue;
        int n = v->stars, sx = cx - (n - 1) * 8 + k * 16;
        int sy = FIG_Y - 8 + uk_tri(clock + (uint32_t)k * 120, 600, 6) - 3;
        place_c(sd->star[k], sx, sy);
    }
}

// 一个人的动作图、往前挪多少、小招的道具(弹珠、锅盖飞过去,太极架势)
void fight_ui_pose(const fight_fighter_t *f, fight_side_view_t *v) {
    const fight_char_def_t *c = fight_char(f->ch);
    const fight_move_def_t *d = &c->mv[f->move < FM_COUNT ? f->move : 0];
    uint16_t hit_at = d->startup, out = (uint16_t)(d->startup + d->active), end = (uint16_t)(out + d->recovery);
    v->dx = 0;
    v->prop = 0;
    v->prop_x = 0;
    switch (f->st) {
        case FS_MOVE:
            switch (f->move) {
                case FM_HEAVY:
                    if (f->t < hit_at) {
                        v->dx = -4;
                        v->pose = FA_WINDUP;
                    } else {
                        v->dx = f->t < out ? 12 : 4;
                        v->pose = f->t < out + d->recovery / 2 ? FA_HEAVY : FA_IDLE;
                    }
                    break;
                case FM_HEAVY_LINK:
                case FM_SUPER:
                    v->dx = f->t < out ? 12 : 0;
                    v->pose = f->t < out ? FA_HEAVY : FA_IDLE;
                    break;
                case FM_SPECIAL:
                    v->pose = f->t < out ? FA_SPECIAL : FA_IDLE;
                    if (f->ch == FC_DAD && f->t < out) v->dx = (int8_t)(f->t < hit_at ? 16 * f->t / hit_at : 16);  // 肩撞冲过去
                    // 弹珠飞到对方身上;锅盖飞过去再飞回来
                    if (f->ch == FC_KID && f->t < hit_at) {
                        v->prop = FX_PELLET + 1;
                        v->prop_x = (int16_t)(REACH + (DIST - REACH) * f->t / (hit_at ? hit_at : 1));
                    } else if (f->ch == FC_GRANNY && f->t < end) {
                        v->prop = FX_LID + 1;
                        v->prop_x = (int16_t)(f->t < hit_at ? REACH + (DIST - REACH) * f->t / (hit_at ? hit_at : 1)
                                                            : DIST - (DIST - REACH) * (f->t - hit_at) / (end - hit_at));
                    }
                    break;
                default:  // 轻拳
                    v->dx = f->t >= hit_at / 2 && f->t < out ? 8 : 0;
                    v->pose = f->t < out ? FA_JAB : FA_IDLE;
                    break;
            }
            break;
        case FS_GUARD: v->pose = FA_GUARD; break;
        case FS_HITSTUN:
            v->dx = (int8_t)(f->t < 150 ? ((f->t / 40) & 1 ? -9 : -5) : -6);  // 往后退、抖两下
            v->pose = FA_HIT;
            break;
        case FS_BLOCKSTUN:  // 拳被挡住:手还伸着,往后弹一点
            v->dx = -3;
            v->pose = f->t < 150 ? FA_JAB : FA_IDLE;
            break;
        case FS_DIZZY:
            v->dx = (int8_t)(uk_tri(f->t, 800, 4) - 2);
            v->pose = FA_DIZZY;
            break;
        case FS_SUPER_IN:
            v->dx = -8;
            v->pose = FA_HIT;
            break;
        case FS_SUPER_OUT:
            v->dx = 12;
            v->pose = FA_HEAVY;
            break;
        case FS_STANCE:  // 爷爷的太极架势
            v->pose = FA_SPECIAL;
            v->prop = FX_TAIJI + 1;
            v->prop_x = FRONT + 8;
            break;
        case FS_KO:
            v->dx = -10;
            v->pose = FA_KO;
            break;
        default: v->pose = FA_IDLE; break;
    }
}

// 速度线:从屏幕中心往外,长度随时间涨;i 是第几根(同 coopa-bomb)
static void ray(int i, uint32_t t) {
    static const int8_t DX[RAYS] = { 0, 38, 71, 92, 100, 92, 71, 38, 0, -38, -71, -92, -100, -92, -71, -38 };
    static const int8_t DY[RAYS] = { -100, -92, -71, -38, 0, 38, 71, 92, 100, 92, 71, 38, 0, -38, -71, -92 };
    int r0 = 30 + (int)(t % 160) / 2, r1 = r0 + 60 + (int)(t / 4 % 120);
    s_ray_pts[i][0].x = UK_W / 2 + DX[i] * r0 / 100;
    s_ray_pts[i][0].y = UK_H / 2 + DY[i] * r0 / 100;
    s_ray_pts[i][1].x = UK_W / 2 + DX[i] * r1 / 100;
    s_ray_pts[i][1].y = UK_H / 2 + DY[i] * r1 / 100;
    lv_line_set_points(s_ray[i], s_ray_pts[i], 2);
    lv_obj_set_style_line_color(s_ray[i], lv_color_hex(((t / 80 + (uint32_t)i) & 1) ? UK_RED : UK_GOLD), 0);
}

// 大招特写:黑底、速度线;头像从左边冲进来,招式名从上面砸下来;然后道具砸下、连着几下白闪、震屏
static void cut_set(const fight_view_t *v) {
    bool on = v->cut;
    uk_show(s_cut_bg, on);
    for (int i = 0; i < RAYS; i++) {
        uk_show(s_ray[i], on);
        if (on) ray(i, v->cut_t);
    }
    uk_show(s_cut_face, on);
    uk_show(s_cut_name, on);
    uint32_t t = v->cut_t;
    bool twins = on && v->cut_char == FC_KID && t >= 450;
    for (int i = 0; i < 2; i++) uk_show(s_cut_twin[i], twins);
    bool prop = on && v->cut_char != FC_KID && t >= 550;
    uk_show(s_cut_prop, prop);
    if (!on) return;
    int shake = t >= 400 && t < 1400 ? ((t / 40) & 1 ? 5 : -5) : 0;
    bool hit = t >= 700 && (t - 700) % 200 < 60 && t < 1300;  // 连着三下打击:白闪
    bg(s_cut_bg, hit ? UK_WHITE : UK_INK);
    set_img(s_cut_face, fight_art_hot(v->cut_char));
    int x = t < 250 ? -160 + (int)(200 * t / 250) : 40;
    place(s_cut_face, x + shake, 24, CUT_FACE);
    if (twins) {
        for (int i = 0; i < 2; i++) {
            set_img(s_cut_twin[i], fight_art_hot(v->cut_char));
            int bob = ((t / 120 + (uint32_t)i) & 1) ? -12 : 0;  // 两个分身一左一右轮着冲上来
            place(s_cut_twin[i], (i ? 156 : -12) + shake, 96 + bob, TWIN);
        }
    }
    if (prop) {
        static const uint8_t PROP[FC_COUNT] = { FX_SPARK, FX_HAMMER, FX_ROLLER, FX_TAIJI };
        set_img(s_cut_prop, fight_art_fx(PROP[v->cut_char < FC_COUNT ? v->cut_char : 0]));
        int py = t < 750 ? -80 + (int)(220 * (t - 550) / 200) : 140;
        place_c(s_cut_prop, 120 + shake, py);
    }
    uk_set_text(s_cut_name, v->cut_name ? v->cut_name : "");
    int y = t < 200 ? -90 : t < 400 ? -90 + (int)(290 * (t - 200) / 200) : 200;
    if (lv_obj_get_style_x(s_cut_name, 0) != shake || lv_obj_get_style_y(s_cut_name, 0) != y)
        lv_obj_align(s_cut_name, LV_ALIGN_TOP_MID, shake, y);
    ink(s_cut_name, (t / 100) & 1 ? UK_GOLD : UK_WHITE);
}

void fight_ui_game_set(const fight_view_t *v) {
    uk_set_text(s_timer, v->timer);
    uk_set_text(s_score, v->score);
    uk_set_text(s_big, v->big);
    ink(s_big, v->big_color);
    uk_set_text(s_sub, v->sub);
    ink(s_sub, v->sub_color);
    uk_set_text(s_hint, v->hint);
    ink(s_hint, v->hint_color);
    for (int i = 0; i < 2; i++) side_set(i, &v->s[i], v->shake, v->clock);
    uk_show(s_mash_bg, v->mash >= 0);
    uk_show(s_mash, v->mash >= 0);
    if (v->mash >= 0) set_w(s_mash, MASH_W * (v->mash > 1000 ? 1000 : v->mash) / 1000);
    cut_set(v);
}
