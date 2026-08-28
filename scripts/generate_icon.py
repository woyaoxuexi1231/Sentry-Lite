"""Generate Sentry-Lite app icon — three equal Uptime capsules (ok / warn / crit).

Renders at high supersample, lightly blurs, then Lanczos-downscales so
title-bar / tray sizes keep smooth rounded corners.
"""

from __future__ import annotations

import struct
from io import BytesIO
from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter

ROOT = Path(__file__).resolve().parents[1]
OUT_ICO = ROOT / "resources" / "app.ico"
OUT_PNG = ROOT / "resources" / "app-source.png"

BG = (23, 23, 23, 255)
OK = (16, 185, 129, 255)
WARN = (251, 191, 36, 255)
CRIT = (248, 113, 113, 255)

SS = 16


def _draw_at(size: int) -> Image.Image:
    img = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    s = size / 512.0

    # Slightly larger corner radius so 16–32px stays visibly round
    pad = int(40 * s)
    radius = int(112 * s)
    d.rounded_rectangle((pad, pad, size - pad, size - pad), radius=radius, fill=BG)

    colors = (OK, WARN, CRIT)
    n = 3
    gap = int(32 * s)
    bar_w = int(58 * s)
    bar_h = int(248 * s)
    total_w = n * bar_w + (n - 1) * gap
    x0 = (size - total_w) // 2
    y0 = (size - bar_h) // 2
    rr = bar_w // 2

    for i, color in enumerate(colors):
        x = x0 + i * (bar_w + gap)
        d.rounded_rectangle((x, y0, x + bar_w, y0 + bar_h), radius=rr, fill=color)

    return img


def draw_icon(size: int) -> Image.Image:
    hi = _draw_at(size * SS)
    # Soften hard raster edges before downscale (keeps shape, improves AA)
    blur = max(1, SS // 8)
    hi = hi.filter(ImageFilter.GaussianBlur(radius=blur))
    out = hi.resize((size, size), Image.Resampling.LANCZOS)
    # Restore near-solid fills after blur bleed
    px = out.load()
    w, h = out.size
    for y in range(h):
        for x in range(w):
            r, g, b, a = px[x, y]
            if a >= 240:
                px[x, y] = (r, g, b, 255)
            elif a <= 8:
                px[x, y] = (0, 0, 0, 0)
    return out


def png_to_ico(png_sizes: dict[int, Image.Image], path: Path) -> None:
    entries = []
    offset = 6 + 16 * len(png_sizes)

    for size in sorted(png_sizes):
        bio = BytesIO()
        png_sizes[size].save(bio, format="PNG")
        data = bio.getvalue()
        entries.append((size, data, offset))
        offset += len(data)

    ico = bytearray()
    ico += struct.pack("<HHH", 0, 1, len(entries))
    for size, data, off in entries:
        w = 0 if size >= 256 else size
        h = 0 if size >= 256 else size
        ico += struct.pack("<BBBBHHII", w, h, 0, 0, 1, 32, len(data), off)
    for _, data, _ in entries:
        ico += data
    path.write_bytes(ico)


def main() -> None:
    source = draw_icon(512)
    OUT_PNG.parent.mkdir(parents=True, exist_ok=True)
    source.save(OUT_PNG, "PNG")
    sizes = {sz: draw_icon(sz) for sz in (16, 20, 24, 32, 40, 48, 64, 128, 256)}
    png_to_ico(sizes, OUT_ICO)
    print(f"wrote {OUT_ICO} ({OUT_ICO.stat().st_size} bytes)")
    print(f"wrote {OUT_PNG}")


if __name__ == "__main__":
    main()
