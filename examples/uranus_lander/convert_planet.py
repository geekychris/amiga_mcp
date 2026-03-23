#!/usr/bin/env python3
"""Convert uranus.png to Amiga planar bitmap C header."""
from PIL import Image
import struct, sys

# Target size
W, H = 80, 72

# Game palette (12-bit Amiga RGB → 8-bit RGB)
# Updated with brown tones for terrain + planet
PALETTE_12BIT = [
    0x002,  #  0: deep space
    0xEEE,  #  1: white
    0x322,  #  2: dark brown
    0x644,  #  3: medium brown
    0x876,  #  4: light tan
    0x226,  #  5: dark blue
    0xEC0,  #  6: yellow
    0x0C0,  #  7: green
    0xD00,  #  8: red
    0xF80,  #  9: orange
    0x0CE,  # 10: cyan
    0x08A,  # 11: dark cyan
    0xD0D,  # 12: magenta
    0xFE0,  # 13: bright yellow
    0x533,  # 14: warm brown
    0xFFF,  # 15: bright white
]

def amiga12_to_rgb(c):
    r = ((c >> 8) & 0xF) * 17
    g = ((c >> 4) & 0xF) * 17
    b = (c & 0xF) * 17
    return (r, g, b)

PALETTE_RGB = [amiga12_to_rgb(c) for c in PALETTE_12BIT]

def closest_color(r, g, b):
    """Find closest palette index by Euclidean distance."""
    best_i, best_d = 0, 999999
    for i, (pr, pg, pb) in enumerate(PALETTE_RGB):
        d = (r - pr) ** 2 + (g - pg) ** 2 + (b - pb) ** 2
        if d < best_d:
            best_d = d
            best_i = i
    return best_i

def main():
    img = Image.open("/Users/chris/uranus.png").convert("RGB")

    # Resize maintaining aspect, fit into WxH
    img.thumbnail((W, H), Image.LANCZOS)

    # Center on black canvas
    canvas = Image.new("RGB", (W, H), (0, 0, 0))
    x_off = (W - img.width) // 2
    y_off = (H - img.height) // 2
    canvas.paste(img, (x_off, y_off))

    # Build a PIL palette image for dithering
    pal_flat = []
    for r, g, b in PALETTE_RGB:
        pal_flat.extend([r, g, b])
    pal_flat.extend([0] * (768 - len(pal_flat)))

    pal_img = Image.new("P", (1, 1))
    pal_img.putpalette(pal_flat)

    # Quantize with dithering to our palette
    quantized = canvas.quantize(palette=pal_img, dither=Image.Dither.FLOYDSTEINBERG)
    pixels = list(quantized.getdata())

    # Make near-black pixels transparent (color 0)
    for i in range(len(pixels)):
        r, g, b = canvas.getpixel((i % W, i // W))
        if r < 20 and g < 20 and b < 20:
            pixels[i] = 0

    # Convert chunky to planar
    depth = 4
    row_bytes = W // 8  # W is multiple of 8

    planes = []
    for plane in range(depth):
        plane_data = bytearray()
        for y in range(H):
            for byt in range(row_bytes):
                byte_val = 0
                for bit in range(8):
                    x = byt * 8 + bit
                    px = pixels[y * W + x]
                    if px & (1 << plane):
                        byte_val |= (0x80 >> bit)
                plane_data.append(byte_val)
        planes.append(plane_data)

    # Generate C header
    lines = []
    lines.append("/* Auto-generated from uranus.png - DO NOT EDIT */")
    lines.append(f"#ifndef PLANET_GFX_H")
    lines.append(f"#define PLANET_GFX_H")
    lines.append(f"")
    lines.append(f"#define PLANET_W {W}")
    lines.append(f"#define PLANET_H {H}")
    lines.append(f"#define PLANET_DEPTH {depth}")
    lines.append(f"#define PLANET_ROW_BYTES {row_bytes}")
    lines.append(f"")

    for p, data in enumerate(planes):
        lines.append(f"static const UBYTE planet_plane{p}[{len(data)}] = {{")
        for i in range(0, len(data), 16):
            chunk = data[i:i+16]
            hex_vals = ",".join(f"0x{b:02X}" for b in chunk)
            lines.append(f"    {hex_vals},")
        lines.append("};")
        lines.append("")

    lines.append(f"static const UBYTE *planet_planes[{depth}] = {{")
    for p in range(depth):
        lines.append(f"    planet_plane{p},")
    lines.append("};")
    lines.append("")
    lines.append("#endif /* PLANET_GFX_H */")

    out_path = "/Users/chris/code/claude_world/amiga_mcp/examples/uranus_lander/planet_gfx.h"
    with open(out_path, "w") as f:
        f.write("\n".join(lines) + "\n")

    print(f"Generated {out_path}")
    print(f"  Size: {W}x{H}, {depth} planes, {sum(len(p) for p in planes)} bytes total")

    # Also save a preview
    preview = Image.new("RGB", (W, H))
    for y in range(H):
        for x in range(W):
            px = pixels[y * W + x] & 0xF
            preview.putpixel((x, y), PALETTE_RGB[px])
    preview.save("/Users/chris/code/claude_world/amiga_mcp/examples/uranus_lander/planet_preview.png")
    print("  Preview: planet_preview.png")

if __name__ == "__main__":
    main()
