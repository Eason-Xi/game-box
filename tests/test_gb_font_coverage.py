#!/usr/bin/env python3
"""证明游戏盒共用字库覆盖全部界面文案（无需 Pillow / LVGL）。

检查三件事：
1. 每个生成的 assets/fonts/gb_*.c 包含 gen_fonts.required_codepoints() 要求的全部码点；
2. 反例：刻意未收录的字（U+9F98「龘」）不在字库里，防止检查无条件通过；
3. main/ 下的源码中没有散落的非 ASCII 字符串字面量 —— 上屏文字必须写在某个
   *_text.h 里，否则字库生成器看不到它们。ESP_LOGx(...) 里的中文只进串口日志、
   不经过 LVGL，不在检查范围内。参考 demo（demo_*.c、main.c、ui_pixel*.c）不编进
   本固件，也不检查。
"""

from __future__ import annotations

import re
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools" / "gamebox"))

import gen_fonts  # noqa: E402

LOG_CALL_RE = re.compile(r"ESP_LOG[EWIDV]\s*\(.*?\);", re.S)
REFERENCE_DEMO = re.compile(r"^(main|demo_.*|ui_pixel.*)\.c$")


def ui_sources() -> list[Path]:
    main = ROOT / "main"
    return [p for p in sorted(main.rglob("*.c")) if not REFERENCE_DEMO.match(p.name)]


def string_literals(source: str) -> list[str]:
    """提取 C 源码中的字符串字面量，跳过注释与字符常量。"""
    literals: list[str] = []
    i, n = 0, len(source)
    while i < n:
        ch = source[i]
        if source.startswith("//", i):
            i = source.find("\n", i)
            i = n if i < 0 else i
        elif source.startswith("/*", i):
            end = source.find("*/", i + 2)
            i = n if end < 0 else end + 2
        elif ch in "\"'":
            j = i + 1
            while j < n and source[j] != ch:
                j += 2 if source[j] == "\\" else 1
            if ch == '"':
                literals.append(source[i + 1:j])
            i = j + 1
        else:
            i += 1
    return literals


def strip_log_calls(source: str) -> str:
    return LOG_CALL_RE.sub("", source)


class FontCoverageTest(unittest.TestCase):
    def test_fonts_cover_required_codepoints(self) -> None:
        required = gen_fonts.required_codepoints()
        for spec in gen_fonts.FONT_SPECS:
            path = ROOT / "assets" / "fonts" / f"{spec.name}.c"
            present = gen_fonts.read_font_codepoints(path)
            missing = sorted(required[spec.name] - present)
            self.assertFalse(missing, f"{spec.name} 缺字: " + ", ".join(f"U+{cp:04X}" for cp in missing)
                             + "（请运行 tools/gamebox/gen_fonts.py）")
            self.assertNotIn(0x9F98, present, "反例字符不应出现在字库中")

    def test_text_headers_found(self) -> None:
        names = {p.name for p in gen_fonts.text_headers()}
        self.assertIn("gb_text.h", names)
        self.assertIn("gb_launcher_text.h", names)
        self.assertTrue(any(ord(ch) > 0x7E for s in gen_fonts.ui_strings() for ch in s))

    def test_number_font_is_small(self) -> None:
        present = gen_fonts.read_font_codepoints(ROOT / "assets" / "fonts" / "gb_num_48.c")
        self.assertEqual(present, {ord(ch) for ch in gen_fonts.NUMBER_CHARS})

    def test_no_stray_non_ascii_literals(self) -> None:
        offenders = []
        sources = ui_sources()
        self.assertGreaterEqual(len(sources), 8)
        for path in sources:
            for literal in string_literals(strip_log_calls(path.read_text(encoding="utf-8"))):
                if any(ord(ch) > 0x7E for ch in literal):
                    offenders.append(f"{path.relative_to(ROOT)}: \"{literal}\"")
        self.assertFalse(offenders, "非 ASCII 界面文字请移到 *_text.h：\n" + "\n".join(offenders))

    def test_scanner_helpers(self) -> None:
        sample = '// "注释"\nconst char *a = "ok"; /* "块注释" */ char c = \'"\'; const char *b = "中\\"文";'
        self.assertEqual(string_literals(sample), ["ok", '中\\"文'])
        logged = 'ESP_LOGW(TAG, "失败: %s",\n         name); lv_label_set_text(l, "界面");'
        self.assertEqual(string_literals(strip_log_calls(logged)), ["界面"])


if __name__ == "__main__":
    unittest.main()
