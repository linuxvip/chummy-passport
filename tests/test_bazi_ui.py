"""Guard the agreed v1 Bazi overview and minute editing contract."""

from pathlib import Path


root = Path(__file__).resolve().parents[1]
source = (root / "main/apps/bazi/app_bazi.cc").read_text()

assert "s_gods" not in source, "Bazi overview must not create Ten Gods labels"
assert "s_hidden" not in source, "Bazi overview must not create hidden-stem labels"
assert "bazi_minute_step(s_birth.minute, delta)" in source
assert "delta * 5" not in source, "Minute editing must advance one minute per click"
assert "bazi_birth_max_day(&s_birth)" in source
assert "if (day < 1) day = max_day" in source
assert "if (day > max_day) day = 1" in source

print("Bazi UI: four pillars only, month-aware day range, minute advances by one: PASS")
