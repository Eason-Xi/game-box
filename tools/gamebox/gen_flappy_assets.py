#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""生成飞吧小鸟的位图资源(LVGL 9.5 常量图像)。

用法(仓库根目录):

    python3 tools/gamebox/gen_flappy_assets.py            # 生成 assets/images/flappy_sprites.c
    python3 tools/gamebox/gen_flappy_assets.py --preview out.png   # 另存一张拼图预览

全部图形由本脚本用 Pillow 以 4 倍超采样程序化绘制后缩小抗锯齿,没有外部素材,
不涉及第三方版权。生成的 C 文件已提交,正常构建不需要 Pillow。

格式约定:
  * 不透明图(云、远山、地面、管道)用 RGB565,背景天空色直接烘焙进像素,
    绘制时是纯拷贝,比带 alpha 混合更省 CPU。
  * 小鸟需要叠在管道上,用 RGB565A8(先 w*h*2 字节颜色,再 w*h 字节 alpha)。
  * RGB565 以小端存储(LVGL 原生格式),字节交换由 BSP 在刷屏时完成。
"""

from __future__ import annotations

import argparse
import math
from pathlib import Path

from PIL import Image, ImageDraw

REPO_ROOT = Path(__file__).resolve().parents[2]
OUT_C = REPO_ROOT / "assets" / "images" / "flappy_sprites.c"
SS = 4  # 超采样倍数

# 与 main/flappy_theme.h 的 FT_COLOR_SKY 保持一致
SKY = (120, 200, 230)

BIRD_SIZE = 40
BIRD_ANGLES = (-25, 0, 25, 50, 70, 90)
WING_FRAMES = 3


def hex_rgb(value: int) -> tuple[int, int, int]:
    return ((value >> 16) & 0xFF, (value >> 8) & 0xFF, value & 0xFF)


def ss_canvas(w: int, h: int, bg=(0, 0, 0, 0)) -> Image.Image:
    return Image.new("RGBA", (w * SS, h * SS), bg)


def down(img: Image.Image, w: int, h: int) -> Image.Image:
    return img.resize((w, h), Image.LANCZOS)


def s(v: float) -> int:
    return int(round(v * SS))


def box(x0: float, y0: float, x1: float, y1: float) -> list[int]:
    return [s(x0), s(y0), s(x1), s(y1)]


# ---------------------------------------------------------------- 小鸟
def draw_bird(wing: int, angle: int) -> Image.Image:
    """朝右的小鸟,34x24 左右,画在 40x40 画布中心后按角度旋转(正角低头)。"""
    img = ss_canvas(BIRD_SIZE, BIRD_SIZE)
    d = ImageDraw.Draw(img)
    ink = hex_rgb(0x3A2A1A)
    cx, cy = 20.0, 20.0
    ow = 1.4  # 描边宽度(像素)
    # 身体
    d.ellipse(box(cx - 15, cy - 11, cx + 13, cy + 11), fill=ink)
    d.ellipse(box(cx - 15 + ow, cy - 11 + ow, cx + 13 - ow, cy + 11 - ow), fill=hex_rgb(0xFFCC33))
    # 肚皮
    d.ellipse(box(cx - 8, cy + 1, cx + 10, cy + 10 - ow), fill=hex_rgb(0xFFF0B0))
    # 背部高光
    d.ellipse(box(cx - 9, cy - 9, cx + 1, cy - 4), fill=hex_rgb(0xFFE07A))
    # 眼睛
    d.ellipse(box(cx + 1, cy - 10, cx + 12, cy + 1), fill=ink)
    d.ellipse(box(cx + 2.3, cy - 8.7, cx + 10.7, cy - 0.3), fill=(255, 255, 255))
    d.ellipse(box(cx + 6, cy - 6.5, cx + 9.5, cy - 2.5), fill=hex_rgb(0x222222))
    # 腮红
    d.ellipse(box(cx + 1, cy + 1, cx + 6, cy + 4), fill=hex_rgb(0xFF9A7A))
    # 嘴(上下两片)
    d.rounded_rectangle(box(cx + 7, cy - 1, cx + 19, cy + 4), radius=s(2.5), fill=ink)
    d.rounded_rectangle(box(cx + 8, cy, cx + 18, cy + 3), radius=s(1.5), fill=hex_rgb(0xFF7F2A))
    d.rounded_rectangle(box(cx + 7, cy + 3, cx + 16, cy + 8), radius=s(2.5), fill=ink)
    d.rounded_rectangle(box(cx + 8, cy + 4, cx + 15, cy + 7), radius=s(1.5), fill=hex_rgb(0xE8602A))
    # 翅膀:三帧(上扬 / 水平 / 下压)
    wy = (-5.0, 0.0, 4.0)[wing]
    wh = (6.0, 5.0, 6.0)[wing]
    d.ellipse(box(cx - 16, cy + wy - wh / 2 - 1, cx - 3, cy + wy + wh / 2 + 1), fill=ink)
    d.ellipse(box(cx - 15 + ow * 0.7, cy + wy - wh / 2 - 1 + ow * 0.7,
                  cx - 3 - ow * 0.7, cy + wy + wh / 2 + 1 - ow * 0.7), fill=hex_rgb(0xFFF8E0))
    img = img.rotate(-angle, resample=Image.BICUBIC, center=(s(cx), s(cy)))
    return down(img, BIRD_SIZE, BIRD_SIZE)


# ---------------------------------------------------------------- 云
def draw_cloud(w: int, h: int) -> Image.Image:
    img = ss_canvas(w, h, SKY + (255,))
    d = ImageDraw.Draw(img)
    shade = hex_rgb(0xDDF1F8)
    white = (255, 255, 255)
    blobs = [
        (0.08, 0.45, 0.42, 1.0),
        (0.22, 0.12, 0.62, 0.95),
        (0.48, 0.25, 0.84, 1.0),
        (0.68, 0.45, 0.96, 1.0),
    ]
    for x0, y0, x1, y1 in blobs:
        d.ellipse(box(x0 * w, y0 * h + 1.5, x1 * w, y1 * h), fill=shade)
    for x0, y0, x1, y1 in blobs:
        d.ellipse(box(x0 * w, y0 * h, x1 * w, y1 * h - 2.5), fill=white)
    return down(img, w, h)


# ---------------------------------------------------------------- 远山与树丛
def draw_hills(w: int, h: int) -> Image.Image:
    img = ss_canvas(w, h, SKY + (255,))
    d = ImageDraw.Draw(img)
    far = hex_rgb(0xA8DFC0)
    near = hex_rgb(0x82CC9A)
    bush = hex_rgb(0x69BA84)
    for cx, r in ((20, 30), (90, 38), (170, 34), (235, 30)):
        d.ellipse(box(cx - r * 1.6, h - r * 0.9, cx + r * 1.6, h + r * 1.4), fill=far)
    for cx, r in ((-5, 18), (55, 22), (125, 20), (195, 24), (250, 18)):
        d.ellipse(box(cx - r * 1.5, h - r * 0.75, cx + r * 1.5, h + r * 1.2), fill=near)
    for i in range(0, w + 16, 16):
        r = 7 + (i * 7 % 5)
        d.ellipse(box(i - r, h - r * 0.9, i + r, h + r), fill=bush)
    return down(img, w, h)


# ---------------------------------------------------------------- 地面
GROUND_PERIOD = 24
GROUND_BAND_H = 22


def draw_ground_band(w: int) -> Image.Image:
    """草皮 + 斜纹,高 22px;斜纹周期 24px,UI 按周期取模平移即可无缝滚动。"""
    h = GROUND_BAND_H
    img = ss_canvas(w, h, hex_rgb(0xE8D08E) + (255,))
    d = ImageDraw.Draw(img)
    d.rectangle(box(0, 0, w, 2), fill=hex_rgb(0x3E7B27))
    d.rectangle(box(0, 2, w, 5), fill=hex_rgb(0xA5EB6E))
    stripe_top, stripe_bottom = 5, 17
    d.rectangle(box(0, stripe_top, w, stripe_bottom), fill=hex_rgb(0x74CF4C))
    for x in range(-GROUND_PERIOD, w + GROUND_PERIOD, GROUND_PERIOD):
        d.polygon([
            (s(x), s(stripe_bottom)), (s(x + 12), s(stripe_bottom)),
            (s(x + 24), s(stripe_top)), (s(x + 12), s(stripe_top)),
        ], fill=hex_rgb(0x94E060))
    d.rectangle(box(0, stripe_bottom, w, stripe_bottom + 2), fill=hex_rgb(0x5A9E36))
    d.rectangle(box(0, stripe_bottom + 2, w, h), fill=hex_rgb(0xD9BC72))
    return down(img, w, h)


def draw_dirt(w: int, h: int) -> Image.Image:
    """草皮下方的静态沙土,带少量斑点。"""
    img = ss_canvas(w, h, hex_rgb(0xE8D08E) + (255,))
    d = ImageDraw.Draw(img)
    for x in range(0, w + GROUND_PERIOD, GROUND_PERIOD):
        for dx, dy, r in ((4, 5, 1.6), (15, 13, 1.2), (9, 20, 1.4), (20, 8, 1.0)):
            d.ellipse(box(x + dx - r, dy - r, x + dx + r, dy + r), fill=hex_rgb(0xD6B970))
    return down(img, w, h)


# ---------------------------------------------------------------- 管道
PIPE_TONES = [0x2F5E1E, 0x2F5E1E, 0x5BB33B, 0x9BE36A, 0xB8F08A, 0x9BE36A,
              0x6CC447, 0x6CC447, 0x62BC40, 0x5BB33B, 0x55AB36, 0x4E9E32,
              0x46922C, 0x2F5E1E, 0x2F5E1E]


def shade_row(w: int) -> list[tuple[int, int, int]]:
    """按水平位置在色阶中插值,得到一行像素(左亮右暗的圆柱感)。"""
    row = []
    n = len(PIPE_TONES)
    for x in range(w):
        if x < 2 or x >= w - 2:
            row.append(hex_rgb(0x2F5E1E))
            continue
        t = (x - 2) / max(1, w - 5) * (n - 5) + 2
        row.append(hex_rgb(PIPE_TONES[min(n - 3, int(t))]))
    return row


def draw_pipe_body(w: int, h: int) -> Image.Image:
    img = Image.new("RGBA", (w, h))
    row = shade_row(w)
    for y in range(h):
        for x in range(w):
            img.putpixel((x, y), row[x] + (255,))
    return img


def draw_pipe_cap(w: int, h: int) -> Image.Image:
    img = Image.new("RGBA", (w, h))
    row = shade_row(w)
    dark = hex_rgb(0x2F5E1E)
    for y in range(h):
        for x in range(w):
            c = dark if y < 2 or y >= h - 2 else row[x]
            img.putpixel((x, y), c + (255,))
    return img


# ---------------------------------------------------------------- 输出
def rgb565(r: int, g: int, b: int) -> int:
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)


def image_bytes(img: Image.Image, with_alpha: bool) -> bytes:
    px = img.convert("RGBA").load()
    w, h = img.size
    color = bytearray()
    alpha = bytearray()
    for y in range(h):
        for x in range(w):
            r, g, b, a = px[x, y]
            if with_alpha and a == 0:
                r = g = b = 0
            v = rgb565(r, g, b)
            color += bytes((v & 0xFF, v >> 8))
            alpha.append(a)
    return bytes(color + (alpha if with_alpha else b""))


def c_array(name: str, data: bytes) -> str:
    lines = [f"static const uint8_t {name}_map[{len(data)}] = {{"]
    for i in range(0, len(data), 24):
        lines.append("    " + ", ".join(f"0x{b:02X}" for b in data[i:i + 24]) + ",")
    lines.append("};")
    return "\n".join(lines)


def c_dsc(name: str, img: Image.Image, with_alpha: bool) -> str:
    w, h = img.size
    cf = "LV_COLOR_FORMAT_RGB565A8" if with_alpha else "LV_COLOR_FORMAT_RGB565"
    return (
        f"const lv_image_dsc_t {name} = {{\n"
        f"    .header = {{ .magic = LV_IMAGE_HEADER_MAGIC, .cf = {cf},\n"
        f"                .w = {w}, .h = {h}, .stride = {w * 2} }},\n"
        f"    .data_size = sizeof({name}_map),\n"
        f"    .data = {name}_map,\n"
        f"}};"
    )


def build_images() -> list[tuple[str, Image.Image, bool]]:
    images: list[tuple[str, Image.Image, bool]] = []
    for ai, angle in enumerate(BIRD_ANGLES):
        for wing in range(WING_FRAMES):
            images.append((f"flappy_img_bird_{ai}_{wing}", draw_bird(wing, angle), True))
    images.append(("flappy_img_cloud_big", draw_cloud(64, 26), False))
    images.append(("flappy_img_cloud_small", draw_cloud(44, 20), False))
    images.append(("flappy_img_hills", draw_hills(240, 40), False))
    images.append(("flappy_img_ground_band", draw_ground_band(240 + GROUND_PERIOD), False))
    images.append(("flappy_img_dirt", draw_dirt(240, 48 - GROUND_BAND_H), False))
    images.append(("flappy_img_pipe_body", draw_pipe_body(46, 32), False))
    images.append(("flappy_img_pipe_cap", draw_pipe_cap(52, 18), False))
    return images


def write_c(images: list[tuple[str, Image.Image, bool]]) -> None:
    parts = [
        "// assets/images/flappy_sprites.c —— 由 tools/gamebox/gen_flappy_assets.py 生成,请勿手改。",
        "// 全部图形由脚本程序化绘制,无第三方素材。",
        '#include "lvgl.h"',
        "",
    ]
    for name, img, alpha in images:
        parts.append(c_array(name, image_bytes(img, alpha)))
        parts.append(c_dsc(name, img, alpha))
        parts.append("")
    bird_names = ",\n".join(
        "    { " + ", ".join(f"&flappy_img_bird_{ai}_{w}" for w in range(WING_FRAMES)) + " }"
        for ai in range(len(BIRD_ANGLES))
    )
    parts.append(
        f"const lv_image_dsc_t *const flappy_img_bird[{len(BIRD_ANGLES)}][{WING_FRAMES}] = {{\n"
        f"{bird_names}\n}};"
    )
    parts.append(
        "const int16_t flappy_bird_angles[" + str(len(BIRD_ANGLES)) + "] = { "
        + ", ".join(str(a) for a in BIRD_ANGLES) + " };"
    )
    parts.append("")
    OUT_C.parent.mkdir(parents=True, exist_ok=True)
    OUT_C.write_text("\n".join(parts), encoding="utf-8")


def write_preview(images: list[tuple[str, Image.Image, bool]], path: Path) -> None:
    scale = 3
    sheet = Image.new("RGBA", (300 * scale, 420 * scale), SKY + (255,))
    x = y = 4
    row_h = 0
    for _, img, _ in images:
        w, h = img.size
        if x + w > 296:
            x = 4
            y += row_h + 4
            row_h = 0
        big = img.resize((w * scale, h * scale), Image.NEAREST)
        sheet.alpha_composite(big, (x * scale, y * scale))
        x += w + 4
        row_h = max(row_h, h)
    sheet.save(path)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--preview", type=Path, help="另存资源拼图 PNG")
    args = parser.parse_args()
    images = build_images()
    write_c(images)
    if args.preview:
        write_preview(images, args.preview)
    total = sum(len(image_bytes(img, alpha)) for _, img, alpha in images)
    print(f"wrote {OUT_C.relative_to(REPO_ROOT)}: {len(images)} images, {total} bytes of pixel data")


if __name__ == "__main__":
    main()
