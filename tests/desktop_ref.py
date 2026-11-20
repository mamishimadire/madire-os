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
1.3 stage 5: the menus, the M button and the Restart dialog (1.5 stage 6:
the power dialog; the M menu's row Power...)
(kernel/display/ui_menu.h, ui_dialog.h, desktop_core.h's M button
states): the scenes mbutton_hover, mbutton_pressed, menu_mouse, menu_key,
menu_hover, menu_desktop, dialog, dialog_key, dialog_tab,
dialog_pressed_cancel, dialog_pressed_restart and (1.5 stage 6)
dialog_pressed_shutdown (tests/desktopdump.c
describes them; menu_tiles and dialog_tiles are menu_key and dialog_tab
painted by the C side in tiles); Menus, the rules of ui_menus.h written
again (what the pointer and the keys do to the menus and the dialog, and
the desktoplog lines they give), which tests/qmp_desktop.py's model
follows; desktop_windows draws its M button, menu and dialog too.
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
    # 1.5 stage 8: the title-bar buttons' icons and the power icon.
    "minimise": [[(6, 12), (18, 12)]],
    "maximise": [box(6, 6, 18, 18)],
    "unmaximise": [box(5, 9, 15, 19), [(9, 9), (9, 5), (19, 5), (19, 15), (15, 15)]],
    "power": [arc(12, 13, 8, 30, 330, 15), [(12, 3), (12, 12)]],
}
ICON_ORDER = ["M", "console", "about", "restart", "calculator", "close", "plus", "minus", "times", "divide",
              "backspace", "chevron", "minimise", "maximise", "unmaximise", "power"]


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


def taskbar(scr, tok, clock, m="normal"):
    """The taskbar; 1.3 stage 5: the M button in state m (desktop_core.h:
    hover fills the cell below the edge line in surface.active; pressed and
    open also draw a 2 dp accent bar along its bottom)."""
    L = layout(scr.w, scr.h)
    fill(scr, L["bar"], tok["surface.bar"])
    fill(scr, L["edge"], tok["surface.bar.edge"])
    if m != "normal":
        top = L["edge"][1] + L["edge"][3]
        cell = (0, top, L["bar"][3], L["bar"][1] + L["bar"][3] - top)
        fill(scr, cell, tok["surface.active"])
        if m != "hover":
            b = px(2, L["s"])
            fill(scr, (cell[0], cell[1] + cell[3] - b, cell[2], b), tok["accent"])
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


# ---- The splash-to-desktop handover (1.3 stage 8) ----

def handover_boxes(w, h):
    """The handover's places, in pixels (x, y, w, h), from the splash's
    layout (make-wallpaper.py) and the taskbar's (layout above): the M's
    box (its strokes' capsules, 2 px around them), and the boxes where the
    splash's ready state and the desktop may differ: the wordmark (its
    strokes' radius and 2 px around), the ready line's cells, the taskbar."""
    lay, L = MW.splash_layout(w, h), layout(w, h)
    ls, lox, loy, ts, track = lay["ls"], lay["lox"], lay["loy"], lay["ts"], lay["track"]
    r = MW.RX * ls
    ys = [loy + K.mul(K.fx(y * MW.RX, MW.RY), ls) for s_ in MW.STROKES for y in (s_[1], s_[3])]
    m_box = ((lox + MW.LX0 * ls >> 8) - 2, (min(ys) - r >> 8) - 2)
    m_box += (((lox + MW.LX1 * ls + 255) >> 8) + 2 - m_box[0], ((max(ys) + r + 255) >> 8) + 2 - m_box[1])
    adv = sum(MW.kernel_glyph(ch)[0] + track for ch in MW.WORDMARK)
    width = K.mul(adv, ts) - K.mul(track, ts)
    pen = K.fx(w, 2) - width // 2
    rt = max(lay["r_title"], lay["r_os"])
    wx0, wy0 = (pen - rt >> 8) - 2, (lay["title_y"] - rt >> 8) - 2
    word = (wx0, wy0, ((pen + width + rt + 255) >> 8) + 2 - wx0,
            ((lay["title_y"] + K.mul(K.fx(14), ts) + rt + 255) >> 8) + 2 - wy0)
    ready = (lay["ready_x"], lay["text_y"], len(MW.READY_TEXT) * lay["cw"], lay["ch"])
    return {"m": m_box, "allowed": {"wordmark": word, "ready line": ready, "taskbar": L["bar"]}}


def handover_check(w, h, before, after):
    """The splash's ready state (`before`) against the desktop's first
    frame (`after`), both RGB bytes of a w x h screen: the pixels that
    differ inside the M's box (none expected: the M stays still) and
    outside every allowed box (none expected: only the splash's text goes,
    the taskbar comes). Returns (m_differ, outside, boxes, the first
    pixel outside or None)."""
    b = handover_boxes(w, h)
    mx, my, mw, mh = b["m"]

    def inside(x, y, r):
        return r[0] <= x < r[0] + r[2] and r[1] <= y < r[1] + r[3]
    m_differ = outside = 0
    first = None
    for y in range(h):
        row = y * w * 3
        if before[row:row + w * 3] == after[row:row + w * 3]:
            continue
        for x in range(w):
            o = row + 3 * x
            if before[o:o + 3] == after[o:o + 3]:
                continue
            if mx <= x < mx + mw and my <= y < my + mh:
                m_differ += 1
            elif not any(inside(x, y, r) for r in b["allowed"].values()):
                outside += 1
                if first is None:
                    first = (x, y)
    return m_differ, outside, b, first


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


def part_rect(r, s, buttons, part):
    """1.5 stage 8 (ui_window_part_rect): the title-bar button `part`
    (close, maximise, minimise) from the right: close, then maximise if
    the window has it, then minimise; None if it has no such button."""
    tb = px(32, s)
    if part == "close":
        k = 0
    elif part == "maximise" and "max" in buttons:
        k = 1
    elif part == "minimise" and "min" in buttons:
        k = 2 if "max" in buttons else 1
    else:
        return None
    return (r[0] + r[2] - (k + 1) * tb, r[1], tb, tb)


def window(scr, tok, s, r, title, icon_name, active, close, client, extra=None):
    """A window at r (ui_window.h): the frame, the client, the title bar,
    the close button in state `close` (normal, hover, pressed). 1.5 stage
    8: `extra` (or None: close only) a dict buttons (a set of "min",
    "max"), min and max (their states), maximised, size (w, h: the client
    painted in its own size, centred across the window, when maximised)."""
    extra = extra or {"buttons": set(), "min": "normal", "max": "normal", "maximised": False, "size": None}
    buttons = extra["buttons"]
    R, tb = px(6, s), px(32, s)
    fill_round(scr, r, R, 15, tok["surface.window"])
    frame_round(scr, r, R, px(1, s), 15, tok["border.window" if active else "border.inactive"])
    # 1.5 stage 9: a resizable window's client too (its own size, centred
    # across; the same as r while the window has its own size).
    if extra["maximised"] or (extra.get("resizable") and tuple(r[2:4]) != tuple(extra["size"])):
        w, h = extra["size"]
        client(scr, (r[0] + (r[2] - w) // 2, r[1], w, h))
    else:
        client(scr, r)
    ip = px(16, s)
    icon(scr, icon_name, r[0] + px(8, s), r[1] + (tb - ip) // 2, ip, tok["text"])
    cr = (r[0] + r[2] - tb, r[1], tb, tb)
    left = r[0] + r[2] - (1 + len(buttons)) * tb
    tx = r[0] + px(32, s)
    keep = scr.clip
    scr.clip = meet(keep, (tx, r[1], left - px(8, s) - tx, tb))
    if scr.clip is not None:
        text(scr, font(s, True), tx, r[1] + (tb - FONT_PX[s]) // 2, title, tok["text" if active else "text.muted"])
    scr.clip = keep
    style = ([NONE, tok["danger"], tok["danger.pressed"]], [tok["text"]] * 3, False, 6, TR)
    button(scr, tok, cr, style, close, False, s, ic="close", icon_dp=16)
    tstyle = ([NONE, tok["surface.hover"], tok["accent.pressed"]], [tok["text"]] * 3, False, 0, 0)
    if "max" in buttons:
        button(scr, tok, part_rect(r, s, buttons, "maximise"), tstyle, extra["max"], False, s,
               ic="unmaximise" if extra["maximised"] else "maximise", icon_dp=16)
    if "min" in buttons:
        button(scr, tok, part_rect(r, s, buttons, "minimise"), tstyle, extra["min"], False, s,
               ic="minimise", icon_dp=16)


def task_width(wm):
    """1.5 stage 8: each taskbar button's width (ui_window.h)."""
    gap, most, least, n = px(4, wm.s), px(160, wm.s), px(40, wm.s), len(wm.order)
    if n == 0:
        return 0
    room = wm.task_x1 - wm.task_x0 - (n - 1) * gap
    return max(min(max(room, 0) // n, most), least)


def task_rect(wm, k):
    top, bw, gap = wm.bar + px(1, wm.s), task_width(wm), px(4, wm.s)
    return (wm.task_x0 + k * (bw + gap), top, bw, wm.h - top)


def task_buttons(scr, tok, s, task):
    """1.5 stage 8: the taskbar's window buttons (ui_wm_draw_taskbar):
    task is a list of dicts r, icon, title, state, focused, target."""
    f = font(s, False)
    for b in task:
        r = b["r"]
        keep = scr.clip
        scr.clip = meet(keep, r)
        if scr.clip is not None:
            bar = b["focused"] or b["state"] == "pressed"
            if bar or b["state"] == "hover":
                fill(scr, r, tok["surface.active"])
            if bar:
                h = px(2, s)
                fill(scr, (r[0], r[1] + r[3] - h, r[2], h), tok["accent"])
            ip = px(16, s)
            icon(scr, b["icon"], r[0] + px(8, s), r[1] + (r[3] - ip) // 2, ip, tok["text"])
            tx = r[0] + px(32, s)
            inner = scr.clip
            scr.clip = meet(inner, (tx, r[1], r[0] + r[2] - px(8, s) - tx, r[3]))
            if scr.clip is not None:
                text(scr, f, tx, r[1] + (r[3] - f[2]) // 2, b["title"], tok["text"])
            scr.clip = inner
            if b["target"]:
                g = px(2, s)
                frame_round(scr, inflate(r, -g), px(4, s), g, 15, tok["accent"])
        scr.clip = keep


def about_window(scr, tok, s, r, active, close, values, extra=None):
    window(scr, tok, s, r, ABOUT_TITLE, "about", active, close,
           lambda scr_, r_: about_client(scr_, tok, s, r_, values), extra)


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


def console_window(scr, tok, s, r, active, close, grid, extra=None):
    window(scr, tok, s, r, CONSOLE_TITLE, "console", active, close,
           lambda scr_, r_: console_client(scr_, s, r_, grid), extra)


WINDOW_BUTTONS = {"About": {"min", "max"}, "Console": {"min"}}     # 1.5 stage 8 (desktop.c)
WINDOW_RESIZABLE = {"About"}     # 1.5 stage 9 (desktop.c): its own size the smallest
EDGE_LEFT, EDGE_RIGHT, EDGE_TOP, EDGE_BOTTOM = 1, 2, 4, 8           # ui_window.h's UI_EDGE_*
SNAP_LEFT, SNAP_RIGHT = 1, 2                                        # 1.5 stage 9b: UI_SNAP_*
SHORTCUT_NAMES = {"up": "Super+Up", "down": "Super+Down", "left": "Super+Left", "right": "Super+Right",
                  "show_desktop": "Super+D", "minimise_all": "Super+M", "restore_all": "Super+Shift+M",
                  "about": "Super+Pause", "close": "Alt+F4"}
WINDOW_LOOK = {"About": ("about", ABOUT_TITLE), "Console": ("console", CONSOLE_TITLE)}


class WM:
    """The window manager's rules (ui_window.h), written here again: where
    windows open, the clamp, hit testing, focus and z-order, the drag, the
    close button, Esc; 1.5 stage 8: the minimise and maximise buttons,
    minimised and maximised windows, the double click on a title bar, the
    taskbar's window buttons (the order they opened, their states), the
    middle button on them, Alt+Tab over the most-recently-focused order;
    1.5 stage 9: the grab band of a resizable window (8 dp inside each
    edge, after the title-bar buttons) and the resize (resize_rect).
    Each change is kept as the kernel's desktoplog line would say it
    (desktop: ...), in the kernel's order."""

    def __init__(self, w, h, s):
        self.w, self.h, self.s = w, h, s
        self.bar = h - px(40, s)
        self.z, self.focus = [], None           # windows: dicts name, w, h, r, esc, close, ...
        self.order, self.mru = [], []           # 1.5 stage 8: the taskbar's order; most recently focused first
        self.x, self.y = w // 2, h // 2
        self.resize, self.resize_edges = None, 0    # 1.5 stage 9: the window being resized, its moving edges
        # 1.5 stage 9b: a drag dragged away; the changes counted for Super+D;
        # its kept set ({"set", "focus", "changes"} or None) and Super+M's.
        self.dragged_away, self.changes, self.shown, self.minall = False, 0, None, []
        self.left, self.drag, self.pressed = False, None, None
        self.pressed_part, self.task_pressed, self.click_w = "close", None, None
        self.switching, self.switch_i, self.target = False, 0, None
        L = layout(w, h)
        self.task_x0, self.task_x1 = px(40, s) + px(4, s), L["clock"][0] - px(16, s)
        self.events = []

    def clamp(self, r):
        keep = px(48, self.s)
        x = min(max(r[0], keep - r[2]), self.w - keep)
        y = min(max(r[1], 0), max(self.bar - r[3], 0))
        return (x, y, r[2], r[3])

    def place(self, w, h):
        step = px(24, self.s)
        x, y = int((self.w - w) / 2), max(int((self.bar - h) / 2), 0)
        for _ in range(6):                      # 1.5 stage 8: UI_WINDOWS_MAX 4 -> 6
            if not any(o["r"][0] == x and o["r"][1] == y for o in self.z):
                break
            x, y = x + step, y + step
        return self.clamp((x, y, w, h))

    def close_rect(self, r):
        tb = px(32, self.s)
        return (r[0] + r[2] - tb, r[1], tb, tb)

    @staticmethod
    def inside(r, x, y):
        return r is not None and r[0] <= x < r[0] + r[2] and r[1] <= y < r[1] + r[3]

    def hit(self, x, y):
        for win in reversed(self.z):
            if win["minimised"] or not self.inside(win["r"], x, y):
                continue
            if self.inside(self.close_rect(win["r"]), x, y):
                return "close", win
            if self.inside(part_rect(win["r"], self.s, win["buttons"], "maximise"), x, y):
                return "maximise", win
            if self.inside(part_rect(win["r"], self.s, win["buttons"], "minimise"), x, y):
                return "minimise", win
            if self.edges(win, x, y):                   # 1.5 stage 9: after the buttons
                return "resize", win
            return ("title" if y < win["r"][1] + px(32, self.s) else "body"), win
        if y >= self.bar:
            for k, win in enumerate(self.order):
                if self.inside(task_rect(self, k), x, y):
                    return "taskbutton", win
            return "taskbar", None
        return "desktop", None

    def edges(self, win, x, y):
        """1.5 stage 9 (ui_window_edges): the edges whose band (8 dp inside
        the rectangle) holds (x, y); 0 when not resizable, maximised or
        minimised, or outside."""
        r = win["r"]
        if (not win.get("resizable") or win["maximised"] or win["minimised"] or win.get("snapped")
                or not self.inside(r, x, y)):           # 1.5 stage 9b: no band while snapped
            return 0
        b, e = px(8, self.s), 0
        if x < r[0] + b:
            e |= EDGE_LEFT
        elif x >= r[0] + r[2] - b:
            e |= EDGE_RIGHT
        if y < r[1] + b:
            e |= EDGE_TOP
        elif y >= r[1] + r[3] - b:
            e |= EDGE_BOTTOM
        return e

    def resize_rect(self, win, g, edges, dx, dy):
        """1.5 stage 9 (ui_wm_resize_rect): the moving edges 1:1, the size
        between the minimum and the area above the taskbar (each never past
        the start), no edge further off the screen than it started, 48 dp
        on the screen."""
        keep = px(48, self.s)
        minw, minh = min(max(win["min_size"][0], 1), g[2]), min(max(win["min_size"][1], 1), g[3])
        maxw, maxh = max(self.w, g[2]), max(self.bar, g[3])
        clamp = lambda v, lo, hi: min(max(v, lo), hi)  # noqa: E731
        left, top, right, bottom = g[0], g[1], g[0] + g[2], g[1] + g[3]
        if edges & EDGE_LEFT:
            left = clamp(g[0] + dx, max(right - maxw, min(0, g[0])), min(right - minw, max(self.w - keep, g[0])))
        elif edges & EDGE_RIGHT:
            right = clamp(right + dx, max(left + minw, min(keep, right)), min(left + maxw, max(self.w, right)))
        if edges & EDGE_TOP:
            top = clamp(g[1] + dy, max(bottom - maxh, min(0, g[1])), bottom - minh)
        elif edges & EDGE_BOTTOM:
            bottom = clamp(bottom + dy, top + minh, min(top + maxh, max(self.bar, bottom)))
        return (left, top, right - left, bottom - top)

    def resize_end(self):
        win, self.resize = self.resize, None
        if win and tuple(win["r"]) != tuple(self.grab[2]):
            self.ev("resize %s to (%d, %d) %dx%d" % ((win["name"],) + tuple(win["r"])))

    def ev(self, line):
        self.events.append("desktop: " + line)

    def set_focus(self, win):
        if self.focus is win:
            return
        self.focus = win
        if win:
            self.mru.remove(win)
            self.mru.insert(0, win)
        self.ev("focus %s" % win["name"] if win else "focus none")

    def raise_(self, win):
        if self.z[-1] is not win:
            self.z.remove(win)
            self.z.append(win)
            self.ev("raise %s" % win["name"])

    def part_state(self, win, part):
        if self.drag or self.resize or win["minimised"]:     # 1.5 stage 9: a resize as a drag
            return "normal"
        over = self.hit(self.x, self.y) == (part, win)
        if self.pressed:
            return "pressed" if self.pressed is win and self.pressed_part == part and over else "normal"
        return "hover" if over and not self.left else "normal"

    def state(self, win):
        return self.part_state(win, "close")

    def task_state(self, win):
        if self.drag or self.resize:                    # 1.5 stage 9: a resize as a drag
            return "normal"
        over = self.hit(self.x, self.y) == ("taskbutton", win)
        if self.task_pressed:
            return "pressed" if self.task_pressed is win and over else "normal"
        return "hover" if over and not self.left and not self.pressed else "normal"

    def states(self):
        for win in self.z:
            for key, part, line in (("close", "close", "close button of %s: %s"),
                                    ("max", "maximise", "maximise button of %s: %s"),
                                    ("min", "minimise", "minimise button of %s: %s")):
                st = self.part_state(win, part)
                if st != win[key]:
                    win[key] = st
                    self.ev(line % (win["name"], st))
            st = self.task_state(win)
            if st != win["task"]:
                win["task"] = st
                self.ev("taskbar button %s: %s" % (win["name"], st))

    def open(self, name, w, h, esc, buttons=None, look=None):
        win = {"name": name, "w": w, "h": h, "esc": esc, "close": "normal", "min": "normal", "max": "normal",
               "task": "normal", "buttons": set(buttons or ()), "minimised": False, "maximised": False,
               "normal": None, "look": look or WINDOW_LOOK.get(name),
               "resizable": name in WINDOW_RESIZABLE, "min_size": (w, h),   # 1.5 stage 9
               "snapped": 0}                                                # 1.5 stage 9b
        win["r"] = self.place(w, h)
        self.changes += 1
        self.z.append(win)
        self.order.append(win)
        self.mru.append(win)
        self.ev("open %s at (%d, %d) %dx%d" % ((name,) + win["r"]))
        self.set_focus(win)
        self.states()
        return win

    def top_shown(self):
        return next((x for x in reversed(self.z) if not x["minimised"]), None)

    def close(self, win, why):
        self.z.remove(win)
        self.order.remove(win)
        self.mru.remove(win)
        self.changes += 1                               # 1.5 stage 9b
        if self.pressed is win:
            self.pressed = None
        if self.drag is win:
            self.drag = None
        if self.resize is win:                          # 1.5 stage 9
            self.resize = None
        if self.task_pressed is win:
            self.task_pressed = None
        if self.click_w is win:
            self.click_w = None
        if self.switching and self.target is win:
            self.switching, self.target = False, None
        self.ev("close %s (%s)" % (win["name"], why))
        if self.focus is win:
            self.focus = None
            top = self.top_shown()
            if top:
                self.set_focus(top)
            else:
                self.ev("focus none")

    def minimise(self, win):
        if win["minimised"]:
            return
        win["minimised"] = True
        self.changes += 1                               # 1.5 stage 9b
        if self.drag is win:
            self.drag = None
        if self.resize is win:                          # 1.5 stage 9
            self.resize = None
        if self.pressed is win:
            self.pressed = None
        if self.click_w is win:
            self.click_w = None
        self.ev("minimise %s" % win["name"])
        if self.focus is win:
            self.focus = None
            top = self.top_shown()
            if top:
                self.set_focus(top)
            else:
                self.ev("focus none")

    def activate(self, win):
        """ui_wm_activate: restored (if minimised), raised, focused."""
        if win["minimised"]:
            win["minimised"] = False
            self.changes += 1                           # 1.5 stage 9b
            self.ev("restore %s" % win["name"])
        self.raise_(win)
        self.set_focus(win)
        self.states()

    def toggle_max(self, win):
        if win["minimised"] or "max" not in win["buttons"]:
            return
        if win["maximised"]:
            win["maximised"], win["r"] = False, win["normal"]
        else:
            if not win.get("snapped"):                  # 1.5 stage 9b: a snapped window keeps its kept rectangle
                win["normal"] = win["r"]
            win["snapped"], win["maximised"], win["r"] = 0, True, (0, 0, self.w, self.bar)
        self.changes += 1
        self.ev("%s %s to (%d, %d) %dx%d" % (("maximise" if win["maximised"] else "unmaximise", win["name"])
                                              + tuple(win["r"])))

    # ---- 1.5 stage 9b: snapping and the window shortcuts (ui_window.h) ----

    def half_rect(self, side):
        half = self.w // 2
        return (0, 0, half, self.bar) if side == SNAP_LEFT else (half, 0, self.w - half, self.bar)

    def half_fits(self, win, side):
        h = self.half_rect(side)
        return bool(win.get("resizable")) and h[2] >= win["min_size"][0] and h[3] >= win["min_size"][1]

    def place_win(self, win, r, side):
        win["r"], win["snapped"] = tuple(r), side
        self.changes += 1

    def snap_line(self, win):
        self.ev("snap %s to the %s half (%d, %d) %dx%d" % (
            (win["name"], "left" if win["snapped"] == SNAP_LEFT else "right") + tuple(win["r"])))

    def snap(self, win, side):
        """ui_wm_snap: True if it changed."""
        if win not in self.z or win["minimised"]:
            return False
        if side == 0 or (win["snapped"] and win["snapped"] != side):
            if not win["snapped"]:
                return False
            self.place_win(win, win["normal"], 0)
            self.ev("unsnap %s to (%d, %d) %dx%d" % ((win["name"],) + tuple(win["r"])))
            return True
        if win["snapped"] == side or not self.half_fits(win, side):
            return False
        if not win["maximised"]:
            win["normal"] = win["r"]
        win["maximised"] = False
        self.place_win(win, self.half_rect(side), side)
        self.snap_line(win)
        return True

    def snap_release(self, win, x, y):
        """win_snap_release: a moved drag ended at (x, y)."""
        z = px(4, self.s)
        if y >= self.bar:
            return False
        if y < z:
            if "max" not in win["buttons"]:
                return False
            win["normal"], win["maximised"] = self.grab[2], True
            self.place_win(win, (0, 0, self.w, self.bar), 0)
            self.ev("maximise %s to (%d, %d) %dx%d" % ((win["name"],) + tuple(win["r"])))
            return True
        side = SNAP_LEFT if x < z else SNAP_RIGHT if x >= self.w - z else 0
        if not side or not self.half_fits(win, side):
            return False
        win["normal"] = self.grab[2]
        self.place_win(win, self.half_rect(side), side)
        self.snap_line(win)
        return True

    def drag_away(self, win, x, y):
        """win_drag_away: restored at its kept size under the pointer."""
        r, n = win["r"], win["normal"]
        gx, gy, _ = self.grab
        ox, oy = gx - r[0], gy - r[1]
        nx = x - (ox * n[2] // r[2] if r[2] > 0 else 0)
        to = self.clamp((nx, y - oy, n[2], n[3]))
        was_max = win["maximised"]
        win["maximised"] = False
        self.place_win(win, to, 0)
        self.ev("%s %s to (%d, %d) %dx%d" % (("unmaximise" if was_max else "unsnap", win["name"]) + tuple(to)))
        self.grab, self.dragged_away = (x, y, tuple(to)), True

    def minimise_shown(self):
        out = [w for w in self.z if not w["minimised"] and "min" in w["buttons"]]
        for w in out:
            self.minimise(w)
        return out

    def restore_set(self, wins):
        done = 0
        for w in wins:
            if w in self.z and w["minimised"]:
                self.activate(w)
                done += 1
        return done

    def shortcut(self, sc):
        """ui_wm_shortcut (sc: up, down, left, right, show_desktop,
        minimise_all, restore_all, about, close): its line first, then the
        windows' lines; returns the windows it changed."""
        f = self.focus
        at = len(self.events)
        self.events.append(None)
        n, restored = 0, False
        if sc == "up":
            if f and "max" in f["buttons"] and not f["maximised"]:
                self.toggle_max(f)
                n = 1
        elif sc == "down":
            if f and f["maximised"]:
                self.toggle_max(f)
                n = 1
            elif f and f["snapped"]:
                n = 1 if self.snap(f, 0) else 0
            elif f and "min" in f["buttons"]:
                self.minimise(f)
                n = 1
        elif sc in ("left", "right"):
            if f:
                n = 1 if self.snap(f, SNAP_LEFT if sc == "left" else SNAP_RIGHT) else 0
        elif sc == "show_desktop":
            if self.shown and self.shown["changes"] == self.changes:
                n, kept = self.restore_set(self.shown["set"]), self.shown["focus"]
                if kept is not None and kept in self.z and not kept["minimised"]:
                    self.activate(kept)
                elif kept is None:
                    self.set_focus(None)
                self.shown, restored = None, True
            else:
                kept = f
                wins = self.minimise_shown()
                n = len(wins)
                self.shown = {"set": wins, "focus": kept, "changes": self.changes} if wins else None
        elif sc == "minimise_all":
            self.minall = self.minimise_shown()
            n = len(self.minall)
        elif sc == "restore_all":
            n = self.restore_set(self.minall)
            self.minall = []
        elif sc == "close":
            if f:
                self.close(f, "Alt+F4")
                n = 1
        name, wins = SHORTCUT_NAMES[sc], "window" if n == 1 else "windows"
        if sc == "show_desktop":
            line = "shortcut %s: %d %s %s" % (name, n, wins, "restored" if restored else "minimised")
        elif sc == "minimise_all":
            line = "shortcut %s: %d %s minimised" % (name, n, wins)
        elif sc == "restore_all":
            line = "shortcut %s: %d %s restored" % (name, n, wins)
        elif sc == "about" or n:
            line = "shortcut %s" % name
        else:
            line = "shortcut %s: nothing to do" % name
        self.events[at] = "desktop: " + line
        self.states()
        return n

    def pointer(self, x, y, down=False, up=False, double=False):
        """One packet; `double`: a press the test sends right after a
        click on the same title bar (the kernel's double click, within
        UI_WM_DCLICK_TICKS: tests/qmp_desktop.py keeps other clicks apart)."""
        self.x, self.y = x, y
        if self.drag and (self.drag["maximised"] or self.drag["snapped"]):     # 1.5 stage 9b: dragged away
            z, (gx, gy, _) = px(4, self.s), self.grab
            if abs(x - gx) > z or abs(y - gy) > z:
                self.drag_away(self.drag, x, y)
        if self.drag and not self.drag["maximised"] and not self.drag["snapped"]:
            win, (gx, gy, gr) = self.drag, self.grab
            win["r"] = self.clamp((gr[0] + x - gx, gr[1] + y - gy, gr[2], gr[3]))
        if self.resize:                                 # 1.5 stage 9
            gx, gy, gr = self.grab
            self.resize["r"] = self.resize_rect(self.resize, gr, self.resize_edges, x - gx, y - gy)
        if down and not self.left:
            self.left = True
            part, win = self.hit(x, y)
            dclick = double and self.click_w is win and part == "title"
            self.click_w = None
            if part == "taskbutton":
                self.task_pressed = win
            elif part == "desktop":
                self.set_focus(None)
            elif part != "taskbar":
                self.raise_(win)
                self.set_focus(win)
                if part in ("close", "minimise", "maximise"):
                    self.pressed, self.pressed_part = win, part
                elif part == "resize":                  # 1.5 stage 9: the grab band
                    self.resize, self.resize_edges = win, self.edges(win, x, y)
                    self.grab = (x, y, win["r"])
                elif part == "title" and dclick and "max" in win["buttons"]:
                    self.toggle_max(win)
                elif part == "title":
                    self.drag, self.grab = win, (x, y, win["r"])
                    self.dragged_away = False           # 1.5 stage 9b
        if up and self.left:
            self.left = False
            self.resize_end()                           # 1.5 stage 9
            if self.drag:
                win, self.drag = self.drag, None
                # 1.5 stage 9b: a drag that moved may end in a snap zone (no drag line then).
                if tuple(win["r"][:2]) != tuple(self.grab[2][:2]) or self.dragged_away:
                    if not self.snap_release(win, x, y):
                        self.ev("drag %s to (%d, %d) %dx%d" % ((win["name"],) + tuple(win["r"])))
                else:
                    self.click_w = win
            if self.pressed:
                win, part, self.pressed = self.pressed, self.pressed_part, None
                if self.hit(x, y) == (part, win):
                    if part == "close":
                        self.close(win, "the close button")
                    elif part == "minimise":
                        self.minimise(win)
                    else:
                        self.toggle_max(win)
            if self.task_pressed:
                win, self.task_pressed = self.task_pressed, None
                if self.hit(x, y) == ("taskbutton", win):
                    if win["minimised"] or self.focus is not win:
                        self.activate(win)
                    else:
                        self.minimise(win)
        self.states()

    def middle(self, x, y):
        """1.5 stage 8 (ui_wm_middle): the middle button pressed."""
        self.x, self.y = x, y
        part, win = self.hit(x, y)
        if part == "taskbutton":
            self.close(win, "middle click")
        self.states()

    def switch_step(self, back):
        n = len(self.mru)
        if n == 0:
            return False
        if not self.switching:
            self.switching, self.switch_i = True, (n - 1 if back else (1 if n > 1 else 0))
        else:
            self.switch_i = (self.switch_i + (n - 1 if back else 1)) % n
        self.target = self.mru[self.switch_i]
        self.ev("switch: %s marked (Alt+Tab)" % self.target["name"])
        return True

    def switch_commit(self):
        if not self.switching:
            return
        win, self.switching, self.target = self.target, False, None
        self.ev("switch: %s chosen (Alt released)" % win["name"])
        self.activate(win)

    def switch_cancel(self):
        if not self.switching:
            return
        self.switching, self.target = False, None
        self.ev("switch: cancelled (Esc)")

    def task_view(self):
        """The taskbar buttons as task_buttons() draws them."""
        out = []
        for k, win in enumerate(self.order):
            ic, title = win["look"]
            out.append({"r": task_rect(self, k), "icon": ic, "title": title, "state": win["task"],
                        "focused": self.focus is win, "target": self.switching and self.target is win})
        return out

    def cancel(self):
        """1.3 stage 5 (ui_wm_cancel): a held left button let go, nothing
        closed: a drag ends where it is, the close button not pressed."""
        if not self.left:
            return
        self.left = False
        self.resize_end()                               # 1.5 stage 9
        if self.drag:
            win, self.drag = self.drag, None
            if win["r"][:2] != self.grab[2][:2]:
                self.ev("drag %s to (%d, %d) %dx%d" % ((win["name"],) + win["r"]))
        self.pressed = None
        self.task_pressed = None
        self.click_w = None
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
                self.open("Console", console_size[0], console_size[1], False, WINDOW_BUTTONS["Console"])
            else:
                self.activate(con)
            return "opened"
        if self.focus["name"] == "Console":
            return "console"
        if pressed and esc:
            self.esc()
        return "taken"


def wm_shown(wm, values, grid=None):
    """1.5 stage 8: the windows to draw (desktop_windows' list), bottom to
    top, minimised ones left out: a dict r, active, close, values, extra
    (window()'s), and grid for the Console window."""
    shown = []
    for z in wm.z:
        if z["minimised"]:
            continue
        one = {"r": z["r"], "active": z is wm.focus, "close": z["close"], "values": values,
               "extra": {"buttons": z["buttons"], "min": z["min"], "max": z["max"], "maximised": z["maximised"],
                         "size": (z["w"], z["h"]), "resizable": z.get("resizable", False)}}
        if z["name"] == "Console":
            one["grid"] = grid() if callable(grid) else grid
        shown.append(one)
    return shown

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


def desktop_windows(w, h, clock, windows, shell=None):
    """The desktop (desktop()) with windows on it, bottom to top: each a
    dict r, active, close, values (the About window, the only one in 4a);
    1.3 stage 4b: a dict with a "grid" (shadow_grid's) is a Console window.
    1.3 stage 5: shell (Menus.view()): the M button's state, then an open
    menu and the dialog above everything."""
    tok = colours()
    s = scale_of(w, h)
    scr = Screen(w, h, tok["bg.deep"])
    scr.pix = bytearray(desktop(w, h, clock))
    for win in windows:
        if win.get("grid") is not None:
            console_window(scr, tok, s, win["r"], win["active"], win["close"], win["grid"], win.get("extra"))
        else:
            about_window(scr, tok, s, win["r"], win["active"], win["close"], win["values"], win.get("extra"))
    if shell is not None:
        if shell["m"] != "normal":
            taskbar(scr, tok, clock, shell["m"])
        task_buttons(scr, tok, s, shell.get("task", []))          # 1.5 stage 8
        if shell["menu"] is not None:
            menu_draw(scr, tok, shell["menu"])
        if shell["dialog"] is not None:
            dialog_draw(scr, tok, s, shell["dialog"])
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


# ---- 1.5 stage 8: the title-bar buttons and the taskbar's window buttons ----

def scene_chrome(scr, tok, s, variant):
    """tests/desktopdump.c's chrome scenes: chrome (the Console window,
    minimise only, then an About window over it, minimise and maximise,
    every button normal), chrome_hover (About's minimise on hover, its
    maximise pressed, the Console window's minimise on hover), chrome_max
    (an About window alone, maximised)."""
    wm = WM(scr.w, scr.h, s)
    c = None
    if variant != "max":
        cw, chh = console_size(s)
        c = wm.open("Console", cw, chh, False, WINDOW_BUTTONS["Console"])
    aw, ah = about_size(s)
    a = wm.open("About", aw, ah, True, WINDOW_BUTTONS["About"])
    if variant == "hover":
        a["min"], a["max"], c["min"] = "hover", "pressed", "hover"
    elif variant == "max":
        wm.toggle_max(a)
    values = about_values(s, b"1.3", DUMP_LOADER, DUMP_DISPLAY, b"127 MB")
    for win in wm_shown(wm, values, dump_grid()):
        if win.get("grid") is not None:
            console_window(scr, tok, s, win["r"], win["active"], "normal", win["grid"], win["extra"])
        else:
            about_window(scr, tok, s, win["r"], win["active"], "normal", values, win["extra"])
    return "%s; the About window at (%d, %d) %dx%d" % ((variant,) + a["r"])


TASK_SCENE = [(ABOUT_TITLE, "about"), (b"Console", "console"), (b"A title much longer than its button", "calculator"),
              (b"Power", "power"), (b"Restart", "restart"), (b"Close", "close")]


def scene_taskbuttons(scr, tok, s):
    """tests/desktopdump.c's taskbuttons scene: the taskbar with six window
    buttons: focused, hover, pressed (a long title clipped), minimised,
    the Alt+Tab ring, normal."""
    taskbar(scr, tok, FIXED_CLOCK)
    wm = WM(scr.w, scr.h, s)
    wins = [wm.open(t.decode(), 100, 100, False, None, (ic, t)) for t, ic in TASK_SCENE]
    wm.activate(wins[0])
    wins[1]["task"], wins[2]["task"] = "hover", "pressed"
    wm.minimise(wins[3])
    wm.switching, wm.target = True, wins[4]
    task_buttons(scr, tok, s, wm.task_view())
    r = task_rect(wm, 0)
    return "six buttons %dx%d from (%d, %d)" % (r[2], r[3], r[0], r[1])


# ---- 1.5 stage 10: the text field, the list and grid views, the scroll
# bar (kernel/display/ui_textfield.h, ui_list.h; the Design Guide,
# section 13) ----

FIELD_MAX = 255
LIST_TEXT_MAX = FIELD_MAX + 1


def shown_name(b):
    """ui_name_shown: each byte outside printable ASCII as '?'."""
    return bytes(c if 0x20 <= c <= 0x7E else 0x3F for c in b[:LIST_TEXT_MAX - 1])


class Field:
    def __init__(self, text):
        self.buf = bytes(text[:FIELD_MAX])
        self.pos, self.first = len(self.buf), 0


def field_cols(r, s, f):
    room = r[2] - 2 * px(8, s)
    return room // f[1] if room > 0 else 0


def field_fit(t, cols):
    k = max(cols, 1)
    if t.pos < t.first:
        t.first = t.pos
    elif t.pos - t.first > k - 1:
        t.first = t.pos - (k - 1)
    if t.first > 0 and len(t.buf) - t.first < k - 1:
        t.first = max(0, len(t.buf) - (k - 1))


def field_draw(scr, tok, t, r, s, focused):
    f = font(s, False)
    R = px(4, s)
    fill_round(scr, r, R, 15, tok["surface"])
    frame_round(scr, r, R, px(2, s) if focused else px(1, s), 15, tok["accent"] if focused else tok["text.muted"])
    cols = field_cols(r, s, f)
    tx, ty = r[0] + px(8, s), r[1] + (r[3] - f[2]) // 2
    text(scr, f, tx, ty, shown_name(t.buf[t.first:t.first + cols]), tok["text"])
    if focused:
        keep = scr.clip
        scr.clip = meet(keep, inflate(r, -px(2, s)))
        if scr.clip is not None:
            fill(scr, (tx + (t.pos - t.first) * f[1], ty, px(2, s), f[2]), tok["text"])
        scr.clip = keep


def scrollbar_thumb(sb, lines, page, top, s):
    pad = px(4, s)
    L = sb[3] - 2 * pad
    if page == 0 or lines <= page or L <= 0:
        return None
    maxtop = lines - page
    T = min(max(L * page // lines, px(24, s)), L)
    off = (L - T) * min(top, maxtop) // maxtop
    w = px(8, s)
    return (sb[0] + (sb[2] - w) // 2, sb[1] + pad + off, w, T)


def scrollbar_draw(scr, tok, sb, lines, page, top, state, s):
    th = scrollbar_thumb(sb, lines, page, top, s)
    if th:
        fill_round(scr, th, px(4, s), 15, tok["text.muted"] if state == "normal" else tok["text"])


class ListView:
    """struct ui_list: items are [name, size, type, modified, icon, selected]."""

    def __init__(self, view, items, titles, r, s):
        self.view, self.items, self.titles, self.r, self.s = view, items, titles, r, s
        self.top = self.cursor = self.anchor = 0
        self.ring, self.sb = False, "normal"

    def body(self):
        s, r = self.s, self.r
        hd = px(28, s) if self.view == "list" else 0
        return (r[0], r[1] + hd, max(r[2] - px(24, s), 0), max(r[3] - hd, 0))

    def sb_rect(self):
        b, w = self.body(), px(24, self.s)
        return (self.r[0] + self.r[2] - w, b[1], w, b[3])

    def columns(self):
        return 1 if self.view == "list" else max(1, self.body()[2] // px(96, self.s))

    def lines(self):
        c = self.columns()
        return (len(self.items) + c - 1) // c

    def page(self):
        step, h = px(28 if self.view == "list" else 88, self.s), self.body()[3]
        return h // step if step > 0 and h > 0 else 0

    def item_rect(self, i):
        s, b, cols = self.s, self.body(), self.columns()
        line = i // cols - self.top
        if self.view == "list":
            rh = px(28, s)
            return (b[0], b[1] + line * rh, b[2], rh)
        cw, ch = px(96, s), px(88, s)
        return (b[0] + (i % cols) * cw, b[1] + line * ch, cw, ch)


def list_text(scr, f, x, y, room, t, c, right):
    if t is None:
        return
    cut = fit(f, shown_name(t), max(room, 0), LIST_TEXT_MAX)
    text(scr, f, x - len(cut) * f[1] if right else x, y, cut, c)


def list_marks(scr, tok, lv, i, r):
    s = lv.s
    g, in4, grid = px(2, s), px(4, s), lv.view == "grid"
    shape = inflate(r, -g) if grid else r
    if lv.items[i][5]:
        if grid:
            fill_round(scr, shape, in4, 15, tok["surface.active"])
            bar = (shape[0] + in4, shape[1] + shape[3] - in4, shape[2] - 2 * in4, g)
        else:
            fill(scr, r, tok["surface.active"])
            bar = (r[0], r[1] + in4, g, r[3] - 2 * in4)
        fill(scr, bar, tok["accent"])
    if lv.ring and i == lv.cursor:
        ring = shape if grid else (r[0] + g, r[1], r[2] - 2 * g, r[3])
        frame_round(scr, ring, in4, g, 15, tok["accent"])


def list_draw(scr, tok, lv):
    s, r, b = lv.s, lv.r, lv.body()
    f = font(s, False)
    fw, fh = f[1], f[2]
    fill(scr, r, tok["surface.window"])
    keep = scr.clip
    body = meet(keep, b)
    n = len(lv.items)
    if lv.view == "list":
        hd, rh, d8 = px(28, s), px(28, s), px(8, s)
        xm = b[0] + b[2] - px(144, s)
        xt = xm - px(112, s)
        xs = xt - px(80, s)
        hy = r[1] + (hd - fh) // 2
        if lv.titles:
            t = lv.titles
            text(scr, f, r[0] + px(36, s), hy, t[0], tok["text.muted"])
            text(scr, f, xs + px(80, s) - d8 - len(t[1]) * fw, hy, t[1], tok["text.muted"])
            text(scr, f, xt + d8, hy, t[2], tok["text.muted"])
            text(scr, f, xm + d8, hy, t[3], tok["text.muted"])
        fill(scr, (r[0], r[1] + hd - px(1, s), b[2], px(1, s)), tok["border"])
        if body is not None:
            scr.clip = body
            for i in range(lv.top, n):
                row = lv.item_rect(i)
                if row[1] >= b[1] + b[3]:
                    break
                name, size, typ, mod, ic, _ = lv.items[i]
                ty, ip = row[1] + (rh - fh) // 2, px(16, s)
                list_marks(scr, tok, lv, i, row)
                if ic is not None:
                    icon(scr, ic, r[0] + px(12, s), row[1] + (rh - ip) // 2, ip, tok["text"])
                list_text(scr, f, r[0] + px(36, s), ty, xs - r[0] - px(44, s), name, tok["text"], False)
                list_text(scr, f, xs + px(80, s) - d8, ty, px(80, s) - px(16, s), size, tok["text.muted"], True)
                list_text(scr, f, xt + d8, ty, px(112, s) - px(16, s), typ, tok["text.muted"], False)
                list_text(scr, f, xm + d8, ty, px(144, s) - px(16, s), mod, tok["text.muted"], False)
            scr.clip = keep
    elif body is not None:
        scr.clip = body
        cols, cw, ip = lv.columns(), px(96, s), px(48, s)
        for i in range(lv.top * cols, n):
            cell = lv.item_rect(i)
            if cell[1] >= b[1] + b[3]:
                break
            name, ic = lv.items[i][0], lv.items[i][4]
            list_marks(scr, tok, lv, i, cell)
            if ic is not None:
                icon(scr, ic, cell[0] + (cw - ip) // 2, cell[1] + px(8, s), ip, tok["text"])
            if name is not None:
                cut = fit(f, shown_name(name), px(88, s), LIST_TEXT_MAX)
                text(scr, f, cell[0] + (cw - len(cut) * fw) // 2, cell[1] + px(64, s), cut, tok["text"])
        scr.clip = keep
    scrollbar_draw(scr, tok, lv.sb_rect(), lv.lines(), lv.page(), lv.top, lv.sb, s)


# The scenes' items (tests/desktopdump.c builds the same).
LIST_TITLES = (b"Name", b"Size", b"Type", b"Modified")
LIST_LONG = b"A very long file name that cannot fit in the name column at all.txt"
LIST_ODD = b"caf\xe9 \x01menu.txt"
LIST_ICONS = ("console", "about", "calculator", "restart")
FIELD_LONG = b"A long name that does not fit its field at all, so it scrolls.txt"


def scene_items(n, selected):
    out = []
    for i in range(n):
        name = LIST_LONG if i == 4 else LIST_ODD if i == 5 else b"Folder %d" % (i + 1) if i < 4 else \
            b"file-%02d.txt" % i
        size = None if i < 4 else b"123456789 KiB" if i == 7 else b"%d KiB" % (3 * i + 1)
        typ = b"Folder" if i < 4 else b"Text file"
        mod = b"2026-11-%02d 14:%02d" % (i + 1, 3 * i)
        out.append([name, size, typ, mod, None if i == 2 else LIST_ICONS[i % 4], i in selected])
    return out


def scene_textfield(scr, tok, s):
    """tests/desktopdump.c's textfield scene: eight fields on a
    surface.window panel (unfocused and focused, empty, the cursor in the
    middle, a long name scrolled to its end and shown from its start,
    bytes outside printable ASCII, a narrow field)."""
    W, H = scr.w, scr.h
    d = lambda n: px(n, s)  # noqa: E731
    fill(scr, (d(16), d(16), W - 2 * d(16), H - 2 * d(16)), tok["surface.window"])
    fl = Flow(W, d(32), d(16))
    fl.y = d(32)
    cases = [(b"New Folder", None, 240, False), (b"New Folder", None, 240, True), (b"", None, 240, True),
             (b"report-2026.txt", 6, 240, True), (FIELD_LONG, None, 240, True), (FIELD_LONG, 0, 240, True),
             (b"caf\xe9 \x01name.txt", None, 240, False), (b"abcdef", 1, 40, True)]
    f = font(s, False)
    for t_, pos, w_dp, focused in cases:
        r = fl.place(d(w_dp), d(32))
        t = Field(t_)
        if pos is not None:
            t.pos = pos
        field_fit(t, field_cols(r, s, f))
        field_draw(scr, tok, t, r, s, focused)
    return "8 fields, bottom %d" % fl.bottom()


def scene_list(scr, tok, s):
    """tests/desktopdump.c's list scene: a list of 20 items scrolled to the
    fourth, items 4 and 5 selected, the ring on item 6, the thumb; below
    it, a list of 3 (no thumb), the first selected with the ring."""
    d = lambda n: px(n, s)  # noqa: E731
    a = ListView("list", scene_items(20, (4, 5)), LIST_TITLES, (d(16), d(16), d(600), d(266)), s)
    a.top, a.cursor, a.anchor, a.ring = 3, 6, 4, True
    list_draw(scr, tok, a)
    b = ListView("list", scene_items(3, (0,)), LIST_TITLES, (d(16), d(16) + d(266) + d(16), d(600), d(140)), s)
    b.ring = True
    list_draw(scr, tok, b)
    return "page %d of %d lines; page %d of %d" % (a.page(), a.lines(), b.page(), b.lines())


def scene_grid(scr, tok, s):
    """tests/desktopdump.c's grid scene: 14 items, 1 and 2 selected, the
    ring on 7; below, the same scrolled one line, 8 selected with the
    ring, the thumb on hover."""
    d = lambda n: px(n, s)  # noqa: E731
    a = ListView("grid", scene_items(14, (1, 2)), None, (d(16), d(16), d(504), d(216)), s)
    a.cursor, a.ring = 7, True
    list_draw(scr, tok, a)
    b = ListView("grid", scene_items(14, (8,)), None, (d(16), d(16) + d(216) + d(16), d(504), d(216)), s)
    b.top, b.cursor, b.anchor, b.ring, b.sb = 1, 8, 8, True, "hover"
    list_draw(scr, tok, b)
    return "%d columns, page %d of %d lines" % (a.columns(), a.page(), a.lines())


SCROLL_CASES = [(100, 10, 0, "normal"), (100, 10, 45, "hover"), (100, 10, 90, "drag"), (1000, 10, 500, "normal"),
                (20, 10, 10, "normal"), (11, 10, 1, "hover"), (10, 10, 0, "normal"), (3, 10, 0, "normal")]


def scene_scrollbar(scr, tok, s):
    """tests/desktopdump.c's scrollbar scene: eight scroll bars (24 dp x
    160 dp on surface.window): the top, the middle (hover), the end
    (dragged), the least thumb, half, one line more (hover), all fitting
    (no thumb) twice."""
    d = lambda n: px(n, s)  # noqa: E731
    fl = Flow(scr.w, d(16), d(16))
    for lines, page, top, state in SCROLL_CASES:
        r = fl.place(d(24), d(160))
        fill(scr, r, tok["surface.window"])
        scrollbar_draw(scr, tok, r, lines, page, top, state, s)
    return "8 scroll bars, bottom %d" % fl.bottom()


# ---- The menus, the M button and the Restart dialog (1.3 stage 5) ----

# kernel/display/ui_menu.h: size in dp, a header, the rows' tops and the
# dividers in dp, each row's label, icon and action.
MENU_DEFS = {
    "M": {"w": 288, "h": 182, "header": True, "rows": [61, 97, 142], "dividers": [56, 137],
          "labels": [b"About", b"Console", b"Power..."], "icons": ["about", "console", "power"],
          "actions": ["about", "console", "power"]},
    "desktop": {"w": 224, "h": 80, "header": False, "rows": [4, 40], "dividers": [],
                "labels": [b"Open Console", b"About Madire OS"], "icons": ["console", "about"],
                "actions": ["console", "about"]},
}


def menu_place_m(s, bar):
    d = MENU_DEFS["M"]
    return (px(8, s), max(bar - px(8, s) - px(d["h"], s), 0), px(d["w"], s), px(d["h"], s))


def menu_place_at(s, width, bar, x, y):
    d = MENU_DEFS["desktop"]
    w, h = px(d["w"], s), px(d["h"], s)
    return (max(min(x, width - w), 0), max(min(y, bar - h), 0), w, h)


def menu_new(kind, s, r):
    return {"kind": kind, "s": s, "r": r, "sel": -1, "ring": False}


def menu_row_rect(m, row):
    d, s, r = MENU_DEFS[m["kind"]], m["s"], m["r"]
    if not 0 <= row < len(d["rows"]):
        return (0, 0, 0, 0)
    e, t = px(1, s), d["rows"][row]
    y0, y1 = r[1] + px(t, s), r[1] + px(t + 36, s)
    return (r[0] + e, y0, r[2] - 2 * e, y1 - y0)


def menu_row_at(m, x, y):
    for i in range(len(MENU_DEFS[m["kind"]]["rows"])):
        if WM.inside(menu_row_rect(m, i), x, y):
            return i
    return -1


def menu_next(m, d):
    n = len(MENU_DEFS[m["kind"]]["rows"])
    if not 0 <= m["sel"] < n:
        return 0 if d > 0 else n - 1
    return (m["sel"] + (1 if d > 0 else n - 1)) % n


def wordmark(scr, tok, pen, top, ts, r_heavy, r_light):
    """The wordmark "MADIRE OS" in text (make-wallpaper.py's capsule
    letters, the About window's way), its pen and cap top in 24.8."""
    track = K.fx(6)
    text_c = K.rgb(rgb3(tok["text"]))
    for i, ch in enumerate(MW.WORDMARK):
        a, polys = MW.kernel_glyph(ch)
        cov = {}
        placed = [[(pen + K.mul(qx, ts), top + K.mul(qy, ts)) for qx, qy in poly] for poly in polys]
        MW.polys_coverage(K, cov, placed, r_heavy if i < 6 else r_light, scr.w, scr.h)
        paint_k(scr, cov, lambda x: text_c)
        pen += K.mul(a + track, ts)


def menu_draw(scr, tok, m):
    """A menu (ui_menu.h's rules)."""
    d, s, r = MENU_DEFS[m["kind"]], m["s"], m["r"]
    R, e = px(6, s), px(1, s)
    fill_round(scr, r, R, 15, tok["surface"])
    if d["header"]:
        P, hy, hh = px(24, s), r[1] + px(4, s), px(52, s) - px(4, s)
        icon(scr, "M", r[0] + px(16, s), hy + (hh - P) // 2, P, tok["accent"])
        wordmark(scr, tok, (r[0] + px(52, s)) << 8, (hy + (hh - px(10, s)) // 2) << 8, K.fx(10 * s, 56),
                 K.fx(500 * s, 2576), K.fx(250 * s, 2576))
    for dv in d["dividers"]:
        y0, y1 = r[1] + px(dv, s), r[1] + px(dv + 1, s)
        x0, x1 = r[0] + px(16, s), r[0] + px(d["w"] - 16, s)
        fill(scr, (x0, y0, x1 - x0, y1 - y0), tok["border"])
    f = font(s, False)
    for i, label_text in enumerate(d["labels"]):
        row = menu_row_rect(m, i)
        if i == m["sel"]:
            keep = scr.clip
            scr.clip = meet(keep, row)
            if scr.clip is not None:
                fill_round(scr, inflate(r, -e), max(R - e, 0), 15, tok["surface.active"])
            scr.clip = keep
            g, inset_px = px(2, s), px(4, s)
            fill(scr, (row[0], row[1] + inset_px, g, row[3] - 2 * inset_px), tok["accent"])
            if m["ring"]:
                frame_round(scr, (row[0] + g, row[1], row[2] - 2 * g, row[3]), px(4, s), g, 15, tok["accent"])
        ip = px(16, s)
        icon(scr, d["icons"][i], r[0] + px(16, s), row[1] + (row[3] - ip) // 2, ip, tok["text"])
        text(scr, f, r[0] + px(44, s), row[1] + (row[3] - f[2]) // 2, label_text, tok["text"])
    frame_round(scr, r, R, e, 15, tok["border.panel"])


DIALOG_LINES = (b"Shut down or restart Madire OS?", b"Files in Live mode are lost.")
# 1.5 stage 6: the power dialog's buttons left to right (Tab's order), then
# its close button, as the desktoplog lines name them.
DIALOG_PARTS = ("Cancel", "Restart", "Shut down", "the close button")
DIALOG_LABELS = (b"Cancel", b"Restart", b"Shut down")
DIALOG_HITS = ("cancel", "restart", "shutdown")


def dialog_new(w, h, s, ring):
    """The power dialog (ui_dialog.h) on a w x h screen: centred above
    the taskbar, Cancel focused, the ring as given, every button normal."""
    dw, dh, bar = px(360, s), px(172, s), h - px(40, s)
    return {"r": ((w - dw) // 2, max((bar - dh) // 2, 0), dw, dh), "s": s, "focus": 0, "ring": ring,
            "btn": ["normal", "normal", "normal"], "close": "normal"}


def dialog_button(r, s, b):
    """Button b (0 Cancel, 1 Restart, 2 Shut down): 96 dp wide from 32,
    136 or 240 dp across, from 116 to 148 dp down."""
    a = (32, 136, 240)[b]
    x0, x1 = px(a, s), px(a + 96, s)
    return (r[0] + x0, r[1] + px(116, s), x1 - x0, px(148, s) - px(116, s))


def dialog_hit(dlg, x, y):
    r, s = dlg["r"], dlg["s"]
    if not WM.inside(r, x, y):
        return None
    tb = px(32, s)
    if WM.inside((r[0] + r[2] - tb, r[1], tb, tb), x, y):
        return "close"
    for b, name in enumerate(DIALOG_HITS):
        if WM.inside(dialog_button(r, s, b), x, y):
            return name
    return "body"


def dialog_draw(scr, tok, s, dlg):
    """The dialog: a window (always active) with its text and buttons
    (Cancel and Restart in the neutral style, Shut down in the accent
    style, bold)."""
    t = tok
    st = [([t["surface.active"], t["surface.hover"], t["accent.pressed"]], [t["text"]] * 3, False, 4, 15),
          ([t["accent"], t["accent.hover"], t["accent.pressed"]], [t["text.on.accent"], t["text.on.accent"], t["text"]],
           True, 4, 15)]

    def client(scr_, r):
        f = font(s, False)
        text(scr_, f, r[0] + px(24, s), r[1] + px(56, s), DIALOG_LINES[0], tok["text"])
        text(scr_, f, r[0] + px(24, s), r[1] + px(76, s), DIALOG_LINES[1], tok["text"])
        for b in (0, 1, 2):
            button(scr_, tok, dialog_button(r, s, b), st[1 if b == 2 else 0], dlg["btn"][b],
                   dlg["ring"] and dlg["focus"] == b, s, lab=DIALOG_LABELS[b])
    window(scr, tok, s, dlg["r"], b"Power", "power", True, dlg["close"], client)


MENU_NAMES = {"M": "M", "desktop": "desktop"}
WHY = {"mouse": "the mouse", "key": "the Windows key", "esc": "Esc", "outside": "a click outside",
       "windows": "the Windows key", "m": "the M button", "chosen": "a row chosen", "cancel": "Cancel",
       "close": "the close button"}
KEYS = {"up": "up", "down": "down", "ret": "enter", "kp_enter": "enter", "tab": "tab", "esc": "esc",
        "meta_l": "windows", "meta_r": "windows"}


class DeskKeys:
    """1.5 stage 9b: kernel/display/deskkeys_core.h written again: the Super
    key (opens the M menu on its release, alone) and the window shortcuts.
    key() takes a qcode and the modifiers held (qmp_desktop.py's mods);
    QEMU sends Pause only when it is pressed, so its release is no event."""

    SUPER = {"meta_l": 1, "meta_r": 2}
    WITH_SUPER = {"up": "up", "down": "down", "left": "left", "right": "right", "pause": "about",
                  "d": "show_desktop"}

    def __init__(self):
        self.held, self.other = 0, False

    def pointer(self):
        if self.held:
            self.other = True

    def lost(self):
        """A key event dropped (the kernel's keyboard_dropped() moved)."""
        self.held, self.other = 0, False

    def key(self, qcode, pressed, mods, menu_open, dlg_open, switching, focused):
        """"pass", "taken", "none" (no kernel event: Pause's release),
        "menu" or a shortcut name for WM.shortcut."""
        if qcode == "pause" and not pressed:
            return "none"
        bit = self.SUPER.get(qcode)
        if bit:
            if pressed:
                if self.held & bit:
                    return "taken"
                if not self.held:
                    self.other = False
                self.held |= bit
                return "taken"
            if not self.held & bit:
                return "taken"
            self.held &= ~bit
            if self.held:
                return "taken"
            alone, self.other = not self.other, False
            return "menu" if alone and not dlg_open else "taken"
        if self.held:
            if not pressed:
                return "pass"
            self.other = True
            if dlg_open:
                return "taken"
            if qcode == "m":
                return "restore_all" if mods & {"shift", "shift_r"} else "minimise_all"
            return self.WITH_SUPER.get(qcode, "taken")
        if qcode == "f4" and pressed and mods & {"alt", "alt_r"} and not mods & {"ctrl", "ctrl_r"}:
            if switching or menu_open or dlg_open or not focused:
                return "taken"
            return "close"
        return "pass"


class Menus:
    """ui_menus.h's rules written again: the pointer and the keys on the
    menus and the dialog, before the window manager (WM); each change kept
    as the kernel's desktoplog line would say it, in the kernel's order.
    1.5 stage 9b: it holds the Super key's state (dk, DeskKeys), which
    every mouse button press reaches first (desktop_pointer)."""

    def __init__(self, wm):
        self.wm = wm
        self.dk = DeskKeys()
        bar = px(40, wm.s)
        self.m_cell = (0, wm.h - bar, bar, bar)
        self.menu, self.dlg = None, None
        self.x, self.y = wm.x, wm.y
        self.left, self.press = False, None
        self.m_state = "normal"
        self.done, self.restart, self.shutdown = None, False, False
        self.events = []

    def ev(self, line):
        self.events.append("desktop: " + line)

    def view(self):
        return {"m": self.m_state, "menu": dict(self.menu) if self.menu else None,
                "dialog": {**self.dlg, "btn": list(self.dlg["btn"])} if self.dlg else None,
                "task": self.wm.task_view()}                    # 1.5 stage 8

    def state(self):
        m = self.menu
        d = self.dlg
        return (self.m_state, (m["kind"], m["r"], m["sel"], m["ring"]) if m else None,
                (d["focus"], d["ring"], tuple(d["btn"]), d["close"]) if d else None)

    def over_m(self):
        return WM.inside(self.m_cell, self.x, self.y)

    def name(self):
        return MENU_NAMES[self.menu["kind"]]

    def open_menu(self, kind, by_key):
        s = self.wm.s
        r = menu_place_m(s, self.wm.bar) if kind == "M" else menu_place_at(s, self.wm.w, self.wm.bar, self.x, self.y)
        self.menu = menu_new(kind, s, r)
        if by_key:
            self.menu["sel"], self.menu["ring"] = 0, True
        self.ev("menu %s opened by %s at (%d, %d) %dx%d%s" % ((MENU_NAMES[kind], WHY["key" if by_key else "mouse"])
                                                             + r + (", About selected" if by_key else "",)))

    def wm_follows(self):
        self.wm.pointer(self.x, self.y)

    def close_menu(self, why):
        if not self.menu:
            return
        self.ev("menu %s closed (%s)" % (self.name(), WHY[why]))
        self.menu = None
        self.wm_follows()

    def select(self, row, ring):
        m = self.menu
        if m["sel"] == row and m["ring"] == ring:
            return
        m["sel"], m["ring"] = row, ring
        if row < 0:
            self.ev("menu %s: no row selected" % self.name())
        else:
            self.ev("menu %s: %s selected (%s)" % (self.name(), MENU_DEFS[m["kind"]]["labels"][row].decode(),
                                                   "the keyboard" if ring else "the pointer"))

    def wm_open(self, name):
        """ui_wm_open: open the window, or raise and focus it if it is open
        (1.5 stage 8: restored first if it is minimised)."""
        win = next((w for w in self.wm.z if w["name"] == name), None)
        if win is None:
            w, h = about_size(self.wm.s) if name == "About" else console_size(self.wm.s)
            self.wm.open(name, w, h, name == "About", WINDOW_BUTTONS[name])
        else:
            self.wm.activate(win)

    def choose(self, row, by_key):
        d = MENU_DEFS[self.menu["kind"]]
        action = d["actions"][row]
        self.ev("menu %s: %s chosen" % (self.name(), d["labels"][row].decode()))
        self.close_menu("chosen")
        if action in ("about", "console"):
            self.wm_open("About" if action == "about" else "Console")
            self.done = action
        else:
            self.dlg_open(by_key)

    def dlg_open(self, ring):
        self.dlg = dialog_new(self.wm.w, self.wm.h, self.wm.s, ring)
        self.ev("dialog Power opened at (%d, %d) %dx%d, Cancel focused%s" % (
            self.dlg["r"] + (" (the focus ring on)" if ring else "",)))
        self.dlg_states()

    def dlg_close(self, why):
        self.dlg = None
        self.ev("dialog Power closed (%s)" % WHY[why])
        self.wm_follows()

    def do_restart(self):
        self.restart = True
        self.ev("dialog Power: Restart chosen")

    def do_shutdown(self):
        self.shutdown = True
        self.ev("dialog Power: Shut down chosen")

    def dlg_states(self):
        at = dialog_hit(self.dlg, self.x, self.y)
        for i, part in enumerate(DIALOG_HITS + ("close",)):
            over = at == part
            if self.left and self.press == part:
                st = "pressed" if over else "normal"
            else:
                st = "hover" if over and not self.left else "normal"
            now = self.dlg["btn"][i] if i < 3 else self.dlg["close"]
            if st == now:
                continue
            if i < 3:
                self.dlg["btn"][i] = st
            else:
                self.dlg["close"] = st
            self.ev("dialog Power: %s %s" % (DIALOG_PARTS[i], st))

    def update_m(self):
        over = self.over_m()
        if self.menu and self.menu["kind"] == "M":
            st = "open"
        elif self.left and self.press == "m":
            st = "pressed" if over else "normal"
        elif over and not self.left and not self.wm.left and not self.menu and not self.dlg:
            st = "hover"
        else:
            st = "normal"
        if st != self.m_state:
            self.m_state = st
            self.ev("M button: %s" % st)

    def release(self):
        press, self.left, self.press = self.press, False, None
        if press == "m" and self.over_m():
            if self.menu and self.menu["kind"] == "M":
                self.close_menu("m")
            elif not self.menu:
                self.open_menu("M", False)
        elif press == "menu" and self.menu:
            row = menu_row_at(self.menu, self.x, self.y)
            if row >= 0:
                self.choose(row, False)

    def pointer(self, x, y, down=False, up=False, right=False, double=False):
        self.x, self.y = x, y
        if down or right:
            self.dk.pointer()                           # 1.5 stage 9b
        if self.dlg:
            at = dialog_hit(self.dlg, x, y)
            if down and not self.left:
                self.left = True
                self.press = at if at in DIALOG_HITS + ("close",) else "outside"
            closed = False
            if up and self.left:
                press, self.left, self.press = self.press, False, None
                if press == "cancel" and at == "cancel":
                    self.dlg_close("cancel")
                    closed = True
                elif press == "close" and at == "close":
                    self.dlg_close("close")
                    closed = True
                elif press == "restart" and at == "restart":
                    self.do_restart()
                elif press == "shutdown" and at == "shutdown":
                    self.do_shutdown()
            if not closed:
                self.dlg_states()
        elif self.menu:
            row = menu_row_at(self.menu, x, y)
            if row >= 0:
                self.select(row, False)
            elif not self.menu["ring"]:
                self.select(-1, False)
            inside = WM.inside(self.menu["r"], x, y)
            if down and not self.left:
                self.left = True
                if self.menu["kind"] == "M" and self.over_m():
                    self.press = "m"
                elif inside:
                    self.press = "menu"
                else:
                    self.press = "outside"
                    self.close_menu("outside")
            elif right and not inside:
                self.close_menu("outside")
            if up and self.left:
                self.release()
        elif self.left:
            if up:
                self.release()
        elif down and self.over_m() and not self.wm.left:
            self.left, self.press = True, "m"
        else:
            self.wm.pointer(x, y, down, up, double)
            if right and not self.wm.left and self.wm.hit(x, y)[0] == "desktop":
                self.open_menu("desktop", False)
        self.update_m()

    def middle(self, x, y):
        """1.5 stage 8 (desktop_pointer): the middle button pressed, after
        the packet's pointer() call; nothing with a menu or the dialog open."""
        self.dk.pointer()                               # 1.5 stage 9b
        if not self.menu and not self.dlg:
            self.wm.middle(x, y)

    def close(self):
        """1.5 stage 9b (ui_menus_close): a Super key shortcut closes an open
        menu first."""
        if self.menu:
            self.close_menu("windows")
        self.update_m()

    def key(self, qcode, pressed=True):
        """One key event (its qcode); "wm" if the window manager takes it."""
        k = KEYS.get(qcode, "other")
        route = "taken"
        if self.dlg:
            if pressed and k == "enter":
                if self.dlg["focus"] == 0:
                    self.dlg_close("cancel")
                elif self.dlg["focus"] == 1:
                    self.do_restart()
                else:
                    self.do_shutdown()
            elif pressed and k == "tab":
                self.dlg["focus"], self.dlg["ring"] = (self.dlg["focus"] + 1) % 3, True
                self.ev("dialog Power: %s focused" % DIALOG_PARTS[self.dlg["focus"]])
            elif pressed and k == "esc":
                self.dlg_close("esc")
        elif k == "windows":
            if pressed and self.menu and self.menu["kind"] == "M":
                self.close_menu("windows")
            elif pressed:
                self.close_menu("windows")
                self.left, self.press = False, None
                self.wm.cancel()
                self.open_menu("M", True)
        elif self.menu:
            if pressed and k in ("up", "down"):
                self.select(menu_next(self.menu, 1 if k == "down" else -1), True)
            elif pressed and k == "enter" and self.menu["sel"] >= 0:
                self.choose(self.menu["sel"], True)
            elif pressed and k == "esc":
                self.close_menu("esc")
        else:
            route = "wm"
        self.update_m()
        return route


def scene_menus(scr, tok, s, name):
    """tests/desktopdump.c's stage 5 scenes on bg.deep: the taskbar with the
    clock 09:41 and the M button in its state, a menu or the dialog."""
    W, H = scr.w, scr.h
    bar = H - px(40, s)
    m, menu, dlg = "normal", None, None
    if name == "mbutton_hover":
        m = "hover"
    elif name == "mbutton_pressed":
        m = "pressed"
    elif name == "menu_desktop":
        menu = menu_new("desktop", s, menu_place_at(s, W, bar, W - px(10, s), bar - px(10, s)))
        menu["sel"] = 1
    elif name.startswith("menu_"):
        m = "open"
        menu = menu_new("M", s, menu_place_m(s, bar))
        if name == "menu_key":
            menu["sel"], menu["ring"] = 0, True
        elif name == "menu_hover":
            menu["sel"] = 2
    else:
        dlg = dialog_new(W, H, s, False)
        if name == "dialog":
            dlg["close"] = "hover"
        elif name == "dialog_key":
            dlg["ring"], dlg["btn"][2] = True, "hover"      # 1.5 stage 6: Shut down (was Restart)
        elif name == "dialog_tab":
            dlg["focus"], dlg["ring"], dlg["btn"][0] = 1, True, "hover"
        elif name == "dialog_pressed_cancel":
            dlg["focus"], dlg["ring"], dlg["btn"][0] = 1, True, "pressed"
        elif name == "dialog_pressed_restart":
            dlg["ring"], dlg["btn"][1] = True, "pressed"
        elif name == "dialog_pressed_shutdown":                 # 1.5 stage 6
            dlg["ring"], dlg["btn"][2] = True, "pressed"
    taskbar(scr, tok, FIXED_CLOCK, m)
    desc = "the M button %s" % m
    if menu is not None:
        menu_draw(scr, tok, menu)
        desc += "; the %s menu at (%d, %d) %dx%d, row %d selected%s" % (
            (menu["kind"],) + menu["r"] + (menu["sel"], ", the ring on" if menu["ring"] else ""))
    if dlg is not None:
        dialog_draw(scr, tok, s, dlg)
        desc += "; the dialog at (%d, %d) %dx%d, focus %s%s, buttons %s %s %s, close %s" % (
            dlg["r"] + (DIALOG_PARTS[dlg["focus"]], " (ring)" if dlg["ring"] else "", dlg["btn"][0], dlg["btn"][1],
                        dlg["btn"][2], dlg["close"]))
    return desc


MENU_SCENES = ("mbutton_hover", "mbutton_pressed", "menu_mouse", "menu_key", "menu_hover", "menu_desktop", "dialog",
               "dialog_key", "dialog_tab", "dialog_pressed_cancel", "dialog_pressed_restart",
               "dialog_pressed_shutdown")


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


CARRIED = ("carried-bar", "carried-pointer")     # 1.3 stage 5 (kernel/display/ui_trace.h)


def contrast_check(path):
    """Every pair recorded by tests/desktopdump.c (PAIR lines) must be a row
    of section 12's table (Text for text, Icons for icons and state marks)
    whose result is pass, with its stated ratio equal to the ratio
    recomputed here from tokens.h's values (WCAG 2.x, two decimals), at or
    above its rule's minimum. 1.3 stage 5: a carried pair (a fill whose
    state a bar or the pointer carries) must be a row of the Icons table
    whose result starts with "**fail alone**", its stated ratio equal to
    the recomputed one; carried-bar: the row's result names the `accent`
    bar, and in every scene that recorded the pair, the same scene also
    recorded the bar as a state pair (`accent` on that fill) that passes;
    carried-pointer: the row's result names the pointer."""
    defs = c_defines("kernel/display/tokens.h")
    names = {}
    for t, _, _ in guide_tokens():
        v = defs.get("UI_" + t.upper().replace(".", "_"))
        if v is not None:
            names.setdefault(v, []).append(t)
    tables = contrast_tables()
    pairs, scenes, lost = {}, set(), 0
    in_scene = {}                       # (kind, fg, bg) -> the scenes that recorded it
    for line in open(path, encoding="ascii"):
        w = line.split()
        if w[:2] == ["PAIRS", "LOST"]:
            lost += 1
        if len(w) != 6 or w[0] != "PAIR":
            continue
        key = (w[1], int(w[2], 16), int(w[3], 16))
        pairs[key] = pairs.get(key, 0) + int(w[4])
        scenes.add(w[5])
        in_scene.setdefault(key, set()).add(w[5])
    bad = lost
    state_ok = {}                       # (fg, bg) of a state pair -> it passes
    carried = []
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
        if kind in CARRIED:
            carried.append((kind, fg, bg, desc, fname, bname, rule, got, stated, result, where))
            continue
        ok = (rule in RULES and result.startswith("pass") and round(got, 2) == stated and got >= RULES[rule])
        print("CONTRAST %s %s: %s on %s, rule %s, ratio %.2f (the table: %.2f, %s): %s" % (
            "pass" if ok else "FAIL", desc, fname, bname, rule, got, stated, result, where))
        bad += not ok
        if kind == "state":
            state_ok[(fg, bg)] = ok
    accent = defs.get("UI_ACCENT")
    for kind, fg, bg, desc, fname, bname, rule, got, stated, result, where in carried:
        ok = result.startswith("**fail alone**") and round(got, 2) == stated
        why = ""
        if kind == "carried-bar":
            missing = sorted(sc for sc in in_scene[(kind, fg, bg)]
                             if sc not in in_scene.get(("state", accent, fg), set()))
            ok = ok and "`accent` bar" in result and state_ok.get((accent, fg), False) and not missing
            why = "; its accent bar recorded and passing in every scene" if ok else \
                  "; no passing accent bar on it in: %s" % (" ".join(missing) or "(the bar's pair fails)")
        else:
            ok = ok and "pointer" in result
        print("CONTRAST carried %s %s: %s on %s, rule %s, ratio %.2f (the table: %.2f, %s)%s: %s" % (
            "pass" if ok else "FAIL", desc, fname, bname, rule, got, stated, result, why, where))
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
          "console_about": lambda scr, tok, s: scene_console(scr, tok, s, "about"),
          # 1.5 stage 8
          "chrome": lambda scr, tok, s: scene_chrome(scr, tok, s, "normal"),
          "chrome_hover": lambda scr, tok, s: scene_chrome(scr, tok, s, "hover"),
          "chrome_max": lambda scr, tok, s: scene_chrome(scr, tok, s, "max"),
          "taskbuttons": scene_taskbuttons,
          # 1.5 stage 10
          "textfield": scene_textfield, "list": scene_list, "grid": scene_grid, "scrollbar": scene_scrollbar}
SCENES.update({name: (lambda scr, tok, s, n=name: scene_menus(scr, tok, s, n)) for name in MENU_SCENES})


def scale_of(w, h):
    """The console's scale (fbcon_core.h): from the height, then smaller until 80 x 25 cells fit."""
    want = 4 if h < 900 else 5 if h < 1080 else 6 if h < 1440 else 8
    for s in (8, 6, 5, 4):
        if s <= want and 80 * (FONT_PX[s] // 2) <= w and 25 * FONT_PX[s] <= h:
            return s
    return None


# 1.4 stage 12 (docs/plans/1.4.md, C10): Disk mode's About Mode row and
# Restart line (ui_about.h, ui_dialog.h); "rw" or "ro", None for Live mode.
DISK_TEXTS = {
    "rw": (b"Disk (home is saved)", b"Files in /home are saved first."),
    "ro": (b"Disk (home read-only)", b"Disk is read-only; /home is unchanged."),
}
LIVE_TEXTS = (ABOUT_MODE, DIALOG_LINES[1])


def set_disk_mode(kind):
    """The About and Restart texts of Live mode (None) or Disk mode ("rw",
    "ro"), for every later rendering (tests/qmp_desktop.py --diskmode)."""
    global ABOUT_MODE, DIALOG_LINES
    mode, line = DISK_TEXTS[kind] if kind else LIVE_TEXTS
    ABOUT_MODE = mode
    DIALOG_LINES = (DIALOG_LINES[0], line)


def render(w, h, scene):
    tok = colours()
    s = scale_of(w, h)
    # 1.4 stage 12: <base>_disk and <base>_diskro: <base> in Disk mode.
    for suffix, kind in (("_diskro", "ro"), ("_disk", "rw")):
        if scene.endswith(suffix) and scene[:-len(suffix)] in SCENES:
            set_disk_mode(kind)
            scene = scene[:-len(suffix)]
            break
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
