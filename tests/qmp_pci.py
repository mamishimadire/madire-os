#!/usr/bin/env python3
"""Reads QEMU's own view of the PCI buses over QMP (1.4 stage 3, the [1.4]
PCI checks of tests/qemu-part-static.sh; standard library only).

    qmp_pci.py SOCKET SERIAL LINE [--timeout SECONDS]

Connects to a QMP socket of a running QEMU (a second -qmp socket; the
first is qmp_keys.py's), waits until LINE (for example "System ready.
Press any key to open the console.") is a whole line of the serial log,
then asks QEMU for its PCI devices (query-pci: every function on every bus,
those behind a PCI-to-PCI bridge under the bridge) and prints, in the
order Madire scans them (bus 0 by device and function, then each bridge's
secondary bus in the order the bridges come):

    QPCI bb:dd.f vvvv:dddd class cc.ss bar5=0x<address>|none
    QPCI_SUMMARY buses=<n> devices=<n> functions=<n> bridges=<n> ahci=bb:dd.f|none bar5=0x<address>|none

The AHCI controller is the first function of class 01.06 (QMP's class_info
gives no programming interface; the tests' only such function is QEMU's
ich9-ahci). BAR5's address is the one the firmware assigned, as QEMU
holds it. It decides no check itself. Any error prints "qmp_pci: ERROR:
..." and exits 1.
"""
import argparse
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
# No tests/__pycache__/ from this import: tests/ is in the files under test,
# which must not change during a run (run 39923089306).
sys.dont_write_bytecode = True
from qmp_menu import Qmp, serial_text  # noqa: E402


def wait_line(path, line, timeout):
    """Seconds until `line` is a whole line of the serial log, or None."""
    t0 = time.monotonic()
    while time.monotonic() - t0 < timeout:
        if line in serial_text(path).splitlines():
            return time.monotonic() - t0
        time.sleep(0.1)
    return None


def walk(buses):
    """Every function (QMP's device dicts) in Madire's scan order, the
    number of bridges, and the bus numbers scanned (0 and each bridge's
    secondary bus)."""
    out, queue, bridges, numbers = [], [], 0, {0}
    for b in buses:
        queue.append(b.get("devices", []))
    while queue:
        devices = sorted(queue.pop(0), key=lambda d: (d["slot"], d["function"]))
        for d in devices:
            out.append(d)
            br = d.get("pci_bridge")
            if br is not None:
                bridges += 1
                numbers.add(br["bus"]["secondary"])
                queue.append(br.get("devices", []))
    return out, bridges, numbers


def bar5(d):
    for r in d.get("regions", []):
        if r.get("bar") == 5 and r.get("type") == "memory":
            return "0x%08x" % r["address"]
    return "none"


def main():
    ap = argparse.ArgumentParser(description="QEMU's PCI devices over QMP")
    ap.add_argument("socket")
    ap.add_argument("serial")
    ap.add_argument("line")
    ap.add_argument("--timeout", type=float, default=60)
    a = ap.parse_args()
    try:
        qmp = Qmp(a.socket)
        seen = wait_line(a.serial, a.line, a.timeout)
        if seen is None:
            print(f"QPCI_LINE {a.line!r}: not seen in {a.timeout:.0f} s", flush=True)
            return 0
        print(f"QPCI_LINE {a.line!r}: seen after {seen:.1f} s", flush=True)
        funcs, bridges, numbers = walk(qmp.command("query-pci"))
        ahci = None
        for d in funcs:
            cls = d["class_info"]["class"]
            loc = "%02x:%02x.%x" % (d["bus"], d["slot"], d["function"])
            print("QPCI %s %04x:%04x class %02x.%02x bar5=%s" % (loc, d["id"]["vendor"], d["id"]["device"],
                  cls >> 8, cls & 0xFF, bar5(d)), flush=True)
            if ahci is None and cls == 0x0106:
                ahci = (loc, bar5(d))
        buses = len(numbers)
        devices = len({(d["bus"], d["slot"]) for d in funcs})
        print("QPCI_SUMMARY buses=%d devices=%d functions=%d bridges=%d ahci=%s bar5=%s" %
              (buses, devices, len(funcs), bridges, ahci[0] if ahci else "none", ahci[1] if ahci else "none"),
              flush=True)
    except Exception as e:  # noqa: BLE001 (any failure is reported, not raised)
        print(f"qmp_pci: ERROR: {e}", flush=True)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
