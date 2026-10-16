// src/fight_art.h —— 人物、特效、热血头像的像素画(tools/gen_art.py 从 art/*.txt 生成,勿手改)。
// 全是 LV_COLOR_FORMAT_RGB565A8 的常量图,放在游戏块(flash)里,LVGL 直接读,不进堆;画面上 lv_image_set_scale 放大。
// 人物 16×16,原图朝右,facing_left 给水平翻转的那份。下标 ch = fight_char_t(0 库巴、1 爸爸、2 奶奶、3 爷爷)。
//   FA_IDLE:站着(拳头举在胸前)
//   FA_JAB:轻拳(手伸出去)
//   FA_WINDUP:抡重击(手往后拉)
//   FA_HEAVY:重击(砸出去)
//   FA_GUARD:举盾(双臂交叉挡在前面)
//   FA_HIT:挨打(往后仰)
//   FA_DIZZY:晕(歪着、眼睛半闭)
//   FA_KO:倒下(躺着)
//   FA_SPECIAL:小招
//   FA_WIN:赢了(双手举起)
//   FX_STAR:晕的星星
//   FX_SPARK:打中的火花
//   FX_SHIELD:挡住的盾
//   FX_PELLET:库巴的弹珠
//   FX_LID:奶奶的锅盖
//   FX_CAGE:爸爸大招的铁笼
//   FX_TAIJI:爷爷的太极
//   FX_ROLLER:奶奶大招的擀面杖
//   FX_HAMMER:爸爸大招的大锤
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "lvgl.h"

typedef enum { FA_IDLE = 0, FA_JAB, FA_WINDUP, FA_HEAVY, FA_GUARD, FA_HIT, FA_DIZZY, FA_KO, FA_SPECIAL, FA_WIN, FA_POSES } fight_pose_t;
typedef enum { FX_STAR = 0, FX_SPARK, FX_SHIELD, FX_PELLET, FX_LID, FX_CAGE, FX_TAIJI, FX_ROLLER, FX_HAMMER, FX_COUNT } fight_fx_t;

const lv_image_dsc_t *fight_art_pose(uint8_t ch, uint8_t pose, bool facing_left);  // 越界返回库巴站立
const lv_image_dsc_t *fight_art_fx(uint8_t fx);    // 越界返回 NULL
const lv_image_dsc_t *fight_art_hot(uint8_t ch);   // 特写大头像(传炸弹的热血头像);越界返回库巴
