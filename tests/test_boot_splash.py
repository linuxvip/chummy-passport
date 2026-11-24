"""Guard the selected dawn boot-splash implementation and its font coverage."""

from pathlib import Path


root = Path(__file__).resolve().parents[1]
source = (root / "main/boot_splash.c").read_text(encoding="utf-8")
header = (root / "main/boot_splash.h").read_text(encoding="utf-8")
fonts = (root / "main/fonts/boot_title_24.c").read_text(encoding="utf-8")
cmake = (root / "main/CMakeLists.txt").read_text(encoding="utf-8")

assert "命海日出" in header
assert '"命海拾遗"' in source
assert '"从一日之始，拾起岁月"' in source
assert '"ALMANAC / WEATHER / VOICE"' in source
assert "kGold = 0xF0C76B" in source
assert "kSkyBottom = 0x092730" in source
assert "#define kBarN 14" in source
assert "#define kStageN 4" in source
assert "#define kStageMs 80" in source
assert "lv_obj_set_y(s_sun_stage, 100 -" in source
assert "lv_timer_create(dawn_step, kStageMs, NULL)" in source
assert "lv_line_set_points" in source
assert "boot_title_24" in source
assert "fonts/boot_title_24.c" in cmake
assert "TRAVEL INSTRUMENT" not in source
assert "#include \"boot_splash.h\"" in source
assert "LV_GRAD_DIR_VER" in source
assert "LV_OPA_TRANSP" in source

for char, codepoint in {
    "命": "U+547D",
    "海": "U+6D77",
    "拾": "U+62FE",
    "遗": "U+9057",
}.items():
    assert codepoint in fonts, f"missing boot title glyph {char}"

print("Boot splash: dawn scene, 14-stage rise animation and title glyphs PASS")
