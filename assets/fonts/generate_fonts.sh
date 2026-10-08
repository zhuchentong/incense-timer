#!/usr/bin/env bash
# 生成焚香计时器的中文子集字库（app_font_48 / app_font_24）。
#
# 输入：assets/fonts/incense_chars.txt（码点清单，必须覆盖 main/incense_text.h
# 全部字符——tests/test_incense_text.c 会校验）+ ASCII 0x20-0x7E。
# 工具：lv_font_conv 固定版本 1.5.3（经 npx 拉取，需要 node）。
# 字体：Source Han Sans SC Regular 2.004R（OFL 1.1，见本目录 LICENSE.txt）。
#
# 用法：仓库根目录下 ./assets/fonts/generate_fonts.sh
set -euo pipefail

repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
font="${repo_root}/assets/fonts/SourceHanSansSC-Regular.otf"
inventory="${repo_root}/assets/fonts/incense_chars.txt"
converter="lv_font_conv@1.5.3"

# 码点清单 → --range 参数（ASCII 区段 + 清单中逐个码点）。
range="0x20-0x7E"
while IFS= read -r line; do
    line="${line%%#*}"
    line="$(echo "$line" | tr -d '[:space:]')"
    [[ -z "$line" ]] && continue
    range="${range},$(echo "$line" | tr 'A-F' 'a-f')"
done < "$inventory"

cd "$repo_root"
for size in 48 24; do
    npx --yes "$converter" \
        --font "$font" \
        --range "$range" \
        --size "$size" --bpp 4 --format lvgl --no-compress \
        --lv-font-name "app_font_${size}" --lv-include lvgl.h \
        --output "assets/fonts/app_font_${size}.c"
done

echo "Generated: assets/fonts/app_font_48.c assets/fonts/app_font_24.c"
