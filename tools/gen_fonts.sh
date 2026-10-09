#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$root"
ttf="managed_components/lvgl__lvgl/scripts/generators/built_in_font/Montserrat-Medium.ttf"
out="components/ui/fonts"
for size in 72 120; do
    npx -y lv_font_conv@1.5.3 --no-compress --bpp 4 --size "$size" --font "$ttf" -r 0x30-0x3A \
        --format lvgl --lv-include lvgl.h --lv-font-name "font_digits_$size" -o "$out/font_digits_$size.c"
done
