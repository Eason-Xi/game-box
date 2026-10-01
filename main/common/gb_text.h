// main/common/gb_text.h —— 平台公共界面文案。
//
// 约定（tools/gamebox/gen_fonts.py 与字库覆盖测试依赖它）：
//   * 所有上屏文字都写成 #define 字符串，放在某个 *_text.h 里；
//   * 直接写 UTF-8 字符，不使用 C 转义；
//   * 界面源码里不出现非 ASCII 字符串字面量。
// 改了任何 *_text.h 之后必须重新运行字库生成脚本，否则静态门禁会报缺字。
#pragma once

#define GB_TXT_BATTERY_NA     "--"
#define GB_TXT_EXIT_TITLE     "返回游戏列表？"
#define GB_TXT_EXIT_HINT_OK   "OK 确认退出"
#define GB_TXT_EXIT_HINT_BACK "▲▼ 继续玩"
#define GB_TXT_NO_MEMORY      "内存不足，请返回重试"
