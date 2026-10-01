// main/games/crane/crane_text.h —— 堆箱子全部界面文案(唯一来源)。
//
// 游戏盒的 tools/gamebox/gen_fonts.py 从所有 *_text.h 提取字符生成共用字库,
// tests/test_gb_font_coverage.py 反向校验覆盖情况。改文案后必须重新生成字库。
//
// 约定:
//   * 每个宏独占一行,形如 #define TX_xxx "文本",只用普通 UTF-8 字面量。
//   * 动态内容只有数字(分数、层数、电量),字库固定包含 ASCII 可打印区。
#pragma once

#define TX_GAME_TITLE    "堆箱子"           // 游戏列表里的名字
#define TX_GAME_TAGLINE  "松钩叠箱，越高越好" // 游戏列表里的介绍

#define TX_LOGO          "堆箱子"
#define TX_LOGO_EN       "CRANE STACK"

#define TX_MENU_START    "开始游戏"
#define TX_MENU_SOUND    "音效"
#define TX_SOUND_ON      "开"
#define TX_SOUND_OFF     "关"
#define TX_BEST          "最高分"
#define TX_BEST_HEIGHT   "最高"
#define TX_LAYER         "层"
#define TX_SEP           " · "
#define TX_TITLE_HINT    "上下键选择 · OK 确认"

#define TX_PLAY_HINT     "OK 松钩 · 上键 暂停"

#define TX_PAUSED        "暂停"
#define TX_RESUME        "继续游戏"
#define TX_RESTART       "重新开始"
#define TX_TO_TITLE      "返回标题"

#define TX_OVER_COLLAPSE "塔倒了！"
#define TX_OVER_LIVES    "机会用完了"
#define TX_SCORE         "得分"
#define TX_HEIGHT        "层数"
#define TX_PERFECTS      "完美"
#define TX_NEW_RECORD    "新纪录！"
#define TX_AGAIN         "再来一局"

#define TX_POP_PERFECT   "完美！"
#define TX_POP_COMBO     "完美 ×"
#define TX_POP_LIFE      "奖励一条命"
#define TX_POP_MISS      "没压住！"

