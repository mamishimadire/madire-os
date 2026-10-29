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

1.3 stage 4b: the Console window. The model also follows every key the
type: step sends (desktop_ref.WM.key: with no window focused a key opens
the Console window and is not typed; with it focused the console takes
the key; with the About window focused Esc closes it), and waits for the
kernel's window lines of what it changed. --shadow PHYS (vga_shadow's
physical address): each wshot with a Console window also saves the
kernel's cell grid while the guest is stopped and draws the window from
it (desktop_ref.console_window), and prints
    GRID <n> crc=<crc32 of the 4051 bytes> row0='...' row24='...'
(the rows' characters as bytes, trailing spaces cut). More steps:
    to:WHERE@NAME a point of window NAME's title bar (title) or body (body)
                  that the hit test gives to it (no window above it there);
    wait:LINE     (in window mode) also the point after which wshot waits
                  for a frame;
    dragburst:N[:TEXT]  press on the top window's title bar (the pointer
                  must be there), N moves with the left button held (each
                  at most 20 px, never more than 4 packets waiting, every
                  packet's line checked), release; with TEXT, its keys
                  spread over the burst (tests/qmp_keys.py's syntax);
                  prints the DRAG line and
    DRAGBURST sent=<n> exact=<n> seconds=<s> keys=<k>
    ready         wait for the desktop's first frame marker (the mouse is
                  started before it), taking no screen: the first step of
                  a boot that sends input before its first wshot;
    crash:LINE    (with --shadow) wait for LINE (spaces around it ignored),
                  then the crash screen against tests/fbcon_ref.py's
                  full-screen rendering of the saved grid, the screen taken
                  again 1 s later (held: unchanged); prints
    CRASH size=<w>x<h> differ=<pixels> held=<yes|no> (<description>)
"""
import argparse
import os
import re
import sys
import time
import zlib

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

    def __init__(self, size, about, shadow=None):
        self.w, self.h = size
        self.s = desktop_ref.scale_of(self.w, self.h)
        self.wm = desktop_ref.WM(self.w, self.h, self.s)
        self.sync = 0                   # the serial line after which the next frame shows the model
        self.drags = 0
        self.shadow = shadow            # 1.3 stage 4b: vga_shadow's physical address (hex), or None
        self.console_size = desktop_ref.console_size(self.s)
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
        if "@" in where:                # 1.3 stage 4b: a named window's own visible point
            part, name = where.split("@")
            win = next((w for w in self.wm.z if w["name"] == name), None)
            if win is None:
                raise RuntimeError("no open window %s (model)" % name)
            r, tb = win["r"], d(32)
            ys = [r[1] + d(16)] if part == "title" else range(r[1] + tb + d(8), r[1] + r[3] - d(8), d(8))
            for y in ys:
                for x in range(r[0] + d(8), r[0] + r[2] - tb - d(8), d(8)):
                    if 0 <= x < self.w and 0 <= y < self.wm.bar and self.wm.hit(x, y) == (part, win):
                        return x, y
            raise RuntimeError("no visible point of %s's %s (model)" % (name, part))
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

    def key(self, pressed, esc):
        return self.wm.key(self.console_size, pressed, esc)

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
    shadow = None
    if win.shadow and any(x["name"] == "Console" for x in win.wm.z):     # 1.3 stage 4b
        mem = os.path.abspath(dump + ".shadow")
        if os.path.exists(mem):
            os.remove(mem)
        qmp.command("pmemsave", val=int(win.shadow, 16), size=fbcon_ref.SHADOW_SIZE, filename=mem)
    frame, clock = last_clock(serial)
    seen = markers(serial)
    qmp.command("cont")
    if win.shadow and any(x["name"] == "Console" for x in win.wm.z):
        shadow = open(mem, "rb").read()
        os.remove(mem)
    win.last_shadow = shadow
    if clock is None:
        return w, h, -1, None, "no 'desktop: clock' line after a frame marker", seen
    shown = []
    values = win.values(serial) if any(x["name"] == "About" for x in win.wm.z) else None
    for x in win.wm.z:
        one = {"r": x["r"], "active": x is win.wm.focus, "close": x["close"], "values": values}
        if x["name"] == "Console":
            if shadow is None:
                raise RuntimeError("a Console window to draw but no --shadow")
            one["grid"] = desktop_ref.shadow_grid(shadow)
        shown.append(one)
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
    if win.last_shadow is not None:         # 1.3 stage 4b
        g = win.last_shadow
        rows = [bytes(g[2 * (80 * r + c)] for c in range(80)).rstrip(b" ") for r in (0, 24)]
        print("GRID %d crc=%08x row0=%r row24=%r" % (n, zlib.crc32(g[:fbcon_ref.SHADOW_SIZE]),
                                                   rows[0].decode("latin-1"), rows[1].decode("latin-1")), flush=True)


def crash_screen(qmp, a, dump):
    """1.3 stage 4b: the crash screen after a panic (the guest halted): the
    screen and vga_shadow, then the screen again 1 s later; against
    tests/fbcon_ref.py's full-screen rendering of the cell grid (no window,
    no taskbar, no pointer)."""
    if not a.shadow:
        raise RuntimeError("crash: needs --shadow")
    time.sleep(1)
    mem = os.path.abspath(dump + ".shadow")
    if os.path.exists(mem):
        os.remove(mem)
    qmp.command("stop")
    w, h, pix = qmp.screendump(dump)
    qmp.command("pmemsave", val=int(a.shadow, 16), size=fbcon_ref.SHADOW_SIZE, filename=mem)
    qmp.command("cont")
    shadow = open(mem, "rb").read()
    os.remove(mem)
    time.sleep(1)
    _, _, again = qmp.screendump(dump)
    want, desc = fbcon_ref.render(desktop_ref.FONTDIR, shadow, w, h)
    differ, first = fbcon_ref.compare(pix, want, w)
    if first is not None:
        desc += ", first at %d,%d: screen %s, reference %s" % first
    print("CRASH size=%dx%d differ=%d held=%s (the console's cell grid full-screen, no pointer: %s)" % (
        w, h, differ, "yes" if again == pix else "no", desc), flush=True)


def model_keys(win, actions):
    """1.3 stage 4b: what the keys of one send-key do in the model (each
    pressed in order, then released)."""
    for k in actions:
        win.key(True, k == "esc")
    for k in actions:
        win.key(False, k == "esc")


def wait_events(serial, win, before):
    """The kernel's lines of the model's events since `before` (their
    number then); the sync point moves to the last one."""
    for want in win.wm.events[before:]:
        i = serial.wait(lambda l, w=want: l == w, 20, win.sync)
        if i is None:
            raise RuntimeError("no line %r" % want)
        win.sync = i


def drag_burst(qmp, serial, win, m, count, keys):
    """1.3 stage 4b: press on the top window's title bar, `count` moves with
    the left button held, release; `keys` spread over the moves."""
    packet(m, win, serial, 0, 0, True)
    if win.wm.drag is None:
        raise RuntimeError("dragburst: the press was not on a title bar (model)")
    target, start, x0, y0 = win.wm.drag, win.wm.drag["r"], m.x, m.y
    actions = [a for a in text_to_actions(keys) if a[0] != "wait"] if keys else []
    every = max(count // (len(actions) + 1), 1)
    t0 = time.monotonic()
    first = m.n + 1                     # the burst's first packet
    pending, sent_keys = [], 0
    for i in range(count):
        while len(pending) >= 4:
            m.check(*pending.pop(0))
        dx = (i * 37) % 41 - 20 or 3
        dy = (i * 53) % 31 - 15 or -2
        pending.append(m.move(dx, dy))
        win.wm.pointer(m.x, m.y)
        if sent_keys < len(actions) and i % every == every - 1:
            value = actions[sent_keys][1]
            sent_keys += 1
            qmp.command("send-key", keys=[{"type": "qcode", "data": k} for k in value], **{"hold-time": 20})
            model_keys(win, value)
    for n, want in pending:
        m.check(n, want)
    seconds = time.monotonic() - t0
    packet(m, win, serial, 0, 0, False)
    for _, value in actions[sent_keys:]:
        qmp.command("send-key", keys=[{"type": "qcode", "data": k} for k in value], **{"hold-time": 20})
        model_keys(win, value)
        sent_keys += 1
        time.sleep(0.05)
    win.drags += 1
    asked = (start[0] + m.x - x0, start[1] + m.y - y0)
    print("DRAG %d asked (%d, %d) got (%d, %d) %dx%d bar %d left %d" % (
        win.drags, asked[0], asked[1], target["r"][0], target["r"][1], target["r"][2], target["r"][3],
        win.wm.bar, desktop_ref.px(48, win.s) - target["r"][2]), flush=True)
    bad = sum(1 for n, _, _ in m.mismatches if first <= n < first + count)
    print("DRAGBURST sent=%d exact=%d seconds=%.1f keys=%d" % (count, count - bad, seconds, sent_keys), flush=True)


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
    elif step.startswith("dragburst:"):                          # 1.3 stage 4b
        parts = step.split(":", 2)
        drag_burst(qmp, serial, win, m, int(parts[1]), parts[2] if len(parts) > 2 else "")
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
    ap.add_argument("--shadow")                                 # 1.3 stage 4b
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
        win = Windows(size, a.about, a.shadow) if size else None
        if win:
            win.last_shadow = None
        windowed = a.about
        t0 = time.monotonic()
        qmp.command("cont")
        shots, used = 0, 0              # used: serial lines already consumed by screen steps
        wshots = 0                      # 1.3 stage 4a: WSCREEN steps
        for step in a.steps:
            left = max(a.timeout - (time.monotonic() - t0), 1)
            if win and (step in ("wshot", "press", "release", "esc") or step.split(":")[0] in
                        ("to", "rel", "drag", "key", "dragburst")):
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
                i = serial.wait(lambda l, s=step[5:]: l == s, left, win.sync if windowed else 0)
                if i is None:
                    print(f"WAIT {step[5:]!r}: not seen in {left:.0f} s", flush=True)
                    return 0
                if windowed:
                    win.sync = max(win.sync, i)         # 1.3 stage 4b
            elif step == "ready":                               # 1.3 stage 4b
                if serial.wait(lambda l: MARKER.match(l) is not None, left) is None:
                    print("MARKER not seen in %.0f s" % left, flush=True)
                    return 0
            elif step.startswith("crash:"):                     # 1.3 stage 4b
                i = serial.wait(lambda l, s=step[6:]: l.strip() == s.strip(), left)
                if i is None:
                    print(f"CRASH {step[6:]!r}: not seen in {left:.0f} s", flush=True)
                    return 0
                crash_screen(qmp, a, dump)
            elif step.startswith("type:"):
                before = len(win.wm.events) if win else 0
                for kind, value in text_to_actions(step[5:]):
                    if kind == "wait":
                        time.sleep(value)
                        continue
                    qmp.command("send-key", keys=[{"type": "qcode", "data": k} for k in value],
                                **{"hold-time": 20})
                    if win:
                        model_keys(win, value)          # 1.3 stage 4b
                    time.sleep(0.05)
                if windowed and len(win.wm.events) > before:
                    wait_events(serial, win, before)
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
