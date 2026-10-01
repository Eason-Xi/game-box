#!/usr/bin/env bash
# 游戏盒主机渲染预览：用 managed_components 里同版本的 LVGL 9.5 在主机上渲染
# 真实的游戏列表与各游戏页面，截图写到 build/gamebox_preview/，并检查反复
# 进出游戏后 LVGL 内存池没有泄漏（泄漏时以非零码退出）。
#
# 前提：已执行过一次 idf.py build 或 ./tools/validate.sh --firmware
# （用于下载 managed_components/lvgl__lvgl）。
# 可选：安装 Pillow 时额外生成拼图 contact_sheet.png。
# 这是开发辅助工具，不属于 validate.sh 门禁，也不替代真机测试。
set -euo pipefail

repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
lvgl="${repo_root}/managed_components/lvgl__lvgl"
out="${repo_root}/build/gamebox_preview"
cc_bin="${CC:-cc}"
# 与 sdkconfig.defaults 的 CONFIG_LV_MEM_SIZE_KILOBYTES 保持一致。
defines=(-DLV_CONF_SKIP "-DLV_MEM_SIZE=(64*1024U)")

if [[ ! -d "${lvgl}/src" ]]; then
    echo "ERROR: ${lvgl} 不存在，请先运行一次 idf.py build 下载依赖。" >&2
    exit 1
fi

mkdir -p "${out}/obj"
# LVGL 只需编译一次；源码更新后删除 build/gamebox_preview/liblvgl.a 重新生成。
if [[ ! -f "${out}/liblvgl.a" ]]; then
    echo "编译主机版 LVGL..."
    export cc_bin lvgl out
    find "${lvgl}/src" -name '*.c' -print0 |
        xargs -0 -n 1 -P "$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)" sh -c '
            obj="${out}/obj/$(echo "${1#"${lvgl}/src/"}" | tr "/" "_" | sed "s/\.c$/.o/")"
            "${cc_bin}" -c -O1 -w -DLV_CONF_SKIP "-DLV_MEM_SIZE=(64*1024U)" \
                -I"${lvgl}" -I"${lvgl}/src" "$1" -o "${obj}"' _
    ar rcs "${out}/liblvgl.a" "${out}"/obj/*.o
fi

main="${repo_root}/main"
"${cc_bin}" -O1 -std=gnu11 -Wall -Wextra "${defines[@]}" \
    -I"${repo_root}/tools/gamebox_preview/stubs" \
    -I"${main}" -I"${main}/common" -I"${main}/launcher" \
    -I"${main}/games/crane" -I"${main}/games/flappy" -I"${main}/games/dodge" -I"${main}/games/tap" \
    -I"${repo_root}/components/bsp/include" -I"${repo_root}/components/bsp/src" \
    -I"${lvgl}" -I"${lvgl}/src" \
    "${repo_root}/tools/gamebox_preview/harness.c" "${repo_root}/tools/gamebox_preview/flappy_probe.c" \
    "${main}/gb_registry.c" "${main}/launcher/gb_launcher.c" "${main}/launcher/gb_launcher_layout.c" \
    "${main}/common/gb_ui.c" "${main}/common/gb_canvas.c" "${main}/common/gb_i4.c" "${main}/common/gb_tone.c" \
    "${main}/games/crane/crane_model.c" "${main}/games/crane/crane_flow.c" "${main}/games/crane/crane_sfx.c" \
    "${main}/games/flappy/flappy_model.c" "${main}/games/flappy/flappy_flow.c" "${main}/games/flappy/flappy_sfx.c" \
    "${main}/games/dodge/dodge_game.c" "${main}/games/dodge/dodge_model.c" "${main}/games/dodge/dodge_draw.c" \
    "${main}/games/tap/tap_game.c" "${main}/games/tap/tap_model.c" \
    "${repo_root}/components/bsp/src/bsp_display_rounding.c" \
    "${repo_root}"/assets/fonts/gb_*.c "${repo_root}/assets/images/flappy_sprites.c" \
    "${out}/liblvgl.a" -o "${out}/preview"

rm -f "${out}"/*.ppm
"${out}/preview" "${out}" | grep -E '^(shot|lifecycle|LEAK|FAIL|LVGL|preview|E |W )'

if python3 -c 'import PIL' 2>/dev/null; then
    python3 - "${out}" <<'EOF'
import glob, os, sys
from PIL import Image
out = sys.argv[1]
files = sorted(glob.glob(os.path.join(out, "*.ppm")))
cols = 6
rows = (len(files) + cols - 1) // cols
sheet = Image.new("RGB", (cols * 250, rows * 330), (32, 32, 32))
for i, f in enumerate(files):
    sheet.paste(Image.open(f), ((i % cols) * 250 + 5, (i // cols) * 330 + 5))
sheet.save(os.path.join(out, "contact_sheet.png"))
print("contact sheet:", os.path.join(out, "contact_sheet.png"))
EOF
fi
echo "截图目录: ${out}"
