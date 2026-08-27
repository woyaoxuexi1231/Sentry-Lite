"""Generate Sentry-Lite icon — port-manager visual language (dark tile, white strokes, green dot)."""

from __future__ import annotations

import struct
from pathlib import Path

from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[1]
OUT_ICO = ROOT / "resources" / "app.ico"
OUT_PNG = ROOT / "resources" / "app-source.png"

# port-manager palette
BG = (27, 29, 33, 255)
STROKE = (255, 255, 255, 255)
GREEN = (22, 163, 74, 255)
MUTED = (107, 114, 128, 255)


def draw_icon(size: int) -> Image.Image:
    img = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    s = size / 512.0
    pad = int(56 * s)
    r = int(72 * s)

    # Rounded tile
    d.rounded_rectangle((pad, pad, size - pad, size - pad), radius=r, fill=BG)

    cx = size // 2
    # Three metric bars (CPU / GPU / RAM) — same capsule language as port-manager rows
    bar_w = int(248 * s)
    bar_h = int(36 * s)
    gap = int(28 * s)
    x0 = cx - bar_w // 2
    y0 = int(168 * s)
    fills = (0.72, 0.54, 0.86)
    for i, fill in enumerate(fills):
        y = y0 + i * (bar_h + gap)
        d.rounded_rectangle((x0, y, x0 + bar_w, y + bar_h), radius=int(bar_h // 2), outline=STROKE, width=max(2, int(5 * s)))
        inner_w = int((bar_w - 16 * s) * fill)
        if inner_w > 0:
            d.rounded_rectangle(
                (x0 + int(8 * s), y + int(8 * s), x0 + int(8 * s) + inner_w, y + bar_h - int(8 * s)),
                radius=int((bar_h - 16 * s) // 2),
                fill=STROKE if i < 2 else MUTED,
            )

    # Small pulse line under bars (network / sentry watch)
    py = int(360 * s)
    pts = []
    for i, h in enumerate((0, 0.35, 0.9, 0.45, 0.2, 0.55, 0)):
        x = x0 + int(i * (bar_w / 6))
        y = py - int(h * 28 * s)
        pts.append((x, y))
    d.line(pts, fill=STROKE, width=max(2, int(4 * s)), joint="curve")

    # Status dot — port-manager accent
    dot_r = int(22 * s)
    dot_cx = size - pad - int(36 * s)
    dot_cy = pad + int(36 * s)
    d.ellipse((dot_cx - dot_r, dot_cy - dot_r, dot_cx + dot_r, dot_cy + dot_r), fill=GREEN)

    return img


def png_to_ico(png_sizes: dict[int, Image.Image], path: Path) -> None:
    entries = []
    image_data = b""
    offset = 6 + 16 * len(png_sizes)

    for size in sorted(png_sizes):
        img = png_sizes[size]
        from io import BytesIO

        bio = BytesIO()
        img.save(bio, format="PNG")
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
    sizes = {sz: draw_icon(sz) for sz in (16, 32, 48, 64, 128, 256)}
    png_to_ico(sizes, OUT_ICO)
    print(f"wrote {OUT_ICO} ({OUT_ICO.stat().st_size} bytes)")
    print(f"wrote {OUT_PNG}")


if __name__ == "__main__":
    main()
