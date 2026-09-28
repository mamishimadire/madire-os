#!/usr/bin/env python3
"""Types scripted keys into a QEMU guest through QMP (standard library only).

Waits until READY appears in the guest's serial log, then sends each key
combination with QMP send-key. QEMU sends a make and a break for every key
in a combination, exactly like a real PS/2 keyboard.

    python3 tests/qmp_keys.py SOCKET LOG READY KEYS [--gap SECONDS] [--hold MS]

KEYS is comma-separated; each item is QEMU qcodes joined by '+', e.g.
    a,shift+b,caps_lock,up,ctrl_r,ret,kp_enter
"""
import argparse
import json
import os
import socket
import sys
import time


def wait_for(predicate, timeout, what):
    deadline = time.time() + timeout
    while time.time() < deadline:
        if predicate():
            return
        time.sleep(0.05)
    raise TimeoutError(f"timed out after {timeout}s waiting for {what}")


def log_contains(path, text):
    try:
        with open(path, errors="replace") as f:
            return text in f.read()
    except FileNotFoundError:
        return False


class Qmp:
    def __init__(self, path):
        self.sock = socket.socket(socket.AF_UNIX)
        self.sock.connect(path)
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


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("socket")
    ap.add_argument("log")
    ap.add_argument("ready")
    ap.add_argument("keys")
    ap.add_argument("--gap", type=float, default=0.15, help="seconds between combinations")
    ap.add_argument("--hold", type=int, default=30, help="ms each combination is held")
    args = ap.parse_args()

    try:
        wait_for(lambda: os.path.exists(args.socket), 20, "the QMP socket")
        qmp = None
        deadline = time.time() + 20
        while qmp is None:
            try:
                qmp = Qmp(args.socket)
            except (ConnectionRefusedError, FileNotFoundError):
                if time.time() > deadline:
                    raise
                time.sleep(0.1)

        wait_for(lambda: log_contains(args.log, args.ready), 40, repr(args.ready))

        combos = [c for c in args.keys.split(",") if c]
        for combo in combos:
            keys = [{"type": "qcode", "data": k} for k in combo.split("+")]
            qmp.command("send-key", keys=keys, **{"hold-time": args.hold})
            time.sleep(args.gap)
        time.sleep(0.5)                      # let the last break code arrive
        print(f"qmp_keys: sent {len(combos)} key combinations")
        return 0
    except Exception as e:                   # reported as a test failure by the caller
        print(f"qmp_keys: ERROR: {e}")
        return 1


if __name__ == "__main__":
    sys.exit(main())
