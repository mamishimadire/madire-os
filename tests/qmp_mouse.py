#!/usr/bin/env python3
"""Drives the guest's PS/2 mouse over QMP and reads what the kernel made
of it (1.2 stage 7, the [1.2] mouse checks of tests/qemu-part-1.sh and the
[stress] burst; standard library only).

QEMU must be started paused (-S) with a QMP socket; this script connects,
starts the guest (cont) and reports facts as lines the caller's checks
read. It decides no check itself. The kernel must log every packet (the
boot option "mouselog", or test=mouse; kernel/display/pointer.h):

    qmp_mouse.py SOCKET SERIAL STEP... [--screen WxH] [--shadow PHYS
                 --fontdir DIR] [--until TEXT] [--timeout SECONDS]

The steps run in order:
    wait:LINE      wait until LINE is a whole line of the serial log;
    type:TEXT      type TEXT on the PS/2 keyboard (as tests/qmp_fbcon.py);
    move:DX,DY     one QMP input-send-event: X by DX, Y by DY (screen
                   directions: right and down positive);
    btn:B:down, btn:B:up   one input-send-event: button B (left, middle,
                   right) pressed or released;
    screen         take a screen (below) once the pointer is drawn;
    console        (1.2 stage 10) take a screen of a console without a
                   pointer: after the first "uptime: " line that follows
                   the last packet's line (the idle loop prints it after
                   the packets were taken, so any drawing for them is
                   done), compared with the cell grid alone;
    burst:N        N packets as fast as the kernel takes them, never more
                   than 4 waiting (the PS/2 device's queue holds 5), each
                   a move (and every 25th also a left button change).
After every move and button step this script waits for the kernel's line
of that packet and compares it with the one it predicts: the packet's
number (counted from 1: every packet of the boot is injected here), the
motion, the buttons held, pressed and released and, with --screen, the
clamped position:
    mouse: packet <n>: dx <+-d> dy <+-d> buttons <L|-><M|-><R|->
           [ pressed ..][ released ..][ at (<x>, <y>)]
Taking screen n: waits for "pointer: drawn at (<x>, <y>)" (the predicted
position) after the last packet's line, stops the guest, takes a
screendump, saves the kernel's vga_shadow (with --shadow), lets it go on
(cont) and compares the screen with tests/fbcon_ref.py's rendering of the
cell grid with tests/pointer_ref.py's pointer drawn on it. Prints

    PACKETS sent=<n> exact=<n> (each mismatch: MISMATCH <n> want '...' got '...')
    SCREEN <n> size=<w>x<h> differ=<pixels> (<descriptions>)
    BURST sent=<n> exact=<n> seconds=<s>
    UNTIL '<text>': seen after <s> s      (or: not seen in <t> s)

and "qmp_mouse: ERROR: ..." (exit 1) on any error, a packet line that
never comes among them. One QMP client at a time.
"""
import argparse
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
# No tests/__pycache__/ from these imports (tests/ is in the files under test).
sys.dont_write_bytecode = True
from qmp_menu import Qmp, wait_serial  # noqa: E402
from qmp_keys import text_to_actions  # noqa: E402
import fbcon_ref  # noqa: E402
import pointer_ref  # noqa: E402

BUTTONS = {"left": 1, "right": 2, "middle": 4}
PACKET_WAIT = 20                    # seconds for one packet's line


class Serial:
    """The serial log's lines, read on as it grows."""

    def __init__(self, path):
        self.path, self.pos, self.part, self.lines = path, 0, "", []

    def poll(self):
        try:
            with open(self.path, "rb") as f:
                f.seek(self.pos)
                data = f.read()
        except FileNotFoundError:
            return
        self.pos += len(data)
        text = self.part + data.decode("latin-1")
        parts = text.split("\n")
        self.part = parts.pop()
        self.lines.extend(p.rstrip("\r") for p in parts)

    def wait(self, test, timeout, start=0):
        """Index of the first line from `start` for which test(line), or None."""
        t0 = time.monotonic()
        i = start
        while True:
            self.poll()
            while i < len(self.lines):
                if test(self.lines[i]):
                    return i
                i += 1
            if time.monotonic() - t0 > timeout:
                return None
            time.sleep(0.002)


class Mouse:
    def __init__(self, qmp, serial, screen):
        self.qmp, self.serial, self.screen = qmp, serial, screen
        self.n, self.buttons = 0, 0
        self.x, self.y = (screen[0] // 2, screen[1] // 2) if screen else (None, None)
        self.sent = self.exact = 0
        self.last_line = 0              # the index of the last packet's line
        self.mismatches = []

    def send(self, events):
        self.qmp.command("input-send-event", events=events)

    def predict(self, dx, dy, buttons):
        self.n += 1
        down, up = buttons & ~self.buttons, self.buttons & ~buttons
        self.buttons = buttons
        letters = lambda b: ("L" if b & 1 else "") + ("M" if b & 4 else "") + ("R" if b & 2 else "")
        line = "mouse: packet %d: dx %s%d dy %s%d buttons %s%s%s" % (
            self.n, "-" if dx < 0 else "+", abs(dx), "-" if dy < 0 else "+", abs(dy),
            ("L" if buttons & 1 else "-") + ("M" if buttons & 4 else "-") + ("R" if buttons & 2 else "-"),
            " pressed " + letters(down) if down else "", " released " + letters(up) if up else "")
        if self.screen:
            self.x = min(max(self.x + dx, 0), self.screen[0] - 1)
            self.y = min(max(self.y + dy, 0), self.screen[1] - 1)
            line += " at (%d, %d)" % (self.x, self.y)
        self.sent += 1
        return line

    def check(self, n, want):
        prefix = "mouse: packet %d: " % n
        i = self.serial.wait(lambda l: l.startswith(prefix), PACKET_WAIT, self.last_line)
        if i is None:
            raise RuntimeError("no line for packet %d in %d s (want %r)" % (n, PACKET_WAIT, want))
        self.last_line = i
        got = self.serial.lines[i]
        if got == want:
            self.exact += 1
        else:
            self.mismatches.append((n, want, got))
            print("MISMATCH %d want %r got %r" % (n, want, got), flush=True)

    def move(self, dx, dy, buttons=None):
        events = []
        if buttons is not None:
            for name, bit in BUTTONS.items():
                if (buttons ^ self.buttons) & bit:
                    events.append({"type": "btn", "data": {"down": bool(buttons & bit), "button": name}})
        events += [{"type": "rel", "data": {"axis": "x", "value": dx}},
                   {"type": "rel", "data": {"axis": "y", "value": dy}}]
        want = self.predict(dx, dy, self.buttons if buttons is None else buttons)
        self.send(events)
        return self.n, want

    def button(self, name, down):
        bit = BUTTONS[name]
        new = self.buttons | bit if down else self.buttons & ~bit
        want = self.predict(0, 0, new)
        self.send([{"type": "btn", "data": {"down": down, "button": name}}])
        return self.n, want


def grab(qmp, a, dump, mem):
    """The screen and the kernel's vga_shadow, the guest stopped meanwhile."""
    qmp.command("stop")
    w, h, pix = qmp.screendump(dump)
    if os.path.exists(mem):
        os.remove(mem)
    qmp.command("pmemsave", val=int(a.shadow, 16), size=fbcon_ref.SHADOW_SIZE, filename=mem)
    qmp.command("cont")
    return w, h, pix, open(mem, "rb").read()


def burst(m, count):
    """count packets, at most 4 waiting; returns the seconds taken."""
    t0 = time.monotonic()
    pending = []
    for i in range(count):
        while len(pending) >= 4:
            n, want = pending.pop(0)
            m.check(n, want)
        dx = (i * 37) % 199 - 99 or 7
        dy = (i * 53) % 151 - 75 or -5
        buttons = m.buttons ^ 1 if i % 25 == 24 else None
        pending.append(m.move(dx, dy, buttons))
    for n, want in pending:
        m.check(n, want)
    return time.monotonic() - t0


def main():
    ap = argparse.ArgumentParser(description="PS/2 mouse sessions over QMP")
    ap.add_argument("socket")
    ap.add_argument("serial")
    ap.add_argument("steps", nargs="+")
    ap.add_argument("--screen", help="WxH: the pointer is on a screen of that size")
    ap.add_argument("--shadow", help="physical address of vga_shadow (hex)")
    ap.add_argument("--fontdir")
    ap.add_argument("--until")
    ap.add_argument("--timeout", type=float, default=120)
    a = ap.parse_args()
    screen = tuple(int(v) for v in a.screen.split("x")) if a.screen else None
    dump, mem = a.socket + ".ppm", os.path.abspath(a.socket + ".shadow")
    try:
        qmp = Qmp(a.socket)
        serial = Serial(a.serial)
        m = Mouse(qmp, serial, screen)
        t0 = time.monotonic()
        qmp.command("cont")
        shots = 0
        for step in a.steps:
            left = max(a.timeout - (time.monotonic() - t0), 1)
            if step.startswith("wait:"):
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
            elif step.startswith("move:"):
                dx, dy = (int(v) for v in step[5:].split(","))
                m.check(*m.move(dx, dy))
            elif step.startswith("btn:"):
                _, name, how = step.split(":")
                m.check(*m.button(name, how == "down"))
            elif step.startswith("burst:"):
                s = burst(m, int(step[6:]))
                print(f"BURST sent={m.sent} exact={m.exact} seconds={s:.1f}", flush=True)
            elif step == "screen":
                shots += 1
                drawn = "pointer: drawn at (%d, %d)" % (m.x, m.y)
                if serial.wait(lambda l: l == drawn, PACKET_WAIT, m.last_line) is None:
                    raise RuntimeError("no line %r after packet %d" % (drawn, m.n))
                w, h, pix, shadow = grab(qmp, a, dump, mem)
                cells, desc = fbcon_ref.render(a.fontdir, shadow, w, h)
                want, pdesc = pointer_ref.paint(cells, w, h, m.x, m.y)
                differ, first = fbcon_ref.compare(pix, want, w)
                extra = "" if first is None else ", first at %d,%d: screen %s, reference %s" % first
                print(f"SCREEN {shots} size={w}x{h} differ={differ} ({desc}; {pdesc}{extra})", flush=True)
            elif step == "console":
                shots += 1
                if serial.wait(lambda l: l.startswith("uptime: "), PACKET_WAIT, m.last_line + 1) is None:
                    raise RuntimeError("no uptime line after packet %d in %d s" % (m.n, PACKET_WAIT))
                w, h, pix, shadow = grab(qmp, a, dump, mem)
                want, desc = fbcon_ref.render(a.fontdir, shadow, w, h)
                differ, first = fbcon_ref.compare(pix, want, w)
                extra = "" if first is None else ", first at %d,%d: screen %s, reference %s" % first
                print(f"SCREEN {shots} size={w}x{h} differ={differ} ({desc}; no pointer{extra})", flush=True)
            else:
                raise ValueError("unknown step %r" % step)
        print(f"PACKETS sent={m.sent} exact={m.exact}", flush=True)
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
        print(f"qmp_mouse: ERROR: {e}")
        return 1


if __name__ == "__main__":
    sys.exit(main())
