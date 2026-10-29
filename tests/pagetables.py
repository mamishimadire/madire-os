#!/usr/bin/env python3
"""Recomputes the kernel's page-table count from a boot's serial log (1.0).

The kernel's own tables (kernel/memory/paging.c, build_tables) map:
  - the kernel image, physical [1 MiB, end), at 0xFFFFFFFF80000000 + physical
    (PML4 slot 511);
  - the physical view (slot 256, 0xFFFF800000000000 + physical): the low
    1 MiB from 0x1000, and every boot-data reservation below 4 GiB from
    0x1000, but never a page of the kernel image;
  - one page table for the physical-access window (slot 508) and one for
    the kernel thread stacks (slot 507), at the addresses the walk-check
    logs;
  - 1.2 stage 5b, only when the kernel logs its "Display: slot 506: ..."
    line (a boot with a usable framebuffer): the framebuffer's pages from
    0xFFFFFD0000000000 and the back buffer's from 0xFFFFFD0040000000
    (slot 506), as many as that line gives; the slot's own count of page
    tables, logged on the same line, must equal the one computed here.
A page-directory-pointer table, page directory or page table exists for each
distinct 512 GiB, 1 GiB or 2 MiB region holding a mapped page (the recursive
slot 510 is not a table). The count is compared with the kernel's
"Paging tables: N page tables + PML4" line. 1.3 stage 3b: on a desktop
boot, slot 506 is also modelled with the background cache's area
(desktop_slot below; its own PAGETABLES_DESKTOP line).

    python3 tests/pagetables.py <serial log> --label <name>

Prints the raw figures (PAGETABLES lines) and one PAGETABLES_RESULT line;
exit status 0 when the computed count equals the logged one, 1 when it
differs, 2 when the log lacks a line it needs.
"""
import argparse
import re
import sys

KERNEL_BASE = 0xFFFFFFFF80000000
VIEW_BASE = 0xFFFF800000000000
PAGE = 0x1000
LOW_MAP = (0x1000, 0x100000)
LOW_4G = 1 << 32


def pages(start, end):
    p = start & ~(PAGE - 1)
    while p < end:
        yield p
        p += PAGE


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("log")
    ap.add_argument("--label", default="boot")
    a = ap.parse_args()
    text = open(a.log, encoding="utf-8", errors="replace").read()
    say = lambda s: print("PAGETABLES %s: %s" % (a.label, s))

    k = re.search(r"^Kernel: text 0x([0-9a-f]+)-.* end 0x([0-9a-f]+)$", text, re.M)
    logged = re.search(r"^Paging tables: (\d+) page tables \+ PML4, (\d+) mapped pages", text, re.M)
    win = re.search(r"^Paging check: window page table present 0x([0-9a-f]{16})-", text, re.M)
    kst = re.search(r"^Paging check: kernel thread stack page table present 0x([0-9a-f]{16})-", text, re.M)
    disp = re.search(r"^Display: slot 506: framebuffer 0x[0-9a-f]+-0x[0-9a-f]+ at 0x([0-9a-f]{16}) "
                     r"\((\d+) pages\), back buffer at 0x([0-9a-f]{16}) \((\d+) pages of free RAM, "
                     r"pitch \d+\), (\d+) page tables$", text, re.M)
    res = re.findall(r"^reserve: start=0x([0-9a-f]+) end=0x([0-9a-f]+) (.*)$", text, re.M)
    missing = [n for n, m in (("Kernel:", k), ("Paging tables:", logged), ("window", win),
                              ("kernel thread stack", kst)) if not m] + ([] if res else ["reserve:"])
    if missing:
        say("log lacks: " + ", ".join(missing))
        print("PAGETABLES_RESULT label=%s computed=? logged=? MISSING" % a.label)
        return 2

    kstart, kend = int(k.group(1), 16), int(k.group(2), 16)
    say("kernel image 0x%x-0x%x (physical; %d pages)" % (kstart, kend, (kend - kstart) // PAGE))
    regions = {"kernel image (slot 511)": set(KERNEL_BASE + p for p in pages(kstart, kend))}
    view = set(VIEW_BASE + p for p in pages(*LOW_MAP))
    for s, e, what in res:
        s, e = int(s, 16), int(e, 16)
        say("reserve 0x%x-0x%x %s" % (s, e, what))
        if s >= e or s >= LOW_4G:
            continue
        for p in pages(max(s, LOW_MAP[0]), min(e, LOW_4G)):
            if not (kstart <= p < kend):        # never the kernel image in the view
                view.add(VIEW_BASE + p)
    regions["physical view (slot 256)"] = view
    regions["window (slot 508)"] = {int(win.group(1), 16)}
    regions["kernel thread stacks (slot 507)"] = {int(kst.group(1), 16)}
    if disp:
        fb_va, fb_n, bb_va, bb_n = (int(disp.group(1), 16), int(disp.group(2)),
                                    int(disp.group(3), 16), int(disp.group(4)))
        say("display slot 506: framebuffer %d pages at 0x%016x, back buffer %d pages at 0x%016x, "
            "%s page tables logged" % (fb_n, fb_va, bb_n, bb_va, disp.group(5)))
        regions["display (slot 506)"] = ({fb_va + i * PAGE for i in range(fb_n)} |
                                         {bb_va + i * PAGE for i in range(bb_n)})
    else:
        say("no display slot 506 line: slot 506 not modelled")

    total = 0
    kernel_slot = None
    display_slot = None
    for name, vas in regions.items():
        l3 = {(va >> 39) & 511 for va in vas}
        l2 = {((va >> 39) & 511, (va >> 30) & 511) for va in vas}
        l1 = {((va >> 39) & 511, (va >> 30) & 511, (va >> 21) & 511) for va in vas}
        n = len(l3) + len(l2) + len(l1)
        total += n
        if name.startswith("kernel image"):
            kernel_slot = n
        if name.startswith("display"):
            display_slot = n
        say("%s: %d PDPT + %d PD + %d PT = %d tables (2 MiB regions: %s)" % (
            name, len(l3), len(l2), len(l1), n,
            ", ".join("0x%016x" % r for r in sorted({(va >> 21) << 21 for va in vas}))))
    got = int(logged.group(1))
    say("computed %d tables, logged %d (%s mapped pages logged)" % (total, got, logged.group(2)))
    ok = total == got and (not disp or display_slot == int(disp.group(5)))
    print("PAGETABLES_RESULT label=%s computed=%d logged=%d kernel_slot=%d kernel_end=0x%x display_slot=%s %s" % (
        a.label, total, got, kernel_slot, kend,
        "none" if not disp else "%d/%s" % (display_slot, disp.group(5)), "MATCH" if ok else "DIFFER"))
    desktop_slot(text, disp, say, a.label)
    return 0 if ok else 1


def desktop_slot(text, disp, say, label):
    """1.3 stage 3b: on a desktop boot (its "Desktop: slot 506 now: ..."
    line, the kernel's walk of the live slot), slot 506 is modelled again:
    the framebuffer's and the back buffer's pages as above, plus the
    background cache's pages from 0xFFFFFD0080000000 when the "Desktop:
    slot 506: background cache ..." line says it was mapped (absent
    otherwise). The modelled tables and pages must equal the walk's, and
    the cache's own tables (its page directory and page tables) the ones
    that line logs. One PAGETABLES_DESKTOP line; it decides nothing about
    PAGETABLES_RESULT or the exit status."""
    walk = re.search(r"^Desktop: slot 506 now: (\d+) page tables, (\d+) pages mapped \(walked\)$", text, re.M)
    if not walk:
        return
    cache = re.search(r"^Desktop: slot 506: background cache at 0x([0-9a-f]{16}) \((\d+) pages of free RAM, "
                      r"pitch \d+\), (\d+) page tables; ", text, re.M)
    if not disp:
        print("PAGETABLES_DESKTOP label=%s no display slot 506 line MISSING" % label)
        return
    vas = ({int(disp.group(1), 16) + i * PAGE for i in range(int(disp.group(2)))} |
           {int(disp.group(3), 16) + i * PAGE for i in range(int(disp.group(4)))})
    cache_ok = True
    if cache:
        base, n = int(cache.group(1), 16), int(cache.group(2))
        own = {base + i * PAGE for i in range(n)}
        cache_tables = len({(va >> 30) for va in own}) + len({(va >> 21) for va in own})
        cache_ok = cache_tables == int(cache.group(3)) and not (own & vas)
        say("desktop: background cache %d pages at 0x%016x, %d tables modelled, %s logged" % (
            n, base, cache_tables, cache.group(3)))
        vas |= own
    else:
        say("desktop: no background cache mapped")
    tables = len({va >> 39 for va in vas}) + len({va >> 30 for va in vas}) + len({va >> 21 for va in vas})
    logged = (int(walk.group(1)), int(walk.group(2)))
    ok = cache_ok and (tables, len(vas)) == logged
    print("PAGETABLES_DESKTOP label=%s cache=%s computed=%d/%d logged=%d/%d %s" % (
        label, "present" if cache else "absent", tables, len(vas), logged[0], logged[1], "MATCH" if ok else "DIFFER"))


if __name__ == "__main__":
    sys.exit(main())
