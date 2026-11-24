"""Every launcher title must fit the compact app-grid font and grid geometry."""
from pathlib import Path
import re

root=Path(__file__).resolve().parents[1]
registry=(root/'main/launcher/app_registry.cc').read_text(encoding='utf-8')
array=registry.split('const passport_app_t *s_apps[] = {',1)[1].split('};',1)[0]
registered=re.findall(r'&g_(\w+)_app',array)
launcher_glyphs={chr(int(c,16)) for c in re.findall(r'/\* U\+([0-9A-Fa-f]{4,6})',(root/'main/fonts/launcher_font_12.c').read_text(encoding='utf-8'))}
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
launcher=(root/'main/launcher/launcher.cc').read_text(encoding='utf-8')
assert 'constexpr int kTileW = 70;' in launcher
assert 'constexpr int kTileH = 54;' in launcher
assert 'if (index == 9) return 10;' in launcher
assert 'BTN_EVT_LONG' in launcher and 'row_target_index(direction)' in launcher
assert '"上下逐项 · 长按跨行"' in launcher
print(f'Launcher: {len(registered)} titles fit the 12px 3x4 app grid and key navigation: PASS')
