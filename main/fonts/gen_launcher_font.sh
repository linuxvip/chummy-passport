#!/usr/bin/env bash
set -euo pipefail

PROJ="$(cd -- "$(dirname -- "$0")/../.." && pwd)"
OTF="$PROJ/managed_components/lvgl__lvgl/scripts/built_in_font/SourceHanSansSC-Normal.otf"
OUT="$PROJ/main/fonts/launcher_font_12.c"
SYMBOLS="小智AI城市收音机音乐播放器番茄时钟秒表备忘提醒文字清单八字排盘万年历设置选择一个应用上下逐项·长按跨行确定打开返回"

cd "$PROJ"
npx --yes lv_font_conv \
  --font "$OTF" \
  --range 0x20-0x7e \
  --symbols "$SYMBOLS" \
  --size 12 --bpp 2 --format lvgl --no-compress --no-prefilter --no-kerning \
  --lv-include lvgl.h --lv-font-name launcher_font_12 \
  -o "$OUT"

perl -0777 -pi -e 's/\n+\z/\n/' "$OUT"
echo "Generated $OUT"
