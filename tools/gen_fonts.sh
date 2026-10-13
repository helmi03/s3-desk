#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$root"
ttf="components/ui/fonts/paper-mono/PaperMono-Medium.ttf"
icons="managed_components/lvgl__lvgl/scripts/generators/built_in_font/FontAwesome5-Solid+Brands+Regular.woff"
out="components/ui/fonts"
conv() { npx -y lv_font_conv@1.5.3 --no-compress --bpp 4 --format lvgl --lv-include lvgl.h "$@"; }

# Text: printable ASCII plus the degree sign and middle dot.
for size in 14 28 36 48; do
    conv --size "$size" --font "$ttf" -r 0x20-0x7E,0xB0,0xB7 --lv-font-name "font_paper_$size" -o "$out/font_paper_$size.c"
done
# The dropdowns use size 18; LV_SYMBOL_DOWN (0xF078) is their arrow.
conv --size 18 --font "$ttf" -r 0x20-0x7E,0xB0,0xB7 --font "$icons" -r 0xF078 --lv-font-name font_paper_18 -o "$out/font_paper_18.c"
# The Pomodoro buttons use size 22 with LV_SYMBOL_REFRESH and LV_SYMBOL_NEXT.
conv --size 22 --font "$ttf" -r 0x20-0x7E,0xB0,0xB7 --font "$icons" -r 0xF021,0xF051 --lv-font-name font_paper_22 -o "$out/font_paper_22.c"
# Big time digits.
for size in 72 120; do
    conv --size "$size" --font "$ttf" -r 0x20,0x30-0x3A,0x41,0x4D,0x50 --lv-font-name "font_digits_$size" -o "$out/font_digits_$size.c"
done
