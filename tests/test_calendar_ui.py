"""Guard the Calendar month-cell and key-instruction contract."""

from pathlib import Path


root = Path(__file__).resolve().parents[1]
source = (root / "main/apps/calendar/app_calendar.cc").read_text()

assert "cell.day" in source
assert "cell.sub" in source
assert "cell.ganzhi" in source
assert "info.day_ganzhi" in source
assert "长按上今天 · 长按下年月" in source
assert "LV_LABEL_LONG_MODE_DOTS" in source

print("Calendar UI: three-line cells and navigation hints: PASS")
