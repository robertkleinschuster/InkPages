# Generates the pen cursors in cursors/: a filled dot in the pen's color (diameter = pen width in DIPs, as drawn at
# half pressure) with a thin white halo, hotspot in the center. Each .cur holds 32/48/64 px images (100/150/200 %);
# Windows picks the image for the cursor size and DPI.
# Run: python demo/tools/make_cursors.py (writes demo/cursors)
import math
import struct
from pathlib import Path

COLORS = {"black": (0, 0, 0), "blue": (0x1E, 0x5A, 0xC8), "red": (0xD2, 0x1E, 0x1E)}
WIDTHS = {"small": 2.0, "large": 6.0}   # pen widths in DIPs (must match demo/main.cpp)
SIZES = [32, 48, 64]


def coverage(px, py, cx, cy, r):
    """Anti-aliased coverage of pixel (px, py) by a disc (4x4 supersampling)."""
    hits = 0
    for sy in range(4):
        for sx in range(4):
            x = px + (sx + 0.5) / 4
            y = py + (sy + 0.5) / 4
            if (x - cx) ** 2 + (y - cy) ** 2 <= r * r:
                hits += 1
    return hits / 16


def image(size, rgb, width_dip):
    scale = size / 32
    c = size / 2
    r = max(1.0, width_dip * scale / 2)
    halo = r + max(1.0, scale)
    rows = []
    for y in range(size - 1, -1, -1):  # bottom-up
        row = bytearray()
        for x in range(size):
            dot = coverage(x, y, c, c, r)
            ring = coverage(x, y, c, c, halo)
            # dot over a half-transparent white halo, premultiplied for the blend below
            a_ring = ring * 0.8
            a = dot + a_ring * (1 - dot)
            if a <= 0:
                row += bytes((0, 0, 0, 0))
                continue
            col = [(rgb[i] * dot + 255 * a_ring * (1 - dot)) / a for i in range(3)]
            row += bytes((round(col[2]), round(col[1]), round(col[0]), round(a * 255)))
        rows.append(bytes(row))
    xor = b"".join(rows)
    mask_row = ((size + 31) // 32) * 4
    and_mask = bytes(mask_row * size)
    header = struct.pack("<IiiHHIIiiII", 40, size, size * 2, 1, 32, 0, len(xor) + len(and_mask), 0, 0, 0, 0)
    return header + xor + and_mask


def cursor(rgb, width_dip):
    images = [image(s, rgb, width_dip) for s in SIZES]
    out = struct.pack("<HHH", 0, 2, len(SIZES))
    offset = 6 + 16 * len(SIZES)
    for s, data in zip(SIZES, images):
        out += struct.pack("<BBBBHHII", s % 256, s % 256, 0, 0, s // 2, s // 2, len(data), offset)
        offset += len(data)
    return out + b"".join(images)


root = Path(__file__).resolve().parent.parent / "cursors"
root.mkdir(exist_ok=True)
for cname, rgb in COLORS.items():
    for wname, w in WIDTHS.items():
        (root / f"dot_{cname}_{wname}.cur").write_bytes(cursor(rgb, w))
        print(f"dot_{cname}_{wname}.cur")
