#!/usr/bin/env python3
"""Generate qbitx_wallet_icon.ico with standard Windows sizes (requires Pillow)."""
from pathlib import Path

try:
    from PIL import Image, ImageDraw
except ImportError as exc:
    raise SystemExit("Install Pillow: pip install Pillow") from exc

OUT = Path(__file__).resolve().parent.parent / "assets" / "qbitx_wallet_icon.ico"
SIZES = [16, 32, 48, 64, 128, 256]

images = []
for s in SIZES:
    img = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    margin = max(1, s // 8)
    d.rounded_rectangle(
        [margin, margin, s - margin - 1, s - margin - 1],
        radius=max(2, s // 6),
        fill=(26, 115, 232, 255),
    )
    d.text((s // 2, s // 2), "Q", fill="white", anchor="mm")
    images.append(img)

OUT.parent.mkdir(parents=True, exist_ok=True)
images[0].save(OUT, format="ICO", sizes=[(s, s) for s in SIZES])
print(f"Wrote {OUT} ({OUT.stat().st_size} bytes)")
