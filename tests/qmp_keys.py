#!/usr/bin/env python3
"""Types scripted keys into a QEMU guest through QMP (standard library only).

Waits until READY appears in the guest's serial log, then sends each key
combination with QMP send-key. QEMU sends a make and a break for every key
in a combination, exactly like a real PS/2 keyboard.

    python3 tests/qmp_keys.py SOCKET LOG READY KEYS [--gap SECONDS] [--hold MS]
    python3 tests/qmp_keys.py SOCKET LOG READY --text TEXT [--gap SECONDS]

KEYS is comma-separated; each item is QEMU qcodes joined by '+', e.g.
    a,shift+b,caps_lock,up,ctrl_r,ret,kp_enter

TEXT is typed character by character (capitals and symbols with Shift).
Special keys are written in braces:
    {ret} {tab} {bs} {del} {left} {right} {home} {end} {up} {down} {caps}
    {ctrl+c}      any '+' combination of qcodes
    {wait:2}      pause 2 seconds
Either way every key goes through QEMU's PS/2 keyboard, so the guest sees
real make/break codes on IRQ1.

QEMU queues send-key requests and delivers them at its own pace, so the
last key may reach the guest well after this script has sent it. With
--done TEXT the script waits until TEXT appears in the log (for example the
output of a final command) before returning.
"""
import argparse
import json
import os
import socket
import sys
import time


PLAIN = {" ": "spc", "-": "minus", "=": "equal", "[": "bracket_left",
         "]": "bracket_right", ";": "semicolon", "'": "apostrophe",
         "`": "grave_accent", "\\": "backslash", ",": "comma", ".": "dot",
         "/": "slash"}
SHIFTED = {"!": "1", "@": "2", "#": "3", "$": "4", "%": "5", "^": "6",
           "&": "7", "*": "8", "(": "9", ")": "0", "_": "minus", "+": "equal",
           ":": "semicolon", '"': "apostrophe", "~": "grave_accent",
           "|": "backslash", "<": "comma", ">": "dot", "?": "slash"}
TOKENS = {"ret": "ret", "tab": "tab", "bs": "backspace", "del": "delete",
          "left": "left", "right": "right", "home": "home", "end": "end",
          "up": "up", "down": "down", "caps": "caps_lock"}


def text_to_actions(text):
    """Returns a list of ('keys', [qcodes]) and ('wait', seconds) actions."""
    actions = []
    i = 0
    while i < len(text):
        c = text[i]
        if c == "{":
            end = text.index("}", i)
            token = text[i + 1:end]
            i = end + 1
            if token.startswith("wait:"):
                actions.append(("wait", float(token[5:])))
            elif token in TOKENS:
                actions.append(("keys", [TOKENS[token]]))
            elif "+" in token:
                actions.append(("keys", token.split("+")))
            else:
                raise ValueError(f"unknown key token {{{token}}}")
            continue
        i += 1
        if c.islower() or c.isdigit():
            actions.append(("keys", [c]))
        elif c.isupper():
            actions.append(("keys", ["shift", c.lower()]))
        elif c in PLAIN:
            actions.append(("keys", [PLAIN[c]]))
        elif c in SHIFTED:
            actions.append(("keys", ["shift", SHIFTED[c]]))
        else:
            raise ValueError(f"cannot type {c!r}")
    return actions


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
    ap.add_argument("keys", nargs="?", default="")
    ap.add_argument("--text", help="type this text instead of KEYS (see above)")
    ap.add_argument("--gap", type=float, default=0.15, help="seconds between combinations")
    ap.add_argument("--hold", type=int, default=30, help="ms each combination is held")
    ap.add_argument("--done", help="after sending, wait until this text is in the log")
    ap.add_argument("--done-timeout", type=float, default=90)
    args = ap.parse_args()

    try:
        if args.text is not None:
            actions = text_to_actions(args.text)
        else:
            actions = [("keys", c.split("+")) for c in args.keys.split(",") if c]
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

        sent = 0
        for kind, value in actions:
            if kind == "wait":
                time.sleep(value)
                continue
            keys = [{"type": "qcode", "data": k} for k in value]
            qmp.command("send-key", keys=keys, **{"hold-time": args.hold})
            sent += 1
            time.sleep(args.gap)
        time.sleep(0.5)                      # let the last break code arrive
        if args.done is not None:
            wait_for(lambda: log_contains(args.log, args.done), args.done_timeout,
                     f"{args.done!r} (guest to finish processing the keys)")
        print(f"qmp_keys: sent {sent} key combinations")
        return 0
    except Exception as e:                   # reported as a test failure by the caller
        print(f"qmp_keys: ERROR: {e}")
        return 1


if __name__ == "__main__":
    sys.exit(main())
