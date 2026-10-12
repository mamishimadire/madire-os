#!/usr/bin/env python3
"""Compare VirtualBox serial logs with QEMU's (0.12).

scripts/vbox-acceptance.ps1 keeps one serial log per test as
<prefix>-<name>.serial.log; scripts/remote-regress.ps1 downloads QEMU's
per-boot logs (build/serial-*.log) as results/boot-logs.tar.gz. This script
keeps the result lines of each pair (PASS/FAIL/OK/MISMATCH, crash-screen
details, end lines), masks numbers and addresses, and lists the lines found
in only one of them. Differences in timing figures disappear with the
masking; what remains is for a person to judge.

    python scripts/compare-serial.py VBOX_DIR QEMU_DIR PREFIX [NAME,NAME,...]

QEMU_DIR is the unpacked boot-logs.tar.gz (or a build/ folder). Without
NAME, every <PREFIX>-*.serial.log in VBOX_DIR is compared. Exit status 0
always: it reports, it doesn't decide.
"""
import os
import re
import sys

# VirtualBox test names whose QEMU log has another name.
QEMU_NAMES = {
    "boot-console": "serial-boot.log",
    "scrollback": "serial-scroll.log",
    "commands": "serial-cmds091.log",
    "reboot": "serial-reboot.log",
    "module": "serial-bootmenu-module.log",
    "memexhaust": "serial-exhaust-128M.log",
    "procs": "serial-run.log",
}

KEEP = re.compile(r"PASS|FAIL|: OK\b|OK$|MISMATCH|NOT APPLICABLE|CPU exception|Error code|"
                  r"Page fault:|Cause:|Details:|Address:|test:|self-test|killed|exited|\[pid ")


def qemu_name(name):
    if name in QEMU_NAMES:
        return QEMU_NAMES[name]
    if name.startswith("user") or name in ("smep", "smap"):
        return "serial-bootmenu-%s.log" % name
    return "serial-%s.log" % name


def normalise(line):
    line = re.sub(r"0x[0-9a-fA-F]+", "0xH", line)
    line = re.sub(r"\d+", "N", line)
    return line.rstrip()


def result_lines(path):
    with open(path, encoding="latin-1") as f:
        return [normalise(l) for l in f if KEEP.search(l)]


def main():
    if len(sys.argv) < 4:
        print(__doc__)
        return 0
    vdir, qdir, prefix = sys.argv[1:4]
    if len(sys.argv) > 4:
        names = [n for n in sys.argv[4].split(",") if n]
    else:
        tail = ".serial.log"
        names = sorted(f[len(prefix) + 1:-len(tail)] for f in os.listdir(vdir)
                       if f.startswith(prefix + "-") and f.endswith(tail))
    for name in names:
        v = os.path.join(vdir, "%s-%s.serial.log" % (prefix, name))
        q = os.path.join(qdir, qemu_name(name))
        if not (os.path.exists(v) and os.path.exists(q)):
            print("== %s: no pair (VirtualBox log %s, QEMU log %s)" % (
                name, "found" if os.path.exists(v) else "missing",
                "found" if os.path.exists(q) else "missing"))
            continue
        vs, qs = result_lines(v), result_lines(q)
        only_v = [l for l in dict.fromkeys(vs) if l not in set(qs)]
        only_q = [l for l in dict.fromkeys(qs) if l not in set(vs)]
        print("== %s: %d VirtualBox / %d QEMU result lines; %d only in VirtualBox, %d only in QEMU" % (
            name, len(vs), len(qs), len(only_v), len(only_q)))
        for l in only_v:
            print("   V+ " + l[:200])
        for l in only_q:
            print("   Q+ " + l[:200])
    return 0


if __name__ == "__main__":
    sys.exit(main())
