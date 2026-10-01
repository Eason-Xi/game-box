// main/games/flappy/flappy_text.h —— 飞吧小鸟全部界面文案(唯一来源)。
//
// 游戏盒的 tools/gamebox/gen_fonts.py 从所有 *_text.h 提取字符生成共用字库,
// tests/test_gb_font_coverage.py 反向校验覆盖情况。改文案后必须重新生成字库。
//
// 约定:
//   * 每个宏独占一行,形如 #define FT_xxx "文本",只用普通 UTF-8 字面量。
//   * 动态内容只有数字(分数、电量),字库固定包含 ASCII 可打印区。
#pragma once

#define FT_GAME_TITLE    "飞吧小鸟"         // 游戏列表里的名字
#define FT_GAME_TAGLINE  "点一下，飞一下"   // 游戏列表里的介绍

#define FT_LOGO          "飞吧小鸟"
#define FT_LOGO_EN       "FLAPPY BIRD"

#define FT_MENU_START    "开始游戏"
#define FT_MENU_DIFF     "难度"
#define FT_MENU_SOUND    "音效"
#define FT_DIFF_EASY     "简单"
#define FT_DIFF_NORMAL   "普通"
#define FT_DIFF_HARD     "困难"
#define FT_SOUND_ON      "开"
#define FT_SOUND_OFF     "关"
#define FT_BEST          "最高分"
#define FT_TITLE_HINT    "上下键选择 · OK 确认"

#define FT_READY         "准备起飞"
#define FT_READY_HINT    "OK 或 上键 扇动翅膀"
#define FT_READY_BACK    "下键 返回标题"
#define FT_PLAY_HINT     "下键 暂停"

#define FT_PAUSED        "暂停"
#define FT_RESUME        "继续游戏"
#define FT_RESTART       "重新开始"
#define FT_TO_TITLE      "返回标题"

#define FT_OVER          "游戏结束"
#define FT_SCORE         "得分"
#define FT_NEW_RECORD    "新纪录！"
#define FT_AGAIN         "再来一局"
#define FT_MEDAL_NONE    "加油"
#define FT_MEDAL_BRONZE  "铜牌"
#define FT_MEDAL_SILVER  "银牌"
#define FT_MEDAL_GOLD    "金牌"
#define FT_MEDAL_PLATINUM "白金"
