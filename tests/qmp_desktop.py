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

1.3 stage 4a: the windows. tests/desktop_ref.py's WM (the window manager's
rules written again) follows every packet and key, and predicts the
kernel's desktoplog lines and the windows on the screen. --about: the
boot has desktopabout (the About window opens at the start). More steps
(--size is needed; mouselog and desktoplog on the boot):
    to:WHERE      relative moves (at most 127 each) to a point of the top
                  window: title (100 dp, 16 dp into it), title-right (312
                  dp, 16 dp into it: a drag from there to the screen's
                  left edge reaches the clamp), close (its close
                  button's centre), body (40 dp, 250 dp into it), or
                  desktop (a corner of the area above the taskbar outside
                  every window);
    rel:DX,DY     relative moves adding up to (DX, DY);
    press, release  the left button, no move;
    drag:DX,DY    press, rel:DX,DY, release; prints
                  DRAG <n> asked (<x>, <y>) got (<x>, <y>) <w>x<h> bar <top> left <x min>
                  (the window's start plus the pointer's own movement,
                  and where the rules put it);
    key:NAME      one key (a qcode) that the model says changes nothing;
    esc           Esc (the model closes the focused window if it may);
    wshot         a screen against the desktop with the model's windows
                  (each with its logged About values, cut as ui_about.h
                  says) and, after the first packet, the pointer: taken at
                  the first frame marker after the last step that changed
                  what the model shows, once the last packet's "pointer:
                  drawn at" line is there; prints
                  WSCREEN <n> size=<w>x<h> differ=<pixels> clock=<HH:MM> tries=<k> (<descriptions>)
At the end (with --about or any of these steps): the kernel's window lines
("desktop: open|raise|focus|drag|close ...") against the model's, in order:
    EVENTS predicted=<n> logged=<n> same=<yes|no>[ first difference <i>: want '...' got '...']
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


# ---- 1.3 stage 4a: the windows ----

WIN_EVENT = re.compile(r"^desktop: (open|raise|focus|drag|close) ")
ABOUT_VALUE = re.compile(r"^desktop: about (Version|Boot loader|Display|Memory|Mode): (.*)$")


class Windows:
    """The model (desktop_ref.WM) and what follows it: the last serial line
    that showed a change, the About window's logged values."""

    def __init__(self, size, about):
        self.w, self.h = size
        self.s = desktop_ref.scale_of(self.w, self.h)
        self.wm = desktop_ref.WM(self.w, self.h, self.s)
        self.sync = 0                   # the serial line after which the next frame shows the model
        self.drags = 0
        if about:
            aw, ah = desktop_ref.about_size(self.s)
            self.wm.open("About", aw, ah, True)

    def changed(self, before):
        """True if the model's look changed since `before` (its state())."""
        return self.state() != before

    def state(self):
        return [(w["name"], w["r"], w["close"], w is self.wm.focus) for w in self.wm.z]

    def target(self, where):
        d = lambda n: desktop_ref.px(n, self.s)  # noqa: E731
        if where == "desktop":
            for x, y in ((d(8), d(8)), (self.w - d(8), d(8)), (d(8), self.wm.bar - d(8)),
                         (self.w - d(8), self.wm.bar - d(8))):
                if self.wm.hit(x, y)[0] == "desktop":
                    return x, y
            raise RuntimeError("no corner of the desktop outside every window")
        r = self.wm.z[-1]["r"]
        if where == "title":
            return r[0] + d(100), r[1] + d(16)
        if where == "title-right":                      # the title bar's right end, before the close button
            return r[0] + d(312), r[1] + d(16)
        if where == "close":
            c = self.wm.close_rect(r)
            return c[0] + c[2] // 2, c[1] + c[3] // 2
        if where == "body":
            return r[0] + d(40), r[1] + d(250)
        raise ValueError("unknown place %r" % where)

    def values(self, serial):
        serial.poll()
        got = {}
        for line in serial.lines:
            m = ABOUT_VALUE.match(line)
            if m:
                got[m.group(1)] = m.group(2).encode("latin-1")
        keys = ("Version", "Boot loader", "Display", "Memory")
        if any(k not in got for k in keys):
            raise RuntimeError("no 'desktop: about ...' line for %s" % ", ".join(k for k in keys if k not in got))
        return desktop_ref.about_values(self.s, *(got[k] for k in keys))


def packet(m, win, serial, dx, dy, button=None):
    """One packet (a move, or a button with no move), checked, then given
    to the model; a change moves the sync point to its line."""
    before = win.state()
    if button is None:
        n, want = m.move(dx, dy)
    else:
        n, want = m.button("left", button)
    m.check(n, want)
    win.wm.pointer(m.x, m.y, button is True, button is False)
    if win.changed(before):
        win.sync = m.last_line


def rel(m, win, serial, dx, dy):
    while dx or dy:
        sx, sy = max(-127, min(127, dx)), max(-127, min(127, dy))
        packet(m, win, serial, sx, sy)
        dx, dy = dx - sx, dy - sy


def wtake(qmp, serial, dump, win, pointer_at):
    qmp.command("stop")
    w, h, pix = qmp.screendump(dump)
    frame, clock = last_clock(serial)
    seen = markers(serial)
    qmp.command("cont")
    if clock is None:
        return w, h, -1, None, "no 'desktop: clock' line after a frame marker", seen
    shown = []
    if win.wm.z:
        values = win.values(serial)
        shown = [{"r": x["r"], "active": x is win.wm.focus, "close": x["close"], "values": values}
                 for x in win.wm.z]
    want = desktop_ref.desktop_windows(w, h, clock.encode("ascii"), shown)
    desc = "the desktop with the clock %s (frame %d)" % (clock, frame)
    for x in win.wm.z:
        desc += "; %s at (%d, %d) %dx%d, %s, close %s" % (x["name"], x["r"][0], x["r"][1], x["r"][2], x["r"][3],
                                                       "active" if x is win.wm.focus else "inactive", x["close"])
    if pointer_at is not None:
        want, pdesc = pointer_ref.paint(want, w, h, *pointer_at)
        desc += "; " + pdesc
    differ, first = fbcon_ref.compare(pix, want, w)
    if first is not None:
        desc += ", first at %d,%d: screen %s, reference %s" % first
    return w, h, differ, clock, desc, seen


def wshot(qmp, serial, dump, win, m, n):
    if m.sent:
        drawn = "pointer: drawn at (%d, %d)" % (m.x, m.y)
        if serial.wait(lambda l: l == drawn, PACKET_WAIT, m.last_line) is None:
            raise RuntimeError("no line %r after packet %d" % (drawn, m.n))
    if serial.wait(lambda l: MARKER.match(l) is not None, 30, win.sync + 1 if win.sync else 0) is None:
        raise RuntimeError("no frame marker after line %d" % win.sync)
    pointer_at = (m.x, m.y) if m.sent else None
    tries = 0
    while True:
        tries += 1
        w, h, differ, clock, desc, seen = wtake(qmp, serial, dump, win, pointer_at)
        if differ == 0 or tries == 3:
            break
        t0 = time.monotonic()
        while markers(serial) == seen and time.monotonic() - t0 < 3:
            time.sleep(0.05)
        if markers(serial) == seen:
            break
    print("WSCREEN %d size=%dx%d differ=%d clock=%s tries=%d (%s)" % (n, w, h, differ, clock, tries, desc),
          flush=True)


def window_step(step, qmp, serial, dump, win, m, shots):
    """One of the window steps; returns the new number of screens taken."""
    if step == "wshot":
        shots += 1
        wshot(qmp, serial, dump, win, m, shots)
    elif step.startswith("to:"):
        x, y = win.target(step[3:])
        rel(m, win, serial, x - m.x, y - m.y)
    elif step.startswith("rel:"):
        dx, dy = (int(v) for v in step[4:].split(","))
        rel(m, win, serial, dx, dy)
    elif step in ("press", "release"):
        packet(m, win, serial, 0, 0, step == "press")
    elif step.startswith("drag:"):
        dx, dy = (int(v) for v in step[5:].split(","))
        packet(m, win, serial, 0, 0, True)
        if win.wm.drag is None:
            raise RuntimeError("drag: the press was not on a title bar (model)")
        target, start, x0, y0 = win.wm.drag, win.wm.drag["r"], m.x, m.y
        rel(m, win, serial, dx, dy)
        packet(m, win, serial, 0, 0, False)
        win.drags += 1
        asked = (start[0] + m.x - x0, start[1] + m.y - y0)      # 1:1 with the pointer (itself kept on the screen)
        print("DRAG %d asked (%d, %d) got (%d, %d) %dx%d bar %d left %d" % (
            win.drags, asked[0], asked[1], target["r"][0], target["r"][1], target["r"][2], target["r"][3],
            win.wm.bar, desktop_ref.px(48, win.s) - target["r"][2]), flush=True)
    elif step.startswith("key:"):
        qmp.command("send-key", keys=[{"type": "qcode", "data": step[4:]}], **{"hold-time": 20})
        time.sleep(1.5)
    elif step == "esc":
        before = len(win.wm.events)
        win.wm.esc()
        qmp.command("send-key", keys=[{"type": "qcode", "data": "esc"}], **{"hold-time": 20})
        if len(win.wm.events) > before:
            want = win.wm.events[before]
            i = serial.wait(lambda l: l == want, 20, win.sync)
            if i is None:
                raise RuntimeError("no line %r after Esc" % want)
            win.sync = i
        else:
            time.sleep(1.5)
    else:
        raise ValueError("unknown step %r" % step)
    return shots


def events_report(serial, win):
    want = win.wm.events
    t0 = time.monotonic()
    while True:
        serial.poll()
        got = [line for line in serial.lines if WIN_EVENT.match(line)]
        if len(got) >= len(want) or time.monotonic() - t0 > 5:
            break
        time.sleep(0.1)
    same = got == want
    out = "EVENTS predicted=%d logged=%d same=%s" % (len(want), len(got), "yes" if same else "no")
    if not same:
        i = next((k for k in range(min(len(want), len(got))) if want[k] != got[k]), min(len(want), len(got)))
        out += " first difference %d: want %r got %r" % (i, want[i] if i < len(want) else None,
                                                       got[i] if i < len(got) else None)
    print(out, flush=True)


def main():
    ap = argparse.ArgumentParser(description="desktop screens over QMP")
    ap.add_argument("socket")
    ap.add_argument("serial")
    ap.add_argument("steps", nargs="+")
    ap.add_argument("--size")
    ap.add_argument("--until")
    ap.add_argument("--timeout", type=float, default=150)
    ap.add_argument("--about", action="store_true")         # 1.3 stage 4a
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
        win = Windows(size, a.about) if size else None
        windowed = a.about
        t0 = time.monotonic()
        qmp.command("cont")
        shots, used = 0, 0              # used: serial lines already consumed by screen steps
        wshots = 0                      # 1.3 stage 4a: WSCREEN steps
        for step in a.steps:
            left = max(a.timeout - (time.monotonic() - t0), 1)
            if win and (step in ("wshot", "press", "release", "esc") or step.split(":")[0] in
                        ("to", "rel", "drag", "key")):
                windowed = True
                wshots = window_step(step, qmp, serial, dump, win, m, wshots)
            elif step == "screen":
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
        if windowed and win:
            events_report(serial, win)
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
