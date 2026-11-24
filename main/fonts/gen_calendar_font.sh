#!/usr/bin/env bash
set -euo pipefail

PROJ="$(cd -- "$(dirname -- "$0")/../.." && pwd)"
OTF="managed_components/lvgl__lvgl/scripts/built_in_font/SourceHanSansSC-Normal.otf"
OUT="main/fonts/calendar_font_9.c"
SYMBOLS="日一二三四五六七八九正冬腊闰月初始廿十甲乙丙丁戊己庚辛壬癸子丑寅卯辰巳午未申酉戌亥小寒大寒立春雨水惊蛰春分清明谷雨立夏小满芒种夏至小暑大暑立秋处暑白露秋分寒露霜降立冬小雪大雪冬至"

cd "$PROJ"
npx --yes lv_font_conv \
  --font "$OTF" \
  --range 0x20-0x7e \
  --symbols "$SYMBOLS" \
  --size 9 --bpp 4 --format lvgl --no-compress --no-prefilter --no-kerning \
  --lv-include lvgl.h --lv-font-name calendar_font_9 \
  -o "$OUT"

echo "Generated $OUT"

# Keep generated sources stable under git diff --check.
perl -0777 -pi -e 's/\n+\z/\n/' "$OUT"
