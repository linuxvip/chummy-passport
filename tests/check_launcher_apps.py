"""Every launcher title must fit the Passport Flow card font and geometry."""
from pathlib import Path
import re

root=Path(__file__).resolve().parents[1]
registry=(root/'main/launcher/app_registry.cc').read_text(encoding='utf-8')
array=registry.split('const passport_app_t *s_apps[] = {',1)[1].split('};',1)[0]
registered=re.findall(r'&g_(\w+)_app',array)
launcher_glyphs={chr(int(c,16)) for c in re.findall(r'/\* U\+([0-9A-Fa-f]{4,6})',(root/'main/fonts/buddy_font_16.c').read_text(encoding='utf-8'))}
title_glyphs={chr(int(c,16)) for c in re.findall(r'/\* U\+([0-9A-Fa-f]{4,6})',(root/'main/fonts/launcher_title_20.c').read_text(encoding='utf-8'))}
apps={}
for path in (root/'main').rglob('*.cc'):
    source=path.read_text(encoding='utf-8')
    for match in re.finditer(r'const passport_app_t g_(\w+)_app\s*=\s*\{([\s\S]+?)\};',source):
        name=re.search(r'\.name\s*=\s*"([^"]+)"',match[2])
        if name:apps[match[1]]=name[1]
for key in registered:
    assert key in apps,key+' has no app title'
    missing=set(apps[key])-launcher_glyphs
    assert not missing,(apps[key],'missing 12px glyphs',''.join(sorted(missing)))
    missing=set(apps[key])-title_glyphs
    assert not missing,(apps[key],'missing 20px title glyphs',''.join(sorted(missing)))
launcher=(root/'main/launcher/launcher.cc').read_text(encoding='utf-8')
assert 'constexpr int kCardW = 240;' in launcher
assert 'constexpr int kCardH = 295;' in launcher
assert 'constexpr int kCardStep = 295;' in launcher
assert 'constexpr int kIconSize = 64;' in launcher
assert 'constexpr int kGlyphSpan = 54;' in launcher
assert 'IconPalette icon_palette(passport_app_id_t id)' in launcher
assert 'icon_tile_color(tile.id)' in launcher
assert 'icon_ink_color(tile.id)' in launcher
assert 'launcher_title_20' in launcher
assert 'tile.description' not in launcher
assert '"选择应用"' in launcher
assert '"PASS"' not in launcher
assert 'ui_pixel_footer' not in launcher
assert 'lv_obj_set_size(tile.accent_bar, 3, 42);' in launcher
assert 's_ok_button' in launcher
assert 'set_key_feedback(btn, true);' in launcher
assert 'lv_obj_set_pos(s_up_arrow, 210, 46);' in launcher
assert 'lv_obj_set_pos(s_down_arrow, 210, 150);' in launcher
assert 'lv_obj_set_pos(s_ok_button, 201, 269);' in launcher
assert 'lv_obj_set_size(s_rail[i], selected ? 9 : 6, selected ? 24 : 6);' in launcher
assert 'lv_obj_set_style_border_width(tile.ring, 2, 0);' in launcher
assert 'lv_obj_set_style_border_opa(tile.ring, 220, 0);' in launcher
assert 's_scroll_track' not in launcher
assert 's_scroll_thumb' not in launcher
assert 'lv_anim_path_overshoot' in launcher
assert 'move_selection(direction * (event == BTN_EVT_LONG ? 4 : 1));' in launcher
assert 'lv_timer_create(commit_entry, 230' in launcher
assert 'kMaxTiles = 10' in launcher
print(f'Launcher: {len(registered)} titles fit Passport Flow pages, wheel motion and entry animation: PASS')
