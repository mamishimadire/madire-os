#!/usr/bin/env python3
"""The mouse pointer's reference renderer (1.2 stage 7; standard library
only; used as a module by tests/qmp_mouse.py).

Draws the pointer on an RGB screen (3 bytes per pixel, row by row) from
the rules written in kernel/display/pointer_core.h, independently of the
kernel's C:

  - the shape: the 12 x 18 rows of `pointer_shape` in
    kernel/display/pointer_core.c, the sprite's definition ('.' none, 'o'
    edge, 'w' fill, 'a' spine), read from that file;
  - the colours: the Design Guide's adopted tokens (docs/design/
    design-guide.md, section 2), read from its table: the edge `bg.deep`,
    the fill `text`, the spine `accent`;
  - the scale from the screen height H (decision 19): H < 900 -> 4,
    H < 1080 -> 5, H < 1440 -> 6, otherwise 8; the sprite (12 s + 2) // 4
    x (18 s + 2) // 4 pixels, pixel (x, y) showing the shape's cell
    (x * 12 // w, y * 18 // h);
  - the hot spot (the top-left pixel) at the pointer's position; pixels
    past the right or bottom edge are not drawn.

1.5 stage 14b: the other kinds of pointer (kernel/display/pointer_core.h,
notes (11), (12)(b), (14), (15)): paint(..., shape=<name>, frame=<n>) draws
kind <name> ("resize-ew", ..., "no-drop"; "arrow", the default, exactly as
above) from its rows and hot spot in pointer_core.c's pointer_defs table:
'.' none, 'o' bg.deep, 'w' text, 'a' accent, 'd' danger, '1'-'8' the busy
pointer's dots (the one of the frame in accent, the others in text); the
sprite (W s + 2) // 4 x (H s + 2) // 4, pixel (x, y) showing the cell
(x W // w, y H // h); its top left at the position less the hot spot
((hx s + 2) // 4, (hy s + 2) // 4); pixels off the screen on any side not
drawn.
"""
import os
import re

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
SHAPE_W, SHAPE_H = 12, 18
TOKENS = {"o": "bg.deep", "w": "text", "a": "accent"}


def read_shape(root=ROOT):
    src = open(os.path.join(root, "kernel/display/pointer_core.c"), encoding="ascii").read()
    m = re.search(r"pointer_shape\[POINTER_DP_H\] = \{(.*?)\};", src, re.S)
    if not m:
        raise ValueError("pointer_shape not found in kernel/display/pointer_core.c")
    rows = re.findall(r'"([^"]*)"', m.group(1))
    if len(rows) != SHAPE_H or any(len(r) != SHAPE_W or set(r) - set(".owa") for r in rows):
        raise ValueError("pointer_shape: not 18 rows of 12 of '.owa'")
    return rows


def read_tokens(root=ROOT):
    """{token: (r, g, b)}: the adopted column of the Design Guide's colour table."""
    out = {}
    for line in open(os.path.join(root, "docs/design/design-guide.md"), encoding="utf-8"):
        cols = [c.strip() for c in line.strip().strip("|").split("|")]
        if len(cols) >= 4 and re.fullmatch(r"`[a-z.]+`", cols[0]):
            m = re.match(r"`#([0-9A-Fa-f]{6})`", cols[3])
            if m:
                out[cols[0].strip("`")] = bytes.fromhex(m.group(1))
    for t in TOKENS.values():
        if t not in out:
            raise ValueError("Design Guide: no adopted colour for %s" % t)
    return out


def scale_for_height(h):
    return 4 if h < 900 else 5 if h < 1080 else 6 if h < 1440 else 8


def sprite(scale, root=ROOT):
    """(w, h, rows of characters) of the sprite at a scale step."""
    shape = read_shape(root)
    w, h = (SHAPE_W * scale + 2) // 4, (SHAPE_H * scale + 2) // 4
    return w, h, ["".join(shape[y * SHAPE_H // h][x * SHAPE_W // w] for x in range(w)) for y in range(h)]


KIND_TOKENS = {"o": "bg.deep", "w": "text", "a": "accent", "d": "danger"}


def read_kinds(root=ROOT):
    """{name: (rows, w, h, hx, hy)} from pointer_core.c's pointer_defs
    (1.5 stage 14b; the arrow's rows are pointer_shape's)."""
    src = open(os.path.join(root, "kernel/display/pointer_core.c"), encoding="ascii").read()
    arrays = {"pointer_shape": read_shape(root)}
    for m in re.finditer(r"static const char \*const (pointer_rows_\w+)\[(\d+)\] = \{(.*?)\};", src, re.S):
        rows = re.findall(r'"([^"]*)"', m.group(3))
        if len(rows) != int(m.group(2)):
            raise ValueError("%s: %d rows, not %s" % (m.group(1), len(rows), m.group(2)))
        arrays[m.group(1)] = rows
    m = re.search(r"pointer_defs\[POINTER_KINDS\] = \{(.*?)\};", src, re.S)
    if not m:
        raise ValueError("pointer_defs not found in kernel/display/pointer_core.c")
    kinds = {}
    for e in re.finditer(r'\{ "([a-z-]+)", (\w+), (\d+), (\d+), (\d+), (\d+) \}', m.group(1)):
        name, arr, w, h, hx, hy = e.group(1), e.group(2), *(int(e.group(k)) for k in range(3, 7))
        rows = arrays[arr]
        if len(rows) != h or any(len(r) != w or set(r) - set(".owad12345678") for r in rows):
            raise ValueError("pointer_defs %s: not %d rows of %d known cells" % (name, h, w))
        kinds[name] = (rows, w, h, hx, hy)
    if len(kinds) != 12:
        raise ValueError("pointer_defs: %d kinds, not 12" % len(kinds))
    return kinds


def kind_sprite(name, scale, frame=0, root=ROOT):
    """(w, h, hx, hy, rows of characters) of kind `name` at a scale step,
    the busy pointer's dots resolved for `frame` ('a' or 'w')."""
    rows, W, H, hx, hy = read_kinds(root)[name]
    w, h = (W * scale + 2) // 4, (H * scale + 2) // 4
    out = []
    for y in range(h):
        line = ""
        for x in range(w):
            ch = rows[y * H // h][x * W // w]
            if ch in "12345678":
                ch = "a" if int(ch) - 1 == frame % 8 else "w"
            line += ch
        out.append(line)
    return w, h, (hx * scale + 2) // 4, (hy * scale + 2) // 4, out


def paint(screen, width, height, x, y, root=ROOT, shape="arrow", frame=0):
    """Returns the screen (RGB bytes) with the pointer's hot spot at (x, y),
    and a description. 1.5 stage 14b: `shape` another kind (its own rules
    above); the arrow's path below is unchanged."""
    if shape != "arrow":
        return paint_kind(screen, width, height, x, y, shape, frame, root)
    scale = scale_for_height(height)
    w, h, rows = sprite(scale, root)
    colours = {k: read_tokens(root)[t] for k, t in TOKENS.items()}
    out = bytearray(screen)
    for r in range(h):
        for c in range(w):
            ch = rows[r][c]
            if ch != "." and x + c < width and y + r < height:
                o = ((y + r) * width + x + c) * 3
                out[o:o + 3] = colours[ch]
    return bytes(out), "pointer %dx%d (scale %d %%) at (%d, %d)" % (w, h, scale * 25, x, y)


def paint_kind(screen, width, height, x, y, shape, frame=0, root=ROOT):
    """1.5 stage 14b: kind `shape` with its hot spot at (x, y)."""
    scale = scale_for_height(height)
    w, h, hx, hy, rows = kind_sprite(shape, scale, frame, root)
    tok = read_tokens(root)
    colours = {k: tok[t] for k, t in KIND_TOKENS.items()}
    out = bytearray(screen)
    for r in range(h):
        for c in range(w):
            ch = rows[r][c]
            px, py = x - hx + c, y - hy + r
            if ch != "." and 0 <= px < width and 0 <= py < height:
                o = (py * width + px) * 3
                out[o:o + 3] = colours[ch]
    return bytes(out), "pointer %s %dx%d (scale %d %%, frame %d) at (%d, %d)" % (shape, w, h, scale * 25,
                                                                                frame % 8, x, y)
