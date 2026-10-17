// fight_texts.h —— 库巴格斗的全部文案。字库从这里收集字符;.c 里不写中文。
#pragma once

#define TX_TITLE "库巴格斗"
#define TX_SUB "三个键的格斗，打中接大招"
#define TX_CPU "和电脑打"
#define TX_LINK "两人联机"
#define TX_HELP "出招表"
#define TX_BACK "回菜单"
#define TX_WINS "赢了 %u 场"

// 找对方、版本、断线(同库巴传炸弹)
#define TX_SEARCH "正在找对方……"
#define TX_SEARCH2 "让对方也按「两人联机」"
#define TX_SEARCH3 "两张卡靠近一点"
#define TX_HOME_HINT "● 回首页"
#define TX_NO_MEM "内存不够"
#define TX_NO_MEM2 "先退出游戏再进来"
#define TX_BAD_VER "版本不一样"
#define TX_BAD_VER2 "两张卡都去商店更新"
#define TX_LOST "对方走开了"
#define TX_LOST2 "正在等他回来……"

// 选人页
#define TX_PICK "选你的人"
#define TX_PICK_HINT "▲▼ 选  ● 确定"
#define TX_PICK_WAIT "等对方选……  ▼ 重选"
#define TX_PEER_PICKING "对方还在选"
#define TX_PEER_PICKED "对方选了%s"
#define TX_HOME "回首页"

// 角色名(下标 = fight_char_t)、小招、大招(特写里一行放不下的用换行分开)
#define TX_KID "库巴"
#define TX_DAD "库巴爸爸"
#define TX_GRANNY "库巴奶奶"
#define TX_GRANDPA "库巴爷爷"
#define TX_SP_KID "弹弓！"
#define TX_SP_DAD "肩撞！"
#define TX_SP_GRANNY "锅盖飞盘！"
#define TX_SP_GRANDPA "太极推手！"
#define TX_SU_KID "影分身\n乱打"
#define TX_SU_DAD "铁笼\n大锤"
#define TX_SU_GRANNY "念叨\n风暴"
#define TX_SU_GRANDPA "太极·\n排山倒海"

// 出招表(第一次进游戏放一遍;首页「出招表」随时看)
#define TX_HELP_JAB "● 拳"
#define TX_HELP_JAB_WHAT "出手快"
#define TX_HELP_HEAVY "▲ 重击"
#define TX_HELP_HEAVY_WHAT "砸开防御"
#define TX_HELP_GUARD "▼ 防"
#define TX_HELP_GUARD_WHAT "挡拳，看准弹开"
#define TX_HELP_SPECIAL "▼ ▲"
#define TX_HELP_SPECIAL_WHAT "小招，不花气"
#define TX_HELP_SUPER "● 打中后马上"
#define TX_HELP_SUPER2 "▼ ● 大招！"
#define TX_HELP_TAP "花一格气。嗒嗒嗒，快快点三下"
#define TX_HELP_MASH "晕了就狂按挣脱"
#define TX_HELP_OK "● 知道了"

// 对打页
#define TX_ROUND "第 %u 回合"
#define TX_STAGE "第 %u 关"
#define TX_SCORE "%u : %u"
#define TX_FIGHT "开打！"
#define TX_KO "KO！"
#define TX_TIMEUP "时间到！"
#define TX_ROUND_ME "你拿下这一回合"
#define TX_ROUND_PEER "对方拿下这一回合"
#define TX_ROUND_DRAW "平手，重打"
#define TX_KEYS "● 拳 ▲ 重击 ▼ 防"
#define TX_KEYS_SUPER "● 打中后\n▼ ● 大招"   // 一行放不下(▲▼● 和汉字一样宽)
#define TX_MASH "狂按挣脱！"
#define TX_BREAK "破防！"
#define TX_COUNTER "接住！"
#define TX_PARRY "弹开！"

// 结束页
#define TX_WIN "你赢了！"
#define TX_LOSE "对方赢了"
#define TX_AGAIN "● 再来\n▼ 回首页"
#define TX_WAIT_AGAIN "等对方按 ●"
#define TX_NEXT "● 下一关\n▼ 回首页"
#define TX_RETRY "● 再打一次\n▼ 回首页"
#define TX_CLEAR "通关！"
#define TX_CLEAR_HINT "● 再闯一次\n▼ 回首页"

// 开机菜单卡片
#define TX_CARD_SUB "打中接大招"
