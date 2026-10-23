#!/usr/bin/env python3
"""Reads a framebuffer boot's screen over QMP (1.2 stage 5b, the [1.2]
framebuffer checks of tests/qemu-part-1.sh; standard library only).

QEMU must be started paused (-S) with a QMP socket; this script connects,
starts the guest (cont), and reports facts as lines the caller's checks
read. It decides no check itself.

    qmp_fb.py SOCKET SERIAL MARKER [--until TEXT] [--timeout SECONDS]

Waits until MARKER (for example "display: frame 1", which the kernel logs
once a frame is on the framebuffer) is a line of the serial log, then stops
the guest (QMP stop), takes a screendump and lets the guest go on (cont), so
nothing the kernel does after the marker can change the screen in between.
Prints:
    MARKER 'display: frame 1': seen after <s> s     (or: not seen in <n> s)
    SCREEN size=<w>x<h> crc=0x<crc32> colours=<n>
the CRC-32 (zlib's) of the screen's pixels as red, green, blue bytes, row by
row from the top (the bytes of QEMU's PPM screendump, as
kernel/display/surface_core.c's surface_crc32 computes them), and the number
of distinct colours (report only). With --until, it then waits for TEXT in
the serial log and prints "UNTIL <text>: seen after <s> s".

The QMP client and the screendump reader are tests/qmp_menu.py's (imported,
unchanged). One QMP client at a time. Any error prints "qmp_fb: ERROR: ..."
and exits 1.
"""
import argparse
import os
import sys
import time
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from qmp_menu import Qmp, serial_text, wait_serial  # noqa: E402


def wait_line(path, line, timeout):
    """Seconds until `line` is a whole line of the serial log, or None."""
    t0 = time.monotonic()
    while time.monotonic() - t0 < timeout:
        if line in serial_text(path).splitlines():
            return time.monotonic() - t0
        time.sleep(0.1)
    return None


def main():
    ap = argparse.ArgumentParser(description="framebuffer screen checks over QMP")
    ap.add_argument("socket")
    ap.add_argument("serial")
    ap.add_argument("marker")
    ap.add_argument("--until")
    ap.add_argument("--timeout", type=float, default=90)
    a = ap.parse_args()
    dump = a.socket + ".ppm"
    try:
        qmp = Qmp(a.socket)
        t0 = time.monotonic()
        qmp.command("cont")
        seen = wait_line(a.serial, a.marker, a.timeout)
        if seen is None:
            print(f"MARKER {a.marker!r}: not seen in {a.timeout:.0f} s", flush=True)
            return 0
        print(f"MARKER {a.marker!r}: seen after {seen:.1f} s", flush=True)
        qmp.command("stop")
        w, h, pix = qmp.screendump(dump)
        qmp.command("cont")
        colours = len({pix[i:i + 3] for i in range(0, len(pix), 3)})
        print(f"SCREEN size={w}x{h} crc=0x{zlib.crc32(pix) & 0xffffffff:08x} colours={colours}", flush=True)
        if a.until:
            left = max(a.timeout - (time.monotonic() - t0), 1)
            s = wait_serial(a.serial, a.until, left)
            print(f"UNTIL {a.until!r}: " + ("not seen in %.0f s" % left if s is None else f"seen after {s:.1f} s"),
                  flush=True)
        if os.path.exists(dump):
            os.remove(dump)
        return 0
    except Exception as e:                   # reported as a test failure by the caller
        print(f"qmp_fb: ERROR: {e}")
        return 1


if __name__ == "__main__":
    sys.exit(main())
