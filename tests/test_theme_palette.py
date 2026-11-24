"""Verify all themes and the shared five-element palette are consistently wired."""

from pathlib import Path
import re


ROOT = Path(__file__).resolve().parents[1]
header = (ROOT / "main/ui/ui_palette.h").read_text()
pixel = (ROOT / "main/ui/ui_pixel.c").read_text()
home = (ROOT / "main/apps/home/app_home.cc").read_text()
bazi = (ROOT / "main/apps/bazi/app_bazi.cc").read_text()
calendar = (ROOT / "main/apps/calendar/app_calendar.cc").read_text()

for declaration in (
    "UI_THEME_PAPER = 4",
    "UI_THEME_NIGHT = 5",
):
    assert declaration in header

names = {
    "UI_THEME_CLASSIC": "经典绿",
    "UI_THEME_TRAVEL": "旅行仪器",
    "UI_THEME_PLAYFUL": "像素白",
    "UI_THEME_NEON": "霓虹黑",
    "UI_THEME_PAPER": "宣纸朱砂",
    "UI_THEME_NIGHT": "青金守夜",
}
for key, label in names.items():
    assert re.search(
        rf"case {key}:\s*return \"{label}\";|default:\s*return \"{label}\";",
        pixel,
    )


def palette(theme: str) -> list[str]:
    match = re.search(
        rf"\[{theme}\]\s*=\s*\{{(.*?)\}},\s*(?=\[UI_THEME_|\}};)",
        pixel,
        re.S,
    )
    assert match, f"missing {theme} palette"
    colors = re.findall(r"0x([0-9A-Fa-f]{6})", match.group(1))
    assert len(colors) == 13, f"{theme}: expected 13 color roles, got {len(colors)}"
    return colors


def luminance(value: str) -> float:
    rgb = [int(value[i:i + 2], 16) / 255 for i in (0, 2, 4)]
    linear = [v / 12.92 if v <= 0.04045 else ((v + 0.055) / 1.055) ** 2.4 for v in rgb]
    return 0.2126 * linear[0] + 0.7152 * linear[1] + 0.0722 * linear[2]


def contrast(a: str, b: str) -> float:
    high, low = sorted((luminance(a), luminance(b)), reverse=True)
    return (high + 0.05) / (low + 0.05)


for theme in ("UI_THEME_PAPER", "UI_THEME_NIGHT"):
    bg, _, _, _, text, dim, accent, _, on_accent, *_ = palette(theme)
    assert contrast(bg, text) >= 7.0
    assert contrast(bg, dim) >= 4.5
    assert contrast(bg, accent) >= 4.5
    assert contrast(accent, on_accent) >= 4.5

assert "s_ui_theme == UI_THEME_PAPER" in pixel
assert "s_ui_theme == UI_THEME_NIGHT" in pixel
assert "ui_theme_element_color" in home
assert "ui_theme_element_color" in bazi
assert "ui_theme_element_color" in calendar
for theme in names:
    entries = re.findall(
        rf"\[{theme}\]\s*=\s*\{{([^{{}}]*)\}}",
        pixel,
    )
    assert any(len(re.findall(r"0x([0-9A-Fa-f]{6})", entry)) == 5 for entry in entries)
print("Theme palettes: six themes, shared five-element colors and contrast PASS")
