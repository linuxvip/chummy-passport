"""Static checks for the short, curated Home quotation fallback."""

from pathlib import Path
import re


root = Path(__file__).resolve().parents[1]
source = (root / "main/apps/home/home_daily_quote.c").read_text()
start = source.index("static const char *const kCuratedQuotes[]")
end = source.index("};", start)
quotes = re.findall(r'"([^"]+)"', source[start:end])
assert len(quotes) >= 8
assert all(8 <= len(quote) <= 24 for quote in quotes), quotes

kconfig = (root / "main/Kconfig.projbuild").read_text()
assert "c=k" in kconfig and "min_length=8" in kconfig and "max_length=24" in kconfig
home = (root / "main/apps/home/app_home.cc").read_text()
assert "s_lbl_quote_source" not in home
assert "lines == 1 ? 15 : 1" in home
assert '"上干支 · 下名言 · 确定应用"' in home
assert '"长按上换句 · 长按下设置"' in home
assert "home_daily_quote_request_refresh(quote_day)" in home

print(f"Daily quote: {len(quotes)} short curated fallbacks, philosophy endpoint and text-only UI: PASS")
