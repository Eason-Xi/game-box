<p align="right">
  <strong>简体中文</strong> · <a href="game-box.md">English</a>
</p>

# 游戏盒：平台结构与游戏接入指南

本固件是一个小游戏合集。开机进入游戏列表，选中后进入游戏，玩完返回列表。
本文说明平台结构，以及如何把一个已有游戏（例如贪吃蛇、Flappy Bird、堆箱子）
接入进来。

## 游戏列表

开机进入游戏列表，一屏显示 4 个游戏，超过 4 个时列表跟着选中项滚动，右侧出现
滚动条。每一行包括：40x40 图标、游戏名（最多 4 个汉字）、右上角「★最高分」、
第二行一句话介绍（约 9 个汉字，超出显示省略号）。选中行用游戏的主题色填充并加
黄色边框。

## 按键约定

| 位置 | 上 / 下 单击 | 确定 单击 | 确定 长按 |
| --- | --- | --- | --- |
| 游戏列表 | 上下移动选中项（首尾循环） | 开始游戏 | — |
| 游戏中 | 游戏自定义 | 游戏自定义 | 弹出「返回游戏列表？」确认框 |
| 确认框 | 关闭确认框，继续玩 | 退出到游戏列表 | — |

「确定长按」由平台统一拦截，游戏收不到。本板长按判定只有 500ms，所以长按只
弹出确认框，不直接退出，避免误触丢掉一局。弹框时平台会调用游戏的 `pause(true)`。

注意：按键组件在长按之前一定先发出 PRESS（按下瞬间）。用 PRESS 做操作的游戏
（堆箱子的松钩、飞吧小鸟的扇翅、连点挑战的计数）在长按退出时会先执行一次这个操作；用 CLICK
做操作的游戏（躲避方块的开始）不受影响。

无操作 30 秒调暗背光，90 秒关闭背光；熄屏后的第一次按键只负责唤醒，不会触发
操作。游戏进行中调用 `gb_app_set_playing(true)` 可以阻止调暗。

## 目录结构

```text
main/
├── gb_main.c              固件入口：初始化显示、音频、电量计
├── gb_app.c               按键分发、页面切换、退出确认框、空闲调暗
├── gb_nav.c               导航状态机（纯逻辑，有主机测试）
├── gb_game.h              每个游戏实现的接口 gb_game_t
├── gb_registry.c          游戏清单（列表按此顺序显示）
├── launcher/              游戏列表（滚动计算在 gb_launcher_layout.c，有主机测试）
├── common/                所有游戏共用的功能
│   ├── gb_sfx.c           音效播放器（开机即运行；方波音效表或游戏自带的合成器）
│   ├── gb_tone.c          方波 PCM 合成与流式播放（纯逻辑）
│   ├── gb_score.c         最高分与自定义存档（NVS 命名空间 gamebox，键名 b_<id> / s_<id>）
│   ├── gb_canvas.c        画布内存：从系统堆申请、退出时释放
│   ├── gb_i4.c            16 色画布上的像素 / 矩形绘制（纯逻辑）
│   ├── gb_idle.c          空闲调暗状态机（纯逻辑）
│   ├── gb_ui.c            配色、共用字库、电量指示
│   └── gb_text.h          平台文案
└── games/
    ├── crane/             堆箱子：从独立固件 crane-stack 移植，自带合成器与存档
    ├── flappy/            飞吧小鸟：从独立固件 FlappyBird 移植，自带合成器、存档与精灵图
    ├── dodge/             躲避方块：画布类游戏样板
    └── tap/               连点挑战：最简单的控件类游戏样板
```

LVGL 内存池为 64KB（`sdkconfig.defaults`）：同一时间只有一个游戏的界面，最重的
堆箱子一屏约 120 个对象，再加上叠在游戏画面上的退出确认框。池子耗尽时 LVGL 会
停在断言里，整机卡死，所以必须留足余量。

`main/main.c`、`main/demo_*.c` 和 `main/ui_pixel*.c` 是上游的硬件测试 demo，
留作 BSP 用法参考，并继续接受 host tests，但不编进本固件。

## 接入一个游戏

1. **建目录。** 在 `main/games/<id>/` 放代码。`id` 只能用小写字母、数字、
   下划线，最长 13 个字符（会用作最高分的 NVS 键名）。
2. **拆分规则与画面。** 规则（状态、碰撞、计分）写成不依赖 ESP-IDF / LVGL
   的 `<id>_model.c`，并加入 `tests/` 下的主机测试；页面代码只负责把模型画出来、
   把按键翻译成模型操作。
3. **实现 `gb_game_t`**，导出为 `const gb_game_t GB_GAME_<ID>`。各回调由谁调用、
   是否持有 LVGL 锁，见 `main/gb_game.h` 顶部注释。要点：
   - `enter()` 建立并 `lv_screen_load()` 自己的屏幕，用 `lv_timer` 做帧循环；
   - `exit()` 删除定时器和屏幕，释放画布，清空所有对象指针；
   - `key()` 必须很快返回，不能阻塞；
   - 只有自己起了后台任务时，才需要实现 `start()` / `stop()`；
   - `title` 最多 4 个汉字，`tagline` 约 9 个汉字；`icon()` 在 40x40
     （`GB_ICON_SIZE`）的容器里用 LVGL 控件画一个小图标，不提供时显示游戏名首字。
4. **画布内存在 enter 里申请。** 把 `LV_DRAW_BUF_DEFINE_STATIC` 改成
   `gb_canvas_create()` / `gb_canvas_destroy()`。静态缓冲开机就一直占用内存，
   每个游戏一块，加起来会把没有 PSRAM 的 C3 吃光；`lv_draw_buf_create()` 又会
   占用 LVGL 的内存池（界面对象也在这里分配）。能用 `LV_COLOR_FORMAT_I4`（16 色）就别用
   RGB565：200x240 的 I4 画布 24KB，RGB565 要 94KB。
5. **改用共用功能。**
   - 音效：删掉游戏自带的播放任务。简单方波音用 `gb_note_t` 数组 + `GB_TUNE()`
     定义音效表，调用 `gb_sfx_play()`；按键这类短促提示用 `gb_sfx_play_feedback()`。
     游戏有自己的合成器（扫频、三角波、噪声）时，把它包成 `gb_synth_t`，调用
     `gb_sfx_play_synth()`，采样率固定 16kHz；
   - 最高分：删掉游戏自带的 NVS 代码，改用 `gb_score_best()` /
     `gb_score_submit()`（分数越大越好）；
   - 其他要保存的数据（设置、额外纪录）：用 `gb_save_load()` / `gb_save_store()`，
     每个游戏一块，不超过 32 字节，结构体里带版本号。不要自己打开 NVS，更不要
     擦除 NVS 分区；
   - 写 flash 是异步的：平台在两段音效之间才真正写入，避免写 flash 时音效爆音；
   - 电量：需要时调用 `gb_ui_battery_create()`，或者用 `bsp_battery_soc()` 画自己的样式
     （在 lv_timer 里读，几秒一次即可）。
6. **文案与字库。** 所有上屏文字写进 `main/games/<id>/<id>_text.h`，用 `#define`
   定义、直接写 UTF-8。然后重新生成共用字库：

   ```bash
   python3 tools/gamebox/gen_fonts.py --font /path/to/SourceHanSansSC-Regular.otf
   ```

   游戏自带的字库删掉，改用 `gb_font_14/16/20/24/28/40`（全部文案）与 `gb_num_48`
   （大号数字）。需要别的字号时，在 `tools/gamebox/gen_fonts.py` 的 `FONT_SPECS`
   里加一项。如果字库漏了字，或者源码里残留中文字符串字面量，
   `tests/test_gb_font_coverage.py` 都会报错。
7. **注册。** 把源文件加进 `main/CMakeLists.txt` 的 `GAME_SRCS`，在
   `main/gb_registry.c` 的 `GAMES[]` 里加一行。如果有新的纯逻辑测试，加进
   `tools/validate.sh`。
8. **主机预览。** 把游戏加进 `tools/gamebox_preview/harness.c` 的场景，运行：

   ```bash
   ./tools/render_gamebox_preview.sh
   ```

   它用与固件同版本的 LVGL 在电脑上渲染真实代码，截图写到
   `build/gamebox_preview/`。同时把每个游戏反复进出 8 次，检查 LVGL 内存池里
   占用的块数不变（检查 exit() 有没有漏删对象、样式或定时器），并检查堆箱子
   运行时内存池的余量。需要先跑过一次固件构建以下载 LVGL 源码。
9. **验证。** 运行 `./tools/validate.sh`，然后上真机检查：列表里的这一行、进入游戏、
   长按确定弹出确认框、退出后再进入（反复几次，串口日志会打印每次进出时的
   空闲堆）、纪录断电后仍在、帧率与按键手感。

### 移植贪吃蛇时的对照

| 原来（`AI-PASSPORT` 仓库） | 改成 |
| --- | --- |
| `demo_snake_enter/exit/key/start/stop` | `gb_game_t GB_GAME_SNAKE` 的回调 |
| `LV_DRAW_BUF_DEFINE_STATIC(board_buf, …)` | `gb_canvas_create()` / `gb_canvas_destroy()` |
| `snake_sfx.c`、`snake_sfx_render.c` | 删除；`snake_sfx_tunes.h` 的音表改成 `gb_note_t` 数组 |
| `snake_store.c` | 删除；改用 `gb_score_*("snake", …)` |
| `snake_font_16/28` | 删除；`snake_text.h` 移到 `games/snake/`，改用共用字库 |
| 页面里自己的空闲调暗 | 删除；改为调用 `gb_app_set_playing()` |

### 移植堆箱子时的对照

| 原来（`crane-stack` 工作区） | 改成 |
| --- | --- |
| `crane_model`、`crane_flow`、`crane_sfx`、`crane_theme.h` 及其测试 | 原样复制到 `games/crane/` 与 `tests/`，测试接入 `validate.sh` |
| `crane_ui.c`（开机建一次、永不删除） | `crane_game.c`：`enter()` 重建界面并重置所有「已绘制」缓存，`exit()` 删除定时器、屏幕和共享样式 |
| `crane_service.c`（音效 + 存档 + 电量的后台任务） | 删除；音效经 `gb_synth_t` 交给 `gb_sfx`，电量直接读 `bsp_battery_soc()` |
| `crane_store.c`（自己初始化并可能擦除 NVS） | 删除；最高分进 `gb_score`，音效开关与最高层数存进 `gb_save_*("crane", …)` |
| `crane_font_16/24/40` | 删除；改用共用字库的同字号 |
| `main.c` | 删除；初始化由平台完成 |
| 标题页「按键不可用」提示 | 删除：能进入游戏说明按键可用 |
| 独立运行时的暂停菜单 | 保留；平台退出确认框打开时另外冻结模拟 |

### 移植飞吧小鸟时的对照

做法与堆箱子相同，只列出不同之处：

| 原来（`FlappyBird` 仓库） | 改成 |
| --- | --- |
| `flappy_model`、`flappy_flow`、`flappy_sfx`、`flappy_theme.h` 及其测试 | 原样复制到 `games/flappy/` 与 `tests/`，测试接入 `validate.sh` |
| `assets/images/flappy_sprites.c` 与 `tools/gen_flappy_assets.py` | 复制到 `assets/images/` 与 `tools/gamebox/`；在新位置重新生成，结果逐字节一致 |
| 三个难度各自的最高分 | 仍存在本游戏的自定义存档里，标题页按难度显示；每局得分另外提交给 `gb_score`，游戏列表显示各难度中的最高分 |
| `flappy_font_16/24/40` | 删除；改用共用字库的同字号 |
| 列表图标 | 直接用游戏里 40x40 的小鸟精灵 |

## 当前状态

- 第一阶段：平台框架、共用功能、两个样板游戏，已完成并通过构建与主机测试，
  尚未上真机验证。
- 堆箱子、飞吧小鸟已移植：构建、主机测试与主机渲染预览通过，尚未上真机验证。
- 待移植：贪吃蛇。
