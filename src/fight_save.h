// src/fight_save.h —— 库巴格斗的存档(saves 分区的 "fight"/"save"):上次选的人、闯关通关、打了几场。
// 读写走 kit_sv_*(命名空间 = coopa.toml 的 id);宿主测试用假的 kit_store.h。
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define FIGHT_NS "fight"
#define FIGHT_KEY "save"
#define FIGHT_SAVE_VER 1
#define FIGHT_SAVE_CHARS 4  // = 角色数(存档不依赖规则头文件)

typedef struct {
    uint8_t ver;
    uint8_t last_char;      // 上次选的人;>= FIGHT_SAVE_CHARS 当 0
    uint8_t crowns;         // 低 4 位:哪几个人打赢过电脑闯关
    uint8_t seen_tutorial;  // 0 或 1
    uint32_t wins, matches, supers;
    uint8_t pad[8];
} fight_save_t;

void fight_save_load(fight_save_t *s);         // 读不到 / 版本不对 → 全 0、ver=1;读到的字段会被规整
void fight_save_store(const fight_save_t *s);
// 里程碑位:0 第一次打出大招(supers>=1)、1 赢一场、2 赢 10 场。
uint8_t fight_save_milestones(const fight_save_t *s);
bool fight_save_crown(const fight_save_t *s, uint8_t c);
bool fight_save_set_crown(fight_save_t *s, uint8_t c);  // 返回 true = 新拿到;越界 false
