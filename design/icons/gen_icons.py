#!/usr/bin/env python3
"""Generate WeatherHintIcons.h from the 1-bit PNG icons in this folder.

Each source PNG must be 32x32, black shapes on transparent (or white).
Pixels with alpha >= 128 become black (bit=0); everything else stays white (bit=1).
Output is row-packed, MSB-first, matching GfxRenderer::drawImage convention
where bit=1 = white and bit=0 = black (FreeInkDisplay blitImage).
Missing PNGs generate an all-white placeholder so compilation stays valid.
"""
import os
from PIL import Image

SIZE = 32
ICONS = ["refresh", "menu", "close", "select", "up", "down", "back", "left", "right"]
OUT = os.path.join(os.path.dirname(__file__), "..", "..", "src", "images", "WeatherHintIcons.h")


def to_rows(img):
    """Convert PNG to row-packed bytes. bit=0=black (opaque), bit=1=white (transparent)."""
    px = img.load()
    rows = []
    for y in range(SIZE):
        byte = 0
        out = []
        for x in range(SIZE):
            a = px[x, y][3]
            if a < 128:
                byte |= 1 << (7 - (x % 8))
            if x % 8 == 7:
                out.append(byte)
                byte = 0
        rows.append(out)
    return rows


def placeholder_rows():
    """All-white placeholder for missing PNGs."""
    return [[0xFF] * 4 for _ in range(SIZE)]


def rotate_cw(rows):
    """Rotate 90 degrees clockwise."""
    bits = [[0] * SIZE for _ in range(SIZE)]
    for y in range(SIZE):
        for byte_idx in range(4):
            b = rows[y][byte_idx]
            for bit_idx in range(8):
                x = byte_idx * 8 + bit_idx
                if x < SIZE:
                    bits[y][x] = (b >> (7 - bit_idx)) & 1
    dst_bits = [[0] * SIZE for _ in range(SIZE)]
    for y in range(SIZE):
        for x in range(SIZE):
            dst_bits[y][x] = bits[SIZE - 1 - x][y]
    return _pack(dst_bits)


def rotate_180(rows):
    """Rotate 180 degrees."""
    bits = [[0] * SIZE for _ in range(SIZE)]
    for y in range(SIZE):
        for byte_idx in range(4):
            b = rows[y][byte_idx]
            for bit_idx in range(8):
                x = byte_idx * 8 + bit_idx
                if x < SIZE:
                    bits[y][x] = (b >> (7 - bit_idx)) & 1
    dst_bits = [[0] * SIZE for _ in range(SIZE)]
    for y in range(SIZE):
        for x in range(SIZE):
            dst_bits[y][x] = bits[SIZE - 1 - y][SIZE - 1 - x]
    return _pack(dst_bits)


def rotate_ccw(rows):
    """Rotate 90 degrees counter-clockwise (the drawImage portrait transform
    applies 90° CW to the bitmap, so CCW pre-rotation makes icons display
    exactly as their source PNG)."""
    bits = [[0] * SIZE for _ in range(SIZE)]
    for y in range(SIZE):
        for byte_idx in range(4):
            b = rows[y][byte_idx]
            for bit_idx in range(8):
                x = byte_idx * 8 + bit_idx
                if x < SIZE:
                    bits[y][x] = (b >> (7 - bit_idx)) & 1
    dst_bits = [[0] * SIZE for _ in range(SIZE)]
    for y in range(SIZE):
        for x in range(SIZE):
            dst_bits[y][x] = bits[x][SIZE - 1 - y]
    return _pack(dst_bits)


def _pack(bits):
    dst_rows = []
    for y in range(SIZE):
        out = []
        for byte_idx in range(4):
            byte = 0
            for bit_idx in range(8):
                x = byte_idx * 8 + bit_idx
                if bits[y][x]:
                    byte |= 1 << (7 - bit_idx)
            out.append(byte)
        dst_rows.append(out)
    return dst_rows


def emit_array(name, rows):
    lines = ["static const uint8_t %s[] = {" % name]
    for row in rows:
        lines.append("    " + ", ".join("0x%02X" % b for b in row) + ",")
    lines.append("};")
    return lines


def main():
    blocks = ["#pragma once", "#include <cstdint>", "",
              "// 32x32 1-bit hint-bar icons used by the weather app. Generated",
              "// from design/icons/*.png by design/icons/gen_icons.py — edit the",
              "// PNGs and re-run the script, do not hand-edit this file.",
              "// bit=0 = black (icon), bit=1 = white (background); rows packed MSB-first.",
              "static constexpr int kWeatherHintIconSize = 32;", ""]
    for name in ICONS:
        path = os.path.join(os.path.dirname(__file__), name + ".png")
        if os.path.exists(path):
            img = Image.open(path).convert("RGBA")
            assert img.size == (SIZE, SIZE), "%s must be %dx%d" % (name, SIZE, SIZE)
            upright = to_rows(img)
        else:
            print("WARNING: missing", path, "— using all-white placeholder")
            upright = placeholder_rows()
        cw = rotate_cw(upright)
        rot180 = rotate_180(upright)
        ccw = rotate_ccw(upright)
        cap = name.capitalize()
        blocks += emit_array("WxIcon" + cap, upright)
        blocks.append("")
        blocks += emit_array("WxIcon" + cap + "CW", cw)
        blocks.append("")
        blocks += emit_array("WxIcon" + cap + "180", rot180)
        blocks.append("")
        blocks += emit_array("WxIcon" + cap + "CCW", ccw)
        blocks.append("")
    with open(OUT, "w") as f:
        f.write("\n".join(blocks).rstrip() + "\n")
    print("wrote", os.path.abspath(OUT), "-", sum(len(b) for b in blocks), "lines")


if __name__ == "__main__":
    main()
