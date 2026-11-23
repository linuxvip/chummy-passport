"""Guard the Calendar month-cell and key-instruction contract."""

from pathlib import Path


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

print("Calendar UI: large day/Ganzhi cells, selection summary and navigation hints: PASS")
