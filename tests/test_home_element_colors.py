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

print("Home five-element colors: all 10 stems and 12 branches mapped: PASS")
