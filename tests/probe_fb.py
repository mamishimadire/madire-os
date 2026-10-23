#!/usr/bin/env python3
"""The probe's helper (1.2 stage 3, docs/plans/1.2.md; test-only, standard
library only). It decides no check.

    probe_fb.py patch IN OUT WIDTH HEIGHT DEPTH
    probe_fb.py watch SOCKET SERIAL --until TEXT [--wallpaper PNG --layout JSON]
                      [--mouse-after TEXT] [--every SECONDS] [--timeout SECONDS]

patch: copies the probe kernel IN to OUT with its Multiboot header's video
request (flag bit 2 must be set) changed to WIDTH x HEIGHT x DEPTH. The
checksum covers only the magic, the flags and itself, so it stays valid.

watch: QEMU must be started paused (-S) with a QMP socket. Starts the guest
(power-on is time 0), then takes a screendump every SECONDS (0.25) until
TEXT is in the serial log (at most --timeout, 90 s), and one more 1 s
later ("FINAL"). Each screen is classed by the serial log's size read just
before it ("before" while the log is empty, else "after"), its size,
whether it is blank (one colour, given as RRGGBB) and, with --wallpaper and
--layout, whether the layout's header box equals the wallpaper's pixel for
pixel (the wallpaper centred, as Limine's "wallpaper_style: centered"
draws it; "n/a" on a smaller screen). A SHOT line is printed at each change
of class, SUMMARY lines at the end. With --mouse-after, once that text is
in the serial log, mouse events are sent with QMP input-send-event
("MOUSE").

Any error prints "probe_fb: ERROR: ..." and exits 1.
"""
import argparse
import json
import os
import struct
import sys
import time

# No __pycache__ in the source tree: the run's file guard (the snapshot
# hashed before and after) must see the tree unchanged.
sys.dont_write_bytecode = True
from qmp_menu import Qmp, png_rgb, serial_text

MAGIC = 0x1BADB002

# The mouse events sent once --mouse-after is logged: a move, a left click,
# a move back (QMP's y grows downwards; the PS/2 mouse's grows upwards).
MOUSE_EVENTS = [
    ("rel x=10 y=-5", [{"type": "rel", "data": {"axis": "x", "value": 10}},
                       {"type": "rel", "data": {"axis": "y", "value": -5}}]),
    ("btn left down", [{"type": "btn", "data": {"down": True, "button": "left"}}]),
    ("btn left up", [{"type": "btn", "data": {"down": False, "button": "left"}}]),
    ("rel x=-3 y=7", [{"type": "rel", "data": {"axis": "x", "value": -3}},
                      {"type": "rel", "data": {"axis": "y", "value": 7}}]),
]


def run_patch(a):
    d = bytearray(open(a.input, "rb").read())
    for off in range(0, min(len(d), 8192) - 48 + 1, 4):
        magic, flags, checksum = struct.unpack_from("<III", d, off)
        if magic == MAGIC and (magic + flags + checksum) & 0xFFFFFFFF == 0:
            break
    else:
        raise RuntimeError(f"{a.input}: no Multiboot header in the first 8 KiB")
    if not flags & 4:
        raise RuntimeError(f"{a.input}: the header asks for no video mode (flags 0x{flags:08x})")
    struct.pack_into("<III", d, off + 36, a.width, a.height, a.depth)
    with open(a.output, "wb") as f:
        f.write(d)
    print(f"PATCH {a.output}: header at offset {off}, flags 0x{flags:08x}, "
          f"mode type {struct.unpack_from('<I', d, off + 32)[0]}, {a.width}x{a.height}x{a.depth}")
    return 0


def classify(w, h, pix, ref, layout):
    """(blank, header) of one screen."""
    if w * h == 0:
        return "none", "n/a"
    blank = pix[:3].hex() if pix == pix[:3] * (w * h) else "no"
    header = "n/a"
    if ref is not None:
        ww, wh, rp = ref
        if w >= ww and h >= wh:
            ox, oy = (w - ww) // 2, (h - wh) // 2
            x0, y0, x1, y1 = layout["header"]
            same = all(pix[((oy + y) * w + ox + x0) * 3:((oy + y) * w + ox + x1 + 1) * 3]
                       == rp[(y * ww + x0) * 3:(y * ww + x1 + 1) * 3] for y in range(y0, y1 + 1))
            header = "match" if same else "differ"
    return blank, header


def send_mouse(qmp):
    done = []
    try:
        for name, events in MOUSE_EVENTS:
            qmp.command("input-send-event", events=events)
            done.append(name)
            time.sleep(0.1)
        print(f"MOUSE sent with input-send-event: {'; '.join(done)}", flush=True)
    except Exception as e:
        print(f"MOUSE error after {len(done)} of {len(MOUSE_EVENTS)} events: {e}", flush=True)


def run_watch(a, qmp, dump):
    ref = png_rgb(a.wallpaper) if a.wallpaper else None
    layout = None
    if a.layout:
        with open(a.layout) as f:
            layout = json.load(f)
    counts, last, first_byte, mouse_sent, seen = {}, None, None, False, None
    t0 = time.monotonic()
    qmp.command("cont")
    while time.monotonic() - t0 < a.timeout:
        size = os.path.getsize(a.serial) if os.path.exists(a.serial) else 0
        t = time.monotonic() - t0
        if size and first_byte is None:
            first_byte = t
        phase = "before" if size == 0 else "after"
        w, h, pix = qmp.screendump(dump)
        blank, header = classify(w, h, pix, ref, layout)
        key = (phase, f"{w}x{h}", blank, header)
        counts[key] = counts.get(key, 0) + 1
        if key != last:
            print(f"SHOT {t:.2f} s phase={phase} size={w}x{h} blank={blank} header={header}", flush=True)
            last = key
        text = serial_text(a.serial)
        if a.mouse_after and not mouse_sent and a.mouse_after in text:
            mouse_sent = True
            send_mouse(qmp)
        if a.until in text:
            seen = time.monotonic() - t0
            break
        time.sleep(a.every)
    if seen is None:
        print(f"UNTIL {a.until!r}: not seen in {a.timeout:.0f} s", flush=True)
    else:
        print(f"UNTIL {a.until!r}: seen after {seen:.1f} s", flush=True)
        time.sleep(1)
        w, h, pix = qmp.screendump(dump)
        blank, header = classify(w, h, pix, ref, layout)
        print(f"FINAL 1 s later: size={w}x{h} blank={blank} header={header}", flush=True)
    for (phase, size, blank, header), n in counts.items():
        print(f"SUMMARY phase={phase} size={size} blank={blank} header={header}: {n} shots", flush=True)
    total = sum(counts.values())
    matches = sum(n for k, n in counts.items() if k[3] == "match")
    print("FIRST_BYTE " + ("none seen" if first_byte is None else f"seen by {first_byte:.2f} s"), flush=True)
    print(f"HEADER_MATCHES {matches} of {total} shots", flush=True)
    if a.mouse_after and not mouse_sent:
        print(f"MOUSE not sent: {a.mouse_after!r} never logged", flush=True)
    return 0


def main():
    ap = argparse.ArgumentParser(description="the 1.2 stage 3 probe's helper")
    sub = ap.add_subparsers(dest="mode", required=True)
    p = sub.add_parser("patch")
    p.add_argument("input")
    p.add_argument("output")
    p.add_argument("width", type=int)
    p.add_argument("height", type=int)
    p.add_argument("depth", type=int)
    w = sub.add_parser("watch")
    w.add_argument("socket")
    w.add_argument("serial")
    w.add_argument("--until", required=True)
    w.add_argument("--wallpaper")
    w.add_argument("--layout")
    w.add_argument("--mouse-after")
    w.add_argument("--every", type=float, default=0.25)
    w.add_argument("--timeout", type=float, default=90)
    a = ap.parse_args()
    try:
        if a.mode == "patch":
            return run_patch(a)
        dump = a.socket + ".ppm"
        rc = run_watch(a, Qmp(a.socket), dump)
        if os.path.exists(dump):
            os.remove(dump)
        return rc
    except Exception as e:                   # reported in the probe's lines
        print(f"probe_fb: ERROR: {e}")
        return 1


if __name__ == "__main__":
    sys.exit(main())
