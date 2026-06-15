"""Amiga screenshot capture - planar to chunky conversion and PNG rendering."""

from __future__ import annotations

import base64
import io
import logging
import struct
import tempfile
from pathlib import Path
from typing import Any

logger = logging.getLogger(__name__)


def planar_to_chunky(
    width: int,
    height: int,
    depth: int,
    planes_data: list[list[bytes]],
) -> list[int]:
    """Convert Amiga planar bitmap data to chunky pixel indices.

    Args:
        width: Image width in pixels.
        height: Image height in pixels.
        depth: Number of bitplanes.
        planes_data: planes_data[row][plane] = bytes for that bitplane row.

    Returns:
        List of pixel color indices, row-major order.
    """
    bytes_per_row = ((width + 15) // 16) * 2
    pixels = []

    for y in range(height):
        for x in range(width):
            byte_idx = x // 8
            bit_idx = 7 - (x % 8)
            color = 0

            for p in range(depth):
                if byte_idx < len(planes_data[y][p]):
                    if planes_data[y][p][byte_idx] & (1 << bit_idx):
                        color |= (1 << p)

            pixels.append(color)

    return pixels


def rle_decode(data: bytes, bpp: int) -> bytes:
    """Decode one TGA/PackBits-style RLE row (raw bytes) to raw pixel bytes.

    Packet header byte:
      high bit SET   -> run:     (hdr & 0x7f)+1 copies of the next 1 pixel.
      high bit CLEAR -> literal: (hdr & 0x7f)+1 pixels follow verbatim.
    """
    out = bytearray()
    i = 0
    n = len(data)
    while i < n:
        hdr = data[i]
        i += 1
        count = (hdr & 0x7F) + 1
        if hdr & 0x80:
            pix = data[i:i + bpp]
            i += bpp
            out += pix * count
        else:
            nbytes = count * bpp
            out += data[i:i + nbytes]
            i += nbytes
    return bytes(out)


def parse_palette(palette_str: str) -> list[tuple[int, int, int]]:
    """Parse palette string 'rgb,rgb,...' where each is 3 hex digits (OCS 4-bit).

    Returns list of (r8, g8, b8) tuples scaled to 0-255.
    """
    colors = []
    for entry in palette_str.split(","):
        entry = entry.strip()
        if len(entry) >= 6:
            # 8-bit per channel (RRGGBB) from GetRGB32
            colors.append((int(entry[0:2], 16), int(entry[2:4], 16), int(entry[4:6], 16)))
        elif len(entry) >= 3:
            # Legacy 4-bit per channel (RGB) from GetRGB4: scale 0-15 -> 0-255
            r4 = int(entry[0], 16)
            g4 = int(entry[1], 16)
            b4 = int(entry[2], 16)
            colors.append((r4 * 17, g4 * 17, b4 * 17))
        else:
            colors.append((0, 0, 0))
    return colors


def render_png(
    width: int,
    height: int,
    pixel_indices: list[int],
    palette: list[tuple[int, int, int]],
) -> bytes:
    """Render pixel indices + palette into a PNG file in memory.

    Uses raw PNG encoding (no Pillow dependency).
    """
    try:
        from PIL import Image
        img = Image.new("RGB", (width, height))
        rgb_data = []
        for idx in pixel_indices:
            if idx < len(palette):
                rgb_data.append(palette[idx])
            else:
                rgb_data.append((0, 0, 0))
        img.putdata(rgb_data)
        buf = io.BytesIO()
        img.save(buf, format="PNG")
        return buf.getvalue()
    except ImportError:
        # Fallback: write raw PPM (universally readable)
        return _render_ppm(width, height, pixel_indices, palette)


def _render_ppm(
    width: int,
    height: int,
    pixel_indices: list[int],
    palette: list[tuple[int, int, int]],
) -> bytes:
    """Render as PPM format (fallback if PIL not available)."""
    header = f"P6\n{width} {height}\n255\n".encode("ascii")
    data = bytearray()
    for idx in pixel_indices:
        if idx < len(palette):
            r, g, b = palette[idx]
        else:
            r, g, b = 0, 0, 0
        data.extend((r, g, b))
    return header + bytes(data)


def _render_rgb_png(width, height, rgb_tuples) -> bytes:
    """Render a list of (r,g,b) tuples to PNG (PIL) or PPM (fallback)."""
    try:
        from PIL import Image
        img = Image.new("RGB", (width, height))
        img.putdata(rgb_tuples)
        buf = io.BytesIO()
        img.save(buf, format="PNG")
        return buf.getvalue()
    except ImportError:
        header = f"P6\n{width} {height}\n255\n".encode("ascii")
        data = bytearray()
        for (r, g, b) in rgb_tuples:
            data.extend((r, g, b))
        return header + bytes(data)


def _write_image(png_data: bytes, save_path: str | None) -> str:
    """Write PNG/PPM bytes to save_path (or a temp file) and return the path."""
    ext = ".png" if png_data[:4] == b"\x89PNG" else ".ppm"
    if save_path:
        if not save_path.endswith(ext):
            save_path = save_path.rsplit(".", 1)[0] + ext
        with open(save_path, "wb") as f:
            f.write(png_data)
        return save_path
    tmp = tempfile.NamedTemporaryFile(prefix="amiga_screenshot_", suffix=ext, delete=False)
    tmp.write(png_data)
    tmp.close()
    return tmp.name


def save_screenshot(
    scrinfo: dict[str, Any],
    scrdata_lines: list[dict[str, Any]],
    save_path: str | None = None,
    rgb_lines: list[dict[str, Any]] | None = None,
    rle_lines: list[dict[str, Any]] | None = None,
) -> str:
    """Orchestrate screenshot conversion and save to file.

    Args:
        scrinfo: Parsed SCRINFO message with width, height, depth, palette.
        scrdata_lines: List of parsed SCRDATA messages.
        save_path: Optional explicit path to save to. If None, uses temp file.
        rgb_lines: List of parsed SCRRGB messages (true-colour, 3 bytes/pixel).
        rle_lines: List of parsed SCRRLE messages (RLE-compressed rows). Decoded
            here into the equivalent true-colour or chunky rows; pixel size is
            3 bytes when depth==24, else 1 byte (chunky pen indices).

    Returns:
        Path to the saved PNG/PPM file.
    """
    width = scrinfo["width"]
    height = scrinfo["height"]
    depth = scrinfo["depth"]

    # Expand RLE rows into the raw shapes the renderers below already expect.
    # The SCRRLE payload is base64(RLE(pixels)); decode base64 then RLE.
    if rle_lines:
        if depth > 8:
            rgb_lines = list(rgb_lines or [])
            rgb_lines += [
                {"row": r["row"],
                 "hexData": rle_decode(base64.b64decode(r["hexData"]), 3).hex()}
                for r in rle_lines
            ]
        else:
            scrdata_lines = list(scrdata_lines or [])
            scrdata_lines += [
                {"row": r["row"], "plane": 255,
                 "hexData": rle_decode(base64.b64decode(r["hexData"]), 1).hex()}
                for r in rle_lines
            ]

    # True-colour (RTG/Picasso96 >8bpp) path: SCRRGB rows carry raw RGB bytes.
    if rgb_lines:
        rows = {sd["row"]: bytes.fromhex(sd["hexData"]) for sd in rgb_lines}
        pixel_indices = []  # reused as flat RGB tuples
        for y in range(height):
            row = rows.get(y, b"")
            for x in range(width):
                i = x * 3
                if i + 2 < len(row):
                    pixel_indices.append((row[i], row[i + 1], row[i + 2]))
                else:
                    pixel_indices.append((0, 0, 0))
        png_data = _render_rgb_png(width, height, pixel_indices)
        return _write_image(png_data, save_path)

    palette = parse_palette(scrinfo["palette"])

    # Chunky (RTG / Picasso96) path: rows tagged with plane==255 carry one
    # pen-index byte per pixel; decode directly instead of planar deinterleaving.
    chunky_rows = {
        sd["row"]: bytes.fromhex(sd["hexData"])
        for sd in scrdata_lines
        if sd.get("plane") == 255
    }
    if chunky_rows:
        pixel_indices = []
        for y in range(height):
            row = chunky_rows.get(y, b"")
            for x in range(width):
                pixel_indices.append(row[x] if x < len(row) else 0)
    else:
        # Planar path: organize plane data planes_data[row][plane] = bytes
        planes_data: list[list[bytes]] = []
        for y in range(height):
            row_planes: list[bytes] = []
            for p in range(depth):
                row_planes.append(b"\x00" * ((width + 15) // 16 * 2))
            planes_data.append(row_planes)

        for sd in scrdata_lines:
            row = sd["row"]
            plane = sd["plane"]
            hex_data = sd["hexData"]
            if row < height and plane < depth:
                planes_data[row][plane] = bytes.fromhex(hex_data)

        pixel_indices = planar_to_chunky(width, height, depth, planes_data)

    png_data = render_png(width, height, pixel_indices, palette)

    if save_path:
        # Use explicit path
        ext = ".png" if png_data[:4] == b"\x89PNG" else ".ppm"
        if not save_path.endswith(ext):
            save_path = save_path.rsplit(".", 1)[0] + ext
        with open(save_path, "wb") as f:
            f.write(png_data)
        out_path = save_path
    else:
        # Fallback to temp file
        ext = ".png" if png_data[:4] == b"\x89PNG" else ".ppm"
        tmp = tempfile.NamedTemporaryFile(
            prefix="amiga_screenshot_",
            suffix=ext,
            delete=False,
        )
        tmp.write(png_data)
        tmp.close()
        out_path = tmp.name

    logger.info("Screenshot saved to %s (%dx%d, %d colors)",
                out_path, width, height, len(palette))
    return out_path
