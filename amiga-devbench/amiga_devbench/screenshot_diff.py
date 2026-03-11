"""Visual screenshot diff comparison for Amiga regression testing.

Compares two PNG screenshots and produces a diff image highlighting
pixel differences, with statistics and region detection.
"""

from __future__ import annotations

import os
import tempfile
import uuid
from typing import Optional

try:
    from PIL import Image, ImageDraw
    HAS_PILLOW = True
except ImportError:
    HAS_PILLOW = False


BLOCK_SIZE = 16
BLOCK_CHANGE_THRESHOLD = 0.05  # 5% of pixels in a block must differ


def _pixel_diff(px_a: tuple, px_b: tuple) -> int:
    """Return the max channel difference between two RGB(A) pixels."""
    # Compare only RGB channels (ignore alpha if present)
    channels = min(len(px_a), len(px_b), 3)
    return max(abs(px_a[i] - px_b[i]) for i in range(channels))


def _to_rgb(img: Image.Image) -> Image.Image:
    """Convert image to RGB mode."""
    if img.mode == "RGBA":
        # Composite onto white background
        bg = Image.new("RGB", img.size, (255, 255, 255))
        bg.paste(img, mask=img.split()[3])
        return bg
    if img.mode != "RGB":
        return img.convert("RGB")
    return img


def _detect_regions(changed_grid: list[list[bool]], grid_w: int, grid_h: int,
                    block_size: int, img_w: int, img_h: int) -> list[dict]:
    """Detect bounding boxes of changed regions by merging adjacent blocks.

    Uses flood-fill on the block grid to find connected components,
    then computes a bounding rectangle for each component.
    """
    visited = [[False] * grid_w for _ in range(grid_h)]
    regions = []

    for gy in range(grid_h):
        for gx in range(grid_w):
            if changed_grid[gy][gx] and not visited[gy][gx]:
                # Flood-fill to find connected component
                stack = [(gx, gy)]
                min_gx, max_gx = gx, gx
                min_gy, max_gy = gy, gy
                while stack:
                    cx, cy = stack.pop()
                    if cx < 0 or cx >= grid_w or cy < 0 or cy >= grid_h:
                        continue
                    if visited[cy][cx] or not changed_grid[cy][cx]:
                        continue
                    visited[cy][cx] = True
                    min_gx = min(min_gx, cx)
                    max_gx = max(max_gx, cx)
                    min_gy = min(min_gy, cy)
                    max_gy = max(max_gy, cy)
                    stack.extend([
                        (cx - 1, cy), (cx + 1, cy),
                        (cx, cy - 1), (cx, cy + 1),
                    ])

                x = min_gx * block_size
                y = min_gy * block_size
                w = min((max_gx + 1) * block_size, img_w) - x
                h = min((max_gy + 1) * block_size, img_h) - y
                regions.append({"x": x, "y": y, "w": w, "h": h})

    return regions


def compare_screenshots(path_a: str, path_b: str, threshold: int = 10) -> dict:
    """Compare two screenshots and return diff info.

    Args:
        path_a: path to first screenshot PNG
        path_b: path to second screenshot PNG
        threshold: pixel difference threshold (0-255) to count as "changed"

    Returns:
        dict with keys: diff_path, total_pixels, changed_pixels,
        change_percent, identical, dimensions_match, size_a, size_b, regions
    """
    if not HAS_PILLOW:
        raise RuntimeError(
            "Pillow is required for screenshot diff. "
            "Install with: pip install Pillow"
        )

    img_a = _to_rgb(Image.open(path_a))
    img_b = _to_rgb(Image.open(path_b))

    size_a = list(img_a.size)
    size_b = list(img_b.size)
    dimensions_match = (size_a == size_b)

    # Work on the overlapping region
    w = min(img_a.width, img_b.width)
    h = min(img_a.height, img_b.height)
    total_pixels = w * h

    pixels_a = img_a.load()
    pixels_b = img_b.load()

    # Create diff image: dimmed version of image A as background
    diff_img = img_a.copy()
    if diff_img.size != (w, h):
        diff_img = diff_img.crop((0, 0, w, h))
    # Dim to 50% brightness
    diff_img = Image.eval(diff_img, lambda v: v // 2)
    diff_pixels = diff_img.load()

    # Grid for region detection
    grid_w = (w + BLOCK_SIZE - 1) // BLOCK_SIZE
    grid_h = (h + BLOCK_SIZE - 1) // BLOCK_SIZE
    block_changed_count = [[0] * grid_w for _ in range(grid_h)]
    block_total_count = [[0] * grid_w for _ in range(grid_h)]

    changed_pixels = 0

    for y in range(h):
        for x in range(w):
            pa = pixels_a[x, y]
            pb = pixels_b[x, y]
            diff_val = _pixel_diff(pa, pb)

            gx = x // BLOCK_SIZE
            gy = y // BLOCK_SIZE
            block_total_count[gy][gx] += 1

            if diff_val > threshold:
                changed_pixels += 1
                block_changed_count[gy][gx] += 1
                # Overlay changed pixel in bright magenta
                diff_pixels[x, y] = (255, 0, 255)

    # Determine which blocks are "changed"
    changed_grid = [[False] * grid_w for _ in range(grid_h)]
    for gy in range(grid_h):
        for gx in range(grid_w):
            total = block_total_count[gy][gx]
            if total > 0:
                ratio = block_changed_count[gy][gx] / total
                if ratio > BLOCK_CHANGE_THRESHOLD:
                    changed_grid[gy][gx] = True

    regions = _detect_regions(changed_grid, grid_w, grid_h, BLOCK_SIZE, w, h)

    # Draw yellow bounding boxes around regions
    draw = ImageDraw.Draw(diff_img)
    for r in regions:
        x0, y0 = r["x"], r["y"]
        x1 = x0 + r["w"] - 1
        y1 = y0 + r["h"] - 1
        draw.rectangle([x0, y0, x1, y1], outline=(255, 255, 0), width=2)

    # Save diff image
    diff_path = os.path.join(
        tempfile.gettempdir(),
        f"amiga_diff_{uuid.uuid4().hex[:8]}.png"
    )
    diff_img.save(diff_path, "PNG")

    change_percent = round((changed_pixels / total_pixels) * 100, 2) if total_pixels > 0 else 0.0

    return {
        "diff_path": diff_path,
        "total_pixels": total_pixels,
        "changed_pixels": changed_pixels,
        "change_percent": change_percent,
        "identical": changed_pixels == 0,
        "dimensions_match": dimensions_match,
        "size_a": size_a,
        "size_b": size_b,
        "regions": regions,
    }


def create_side_by_side(path_a: str, path_b: str,
                        diff_path: str | None = None) -> str:
    """Create a side-by-side comparison image: [A] [Diff] [B].

    If diff_path is not provided, a diff is computed automatically.

    Args:
        path_a: path to first screenshot PNG
        path_b: path to second screenshot PNG
        diff_path: optional path to pre-computed diff image

    Returns:
        Path to the output side-by-side PNG.
    """
    if not HAS_PILLOW:
        raise RuntimeError(
            "Pillow is required for screenshot diff. "
            "Install with: pip install Pillow"
        )

    img_a = _to_rgb(Image.open(path_a))
    img_b = _to_rgb(Image.open(path_b))

    # Generate diff if not provided
    if diff_path is None:
        result = compare_screenshots(path_a, path_b)
        diff_path = result["diff_path"]

    img_diff = _to_rgb(Image.open(diff_path))

    # Use the max height across all three panels
    panel_h = max(img_a.height, img_b.height, img_diff.height)
    gap = 4  # pixels between panels
    total_w = img_a.width + img_diff.width + img_b.width + gap * 2

    canvas = Image.new("RGB", (total_w, panel_h), (32, 32, 32))

    # Paste panels, vertically centered
    def paste_centered(img: Image.Image, x_offset: int) -> None:
        y_offset = (panel_h - img.height) // 2
        canvas.paste(img, (x_offset, y_offset))

    x = 0
    paste_centered(img_a, x)
    x += img_a.width + gap
    paste_centered(img_diff, x)
    x += img_diff.width + gap
    paste_centered(img_b, x)

    # Add labels
    draw = ImageDraw.Draw(canvas)
    label_y = 2
    # Simple text labels (uses default bitmap font)
    draw.text((4, label_y), "A", fill=(255, 255, 255))
    draw.text((img_a.width + gap + 4, label_y), "Diff", fill=(255, 255, 0))
    draw.text((img_a.width + img_diff.width + gap * 2 + 4, label_y), "B",
              fill=(255, 255, 255))

    output_path = os.path.join(
        tempfile.gettempdir(),
        f"amiga_sbs_{uuid.uuid4().hex[:8]}.png"
    )
    canvas.save(output_path, "PNG")
    return output_path
