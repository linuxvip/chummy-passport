"""Guard the agreed Bazi overview and single-page birth editor contract."""

import re
from pathlib import Path


root = Path(__file__).resolve().parents[1]
source = (root / "main/apps/bazi/app_bazi.cc").read_text(encoding="utf8")

assert "s_gods" not in source, "Bazi overview must not create Ten Gods labels"
assert "s_hidden" not in source, "Bazi overview must not create hidden-stem labels"
assert "bazi_minute_step(s_birth.minute, delta)" in source
assert "delta * 5" not in source, "Minute editing must advance one minute per click"
assert "bazi_birth_max_day(&s_birth)" in source
assert "if (day < 1) day = max_day" in source
assert "if (day > max_day) day = 1" in source

# Single-page birth editor: composite date/time rows, a fixed calculate row,
# and three compact section headers that fit above the shared footer.
assert "出生日期" in source and "出生时间" in source
assert "开始排盘" in source
assert "上下调整 · 确定下一段" in source
assert "上下选择 · 确定切换" in source
assert "liuyao_font_12" in source


def function_body(name):
    """Return a top-level function body, matching braces without a C parser."""
    match = re.search(rf"\b{name}\s*\([^;{{]*\)\s*\{{", source)
    assert match, f"missing {name}()"
    depth = 1
    cursor = match.end()
    while cursor < len(source) and depth:
        if source[cursor] == "{":
            depth += 1
        elif source[cursor] == "}":
            depth -= 1
        cursor += 1
    assert depth == 0, f"unbalanced braces in {name}()"
    return source[match.end():cursor - 1]


def int_array(name, count):
    match = re.search(rf"{name}\s*\[[^\]]*\]\s*=\s*\{{([^}}]*)\}}", source)
    assert match, f"missing {name}"
    values = [int(value) for value in re.findall(r"-?\d+", match.group(1))]
    assert len(values) == count, f"{name} has {len(values)} values, expected {count}"
    return values


def assert_disjoint(intervals, label):
    ordered = sorted(intervals)
    for (_, end), (start, _) in zip(ordered, ordered[1:]):
        assert end <= start, f"{label} intervals overlap: {ordered}"


rows = int(re.search(r"constexpr uint8_t kEditorRows = (\d+);", source).group(1))
assert rows == 8
row_tops = int_array("kEditorRowTop", rows)
row_heights = [24] * (rows - 1) + [26]
assert "const int height = calculate ? 26 : 24;" in function_body("editor_row")
for index in range(rows - 1):
    assert row_tops[index] + row_heights[index] <= row_tops[index + 1], (
        f"editor row {index} overlaps row {index + 1}")
    assert row_tops[index] >= row_tops[index - 1] if index else True
assert row_tops[0] >= 27 and row_tops[-1] + row_heights[-1] <= 269, (
    "editor rows must stay between the top bar and the footer rule at y=272")

field_enum = re.search(r"enum class Field[^{]*\{(.*?)\};", source, re.S).group(1)
fields = [item.strip() for item in field_enum.split(",") if item.strip()]
assert fields == [
    "Sex", "Calendar", "Date", "Time", "ZiRule", "City", "TimeMode",
    "Calculate", "Count",
], f"unexpected field order: {fields}"

chip_calls = re.findall(
    r'editor_chip\(\(uint8_t\)Field::(\w+),\s*(\d+),\s*"([^"]*)",\s*(\d+),\s*(\d+)\)',
    source)
assert len(chip_calls) == 9, "expected 2 sex + 3 calendar + 2 zi + 2 time-mode chips"
chips_by_field = {}
for field, index, _text, x, width in chip_calls:
    x, width, index = int(x), int(width), int(index)
    assert 0 <= x and x + width <= 216, f"{field} chip {index} escapes its row"
    chips_by_field.setdefault(field, []).append((index, x, width))
for field, entries in chips_by_field.items():
    entries.sort()
    assert [entry[0] for entry in entries] == list(range(len(entries))), (
        f"{field} chip indexes are not sequential")
    assert_disjoint([(x, x + width) for _, x, width in entries],
                    f"{field} chips")

date_x = int_array("kDatePartX", 3)
date_w = int_array("kDatePartW", 3)
time_x = int_array("kTimePartX", 2)
time_w = int_array("kTimePartW", 2)
for x, width in list(zip(date_x, date_w)) + list(zip(time_x, time_w)):
    assert 0 <= x and x + width <= 216, "date/time segment escapes its row"
assert_disjoint([(date_x[i], date_x[i] + date_w[i]) for i in range(3)],
                "date segments")
assert_disjoint([(time_x[i], time_x[i] + time_w[i]) for i in range(2)],
                "time segments")

separators = [
    (int(x), int(x) + int(width))
    for _mark, x, width in re.findall(
        r'label\(row,\s*"([-:])",\s*&buddy_font_16,\s*UI_TEXT_DIM,\s*(\d+),\s*2,\s*(\d+),',
        source)
]
assert len(separators) == 3, "expected two date separators and one time separator"
for x, end in separators:
    assert 0 <= x and end <= 216, "separator escapes its row"
assert_disjoint(
    [(date_x[i], date_x[i] + date_w[i]) for i in range(3)] +
    [separators[0], separators[1]], "date parts and separators")
assert_disjoint(
    [(time_x[i], time_x[i] + time_w[i]) for i in range(2)] + [separators[2]],
    "time parts and separator")

leap_body = function_body("normalize_birth_day")
assert "s_birth.lunar_leap = 0;" in leap_body
assert "bazi_birth_max_day(&s_birth)" in leap_body
calendar_body = function_body("cycle_calendar")
assert "lunar_leap_available()" in calendar_body
assert "s_birth.lunar_leap = 1;" in calendar_body
city_body = function_body("adjust_city")
assert "s_birth.longitude_e4 = bazi_city_longitude_e4(s_birth.city_id);" in city_body
activate_body = function_body("activate_editor_field")
for field in ("Sex", "Calendar", "Date", "Time", "ZiRule", "City", "TimeMode"):
    assert f"Field::{field}" in activate_body
assert "s_birth.sex =" in activate_body
assert "s_birth.zi_rule =" in activate_body
assert "s_birth.time_mode =" in activate_body
key_body = function_body("editor_key")
assert key_body.index("BTN_EVT_LONG") < key_body.index("event != BTN_EVT_CLICK"), (
    "long-DOWN must be handled before the click-only filter")
assert "adjust_active_field(1)" in key_body
assert "adjust_active_field(-1)" in key_body
assert "advance_editor_segment()" in key_body
advance_body = function_body("advance_editor_segment")
assert "move_editor(1)" in advance_body
assert "s_editor_editing = false;" in advance_body
assert '"确定排盘"' in function_body("refresh_editor"), (
    "the calculate row must name its action in the footer")

# Chart: five flow-year rows replace the two-line footer; page dots carry the
# position without spending another row.
assert "constexpr uint8_t kVisibleRows = 5;" in source
chart_body = function_body("build_chart")
assert "ui_pixel_footer" not in chart_body, (
    "the chart must spend the footer area on flow years")
assert "s_flow_dots" in chart_body
flow_top = int(re.search(r"constexpr int kFlowTop = (\d+);", source).group(1))
flow_step = int(re.search(r"constexpr int kFlowStep = (\d+);", source).group(1))
flow_height = int(re.search(r"constexpr int kFlowHeight = (\d+);", source).group(1))
assert flow_step >= flow_height + 1
assert flow_top + 4 * flow_step + flow_height <= 318, (
    "five flow rows must stay inside the screen")
assert "s_flow_dots" in function_body("refresh_luck_table")

# Luck table: five wider columns, 20px luck glyphs, long-press window scroll.
assert "constexpr uint8_t kVisibleLuck = 5;" in source
assert "constexpr int kColumnW = 43;" in source
column_w = int(re.search(r"constexpr int kColumnW = (\d+);", source).group(1))
assert 12 + 5 * column_w <= 228, "five luck columns must fit the 216px table"
assert "&ganzhi_font_20" in chart_body, "luck cells use the 20px pillar font"
assert "&ganzhi_font_13" in chart_body, "flow cells use the 13px compact font"
chart_key = function_body("chart_key")
assert "BTN_EVT_LONG" in chart_key
assert "BSP_BTN_UP" in chart_key and "BSP_BTN_DOWN" in chart_key
assert "open_editor" not in chart_key, (
    "long DOWN now moves the luck window; long OK still returns")
assert "reset_luck_window()" in chart_key
reset_window = function_body("reset_luck_window")
assert "BAZI_LUCK_COUNT - kVisibleLuck" in reset_window
refresh_body = function_body("refresh_luck_table")
assert '"起运%d 大运%d-%d"' in refresh_body
assert '"流年%u-%u"' in refresh_body
assert "s_chart.luck.step[0].start_year" in refresh_body
assert "s_chart.luck.step[s_selected_luck].start_year" in refresh_body
assert "BAZI_YEARS_PER_LUCK - 1" in refresh_body
assert '"大运 %s %d %u-%u"' not in source
assert "12, 132, 144, 14" in source
assert "156, 132, 60, 14" in source

# Selected luck: the cursor moves first; the window follows only at an edge.
assert "s_selected_luck" in source
assert "s_luck_marks" in source
assert "s_selected_luck = s_current_luck;" in reset_window
assert "--s_selected_luck" in chart_key and "++s_selected_luck" in chart_key
assert "s_luck_first = s_selected_luck;" in chart_key
assert "s_selected_luck - kVisibleLuck + 1" in chart_key
refresh_body = function_body("refresh_luck_table")
assert "s_luck_marks" in refresh_body
assert "const bool selected_col = luck_index == s_selected_luck;" in refresh_body
assert "selected_col && luck_index == s_current_luck" in refresh_body

title_font = (root / "main/fonts/liuyao_font_12.c").read_text(
    encoding="utf8", errors="ignore")
title_glyphs = {
    chr(int(m.group(1), 16))
    for m in re.finditer(r"/\* U\+([0-9A-Fa-f]{4,6})", title_font)
}
for ch in "基本出生时刻地点与校正":
    assert ch in title_glyphs, f"liuyao_font_12 missing section glyph {ch}"

print("Bazi UI: four pillars, single-page editor, bounds and keys guarded: PASS")
