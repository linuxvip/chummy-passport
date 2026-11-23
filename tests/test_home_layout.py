"""Check every Home preset is independently selectable and persisted."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
layout = (ROOT / "main/apps/home/home_layout.h").read_text()
settings = (ROOT / "main/launcher/settings.cc").read_text()
home = (ROOT / "main/apps/home/app_home.cc").read_text()

for value in (
    "HOME_LAYOUT_ALMANAC = 0",
    "HOME_LAYOUT_MINIMAL = 1",
    "HOME_LAYOUT_WEATHER = 2",
    "HOME_LAYOUT_PROFILE = 3",
    "HOME_LAYOUT_COUNT",
):
    assert value in layout
for label in ("命海拾遗", "极简四柱", "天气生活", "人物志"):
    assert label in layout

assert "constexpr size_t kRowCount = 11;" in settings
assert '"首页样式"' in settings
assert 'nvs_set_u8(handle, "home_layout"' in settings
assert 'nvs_get_u8(handle, "home_layout"' in settings
assert "s_home_layout = HOME_LAYOUT_ALMANAC;" in settings

for builder in ("create_almanac_ui", "create_minimal_ui", "create_weather_ui",
                "create_profile_ui"):
    assert f"void {builder}()" in home
assert "s_layout = settings_get_home_layout();" in home
assert "HOME_LAYOUT_MINIMAL" in home
assert "HOME_LAYOUT_WEATHER" in home
assert "HOME_LAYOUT_PROFILE" in home
assert "sync_profile_photo();" in home
assert "一页人物志" not in home
assert "一张彩色相片" not in home
print("Home layouts: four presets, settings cycling and NVS selection PASS")
