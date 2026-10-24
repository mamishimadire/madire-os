#!/usr/bin/env python3
"""The desktop toolkit's reference renderer (1.3 stage 2; standard library
only).

Draws the test scenes of tests/desktopdump.c independently of the C
toolkit: from the rules written in kernel/display/ui_draw.h, ui_icon.h,
ui_widget.h and ui_rect.h, the Design Guide's colour tables
(docs/design/design-guide.md, section 2, read here, never tokens.h), the
pinned BDF files in third_party/terminus/ (never the generated C fonts),
and its own description of the icons (ICONS below, in dp, the arcs
computed here). It reuses scripts/make-wallpaper.py's fixed point (class
Fixed at 8 fraction bits: fx, mul, mix, byte), its capsule coverage, the
logo's strokes (STROKES, RX, RY) and tests/fbcon_ref.py's BDF reader and
code page 437 table. It never calls or translates the C toolkit.

    desktop_ref.py WIDTH HEIGHT SCENE OUT.ppm   the scene as a PPM (P6)
    desktop_ref.py --tokens                     the token check (part static)

SCENE is prims, icons or widgets (tests/desktopdump.c describes them).
--tokens compares kernel/display/tokens.h with both of the guide's colour
tables, and the colour constants of kernel/display/wallpaper_core.h and
pointer_core.h with their tokens (TOKEN_TWINS), printing "TOKENS ..." lines
and exiting 1 on any difference.
"""
import importlib.util
import math
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, "..")
sys.path.insert(0, HERE)
sys.dont_write_bytecode = True
import fbcon_ref  # noqa: E402

_spec = importlib.util.spec_from_file_location("make_wallpaper", os.path.join(ROOT, "scripts", "make-wallpaper.py"))
MW = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(MW)
K = MW.K                                    # Fixed(8): 24.8

FONTDIR = os.path.join(ROOT, "third_party", "terminus")
FONT_PX = {4: 16, 5: 20, 6: 24, 8: 32}      # the font height per scale step (Design Guide, section 3)
SCALES = (4, 5, 6, 8)

# The colour constants that predate tokens.h (1.2), each with its token.
TOKEN_TWINS = {
    "kernel/display/wallpaper_core.h": {
        "WP_BG_DEEP": "bg.deep", "WP_BG_BASE": "bg.base", "WP_BG_LOW": "bg.low",
        "WP_SURFACE_ACTIVE": "surface.active", "WP_ACCENT_GLOW": "accent.glow",
        "WP_ACCENT_LIGHT": "accent.light", "WP_ACCENT_DEEP": "accent.deep",
        "WP_TEXT": "text", "WP_TEXT_MUTED": "text.muted",
    },
    "kernel/display/pointer_core.h": {
        "POINTER_EDGE": "bg.deep", "POINTER_FILL": "text", "POINTER_SPINE": "accent",
    },
}


# ---- Tokens ----

def guide_tokens(root=ROOT):
    """[(token, 0xRRGGBB, table number)]: every row of the guide's tables
    whose header has an "Adopted" column, in order (the colour tables)."""
    out, adopted, table = [], None, 0
    for line in open(os.path.join(root, "docs/design/design-guide.md"), encoding="utf-8"):
        s = line.strip()
        if not s.startswith("|"):
            adopted = None
            continue
        cols = [c.strip() for c in s.strip("|").split("|")]
        if cols[0] == "Token":
            adopted = next((i for i, c in enumerate(cols) if c.startswith("Adopted")), None)
            table += adopted is not None
            continue
        if adopted is None or not re.fullmatch(r"`[a-z.]+`", cols[0]):
            continue
        m = re.match(r"`#([0-9A-Fa-f]{6})`", cols[adopted])
        if not m:
            raise ValueError("Design Guide: no adopted colour for %s" % cols[0])
        out.append((cols[0].strip("`"), int(m.group(1), 16), table))
    return out


def colours(root=ROOT):
    return {t: v for t, v, _ in guide_tokens(root)}


def c_defines(path):
    """{name: value} of the `#define NAME 0xRRGGBBu` lines of a header."""
    src = open(os.path.join(ROOT, path), encoding="ascii").read()
    return {n: int(v, 16) for n, v in re.findall(r"^#define\s+(\w+)\s+0x([0-9A-Fa-f]{6})u\b", src, re.M)}


def tokens_check():
    rows = guide_tokens()
    bad = 0
    tok = {t: v for t, v, _ in rows}
    if len(tok) != len(rows):
        print("TOKENS FAIL: a token appears twice in the guide's tables")
        bad += 1
    defs = c_defines("kernel/display/tokens.h")
    want = {"UI_" + t.upper().replace(".", "_"): (t, v) for t, v in tok.items()}
    for name, (t, v) in want.items():
        if defs.get(name) != v:
            print("TOKENS FAIL: %s (%s) is %s in tokens.h, #%06X in the guide" %
                  (name, t, "missing" if name not in defs else "0x%06X" % defs[name], v))
            bad += 1
    for name in sorted(set(defs) - set(want)):
        print("TOKENS FAIL: %s in tokens.h is no token of the guide" % name)
        bad += 1
    tables = sorted({n for _, _, n in rows})
    print("TOKENS guide: %d tokens in %d tables (%s); tokens.h: %d defines" %
          (len(tok), len(tables), ", ".join("%d in table %d" % (sum(1 for r in rows if r[2] == n), n)
                                             for n in tables), len(defs)))
    for path, twins in TOKEN_TWINS.items():
        got = c_defines(path)
        for name in sorted(set(got) - set(twins)):
            print("TOKENS FAIL: %s in %s has no token assigned (TOKEN_TWINS)" % (name, path))
            bad += 1
        for name, t in twins.items():
            ok = got.get(name) == tok.get(t)
            print("TOKENS twin %s %s = %s -> %s %s" % (path.split("/")[-1], name,
                  "0x%06X" % got[name] if name in got else "missing", t, "equal" if ok else "DIFFERENT"))
            bad += not ok
    print("TOKENS result: %s" % ("PASS" if bad == 0 else "FAIL (%d)" % bad))
    return 0 if bad == 0 else 1


# ---- The screen ----

def px(dp, s):
    return (dp * s + 2) // 4


def rgb3(v):
    return bytes(((v >> 16) & 0xFF, (v >> 8) & 0xFF, v & 0xFF))


def meet(a, b):
    """The overlap of two (x, y, w, h), or None."""
    if a is None or b is None or a[2] <= 0 or a[3] <= 0 or b[2] <= 0 or b[3] <= 0:
        return None
    x0, y0 = max(a[0], b[0]), max(a[1], b[1])
    x1, y1 = min(a[0] + a[2], b[0] + b[2]), min(a[1] + a[3], b[1] + b[3])
    return (x0, y0, x1 - x0, y1 - y0) if x1 > x0 and y1 > y0 else None


class Screen:
    def __init__(self, w, h, fill):
        self.w, self.h = w, h
        self.pix = bytearray(rgb3(fill) * (w * h))
        self.clip = (0, 0, w, h)

    def set_clip(self, r):
        self.clip = meet(r, (0, 0, self.w, self.h))

    def span(self, y, x0, x1, c):
        k = self.clip
        if k is None or not k[1] <= y < k[1] + k[3]:
            return
        x0, x1 = max(x0, k[0]), min(x1, k[0] + k[2])
        if x1 > x0:
            o = (y * self.w + x0) * 3
            self.pix[o:o + (x1 - x0) * 3] = rgb3(c) * (x1 - x0)

    def put(self, x, y, c):
        self.span(y, x, x + 1, c)

    def blend(self, x, y, c, a):
        k = self.clip
        if a <= 0 or k is None or not (k[0] <= x < k[0] + k[2] and k[1] <= y < k[1] + k[3]):
            return
        o = (y * self.w + x) * 3
        v = K.mix(K.rgb(self.pix[o:o + 3]), K.rgb(rgb3(c)), a)
        self.pix[o:o + 3] = bytes(K.byte(t) for t in v)


def fill(scr, r, c):
    if r[2] <= 0 or r[3] <= 0:
        return
    for y in range(r[1], r[1] + r[3]):
        scr.span(y, r[0], r[0] + r[2], c)


TL, TR, BL, BR = 1, 2, 4, 8


def radius_of(r, radius):
    return max(0, min(radius, r[2] // 2, r[3] // 2))


def inset(R, j):
    """Columns left out of a corner square's row j (from the corner): the
    pixels whose centre is outside the quarter-circle of radius R."""
    q = 4 * R * R - (2 * R - 2 * j - 1) ** 2
    return max(0, -(-(2 * R - 1 - math.isqrt(q)) // 2))


def row_of(r, R, corners, y):
    """[x0, x1) of row y inside the rounded rectangle, or None."""
    if r[2] <= 0 or r[3] <= 0 or not r[1] <= y < r[1] + r[3]:
        return None
    top, bottom = y - r[1], r[1] + r[3] - 1 - y
    left = right = 0
    if top < R:
        left, right = (inset(R, top) if corners & TL else 0), (inset(R, top) if corners & TR else 0)
    elif bottom < R:
        left, right = (inset(R, bottom) if corners & BL else 0), (inset(R, bottom) if corners & BR else 0)
    return r[0] + left, r[0] + r[2] - right


def fill_round(scr, r, radius, corners, c):
    R = radius_of(r, radius)
    for y in range(r[1], r[1] + max(r[3], 0)):
        sp = row_of(r, R, corners, y)
        if sp:
            scr.span(y, sp[0], sp[1], c)


def inflate(r, d):
    x0, y0, x1, y1 = r[0] - d, r[1] - d, r[0] + r[2] + d, r[1] + r[3] + d
    return (x0, y0, x1 - x0, y1 - y0) if x1 > x0 and y1 > y0 and r[2] > 0 and r[3] > 0 else (0, 0, 0, 0)


def frame_round(scr, r, radius, width, corners, c):
    if r[2] <= 0 or r[3] <= 0 or width <= 0:
        return
    R = radius_of(r, radius)
    inner = inflate(r, -width)
    Ri = radius_of(inner, R - width)
    for y in range(r[1], r[1] + r[3]):
        a = row_of(r, R, corners, y)
        if not a:
            continue
        b = row_of(inner, Ri, corners, y)
        if b and b[0] < b[1]:
            scr.span(y, a[0], b[0], c)
            scr.span(y, b[1], a[1], c)
        else:
            scr.span(y, a[0], a[1], c)


_fonts = {}


def font(s, bold):
    key = (s, bold)
    if key not in _fonts:
        h = FONT_PX[s]
        glyphs, w, fh = fbcon_ref.read_bdf(os.path.join(FONTDIR, "ter-u%d%s.bdf" % (h, "b" if bold else "n")))
        if fh != h:
            raise ValueError("ter-u%d: height %d" % (h, fh))
        _fonts[key] = (glyphs, w, h)
    return _fonts[key]


def text(scr, f, x, y, s, c):
    glyphs, w, h = f
    for i, byte in enumerate(s):
        rows = glyphs[fbcon_ref.code_point(byte)]
        for gy in range(h):
            for gx in range(w):
                if (rows[gy] >> (w - 1 - gx)) & 1:
                    scr.put(x + i * w + gx, y + gy, c)


# ---- Icons (Design Guide, section 5) ----

def arc(cx, cy, r, a0, a1, step):
    """Points of a circle from a0 to a1 degrees clockwise from the top."""
    return [(cx + r * math.sin(math.radians(a)), cy - r * math.cos(math.radians(a))) for a in range(a0, a1 + 1, step)]


def box(x0, y0, x1, y1):
    return [(x0, y0), (x1, y0), (x1, y1), (x0, y1), (x0, y0)]


ICONS = {   # in dp on the 24 dp grid; the M is the logo's strokes (below)
    "console": [box(3, 5, 21, 19), [(7, 9), (10, 12), (7, 15)], [(12, 15), (16, 15)]],
    "about": [arc(12, 12, 9, 0, 360, 15), [(12, 7.5)], [(12, 11), (12, 16.5)]],
    "restart": [arc(12, 13, 8, 60, 360, 15), [(9, 2), (12, 5), (9, 8)]],
    "calculator": [box(5, 3, 19, 21), [(8, 7), (16, 7)], [(9, 12)], [(15, 12)], [(9, 17)], [(15, 17)]],
    "close": [[(6, 6), (18, 18)], [(18, 6), (6, 18)]],
    "plus": [[(12, 5), (12, 19)], [(5, 12), (19, 12)]],
    "minus": [[(5, 12), (19, 12)]],
    "times": [[(7, 7), (17, 17)], [(17, 7), (7, 17)]],
    "divide": [[(5, 12), (19, 12)], [(12, 6.5)], [(12, 17.5)]],
    "backspace": [[(8, 5), (21, 5), (21, 19), (8, 19), (2, 12), (8, 5)], [(12, 9.5), (17, 14.5)],
                  [(17, 9.5), (12, 14.5)]],
    "chevron": [[(9, 5), (16, 12), (9, 19)]],
}
ICON_ORDER = ["M", "console", "about", "restart", "calculator", "close", "plus", "minus", "times", "divide",
              "backspace", "chevron"]


def unit(v):
    return int(math.floor(8 * v + 0.5))      # 1/8 dp


def icon(scr, name, x, y, p, c):
    """Icon `name` in the p x p box at (x, y) (ui_icon.h's rules)."""
    ox, oy, caps = x << 8, y << 8, []
    if name == "M":
        m = K.fx(22 * p, 24 * 210)
        mid, half = K.fx(p, 2), K.fx(167, 2)
        for x1, y1, x2, y2 in MW.STROKES:
            caps.append((ox + mid + (x1 - 144) * m, oy + mid + K.mul(K.fx(y1 * MW.RX, MW.RY) - half, m),
                         ox + mid + (x2 - 144) * m, oy + mid + K.mul(K.fx(y2 * MW.RX, MW.RY) - half, m),
                         MW.RX * m))
    else:
        r = K.fx(8 * p, 192)
        for poly in ICONS[name]:
            pts = [(ox + K.fx(unit(a) * p, 192), oy + K.fx(unit(b) * p, 192)) for a, b in poly]
            segs = [(pts[0], pts[0])] if len(pts) == 1 else list(zip(pts, pts[1:]))
            caps += [(a[0], a[1], b[0], b[1], r) for a, b in segs]
    cov = {}
    for x1, y1, x2, y2, r in caps:
        MW.capsule(K, cov, x1, y1, x2, y2, r, scr.w, scr.h)
    for (qx, qy), a in sorted(cov.items()):
        if x <= qx < x + p and y <= qy < y + p:
            scr.blend(qx, qy, c, a)


# ---- Rectangles and the damage list (ui_rect.h) ----

def union(a, b):
    if a is None or a[2] <= 0 or a[3] <= 0:
        return b
    if b is None or b[2] <= 0 or b[3] <= 0:
        return a
    x0, y0 = min(a[0], b[0]), min(a[1], b[1])
    return (x0, y0, max(a[0] + a[2], b[0] + b[2]) - x0, max(a[1] + a[3], b[1] + b[3]) - y0)


def inside(a, b):
    return a[0] >= b[0] and a[1] >= b[1] and a[0] + a[2] <= b[0] + b[2] and a[1] + a[3] <= b[1] + b[3]


class Damage:
    MAX = 32

    def __init__(self, w, h):
        self.screen, self.rects = (0, 0, w, h), []

    def add(self, r):
        r = meet(r, self.screen)
        if r is None or any(inside(r, q) for q in self.rects):
            return
        if len(self.rects) < self.MAX:
            self.rects.append(r)
            return
        for q in self.rects:
            r = union(r, q)
        self.rects = [r]


# ---- Widgets (ui_widget.h) ----

def label(scr, r, f, s, c, align):
    glyphs, w, h = f
    tw = len(s) * w
    x = r[0] if align == "left" else r[0] + (r[2] - tw) // 2 if align == "centre" else r[0] + r[2] - tw
    keep = scr.clip
    scr.clip = meet(keep, r)
    if scr.clip is not None:
        text(scr, f, x, r[1] + (r[3] - h) // 2, s, c)
    scr.clip = keep


NONE, UNSET = "none", "unset"
STATES = ("normal", "hover", "pressed")


def styles(tok):
    t = tok
    return {   # fill and foreground per state, bold, radius in dp, corners (Design Guide, sections 8 and 9)
        "close": ([NONE, t["danger"], t["danger.pressed"]], [t["text"]] * 3, False, 0, 0),
        "secondary": ([t["surface.active"], UNSET, UNSET], [t["text"], UNSET, UNSET], False, 4, 15),
        "primary": ([t["accent"], UNSET, UNSET], [t["text.on.accent"], UNSET, UNSET], True, 4, 15),
        "key": ([t["surface.active"], UNSET, t["accent"]], [t["text"], UNSET, t["text.on.accent"]], False, 4, 15),
        "equals": ([t["accent"], UNSET, t["accent"]], [t["text.on.accent"], UNSET, t["text.on.accent"]], False, 4, 15),
    }


def button(scr, tok, r, style, state, focused, s, lab=None, ic=None, icon_dp=0):
    fills, fgs, bold, radius_dp, corners = style
    k = STATES.index(state)
    if fills[k] == UNSET or fgs[k] == UNSET:
        return False
    R = radius_of(r, px(radius_dp, s))
    if fills[k] != NONE:
        fill_round(scr, r, R, corners, fills[k])
    if ic is not None:
        p = px(icon_dp, s)
        keep = scr.clip
        scr.clip = meet(keep, r)
        if scr.clip is not None:
            icon(scr, ic, r[0] + (r[2] - p) // 2, r[1] + (r[3] - p) // 2, p, fgs[k])
        scr.clip = keep
    elif lab is not None:
        label(scr, r, font(s, bold), lab, fgs[k], "centre")
    if focused:
        g = w = px(2, s)
        frame_round(scr, inflate(r, g + w), R + g + w, w, corners, tok["accent"])
    return True


# ---- The scenes (tests/desktopdump.c) ----

class Flow:
    def __init__(self, w, margin, gap):
        self.w, self.m, self.gap = w, margin, gap
        self.x = self.y = margin
        self.row_h = 0

    def brk(self):
        if self.x != self.m:
            self.x, self.y, self.row_h = self.m, self.y + self.row_h + self.gap, 0

    def place(self, w, h):
        if self.x != self.m and self.x + w > self.w - self.m:
            self.brk()
        r = (self.x, self.y, w, h)
        self.x += w + self.gap
        self.row_h = max(self.row_h, h)
        return r

    def bottom(self):
        return self.y + self.row_h


def scene_prims(scr, tok, s):
    W, H = scr.w, scr.h
    d = lambda n: px(n, s)  # noqa: E731
    fill(scr, (-d(8), d(8), d(40), d(24)), tok["surface"])
    fill(scr, (W - d(24), d(8), d(48), d(24)), tok["surface.window"])
    fill(scr, (d(48), -d(10), d(32), d(24)), tok["surface.active"])
    f = Flow(W, d(16), d(8))
    f.y = d(40)
    for rad in (0, 2, 4, 6, 12, 40):
        fill_round(scr, f.place(d(64), d(40)), d(rad), 15, tok["surface.window"])
    for corners in (TL, TR, BL, BR, TL | BR, 0):
        fill_round(scr, f.place(d(64), d(40)), d(12), corners, tok["surface"])
    fill_round(scr, f.place(d(33) + 1, d(21) + 1), d(8), 15, tok["surface.active"])
    fill_round(scr, f.place(d(7), d(40)), d(6), 15, tok["surface.active"])
    f.brk()
    edges = ("border.window", "border.panel", "border")
    for i, (rad, wd) in enumerate(((0, 1), (6, 1), (6, 2), (12, 3), (40, 2), (2, 4), (6, 30))):
        frame_round(scr, f.place(d(64), d(40)), d(rad), d(wd), 15, tok[edges[i % 3]])
    frame_round(scr, f.place(d(64), d(40)), d(12), d(2), TR | BL, tok["border.inactive"])
    f.brk()
    line = b"Madire 1.3 \xfa Ag{}|_@ 0189 \x10"
    ph = d(8) + sum(2 * (FONT_PX[k] + d(4)) for k in SCALES)
    panel = f.place(W - 2 * d(16), ph)
    fill(scr, panel, tok["surface.window"])
    ty = panel[1] + d(4)
    for k in SCALES:
        for bold in (False, True):
            text(scr, font(k, bold), panel[0] + d(8), ty, line, tok["text" if bold else "text.muted"])
            ty += FONT_PX[k] + d(4)
    f.brk()
    k = f.place(d(120), d(30))
    fill(scr, k, tok["surface"])
    scr.set_clip(k)
    fill_round(scr, (k[0] - d(20), k[1] - d(10), d(100), d(50)), d(12), 15, tok["surface.active"])
    text(scr, font(4, False), k[0] + d(60), k[1] + d(4), b"clipped text here", tok["text"])
    scr.set_clip((0, 0, W, H))
    f.brk()
    ya = f.place(d(100), d(52))[1]
    a = Damage(W, H)
    for r in ((d(16), ya, d(40), d(20)), (d(26), ya + d(5), d(10), d(5)), (d(76), ya, d(30), d(30)),
              (-d(10), ya + d(40), d(30), d(10)), (W - d(20), ya, d(60), d(10)), (0, 0, 0, 5)):
        a.add(r)
    for r in a.rects:
        frame_round(scr, r, 0, 1, 0, tok["border.panel"])
    f.brk()
    yb = f.place(d(210), d(20))[1]
    b = Damage(W, H)
    for i in range(33):
        b.add((d(16) + i * d(6), yb + (i % 3) * d(6), d(4), d(4)))
    for r in b.rects:
        frame_round(scr, r, 0, 1, 0, tok["border.window"])
    return "bottom %d, damage lists %d and %d rectangles, the full list's (%d, %d) %dx%d" % (
        f.bottom(), len(a.rects), len(b.rects), *b.rects[0])


ICON_PAIRS = [("text", "surface.window"), ("text", "surface"), ("text", "surface.active"), ("text", "danger"),
              ("text", "danger.pressed"), ("text.on.accent", "accent"), ("accent", "surface.bar"),
              ("accent", "surface.active"), ("accent", "surface")]


def scene_icons(scr, tok, s):
    W = scr.w
    f = Flow(W, 8, 4)
    k = 0
    for sc, size in ((4, 16), (4, 24), (5, 16), (5, 24), (6, 16), (6, 24), (8, 16), (8, 24), (s, 32), (s, 48)):
        p = px(size, sc)
        for name in ICON_ORDER:
            cell = f.place(p + 4, p + 4)
            fg, bg = ICON_PAIRS[k % 9]
            fill(scr, cell, tok[bg])
            icon(scr, name, cell[0] + 2, cell[1] + 2, p, tok[fg])
            k += 1
        f.brk()
    p = px(24, s)
    y = f.place(p + 4, p + 4)[1]
    fill(scr, (-(p // 2) - 2, y, p + 4, p + 4), tok["surface.window"])
    icon(scr, "about", -(p // 2), y + 2, p, tok["text"])
    fill(scr, (W - p // 2 - 2, y, p + 4, p + 4), tok["surface.window"])
    icon(scr, "restart", W - p // 2, y + 2, p, tok["text"])
    cell = (W // 2, y, p + 4, p + 4)
    fill(scr, cell, tok["surface"])
    scr.set_clip((cell[0], cell[1], cell[2] // 2, cell[3]))
    icon(scr, "M", cell[0] + 2, cell[1] + 2, p, tok["accent"])
    scr.set_clip((0, 0, W, scr.h))
    return "%d icons, bottom %d" % (k, f.bottom())


LABEL_PAIRS = [("text", "surface.window"), ("text.muted", "surface.window"), ("text", "surface"),
               ("text", "surface.bar"), ("text", "surface.active"), ("text.on.accent", "accent")]
BUTTONS = [   # style, w and h in dp, label, icon, icon dp, state, focused (tests/desktopdump.c)
    ("close", 32, 32, None, "close", 16, "normal", False),
    ("close", 32, 32, None, "close", 16, "hover", False),
    ("close", 32, 32, None, "close", 16, "pressed", False),
    ("secondary", 96, 32, b"Cancel", None, 0, "normal", False),
    ("secondary", 96, 32, b"Cancel", None, 0, "normal", True),
    ("primary", 96, 32, b"Restart", None, 0, "normal", False),
    ("primary", 96, 32, b"Restart", None, 0, "normal", True),
    ("key", 52, 40, b"7", None, 0, "normal", False),
    ("key", 52, 40, b"7", None, 0, "pressed", False),
    ("key", 52, 40, b"7", None, 0, "normal", True),
    ("key", 52, 40, None, "times", 16, "normal", False),
    ("key", 52, 40, None, "times", 16, "pressed", False),
    ("key", 52, 40, None, "backspace", 16, "normal", False),
    ("key", 52, 40, None, "backspace", 16, "pressed", True),
    ("equals", 52, 40, b"=", None, 0, "normal", False),
    ("equals", 52, 40, b"=", None, 0, "pressed", False),
]


def scene_widgets(scr, tok, s):
    W = scr.w
    d = lambda n: px(n, s)  # noqa: E731
    f = Flow(W, d(16), d(8))
    for i in range(8):
        r = f.place(d(160), d(32) if i < 7 else d(8))
        fg, bg = LABEL_PAIRS[i % 6]
        fill(scr, r, tok[bg])
        s_ = b"A label longer than its box" if i == 6 else b"Low" if i == 7 else b"Label"
        label(scr, r, font(s, i % 2 == 1), s_, tok[fg], ("left", "centre", "right")[i // 2] if i < 6 else "centre")
    f.brk()
    st, drawn = styles(tok), 0
    for sc in (s, 5):
        pad = px(6, sc)
        for style, w, h, lab, ic, idp, state, focused in BUTTONS:
            bw, bh = px(w, sc), px(h, sc)
            cell = f.place(bw + 2 * pad, bh + 2 * pad)
            fill(scr, cell, tok["surface.window"])
            drawn += button(scr, tok, (cell[0] + pad, cell[1] + pad, bw, bh), st[style], state, focused, sc,
                            lab, ic, idp)
        f.brk()
    return "%d buttons drawn, bottom %d" % (drawn, f.bottom())


SCENES = {"prims": scene_prims, "icons": scene_icons, "widgets": scene_widgets}


def scale_of(w, h):
    """The console's scale (fbcon_core.h): from the height, then smaller until 80 x 25 cells fit."""
    want = 4 if h < 900 else 5 if h < 1080 else 6 if h < 1440 else 8
    for s in (8, 6, 5, 4):
        if s <= want and 80 * (FONT_PX[s] // 2) <= w and 25 * FONT_PX[s] <= h:
            return s
    return None


def render(w, h, scene):
    tok = colours()
    s = scale_of(w, h)
    if s is None or scene not in SCENES:
        raise ValueError("no scene %s on %dx%d" % (scene, w, h))
    scr = Screen(w, h, tok["bg.deep"])
    desc = SCENES[scene](scr, tok, s)
    return bytes(scr.pix), "scale %d %%, %s" % (s * 25, desc)


def main():
    if sys.argv[1:] == ["--tokens"]:
        return tokens_check()
    if len(sys.argv) != 5:
        print(__doc__)
        return 1
    w, h, scene, out = int(sys.argv[1]), int(sys.argv[2]), sys.argv[3], sys.argv[4]
    pix, desc = render(w, h, scene)
    with open(out, "wb") as f:
        f.write(b"P6\n%d %d\n255\n" % (w, h) + pix)
    print("REF %s %dx%d: %s" % (scene, w, h, desc))
    return 0


if __name__ == "__main__":
    sys.exit(main())
