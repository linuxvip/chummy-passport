#!/usr/bin/env bash
set -euo pipefail

PROJ="$(cd -- "$(dirname -- "$0")/../.." && pwd)"
OTF="$PROJ/managed_components/lvgl__lvgl/scripts/built_in_font/SourceHanSansSC-Normal.otf"
OUT="$PROJ/main/fonts/boot_title_24.c"
SYMBOLS="命海拾遗"

cd "$PROJ"
npx --yes lv_font_conv \
  --font "$OTF" \
  --symbols "$SYMBOLS" \
  --size 24 --bpp 4 --format lvgl --no-compress --no-prefilter --no-kerning \
  --lv-include lvgl.h --lv-font-name boot_title_24 \
  -o "$OUT"

perl -0777 -pi -e 's/\n+\z/\n/' "$OUT"
echo "Generated $OUT"
