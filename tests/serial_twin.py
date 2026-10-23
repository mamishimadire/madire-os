#!/usr/bin/env python3
"""Compares a framebuffer boot's serial log with its text twin's (1.2
stage 6; standard library only).

The twins boot the same test ISO entry on the same machine, once with
"textmode: yes" (VGA text) and once without (the framebuffer and its
console). Their serial logs must be the same apart from these allowed
differences, each named in the output:

  fb-only    lines only the framebuffer boot logs: "Display: slot 506: ...",
             "Display: console on the framebuffer: ...", and the paging
             walk-check's items for slot 506 (every "Paging check: display
             ..." line: the slot, the framebuffer, the back buffer and the
             unmapped page after each); since 1.2 stage 7 also every
             "Mouse: ..." line (the mouse starts on framebuffer boots
             only, decision 10);
  frames     the walk-check's free-frame items ("Paging check: PML4 frame
             (free RAM) ..." and "Paging check: page table frame (free
             RAM) ..."), one pair per page table: slot 506 adds page
             tables, so these are compared by count only (reported);
  display    the "Display: " line of what the boot loader gave (the
             framebuffer's mode, or VGA text);
  kernel-half  "Kernel half frozen: ...": the framebuffer boot's list is the
             text boot's with slot 506 added (7 entries instead of 6);
  figures    lines that are equal once every number is masked, from the
             figures slot 506 changes: those starting with "Paging",
             "Kernel heap self-test: no page leak", "Scheduler self-test:
             everything returned", "User-mode self-test: ", "Filesystem:
             budget", "Filesystem self-test: data". In these, every free-page
             figure ("free pages N", "of N free pages") must be lower by
             exactly the slot's cost (its back-buffer pages and page tables,
             from the "Display: slot 506" line), and every page-table count
             ("N page tables", "kernel page tables N", "expected N tables")
             higher by exactly its page tables;
  timing     the lines that differ between any two boots: "Diagnostic
             waits: calibrated: ..." and "Filesystem: clock: ..." (the RTC's
             time), equal once every number is masked.

Every other line must be identical, in the same order.

    serial_twin.py TEXT_LOG FB_LOG

Prints the counts per kind, every allowed difference (both lines), every
other difference, and a last line
    TWIN_RESULT lines=<n> identical=<n> fb-only=<n> frames=<t>/<f> display=<n> kernel-half=<n> figures=<n> timing=<n> other=<n> MATCH|MISMATCH
MATCH only with other=0 and the slot's cost read. Exit status 0 always:
it reports, the caller's check decides.
"""
import re
import sys

FB_ONLY = re.compile(r"^(Display: slot 506: |Display: console on the framebuffer: |Paging check: display |Mouse: )")
FRAMES = re.compile(r"^Paging check: (PML4 frame|page table frame) \(free RAM\) ")
DISPLAY = re.compile(r"^Display: (framebuffer |VGA text )")
FIGURES = re.compile(r"^(Paging|Kernel heap self-test: no page leak|Scheduler self-test: everything returned|"
                     r"User-mode self-test: |Filesystem: budget|Filesystem self-test: data)")
TIMING = re.compile(r"^(Diagnostic waits: calibrated: |Filesystem: clock: )")
SLOT = re.compile(r"^Display: slot 506: .* back buffer at 0x[0-9a-f]+ \((\d+) pages of free RAM, pitch \d+\), (\d+) page tables$")
FREE = re.compile(r"free pages (\d+)|of (\d+) free pages")
TABLES = re.compile(r"(\d+) page tables|kernel page tables (\d+)|kernel page tables \d+ -> (\d+)|expected (\d+) tables")


def mask(line):
    return re.sub(r"\d+", "N", re.sub(r"0x[0-9a-fA-F]+", "0xH", line))


def numbers(regex, line):
    return [int(next(g for g in m.groups() if g is not None)) for m in regex.finditer(line)]


def lines(path):
    with open(path, encoding="latin-1") as f:
        return [l.rstrip("\n") for l in f]


def figures_ok(a, b, cost, tables):
    """Masked-equal, every free-page figure lower by `cost`, every page-table
    count higher by `tables`."""
    if mask(a) != mask(b) or cost is None:
        return False
    fa, fb = numbers(FREE, a), numbers(FREE, b)
    ta, tb = numbers(TABLES, a), numbers(TABLES, b)
    return (len(fa) == len(fb) and all(x - y == cost for x, y in zip(fa, fb)) and
            len(ta) == len(tb) and all(y - x == tables for x, y in zip(ta, tb)))


def main():
    if len(sys.argv) != 3:
        print(__doc__)
        return 0
    text, fb = lines(sys.argv[1]), lines(sys.argv[2])
    cost = tables = None
    for l in fb:
        m = SLOT.match(l)
        if m:
            tables = int(m.group(2))
            cost = int(m.group(1)) + tables
    fb_only = [l for l in fb if FB_ONLY.match(l)]
    t_frames = [l for l in text if FRAMES.match(l)]
    f_frames = [l for l in fb if FRAMES.match(l)]
    t = [l for l in text if not FRAMES.match(l)]
    f = [l for l in fb if not FB_ONLY.match(l) and not FRAMES.match(l)]
    n = dict(identical=0, display=0, kernel_half=0, figures=0, timing=0, other=0)
    for l in fb_only:
        print("  fb-only: %s" % l)
    print("  frames: %d free-frame items in the text boot, %d in the framebuffer boot" % (len(t_frames), len(f_frames)))
    print("  slot 506's cost: %s pages (back buffer and page tables), %s page tables" % (
        "?" if cost is None else cost, "?" if tables is None else tables))
    for i in range(max(len(t), len(f))):
        a = t[i] if i < len(t) else None
        b = f[i] if i < len(f) else None
        if a == b:
            n["identical"] += 1
            continue
        both = a is not None and b is not None
        if both and DISPLAY.match(a) and DISPLAY.match(b):
            kind = "display"
        elif (both and a.startswith("Kernel half frozen: PML4 slots 256, 507, ") and "(6 entries)" in a and
              b == a.replace("slots 256, 507, ", "slots 256, 506, 507, ").replace("(6 entries)", "(7 entries)")):
            kind = "kernel_half"
        elif both and FIGURES.match(a) and FIGURES.match(b) and figures_ok(a, b, cost, tables):
            kind = "figures"
        elif both and TIMING.match(a) and TIMING.match(b) and mask(a) == mask(b):
            kind = "timing"
        else:
            kind = "other"
        n[kind] += 1
        print("  %s, line %d:" % ("OTHER" if kind == "other" else kind.replace("_", "-"), i + 1))
        print("    text: %s" % ("(none)" if a is None else a))
        print("    fb:   %s" % ("(none)" if b is None else b))
        if kind == "other" and n["other"] >= 20:
            print("  (more differences not listed)")
            break
    ok = n["other"] == 0 and cost is not None and bool(fb)
    print("TWIN_RESULT lines=%d identical=%d fb-only=%d frames=%d/%d display=%d kernel-half=%d figures=%d "
          "timing=%d other=%d %s" % (len(f), n["identical"], len(fb_only), len(t_frames), len(f_frames),
                                     n["display"], n["kernel_half"], n["figures"], n["timing"], n["other"],
                                     "MATCH" if ok else "MISMATCH"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
