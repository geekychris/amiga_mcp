# Convert/Downres Image for Amiga

Convert a modern image to Amiga-friendly format: reduce resolution, limit color palette, and optionally generate C source data or IFF/ILBM files.

## Arguments
- $ARGUMENTS: Path to the source image, plus optional instructions (e.g., "/path/to/photo.png 320x256 16 colors", "logo.jpg fit in 160x100 8 colors for a title screen"). If empty, ask for the image path and target specs.

## Default Amiga Specs
- **Lo-res**: 320x256 (PAL) or 320x200 (NTSC)
- **Hi-res**: 640x256 (PAL) or 640x200 (NTSC)
- **Color depths**: 2 (2 colors), 4 (4 colors), 8 (8 colors), 16 (16 colors), 32 (32 colors), 64 (EHB), 4096 (HAM6)
- **Palette**: OCS/ECS uses 12-bit color (4 bits per channel, 0-15 per R/G/B)

## Steps

### 1. Create and run a Python conversion script

```python
#!/usr/bin/env python3
"""Convert image to Amiga-friendly format."""
import sys
from PIL import Image
import struct

def to_amiga_rgb4(r, g, b):
    """Convert 8-bit RGB to Amiga 12-bit RGB4 (0x0RGB)."""
    return ((r >> 4) << 8) | ((g >> 4) << 4) | (b >> 4)

def convert_for_amiga(input_path, output_path, width=320, height=256,
                       max_colors=16, dither=True):
    img = Image.open(input_path).convert('RGB')

    # Resize maintaining aspect ratio, then crop/pad to exact dimensions
    img.thumbnail((width, height), Image.LANCZOS)

    # Create canvas at exact target size (black background)
    canvas = Image.new('RGB', (width, height), (0, 0, 0))
    # Center the image
    x_off = (width - img.width) // 2
    y_off = (height - img.height) // 2
    canvas.paste(img, (x_off, y_off))
    img = canvas

    # Quantize to limited palette
    if dither:
        img_q = img.quantize(colors=max_colors, method=Image.Quantize.MEDIANCUT,
                              dither=Image.Dither.FLOYDSTEINBERG)
    else:
        img_q = img.quantize(colors=max_colors, method=Image.Quantize.MEDIANCUT,
                              dither=Image.Dither.NONE)

    # Snap palette to 12-bit Amiga colors (4 bits per channel)
    palette = img_q.getpalette()[:max_colors * 3]
    amiga_palette = []
    snapped_palette = []
    for i in range(0, len(palette), 3):
        r, g, b = palette[i], palette[i+1], palette[i+2]
        # Snap to 4-bit per channel
        r4 = (r >> 4); g4 = (g >> 4); b4 = (b >> 4)
        snapped_palette.extend([r4 * 17, g4 * 17, b4 * 17])
        amiga_palette.append(to_amiga_rgb4(r, g, b))

    # Re-quantize with snapped palette for accuracy
    snap_img = Image.new('P', (1, 1))
    snap_img.putpalette(snapped_palette + [0] * (768 - len(snapped_palette)))
    img_final = img.quantize(palette=snap_img, dither=1 if dither else 0)

    # Save preview PNG
    img_final.save(output_path)

    return img_final, amiga_palette

def generate_c_palette(amiga_palette, var_name="palette"):
    """Generate C source for Amiga palette array."""
    lines = [f"static UWORD {var_name}[{len(amiga_palette)}] = {{"]
    for i, col in enumerate(amiga_palette):
        comma = "," if i < len(amiga_palette) - 1 else ""
        lines.append(f"    0x{col:03X}{comma}  /* {i}: R={col>>8&0xF} G={col>>4&0xF} B={col&0xF} */")
    lines.append("};")
    return "\n".join(lines)

def generate_c_bitmap(img, var_name="image_data"):
    """Generate C source for planar bitmap data (Amiga bitplane format)."""
    width, height = img.size
    pixels = list(img.getdata())
    num_colors = max(pixels) + 1
    depth = num_colors.bit_length()

    # Convert chunky to planar
    row_words = (width + 15) // 16
    row_bytes = row_words * 2

    planes = []
    for plane in range(depth):
        plane_data = bytearray()
        for y in range(height):
            for byt in range(row_bytes):
                byte_val = 0
                for bit in range(8):
                    x = byt * 8 + bit
                    if x < width:
                        px = pixels[y * width + x]
                        if px & (1 << plane):
                            byte_val |= (0x80 >> bit)
                plane_data.append(byte_val)
        planes.append(plane_data)

    # Generate C source
    lines = [f"/* {width}x{height}, {depth} bitplanes, {row_bytes} bytes/row */"]
    lines.append(f"#define {var_name.upper()}_WIDTH {width}")
    lines.append(f"#define {var_name.upper()}_HEIGHT {height}")
    lines.append(f"#define {var_name.upper()}_DEPTH {depth}")
    lines.append(f"#define {var_name.upper()}_ROW_BYTES {row_bytes}")
    lines.append(f"")

    for p, plane_data in enumerate(planes):
        lines.append(f"static const UBYTE {var_name}_plane{p}[] = {{")
        for i in range(0, len(plane_data), 16):
            chunk = plane_data[i:i+16]
            hex_vals = ",".join(f"0x{b:02X}" for b in chunk)
            lines.append(f"    {hex_vals},")
        lines.append("};")
        lines.append("")

    # Array of plane pointers
    lines.append(f"static const UBYTE *{var_name}_planes[{depth}] = {{")
    for p in range(depth):
        lines.append(f"    {var_name}_plane{p},")
    lines.append("};")

    return "\n".join(lines)

if __name__ == "__main__":
    input_path = sys.argv[1]
    output_path = sys.argv[2] if len(sys.argv) > 2 else "output_amiga.png"
    width = int(sys.argv[3]) if len(sys.argv) > 3 else 320
    height = int(sys.argv[4]) if len(sys.argv) > 4 else 256
    colors = int(sys.argv[5]) if len(sys.argv) > 5 else 16

    img, pal = convert_for_amiga(input_path, output_path, width, height, colors)

    print(f"Converted: {width}x{height}, {colors} colors")
    print(f"Preview saved to: {output_path}")
    print()
    print("--- Amiga Palette (C source) ---")
    print(generate_c_palette(pal))
    print()
    print(f"--- Bitmap data: use generate_c_bitmap() for planar data ---")
```

### 2. Usage
```bash
# Install Pillow if needed
pip install Pillow

# Convert image
python3 convert_amiga.py input.png output_preview.png 320 256 16
```

### 3. Output options
- **Preview PNG**: Quantized/dithered image at target resolution for visual check
- **C palette array**: `UWORD palette[N]` in OCS 12-bit format, paste into your game
- **C bitmap data**: Planar bitplane arrays ready to blit into Amiga screen memory
- **Raw planar binary**: For loading at runtime via `Open()`/`Read()`

### 4. Tips
- Use **Floyd-Steinberg dithering** for photos (looks great on Amiga)
- Use **no dithering** for pixel art, logos, and UI elements
- **16 colors** (4 bitplanes) is the sweet spot for games
- **32 colors** gives better gradients but costs more chip RAM
- For **title screens**, consider 32 or even 64 colors (EHB mode)
- **HAM6** mode can display all 4096 colors but has fringing artifacts — best for static images
- Background color (index 0) is typically black or the dominant dark color
- Keep total bitmap size reasonable: 320x256x4 planes = 40KB of chip RAM

### 5. Deploy
Copy the preview PNG or raw data to the project, embed in source or load at runtime.
