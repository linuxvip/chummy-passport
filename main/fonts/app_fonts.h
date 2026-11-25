#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

// Type roles are recorded in DESIGN.md (Typography). Every font here is linked;
// do not add one without a role there.

// Body (正文): 5,886-glyph SourceHanSansSC 16px subset, 2bpp (GB2312 level 1,
// common level-2 characters and chat particles; see gen_buddy_font.sh).
// Top-bar app names, body text, lists, key hints and all dynamic text (city
// names, transcripts, replies). 2bpp is a flash budget decision: 4bpp would
// add ~320 KB.
LV_FONT_DECLARE(buddy_font_16);
// Boot splash title: the four glyphs in "命海拾遗" at 24px/4bpp.
LV_FONT_DECLARE(boot_title_24);
// Four pillars and the five visible Dayun cells: ten stems and twelve
// branches at 20px/4bpp.
LV_FONT_DECLARE(ganzhi_font_20);
// Flow-year table: the same 22 glyphs at 13px/4bpp.
LV_FONT_DECLARE(ganzhi_font_13);
// Chart headers and the small set of fixed UI labels at 12px.
LV_FONT_DECLARE(ganzhi_font_12);
// Compact launcher labels, sized for the 70px application grid.
LV_FONT_DECLARE(launcher_font_12);
LV_FONT_DECLARE(launcher_title_20);
// Dense calendar cells: ASCII, lunar days, solar terms and earthly branches.
LV_FONT_DECLARE(calendar_font_9);
// Title (标题): shared SourceHanSansSC 20px/4bpp subset for static titles.
// Missing glyphs fall back to the 16px body font, so new title text must add
// its glyphs; regenerate with:
//   regenerate from the repository's font tooling and verify with tests/check_title_glyphs.py
LV_FONT_DECLARE(ui_font_20);
// Display readout (大读数): Montserrat 32 digits, '.', ':', space and "NET",
// copied from LVGL's built-in font by tools/gen_digit_font.py.
LV_FONT_DECLARE(ui_digits_32);
// Liuyao chart terminology and hexagram names. The 12px face is the compact
// two-hexagram table; the 20px face emphasizes the centered gua names.
LV_FONT_DECLARE(liuyao_font_12);
LV_FONT_DECLARE(liuyao_title_20);

#ifdef __cplusplus
}
#endif
