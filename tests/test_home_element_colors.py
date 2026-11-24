"""Check that every stem and branch has one five-element color mapping."""

from pathlib import Path


root = Path(__file__).resolve().parents[1]
source = (root / "main/apps/home/app_home.cc").read_text(encoding="utf-8")
stems = "甲乙丙丁戊己庚辛壬癸"
branches = "子丑寅卯辰巳午未申酉戌亥"
groups = ["甲乙寅卯", "丙丁巳午", "戊己辰戌丑未", "庚辛申酉", "壬癸亥子"]
covered = "".join(groups)
assert sorted(covered) == sorted(stems + branches)
assert all(group in source for group in groups)
assert "five_element_color(five_element_for_glyph(stem))" in source
assert "five_element_color(five_element_for_glyph(branch))" in source
profile = source[source.index("void create_profile_ui()"):source.index("void create_ui()")]
assert "lv_obj_set_size(s_pillar_labels[1][col], 26, 26);" in profile
assert "lv_obj_set_pos(s_pillar_labels[2][col], kPillarX[col] + 26, 70);" in profile
assert "lv_obj_add_flag(s_pillar_labels[2][col], LV_OBJ_FLAG_HIDDEN);" not in profile
assert "constexpr int kPillarX[4] = {12, 64, 116, 168};" in profile

print("Home five-element colors: profile stems and branches colored separately: PASS")
