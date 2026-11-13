#!/usr/bin/env python3
"""Watches a QEMU guest's power-off through QMP (1.5 stage 6; standard
library only).

Connects to a QMP socket of its own (QEMU allows several -qmp monitors, so
tests/qmp_keys.py types on another one), turns on capabilities and reads
QMP's events until QEMU closes the socket (it exits) or --timeout seconds
pass. Meanwhile it polls the guest's serial log for the first line equal to
MARKER. QEMU sends SHUTDOWN with "reason": "guest-shutdown" when the guest
powered itself off (ACPI S5), "guest-reset" for a reset (with -no-reboot).

    python3 tests/qmp_power.py SOCKET LOG MARKER [--timeout SECONDS]

Prints one line:
    POWER marker=<s|none> event=<SHUTDOWN:<reason>|none> event_at=<s|none>
          after_ms=<ms from the marker to the event|none> closed=<yes|no>
(times in seconds since this script started, one decimal). Exits 0 if the
event came, 1 otherwise. It decides no check: the caller does.
"""
import argparse
import json
import socket
import sys
import time


def marker_seen(path, marker):
    try:
        with open(path, errors="replace") as f:
            return any(line.rstrip("\r\n") == marker for line in f)
    except FileNotFoundError:
        return False


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("socket")
    ap.add_argument("log")
    ap.add_argument("marker")
    ap.add_argument("--timeout", type=float, default=120)
    a = ap.parse_args()
    t0 = time.monotonic()
    s = None
    while time.monotonic() - t0 < a.timeout:
        try:
            s = socket.socket(socket.AF_UNIX)
            s.connect(a.socket)
            break
        except (ConnectionRefusedError, FileNotFoundError):
            s.close()
            s = None
            time.sleep(0.05)
    if s is None:
        print("POWER marker=none event=none event_at=none after_ms=none closed=no (no QMP socket)")
        return 1
    s.settimeout(0.2)
    buf = b""
    marker_at = event_at = None
    event = None
    closed = False
    sent = False
    while time.monotonic() - t0 < a.timeout:
        if marker_at is None and marker_seen(a.log, a.marker):
            marker_at = time.monotonic() - t0
        try:
            data = s.recv(4096)
        except socket.timeout:
            continue
        except OSError:
            closed = True
            break
        if not data:
            closed = True
            break
        buf += data
        while b"\n" in buf:
            line, buf = buf.split(b"\n", 1)
            try:
                msg = json.loads(line)
            except ValueError:
                continue
            if "QMP" in msg and not sent:
                s.sendall(b'{"execute": "qmp_capabilities"}\n')
                sent = True
            elif msg.get("event") == "SHUTDOWN" and event is None:
                event_at = time.monotonic() - t0
                event = "SHUTDOWN:%s" % msg.get("data", {}).get("reason", "?")
    if marker_at is None and marker_seen(a.log, a.marker):
        marker_at = time.monotonic() - t0       # (the log flushed after the event)
    fmt = lambda v: "none" if v is None else "%.1f" % v  # noqa: E731
    after = "none"
    if marker_at is not None and event_at is not None:
        after = "%d" % max(0, round((event_at - marker_at) * 1000))
    print("POWER marker=%s event=%s event_at=%s after_ms=%s closed=%s" % (
        fmt(marker_at), event or "none", fmt(event_at), after, "yes" if closed else "no"))
    return 0 if event else 1


if __name__ == "__main__":
    sys.exit(main())
