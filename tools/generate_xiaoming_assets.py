#!/usr/bin/env python3
"""Build the Xiaoming RGB565A8 portrait frames from the CC-BY source art.

The source PNGs are downloaded separately from OpenGameArt; their hashes are
checked here so a changed or substituted image cannot silently enter firmware.
This tool uses ffmpeg only for decoding, alpha-safe scaling and preview output.
"""

from __future__ import annotations

import argparse
import colorsys
import hashlib
import struct
import subprocess
from pathlib import Path


FRAME_W = 144
FRAME_H = 144
MINI_W = 26
MINI_H = 20
SOURCE_W = 1401
SOURCE_H = 2150

SOURCES = {
    "calm": ("qboy-05_1.png", "5080ff78c6560ff9355b1c1f9c00e61beeceeb2ce5fd913c223e186c4675dfae"),
    "happy": ("qboy-02_3.png", "4a6e66b4168c89ad92ec3affa207ca414b06ea7ff0a52fc602acfad663a7273c"),
    "speak": ("qboy-01.png", "8c3479cff45f1e97a635b712f2fc10765a4994c2f510427a8a8b534901d151ed"),
    "alert": ("qboy-03_1.png", "b957edc6db65f61322ac5f7068da164c0f918c043b0693019b4c859aa7d2d1ba"),
    "confident": ("qboy-04_1.png", "4edcb25dfe527054a2f08543786145978b1d6e2647f4094d9b3127661adf44e9"),
}


def run_ffmpeg_decode(path: Path) -> bytes:
    return subprocess.run(
        [
            "ffmpeg", "-v", "error", "-i", str(path), "-frames:v", "1",
            "-f", "rawvideo", "-pix_fmt", "rgba", "-",
        ],
        check=True,
        capture_output=True,
    ).stdout


def scale_rgba(raw: bytes, width: int, height: int, vf: str) -> bytes:
    return subprocess.run(
        [
            "ffmpeg", "-v", "error",
            "-f", "rawvideo", "-pix_fmt", "rgba",
            "-s", f"{width}x{height}", "-i", "-",
            "-vf", vf, "-frames:v", "1",
            "-f", "rawvideo", "-pix_fmt", "rgba", "-",
        ],
        input=raw,
        check=True,
        capture_output=True,
    ).stdout


def save_png(raw: bytes, width: int, height: int, path: Path) -> None:
    subprocess.run(
        [
            "ffmpeg", "-y", "-v", "error",
            "-f", "rawvideo", "-pix_fmt", "rgba",
            "-s", f"{width}x{height}", "-i", "-",
            "-frames:v", "1", str(path),
        ],
        input=raw,
        check=True,
    )


def recolor_hair(raw: bytes) -> bytes:
    """Move the warm hair palette to deep sea-teal without touching skin.

    The source has deliberately flat palette colors, so a hue/lightness mapping
    keyed on the large red-blue separation of the golden hair is stable.
    """
    out = bytearray(raw)
    pixels = len(raw) // 4
    for pixel in range(pixels):
        offset = pixel * 4
        r, g, b, a = out[offset:offset + 4]
        if a < 8:
            continue
        y = pixel // SOURCE_W
        if y >= 1120 or r <= 100 or r - b <= 55 or g - b <= 28:
            continue
        hue, lightness, saturation = colorsys.rgb_to_hls(r / 255, g / 255, b / 255)
        del hue
        hue = 192 / 360
        saturation = min(0.64, saturation * 0.75 + 0.18)
        lightness = max(0.09, min(0.46, lightness * 0.48))
        rr, gg, bb = colorsys.hls_to_rgb(hue, lightness, saturation)
        out[offset:offset + 3] = bytes((round(rr * 255), round(gg * 255), round(bb * 255)))
    return bytes(out)


def load_frame(path: Path, expected_sha256: str) -> bytes:
    digest = hashlib.sha256(path.read_bytes()).hexdigest()
    if digest != expected_sha256:
        raise SystemExit(f"source checksum mismatch for {path}: {digest}")
    raw = recolor_hair(run_ffmpeg_decode(path))
    if len(raw) != SOURCE_W * SOURCE_H * 4:
        raise SystemExit(f"unexpected decoded size for {path}: {len(raw)}")
    return scale_rgba(raw, SOURCE_W, SOURCE_H, f"crop=1401:1401:0:0,scale={FRAME_W}:{FRAME_H}:flags=lanczos")


def blend_region(base: bytearray, overlay: bytes, x0: int, y0: int, x1: int, y1: int) -> None:
    for y in range(y0, y1):
        for x in range(x0, x1):
            offset = (y * FRAME_W + x) * 4
            src = overlay[offset:offset + 4]
            if src[3]:
                base[offset:offset + 4] = src


def rgb565a8(raw: bytes, width: int, height: int) -> bytes:
    colors = bytearray(width * height * 2)
    alpha = bytearray(width * height)
    for index, offset in enumerate(range(0, len(raw), 4)):
        r, g, b, a = raw[offset:offset + 4]
        value = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
        struct.pack_into("<H", colors, index * 2, value)
        alpha[index] = a
    return bytes(colors + alpha)


def c_bytes(name: str, data: bytes) -> str:
    rows = []
    for offset in range(0, len(data), 16):
        chunk = data[offset:offset + 16]
        rows.append("    " + ", ".join(f"0x{value:02x}" for value in chunk) + ",")
    return f"alignas(4) static const uint8_t {name}[] = {{\n" + "\n".join(rows) + "\n};\n"


def write_source(frames: dict[str, bytes], mini: bytes, output: Path) -> None:
    compiled = "\n".join(c_bytes(f"k{name.title()}", data) for name, data in frames.items())
    mapping = ["Calm", "Blink", "Happy", "Speaking", "Alert", "Confident"]
    data_cases = "\n".join(
        f"        case PortraitFrame::{enum}: return &k{enum}Image;"
        for enum in mapping
    )
    text = f"""// Generated by tools/generate_xiaoming_assets.py. Do not edit by hand.
// Source artwork: "Q-style cartoon boy" by Blue & AVALANCHE Game Studio,
// OpenGameArt, CC-BY 3.0. Modified crop, scale, hair color and expression mix.
#include "apps/xiaozhi/xiaoming_assets.h"

#include <cstddef>

namespace xiaozhi::face::assets {{
namespace {{

constexpr uint16_t kW = {FRAME_W};
constexpr uint16_t kH = {FRAME_H};
constexpr uint32_t kFrameBytes = {FRAME_W * FRAME_H * 3};

{compiled}

alignas(4) static const uint8_t kMini[] = {{
{chr(10).join("    " + ", ".join(f"0x{value:02x}" for value in mini[o:o + 16]) + "," for o in range(0, len(mini), 16))}
}};

lv_image_dsc_t image(const uint8_t *data, uint16_t width, uint16_t height, uint32_t size) {{
    lv_image_dsc_t result{{}};
    result.header.magic = LV_IMAGE_HEADER_MAGIC;
    result.header.cf = LV_COLOR_FORMAT_RGB565A8;
    result.header.flags = 0;
    result.header.w = width;
    result.header.h = height;
    result.header.stride = width * 2;
    result.data_size = size;
    result.data = data;
    return result;
}}

}}  // namespace

const lv_image_dsc_t *portrait(PortraitFrame frame) {{
    static const lv_image_dsc_t kCalmImage = image(kCalm, kW, kH, kFrameBytes);
    static const lv_image_dsc_t kBlinkImage = image(kBlink, kW, kH, kFrameBytes);
    static const lv_image_dsc_t kHappyImage = image(kHappy, kW, kH, kFrameBytes);
    static const lv_image_dsc_t kSpeakingImage = image(kSpeaking, kW, kH, kFrameBytes);
    static const lv_image_dsc_t kAlertImage = image(kAlert, kW, kH, kFrameBytes);
    static const lv_image_dsc_t kConfidentImage = image(kConfident, kW, kH, kFrameBytes);
    switch (frame) {{
{data_cases}
    default: return &kCalmImage;
    }}
}}

const lv_image_dsc_t *mini_portrait() {{
    static const lv_image_dsc_t result =
        image(kMini, {MINI_W}, {MINI_H}, {MINI_W * MINI_H * 3});
    return &result;
}}

}}  // namespace xiaozhi::face::assets
"""
    output.write_text(text, encoding="utf-8")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source-dir", type=Path, default=Path("output/design-references"))
    parser.add_argument("--preview-dir", type=Path, default=Path("output/xiaoming-b"))
    parser.add_argument(
        "--output",
        type=Path,
        default=Path("main/apps/xiaozhi/xiaoming_assets.cc"),
    )
    args = parser.parse_args()
    args.preview_dir.mkdir(parents=True, exist_ok=True)
    frames = {
        name: load_frame(args.source_dir / filename, digest)
        for name, (filename, digest) in SOURCES.items()
    }

    calm = bytearray(frames["calm"])
    blink = bytearray(calm)
    blend_region(blink, frames["happy"], 18, 38, 126, 91)
    speaking = bytearray(calm)
    blend_region(speaking, frames["speak"], 48, 82, 98, 118)
    ordered = {
        "calm": bytes(calm),
        "blink": bytes(blink),
        "happy": frames["happy"],
        "speaking": bytes(speaking),
        "alert": frames["alert"],
        "confident": frames["confident"],
    }
    for name, raw in ordered.items():
        save_png(raw, FRAME_W, FRAME_H, args.preview_dir / f"xiaoming-{name}.png")

    mini = scale_rgba(
        frames["calm"], FRAME_W, FRAME_H,
        f"crop=144:130:0:0,scale={MINI_W}:{MINI_H}:flags=lanczos",
    )
    save_png(mini, MINI_W, MINI_H, args.preview_dir / "xiaoming-mini.png")

    encoded = {name: rgb565a8(raw, FRAME_W, FRAME_H) for name, raw in ordered.items()}
    encoded_mini = rgb565a8(mini, MINI_W, MINI_H)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    write_source(encoded, encoded_mini, args.output)
    print(f"generated {args.output} ({sum(map(len, encoded.values())) + len(encoded_mini)} bytes)")


if __name__ == "__main__":
    main()
