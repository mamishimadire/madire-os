#!/usr/bin/env python3
"""Reads the splash's screens over QMP and compares each with a reference
rendering (1.2 stage 8, the [1.2] splash checks of tests/qemu-part-1.sh;
standard library only).

QEMU must be started paused (-S) with a QMP socket; this script connects,
starts the guest (cont) and reports facts as lines the caller's checks read.
It decides no check itself.

    qmp_splash.py SOCKET SERIAL SHADOW_PHYS FONTDIR STEP... [--size WxH]
                  [--until TEXT] [--timeout SECONDS]

The steps run in order:
    screen:REF:LINE  wait until LINE is a whole line of the serial log, then
                     take a screen and compare it with REF: dots0 ... dots3
                     (the splash with that many dots lit, rendered by
                     scripts/make-wallpaper.py, the one reference algorithm)
                     or console (the framebuffer console's cell grid,
                     rendered by tests/fbcon_ref.py from the kernel's
                     vga_shadow, read at physical SHADOW_PHYS);
    wait:LINE        wait until LINE is a whole line of the serial log;
    type:TEXT        type TEXT on the guest's PS/2 keyboard (QMP send-key,
                     20 ms per key, 50 ms apart; TEXT as for
                     tests/qmp_keys.py: {esc}, {ret} and the like).
Taking screen n: stop the guest (QMP stop), take a screendump, save the
vga_shadow (pmemsave), read the serial log once more, and let the guest go
on (cont). Prints

    MARKER <n> '<line>': seen after <s> s      (or: not seen in <t> s)
    SCREEN <n> size=<w>x<h> held=<yes|no> differ=<pixels> (<reference>)
    ROWS <n> top='<row 0>' bottom='<row 24>'   (console references only)

"held=yes" when the serial log, read while the guest was stopped, does not
yet hold "splash test: hold <n> over" (test=splash draws nothing between
its marker and that line). "differ" is the number of pixels where the
screen and the reference differ (-1: other sizes), with the first one. A
screen taken while the kernel was still drawing can only differ from the
reference, never match it falsely. With --until, it then waits for TEXT in
the serial log ("UNTIL ..."). --size renders the splash's references for
a WxH screen before the guest starts, so a held screen is taken within its
3 s. One QMP client at a time. Any error prints
"qmp_splash: ERROR: ..." and exits 1.
"""
import argparse
import importlib.util
import os
import struct
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
# No __pycache__/ from these imports (tests/ and scripts/ are files under test).
sys.dont_write_bytecode = True
from qmp_menu import Qmp, serial_text, wait_serial  # noqa: E402
from qmp_keys import text_to_actions  # noqa: E402
import fbcon_ref  # noqa: E402

_spec = importlib.util.spec_from_file_location("make_wallpaper",
                                               os.path.join(HERE, "..", "scripts", "make-wallpaper.py"))
make_wallpaper = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(make_wallpaper)


def wait_line(path, line, timeout):
    t0 = time.monotonic()
    while time.monotonic() - t0 < timeout:
        if line in serial_text(path).splitlines():
            return time.monotonic() - t0
        time.sleep(0.05)
    return None


def row_text(shadow, r):
    cells = struct.unpack_from("<80H", shadow, r * 160)
    return bytes(c & 0xFF for c in cells).decode("cp437").rstrip()


_splash = {}


def splash_ref(w, h, phase, fontdir):
    if (w, h) not in _splash:
        _splash[(w, h)] = make_wallpaper.splash_phases(w, h, range(4), fontdir)
    return _splash[(w, h)][phase], f"the splash with {phase} of the 3 dots lit (scripts/make-wallpaper.py)"


def main():
    ap = argparse.ArgumentParser(description="splash screens over QMP")
    ap.add_argument("socket")
    ap.add_argument("serial")
    ap.add_argument("shadow", help="physical address of vga_shadow (hex)")
    ap.add_argument("fontdir")
    ap.add_argument("steps", nargs="+")
    ap.add_argument("--size")
    ap.add_argument("--until")
    ap.add_argument("--timeout", type=float, default=120)
    a = ap.parse_args()
    dump, mem = a.socket + ".ppm", os.path.abspath(a.socket + ".shadow")
    try:
        if a.size:
            w, h = (int(v) for v in a.size.split("x"))
            t = time.monotonic()
            splash_ref(w, h, 0, a.fontdir)
            print(f"REFERENCE {w}x{h}: the 4 splash screens rendered in {time.monotonic() - t:.1f} s", flush=True)
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
                if wait_line(a.serial, step[5:], left) is None:
                    print(f"WAIT {step[5:]!r}: not seen in {left:.0f} s", flush=True)
                    return 0
                continue
            if not step.startswith("screen:") or step.count(":") < 2:
                raise ValueError(f"unknown step {step!r}")
            _, ref, line = step.split(":", 2)
            n += 1
            seen = wait_line(a.serial, line, left)
            if seen is None:
                print(f"MARKER {n} {line!r}: not seen in {left:.0f} s", flush=True)
                return 0
            print(f"MARKER {n} {line!r}: seen after {seen:.1f} s", flush=True)
            qmp.command("stop")
            w, h, pix = qmp.screendump(dump)
            if os.path.exists(mem):
                os.remove(mem)
            qmp.command("pmemsave", val=int(a.shadow, 16), size=fbcon_ref.SHADOW_SIZE, filename=mem)
            held = f"splash test: hold {n} over" not in serial_text(a.serial).splitlines()
            qmp.command("cont")
            if ref == "console":
                want, desc = fbcon_ref.render(a.fontdir, open(mem, "rb").read(), w, h)
                desc = "the console's cell grid: " + desc
            elif ref in ("dots0", "dots1", "dots2", "dots3"):
                want, desc = splash_ref(w, h, int(ref[4]), a.fontdir)
            else:
                raise ValueError(f"unknown reference {ref!r}")
            differ, first = fbcon_ref.compare(pix, want, w)
            extra = "" if first is None else ", first at %d,%d: screen %s, reference %s" % first
            print(f"SCREEN {n} size={w}x{h} held={'yes' if held else 'no'} differ={differ} ({desc}{extra})",
                  flush=True)
            if ref == "console":
                shadow = open(mem, "rb").read()
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
        print(f"qmp_splash: ERROR: {e}")
        return 1


if __name__ == "__main__":
    sys.exit(main())
