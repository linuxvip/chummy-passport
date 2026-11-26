"""Guard the switchable Passport Flow / application-grid launcher contract."""

from pathlib import Path
import re


root = Path(__file__).resolve().parents[1]
launcher = (root / "main/launcher/launcher.cc").read_text(encoding="utf8")
settings = (root / "main/launcher/settings.cc").read_text(encoding="utf8")
settings_header = (root / "main/launcher/settings.h").read_text(encoding="utf8")
style_header = (root / "main/launcher/launcher_style.h").read_text(encoding="utf8")
registry = (root / "main/launcher/app_registry.cc").read_text(encoding="utf8")


def glyphs(font_name: str) -> set[str]:
    source = (root / f"main/fonts/{font_name}.c").read_text(
        encoding="utf8", errors="ignore"
    )
    return {
        chr(int(value, 16))
        for value in re.findall(r"/\* U\+([0-9A-Fa-f]{4,6})", source)
    }


def integer(name: str) -> int:
    match = re.search(rf"constexpr (?:size_t|int) {name} = (\d+);", launcher)
    assert match, f"missing {name}"
    return int(match.group(1))


columns = integer("kGridColumns")
rows = integer("kGridRows")
max_tiles = integer("kGridMaxTiles")
tile_w = integer("kGridTileW")
tile_h = integer("kGridTileH")
gap_x = integer("kGridColumnGap")
gap_y = integer("kGridRowGap")
left = integer("kGridLeft")
top = integer("kGridTop")
icon = integer("kGridIconSize")
footer_rule = integer("kGridFooterRuleY")

assert columns == 3 and rows == 3 and max_tiles == 9
assert left + columns * tile_w + (columns - 1) * gap_x <= 240
assert top + rows * tile_h + (rows - 1) * gap_y <= footer_rule
assert icon <= tile_w
assert "lv_obj_set_pos(tile.label, 0, 51);" in launcher
assert "lv_obj_set_size(tile.label, kGridTileW, 16);" in launcher

app_array = registry.split("const passport_app_t *s_apps[] = {", 1)[1].split("};", 1)[0]
registered = re.findall(r"&g_(\w+)_app", app_array)
assert len(registered) == max_tiles, (
    f"grid expects {max_tiles} registered apps, found {len(registered)}; "
    "add pagination before expanding the grid"
)

assert "LAUNCHER_STYLE_FLOW = 0" in style_header
assert "LAUNCHER_STYLE_GRID = 1" in style_header
assert "return \"单页轮播\"" in style_header
assert "return \"应用网格\"" in style_header
assert "launcher_style_t settings_get_launcher_style(void);" in settings_header
assert 'nvs_set_u8(handle, "launcher_style"' in settings
assert 'nvs_get_u8(handle, "launcher_style"' in settings
assert "s_launcher_style = LAUNCHER_STYLE_FLOW;" in settings
assert "s_launcher_style = static_cast<launcher_style_t>" in settings
assert "launcher_style < LAUNCHER_STYLE_COUNT" in settings
assert "launcher_style_name(s_launcher_style)" in settings
assert "case 11:" in settings
assert '"应用页样式"' in settings

assert "settings_get_launcher_style()" in launcher
assert "s_rendered_style != style" in launcher
assert "build_grid_ui_locked();" in launcher
assert "apply_grid_tile_style(s_selected_index);" in launcher
assert "grid_move_selection(direction *" in launcher
assert "event == BTN_EVT_LONG ? static_cast<int>(kGridColumns) : 1" in launcher
assert "grid_start_entry();" in launcher
assert "app_registry_get_by_index(i)" in launcher
assert "app_registry_get_by_index(s_selected_index)" in launcher
assert "app->desc" not in launcher
assert "lv_timer_create(commit_entry, 150, nullptr);" in launcher
assert "kEntryCardAnimMinLargest" in launcher
assert "draw_grid_icon_event" in launcher
assert "&launcher_font_12" in launcher

assert "LV_OBJ_FLAG_HIDDEN, !selected" in launcher
assert "selected ? LV_OPA_70 : LV_OPA_TRANSP" in launcher
assert "selected ? LV_OPA_COVER : LV_OPA_70" in launcher
assert '"上下逐项 · 长按跨行"' in launcher
assert '"确定打开 · 长按确定返回"' in launcher

launcher_glyphs = glyphs("launcher_font_12")
buddy_glyphs = glyphs("buddy_font_16")
for text in ("上下逐项 · 长按跨行", "确定打开 · 长按确定返回", "GRID 3x3"):
    assert set(text) - {" "} <= launcher_glyphs, f"launcher font missing {text!r}"
for text in ("单页轮播", "应用网格", "应用页样式"):
    assert set(text) - {" "} <= buddy_glyphs, f"buddy font missing {text!r}"

print("Launcher styles: Flow default, 3x3 grid, persistence and key mapping: PASS")
