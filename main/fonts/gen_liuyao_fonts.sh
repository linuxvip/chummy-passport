#!/usr/bin/env bash
set -euo pipefail

PROJ="$(cd -- "$(dirname -- "$0")/../.." && pwd)"
OTF="$PROJ/managed_components/lvgl__lvgl/scripts/built_in_font/SourceHanSansSC-Normal.otf"
SYMBOL_FILE="$PROJ/main/fonts/liuyao_symbols.txt"
SYMBOLS="$(tr -d '\r\n ' < "$SYMBOL_FILE")"

cd "$PROJ"

npx --yes lv_font_conv \
  --font "$OTF" \
  --range 0x20-0x7e \
  --symbols "${SYMBOLS}→" \
  --size 12 --bpp 4 --format lvgl --no-compress --no-prefilter --no-kerning \
  --lv-include lvgl.h --lv-font-name liuyao_font_12 \
  -o main/fonts/liuyao_font_12.c

npx --yes lv_font_conv \
  --font "$OTF" \
  --range 0x20-0x7e \
  --symbols "${SYMBOLS}→" \
  --size 20 --bpp 4 --format lvgl --no-compress --no-prefilter --no-kerning \
  --lv-include lvgl.h --lv-font-name liuyao_title_20 \
  -o main/fonts/liuyao_title_20.c

perl -0777 -pi -e 's/\n+\z/\n/' main/fonts/liuyao_font_12.c main/fonts/liuyao_title_20.c
