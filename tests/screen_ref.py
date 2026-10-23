#!/usr/bin/env python3
"""Reference screens for the VirtualBox graphics group (1.2 stage 10b;
standard library only, so it also runs on the Windows host).

    screen_ref.py splash WIDTH HEIGHT OUTDIR
    screen_ref.py console WIDTH HEIGHT OUT
    screen_ref.py check-console SOCKET

splash: writes the splash's five screens as raw RGB files (3 bytes a
pixel, rows top to bottom), rendered by scripts/make-wallpaper.py, the one
reference algorithm: OUTDIR/splash-dots0.rgb ... splash-dots3.rgb (that
many of its dots lit) and OUTDIR/splash-ready.rgb (its ready state at
System ready).

console: writes the screen of the framebuffer console right after the
command "clear" as a raw RGB file: tests/fbcon_ref.py's rendering of the
cell grid the console then holds (console_cleared below: the prompt
"madire> " on row 0 in light cyan, the input cells after it in white, the
cursor at row 0, column 8, every other cell blank), with the BIOS's 16
colours (the console uses only slots the boot logo never reprograms).

check-console: the same reference against a running QEMU guest's screen,
over QMP (one screendump; the guest must be showing the console just after
"clear"). Prints "CONSOLE size=<w>x<h> differ=<pixels> (...)"; the
QEMU check that this reference is the kernel's screen, so the VirtualBox
group's comparison with it is a comparison with the kernel's rendering.

Each prints one line per file or result; any error prints
"screen_ref: ERROR: ..." and exits 1.
"""
import importlib.util
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
# No __pycache__/ from these imports (tests/ and scripts/ are files under test).
sys.dont_write_bytecode = True
import fbcon_ref  # noqa: E402

FONTDIR = os.path.join(HERE, "..", "third_party", "terminus")

# drivers/vga/vga_text.c's vga_shadow palette: the BIOS's 16 text colours
# (6-bit DAC values).
BIOS_PALETTE = [
    (0x00, 0x00, 0x00), (0x00, 0x00, 0x2A), (0x00, 0x2A, 0x00), (0x00, 0x2A, 0x2A),
    (0x2A, 0x00, 0x00), (0x2A, 0x00, 0x2A), (0x2A, 0x15, 0x00), (0x2A, 0x2A, 0x2A),
    (0x15, 0x15, 0x15), (0x15, 0x15, 0x3F), (0x15, 0x3F, 0x15), (0x15, 0x3F, 0x3F),
    (0x3F, 0x15, 0x15), (0x3F, 0x15, 0x3F), (0x3F, 0x3F, 0x15), (0x3F, 0x3F, 0x3F),
]
# kernel/console/console.c and lineedit.h: the attributes, the prompt's
# width and the input line's length.
ATTR_TEXT, ATTR_INPUT, ATTR_PROMPT = 0x07, 0x0F, 0x0B
PROMPT = "madire> "
LINE_MAX = 71


def console_cleared():
    """The vga_shadow bytes (fbcon_ref.SHADOW_SIZE) of the console right
    after "clear": cells, palette, cursor (shown, row, column)."""
    cells = [ord(" ") | ATTR_TEXT << 8] * 2000
    for i, ch in enumerate(PROMPT):
        cells[i] = ord(ch) | ATTR_PROMPT << 8
    for i in range(len(PROMPT), len(PROMPT) + LINE_MAX):
        cells[i] = ord(" ") | ATTR_INPUT << 8
    shadow = struct.pack("<2000H", *cells) + bytes(v for rgb in BIOS_PALETTE for v in rgb)
    return shadow + bytes([1, 0, len(PROMPT)])


def load_make_wallpaper():
    spec = importlib.util.spec_from_file_location("make_wallpaper",
                                                  os.path.join(HERE, "..", "scripts", "make-wallpaper.py"))
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def main():
    a = sys.argv[1:]
    try:
        if len(a) == 4 and a[0] == "splash":
            w, h, out = int(a[1]), int(a[2]), a[3]
            phases = load_make_wallpaper().splash_phases(w, h, [0, 1, 2, 3, "ready"], FONTDIR)
            for p, pix in phases.items():
                name = "splash-ready.rgb" if p == "ready" else f"splash-dots{p}.rgb"
                with open(os.path.join(out, name), "wb") as f:
                    f.write(pix)
                print(f"REFERENCE {name} {w}x{h} ({'its ready state' if p == 'ready' else f'{p} of the 3 dots lit'}; "
                      f"scripts/make-wallpaper.py)", flush=True)
            return 0
        if len(a) == 4 and a[0] == "console":
            w, h, out = int(a[1]), int(a[2]), a[3]
            pix, desc = fbcon_ref.render(FONTDIR, console_cleared(), w, h)
            with open(out, "wb") as f:
                f.write(pix)
            print(f"REFERENCE {os.path.basename(out)} {w}x{h} (the console after clear: {desc}; tests/fbcon_ref.py)",
                  flush=True)
            return 0
        if len(a) == 2 and a[0] == "check-console":
            from qmp_menu import Qmp  # noqa: E402
            qmp = Qmp(a[1])
            dump = a[1] + ".ppm"
            w, h, pix = qmp.screendump(dump)
            os.remove(dump)
            want, desc = fbcon_ref.render(FONTDIR, console_cleared(), w, h)
            differ, first = fbcon_ref.compare(pix, want, w)
            extra = "" if first is None else ", first at %d,%d: screen %s, reference %s" % first
            print(f"CONSOLE size={w}x{h} differ={differ} (the console after clear: {desc}{extra})", flush=True)
            return 0
        print(__doc__)
        return 1
    except Exception as e:                   # reported as a test failure by the caller
        print(f"screen_ref: ERROR: {e}")
        return 1


if __name__ == "__main__":
    sys.exit(main())
