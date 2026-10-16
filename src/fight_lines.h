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

#pragma once

#include "kit_lines.h"

extern const kit_line_t FL_PICK[4], FL_SPECIAL[4], FL_SUPER[4], FL_WIN[4];
