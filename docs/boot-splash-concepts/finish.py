#!/usr/bin/env python3
"""Crop Quick Look thumbnails and assemble the boot-splash contact sheet."""

from pathlib import Path

from PIL import Image, ImageDraw, ImageFont


ROOT = Path(__file__).resolve().parent
CONCEPTS = [
    ("01-astrolabe", "01 / 星轨四柱", "代码绘制 · 中"),
    ("02-dawn", "02 / 命海日出", "自定义绘制 · 中高"),
    ("03-seal", "03 / 朱砂册页", "代码绘制 · 低"),
    ("04-pixel", "04 / 像素驿站", "代码绘制 · 低"),
]

ZH_FONT = Path("/System/Library/Fonts/Hiragino Sans GB.ttc")
EN_FONT = Path("/System/Library/Fonts/Avenir Next.ttc")


def font(path: Path, size: int, index: int = 0) -> ImageFont.FreeTypeFont:
    return ImageFont.truetype(str(path), size=size, index=index)


for slug, _, _ in CONCEPTS:
    source = Image.open(ROOT / f"{slug}.svg.png").convert("RGB")
    source.crop((0, 0, 480, 640)).save(ROOT / f"{slug}.png", quality=95)

canvas = Image.new("RGB", (1440, 660), "#0D100F")
draw = ImageDraw.Draw(canvas)
title_font = font(EN_FONT, 30, 1)
meta_font = font(EN_FONT, 17, 0)
label_font = font(ZH_FONT, 24, 0)
caption_font = font(ZH_FONT, 17, 0)
footer_font = font(ZH_FONT, 17, 0)

draw.text((40, 27), "PASSPORT OS · BOOT SPLASH DIRECTIONS", fill="#EEE7D6", font=title_font)
draw.text((1400, 34), "240 × 320 · 2× PREVIEW", fill="#8E9993", font=meta_font, anchor="ra")
draw.line((40, 73, 1400, 73), fill="#414743", width=2)

for index, (slug, label, cost) in enumerate(CONCEPTS):
    x = 40 + index * 340
    draw.text((x, 100), label, fill="#EEE7D6", font=label_font)
    preview = Image.open(ROOT / f"{slug}.png").convert("RGB")
    preview = preview.resize((300, 400), Image.Resampling.LANCZOS)
    canvas.paste(preview, (x, 145))
    draw.rectangle((x, 145, x + 300, 545), outline="#414743", width=2)
    draw.text((x, 575), cost, fill="#8E9993", font=caption_font)

draw.line((40, 617, 1400, 617), fill="#414743", width=1)
draw.text(
    (40, 632),
    "命海拾遗 / 四款启动画面概念 · 静态效果稿",
    fill="#B3B2A5",
    font=footer_font,
)
canvas.save(ROOT / "00-overview.png", quality=95)
print("Wrote cropped concept screens and 00-overview.png")
