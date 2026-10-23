#!/usr/bin/env python3
"""The framebuffer console's reference renderer (1.2 stage 6; standard
library only).

Renders the screen Madire's framebuffer console must show for a given VGA
cell grid, independently of the kernel: it reads the pinned BDF files in
third_party/terminus/ itself (never the generated C, never
scripts/make-font.py) and follows the rules written in
kernel/display/fbcon_core.h:

  - scale from the screen height H: H < 900 -> 8x16, H < 1080 -> 10x20,
    H < 1440 -> 12x24, otherwise 16x32 (decision 19); if 80 x 25 cells do
    not fit, the next smaller size;
  - the grid centred: ((W - 80 w) // 2, (H - 25 h) // 2); black elsewhere;
  - a cell: the glyph of its code page 437 character, set pixels in the
    attribute's foreground slot, clear ones in its background slot (bits
    0-3 and 4-7); the cursor, if shown, fills rows [h*13//16, h*15//16) of
    its cell with the foreground colour;
  - a slot's 6-bit DAC value v is (v << 2) | (v >> 4) per channel.

The cell grid comes from the kernel's vga_shadow (drivers/vga/vga_text.h),
read from guest memory: 2000 little-endian 16-bit cells (character |
attribute << 8), 16 x 3 bytes of palette, then cursor_visible, cursor_row,
cursor_col (4051 bytes).

    fbcon_ref.py FONTDIR SHADOW WIDTH HEIGHT [SCREEN.ppm]

renders, and with SCREEN.ppm compares, printing "REF ..." lines (used as a
module by tests/qmp_fbcon.py).
"""
import struct
import sys

SHADOW_SIZE = 2000 * 2 + 48 + 3
FONT_FILES = {16: "ter-u16n.bdf", 20: "ter-u20n.bdf", 24: "ter-u24n.bdf", 32: "ter-u32n.bdf"}
SIZES = [(8, 16), (10, 20), (12, 24), (16, 32)]

# Code page 437's graphic characters 0x01-0x1F (0x00 is a blank cell).
GRAPHIC_LOW = ("☺☻♥♦♣♠•◘○◙♂♀♪"
               "♫☼►◄↕‼¶§▬↨↑↓→"
               "←∟↔▲▼")


def code_point(byte):
    if byte == 0:
        return 0x20
    if byte < 0x20:
        return ord(GRAPHIC_LOW[byte - 1])
    if byte == 0x7F:
        return 0x2302                    # HOUSE
    return ord(bytes([byte]).decode("cp437"))


def read_bdf(path):
    """{code point: [row as an int, leftmost pixel the highest bit]}, width, height."""
    glyphs, width, height = {}, None, None
    enc, rows = None, None
    with open(path, encoding="ascii") as f:
        for line in f:
            w = line.split()
            if not w:
                continue
            if w[0] == "FONTBOUNDINGBOX":
                width, height = int(w[1]), int(w[2])
            elif w[0] == "ENCODING":
                enc = int(w[1])
            elif w[0] == "BITMAP":
                rows = []
            elif w[0] == "ENDCHAR":
                glyphs[enc] = rows
                enc, rows = None, None
            elif rows is not None:
                bits = len(w[0]) * 4
                rows.append(int(w[0], 16) >> (bits - width))
    return glyphs, width, height


def layout(width, height):
    want = 0 if height < 900 else 1 if height < 1080 else 2 if height < 1440 else 3
    for i in range(want, -1, -1):
        cw, ch = SIZES[i]
        if 80 * cw <= width and 25 * ch <= height:
            return cw, ch, (width - 80 * cw) // 2, (height - 25 * ch) // 2
    return None


def widen(v):
    v &= 0x3F
    return (v << 2) | (v >> 4)


def render(fontdir, shadow, width, height):
    """(RGB bytes of the expected screen, a description) for the shadow."""
    if len(shadow) < SHADOW_SIZE:
        raise ValueError("shadow: %d bytes, expected %d" % (len(shadow), SHADOW_SIZE))
    cells = struct.unpack_from("<2000H", shadow, 0)
    pal = shadow[4000:4048]
    colours = [bytes(widen(pal[3 * s + k]) for k in range(3)) for s in range(16)]
    cur_on, cur_row, cur_col = shadow[4048], shadow[4049], shadow[4050]
    lay = layout(width, height)
    if lay is None:
        raise ValueError("no layout for %dx%d" % (width, height))
    cw, ch, x0, y0 = lay
    glyphs, fw, fh = read_bdf(fontdir + "/" + FONT_FILES[ch])
    if (fw, fh) != (cw, ch):
        raise ValueError("font %s is %dx%d" % (FONT_FILES[ch], fw, fh))
    first, end = ch * 13 // 16, ch * 15 // 16
    out = bytearray(width * height * 3)
    for i, e in enumerate(cells):
        r, c = divmod(i, 80)
        g = glyphs[code_point(e & 0xFF)]
        fg, bg = colours[(e >> 8) & 0x0F], colours[e >> 12]
        cursor = cur_on and r == cur_row and c == cur_col
        for y in range(ch):
            bits = g[y]
            under = cursor and first <= y < end
            row = b"".join(fg if under or (bits >> (cw - 1 - x)) & 1 else bg for x in range(cw))
            o = ((y0 + r * ch + y) * width + x0 + c * cw) * 3
            out[o:o + cw * 3] = row
    desc = "%dx%d cells at (%d, %d), cursor %s" % (cw, ch, x0, y0,
                                                    "at row %d, column %d" % (cur_row, cur_col) if cur_on else "hidden")
    return bytes(out), desc


def compare(got, want, width):
    """(number of differing pixels, the first one as (x, y, got, want) or None)."""
    n, first = 0, None
    if len(got) != len(want):
        return -1, None
    if got == want:
        return 0, None
    for o in range(0, len(want), 3):
        if got[o:o + 3] != want[o:o + 3]:
            if first is None:
                p = o // 3
                first = (p % width, p // width, got[o:o + 3].hex(), want[o:o + 3].hex())
            n += 1
    return n, first


def read_ppm(path):
    d = open(path, "rb").read()
    fields, i = [], 0
    while len(fields) < 4:
        while d[i:i + 1].isspace():
            i += 1
        j = i
        while not d[j:j + 1].isspace():
            j += 1
        fields.append(d[i:j])
        i = j
    return int(fields[1]), int(fields[2]), d[i + 1:]


def main():
    if len(sys.argv) not in (5, 6):
        print(__doc__)
        return 1
    fontdir, shadow_path, w, h = sys.argv[1], sys.argv[2], int(sys.argv[3]), int(sys.argv[4])
    want, desc = render(fontdir, open(shadow_path, "rb").read(), w, h)
    print("REF %dx%d: %s" % (w, h, desc))
    if len(sys.argv) == 6:
        sw, sh, got = read_ppm(sys.argv[5])
        n, first = compare(got, want, w) if (sw, sh) == (w, h) else (-1, None)
        print("REF compare: screen %dx%d, %d pixels differ%s" % (sw, sh, n, "" if first is None else
              " (first at %d,%d: screen %s, reference %s)" % first))
    return 0


if __name__ == "__main__":
    sys.exit(main())
