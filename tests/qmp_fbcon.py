#!/usr/bin/env python3
"""Reads the framebuffer console's screens over QMP and compares each with
the reference rendering (1.2 stage 6, the [1.2] console checks of
tests/qemu-part-1.sh; standard library only).

QEMU must be started paused (-S) with a QMP socket; this script connects,
starts the guest (cont) and reports facts as lines the caller's checks read.
It decides no check itself.

    qmp_fbcon.py SOCKET SERIAL SHADOW_PHYS FONTDIR STEP... [--until TEXT]
                 [--timeout SECONDS]

The steps run in order:
    LINE          wait until LINE is a whole line of the serial log (such as
                  "display: frame 1"), then take a screen (below);
    regex:RE      wait until a line of the serial log matches RE, let the
                  guest run 1 s more (it settles), then take a screen;
    wait:LINE     wait until LINE is a whole line of the serial log;
    type:TEXT     type TEXT on the guest's PS/2 keyboard (QMP send-key, 20 ms
                  per key, 50 ms apart; TEXT as for tests/qmp_keys.py: {ret}
                  and the like).
Taking screen n: stop the guest (QMP stop), take a screendump, save the
kernel's vga_shadow (4051 bytes at physical SHADOW_PHYS, its address in
madire.elf minus 0xffffffff80000000) with pmemsave, read the serial log once
more, and let the guest go on (cont). Prints

    MARKER <n> '<step>': seen after <s> s      (or: not seen in <t> s)
    SCREEN <n> size=<w>x<h> held=<yes|no> differ=<pixels> (<reference description>)
    ROWS <n> top='<row 0>' bottom='<row 24>'

"held=yes" when the serial log, read while the guest was stopped, does not
yet hold the line "fbcon test: hold <n> over" (the kernel draws nothing
between the marker and that line, so the screen was still). "differ" is
the number of pixels where the screen and tests/fbcon_ref.py's rendering
of the saved shadow differ (-1: other sizes), with the first difference.
ROWS gives the shadow's first and last rows as text (code page 437,
trailing spaces removed, as Python's ascii()). A screen taken while the kernel was still
drawing can only differ from the reference, never match it falsely. With
--until, it then waits for TEXT in the serial log ("UNTIL ...").

One QMP client at a time. Any error prints "qmp_fbcon: ERROR: ..." and
exits 1.
"""
import argparse
import os
import re
import struct
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
# No tests/__pycache__/ from these imports (tests/ is in the files under test).
sys.dont_write_bytecode = True
from qmp_menu import Qmp, serial_text, wait_serial  # noqa: E402
from qmp_keys import text_to_actions  # noqa: E402
import fbcon_ref  # noqa: E402


def wait_until(path, test, timeout):
    t0 = time.monotonic()
    while time.monotonic() - t0 < timeout:
        if any(test(l) for l in serial_text(path).splitlines()):
            return time.monotonic() - t0
        time.sleep(0.05)
    return None


def row_text(shadow, r):
    cells = struct.unpack_from("<80H", shadow, r * 160)
    return bytes(c & 0xFF for c in cells).decode("cp437").rstrip()


def main():
    ap = argparse.ArgumentParser(description="framebuffer console screens over QMP")
    ap.add_argument("socket")
    ap.add_argument("serial")
    ap.add_argument("shadow", help="physical address of vga_shadow (hex)")
    ap.add_argument("fontdir")
    ap.add_argument("steps", nargs="+")
    ap.add_argument("--until")
    ap.add_argument("--timeout", type=float, default=90)
    a = ap.parse_args()
    dump, mem = a.socket + ".ppm", os.path.abspath(a.socket + ".shadow")
    try:
        qmp = Qmp(a.socket)
        t0 = time.monotonic()
        qmp.command("cont")
        n = 0
        for step in a.steps:
            left = max(a.timeout - (time.monotonic() - t0), 1)
            if step.startswith("type:"):
                for kind, value in text_to_actions(step[5:]):
                    if kind == "wait":
                        time.sleep(value)
                        continue
                    qmp.command("send-key", keys=[{"type": "qcode", "data": k} for k in value],
                                **{"hold-time": 20})
                    time.sleep(0.05)
                continue
            if step.startswith("wait:"):
                seen = wait_until(a.serial, lambda l, s=step[5:]: l == s, left)
                if seen is None:
                    print(f"WAIT {step[5:]!r}: not seen in {left:.0f} s", flush=True)
                    return 0
                continue
            n += 1
            if step.startswith("regex:"):
                rx = re.compile(step[6:])
                seen = wait_until(a.serial, lambda l: rx.search(l) is not None, left)
            else:
                seen = wait_until(a.serial, lambda l: l == step, left)
            if seen is None:
                print(f"MARKER {n} {step!r}: not seen in {left:.0f} s", flush=True)
                return 0
            print(f"MARKER {n} {step!r}: seen after {seen:.1f} s", flush=True)
            if step.startswith("regex:"):
                time.sleep(1.0)
            qmp.command("stop")
            w, h, pix = qmp.screendump(dump)
            if os.path.exists(mem):
                os.remove(mem)
            qmp.command("pmemsave", val=int(a.shadow, 16), size=fbcon_ref.SHADOW_SIZE, filename=mem)
            held = f"fbcon test: hold {n} over" not in serial_text(a.serial).splitlines()
            qmp.command("cont")
            shadow = open(mem, "rb").read()
            want, desc = fbcon_ref.render(a.fontdir, shadow, w, h)
            differ, first = fbcon_ref.compare(pix, want, w)
            extra = "" if first is None else ", first at %d,%d: screen %s, reference %s" % first
            print(f"SCREEN {n} size={w}x{h} held={'yes' if held else 'no'} differ={differ} ({desc}{extra})",
                  flush=True)
            print(f"ROWS {n} top={ascii(row_text(shadow, 0))} bottom={ascii(row_text(shadow, 24))}", flush=True)
        if a.until:
            left = max(a.timeout - (time.monotonic() - t0), 1)
            s = wait_serial(a.serial, a.until, left)
            print(f"UNTIL {a.until!r}: " + ("not seen in %.0f s" % left if s is None else f"seen after {s:.1f} s"),
                  flush=True)
        for p in (dump, mem):
            if os.path.exists(p):
                os.remove(p)
        return 0
    except Exception as e:                   # reported as a test failure by the caller
        print(f"qmp_fbcon: ERROR: {e}")
        return 1


if __name__ == "__main__":
    sys.exit(main())
