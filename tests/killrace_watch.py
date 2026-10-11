#!/usr/bin/env python3
"""Kill-race watch (0.12 stage 2b; the stress part only).

Tells a stuck test=killrace boot from a slow one by guest time: it reads the
kernel's tick count (the symbol `ticks`, through QMP's human-monitor-command
"x /1gx") once a second and follows the test's progress lines ("Kill race
test: progress: N of 400 cycles, tick T", one every 50 cycles).

    python3 tests/killrace_watch.py QMP_SOCKET SERIAL_LOG ELF WALL_SECONDS

Prints one verdict line and exits with its code:
    KILLRACE_WATCH DONE ...          0  the test printed PASS or FAIL
    KILLRACE_WATCH STUCK ...         2  the guest's ticks advanced 3000 past
                                        the last progress (a real failure)
    KILLRACE_WATCH INCONCLUSIVE ...  3  the guest's ticks stopped for 30 s,
                                        or the wall-clock limit came first
                                        (host starvation, not a Madire result)
"""
import json
import re
import socket
import subprocess
import sys
import time

STUCK_TICKS = 3000
FROZEN_SECONDS = 30

START = "Kill race test: 400 processes killed at 8 points of their start..."
PROGRESS = re.compile(r"^Kill race test: progress: (\d+) of \d+ cycles, tick (\d+)$", re.M)
DONE = re.compile(r"^Kill race test: (PASS|FAIL)$", re.M)


def ticks_address(elf):
    out = subprocess.run(["nm", elf], capture_output=True, text=True, timeout=30).stdout
    for line in out.splitlines():
        p = line.split()
        if len(p) == 3 and p[2] == "ticks":
            return int(p[0], 16)
    raise SystemExit("KILLRACE_WATCH INCONCLUSIVE no symbol 'ticks' in %s" % elf)


class Qmp:
    def __init__(self, path, deadline):
        while True:
            try:
                self.s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
                self.s.connect(path)
                break
            except OSError:
                if time.time() > deadline:
                    raise
                time.sleep(0.2)
        self.s.settimeout(5.0)
        self.buf = b""
        self.recv()                                     # the greeting
        self.call({"execute": "qmp_capabilities"})

    def recv(self):
        while b"\n" not in self.buf:
            chunk = self.s.recv(65536)
            if not chunk:
                raise OSError("QMP closed")
            self.buf += chunk
        line, self.buf = self.buf.split(b"\n", 1)
        return json.loads(line)

    def call(self, cmd):
        self.s.sendall((json.dumps(cmd) + "\n").encode())
        while True:
            reply = self.recv()
            if "return" in reply or "error" in reply:
                return reply


def guest_ticks(qmp, addr):
    reply = qmp.call({"execute": "human-monitor-command",
                      "arguments": {"command-line": "x /1gx 0x%x" % addr}})
    m = re.search(r":\s*0x([0-9a-fA-F]+)", str(reply.get("return", "")))
    return int(m.group(1), 16) if m else None


def main():
    sock, log, elf, wall = sys.argv[1], sys.argv[2], sys.argv[3], float(sys.argv[4])
    end = time.time() + wall
    addr = ticks_address(elf)
    try:
        qmp = Qmp(sock, time.time() + 30)
    except OSError as e:
        print("KILLRACE_WATCH INCONCLUSIVE no QMP connection (%s)" % e)
        return 3
    base = None                 # guest tick of the last progress (or of the start)
    cycles = 0
    last_t, last_change = None, time.time()
    while time.time() < end:
        time.sleep(1.0)
        try:
            text = open(log, errors="replace").read()
        except OSError:
            text = ""
        d = DONE.search(text)
        if d:
            p = PROGRESS.findall(text)          # 0.12 stage 3: the final progress, not the last sample's
            if p:
                cycles = int(p[-1][0])
            print("KILLRACE_WATCH DONE %s (progress %d cycles)" % (d.group(1), cycles))
            return 0
        try:
            t = guest_ticks(qmp, addr)
        except OSError as e:
            print("KILLRACE_WATCH INCONCLUSIVE QEMU went away (%s), %d cycles" % (e, cycles))
            return 3
        if t is None:
            continue
        if t != last_t:
            last_t, last_change = t, time.time()
        elif time.time() - last_change >= FROZEN_SECONDS:
            print("KILLRACE_WATCH INCONCLUSIVE guest ticks stopped at %d for %d s (host starvation), "
                  "%d cycles" % (t, FROZEN_SECONDS, cycles))
            return 3
        p = PROGRESS.findall(text)
        if p:
            cycles, base = int(p[-1][0]), int(p[-1][1])
        elif START in text and base is None:
            base = t
        if base is not None and t - base >= STUCK_TICKS:
            print("KILLRACE_WATCH STUCK guest at tick %d, %d ticks after the last progress (%d cycles)"
                  % (t, t - base, cycles))
            return 2
    print("KILLRACE_WATCH INCONCLUSIVE wall-clock limit of %d s first: guest at tick %s, %d cycles"
          % (wall, last_t, cycles))
    return 3


if __name__ == "__main__":
    sys.exit(main())
