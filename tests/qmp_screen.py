#!/usr/bin/env python3
"""Reads the VGA text screen of a running QEMU guest through QMP (0.9.2).

Standard library only. Optionally waits until a regex has matched a given
number of lines in the guest's serial log, then saves the guest's VGA text
buffer (physical 0xB8000, 80x25 cells of character + attribute) with the
QMP command pmemsave, prints the 25 rows, and checks them.

    python3 tests/qmp_screen.py SOCKET DUMPFILE [--log LOG --wait REGEX COUNT]
            [--row N REGEX]... [--attr N HEX]... [--not-row N REGEX]... [--addr HEX]

  --addr HEX           1.2 stage 10b: read the 80x25 cells at this physical
                       address instead of 0xB8000: the console's cell grid
                       (the kernel's vga_shadow, whose cells have the VGA
                       buffer's layout), on a boot whose screen is a
                       framebuffer
  --wait REGEX COUNT   wait (up to --timeout s) until COUNT lines of LOG match
  --row N REGEX        row N's text (trailing spaces removed) must match
  --not-row N REGEX    row N's text must not match
  --attr N HEX         every cell of row N must have this attribute (e.g. 0x70)

Prints each row as "row NN|text|" and one "CHECK OK/FAIL ..." line per
check; exits with 1 if any check fails or the wait times out. DUMPFILE must
be a path QEMU can write (absolute). Only one QMP client can be connected at
a time, so run this between tests/qmp_keys.py calls.
"""
import argparse
import json
import os
import re
import socket
import sys
import time

COLS, ROWS, VGA = 80, 25, 0xB8000


def count_matches(path, regex):
    try:
        with open(path, errors="replace") as f:
            return sum(1 for line in f if regex.search(line))
    except FileNotFoundError:
        return 0


def qmp_pmemsave(sock_path, addr, size, filename):
    s = socket.socket(socket.AF_UNIX)
    s.connect(sock_path)
    f = s.makefile("rw")
    json.loads(f.readline())                        # greeting

    def command(name, **arguments):
        msg = {"execute": name}
        if arguments:
            msg["arguments"] = arguments
        f.write(json.dumps(msg) + "\n")
        f.flush()
        while True:
            reply = json.loads(f.readline())
            if "return" in reply or "error" in reply:
                return reply

    command("qmp_capabilities")
    reply = command("pmemsave", val=addr, size=size, filename=filename)
    s.close()
    if "error" in reply:
        raise RuntimeError(f"pmemsave failed: {reply['error']}")


def main():
    p = argparse.ArgumentParser()
    p.add_argument("socket")
    p.add_argument("dumpfile")
    p.add_argument("--log")
    p.add_argument("--wait", nargs=2, metavar=("REGEX", "COUNT"))
    p.add_argument("--timeout", type=float, default=60)
    p.add_argument("--row", nargs=2, action="append", default=[], metavar=("N", "REGEX"))
    p.add_argument("--not-row", nargs=2, action="append", default=[], metavar=("N", "REGEX"))
    p.add_argument("--attr", nargs=2, action="append", default=[], metavar=("N", "HEX"))
    p.add_argument("--addr", default=hex(VGA))
    a = p.parse_args()

    ok = True
    if a.wait:
        regex, want = re.compile(a.wait[0]), int(a.wait[1])
        deadline = time.time() + a.timeout
        while count_matches(a.log, regex) < want:
            if time.time() > deadline:
                print(f"CHECK FAIL wait: fewer than {want} lines match /{a.wait[0]}/")
                return 1
            time.sleep(0.1)
        time.sleep(0.3)                             # let the screen settle

    if os.path.exists(a.dumpfile):
        os.remove(a.dumpfile)
    qmp_pmemsave(a.socket, int(a.addr, 16), COLS * ROWS * 2, os.path.abspath(a.dumpfile))
    deadline = time.time() + 10
    while not (os.path.exists(a.dumpfile) and os.path.getsize(a.dumpfile) >= COLS * ROWS * 2):
        if time.time() > deadline:
            print("CHECK FAIL dump: no screen dump written")
            return 1
        time.sleep(0.05)
    data = open(a.dumpfile, "rb").read()

    rows, attrs = [], []
    for r in range(ROWS):
        cells = data[r * COLS * 2:(r + 1) * COLS * 2]
        text = "".join(chr(c) if 0x20 <= c <= 0x7E else "?" for c in cells[0::2])
        rows.append(text.rstrip(" "))
        attrs.append(list(cells[1::2]))
        print(f"row {r:02d}|{rows[r]}|")

    for n, regex in a.row:
        good = re.search(regex, rows[int(n)]) is not None
        ok &= good
        print(f"CHECK {'OK' if good else 'FAIL'} row {n} matches /{regex}/")
    for n, regex in a.not_row:
        good = re.search(regex, rows[int(n)]) is None
        ok &= good
        print(f"CHECK {'OK' if good else 'FAIL'} row {n} does not match /{regex}/")
    for n, hexval in a.attr:
        want = int(hexval, 16)
        bad = sum(1 for x in attrs[int(n)] if x != want)
        ok &= bad == 0
        print(f"CHECK {'OK' if bad == 0 else 'FAIL'} row {n}: all {COLS} cells attribute "
              f"0x{want:02x} ({bad} differ)")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
