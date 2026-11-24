"""Guard the Calendar month-cell and key-instruction contract."""

from pathlib import Path
import re


root = Path(__file__).resolve().parents[1]
source = (root / "main/apps/calendar/app_calendar.cc").read_text()

assert "cell.day" in source
assert "cell.ganzhi" in source
assert "cell.sub" not in source
assert "info.day_ganzhi" in source
assert "s_selection_summary" in source
assert "selected.lunar_full" in source
assert "&lv_font_montserrat_14" in source
assert "&ganzhi_font_12" in source
assert "长按上今天 · 长按下年月" in source
assert "LV_LABEL_LONG_MODE_DOTS" in source
assert "const char *const kHourRanges[]" in source
assert '"23-01", "01-03", "03-05", "05-07", "07-09", "09-11"' in source
assert '"11-13", "13-15", "15-17", "17-19", "19-21", "21-23"' in source
assert "for (int branch = 0; branch < 12; ++branch)" in source
assert "five_element_color(five_element_for_glyph(stem))" in source
assert "five_element_color(five_element_for_glyph(branch_text))" in source
assert "s_current_hour_branch" in source
assert "十二时辰" in source
assert "set_text(range, kHourRanges[branch])" in source
assert "子时\", \"丑时" not in source


def constant(name: str) -> int:
    match = re.search(rf"constexpr int {name} = (\d+);", source)
    assert match, f"missing {name}"
    return int(match.group(1))


card_y = constant("kHourCardY")
card_h = constant("kHourCardH")
header_h = constant("kHourHeaderH")
row_h = constant("kHourRowH")
assert card_y + card_h <= 270, "hour card must clear the footer separator at y=272"
assert header_h + 2 * row_h <= card_h, "two hour rows must fit inside the card"

print("Calendar UI: large month cells, twelve two-row hour pillars and five-element colors: PASS")
