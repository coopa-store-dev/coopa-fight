// src/fight_save.h —— 库巴格斗的存档(saves 分区的 "fight"/"save"):上次选的人、闯关通关、打了几场。
// 纯数据和规整,读写由 fight_app.c 用 kit_sv_* 做(同 coopa-bomb 的 bomb_save)。
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define FIGHT_NS "fight"    // = coopa.toml 的 id
#define FIGHT_KEY "save"
#define FIGHT_SAVE_VER 1
#define FIGHT_SAVE_CHARS 4  // = 角色数(存档不依赖规则头文件)

typedef struct {
    uint8_t ver;
    uint8_t last_char;      // 上次选的人;>= FIGHT_SAVE_CHARS 当 0
    uint8_t crowns;         // 低 4 位:哪几个人打赢过电脑闯关
    uint8_t seen_tutorial;  // 0 或 1
    uint32_t wins, matches, supers;
    uint32_t counted;       // 最后记过胜负的场号(断线连回来结束页再来一次也只记一次)
    uint8_t pad[4];
} fight_save_t;

void fight_save_reset(fight_save_t *s);        // 全 0、ver = FIGHT_SAVE_VER
bool fight_save_valid(const void *blob);       // 给 kit_sv_blob_load:版本对
void fight_save_clean(fight_save_t *s);        // 读到以后规整越界的字段
bool fight_save_over(fight_save_t *s, uint32_t match, bool won);  // 一场打完(同一场只记一次);返回 true = 有变化
// 里程碑位:0 第一次打出大招(supers>=1)、1 赢一场、2 赢 10 场。
uint8_t fight_save_milestones(const fight_save_t *s);
bool fight_save_crown(const fight_save_t *s, uint8_t c);
bool fight_save_set_crown(fight_save_t *s, uint8_t c);  // 返回 true = 新拿到;越界 false
