#!/usr/bin/env python3
"""Reads the desktop's screens over QMP and compares each with
tests/desktop_ref.py's rendering (1.3 stage 3b, the [1.3] desktop checks of
tests/qemu-part-2.sh and qemu-part-3.sh; standard library only).

QEMU must be started paused (-S) with a QMP socket on a test=desktop boot;
this script connects, starts the guest (cont) and reports facts as lines
the caller's checks read. It decides no check itself.

    qmp_desktop.py SOCKET SERIAL STEP... [--size WxH] [--until TEXT]
                   [--timeout SECONDS]

The steps run in order:
    screen        wait for the next "display: frame N" line (the desktop's
                  frame marker), then take a screen (below), the pointer
                  not drawn yet;
    move:DX,DY    one relative move (tests/qmp_mouse.py's: the kernel's
                  packet line, with its clamped position, must be the one
                  predicted; the pointer starts at the screen's centre);
    pointer       wait for "pointer: drawn at (X, Y)" (the predicted
                  place) after the last packet's line, then take a screen
                  with the pointer at (X, Y) (tests/pointer_ref.py);
    type:TEXT     type TEXT on the PS/2 keyboard (as tests/qmp_keys.py);
    wait:LINE     wait until LINE is a whole line of the serial log.
Taking a screen: stop the guest (QMP stop), take a screendump, read the
serial log, and let the guest go on (cont). The reference is the desktop
(desktop_ref.desktop) with the clock of the last "desktop: clock HH:MM"
line logged so far (each frame marker is followed by the clock that frame
shows), no mask. If the screen differs and a newer frame marker comes
within 3 s (the minute changed between a frame and its lines), the screen
is taken again, at most twice. Prints

    SCREEN <n> size=<w>x<h> differ=<pixels> clock=<HH:MM> tries=<k> (<descriptions>)
    PACKETS sent=<n> exact=<n> (each mismatch: MISMATCH <n> want '...' got '...')
    UNTIL '<text>': seen after <s> s      (or: not seen in <t> s)

and "qmp_desktop: ERROR: ..." (exit 1) on any error. --size renders the
background for a WxH screen before the guest starts. One QMP client at a
time.
"""
import argparse
import os
import re
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
# No tests/__pycache__/ from these imports (tests/ is in the files under test).
sys.dont_write_bytecode = True
from qmp_menu import Qmp, wait_serial  # noqa: E402
from qmp_keys import text_to_actions  # noqa: E402
from qmp_mouse import Mouse, Serial, PACKET_WAIT  # noqa: E402
import desktop_ref  # noqa: E402
import fbcon_ref  # noqa: E402
import pointer_ref  # noqa: E402

MARKER = re.compile(r"^display: frame (\d+)$")
CLOCK = re.compile(r"^desktop: clock (\d\d:\d\d|--:--)$")


def last_clock(serial):
    """(frame number, clock) of the last frame marker that has its clock line."""
    serial.poll()
    found = (None, None)
    for i, line in enumerate(serial.lines):
        m = MARKER.match(line)
        if m and i + 1 < len(serial.lines):
            c = CLOCK.match(serial.lines[i + 1])
            if c:
                found = (int(m.group(1)), c.group(1))
    return found


def markers(serial):
    serial.poll()
    return sum(1 for line in serial.lines if MARKER.match(line))


def take(qmp, serial, dump, pointer_at):
    """One screen against the desktop with the last logged clock."""
    qmp.command("stop")
    w, h, pix = qmp.screendump(dump)
    frame, clock = last_clock(serial)
    seen = markers(serial)
    qmp.command("cont")
    if clock is None:
        return w, h, -1, None, "no 'desktop: clock' line after a frame marker", seen
    want = desktop_ref.desktop(w, h, clock.encode("ascii"))
    desc = "the desktop with the clock %s (frame %d)" % (clock, frame)
    if pointer_at is not None:
        want, pdesc = pointer_ref.paint(want, w, h, *pointer_at)
        desc += "; " + pdesc
    differ, first = fbcon_ref.compare(pix, want, w)
    if first is not None:
        desc += ", first at %d,%d: screen %s, reference %s" % first
    return w, h, differ, clock, desc, seen


def screen(qmp, serial, dump, n, pointer_at):
    tries = 0
    while True:
        tries += 1
        w, h, differ, clock, desc, seen = take(qmp, serial, dump, pointer_at)
        if differ == 0 or tries == 3:
            break
        t0 = time.monotonic()
        while markers(serial) == seen and time.monotonic() - t0 < 3:
            time.sleep(0.05)
        if markers(serial) == seen:
            break                       # no newer frame: the difference stands
    print("SCREEN %d size=%dx%d differ=%d clock=%s tries=%d (%s)" % (n, w, h, differ, clock, tries, desc),
          flush=True)


def main():
    ap = argparse.ArgumentParser(description="desktop screens over QMP")
    ap.add_argument("socket")
    ap.add_argument("serial")
    ap.add_argument("steps", nargs="+")
    ap.add_argument("--size")
    ap.add_argument("--until")
    ap.add_argument("--timeout", type=float, default=150)
    a = ap.parse_args()
    dump = a.socket + ".ppm"
    try:
        size = None
        if a.size:
            size = tuple(int(v) for v in a.size.split("x"))
            t = time.monotonic()
            desktop_ref.desktop(size[0], size[1], b"00:00")
            print("REFERENCE %dx%d: the background rendered in %.1f s" % (size[0], size[1], time.monotonic() - t),
                  flush=True)
        qmp = Qmp(a.socket)
        serial = Serial(a.serial)
        m = Mouse(qmp, serial, size)
        t0 = time.monotonic()
        qmp.command("cont")
        shots, used = 0, 0              # used: serial lines already consumed by screen steps
        for step in a.steps:
            left = max(a.timeout - (time.monotonic() - t0), 1)
            if step == "screen":
                i = serial.wait(lambda l: MARKER.match(l) is not None, left, used)
                if i is None:
                    print("MARKER not seen in %.0f s" % left, flush=True)
                    return 0
                used = i + 1
                serial.wait(lambda l: CLOCK.match(l) is not None, 5, used)
                shots += 1
                screen(qmp, serial, dump, shots, None)
            elif step == "pointer":
                drawn = "pointer: drawn at (%d, %d)" % (m.x, m.y)
                if serial.wait(lambda l: l == drawn, PACKET_WAIT, m.last_line) is None:
                    raise RuntimeError("no line %r after packet %d" % (drawn, m.n))
                shots += 1
                screen(qmp, serial, dump, shots, (m.x, m.y))
            elif step.startswith("move:"):
                dx, dy = (int(v) for v in step[5:].split(","))
                m.check(*m.move(dx, dy))
            elif step.startswith("wait:"):
                if serial.wait(lambda l, s=step[5:]: l == s, left) is None:
                    print(f"WAIT {step[5:]!r}: not seen in {left:.0f} s", flush=True)
                    return 0
            elif step.startswith("type:"):
                for kind, value in text_to_actions(step[5:]):
                    if kind == "wait":
                        time.sleep(value)
                        continue
                    qmp.command("send-key", keys=[{"type": "qcode", "data": k} for k in value],
                                **{"hold-time": 20})
                    time.sleep(0.05)
            else:
                raise ValueError("unknown step %r" % step)
        print(f"PACKETS sent={m.sent} exact={m.exact}", flush=True)
        if a.until:
            left = max(a.timeout - (time.monotonic() - t0), 1)
            s = wait_serial(a.serial, a.until, left)
            print(f"UNTIL {a.until!r}: " + ("not seen in %.0f s" % left if s is None else f"seen after {s:.1f} s"),
                  flush=True)
        if os.path.exists(dump):
            os.remove(dump)
        return 0
    except Exception as e:                   # reported as a test failure by the caller
        print(f"qmp_desktop: ERROR: {e}")
        return 1


if __name__ == "__main__":
    sys.exit(main())
