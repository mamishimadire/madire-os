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
    desktop_ref.py --coverage-table OUT.h       test_coverage's reference table
    desktop_ref.py --contrast PAIRS             the contrast check (part static, 1.3 stage 4a)

SCENE is prims, icons or widgets (tests/desktopdump.c describes them),
or (1.3 stage 3b) background or taskbar, the desktop's own pictures from
the rules of kernel/display/desktop_core.h (the wallpaper and the M at the
splash's place, from make-wallpaper.py, as the splash's reference does;
no text lies on the desktop's wallpaper). desktop(w, h, clock) is the
whole desktop for tests/qmp_desktop.py. 1.3 stage 4a: the window scenes
windows, windows_hover, windows_pressed and windows_exposed (two About
windows, from kernel/display/ui_window.h's and ui_about.h's rules; the
last is the first scene, drawn by the C side through the exposure path);
WM, the window manager's rules written again (tests/qmp_desktop.py
predicts the kernel's desktoplog lines and its windows with it);
desktop_windows(w, h, clock, windows), the desktop with windows on it.
1.3 stage 4b: the Console window's scenes console, console_inactive,
console_about and console_exposed (kernel/display/ui_console.h's rules: the
grid in tests/fbcon_ref.py's way, drawn only inside the window's inner
rounded shape; a fixed grid, DUMP_GRID; the last scene is console_about,
drawn by the C side through the exposure path); desktop_windows also
draws Console windows (a window with a "grid": the cells, the palette and
the cursor, as shadow_grid reads them from a saved vga_shadow).
--contrast reads the colour pairs tests/desktopdump.c recorded and checks
each against the Design Guide's section 12 table, recomputing the ratios
from tokens.h (CONTRAST lines; exit 1 on any failure).
--tokens compares kernel/display/tokens.h with both of the guide's colour
tables, and the colour constants of kernel/display/wallpaper_core.h and
pointer_core.h with their tokens (TOKEN_TWINS), printing "TOKENS ..." lines
and exiting 1 on any difference. --coverage-table (1.3 stage 3) writes the
capsule coverage that tests/unit/test_coverage.c compares
kernel/display/coverage_core.c with: its fixed-seed capsules, each one's
pixel box and the coverage of every pixel of the box and a 2-pixel margin
around it, from make-wallpaper.py's capsule rule (build/gen/coverage_ref.h,
never committed).
"""
import importlib.util
import math
import os
import re
import struct
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


def twin_defines(path, ui):
    """{name: (value, token define or None)} of a header's colour constants:
    `#define NAME 0xRRGGBBu`, or `#define NAME UI_TOKEN` (1.3 stage 3: the
    1.2 constants taken from tokens.h) with its value from tokens.h's
    defines `ui` (None for a name tokens.h does not define)."""
    src = open(os.path.join(ROOT, path), encoding="ascii").read()
    out = {n: (v, None) for n, v in c_defines(path).items()}
    for n, ref in re.findall(r"^#define\s+(\w+)\s+(UI_\w+)\s*(?:/\*.*\*/)?$", src, re.M):
        out[n] = (ui.get(ref), ref)
    return out


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
        got = twin_defines(path, defs)
        for name in sorted(set(got) - set(twins)):
            print("TOKENS FAIL: %s in %s has no token assigned (TOKEN_TWINS)" % (name, path))
            bad += 1
        for name, t in twins.items():
            v, ref = got.get(name, (None, None))
            # A constant named by a token must name its own token's define.
            ok = v is not None and v == tok.get(t) and ref in (None, "UI_" + t.upper().replace(".", "_"))
            print("TOKENS twin %s %s = %s -> %s %s" % (path.split("/")[-1], name,
                  "0x%06X" % v if v is not None else "missing", t, "equal" if ok else "DIFFERENT"))
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


# ---- The desktop (1.3 stage 3b; kernel/display/desktop_core.h's rules) ----

FIXED_CLOCK = b"09:41"                      # the taskbar scene's time (tests/desktopdump.c)


def layout(w, h):
    """The taskbar's places; the M is the splash's (make-wallpaper.py's
    splash_layout)."""
    s = scale_of(w, h)
    f = font(s, False)
    bar = px(40, s)
    return {
        "s": s, "font": f, "splash": MW.splash_layout(w, h),
        "bar": (0, h - bar, w, bar), "edge": (0, h - bar, w, px(1, s)),
        "m": ((bar - px(24, s)) // 2, h - bar + (bar - px(24, s)) // 2, px(24, s)),
        "clock": (w - px(16, s) - 5 * f[1], h - bar + (bar - f[2]) // 2),
    }


def paint_k(scr, cov, colour_at):
    """Coverage painted with a colour in 24.8 (the logo's gradient)."""
    for (x, y), a in cov.items():
        if a > 0:
            o = (y * scr.w + x) * 3
            v = K.mix(K.rgb(scr.pix[o:o + 3]), colour_at(x), a)
            scr.pix[o:o + 3] = bytes(K.byte(t) for t in v)


def scene_background(scr, tok, s):
    """The wallpaper with the M alone, at the splash's place and size, in
    the splash's gradient (the owner's decision in stage 3b: no wordmark,
    tagline or hint on the desktop)."""
    W, H = scr.w, scr.h
    lay = MW.splash_layout(W, H)
    scr.pix = bytearray(MW.wallpaper(W, H))
    ls, lox, loy = lay["ls"], lay["lox"], lay["loy"]
    adeep, alight, deep = K.rgb(rgb3(tok["accent.deep"])), K.rgb(rgb3(tok["accent.light"])), K.rgb(rgb3(tok["bg.deep"]))
    for i, (x1, y1, x2, y2) in enumerate(MW.STROKES):
        cov = {}
        MW.capsule(K, cov, lox + x1 * ls, loy + K.mul(K.fx(y1 * MW.RX, MW.RY), ls),
                   lox + x2 * ls, loy + K.mul(K.fx(y2 * MW.RX, MW.RY), ls), MW.RX * ls, W, H)

        def col(x, back=i < 2):
            t = max(0, min(K.one, K.div((x << 8) - (lox + MW.LX0 * ls), (MW.LX1 - MW.LX0) * ls)))
            c = K.mix(adeep, alight, t)
            return K.mix(c, deep, K.fx(35, 100)) if back else c
        paint_k(scr, cov, col)
    return "the M at the splash's place (origin %d, %d in 1/256 px)" % (lox, loy)


def taskbar(scr, tok, clock):
    L = layout(scr.w, scr.h)
    fill(scr, L["bar"], tok["surface.bar"])
    fill(scr, L["edge"], tok["surface.bar.edge"])
    mx, my, mp = L["m"]
    icon(scr, "M", mx, my, mp, tok["accent"])
    text(scr, L["font"], L["clock"][0], L["clock"][1], clock, tok["text"])
    return L


def scene_taskbar(scr, tok, s):
    L = taskbar(scr, tok, FIXED_CLOCK)
    return "the bar at y %d (%d high), the clock at (%d, %d)" % (L["bar"][1], L["bar"][3], *L["clock"])


_backgrounds = {}


def desktop(w, h, clock):
    """The whole desktop as tests/qmp_desktop.py expects it on the screen:
    the background, the taskbar with the clock `clock` (bytes, "HH:MM")."""
    tok = colours()
    if (w, h) not in _backgrounds:
        scr = Screen(w, h, tok["bg.deep"])
        scene_background(scr, tok, scale_of(w, h))
        _backgrounds[(w, h)] = bytes(scr.pix)
    scr = Screen(w, h, tok["bg.deep"])
    scr.pix = bytearray(_backgrounds[(w, h)])
    taskbar(scr, tok, clock)
    return bytes(scr.pix)


# ---- Windows and the About window (1.3 stage 4a; kernel/display/ui_window.h, ui_about.h) ----

ABOUT_TITLE = b"About Madire OS"
ABOUT_LABELS = [b"Version", b"Boot loader", b"Display", b"Memory", b"Mode"]
ABOUT_MODE = b"Live (memory only)"
ABOUT_FOOTER = b"SIMPLE \xfa SECURE \xfa FURTHER TOGETHER"
VALUE_MAX = 64                              # a value's buffer, its NUL included (ui_about.h)
DUMP_LOADER = b"Limine 12.9.3 (a boot loader name much longer than the column)"   # tests/desktopdump.c
DUMP_DISPLAY = b"2560x1440x32"


def about_size(s):
    return px(352, s), px(377, s)


def fit(f, s, max_px, size=VALUE_MAX):
    """ui_text_fit: s unchanged if it fits (and its buffer), else its
    longest beginning that leaves room for "..." and the dots."""
    w = f[1]
    held = s[:size - 1]
    if len(s) <= size - 1 and len(s) * w <= max_px:
        return s
    keep = min(max(max_px // w - 3, 0), len(held))
    if keep + 3 > size - 1:
        keep = max(size - 1 - 3, 0)
    return held[:keep] + b"..."


def about_values(s, version, loader, display, memory):
    f = font(s, False)
    col = px(328, s) - px(136, s)
    return [fit(f, v, col) for v in (version, loader, display, memory, ABOUT_MODE)]


def logo(scr, tok, lox, loy, ls):
    """The M in the splash's gradient, its origin and px per unit given."""
    adeep, alight, deep = K.rgb(rgb3(tok["accent.deep"])), K.rgb(rgb3(tok["accent.light"])), K.rgb(rgb3(tok["bg.deep"]))
    for i, (x1, y1, x2, y2) in enumerate(MW.STROKES):
        cov = {}
        MW.capsule(K, cov, lox + x1 * ls, loy + K.mul(K.fx(y1 * MW.RX, MW.RY), ls),
                   lox + x2 * ls, loy + K.mul(K.fx(y2 * MW.RX, MW.RY), ls), MW.RX * ls, scr.w, scr.h)

        def col(x, back=i < 2):
            t = max(0, min(K.one, K.div((x << 8) - (lox + MW.LX0 * ls), (MW.LX1 - MW.LX0) * ls)))
            c = K.mix(adeep, alight, t)
            return K.mix(c, deep, K.fx(35, 100)) if back else c
        paint_k(scr, cov, col)


def about_client(scr, tok, s, r, values):
    """The About window's content inside the window at r (ui_about.h)."""
    P = px(64, s)
    bx, by = r[0] + (r[2] - P) // 2, r[1] + px(56, s)
    m = K.fx(56 * P, 64 * 210)
    logo(scr, tok, (bx << 8) + K.fx(P, 2) - 144 * m, (by << 8) + K.fx(P, 2) - K.mul(K.fx(167, 2), m), m)
    ts, track = K.fx(4 * s, 14), K.fx(6)
    adv = sum(MW.kernel_glyph(ch)[0] + track for ch in MW.WORDMARK)
    pen = (r[0] << 8) + K.fx(r[2], 2) - (K.mul(adv, ts) - K.mul(track, ts)) // 2
    top = (r[1] + px(136, s)) << 8
    text_c = K.rgb(rgb3(tok["text"]))
    for i, ch in enumerate(MW.WORDMARK):
        a, polys = MW.kernel_glyph(ch)
        cov = {}
        placed = [[(pen + K.mul(qx, ts), top + K.mul(qy, ts)) for qx, qy in poly] for poly in polys]
        MW.polys_coverage(K, cov, placed, K.fx(50 * s, 161) if i < 6 else K.fx(25 * s, 161), scr.w, scr.h)
        paint_k(scr, cov, lambda x: text_c)
        pen += K.mul(a + track, ts)
    f = font(s, False)
    for i in range(5):
        y = r[1] + px(176 + 28 * i, s)
        text(scr, f, r[0] + px(24, s), y, ABOUT_LABELS[i], tok["text.muted"])
        text(scr, f, r[0] + px(136, s), y, values[i], tok["text"])
    fill(scr, (r[0] + px(24, s), r[1] + px(320, s), px(328, s) - px(24, s), px(1, s)), tok["border"])
    text(scr, f, r[0] + (r[2] - len(ABOUT_FOOTER) * f[1]) // 2, r[1] + px(337, s), ABOUT_FOOTER, tok["text.muted"])


def window(scr, tok, s, r, title, icon_name, active, close, client):
    """A window at r (ui_window.h): the frame, the client, the title bar,
    the close button in state `close` (normal, hover, pressed)."""
    R, tb = px(6, s), px(32, s)
    fill_round(scr, r, R, 15, tok["surface.window"])
    frame_round(scr, r, R, px(1, s), 15, tok["border.window" if active else "border.inactive"])
    client(scr, r)
    ip = px(16, s)
    icon(scr, icon_name, r[0] + px(8, s), r[1] + (tb - ip) // 2, ip, tok["text"])
    cr = (r[0] + r[2] - tb, r[1], tb, tb)
    tx = r[0] + px(32, s)
    keep = scr.clip
    scr.clip = meet(keep, (tx, r[1], cr[0] - px(8, s) - tx, tb))
    if scr.clip is not None:
        text(scr, font(s, True), tx, r[1] + (tb - FONT_PX[s]) // 2, title, tok["text" if active else "text.muted"])
    scr.clip = keep
    style = ([NONE, tok["danger"], tok["danger.pressed"]], [tok["text"]] * 3, False, 6, TR)
    button(scr, tok, cr, style, close, False, s, ic="close", icon_dp=16)


def about_window(scr, tok, s, r, active, close, values):
    window(scr, tok, s, r, ABOUT_TITLE, "about", active, close,
           lambda scr_, r_: about_client(scr_, tok, s, r_, values))


# ---- The Console window (1.3 stage 4b; kernel/display/ui_console.h) ----

CONSOLE_TITLE = b"Console"
# The VGA's 16 default colours as 6-bit DAC values (tests/desktopdump.c's grid).
VGA_PALETTE = [(0, 0, 0), (0, 0, 42), (0, 42, 0), (0, 42, 42), (42, 0, 0), (42, 0, 42), (42, 21, 0),
               (42, 42, 42), (21, 21, 21), (21, 21, 63), (21, 63, 21), (21, 63, 63), (63, 21, 21),
               (63, 21, 63), (63, 63, 21), (63, 63, 63)]


def console_size(s):
    """The Console window: the 80 x 25 grid, the edge at both sides and
    the bottom, the title bar above."""
    f = font(s, False)
    return 80 * f[1] + 2 * px(1, s), px(32, s) + 25 * f[2] + px(1, s)


def dump_grid():
    """tests/desktopdump.c's grid: cell i (row r, column c) holds character
    (7i + r) mod 256 in colour slot (r + c) mod 16 on slot (r div 5) mod 8;
    both bottom corner cells a full block (0xDB), white on blue; the cursor
    in the bottom-left one."""
    cells = []
    for i in range(2000):
        row, col = divmod(i, 80)
        cells.append(((i * 7 + row) & 0xFF) | ((row + col) % 16) << 8 | ((row // 5) % 8) << 12)
    cells[24 * 80] = cells[24 * 80 + 79] = 0x1FDB
    return cells, VGA_PALETTE, (True, 24, 0)


def shadow_grid(shadow):
    """The grid of a saved vga_shadow (tests/fbcon_ref.py's layout)."""
    cells = list(struct.unpack_from("<2000H", shadow, 0))
    pal = [tuple(shadow[4000 + 3 * k + c] for c in range(3)) for k in range(16)]
    return cells, pal, (bool(shadow[4048]), shadow[4049], shadow[4050])


def console_client(scr, s, r, grid):
    """The grid in the Console window at r: each cell as tests/fbcon_ref.py
    draws it, its top-left at (r.x + edge, r.y + title bar); a pixel only
    inside the window's inner rounded shape (r shrunk by the edge, radius
    6 dp - edge) and the clip."""
    cells, pal, (cur_on, cur_row, cur_col) = grid
    glyphs, cw, ch = font(s, False)
    e = px(1, s)
    inner = inflate(r, -e)
    Ri = radius_of(inner, px(6, s) - e)
    gx0, gy0 = r[0] + e, r[1] + px(32, s)
    colours = [bytes(fbcon_ref.widen(v) for v in p) for p in pal]
    first, end = ch * 13 // 16, ch * 15 // 16
    k = scr.clip
    if k is None:
        return
    rows = {}
    for i, ent in enumerate(cells):
        row, col = divmod(i, 80)
        code = fbcon_ref.code_point(ent & 0xFF)
        fgi, bgi = (ent >> 8) & 0x0F, ent >> 12
        cursor = cur_on and row == cur_row and col == cur_col
        x0 = gx0 + col * cw
        for y in range(ch):
            py = gy0 + row * ch + y
            if not k[1] <= py < k[1] + k[3]:
                continue
            sp = row_of(inner, Ri, 15, py)
            if not sp:
                continue
            a, b = max(x0, sp[0], k[0]), min(x0 + cw, sp[1], k[0] + k[2])
            if b <= a:
                continue
            under = cursor and first <= y < end
            key = (code, y, fgi, bgi, under)
            if key not in rows:
                bits, fg, bg = glyphs[code][y], colours[fgi], colours[bgi]
                rows[key] = b"".join(fg if under or (bits >> (cw - 1 - x)) & 1 else bg for x in range(cw))
            o = (py * scr.w + a) * 3
            scr.pix[o:o + (b - a) * 3] = rows[key][(a - x0) * 3:(b - x0) * 3]


def console_window(scr, tok, s, r, active, close, grid):
    window(scr, tok, s, r, CONSOLE_TITLE, "console", active, close,
           lambda scr_, r_: console_client(scr_, s, r_, grid))


class WM:
    """The window manager's rules (ui_window.h), written here again: where
    windows open, the clamp, hit testing, focus and z-order, the drag, the
    close button, Esc. Each change is kept as the kernel's desktoplog line
    would say it (desktop: ...), in the kernel's order."""

    def __init__(self, w, h, s):
        self.w, self.h, self.s = w, h, s
        self.bar = h - px(40, s)
        self.z, self.focus = [], None           # windows: dicts name, w, h, r, esc, close
        self.x, self.y = w // 2, h // 2
        self.left, self.drag, self.pressed = False, None, None
        self.events = []

    def clamp(self, r):
        keep = px(48, self.s)
        x = min(max(r[0], keep - r[2]), self.w - keep)
        y = min(max(r[1], 0), max(self.bar - r[3], 0))
        return (x, y, r[2], r[3])

    def place(self, w, h):
        step = px(24, self.s)
        x, y = int((self.w - w) / 2), max(int((self.bar - h) / 2), 0)
        for _ in range(4):
            if not any(o["r"][0] == x and o["r"][1] == y for o in self.z):
                break
            x, y = x + step, y + step
        return self.clamp((x, y, w, h))

    def close_rect(self, r):
        tb = px(32, self.s)
        return (r[0] + r[2] - tb, r[1], tb, tb)

    @staticmethod
    def inside(r, x, y):
        return r[0] <= x < r[0] + r[2] and r[1] <= y < r[1] + r[3]

    def hit(self, x, y):
        for win in reversed(self.z):
            if self.inside(win["r"], x, y):
                if self.inside(self.close_rect(win["r"]), x, y):
                    return "close", win
                return ("title" if y < win["r"][1] + px(32, self.s) else "body"), win
        return ("taskbar" if y >= self.bar else "desktop"), None

    def ev(self, line):
        self.events.append("desktop: " + line)

    def set_focus(self, win):
        if self.focus is win:
            return
        self.focus = win
        self.ev("focus %s" % win["name"] if win else "focus none")

    def raise_(self, win):
        if self.z[-1] is not win:
            self.z.remove(win)
            self.z.append(win)
            self.ev("raise %s" % win["name"])

    def state(self, win):
        if self.drag:
            return "normal"
        part, at = self.hit(self.x, self.y)
        over = part == "close" and at is win
        if self.pressed:
            return "pressed" if self.pressed is win and over else "normal"
        return "hover" if over and not self.left else "normal"

    def states(self):
        for win in self.z:
            st = self.state(win)
            if st != win["close"]:
                win["close"] = st
                self.ev("close button of %s: %s" % (win["name"], st))

    def open(self, name, w, h, esc):
        win = {"name": name, "w": w, "h": h, "esc": esc, "close": "normal"}
        win["r"] = self.place(w, h)
        self.z.append(win)
        self.ev("open %s at (%d, %d) %dx%d" % ((name,) + win["r"]))
        self.set_focus(win)
        self.states()
        return win

    def close(self, win, why):
        self.z.remove(win)
        if self.pressed is win:
            self.pressed = None
        if self.drag is win:
            self.drag = None
        self.ev("close %s (%s)" % (win["name"], why))
        if self.focus is win:
            self.focus = None
            if self.z:
                self.set_focus(self.z[-1])
            else:
                self.ev("focus none")

    def pointer(self, x, y, down=False, up=False):
        self.x, self.y = x, y
        if self.drag:
            win, (gx, gy, gr) = self.drag, self.grab
            win["r"] = self.clamp((gr[0] + x - gx, gr[1] + y - gy, gr[2], gr[3]))
        if down and not self.left:
            self.left = True
            part, win = self.hit(x, y)
            if part == "desktop":
                self.set_focus(None)
            elif part != "taskbar":
                self.raise_(win)
                self.set_focus(win)
                if part == "close":
                    self.pressed = win
                elif part == "title":
                    self.drag, self.grab = win, (x, y, win["r"])
        if up and self.left:
            self.left = False
            if self.drag:
                win, self.drag = self.drag, None
                if win["r"][:2] != self.grab[2][:2]:
                    self.ev("drag %s to (%d, %d) %dx%d" % ((win["name"],) + win["r"]))
            if self.pressed:
                win, self.pressed = self.pressed, None
                part, at = self.hit(x, y)
                if part == "close" and at is win:
                    self.close(win, "the close button")
        self.states()

    def esc(self):
        win = self.focus
        if not win or not win["esc"]:
            return False
        self.close(win, "Esc")
        self.states()
        return True

    def key(self, console_size, pressed=True, esc=False):
        """1.3 stage 4b (ui_wm_key): one key event; "opened" (no window
        focused, a press: the Console window opened, or raised, and focused;
        not typed), "console" (the Console window focused: the console's) or
        "taken" (nothing more; Esc pressed closes a focused window that
        closes on Esc)."""
        if self.focus is None:
            if not pressed:
                return "taken"
            con = next((w for w in self.z if w["name"] == "Console"), None)
            if con is None:
                self.open("Console", console_size[0], console_size[1], False)
            else:
                self.raise_(con)
                self.set_focus(con)
                self.states()
            return "opened"
        if self.focus["name"] == "Console":
            return "console"
        if pressed and esc:
            self.esc()
        return "taken"


def scene_windows(scr, tok, s, close="normal"):
    """tests/desktopdump.c's window scenes: two About windows on bg.deep,
    placed by the rules (the second where the first's place was taken),
    the first inactive, the second active with its close button in state
    `close`."""
    wm = WM(scr.w, scr.h, s)
    w, h = about_size(s)
    a, b = wm.open("About", w, h, True), wm.open("About", w, h, True)
    values = about_values(s, b"1.3", DUMP_LOADER, DUMP_DISPLAY, b"127 MB")
    about_window(scr, tok, s, a["r"], False, "normal", values)
    about_window(scr, tok, s, b["r"], True, close, values)
    return "close %s; the windows at (%d, %d) and (%d, %d), %dx%d; the boot loader's value %r" % (
        close, a["r"][0], a["r"][1], b["r"][0], b["r"][1], w, h, values[1].decode("latin-1"))


def desktop_windows(w, h, clock, windows):
    """The desktop (desktop()) with windows on it, bottom to top: each a
    dict r, active, close, values (the About window, the only one in 4a);
    1.3 stage 4b: a dict with a "grid" (shadow_grid's) is a Console window."""
    tok = colours()
    s = scale_of(w, h)
    scr = Screen(w, h, tok["bg.deep"])
    scr.pix = bytearray(desktop(w, h, clock))
    for win in windows:
        if win.get("grid") is not None:
            console_window(scr, tok, s, win["r"], win["active"], win["close"], win["grid"])
        else:
            about_window(scr, tok, s, win["r"], win["active"], win["close"], win["values"])
    return bytes(scr.pix)


def scene_console(scr, tok, s, kind):
    """tests/desktopdump.c's Console window scenes on bg.deep, the window
    placed by the rules with DUMP_GRID in it: console (active), console_inactive,
    console_about (the Console window, then an About window opened over it:
    the Console window inactive, the About window active, overlapping it)."""
    wm = WM(scr.w, scr.h, s)
    cw, chh = console_size(s)
    c = wm.open("Console", cw, chh, False)
    grid = dump_grid()
    if kind != "about":
        console_window(scr, tok, s, c["r"], kind == "active", "normal", grid)
        return "%s; the window at (%d, %d), %dx%d" % (kind, c["r"][0], c["r"][1], cw, chh)
    aw, ah = about_size(s)
    a = wm.open("About", aw, ah, True)
    values = about_values(s, b"1.3", DUMP_LOADER, DUMP_DISPLAY, b"127 MB")
    console_window(scr, tok, s, c["r"], False, "normal", grid)
    about_window(scr, tok, s, a["r"], True, "normal", values)
    return "the Console window at (%d, %d), %dx%d, under the About window at (%d, %d)" % (
        c["r"][0], c["r"][1], cw, chh, a["r"][0], a["r"][1])


# ---- The contrast check (1.3 stage 4a; the Design Guide, section 12) ----

RULES = {"T": 4.5, "L": 3.0, "N": 3.0}


def luminance(v):
    def ch(c):
        c /= 255
        return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4
    return 0.2126 * ch(v >> 16 & 255) + 0.7152 * ch(v >> 8 & 255) + 0.0722 * ch(v & 255)


def contrast(a, b):
    la, lb = sorted((luminance(a), luminance(b)), reverse=True)
    return (la + 0.05) / (lb + 0.05)


def contrast_tables(root=ROOT):
    """{"Text": [...], "Icons": [...]}: section 12's two tables, each row
    (foreground, [backgrounds], [stated ratios], rule, result, where)."""
    tables, cur, in12 = {}, None, False
    for line in open(os.path.join(root, "docs/design/design-guide.md"), encoding="utf-8"):
        s = line.strip()
        if s.startswith("## "):
            in12 = s.startswith("## 12.")
            continue
        if not in12:
            continue
        if s == "**Text**":
            cur = tables.setdefault("Text", [])
        elif s == "**Icons and non-text parts**":
            cur = tables.setdefault("Icons", [])
        elif cur is not None and s.startswith("|"):
            cols = [c.strip() for c in s.strip("|").split("|")]
            if len(cols) != 6 or cols[0] == "Foreground" or set(cols[0]) <= set("-"):
                continue
            fg = re.findall(r"`([a-z.]+)`", cols[0])
            bgs = re.findall(r"`([a-z.]+)`", cols[1])
            ratios = [float(v) for v in re.findall(r"(\d+\.\d\d)", cols[3])]
            if len(fg) == 1 and bgs and len(ratios) == len(bgs):
                cur.append((fg[0], bgs, ratios, cols[4], cols[5], cols[2]))
    return tables


def contrast_check(path):
    """Every pair recorded by tests/desktopdump.c (PAIR lines) must be a row
    of section 12's table (Text for text, Icons for icons and state marks)
    whose result is pass, with its stated ratio equal to the ratio
    recomputed here from tokens.h's values (WCAG 2.x, two decimals), at or
    above its rule's minimum."""
    defs = c_defines("kernel/display/tokens.h")
    names = {}
    for t, _, _ in guide_tokens():
        v = defs.get("UI_" + t.upper().replace(".", "_"))
        if v is not None:
            names.setdefault(v, []).append(t)
    tables = contrast_tables()
    pairs, scenes, lost = {}, set(), 0
    for line in open(path, encoding="ascii"):
        w = line.split()
        if w[:2] == ["PAIRS", "LOST"]:
            lost += 1
        if len(w) != 6 or w[0] != "PAIR":
            continue
        key = (w[1], int(w[2], 16), int(w[3], 16))
        pairs[key] = pairs.get(key, 0) + int(w[4])
        scenes.add(w[5])
    bad = lost
    print("CONTRAST tables: %s; %d distinct pairs from %d scenes (%s)" % (
        ", ".join("%s %d rows" % (k, len(v)) for k, v in sorted(tables.items())), len(pairs), len(scenes),
        " ".join(sorted(scenes))))
    for (kind, fg, bg), n in sorted(pairs.items()):
        table = tables.get("Text" if kind == "text" else "Icons", [])
        found = None
        for fname in names.get(fg, []):
            for bname in names.get(bg, []):
                for row in table:
                    if row[0] == fname and bname in row[1]:
                        found = (fname, bname, row)
        desc = "%s %06X on %06X (%d pixels)" % (kind, fg, bg, n)
        if not found:
            print("CONTRAST FAIL %s: %s on %s is no row of the %s table" % (
                desc, "/".join(names.get(fg, ["no token"])), "/".join(names.get(bg, ["no token"])),
                "Text" if kind == "text" else "Icons"))
            bad += 1
            continue
        fname, bname, (_, bgs, ratios, rule, result, where) = found
        got = contrast(fg, bg)
        stated = ratios[bgs.index(bname)]
        ok = (rule in RULES and result.startswith("pass") and round(got, 2) == stated and got >= RULES[rule])
        print("CONTRAST %s %s: %s on %s, rule %s, ratio %.2f (the table: %.2f, %s): %s" % (
            "pass" if ok else "FAIL", desc, fname, bname, rule, got, stated, result, where))
        bad += not ok
    if not pairs:
        print("CONTRAST FAIL: no pair recorded")
        bad += 1
    print("CONTRAST result: %s" % ("PASS" if bad == 0 else "FAIL (%d)" % bad))
    return 0 if bad == 0 else 1


SCENES = {"prims": scene_prims, "icons": scene_icons, "widgets": scene_widgets, "background": scene_background,
          "taskbar": scene_taskbar, "windows": scene_windows, "windows_exposed": scene_windows,
          "windows_hover": lambda scr, tok, s: scene_windows(scr, tok, s, "hover"),
          "windows_pressed": lambda scr, tok, s: scene_windows(scr, tok, s, "pressed"),
          "console": lambda scr, tok, s: scene_console(scr, tok, s, "active"),
          "console_inactive": lambda scr, tok, s: scene_console(scr, tok, s, "inactive"),
          "console_about": lambda scr, tok, s: scene_console(scr, tok, s, "about")}


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


# ---- The capsule coverage table (tests/unit/test_coverage.c, 1.3 stage 3) ----

def coverage_capsules():
    """test_coverage.c's capsules (x1, y1, x2, y2, r, w, h), in its order:
    the same generator (its 64-bit LCG and seed), each argument drawn in
    the order the suite draws it."""
    st = [0x13C0FFEE2026]

    def rnd():
        st[0] = (st[0] * 6364136223846793005 + 1442695040888963407) & (2 ** 64 - 1)
        return st[0] >> 17

    def between(lo, hi):
        return lo + rnd() % (hi - lo + 1)

    def radius(k):
        s = SCALES[k % 4]
        p = ((16, 24, 32, 48)[(k // 4) % 4] * s + 2) // 4
        kind = (k // 16) % 4
        if kind == 0:
            return K.fx(8 * p, 192)                     # an icon's stroke
        if kind == 1:
            return MW.RX * K.fx(22 * p, 24 * 210)       # the M icon's
        if kind == 2:
            return K.fx(25 * s, 40)                     # the wordmark's
        return between(1, 6 * 256)                      # anything up to 6 px

    caps = []
    for i in range(400):                                # general
        x1 = between(-2000, 30000)
        y1 = between(-2000, 30000)
        r = radius(i)
        dy = between(-10000, 10000)
        dx = between(-10000, 10000)
        caps.append((x1, y1, x1 + dx, y1 + dy, r, 120, 120))
    for i in range(200):                                # dots
        x = between(-1000, 30000)
        y = between(-1000, 30000)
        caps.append((x, y, x, y, radius(i), 120, 120))
    for i in range(300):                                # short
        x = between(0, 25000)
        y = between(0, 25000)
        r = radius(i)
        dy = between(-255, 255)
        dx = between(-255, 255)
        caps.append((x, y, x + dx, y + dy, r, 120, 120))
    for i in range(24):                                 # long
        x = between(0, 2000)
        y = between(0, 2000)
        ln = between(300 * 256, 1500 * 256)
        off = between(-3 * 256, 3 * 256)
        r = radius(i)
        if i % 3 == 0:
            caps.append((x, y, x + ln, y + off, r, 2048, 2048))
        elif i % 3 == 1:
            caps.append((x, y, x + off, y + ln, r, 2048, 2048))
        else:
            caps.append((x, y, x + ln // 3, y + ln // 4, r, 2048, 2048))
    for i in range(300):                                # near the axes
        x = between(0, 25000)
        y = between(0, 25000)
        ln = between(256, 60 * 256)
        off = between(-3, 3)
        r = radius(i)
        if i % 2:
            caps.append((x, y, x + off, y + (ln if i % 4 == 1 else -ln), r, 160, 160))
        else:
            caps.append((x, y, x + (ln if i % 4 == 0 else -ln), y + off, r, 160, 160))
    for i in range(300):                                # clipped
        x1 = between(-4000, 20000)
        y1 = between(-4000, 16000)
        h = between(1, 48)
        w = between(1, 64)
        r = radius(i)
        dy = between(-6000, 6000)
        dx = between(-6000, 6000)
        caps.append((x1, y1, x1 + dx, y1 + dy, r, w, h))
    return caps


def coverage_of(cap):
    """(ok, (bx0, by0, bx1, by1), samples) of one capsule: its pixel box
    (make-wallpaper.py's capsule() box, clipped to the w x h surface), and
    if the box is not empty the coverage of every pixel of the box and a
    2-pixel margin, row by row. The margin may lie off the surface, where
    make-wallpaper.py's capsule() computes nothing, so it is run on the
    capsule and the surface moved SHIFT whole pixels right and down: the
    same distances, so the same coverage, at every pixel that is asked."""
    x1, y1, x2, y2, r, w, h = cap
    one = K.one
    bx0, bx1 = max(0, (min(x1, x2) - r - 2 * one) >> K.fb), min(w - 1, (max(x1, x2) + r + 2 * one) >> K.fb)
    by0, by1 = max(0, (min(y1, y2) - r - 2 * one) >> K.fb), min(h - 1, (max(y1, y2) + r + 2 * one) >> K.fb)
    ok = bx0 <= bx1 and by0 <= by1
    if not ok:
        return ok, (bx0, by0, bx1, by1), []
    shift, d = 4, 4 << K.fb
    cov = {}
    MW.capsule(K, cov, x1 + d, y1 + d, x2 + d, y2 + d, r, bx1 + 3 + shift, by1 + 3 + shift)
    samples = [cov.get((x + shift, y + shift), 0)
               for y in range(by0 - 2, by1 + 3) for x in range(bx0 - 2, bx1 + 3)]
    return ok, (bx0, by0, bx1, by1), samples


def coverage_table(path):
    caps = coverage_capsules()
    rows, samples = [], []
    for cap in caps:
        ok, b, s = coverage_of(cap)
        rows.append("    { %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d }," % (cap + (int(ok),) + b))
        samples += s
    lines = ["/* Generated by tests/desktop_ref.py --coverage-table (1.3 stage 3): never",
             " * edited, never committed. test_coverage.c's capsules, each one's pixel box",
             " * and the coverage of every pixel of the box and a 2-pixel margin around",
             " * it (row by row, in the capsules' order), from scripts/make-wallpaper.py's",
             " * capsule rule. */",
             "#ifndef MADIRE_GEN_COVERAGE_REF_H", "#define MADIRE_GEN_COVERAGE_REF_H",
             "#define COV_REF_CAPSULES %d" % len(caps), "#define COV_REF_SAMPLES %d" % len(samples),
             "struct cov_ref {",
             "    int64_t x1, y1, x2, y2, r;",
             "    uint32_t w, h;",
             "    int32_t ok;",
             "    int64_t bx0, by0, bx1, by1;",
             "};",
             "static const struct cov_ref cov_ref_caps[COV_REF_CAPSULES] = {"] + rows + ["};",
             "static const uint16_t cov_ref_samples[COV_REF_SAMPLES] = {"]
    for i in range(0, len(samples), 24):
        lines.append("    " + ",".join(str(v) for v in samples[i:i + 24]) + ",")
    lines += ["};", "#endif"]
    with open(path, "w", encoding="ascii", newline="\n") as f:
        f.write("\n".join(lines) + "\n")
    print("REF coverage table: %d capsules, %d samples" % (len(caps), len(samples)))
    return 0


def main():
    if sys.argv[1:] == ["--tokens"]:
        return tokens_check()
    if len(sys.argv) == 3 and sys.argv[1] == "--coverage-table":
        return coverage_table(sys.argv[2])
    if len(sys.argv) == 3 and sys.argv[1] == "--contrast":
        return contrast_check(sys.argv[2])
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
