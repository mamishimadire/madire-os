#!/usr/bin/env python3
"""Drives the guest's PS/2 mouse wheel over QMP (1.5 stage 7, docs/plans/
1.5.md, C8; the [1.5] wheel checks of tests/qemu-part-static.sh and
tests/qemu-part-5.sh; standard library only).

QEMU must be started paused (-S) with a QMP socket; this script connects,
starts the guest (cont) and reports facts as lines the caller's checks
read. It decides no check itself. The kernel must log every packet (the
boot option "mouselog", or test=mouse; kernel/display/pointer.h):

    qmp_wheel.py SOCKET SERIAL STEP... [--screen WxH] [--shadow PHYS]
                 [--until TEXT] [--timeout SECONDS]

The steps run in order:
    wait:LINE      wait until LINE is a whole line of the serial log;
    waitre:N:REGEX wait until N lines of the serial log match REGEX;
    type:TEXT      type TEXT on the PS/2 keyboard (tests/qmp_keys.py's
                   syntax, as tests/qmp_mouse.py types);
    move:DX,DY     one relative move (tests/qmp_mouse.py's);
    wheel:N        one QMP input-send-event with |N| presses and releases
                   of the button wheel-up (N < 0) or wheel-down (N > 0):
                   QEMU's PS/2 mouse adds -1 or +1 to the wheel for each
                   press and sends one packet, so the kernel's line is
                       mouse: packet <n>: dx +0 dy +0 buttons <held>
                              wheel <+-|N|>[ at (<x>, <y>)]
                   (QEMU counts wheel-up as -1: away from the user);
    grid:N         save the console's cell grid (the kernel's vga_shadow,
                   whose first 4000 bytes are the 80 x 25 cells of
                   character and attribute; --shadow) and print row 0 and
                   row 24 (the scrollback's indicator row).
After every move and wheel step this script waits for the kernel's line of
that packet and compares it with the one it predicts (the packet's number,
counted from 1, the motion, the buttons, the wheel and, with --screen, the
clamped position). Prints

    PACKETS sent=<n> exact=<n> (each mismatch: MISMATCH <n> want '...' got '...')
    GRID <n> row0='<text>' row24='<text>'
    UNTIL '<text>': seen after <s> s      (or: not seen in <t> s)
    WAIT '<line>': not seen in <t> s      (the steps end there)

and "qmp_wheel: ERROR: ..." (exit 1) on any error, a packet line that
never comes among them. One QMP client at a time; tests/qmp_keys.py is
only imported (unchanged).
"""
import argparse
import os
import re
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
# No tests/__pycache__/ from these imports (tests/ is in the files under test).
sys.dont_write_bytecode = True
from qmp_menu import Qmp, wait_serial  # noqa: E402
from qmp_keys import text_to_actions  # noqa: E402
from qmp_mouse import Mouse, Serial  # noqa: E402

COLS, ROWS = 80, 25


def wheel(m, n):
    """One packet that turns the wheel by n notches; (packet, predicted line)."""
    name = "wheel-up" if n < 0 else "wheel-down"
    events = []
    for _ in range(abs(n)):
        events.append({"type": "btn", "data": {"down": True, "button": name}})
        events.append({"type": "btn", "data": {"down": False, "button": name}})
    line = m.predict(0, 0, m.buttons)
    at = line.find(" at (")
    part = " wheel %s%d" % ("-" if n < 0 else "+", abs(n))
    line = line + part if at < 0 else line[:at] + part + line[at:]
    m.send(events)
    return m.n, line


def wait_count(serial, rx, count, timeout):
    """True once `count` lines of the serial log match rx (within timeout)."""
    t0 = time.monotonic()
    while True:
        serial.poll()
        if sum(1 for l in serial.lines if rx.search(l)) >= count:
            return True
        if time.monotonic() - t0 > timeout:
            return False
        time.sleep(0.01)


def grid(qmp, phys, path):
    """Rows 0 and 24 of the cell grid at physical address phys."""
    if os.path.exists(path):
        os.remove(path)
    qmp.command("pmemsave", val=phys, size=COLS * ROWS * 2, filename=path)
    for _ in range(100):                 # QEMU writes the file before it answers; be safe
        if os.path.exists(path) and os.path.getsize(path) == COLS * ROWS * 2:
            break
        time.sleep(0.01)
    data = open(path, "rb").read()

    def row(r):
        cells = data[r * COLS * 2:(r + 1) * COLS * 2]
        return "".join(chr(c) if 32 <= c < 127 else "?" for c in cells[0::2]).rstrip(" ")
    return row(0), row(ROWS - 1)


def main():
    ap = argparse.ArgumentParser(description="PS/2 mouse wheel sessions over QMP")
    ap.add_argument("socket")
    ap.add_argument("serial")
    ap.add_argument("steps", nargs="+")
    ap.add_argument("--screen", help="WxH: the pointer is on a screen of that size")
    ap.add_argument("--shadow", help="physical address of vga_shadow (hex)")
    ap.add_argument("--until")
    ap.add_argument("--timeout", type=float, default=120)
    a = ap.parse_args()
    screen = tuple(int(v) for v in a.screen.split("x")) if a.screen else None
    mem = os.path.abspath(a.socket + ".grid")
    try:
        qmp = Qmp(a.socket)
        serial = Serial(a.serial)
        m = Mouse(qmp, serial, screen)
        t0 = time.monotonic()
        qmp.command("cont")
        grids = 0
        for step in a.steps:
            left = max(a.timeout - (time.monotonic() - t0), 1)
            if step.startswith("wait:"):
                if serial.wait(lambda l, s=step[5:]: l == s, left) is None:
                    print(f"WAIT {step[5:]!r}: not seen in {left:.0f} s", flush=True)
                    break
            elif step.startswith("waitre:"):
                count, rx = step[7:].split(":", 1)
                if not wait_count(serial, re.compile(rx), int(count), left):
                    print(f"WAIT {count} x {rx!r}: not seen in {left:.0f} s", flush=True)
                    break
            elif step.startswith("type:"):
                for kind, value in text_to_actions(step[5:]):
                    if kind == "wait":
                        time.sleep(value)
                        continue
                    qmp.command("send-key", keys=[{"type": "qcode", "data": k} for k in value],
                                **{"hold-time": 20})
                    time.sleep(0.05)
            elif step.startswith("move:"):
                dx, dy = (int(v) for v in step[5:].split(","))
                m.check(*m.move(dx, dy))
            elif step.startswith("wheel:"):
                n = int(step[6:])
                if n == 0:
                    raise ValueError("wheel:0 sends no notch")
                m.check(*wheel(m, n))
            elif step.startswith("grid:"):
                if not a.shadow:
                    raise ValueError("grid needs --shadow")
                grids += 1
                r0, r24 = grid(qmp, int(a.shadow, 16), mem)
                print(f"GRID {step[5:]} row0='{r0}' row24='{r24}'", flush=True)
            else:
                raise ValueError("unknown step %r" % step)
        print(f"PACKETS sent={m.sent} exact={m.exact}", flush=True)
        if a.until:
            left = max(a.timeout - (time.monotonic() - t0), 1)
            s = wait_serial(a.serial, a.until, left)
            print(f"UNTIL {a.until!r}: " + ("not seen in %.0f s" % left if s is None else f"seen after {s:.1f} s"),
                  flush=True)
        if os.path.exists(mem):
            os.remove(mem)
        return 0
    except Exception as e:                   # reported as a test failure by the caller
        print(f"qmp_wheel: ERROR: {e}")
        return 1


if __name__ == "__main__":
    sys.exit(main())
