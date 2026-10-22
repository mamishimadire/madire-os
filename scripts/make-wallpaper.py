#!/usr/bin/env python3
"""Madire OS boot menu wallpaper (1.1): the 1024x768 PNG that Limine shows
behind its menu once a key reveals it (limine.conf: quiet, term_margin 240).

    make-wallpaper.py OUT.png --version X [--layout-json OUT.json]
    make-wallpaper.py --print-strokes

The Makefile passes the version read from kernel/kernel.h; the static check
[1.1] S2a runs this script again with the test harness's version and
compares the bytes with the ISO's file. --layout-json writes the boxes of
the header, the panel, the hint and the version (the one source of the
rectangles the menu checks use). --print-strokes prints the M's four
strokes, one "x1 y1 x2 y2" line each, for the check against kernel/logo.c.

Python standard library only (math, struct, zlib): no fonts, no images, no
randomness, no clock. The letterforms are original capsule strokes on a
14-unit cap height, only the glyphs used (and the ten digits, for any
version). The same Python and zlib give the same bytes; across machines the
floating-point maths and zlib may differ in the last bit (docs/plans/1.1.md,
section F).
"""
import argparse
import json
import math
import re
import struct
import sys
import zlib

W, H = 1024, 768
BG_TOP, BG_BOT = (0x0B, 0x12, 0x20), (0x0F, 0x2A, 0x4A)
ACCENT, ACCENT2 = (0x1E, 0x90, 0xFF), (0x22, 0xD3, 0xEE)
TEXT, MUTED = (0xE6, 0xED, 0xF3), (0x8B, 0x9B, 0xB4)

# The layout (O2, the plan's section F), in one place. The boxes are
# inclusive pixel rectangles x0, y0, x1, y1; the drawn header, hint and
# version must lie inside theirs (checked before the file is written).
LAYOUT = {
    "header": [308, 36, 715, 257],
    "panel": [224, 276, 800, 492],
    "hint": [391, 507, 632, 519],
    "version": [972, 727, 995, 740],
}
PX0, PY0, PX1, PY1 = LAYOUT["panel"]
PANEL_RADIUS = 14
# The header: the logo's top and its px per 1/8-pixel unit, the title's cap
# top, scale, stroke radius (MADIRE, then OS) and tracking, the tagline's
# cap top, scale and stroke radius. The hint sits 16 px under the panel, the
# horizon's top 44 px under the hint.
LOGO_TOP, LOGO_S = 36, 0.74
TITLE_Y, TITLE_S, TITLE_R, OS_R, TITLE_TRACK = 190, 2.3, 2.5, 1.25, 6.0
TAG_Y, TAG_S, TAG_R = 246, 0.78, 0.75
TAGLINE = "SIMPLE \u00b7 SECURE \u00b7 FURTHER TOGETHER"
HINT = "\u2191 \u2193 SELECT  \u00b7  ENTER BOOT"

# kernel/logo.c's four strokes, in 1/8 pixel of its grid (x1, y1, x2, y2;
# the two back strokes first), and its radii: the y coordinates are scaled
# by RX/RY, the logo's aspect correction.
STROKES = [(95, 57, 139, 169), (182, 23, 229, 122), (59, 123, 95, 57), (139, 169, 182, 23)]
RX, RY = 20, 23


def mix(a, b, t):
    return tuple(a[i] + (b[i] - a[i]) * t for i in range(3))


def arc(cx, cy, rx, ry, t0, t1, n=24):
    return [(cx + rx * math.cos(math.radians(t0 + (t1 - t0) * k / n)),
             cy + ry * math.sin(math.radians(t0 + (t1 - t0) * k / n))) for k in range(n + 1)]


# Each glyph: its advance width and its strokes (polylines, in cap-height
# units, y down; a one-point polyline is a dot).
G = {
    "A": (10, [[(0, 14), (5, 0), (10, 14)], [(2, 9.5), (8, 9.5)]]),
    "B": (9, [[(0, 0), (0, 14)], [(0, 0), (5, 0)] + arc(5, 3.5, 3.5, 3.5, 270, 450) + [(0, 7)],
              [(0, 7), (5.5, 7)] + arc(5.5, 10.5, 3.5, 3.5, 270, 450) + [(0, 14)]]),
    "C": (12, [arc(7, 7, 7, 7, 45, 315, 40)]),
    "D": (11, [[(0, 0), (0, 14)], [(0, 0), (4, 0)] + arc(4, 7, 7, 7, 270, 450, 32) + [(0, 14)]]),
    "E": (9, [[(9, 0), (0, 0), (0, 14), (9, 14)], [(0, 7), (7.5, 7)]]),
    "F": (9, [[(9, 0), (0, 0), (0, 14)], [(0, 7), (7.5, 7)]]),
    "G": (14, [arc(7, 7, 7, 7, 0, 315, 44), [(14, 7), (8, 7)]]),
    "H": (10, [[(0, 0), (0, 14)], [(10, 0), (10, 14)], [(0, 7), (10, 7)]]),
    "I": (0, [[(0, 0), (0, 14)]]),
    "L": (8, [[(0, 0), (0, 14), (8, 14)]]),
    "M": (12, [[(0, 14), (0, 0), (6, 10), (12, 0), (12, 14)]]),
    "N": (10, [[(0, 14), (0, 0), (10, 14), (10, 0)]]),
    "O": (14, [arc(7, 7, 7, 7, 0, 360, 48)]),
    "P": (9, [[(0, 14), (0, 0), (5, 0)] + arc(5, 4, 4, 4, 270, 450) + [(0, 8)]]),
    "R": (9, [[(0, 14), (0, 0), (5, 0)] + arc(5, 4, 4, 4, 270, 450) + [(0, 8)], [(4, 8), (9, 14)]]),
    "S": (10, [arc(5, 3.5, 4.5, 3.5, 330, 90, 24) + arc(5, 10.5, 5, 3.5, 270, 510, 30)]),
    "T": (10, [[(0, 0), (10, 0)], [(5, 0), (5, 14)]]),
    "U": (10, [[(0, 0), (0, 9)] + arc(5, 9, 5, 5, 180, 0, 24) + [(10, 0)]]),
    "v": (8, [[(0, 5), (4, 14), (8, 5)]]),
    "0": (9, [arc(4.5, 7, 4.5, 7, 0, 360, 40)]),
    "1": (5, [[(1, 3), (4, 0), (4, 14)]]),
    "2": (9, [arc(4.5, 4, 4.5, 4, 180, 385, 20) + [(0, 14), (9, 14)]]),
    "3": (9, [arc(4.5, 3.5, 4.5, 3.5, 200, 450, 24) + arc(4.5, 10.5, 4.5, 3.5, 270, 520, 24)]),
    "4": (10, [[(7, 14), (7, 0), (0, 10), (9.5, 10)]]),
    "5": (9, [[(8.5, 0), (1, 0), (0.5, 6.2)] + arc(4.5, 9.5, 4.5, 4.5, 235, 520, 28)]),
    "6": (9, [[(7, 0), (1.2, 6.5)], arc(4.5, 9.5, 4.5, 4.5, 0, 360, 40)]),
    "7": (9, [[(0, 0), (9, 0), (3, 14)]]),
    "8": (9, [arc(4.5, 3.5, 4, 3.5, 0, 360, 36), arc(4.5, 10.5, 4.5, 3.5, 0, 360, 40)]),
    "9": (9, [arc(4.5, 4.5, 4.5, 4.5, 0, 360, 40), [(7.8, 7.5), (2, 14)]]),
    ".": (1, [[(0.5, 13.6)]]),
    "\u00b7": (1, [[(0.5, 7.5)]]),
    "\u2191": (8, [[(4, 14), (4, 0)], [(0, 4), (4, 0), (8, 4)]]),
    "\u2193": (8, [[(4, 0), (4, 14)], [(0, 10), (4, 14), (8, 10)]]),
    " ": (6, []),
}


class Canvas:
    def __init__(self):
        self.img = [[None] * W for _ in range(H)]
        self.boxes = {}

    def capsule(self, cov, x1, y1, x2, y2, r):
        """Coverage (0..1, anti-aliased) of a segment with radius r, kept per
        pixel as the maximum over the strokes of one shape."""
        x0, x3 = int(min(x1, x2) - r - 2), int(max(x1, x2) + r + 2)
        y0, y3 = int(min(y1, y2) - r - 2), int(max(y1, y2) + r + 2)
        dx, dy = x2 - x1, y2 - y1
        l2 = dx * dx + dy * dy
        for py in range(max(0, y0), min(H, y3 + 1)):
            for px in range(max(0, x0), min(W, x3 + 1)):
                cx, cy = px + 0.5 - x1, py + 0.5 - y1
                t = 0.0 if l2 == 0 else max(0.0, min(1.0, (cx * dx + cy * dy) / l2))
                a = r + 0.5 - math.hypot(cx - t * dx, cy - t * dy)
                if a > 0:
                    a = min(1.0, a)
                    if cov.get((px, py), 0) < a:
                        cov[(px, py)] = a

    def paint(self, cov, colour_at, name):
        img = self.img
        for (x, y), a in cov.items():
            img[y][x] = mix(img[y][x], colour_at(x, y), a)
        if cov:
            xs = [k[0] for k in cov]
            ys = [k[1] for k in cov]
            b = self.boxes.get(name, [W, H, 0, 0])
            self.boxes[name] = [min(b[0], min(xs)), min(b[1], min(ys)), max(b[2], max(xs)), max(b[3], max(ys))]


def text_width(s, scale, track):
    return sum((G[ch][0] + track) for ch in s) * scale - track * scale


def draw_text(cv, s, x, y, scale, r, track, colour, name, weight_of=None):
    cov = {}
    pen = x
    for i, ch in enumerate(s):
        adv, polys = G[ch]
        rr = weight_of(i) if weight_of else r
        for poly in polys:
            pts = [(pen + px * scale, y + py * scale) for px, py in poly]
            if len(pts) == 1:
                pts = pts * 2
            for (ax, ay), (bx, by) in zip(pts, pts[1:]):
                cv.capsule(cov, ax, ay, bx, by, rr)
        pen += (adv + track) * scale
    cv.paint(cov, lambda X, Y: colour, name)


def centred(cv, s, y, scale, r, track, colour, name, weight_of=None):
    draw_text(cv, s, 512 - text_width(s, scale, track) / 2, y, scale, r, track, colour, name, weight_of)


def background(cv):
    """The navy gradient, the soft glow behind the logo and the horizon arc."""
    arc_top = PY1 + 16 + 11 + 44
    arc_r = 1500.0
    arc_cy = arc_top + arc_r
    for y in range(H):
        base = mix(BG_TOP, BG_BOT, y / (H - 1))
        row = cv.img[y]
        for x in range(W):
            c = base
            g = math.exp(-(((x - 512) / 300.0) ** 2 + ((y - LOGO_TOP - 118) / 170.0) ** 2))
            c = mix(c, ACCENT, 0.10 * g)
            dc = math.hypot(x - 512.0, y - arc_cy) - arc_r
            if dc > 0:
                c = mix(c, ACCENT, 0.42 * math.exp(-dc / 55.0))
            else:
                c = mix(c, (0x05, 0x0A, 0x14), min(1.0, -dc / 90.0) * 0.85)
                c = mix(c, ACCENT, 0.25 * math.exp(dc / 18.0))
            c = mix(c, (0x9F, 0xDB, 0xFF), 0.9 * math.exp(-(dc / 1.6) ** 2))
            row[x] = c


def logo(cv):
    """The M: kernel/logo.c's capsules, smooth, the accent gradient from left
    to right, the two back strokes darker."""
    ls = LOGO_S
    lx0, lx1 = 59 - RX, 229 + RX
    lox = 512 - (lx0 + lx1) / 2 * ls
    loy = LOGO_TOP - (23 * RX / RY - RX) * ls
    for i, (x1, y1, x2, y2) in enumerate(STROKES):
        cov = {}
        cv.capsule(cov, lox + x1 * ls, loy + y1 * RX / RY * ls, lox + x2 * ls, loy + y2 * RX / RY * ls, RX * ls)

        def col(x, y, back=i < 2):
            t = max(0.0, min(1.0, (x - (lox + lx0 * ls)) / ((lx1 - lx0) * ls)))
            c = mix(ACCENT, ACCENT2, t)
            return mix(c, (0x0B, 0x2A, 0x6A), 0.35) if back else c
        cv.paint(cov, col, "header")


def panel(cv):
    """The rounded panel Limine's menu window lines up with."""
    def rr_dist(x, y):
        cx = min(max(x, PX0 + PANEL_RADIUS), PX1 - PANEL_RADIUS)
        cy = min(max(y, PY0 + PANEL_RADIUS), PY1 - PANEL_RADIUS)
        return math.hypot(x - cx, y - cy) - PANEL_RADIUS
    img = cv.img
    for y in range(PY0 - 2, PY1 + 3):
        for x in range(PX0 - 2, PX1 + 3):
            d = rr_dist(x + 0.5, y + 0.5)
            img[y][x] = mix(img[y][x], (0x07, 0x0E, 0x1C), 0.62 * max(0.0, min(1.0, 0.5 - d)))
            img[y][x] = mix(img[y][x], ACCENT, 0.45 * max(0.0, 1.0 - abs(d + 0.5) / 0.9))


def chunk(kind, data):
    return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xffffffff)


def png_bytes(cv, version):
    raw = bytearray()
    for row in cv.img:
        raw.append(0)
        for c in row:
            raw += bytes(max(0, min(255, int(round(v)))) for v in c)
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", W, H, 8, 2, 0, 0, 0))
            + chunk(b"tEXt", b"Madire-Version\x00" + version.encode("ascii"))
            + chunk(b"IDAT", zlib.compress(bytes(raw), 9)) + chunk(b"IEND", b""))


def render(version):
    cv = Canvas()
    background(cv)
    logo(cv)
    centred(cv, "MADIRE OS", TITLE_Y, TITLE_S, TITLE_R, TITLE_TRACK, TEXT, "header",
            weight_of=lambda i: TITLE_R if i < 6 else OS_R)
    centred(cv, TAGLINE, TAG_Y, TAG_S, TAG_R, 7.0, MUTED, "header")
    panel(cv)
    centred(cv, HINT, PY1 + 16, 0.78, 0.7, 4.0, MUTED, "hint")
    vs = "v" + version
    draw_text(cv, vs, W - 28 - text_width(vs, 0.82, 3.0), H - 28 - 14 * 0.82, 0.82, 0.72, 3.0, MUTED, "version")
    for name, (x0, y0, x1, y1) in cv.boxes.items():
        lx0, ly0, lx1, ly1 = LAYOUT[name]
        if not (lx0 <= x0 and ly0 <= y0 and x1 <= lx1 and y1 <= ly1):
            sys.exit(f"make-wallpaper.py: the {name} drawn at {x0},{y0}-{x1},{y1} is outside "
                     f"its layout box {lx0},{ly0}-{lx1},{ly1}")
    return png_bytes(cv, version), cv.boxes


def main():
    ap = argparse.ArgumentParser(description="Madire OS boot menu wallpaper (1024x768 PNG)")
    ap.add_argument("out", nargs="?", help="the PNG to write")
    ap.add_argument("--version", help="the version shown bottom right, e.g. 1.1")
    ap.add_argument("--layout-json", help="also write the layout boxes to this JSON file")
    ap.add_argument("--print-strokes", action="store_true", help="print the M's strokes and exit")
    a = ap.parse_args()
    if a.print_strokes:
        for s in STROKES:
            print(*s)
        return
    if not a.out or a.version is None:
        ap.error("OUT.png and --version are required")
    if not re.fullmatch(r"[0-9]+(\.[0-9]+)*", a.version):
        ap.error(f"--version {a.version!r}: digits and dots only")
    data, boxes = render(a.version)
    with open(a.out, "wb") as f:
        f.write(data)
    if a.layout_json:
        with open(a.layout_json, "w") as f:
            json.dump(LAYOUT, f, indent=1)
            f.write("\n")
    print(f"{a.out}: {len(data)} bytes, v{a.version}, drawn {json.dumps(boxes)}")


if __name__ == "__main__":
    main()
