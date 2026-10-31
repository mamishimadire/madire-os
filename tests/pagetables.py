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
(desktop_slot below; its own PAGETABLES_DESKTOP line). 1.3 stage 4a: and
again with the window surfaces after the cache (desktop_surfaces below;
its own PAGETABLES_SURFACES line). 1.4 stage 4: on a boot with an AHCI
controller, slot 508's storage areas (storage_slot below; its own
PAGETABLES_STORAGE lines).

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
    desktop_surfaces(text, disp, say, a.label)
    storage_slot(text, win, say, a.label)
    return 0 if ok else 1


STORAGE_BASE = 0xFFFFFE0080000000       # vm_layout.h: VM_STORAGE_BASE (the DMA pages)
BLK_CACHE_BASE = 0xFFFFFE0080200000     # VM_BLK_CACHE_BASE (the block cache's pages)


def storage_slot(text, win, say, label):
    """1.4 stage 4: on a boot that started the AHCI driver (its "ahci:
    slot 508: registers ..." line), slot 508 is modelled after the driver's
    start and, on a test=diskread boot, after the reads: the window's page
    table (no page mapped: the window and the test page are unmapped),
    the registers' pages from the page of their logged address, the DMA
    pages from VM_STORAGE_BASE and the block cache's pages from
    VM_BLK_CACHE_BASE, as many as each line gives. Each model's tables and
    pages must equal the kernel's walk of the slot ("slot 508 now"), and
    the registers' first page must be mapped to their frame with PCD and
    PWT (0x18) set. One PAGETABLES_STORAGE line for each walk; they decide
    nothing about PAGETABLES_RESULT or the exit status."""
    regs = re.search(r"^ahci: slot 508: registers 0x([0-9a-f]{8}) at 0x([0-9a-f]{16}) \((\d+) pages, uncached: "
                     r"PCD and PWT\); the first page's entry: frame 0x([0-9a-f]+), flags 0x([0-9a-f]+)$", text, re.M)
    if not regs:
        return
    start = re.search(r"^ahci: slot 508 now: (\d+) page tables, (\d+) pages mapped \(walked\); (\d+) page tables "
                      r"added, (\d+) DMA pages, (\d+) cache pages$", text, re.M)
    read = re.search(r"^diskread: slot 508 now: (\d+) page tables, (\d+) pages mapped \(walked\)$", text, re.M)
    cache = re.search(r"^diskread: cache: (\d+) of (\d+) pages made, ", text, re.M)
    bar, va, n = int(regs.group(1), 16), int(regs.group(2), 16), int(regs.group(3))
    frame, flags = int(regs.group(4), 16), int(regs.group(5), 16)
    uncached = frame == bar & ~(PAGE - 1) and flags & 0x18 == 0x18 and flags & 1
    reg_vas = {(va & ~(PAGE - 1)) + i * PAGE for i in range(n)}
    say("storage: registers 0x%08x, %d pages at 0x%016x; first entry frame 0x%x flags 0x%x (%s)" % (
        bar, n, va & ~(PAGE - 1), frame, flags, "uncached" if uncached else "NOT uncached"))
    if not start:
        print("PAGETABLES_STORAGE label=%s at=start no 'ahci: slot 508 now' line MISSING" % label)
        return
    window = {int(win.group(1), 16)}
    walks = [("start", start, int(start.group(4)), int(start.group(5)))]
    if read and cache:
        walks.append(("diskread", read, int(start.group(4)), int(cache.group(1))))
    for at, walk, dma, made in walks:
        vas = reg_vas | {STORAGE_BASE + i * PAGE for i in range(dma)} | {BLK_CACHE_BASE + i * PAGE for i in range(made)}
        every = vas | window
        tables = len({v >> 39 for v in every}) + len({v >> 30 for v in every}) + len({v >> 21 for v in every})
        logged = (int(walk.group(1)), int(walk.group(2)))
        ok = uncached and (tables, len(vas)) == logged
        say("storage at %s: %d register pages, %d DMA pages, %d cache pages; %d tables modelled" % (
            at, len(reg_vas), dma, made, tables))
        print("PAGETABLES_STORAGE label=%s at=%s registers=%d dma=%d cache=%d computed=%d/%d logged=%d/%d %s" % (
            label, at, len(reg_vas), dma, made, tables, len(vas), logged[0], logged[1], "MATCH" if ok else "DIFFER"))


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


def desktop_surfaces(text, disp, say, label):
    """1.3 stage 4a: on a desktop boot that planned the window surfaces
    (its "Desktop: slot 506 with the window surfaces: ..." line, the
    kernel's walk after them), slot 506 is modelled once more: the
    framebuffer, the back buffer and the background cache (if mapped), as in
    desktop_slot, plus every "Desktop: slot 506: window surface NAME at VA
    (N pages ...), T page tables" line's pages in the order logged. Each
    surface must lie in the cache's area, right after the pages mapped
    before it (the cache's, then each surface's: VM_DESKTOP_BASE + those
    pages), overlap nothing, and its own T must be the page directory and
    page tables it adds to the area. The modelled tables and pages must
    equal the walk's. One PAGETABLES_SURFACES line; it decides nothing
    about PAGETABLES_RESULT or the exit status."""
    walk = re.search(r"^Desktop: slot 506 with the window surfaces: (\d+) page tables, (\d+) pages mapped "
                     r"\(walked\)$", text, re.M)
    if not walk:
        return
    if not disp:
        print("PAGETABLES_SURFACES label=%s no display slot 506 line MISSING" % label)
        return
    area = 0xFFFFFD0080000000
    vas = ({int(disp.group(1), 16) + i * PAGE for i in range(int(disp.group(2)))} |
           {int(disp.group(3), 16) + i * PAGE for i in range(int(disp.group(4)))})
    cache = re.search(r"^Desktop: slot 506: background cache at 0x([0-9a-f]{16}) \((\d+) pages of free RAM, ",
                      text, re.M)
    mapped = set()                      # the area's pages so far
    if cache:
        mapped = {int(cache.group(1), 16) + i * PAGE for i in range(int(cache.group(2)))}
    ok, names = True, []
    for name, va, n, tables in re.findall(r"^Desktop: slot 506: window surface (\S+) at 0x([0-9a-f]{16}) "
                                          r"\((\d+) pages of free RAM, \d+x\d+, pitch \d+\), (\d+) page tables; ",
                                          text, re.M):
        va, n, tables = int(va, 16), int(n), int(tables)
        own = {va + i * PAGE for i in range(n)}
        before = (len({p >> 30 for p in mapped}), len({p >> 21 for p in mapped}))
        after = (len({p >> 30 for p in mapped | own}), len({p >> 21 for p in mapped | own}))
        adds = after[0] - before[0] + after[1] - before[1]
        placed = va == area + len(mapped) * PAGE and not (own & (mapped | vas))
        say("desktop: window surface %s, %d pages at 0x%016x (%s), %d tables modelled, %d logged" % (
            name, n, va, "right after the area's pages" if placed else "NOT right after the area's pages",
            adds, tables))
        ok = ok and placed and adds == tables
        mapped |= own
        names.append(name)
    vas |= mapped
    tables = len({va >> 39 for va in vas}) + len({va >> 30 for va in vas}) + len({va >> 21 for va in vas})
    logged = (int(walk.group(1)), int(walk.group(2)))
    ok = ok and (tables, len(vas)) == logged
    print("PAGETABLES_SURFACES label=%s cache=%s surfaces=%s computed=%d/%d logged=%d/%d %s" % (
        label, "present" if cache else "absent", ",".join(names) if names else "none", tables, len(vas),
        logged[0], logged[1], "MATCH" if ok else "DIFFER"))


if __name__ == "__main__":
    sys.exit(main())
