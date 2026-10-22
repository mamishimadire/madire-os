#!/usr/bin/env python3
"""Drives Limine's hidden boot menu in a QEMU guest through QMP (1.1, the
[1.1] boot menu checks of tests/qemu-part-4.sh; standard library only).

QEMU must be started paused (-S) with a QMP socket; this script connects,
starts the guest (cont: power-on is time 0), and reports facts as lines the
caller's checks read. It decides no check itself.

    qmp_menu.py menu SOCKET SERIAL WALLPAPER LAYOUT STEP... [--band NAME=Y0-Y1]...
                     [--shots DIR]
    qmp_menu.py nokey SOCKET SERIAL TEXT [--every SECONDS] [--timeout SECONDS]

menu: presses Home every 300 ms from power-on, taking a screendump after
each press, until the screen is 1024x768 (Limine's menu revealed; at most
30 s), prints "REVEAL <s> s, <n> presses", then runs the steps in order:
    hold:S        wait S seconds, then print the serial log's size and the
                  screen's size ("HOLD")
    shot:NAME     a screendump compared with WALLPAPER (the PNG Limine
                  shows) pixel by pixel ("STATE"): the box of every pixel
                  that differs (what Limine drew), whether that box lies
                  inside LAYOUT's panel, and the number of differing pixels
                  in LAYOUT's header, hint and version boxes and in each
                  --band (rows Y0-Y1, the whole width)
    KEY or KEY*N  a QEMU qcode (down, up, ret, home ...), N times
    until:TEXT    wait until TEXT is in the serial log (at most 60 s)
LAYOUT is the JSON written by scripts/make-wallpaper.py --layout-json (the
one source of the rectangles). With --shots, each shot is also saved as
DIR/menu-NAME.png (evidence only).

nokey: presses nothing; takes a screendump every 250 ms until TEXT is in
the serial log (at most 90 s) and prints the number of screendumps of each
size ("SIZES").

One QMP client at a time: the caller never runs this together with
tests/qmp_keys.py on the same QEMU. Any error prints "qmp_menu: ERROR: ..."
and exits 1.
"""
import argparse
import json
import os
import socket
import struct
import sys
import time
import zlib


class Qmp:
    def __init__(self, path):
        deadline = time.time() + 20
        while True:
            try:
                self.sock = socket.socket(socket.AF_UNIX)
                self.sock.connect(path)
                break
            except (ConnectionRefusedError, FileNotFoundError):
                self.sock.close()
                if time.time() > deadline:
                    raise
                time.sleep(0.05)
        self.file = self.sock.makefile("rw")
        greeting = json.loads(self.file.readline())
        if "QMP" not in greeting:
            raise RuntimeError(f"unexpected QMP greeting: {greeting}")
        self.command("qmp_capabilities")

    def command(self, name, **arguments):
        msg = {"execute": name}
        if arguments:
            msg["arguments"] = arguments
        self.file.write(json.dumps(msg) + "\n")
        self.file.flush()
        while True:
            reply = json.loads(self.file.readline())
            if "event" in reply:
                continue
            if "error" in reply:
                raise RuntimeError(f"{name} failed: {reply['error']}")
            return reply.get("return")

    def key(self, qcode):
        self.command("send-key", keys=[{"type": "qcode", "data": qcode}], **{"hold-time": 50})

    def screendump(self, path):
        """Returns (width, height, RGB bytes) of the guest's screen."""
        if os.path.exists(path):
            os.remove(path)
        self.command("screendump", filename=path)
        d = open(path, "rb").read()
        fields, i = [], 0
        while len(fields) < 4:              # P6, width, height, maxval
            while d[i:i + 1].isspace():
                i += 1
            j = i
            while not d[j:j + 1].isspace():
                j += 1
            fields.append(d[i:j])
            i = j
        if fields[0] != b"P6" or fields[3] != b"255":
            raise RuntimeError(f"screendump: not a P6/255 PPM ({fields[0]!r}, {fields[3]!r})")
        w, h = int(fields[1]), int(fields[2])
        pix = d[i + 1:i + 1 + w * h * 3]
        if len(pix) != w * h * 3:
            raise RuntimeError(f"screendump: {len(pix)} pixel bytes for {w}x{h}")
        return w, h, pix


def png_rgb(path):
    """The pixels of an 8-bit RGB PNG whose rows all use filter 0 (what
    scripts/make-wallpaper.py writes)."""
    d = open(path, "rb").read()
    if d[:8] != b"\x89PNG\r\n\x1a\n":
        raise RuntimeError(f"{path}: not a PNG")
    w, h, depth, colour = struct.unpack(">IIBB", d[16:26])
    if (depth, colour) != (8, 2):
        raise RuntimeError(f"{path}: not 8-bit RGB")
    i, idat = 8, b""
    while i < len(d):
        n = struct.unpack(">I", d[i:i + 4])[0]
        if d[i + 4:i + 8] == b"IDAT":
            idat += d[i + 8:i + 8 + n]
        i += 12 + n
    raw = zlib.decompress(idat)
    stride = w * 3 + 1
    if len(raw) != stride * h or any(raw[y * stride] != 0 for y in range(h)):
        raise RuntimeError(f"{path}: unexpected row filters or size")
    return w, h, b"".join(raw[y * stride + 1:(y + 1) * stride] for y in range(h))


def save_png(path, w, h, pix):
    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xffffffff)
    raw = b"".join(b"\x00" + pix[y * w * 3:(y + 1) * w * 3] for y in range(h))
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
                + chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b""))


def differing(w, pix, ref):
    """Every (x, y) where the screen differs from the wallpaper."""
    out = []
    row = w * 3
    for y in range(len(pix) // row):
        a, b = pix[y * row:(y + 1) * row], ref[y * row:(y + 1) * row]
        if a == b:
            continue
        for x in range(w):
            if a[x * 3:x * 3 + 3] != b[x * 3:x * 3 + 3]:
                out.append((x, y))
    return out


def serial_text(path):
    try:
        with open(path, errors="replace") as f:
            return f.read()
    except FileNotFoundError:
        return ""


def wait_serial(path, text, timeout):
    t0 = time.monotonic()
    while time.monotonic() - t0 < timeout:
        if text in serial_text(path):
            return time.monotonic() - t0
        time.sleep(0.1)
    return None


def run_menu(a, qmp, dump):
    ww, wh, ref = png_rgb(a.wallpaper)
    with open(a.layout) as f:
        layout = json.load(f)
    bands = {}
    for b in a.band:
        name, rows = b.split("=")
        y0, y1 = rows.split("-")
        bands[name] = (int(y0), int(y1))
    t0 = time.monotonic()
    qmp.command("cont")
    presses = 0
    while time.monotonic() - t0 < 30:
        qmp.key("home")
        presses += 1
        time.sleep(0.3)
        w, h, _ = qmp.screendump(dump)
        if (w, h) == (ww, wh):
            print(f"REVEAL {time.monotonic() - t0:.1f} s, {presses} presses, {w}x{h}", flush=True)
            break
    else:
        print(f"REVEAL none: the screen never became {ww}x{wh} in 30 s ({presses} presses)", flush=True)
        return 1
    for step in a.steps:
        if step.startswith("hold:"):
            time.sleep(float(step[5:]))
            w, h, _ = qmp.screendump(dump)
            size = os.path.getsize(a.serial) if os.path.exists(a.serial) else 0
            print(f"HOLD {step[5:]} s: serial {size} bytes, screen {w}x{h}", flush=True)
        elif step.startswith("shot:"):
            name = step[5:]
            time.sleep(0.4)
            w, h, pix = qmp.screendump(dump)
            if a.shots:
                save_png(os.path.join(a.shots, f"menu-{name}.png"), w, h, pix)
            if (w, h) != (ww, wh):
                print(f"STATE {name} size={w}x{h} (not the wallpaper's {ww}x{wh})", flush=True)
                continue
            d = differing(w, pix, ref)
            if d:
                xs, ys = [p[0] for p in d], [p[1] for p in d]
                box = (min(xs), min(ys), max(xs), max(ys))
            else:
                box = None
            px0, py0, px1, py1 = layout["panel"]
            inside = box is not None and px0 <= box[0] and py0 <= box[1] and box[2] <= px1 and box[3] <= py1
            line = [f"STATE {name} size={w}x{h}",
                    "box=" + ("none" if box is None else "%d,%d-%d,%d" % box),
                    f"drawn={len(d)}", "inside_panel=" + ("yes" if inside else "no")]
            for area in ("header", "hint", "version"):
                x0, y0, x1, y1 = layout[area]
                line.append(f"{area}={sum(1 for x, y in d if x0 <= x <= x1 and y0 <= y <= y1)}")
            for bname, (y0, y1) in bands.items():
                line.append(f"band_{bname}={sum(1 for x, y in d if y0 <= y <= y1)}")
            print(" ".join(line), flush=True)
        elif step.startswith("until:"):
            s = wait_serial(a.serial, step[6:], 60)
            print(f"UNTIL {step[6:]!r}: " + ("not seen in 60 s" if s is None else f"seen after {s:.1f} s"),
                  flush=True)
        else:
            key, _, n = step.partition("*")
            for _ in range(int(n or 1)):
                qmp.key(key)
                time.sleep(0.15)
    return 0


def run_nokey(a, qmp, dump):
    sizes = {}
    t0 = time.monotonic()
    qmp.command("cont")
    seen = None
    while time.monotonic() - t0 < a.timeout:
        w, h, _ = qmp.screendump(dump)
        sizes[f"{w}x{h}"] = sizes.get(f"{w}x{h}", 0) + 1
        if a.text in serial_text(a.serial):
            seen = time.monotonic() - t0
            break
        time.sleep(a.every)
    print(f"SIZES {' '.join(f'{k}:{v}' for k, v in sizes.items())}", flush=True)
    print(f"UNTIL {a.text!r}: " + ("not seen in %d s" % a.timeout if seen is None else f"seen after {seen:.1f} s"),
          flush=True)
    return 0


def main():
    ap = argparse.ArgumentParser(description="Limine boot menu checks over QMP")
    sub = ap.add_subparsers(dest="mode", required=True)
    m = sub.add_parser("menu")
    m.add_argument("socket")
    m.add_argument("serial")
    m.add_argument("wallpaper")
    m.add_argument("layout")
    m.add_argument("steps", nargs="+")
    m.add_argument("--band", action="append", default=[], help="NAME=Y0-Y1, rows counted in each shot")
    m.add_argument("--shots", help="save each shot as DIR/menu-NAME.png")
    n = sub.add_parser("nokey")
    n.add_argument("socket")
    n.add_argument("serial")
    n.add_argument("text")
    n.add_argument("--every", type=float, default=0.25)
    n.add_argument("--timeout", type=float, default=90)
    a = ap.parse_args()
    dump = a.socket + ".ppm"
    try:
        qmp = Qmp(a.socket)
        rc = run_menu(a, qmp, dump) if a.mode == "menu" else run_nokey(a, qmp, dump)
        if os.path.exists(dump):
            os.remove(dump)
        return rc
    except Exception as e:                   # reported as a test failure by the caller
        print(f"qmp_menu: ERROR: {e}")
        return 1


if __name__ == "__main__":
    sys.exit(main())
