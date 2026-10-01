<p align="right">
  <strong>简体中文</strong> · <a href="README.md">English</a>
</p>

# 资源目录（Assets）

本目录集中存放可复用的资源（字库、图片、音乐等），按资源类型分子目录管理。每个资源放在其类型对应的子目录，并记录放置路径、命名方式、集成方式与来源/许可。二进制资源（字体、图片、音频）不属于纯 markdown 文档，请勿与文档混放。涉及版权/授权的资源需注明来源与许可。

## 字库（fonts）

可复用的字库文件与生成的字库源码放在 `fonts/`。

- 命名要能反映字族、字重、字级与格式。
- 记录来源、许可、字符范围、转换命令与目标放置路径。
- 添加字库前评估 Flash 与内部 RAM 影响；ESP32-C3 无 PSRAM。
- 不提交许可不允许分发的字库。

| 文件 | 字号与字符集 | 用途与来源 |
| --- | --- | --- |
| `fonts/gb_font_14.c`、`gb_font_16.c`、`gb_font_20.c`、`gb_font_24.c`、`gb_font_28.c`、`gb_font_40.c` | 14/16/20/24/28/40 px，4 bpp；所有 `main/**/*_text.h` 文案 + 可打印 ASCII | 游戏盒全部界面共用。由 `tools/gamebox/gen_fonts.py` 从思源黑体 SC Regular（SIL Open Font License 1.1）生成，`main/CMakeLists.txt` 用 `target_sources` 引入。 |
| `fonts/gb_num_48.c` | 48 px，4 bpp；数字与 ` .:/-+%` | 游戏中的大号分数与计时，来源同上。 |

改动任何 `*_text.h` 后重新运行生成脚本；`tests/test_gb_font_coverage.py` 会检查缺字。

## 图片（images）

可复用的源图与生成的显示资产放在 `images/`。

| 文件 | 尺寸与格式 | 用途与来源 |
| --- | --- | --- |
| [`images/gamebox-preview.png`](images/gamebox-preview.png) | 1296 × 352，PNG RGB | 根目录 README 的截图：游戏列表与四个游戏各一帧。由 `tools/render_gamebox_preview.sh` 渲染真实代码得到，再用 Pillow 拼接；界面改动后应重新生成。 |
| [`images/flappy_sprites.c`](images/flappy_sprites.c) | 25 张 LVGL 9 常量图像，约 140 KB Flash：18 帧小鸟（40 × 40 RGB565A8，6 个俯仰角 × 3 个翅膀姿态）、云、远山、滚动草皮带、沙土、管道身体贴片与管口（烘焙了天空底色的 RGB565） | 飞吧小鸟的场景美术，小鸟同时用作游戏列表图标。由 `tools/gamebox/gen_flappy_assets.py` 用 Pillow 程序化绘制（4 倍超采样），无第三方素材。重新生成：`python3 tools/gamebox/gen_flappy_assets.py [--preview sheet.png]`。 |
| [`images/home.jpg`](images/home.jpg) | 3840 × 2160，JPEG | 嵌入中英文项目 README 的产品主图，突出 AI Passport 产品形象与开放、人人可创作的理念。 |
| [`images/readme-hardware-specs.png`](images/readme-hardware-specs.png) | 2172 × 724，PNG RGBA | 保留为可选技术参考图，不再用于首页主视觉。于 2026-09-17 使用内置图像生成工具为本仓库生成；已根据文档中的硬件能力契约核对图中的六项标签与参数。 |
| [`images/logo-wordmark.png`](images/logo-wordmark.png) | 1648 × 336，PNG RGBA | 从仓库原始 `images/logo.png` 中精确裁切并去除背景的黑色字标；用于中英文项目 README 的浅色主题。 |
| [`images/logo-wordmark-dark.png`](images/logo-wordmark-dark.png) | 1648 × 336，PNG RGBA | 提取字标的白色版本；README 使用 `<picture>` 在 GitHub 深色主题下显示。 |

- 使用描述性命名，并记录尺寸、像素格式、转换步骤与目标路径。
- 优先采用适合 240 × 320 RGB565 显示的格式，并纳入 Flash 与内部 RAM 考量。
- 许可允许时保留可编辑源文件，并记录来源与许可。
- 图片中不得包含设备二维码秘密、凭证或个人数据。

## 音乐与音效（music）

可复用的音乐与音效源码放在 `music/`。

- 记录来源、许可、采样率、位深、声道、转换命令与目标路径。
- 与当前 BSP 音频路径匹配时优先采用 16 kHz、16 位单声道 PCM。
- 嵌入音频前评估 Flash 与内部 RAM 成本；长录音应流式或分块。
- 无再分发许可不提交媒体文件。
