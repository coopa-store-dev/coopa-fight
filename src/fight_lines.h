// src/fight_lines.h —— 四个角色的专属语音(tools/gen_lines.py 生成,勿手改)。下标 = fight_char_t。
// 格式同 Coopa OS 内置台词(16 kHz IMA ADPCM),用 sfx_play_line 播放。台词:

// PICK_KID: 库巴来啦！
// PICK_DAD: 放马过来吧！
// PICK_GRANNY: 看奶奶的厉害！
// PICK_GRANDPA: 爷爷来陪你耍。
// SPECIAL_KID: 弹弓，发射！
// SPECIAL_DAD: 看我肩撞！
// SPECIAL_GRANNY: 锅盖飞盘！
// SPECIAL_GRANDPA: 太极推手！
// SUPER_KID: 影分身乱打！
// SUPER_DAD: 铁笼大锤！
// SUPER_GRANNY: 念叨风暴！
// SUPER_GRANDPA: 太极，排山倒海！
// WIN_KID: 耶！我赢啦！
// WIN_DAD: 谁也别想赢过我！
// WIN_GRANNY: 服不服？
// WIN_GRANDPA: 姜还是老的辣！
// HEAVY_KID: 嘿呀！
// HEAVY_DAD: 喝！
// HEAVY_GRANNY: 嘿！
// HEAVY_GRANDPA: 哈！
// HURT_KID: 哎哟！
// HURT_DAD: 呃啊！
// HURT_GRANNY: 哎哟喂！
// HURT_GRANDPA: 哎哟！
// DOWN_KID: 呜哇！
// DOWN_DAD: 哎呀！
// DOWN_GRANNY: 哎呀我的老腰！
// DOWN_GRANDPA: 老骨头散架喽！
// PARRY_KID: 看穿啦！
// PARRY_DAD: 早看出来了！
// PARRY_GRANNY: 想得美！
// PARRY_GRANDPA: 太急喽！
// CALL_ROUND1: 第一回合！
// CALL_ROUND2: 第二回合！
// CALL_FINAL: 决胜局！
// CALL_FIGHT: 开打！
// CALL_KO: K，O！
// CALL_TIMEUP: 时间到！

#pragma once

#include "kit_lines.h"

extern const kit_line_t fight_lines_pick[4], fight_lines_special[4], fight_lines_super[4], fight_lines_win[4], fight_lines_heavy[4], fight_lines_hurt[4], fight_lines_down[4], fight_lines_parry[4];

enum { FCALL_ROUND1, FCALL_ROUND2, FCALL_FINAL, FCALL_FIGHT, FCALL_KO, FCALL_TIMEUP, FCALL_COUNT };  // 裁判
extern const kit_line_t fight_lines_call[FCALL_COUNT];
