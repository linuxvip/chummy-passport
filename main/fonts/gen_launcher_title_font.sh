#!/usr/bin/env bash
set -euo pipefail

PROJ="$(cd -- "$(dirname -- "$0")/../.." && pwd)"
OTF="$PROJ/managed_components/lvgl__lvgl/scripts/built_in_font/SourceHanSansSC-Normal.otf"
OUT="$PROJ/main/fonts/launcher_title_20.c"
SYMBOLS="小智城市收音机音乐播放器备忘提醒事项清单八字六爻排盘万年历手机遥控设置"

cd "$PROJ"
npx --yes lv_font_conv \
  --font "$OTF" \
  --range 0x20-0x7e \
  --symbols "$SYMBOLS" \
  --size 20 --bpp 4 --format lvgl --no-compress --no-prefilter --no-kerning \
  --lv-include lvgl.h --lv-font-name launcher_title_20 \
  -o "$OUT"

perl -0777 -pi -e 's/\n+\z/\n/' "$OUT"
echo "Generated $OUT"
