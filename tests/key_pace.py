#!/usr/bin/env python3
"""Key-pace report (0.12 stage 3; the stress part only; decides no check).

Reads a serial log of a boot with `keylog` in which single keys (no Shift
or other modifier) were typed, and reports how many key presses and
releases the kernel logged, and the first press that was not followed by
its own release before the next press (the loss point).

    python3 tests/key_pace.py SERIAL_LOG LABEL

Prints one line:
    KEY_PACE <label> presses=P releases=R first_loss=N|none
"""
import re
import sys

EVENT = re.compile(r"^key: (press|release) sc=0x[0-9a-f]{2} key=(\S+) ")


def main():
    log, label = sys.argv[1], sys.argv[2]
    try:
        lines = open(log, errors="replace").read().splitlines()
    except OSError:
        lines = []
    events = [m.groups() for m in map(EVENT.match, lines) if m]
    presses = sum(1 for kind, _ in events if kind == "press")
    releases = len(events) - presses
    first_loss, n = None, 0
    for i, (kind, key) in enumerate(events):
        if kind != "press":
            continue
        n += 1
        nxt = events[i + 1] if i + 1 < len(events) else None
        if nxt != ("release", key):
            first_loss = n
            break
    print("KEY_PACE %s presses=%d releases=%d first_loss=%s"
          % (label, presses, releases, first_loss if first_loss else "none"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
