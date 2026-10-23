#!/usr/bin/env python3
"""Madire OS wallpapers (1.1; integer maths since 1.2 stage 8): the one
reference algorithm for the boot menu's wallpaper (Limine's PNG) and for
the kernel's procedural wallpaper and splash (kernel/display/
wallpaper_core.c draws the same pictures with the same integer maths).

    make-wallpaper.py OUT.png --version X [--layout-json OUT.json]
    make-wallpaper.py --print-strokes
    make-wallpaper.py --emit-c OUT.h
    make-wallpaper.py --splash WxH OUT.ppm [--phase P] [--fontdir DIR]

OUT.png: the 1024x768 boot-menu wallpaper that Limine shows behind its menu
once a key reveals it (limine.conf: quiet, term_margin 240). The Makefile
passes the version read from kernel/kernel.h; the static check [1.1] S2a
runs this script again with the test harness's version and compares the
bytes with the ISO's file. --layout-json writes the boxes of the header,
the panel, the hint and the version (the one source of the rectangles the
menu checks use). --print-strokes prints the M's four strokes, one
"x1 y1 x2 y2" line each, for the check against kernel/logo.c.

--emit-c writes the splash's generated data (build/gen/splash_data.h, never
edited, never committed): the M's strokes and the wordmark's polylines in
the kernel's fixed point. --splash renders the kernel's splash on a W x H
screen (the wallpaper, the logo, "MADIRE OS", "Booting Madire OS" and the
dots with P of them lit) as a PPM: the independent reference of the
[1.2] splash checks (tests/qmp_splash.py imports it).

Python standard library only (struct, zlib, math.isqrt): no fonts but the
pinned BDF files for the splash's text, no images, no randomness, no clock,
and **no floating point**. Every quantity is an integer in fixed point
(class Fixed): the boot menu's wallpaper at 32 fraction bits, which gives
the 1.1 floating-point script's pixels exactly (its PNG bytes are those of
v1.1's ISO, SHA-256 2692cfe8...); the kernel's pictures at 8 fraction bits
(24.8, the Design Guide, section 6), which the kernel computes the same way.
exp, sin and cos are series in integers; pi comes from Machin's formula;
square roots are math.isqrt. The letterforms are original capsule strokes
on a 14-unit cap height, only the glyphs used (and the ten digits, for any
version), with coordinates in tenths of a unit.
"""
import argparse
import json
import math
import os
import re
import struct
import sys
import zlib

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")


class Fixed:
    """Integers with `fb` fraction bits. Every rounding is written out:
    to nearest, halves away from zero (fx, mul, div); colour channels to
    bytes, halves to even (byte)."""

    def __init__(self, fb):
        self.fb = fb
        self.one = 1 << fb
        self.half = self.one >> 1
        self._exp = None
        self._pi = None

    def fx(self, num, den=1):
        q = (abs(num) * self.one * 2 + den) // (2 * den)
        return q if num >= 0 else -q

    def mul(self, a, b):
        p = a * b
        return (p + self.half) >> self.fb if p >= 0 else -((-p + self.half) >> self.fb)

    def div(self, a, b):
        q, r = divmod(abs(a) * self.one, abs(b))
        if 2 * r >= abs(b):
            q += 1
        return q if (a >= 0) == (b > 0) else -q

    def hypot(self, x, y):
        return math.isqrt(x * x + y * y)

    def mix(self, a, b, t):
        return tuple(a[i] + self.mul(b[i] - a[i], t) for i in range(3))

    def byte(self, v):
        q, r = divmod(v, self.one)
        if r > self.half or (r == self.half and q & 1):
            q += 1
        return max(0, min(255, q))

    def rgb(self, c):
        return tuple(v << self.fb for v in c)

    # exp(-u) for u >= 0: u = n + f with n whole; exp(-1)^n from a table,
    # exp(-f) as a series, both with 16 guard bits.
    def _series(self, f, bits):
        one = 1 << bits
        term = s = one
        k = 1
        while True:
            term = term * (-f) // (one * k)
            if term == 0:
                return s
            s += term
            k += 1

    def expneg(self, u):
        g = self.fb + 16
        n = u >> self.fb
        if n > 60:
            return 0
        if self._exp is None:
            e1 = self._series(1 << g, g)
            self._exp = [1 << g]
            for _ in range(70):
                self._exp.append(self._exp[-1] * e1 >> g)
        v = self._exp[n] * self._series((u - (n << self.fb)) << 16, g) >> g
        return (v + (1 << 15)) >> 16

    # sin and cos of num/den degrees: pi by Machin's formula, then the
    # series, with 16 guard bits.
    def _arctan_inv(self, x, bits):
        one = 1 << (bits + 10)
        s = t = one // x
        n, x2, sign = 1, x * x, -1
        while t:
            t //= x2
            s += sign * (t // (2 * n + 1))
            n += 1
            sign = -sign
        return s >> 10

    def sincos_deg(self, num, den):
        g = self.fb + 16
        if self._pi is None:
            self._pi = 4 * (4 * self._arctan_inv(5, g) - self._arctan_inv(239, g))
        pi = self._pi
        a = (pi * num // (180 * den)) % (2 * pi)
        if a > pi:
            a -= 2 * pi
        one = 1 << g

        def series(x, k):
            term = one if k == 0 else x
            s = term
            while True:
                term = -term * x // one * x // one // ((k + 1) * (k + 2))
                if term == 0:
                    return s
                s += term
                k += 2
        return (series(a, 1) + (1 << 15)) >> 16, (series(a, 0) + (1 << 15)) >> 16


# ---- The letterforms (tenths of a unit; y down; 14 units cap height) ----
# A polyline is a list of points ("p", x, y) and arcs ("a", cx, cy, rx, ry,
# t0, t1, n): n + 1 points at t0 + (t1 - t0) k / n degrees. A one-point
# polyline is a dot.
def _p(x, y):
    return ("p", x, y)


def _a(cx, cy, rx, ry, t0, t1, n=24):
    return ("a", cx, cy, rx, ry, t0, t1, n)


G = {
    "A": (100, [[_p(0, 140), _p(50, 0), _p(100, 140)], [_p(20, 95), _p(80, 95)]]),
    "B": (90, [[_p(0, 0), _p(0, 140)], [_p(0, 0), _p(50, 0), _a(50, 35, 35, 35, 270, 450), _p(0, 70)],
               [_p(0, 70), _p(55, 70), _a(55, 105, 35, 35, 270, 450), _p(0, 140)]]),
    "C": (120, [[_a(70, 70, 70, 70, 45, 315, 40)]]),
    "D": (110, [[_p(0, 0), _p(0, 140)], [_p(0, 0), _p(40, 0), _a(40, 70, 70, 70, 270, 450, 32), _p(0, 140)]]),
    "E": (90, [[_p(90, 0), _p(0, 0), _p(0, 140), _p(90, 140)], [_p(0, 70), _p(75, 70)]]),
    "F": (90, [[_p(90, 0), _p(0, 0), _p(0, 140)], [_p(0, 70), _p(75, 70)]]),
    "G": (140, [[_a(70, 70, 70, 70, 0, 315, 44)], [_p(140, 70), _p(80, 70)]]),
    "H": (100, [[_p(0, 0), _p(0, 140)], [_p(100, 0), _p(100, 140)], [_p(0, 70), _p(100, 70)]]),
    "I": (0, [[_p(0, 0), _p(0, 140)]]),
    "L": (80, [[_p(0, 0), _p(0, 140), _p(80, 140)]]),
    "M": (120, [[_p(0, 140), _p(0, 0), _p(60, 100), _p(120, 0), _p(120, 140)]]),
    "N": (100, [[_p(0, 140), _p(0, 0), _p(100, 140), _p(100, 0)]]),
    "O": (140, [[_a(70, 70, 70, 70, 0, 360, 48)]]),
    "P": (90, [[_p(0, 140), _p(0, 0), _p(50, 0), _a(50, 40, 40, 40, 270, 450), _p(0, 80)]]),
    "R": (90, [[_p(0, 140), _p(0, 0), _p(50, 0), _a(50, 40, 40, 40, 270, 450), _p(0, 80)],
               [_p(40, 80), _p(90, 140)]]),
    "S": (100, [[_a(50, 35, 45, 35, 330, 90, 24), _a(50, 105, 50, 35, 270, 510, 30)]]),
    "T": (100, [[_p(0, 0), _p(100, 0)], [_p(50, 0), _p(50, 140)]]),
    "U": (100, [[_p(0, 0), _p(0, 90), _a(50, 90, 50, 50, 180, 0, 24), _p(100, 0)]]),
    "v": (80, [[_p(0, 50), _p(40, 140), _p(80, 50)]]),
    "0": (90, [[_a(45, 70, 45, 70, 0, 360, 40)]]),
    "1": (50, [[_p(10, 30), _p(40, 0), _p(40, 140)]]),
    "2": (90, [[_a(45, 40, 45, 40, 180, 385, 20), _p(0, 140), _p(90, 140)]]),
    "3": (90, [[_a(45, 35, 45, 35, 200, 450, 24), _a(45, 105, 45, 35, 270, 520, 24)]]),
    "4": (100, [[_p(70, 140), _p(70, 0), _p(0, 100), _p(95, 100)]]),
    "5": (90, [[_p(85, 0), _p(10, 0), _p(5, 62), _a(45, 95, 45, 45, 235, 520, 28)]]),
    "6": (90, [[_p(70, 0), _p(12, 65)], [_a(45, 95, 45, 45, 0, 360, 40)]]),
    "7": (90, [[_p(0, 0), _p(90, 0), _p(30, 140)]]),
    "8": (90, [[_a(45, 35, 40, 35, 0, 360, 36)], [_a(45, 105, 45, 35, 0, 360, 40)]]),
    "9": (90, [[_a(45, 45, 45, 45, 0, 360, 40)], [_p(78, 75), _p(20, 140)]]),
    ".": (10, [[_p(5, 136)]]),
    "·": (10, [[_p(5, 75)]]),
    "↑": (80, [[_p(40, 140), _p(40, 0)], [_p(0, 40), _p(40, 0), _p(80, 40)]]),
    "↓": (80, [[_p(40, 0), _p(40, 140)], [_p(0, 100), _p(40, 140), _p(80, 100)]]),
    " ": (60, []),
}


def glyph(F, ch, _cache={}):
    """(advance, polylines of points) of `ch` in F's fixed point, in units."""
    key = (F.fb, ch)
    if key not in _cache:
        adv, polys = G[ch]
        out = []
        for poly in polys:
            pts = []
            for it in poly:
                if it[0] == "p":
                    pts.append((F.fx(it[1], 10), F.fx(it[2], 10)))
                    continue
                _, cx, cy, rx, ry, t0, t1, n = it
                cx, cy, rx, ry = F.fx(cx, 10), F.fx(cy, 10), F.fx(rx, 10), F.fx(ry, 10)
                for k in range(n + 1):
                    s, c = F.sincos_deg(t0 * n + (t1 - t0) * k, n)
                    pts.append((cx + F.mul(rx, c), cy + F.mul(ry, s)))
            out.append(pts)
        _cache[key] = (F.fx(adv, 10), out)
    return _cache[key]


def capsule(F, cov, x1, y1, x2, y2, r, w, h):
    """Coverage (0..one, anti-aliased) of a segment with radius r, kept per
    pixel as the maximum over the strokes of one shape."""
    one, half = F.one, F.half
    x0, x3 = (min(x1, x2) - r - 2 * one) >> F.fb, (max(x1, x2) + r + 2 * one) >> F.fb
    y0, y3 = (min(y1, y2) - r - 2 * one) >> F.fb, (max(y1, y2) + r + 2 * one) >> F.fb
    dx, dy = x2 - x1, y2 - y1
    l2 = dx * dx + dy * dy
    for py in range(max(0, y0), min(h, y3 + 1)):
        cy = (py << F.fb) + half - y1
        for px in range(max(0, x0), min(w, x3 + 1)):
            cx = (px << F.fb) + half - x1
            if l2 == 0:
                t = 0
            else:
                d = cx * dx + cy * dy
                t = 0 if d <= 0 else one if d >= l2 else (d << F.fb) // l2
            a = r + half - F.hypot(cx - F.mul(t, dx), cy - F.mul(t, dy))
            if a > 0:
                a = min(one, a)
                if cov.get((px, py), 0) < a:
                    cov[(px, py)] = a


def text_width(F, s, scale, track):
    return F.mul(sum(glyph(F, ch)[0] + track for ch in s), scale) - F.mul(track, scale)


def text_polys(F, s, x, y, scale, track):
    """Per glyph of s: its polylines placed at pen x, cap top y."""
    out, pen = [], x
    for ch in s:
        adv, polys = glyph(F, ch)
        out.append([[(pen + F.mul(px, scale), y + F.mul(py, scale)) for px, py in poly] for poly in polys])
        pen += F.mul(adv + track, scale)
    return out


def polys_coverage(F, cov, polys, r, w, h):
    for pts in polys:
        if len(pts) == 1:
            pts = pts * 2
        for (ax, ay), (bx, by) in zip(pts, pts[1:]):
            capsule(F, cov, ax, ay, bx, by, r, w, h)


# kernel/logo.c's four strokes, in 1/8 pixel of its grid (x1, y1, x2, y2;
# the two back strokes first), and its radii: the y coordinates are scaled
# by RX/RY, the logo's aspect correction.
STROKES = [(95, 57, 139, 169), (182, 23, 229, 122), (59, 123, 95, 57), (139, 169, 182, 23)]
RX, RY = 20, 23
LX0, LX1 = 59 - RX, 229 + RX


# ---- The boot menu's wallpaper (1.1), at 32 fraction bits ----

W, H = 1024, 768
M = Fixed(32)
BG_TOP, BG_BOT = M.rgb((0x0B, 0x12, 0x20)), M.rgb((0x0F, 0x2A, 0x4A))
ACCENT, ACCENT2 = M.rgb((0x1E, 0x90, 0xFF)), M.rgb((0x22, 0xD3, 0xEE))
TEXT, MUTED = M.rgb((0xE6, 0xED, 0xF3)), M.rgb((0x8B, 0x9B, 0xB4))

# The layout (O2, the plan's section F), in one place. The boxes are
# inclusive pixel rectangles x0, y0, x1, y1; the drawn header, hint and
# version must lie inside theirs (checked before the file is written).
# 1.2: the version box holds any two-digit major and minor version (it
# held only "v1.1" before), see WORST_VERSIONS.
LAYOUT = {
    "header": [308, 36, 715, 257],
    "panel": [224, 276, 800, 492],
    "hint": [391, 507, 632, 519],
    "version": [942, 727, 996, 740],
}
# 1.2: the widest versions the version box must hold, checked on every run.
# The text is right-aligned at W - 28, so its left edge depends only on its
# total advance and its right edge only on its last glyph: "4" has the
# widest advance (10 units; "1" 5, the other digits 9), and every last
# digit is tried. Measured over all 10,000 strings v00.00-v99.99, their
# union is the box above.
WORST_VERSIONS = ["44.4" + d for d in "0123456789"]
PX0, PY0, PX1, PY1 = LAYOUT["panel"]
PANEL_RADIUS = 14
# The header: the logo's top and its px per 1/8-pixel unit, the title's cap
# top, scale, stroke radius (MADIRE, then OS) and tracking, the tagline's
# cap top, scale and stroke radius. The hint sits 16 px under the panel, the
# horizon's top 44 px under the hint.
LOGO_TOP, LOGO_S = 36, M.fx(74, 100)
TITLE_Y, TITLE_S, TITLE_R, OS_R, TITLE_TRACK = M.fx(190), M.fx(23, 10), M.fx(25, 10), M.fx(125, 100), M.fx(6)
TAG_Y, TAG_S, TAG_R = M.fx(246), M.fx(78, 100), M.fx(75, 100)
TAGLINE = "SIMPLE · SECURE · FURTHER TOGETHER"
HINT = "↑ ↓ SELECT  ·  ENTER BOOT"


class Canvas:
    def __init__(self):
        self.img = [[None] * W for _ in range(H)]
        self.boxes = {}

    def paint(self, cov, colour_at, name):
        img = self.img
        for (x, y), a in cov.items():
            img[y][x] = M.mix(img[y][x], colour_at(x, y), a)
        if cov:
            xs = [k[0] for k in cov]
            ys = [k[1] for k in cov]
            b = self.boxes.get(name, [W, H, 0, 0])
            self.boxes[name] = [min(b[0], min(xs)), min(b[1], min(ys)), max(b[2], max(xs)), max(b[3], max(ys))]


def text_coverage(s, x, y, scale, r, track, weight_of=None):
    cov = {}
    for i, polys in enumerate(text_polys(M, s, x, y, scale, track)):
        polys_coverage(M, cov, polys, weight_of(i) if weight_of else r, W, H)
    return cov


def draw_text(cv, s, x, y, scale, r, track, colour, name, weight_of=None):
    cv.paint(text_coverage(s, x, y, scale, r, track, weight_of), lambda X, Y: colour, name)


def version_at(cv, version, paint=True):
    """The version, right-aligned at W - 28 on the bottom line; drawn, or
    (paint=False) only measured: its inclusive pixel box."""
    vs = "v" + version
    scale, track = M.fx(82, 100), M.fx(3)
    args = (vs, M.fx(W - 28) - text_width(M, vs, scale, track), M.fx(H - 28) - M.mul(M.fx(14), scale),
            scale, M.fx(72, 100), track)
    if paint:
        draw_text(cv, *args, MUTED, "version")
        return None
    cov = text_coverage(*args)
    xs = [k[0] for k in cov]
    ys = [k[1] for k in cov]
    return [min(xs), min(ys), max(xs), max(ys)]


def check_layout(cv):
    """Every box on screen, no two boxes overlapping, and the worst-case
    versions inside the version box (1.2)."""
    for name, (x0, y0, x1, y1) in LAYOUT.items():
        if not (0 <= x0 <= x1 < W and 0 <= y0 <= y1 < H):
            sys.exit(f"make-wallpaper.py: the {name} layout box {x0},{y0}-{x1},{y1} is not on screen")
    names = sorted(LAYOUT)
    for i, a in enumerate(names):
        for b in names[i + 1:]:
            (ax0, ay0, ax1, ay1), (bx0, by0, bx1, by1) = LAYOUT[a], LAYOUT[b]
            if ax0 <= bx1 and bx0 <= ax1 and ay0 <= by1 and by0 <= ay1:
                sys.exit(f"make-wallpaper.py: the {a} and {b} layout boxes overlap")
    lx0, ly0, lx1, ly1 = LAYOUT["version"]
    for v in WORST_VERSIONS:
        x0, y0, x1, y1 = version_at(cv, v, paint=False)
        if not (lx0 <= x0 and ly0 <= y0 and x1 <= lx1 and y1 <= ly1):
            sys.exit(f"make-wallpaper.py: the worst-case version v{v} drawn at {x0},{y0}-{x1},{y1} "
                     f"is outside its layout box {lx0},{ly0}-{lx1},{ly1}")


def centred(cv, s, y, scale, r, track, colour, name, weight_of=None):
    draw_text(cv, s, M.fx(512) - text_width(M, s, scale, track) // 2, y, scale, r, track, colour, name,
              weight_of)


def background(cv):
    """The navy gradient, the soft glow behind the logo and the horizon arc.
    The glow is exp(-(dx/300)^2) exp(-(dy/170)^2), one factor per column
    and per row."""
    arc_r = M.fx(1500)
    arc_cy = M.fx(PY1 + 16 + 11 + 44) + arc_r
    gx = [M.expneg(M.div(M.fx(x - 512), M.fx(300)) ** 2 >> M.fb) for x in range(W)]
    gy = [M.expneg(M.div(M.fx(y - LOGO_TOP - 118), M.fx(170)) ** 2 >> M.fb) for y in range(H)]
    dark, rim = M.rgb((0x05, 0x0A, 0x14)), M.rgb((0x9F, 0xDB, 0xFF))
    for y in range(H):
        base = M.mix(BG_TOP, BG_BOT, M.fx(y, H - 1))
        row = cv.img[y]
        for x in range(W):
            c = M.mix(base, ACCENT, M.mul(M.fx(10, 100), M.mul(gx[x], gy[y])))
            dc = M.hypot(M.fx(x - 512), M.fx(y) - arc_cy) - arc_r
            if dc > 0:
                c = M.mix(c, ACCENT, M.mul(M.fx(42, 100), M.expneg(M.div(dc, M.fx(55)))))
            else:
                c = M.mix(c, dark, M.mul(min(M.one, M.div(-dc, M.fx(90))), M.fx(85, 100)))
                c = M.mix(c, ACCENT, M.mul(M.fx(25, 100), M.expneg(M.div(-dc, M.fx(18)))))
            q = M.div(dc, M.fx(16, 10))
            q2 = M.mul(q, q)
            if q2 < (50 << M.fb):           # beyond: below 2^-72 of a channel
                c = M.mix(c, rim, M.mul(M.fx(9, 10), M.expneg(q2)))
            row[x] = c


def logo(cv):
    """The M: kernel/logo.c's capsules, smooth, the accent gradient from left
    to right, the two back strokes darker."""
    ls = LOGO_S
    lox = M.fx(512) - M.mul(M.fx(LX0 + LX1, 2), ls)
    loy = M.fx(LOGO_TOP) - M.mul(M.fx(23 * RX, RY) - M.fx(RX), ls)
    back_col = M.rgb((0x0B, 0x2A, 0x6A))
    for i, (x1, y1, x2, y2) in enumerate(STROKES):
        cov = {}
        capsule(M, cov, lox + x1 * ls, loy + M.mul(M.fx(y1 * RX, RY), ls), lox + x2 * ls,
                loy + M.mul(M.fx(y2 * RX, RY), ls), RX * ls, W, H)

        def col(x, y, back=i < 2):
            t = max(0, min(M.one, M.div(M.fx(x) - (lox + LX0 * ls), (LX1 - LX0) * ls)))
            c = M.mix(ACCENT, ACCENT2, t)
            return M.mix(c, back_col, M.fx(35, 100)) if back else c
        cv.paint(cov, col, "header")


def panel(cv):
    """The rounded panel Limine's menu window lines up with."""
    rad = M.fx(PANEL_RADIUS)

    def rr_dist(x, y):
        cx = min(max(x, M.fx(PX0) + rad), M.fx(PX1) - rad)
        cy = min(max(y, M.fx(PY0) + rad), M.fx(PY1) - rad)
        return M.hypot(x - cx, y - cy) - rad
    img, shade = cv.img, M.rgb((0x07, 0x0E, 0x1C))
    for y in range(PY0 - 2, PY1 + 3):
        for x in range(PX0 - 2, PX1 + 3):
            d = rr_dist(M.fx(x) + M.half, M.fx(y) + M.half)
            img[y][x] = M.mix(img[y][x], shade, M.mul(M.fx(62, 100), max(0, min(M.one, M.half - d))))
            img[y][x] = M.mix(img[y][x], ACCENT,
                              M.mul(M.fx(45, 100), max(0, M.one - M.div(abs(d + M.half), M.fx(9, 10)))))


def chunk(kind, data):
    return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xffffffff)


def png_bytes(cv, version):
    raw = bytearray()
    for row in cv.img:
        raw.append(0)
        for c in row:
            raw += bytes(M.byte(v) for v in c)
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", W, H, 8, 2, 0, 0, 0))
            + chunk(b"tEXt", b"Madire-Version\x00" + version.encode("ascii"))
            + chunk(b"IDAT", zlib.compress(bytes(raw), 9)) + chunk(b"IEND", b""))


def render(version):
    cv = Canvas()
    background(cv)
    logo(cv)
    centred(cv, "MADIRE OS", TITLE_Y, TITLE_S, TITLE_R, TITLE_TRACK, TEXT, "header",
            weight_of=lambda i: TITLE_R if i < 6 else OS_R)
    centred(cv, TAGLINE, TAG_Y, TAG_S, TAG_R, M.fx(7), MUTED, "header")
    panel(cv)
    centred(cv, HINT, M.fx(PY1 + 16), M.fx(78, 100), M.fx(7, 10), M.fx(4), MUTED, "hint")
    version_at(cv, version)
    check_layout(cv)
    for name, (x0, y0, x1, y1) in cv.boxes.items():
        lx0, ly0, lx1, ly1 = LAYOUT[name]
        if not (lx0 <= x0 and ly0 <= y0 and x1 <= lx1 and y1 <= ly1):
            sys.exit(f"make-wallpaper.py: the {name} drawn at {x0},{y0}-{x1},{y1} is outside "
                     f"its layout box {lx0},{ly0}-{lx1},{ly1}")
    return png_bytes(cv, version), cv.boxes


# ---- The kernel's wallpaper and splash (1.2 stage 8), at 8 fraction bits ----
# The rules kernel/display/wallpaper_core.h states; the kernel's C follows
# the same steps. Sizes in dp become pixels as (dp x s + 2) / 4 (the
# Design Guide, section 6), s the screen's scale step (4, 5, 6 or 8).

K = Fixed(8)
WORDMARK = "MADIRE OS"
SPLASH_TEXT = "Booting Madire OS"
# The three curves: each a circle through (0, H yl / 1000) and (W, H yr /
# 1000) with its centre at x = W cx / 1000 (far right, below the screen),
# so it rises from the lower left and flattens towards the upper right.
CURVES = [(1200, 1000, 400), (1350, 1060, 600), (1120, 560, 40)]
FONT_FILES = {16: "ter-u16n.bdf", 20: "ter-u20n.bdf", 24: "ter-u24n.bdf", 32: "ter-u32n.bdf"}
FONT_SIZES = [(8, 16), (10, 20), (12, 24), (16, 32)]


def read_tokens(root=ROOT):
    """{token: (r, g, b)}: the adopted column of the Design Guide's colour
    table (docs/design/design-guide.md, section 2)."""
    out = {}
    for line in open(os.path.join(root, "docs/design/design-guide.md"), encoding="utf-8"):
        cols = [c.strip() for c in line.strip().strip("|").split("|")]
        if len(cols) >= 4 and re.fullmatch(r"`[a-z.]+`", cols[0]):
            m = re.match(r"`#([0-9A-Fa-f]{6})`", cols[3])
            if m:
                v = int(m.group(1), 16)
                out[cols[0].strip("`")] = (v >> 16, (v >> 8) & 0xFF, v & 0xFF)
    return out


def scale_layout(width, height):
    """(s, cell width, cell height): the console's scale and font (the
    rules of kernel/display/fbcon_core.h: from the height, then smaller
    until 80 x 25 cells fit)."""
    want = 0 if height < 900 else 1 if height < 1080 else 2 if height < 1440 else 3
    for i in range(want, -1, -1):
        cw, ch = FONT_SIZES[i]
        if 80 * cw <= width and 25 * ch <= height:
            return (4, 5, 6, 8)[i], cw, ch
    return None


def dpx(dp, s):
    return (dp * s + 2) // 4


def kernel_point(v):
    """A 32-fraction-bit value in the kernel's 8, halves away from zero."""
    q = (abs(v) + (1 << 23)) >> 24
    return q if v >= 0 else -q


def kernel_glyph(ch):
    """(advance, polylines) of a wordmark glyph in the kernel's fixed point:
    the points computed at 32 bits (the arcs' sines and cosines), then
    rounded to 8. The kernel compiles exactly these (--emit-c)."""
    adv, polys = glyph(M, ch)
    return kernel_point(adv), [[(kernel_point(x), kernel_point(y)) for x, y in poly] for poly in polys]


def curve_circles(width, height):
    """[(cx, cy, r)] in whole pixels: integer maths only (floor division)."""
    out = []
    for fcx, fyl, fyr in CURVES:
        cx, yl, yr = width * fcx // 1000, height * fyl // 1000, height * fyr // 1000
        # |(0, yl) - c| = |(width, yr) - c| solved for c's y.
        cy = (cx * cx - (cx - width) ** 2 + yl * yl - yr * yr) // (2 * (yl - yr))
        out.append((cx, cy, math.isqrt(cx * cx + (cy - yl) ** 2)))
    return out


def wallpaper(width, height, tok=None):
    """The kernel's wallpaper as RGB bytes: bg.deep at the top to bg.low at
    the bottom, brightened towards bg.base around the upper centre, crossed
    by three glowing curves (a halo of accent.glow, a core of
    accent.light)."""
    tok = tok or read_tokens()
    s = scale_layout(width, height)[0]
    deep, low, base = K.rgb(tok["bg.deep"]), K.rgb(tok["bg.low"]), K.rgb(tok["bg.base"])
    glow, light = K.rgb(tok["accent.glow"]), K.rgb(tok["accent.light"])
    halo_w = dpx(40, s) << K.fb             # the halo's reach
    core_r = 32 * s                         # the core's radius: s / 8 px
    halo_a, core_a = K.fx(30, 100), K.fx(70, 100)
    ccx, ccy = width // 2, height * 3 // 10
    rx, ry = width * 3 // 5, height // 2
    circles = [(cx << K.fb, cy << K.fb, r << K.fb) for cx, cy, r in curve_circles(width, height)]
    out = bytearray(width * height * 3)
    o = 0
    for y in range(height):
        row = K.mix(deep, low, K.fx(y, height - 1))
        dy = y - ccy
        qy = K.div(dy * dy, ry * ry)
        py = (y << K.fb) + K.half
        for x in range(width):
            dx = x - ccx
            w = K.one - K.div(dx * dx, rx * rx) - qy
            c = K.mix(row, base, K.mul(w, w)) if w > 0 else row
            px = (x << K.fb) + K.half
            for cx, cy, r in circles:
                d = abs(K.hypot(px - cx, py - cy) - r)
                if d < halo_w:
                    t = halo_w - d
                    c = K.mix(c, glow, K.mul(halo_a, t * t // halo_w * K.one // halo_w))
                    cov = core_r + K.half - d
                    if cov > 0:
                        c = K.mix(c, light, K.mul(core_a, min(K.one, cov)))
            out[o:o + 3] = bytes(K.byte(v) for v in c)
            o += 3
    return out


def splash_layout(width, height):
    """The splash's places, as wallpaper_core.c computes them."""
    s, cw, ch = scale_layout(width, height)
    ls = K.fx(74 * s, 400)                  # px per 1/8-pixel unit of the logo
    ts = K.fx(23 * s, 40)                   # px per unit of the wordmark
    logo_h = K.mul(K.fx(167), ls)
    cap_h = K.mul(K.fx(14), ts)
    g1, g2 = dpx(28, s) << K.fb, dpx(40, s) << K.fb
    top = ((height << K.fb) - (logo_h + g1 + cap_h + g2 + (ch << K.fb))) // 2
    lay = {"s": s, "cw": cw, "ch": ch, "ls": ls, "ts": ts,
           "lox": K.fx(width, 2) - K.mul(K.fx(LX0 + LX1, 2), ls),
           "loy": top - K.mul(K.fx(23 * RX, RY) - K.fx(RX), ls),
           "title_y": top + logo_h + g1,
           "r_title": K.fx(25 * s, 40), "r_os": K.fx(125 * s, 400), "track": K.fx(6)}
    gap, spacing = dpx(6, s), dpx(8, s)
    tw = len(SPLASH_TEXT) * cw
    total = tw + gap + 2 * spacing + dpx(3, s)
    lay["text_x"] = (width - total) // 2
    lay["text_y"] = (lay["title_y"] + cap_h + g2) >> K.fb
    rd = K.fx(3 * s, 8)                     # the dots' radius: 1.5 dp
    x0 = ((lay["text_x"] + tw + gap) << K.fb) + rd
    lay["dots"] = [(x0 + ((i * spacing) << K.fb), (lay["text_y"] << K.fb) + (ch << K.fb) * 11 // 16)
                   for i in range(3)]
    lay["rd"] = rd
    (fx0, cy), (lx, _) = lay["dots"][0], lay["dots"][2]
    lay["dots_box"] = ((fx0 - rd >> K.fb) - 2, (cy - rd >> K.fb) - 2,
                       (lx + rd >> K.fb) + 2, (cy + rd >> K.fb) + 2)
    return lay


def read_bdf(path):
    """{code point: [row bits, leftmost pixel the highest bit]}, width."""
    glyphs, width, enc, rows = {}, None, None, None
    with open(path, encoding="ascii") as f:
        for line in f:
            w = line.split()
            if not w:
                continue
            if w[0] == "FONTBOUNDINGBOX":
                width = int(w[1])
            elif w[0] == "ENCODING":
                enc = int(w[1])
            elif w[0] == "BITMAP":
                rows = []
            elif w[0] == "ENDCHAR":
                glyphs[enc], enc, rows = rows, None, None
            elif rows is not None:
                rows.append(int(w[0], 16) >> (len(w[0]) * 4 - width))
    return glyphs, width


class Screen:
    """RGB bytes, painted as the kernel paints its back buffer: each paint
    reads the pixel's bytes, mixes in 8 fraction bits and rounds back."""

    def __init__(self, width, height, pix):
        self.w, self.h, self.pix = width, height, pix

    def paint(self, cov, colour_at):
        for (x, y), a in cov.items():
            o = (y * self.w + x) * 3
            c = K.mix(K.rgb(self.pix[o:o + 3]), colour_at(x), a)
            self.pix[o:o + 3] = bytes(K.byte(v) for v in c)


def splash_dots(scr, lay, phase, tok):
    lit, dim = K.rgb(tok["text"]), K.rgb(tok["surface.active"])
    for i, (cx, cy) in enumerate(lay["dots"]):
        cov = {}
        capsule(K, cov, cx, cy, cx, cy, lay["rd"], scr.w, scr.h)
        scr.paint(cov, lambda x, c=lit if i < phase else dim: c)


def splash(width, height, phase, fontdir=None, tok=None):
    """The kernel's splash as RGB bytes, with `phase` dots lit (0-3)."""
    return splash_phases(width, height, [phase], fontdir, tok)[phase]


def splash_phases(width, height, phases, fontdir=None, tok=None):
    """{phase: the splash's RGB bytes}: the wallpaper, the logo, the
    wordmark and the text drawn once, then the dots for each phase."""
    tok = tok or read_tokens()
    lay = splash_layout(width, height)
    scr = Screen(width, height, wallpaper(width, height, tok))
    ls = lay["ls"]
    adeep, alight, deep = K.rgb(tok["accent.deep"]), K.rgb(tok["accent.light"]), K.rgb(tok["bg.deep"])
    for i, (x1, y1, x2, y2) in enumerate(STROKES):
        cov = {}
        capsule(K, cov, lay["lox"] + x1 * ls, lay["loy"] + K.mul(K.fx(y1 * RX, RY), ls),
                lay["lox"] + x2 * ls, lay["loy"] + K.mul(K.fx(y2 * RX, RY), ls), RX * ls, width, height)

        def col(x, back=i < 2):
            t = max(0, min(K.one, K.div((x << K.fb) - (lay["lox"] + LX0 * ls), (LX1 - LX0) * ls)))
            c = K.mix(adeep, alight, t)
            return K.mix(c, deep, K.fx(35, 100)) if back else c
        scr.paint(cov, col)
    ts, track = lay["ts"], lay["track"]
    adv = sum(kernel_glyph(ch)[0] + track for ch in WORDMARK)
    pen = K.fx(width, 2) - (K.mul(adv, ts) - K.mul(track, ts)) // 2
    text = K.rgb(tok["text"])
    for i, ch in enumerate(WORDMARK):
        a, polys = kernel_glyph(ch)
        cov = {}
        placed = [[(pen + K.mul(px, ts), lay["title_y"] + K.mul(py, ts)) for px, py in poly] for poly in polys]
        polys_coverage(K, cov, placed, lay["r_title"] if i < 6 else lay["r_os"], width, height)
        scr.paint(cov, lambda x: text)
        pen += K.mul(a + track, ts)
    glyphs, fw = read_bdf(os.path.join(fontdir or os.path.join(ROOT, "third_party/terminus"),
                                       FONT_FILES[lay["ch"]]))
    muted = bytes(tok["text.muted"])
    for i, chr_ in enumerate(SPLASH_TEXT):
        rows = glyphs[ord(chr_)]
        for y, bits in enumerate(rows):
            for x in range(fw):
                if (bits >> (fw - 1 - x)) & 1:
                    o = ((lay["text_y"] + y) * width + lay["text_x"] + i * fw + x) * 3
                    scr.pix[o:o + 3] = muted
    out = {}
    for phase in phases:
        dots = Screen(width, height, bytearray(scr.pix))
        splash_dots(dots, lay, phase, tok)
        out[phase] = bytes(dots.pix)
    return out


def emit_c(path):
    """The splash's generated data for wallpaper_core.c."""
    lines = ["/* Generated by scripts/make-wallpaper.py --emit-c (1.2 stage 8): never edited,",
             " * never committed. kernel/logo.c's strokes (a copy; part static checks it",
             " * against logo.c) and the wordmark's polylines, in units of 1/256 (the",
             " * arcs computed in integers at 32 fraction bits, then rounded). */",
             f"#define SPLASH_LOGO_RX {RX}", f"#define SPLASH_LOGO_RY {RY}",
             "static const int32_t splash_logo_strokes[4][4] = {"]
    lines += [f"    {{ {x1}, {y1}, {x2}, {y2} }}," for x1, y1, x2, y2 in STROKES]
    lines.append("};")
    pts, poly_tab, glyph_tab = [], [], []
    for ch in WORDMARK:
        adv, polys = kernel_glyph(ch)
        glyph_tab.append((adv, len(poly_tab), len(polys), ch))
        for poly in polys:
            poly_tab.append((len(pts), len(poly)))
            pts.extend(poly)
    lines.append(f"static const int16_t splash_wm_points[{len(pts)}][2] = {{")
    for i in range(0, len(pts), 6):
        lines.append("    " + " ".join(f"{{ {x}, {y} }}," for x, y in pts[i:i + 6]))
    lines.append("};")
    lines.append(f"static const struct wp_poly splash_wm_polys[{len(poly_tab)}] = {{")
    lines += [f"    {{ {a}, {n} }}," for a, n in poly_tab]
    lines.append("};")
    lines.append(f"static const struct wp_glyph splash_wm_glyphs[{len(glyph_tab)}] = {{")
    lines += [f"    {{ {adv}, {first}, {n} }},   /* '{ch}' */" for adv, first, n, ch in glyph_tab]
    lines.append("};")
    with open(path, "w", encoding="ascii", newline="\n") as f:
        f.write("\n".join(lines) + "\n")


def main():
    ap = argparse.ArgumentParser(description="Madire OS wallpapers (integer maths)")
    ap.add_argument("out", nargs="?", help="the PNG (or with --splash the PPM) to write")
    ap.add_argument("--version", help="the version shown bottom right, e.g. 1.1")
    ap.add_argument("--layout-json", help="also write the layout boxes to this JSON file")
    ap.add_argument("--print-strokes", action="store_true", help="print the M's strokes and exit")
    ap.add_argument("--emit-c", metavar="OUT_H", help="write the splash's generated data and exit")
    ap.add_argument("--splash", metavar="WxH", help="render the kernel's splash (a PPM)")
    ap.add_argument("--phase", type=int, default=0, choices=range(4), help="the splash's lit dots")
    ap.add_argument("--fontdir", help="the BDF files (default: third_party/terminus)")
    a = ap.parse_args()
    if a.print_strokes:
        for s in STROKES:
            print(*s)
        return
    if a.emit_c:
        emit_c(a.emit_c)
        return
    if a.splash:
        m = re.fullmatch(r"([0-9]+)x([0-9]+)", a.splash)
        if not m or not a.out:
            ap.error("--splash WxH OUT.ppm")
        w, h = int(m.group(1)), int(m.group(2))
        if scale_layout(w, h) is None:
            ap.error(f"--splash {a.splash}: smaller than the console's 80 x 25 cells")
        pix = splash(w, h, a.phase, a.fontdir)
        with open(a.out, "wb") as f:
            f.write(b"P6\n%d %d\n255\n" % (w, h) + pix)
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
