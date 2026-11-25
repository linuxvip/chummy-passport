"""Guard the agreed portrait-casting, landscape-chart Liuyao contract."""

from pathlib import Path
import re


root = Path(__file__).resolve().parents[1]
app = (root / "main/apps/liuyao/app_liuyao.cc").read_text()
core = (root / "main/apps/liuyao/liuyao_core.c").read_text()
core_header = (root / "main/apps/liuyao/liuyao_core.h").read_text()
cmake = (root / "main/CMakeLists.txt").read_text()
registry = (root / "main/launcher/app_registry.cc").read_text()
capture = (root / "main/ui/ui_capture.c").read_text()

assert 'LIUYAO_METHOD_TIME' in core_header and 'LIUYAO_METHOD_NUMBER' in core_header
assert 'fill_time_preview(chart)' in core
assert 'fill_number_preview(chart)' in core
assert '"时间起卦"' in app and '"数字起卦"' in app
assert 'set_landscape(false);' in app and 'set_landscape(true);' in app
# Landscape chart must put the physical keys on the top edge, which is the
# opposite rotation from LV_DISPLAY_ROTATION_90 (keys on the bottom edge).
assert 'LV_DISPLAY_ROTATION_270' in app
# Wi-Fi signal sits on the left like the other apps, not next to the battery.
assert 'ui_pixel_wifi_bars_create(top, UI_STATUS_WIFI_X' in app
# The chart has no bottom hint row; the meaning page uses the shared top bar.
assert '"确定看卦意' not in app
assert 'add_top_bar("六爻排盘-卦意")' in app
assert 'add_band("卦意"' not in app
assert 'void cast_current_time(void)' in app
assert 'cast_current_time();' in app
assert 'open_editor(LIUYAO_METHOD_NUMBER);' in app
assert '"当前时间直接起卦"' in app
assert '事项' not in app
assert 'kCategories' not in app
assert 'add_hex_title(1, "本卦"' in app
assert 'add_hex_title(0, "变卦"' in app
assert 'void build_meaning(void)' in app
assert 'liuyao_hex_explanation(s_chart.original_id)' in app
assert 'liuyao_hex_explanation(s_chart.changed_id)' in app
assert '单爻详参' not in app
assert 'return line.yang ? "O →" : "X →";' in app
assert '初爻' not in app and '二爻' not in app and '三爻' not in app
assert '世%u应%u' not in app
assert 'label(box, name, &buddy_font_16, UI_WARNING, 4, y, 36, 20);' in app
assert 'label(box, value, &buddy_font_16, UI_TEXT, 44, y, 252, 20);' in app
assert 'meta_row_text(meta, 40, "旬空", text);' in app
assert 'home_profile_get(&profile)' in app
assert 'BAZI_TIME_TRUE_SOLAR' in app
assert '"六爻排盘-%s"' in app
assert 'add_top_bar(title);' in app
assert 'add_top_bar("六爻排盘-起卦方式");' in app
assert 'add_band("起卦方式", "");' not in app
assert 'nvs_set' not in app
assert 'nvs_' not in app
assert 'apps/liuyao/liuyao_core.c' in cmake
assert 'apps/liuyao/app_liuyao.cc' in cmake
assert '&g_liuyao_app' in registry
assert 'APP_ID_LIUYAO' in registry
assert 'lv_display_get_horizontal_resolution(disp)' in capture
assert 'lv_display_get_vertical_resolution(disp)' in capture

used = "".join(re.findall(r'"([^"\\]*(?:\\.[^"\\]*)*)"', app + core))
used += "甲乙丙丁戊己庚辛壬癸子丑寅卯辰巳午未申酉戌亥"
for font in ("liuyao_font_12.c", "liuyao_title_20.c"):
    glyph_source = (root / "main/fonts" / font).read_text(errors="ignore")
    glyphs = {chr(int(match, 16)) for match in re.findall(r"/\* U\+([0-9A-Fa-f]{4,6})", glyph_source)}
    missing = sorted({char for char in used if ord(char) > 127 and char not in glyphs})
    assert not missing, (font, "".join(missing))

print("Liuyao UI: portrait casting, direct time cast, landscape chart, centered names, X/O markers: PASS")
