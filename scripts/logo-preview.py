#!/usr/bin/env python3
"""Reference model of the Madire OS boot logo (kernel/logo.c).

Uses the same stroke data and integer math as the kernel, so it serves as an
independent check: `make test` compares the kernel's lit-pixel count on the
serial port with the count printed here. It also writes a PNG preview at true
VGA text-mode scale (each logo pixel = half a 9x16 character cell).

    python scripts/logo-preview.py [preview.png]
"""
import struct
import sys
import zlib

W, H = 36, 24            # logo size in pixels (36 columns x 12 text rows)
Q = 8                    # stroke coordinates are in 1/8 pixel
RX, RY = 20, 23          # stroke radius (horizontal, vertical) in 1/8 pixel
STEPS = 6                # gradient steps per stroke, dark blue -> cyan

# Keep in sync with kernel/logo.c. Drawn in order; later strokes are on top.
STROKES = [
    (95, 57, 139, 169),   # left peak -> valley     (back)
    (182, 23, 229, 122),  # right peak -> right foot (back)
    (59, 123, 95, 57),    # left foot -> left peak   (front)
    (139, 169, 182, 23),  # valley -> right peak     (front)
]
DAC = [(0, 16, 50), (0, 25, 63), (0, 35, 63), (5, 44, 63), (10, 52, 63), (16, 58, 63)]


def inside(px, py, s):
    x1, y1, x2, y2 = s
    k = lambda y: y * RX // RY          # make the capsule round on screen
    y1, y2, py = k(y1), k(y2), k(py)
    dx, dy, ax, ay = x2 - x1, y2 - y1, px - x1, py - y1
    dot, len2 = ax * dx + ay * dy, dx * dx + dy * dy
    if dot <= 0:
        return ax * ax + ay * ay <= RX * RX
    if dot >= len2:
        bx, by = px - x2, py - y2
        return bx * bx + by * by <= RX * RX
    cross = ax * dy - ay * dx
    return cross * cross <= RX * RX * len2


def render():
    grid = [[-1] * W for _ in range(H)]
    for s in STROKES:
        lo, hi = min(s[0], s[2]) - RX, max(s[0], s[2]) + RX
        for y in range(H):
            for x in range(W):
                px, py = x * Q + Q // 2, y * Q + Q // 2
                if inside(px, py, s):
                    grid[y][x] = max(0, min(STEPS - 1, (px - lo) * STEPS // (hi - lo + 1)))
    return grid


def write_png(path, grid):
    rgb = [tuple(v * 255 // 63 for v in c) for c in DAC]
    sw, sh = W * 9, H * 8
    rows = []
    for y in range(sh):
        row = bytearray(b"\x00")
        for x in range(sw):
            c = grid[y // 8][x // 9]
            row += bytes(rgb[c] if c >= 0 else (0, 0, 0))
        rows.append(bytes(row))

    def chunk(t, d):
        return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d))

    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n"
                + chunk(b"IHDR", struct.pack(">IIBBBBB", sw, sh, 8, 2, 0, 0, 0))
                + chunk(b"IDAT", zlib.compress(b"".join(rows)))
                + chunk(b"IEND", b""))


if __name__ == "__main__":
    grid = render()
    for row in grid:
        print("".join("#" if c >= 0 else "." for c in row))
    print(f"Logo: {W}x{H}, {sum(c >= 0 for r in grid for c in r)} pixels lit")
    if len(sys.argv) > 1:
        write_png(sys.argv[1], grid)
