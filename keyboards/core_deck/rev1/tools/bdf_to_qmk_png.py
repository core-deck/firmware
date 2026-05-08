#!/usr/bin/env python3
"""Generate QMK Quantum Painter font PNG from BDF bitmap source.

Produces pixel-perfect output (no rasterizer artifacts) for bitmap fonts.

QMK PNG format:
  - Row 0: magenta (255,0,255) delimiter pixel at START of each glyph
  - Rows 1..H: glyph pixels (white=set, black=background)
  - N glyphs = N delimiters, no trailing delimiter

Usage:
    python3 bdf_to_qmk_png.py ter-u14n.bdf -o font.png -u "·↑↓…" --thin-space 4
"""

import argparse
import sys
from pathlib import Path

try:
    from PIL import Image
except ImportError:
    sys.exit("Pillow required: pip install Pillow")


def parse_bdf(path):
    """Parse BDF font file. Returns (glyphs, pixel_size, ascent, descent).

    glyphs: dict of codepoint -> {width, bbx_w, bbx_h, bbx_xoff, bbx_yoff, bitmap}
    """
    glyphs = {}
    pixel_size = 0
    ascent = 0
    descent = 0

    with open(path, "r") as f:
        lines = f.readlines()

    i = 0
    while i < len(lines):
        line = lines[i].strip()

        if line.startswith("PIXEL_SIZE "):
            pixel_size = int(line.split()[1])
        elif line.startswith("FONT_ASCENT "):
            ascent = int(line.split()[1])
        elif line.startswith("FONT_DESCENT "):
            descent = int(line.split()[1])
        elif line.startswith("STARTCHAR"):
            encoding = -1
            dwidth = 0
            bbx_w = bbx_h = bbx_xoff = bbx_yoff = 0
            bitmap = []

            i += 1
            while i < len(lines) and not lines[i].strip().startswith("ENDCHAR"):
                l = lines[i].strip()
                if l.startswith("ENCODING "):
                    encoding = int(l.split()[1])
                elif l.startswith("DWIDTH "):
                    dwidth = int(l.split()[1])
                elif l.startswith("BBX "):
                    parts = l.split()
                    bbx_w, bbx_h = int(parts[1]), int(parts[2])
                    bbx_xoff, bbx_yoff = int(parts[3]), int(parts[4])
                elif l == "BITMAP":
                    i += 1
                    while i < len(lines) and lines[i].strip() != "ENDCHAR":
                        bitmap.append(lines[i].strip())
                        i += 1
                    continue
                i += 1

            if encoding >= 0:
                glyphs[encoding] = {
                    "width": dwidth if dwidth > 0 else bbx_w,
                    "bbx_w": bbx_w,
                    "bbx_h": bbx_h,
                    "bbx_xoff": bbx_xoff,
                    "bbx_yoff": bbx_yoff,
                    "bitmap": bitmap,
                }
        i += 1

    return glyphs, pixel_size, ascent, descent


def render_glyph(glyph, font_height, ascent):
    """Render a BDF glyph into a 2D pixel grid (font_height x glyph width)."""
    width = glyph["width"]
    pixels = [[0] * width for _ in range(font_height)]

    bbx_w = glyph["bbx_w"]
    bbx_yoff = glyph["bbx_yoff"]
    bbx_xoff = glyph["bbx_xoff"]
    bbx_h = glyph["bbx_h"]

    # BDF: bitmap top row starts at (ascent - bbx_yoff - bbx_h) from cell top
    y_start = ascent - bbx_yoff - bbx_h

    for row_idx, hex_str in enumerate(glyph["bitmap"]):
        y = y_start + row_idx
        if y < 0 or y >= font_height:
            continue
        bits = int(hex_str, 16)
        num_bits = len(hex_str) * 4
        for bit_idx in range(bbx_w):
            x = bbx_xoff + bit_idx
            if 0 <= x < width and (bits & (1 << (num_bits - 1 - bit_idx))):
                pixels[y][x] = 1

    return pixels


def generate_png(glyphs, font_height, ascent, codepoints, output, thin_space_width):
    """Build QMK-format PNG with delimiter row + glyph pixels."""
    THIN_SPACE = 0x2009

    # Build ordered glyph list with widths
    entries = []
    for cp in codepoints:
        if cp == THIN_SPACE and thin_space_width is not None:
            entries.append((cp, thin_space_width))
        elif cp in glyphs:
            entries.append((cp, glyphs[cp]["width"]))
        else:
            print(f"  Warning: U+{cp:04X} not in BDF, skipping")

    total_w = sum(1 + w for _, w in entries)  # 1 delimiter + w pixels each
    img_h = font_height + 1  # +1 for delimiter row
    img = Image.new("RGB", (total_w, img_h), (0, 0, 0))

    x = 0
    for cp, w in entries:
        # Magenta delimiter pixel in row 0
        img.putpixel((x, 0), (255, 0, 255))

        # Glyph pixels (skip synthetic thin space — all black)
        if cp != THIN_SPACE or thin_space_width is None:
            if cp in glyphs:
                rows = render_glyph(glyphs[cp], font_height, ascent)
                for ry, row in enumerate(rows):
                    for cx, px in enumerate(row):
                        if px:
                            img.putpixel((x + 1 + cx, ry + 1), (255, 255, 255))

        x += 1 + w

    img.save(output)
    print(f"  Saved {output} ({total_w}x{img_h}, {len(entries)} glyphs)")


def main():
    parser = argparse.ArgumentParser(description="BDF to QMK Quantum Painter font PNG")
    parser.add_argument("bdf", help="Input BDF file")
    parser.add_argument("-o", "--output", required=True, help="Output PNG path")
    parser.add_argument("-u", "--unicode", default="", help="Additional Unicode glyphs to include")
    parser.add_argument("--thin-space", type=int, default=None,
                        help="Add U+2009 THIN SPACE with this pixel width")
    args = parser.parse_args()

    glyphs, pixel_size, ascent, descent = parse_bdf(args.bdf)
    font_height = ascent + descent
    print(f"Parsed {args.bdf}: {len(glyphs)} glyphs, {pixel_size}px, ascent={ascent} descent={descent}")

    # ASCII 0x20..0x7E + requested Unicode + optional thin space
    codepoints = list(range(0x20, 0x7F))
    for ch in args.unicode:
        cp = ord(ch)
        if cp not in codepoints:
            codepoints.append(cp)
    if args.thin_space is not None:
        if 0x2009 not in codepoints:
            codepoints.append(0x2009)
    codepoints.sort()

    generate_png(glyphs, font_height, ascent, codepoints, args.output, args.thin_space)


if __name__ == "__main__":
    main()
