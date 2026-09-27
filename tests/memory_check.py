#!/usr/bin/env python3
"""Independent check of the kernel's memory accounting.

Reads a serial log, takes the memory map and reserved ranges the kernel
logged, recomputes usable / free / reserved pages and RAM totals with separate
code (Python, per-page), and compares them with the kernel's own "memory:"
summary line. Optional pinned expectations catch a map that was misparsed.

    python3 tests/memory_check.py LOG [--usable-pages N] [--above4g-mb N]
                                      [--ram-mb N] [--expect-module]

Prints PASS:/FAIL: lines; exit status 1 if anything failed.
"""
import argparse
import re
import sys

PAGE = 4096
LIMIT = 1 << 32
RAM_TYPES = (1, 3, 4, 5)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("log")
    ap.add_argument("--label", default="memory")
    ap.add_argument("--usable-pages", type=int)
    ap.add_argument("--above4g-mb", type=int)
    ap.add_argument("--ram-mb", type=int)
    ap.add_argument("--expect-module", action="store_true")
    args = ap.parse_args()

    text = open(args.log, errors="replace").read()
    failures = 0

    def result(ok, what):
        nonlocal failures
        print(("PASS: " if ok else "FAIL: ") + f"{args.label}: {what}")
        failures += not ok

    mmap = [(int(b, 16), int(e, 16), int(t)) for b, e, t in re.findall(
        r"^mmap: base=0x([0-9a-f]{16}) end=0x([0-9a-f]{16}) type=(\d+)", text, re.M)]
    res = [(int(s, 16), int(e, 16), w.strip()) for s, e, w in re.findall(
        r"^reserve: start=0x([0-9a-f]{8}) end=0x([0-9a-f]{16}) (.*)$", text, re.M)]
    m = re.search(r"^memory: (.*)$", text, re.M)
    if not mmap or not res or not m:
        result(False, "kernel logged its memory map, reservations and summary")
        return 1
    kernel = {k: int(v, 0) for k, v in re.findall(r"(\w+)=(0x[0-9a-f]+|\d+)", m.group(1))}

    # Per-page model of the allocator rules, written independently of pmm.c.
    alloc = bytearray(LIMIT // PAGE)
    for base, end, typ in mmap:                       # usable: whole pages inward
        if typ == 1 and base < LIMIT:
            first, last = -(-base // PAGE), min(end, LIMIT) // PAGE
            alloc[first:last] = b"\x01" * max(0, last - first)
    for base, end, typ in mmap:                       # other regions: outward
        if typ != 1 and base < LIMIT:
            first, last = base // PAGE, -(-min(end, LIMIT) // PAGE)
            alloc[first:last] = bytes(max(0, last - first))
    usable = alloc.count(1)
    for start, end, _ in res:                         # reservations: outward
        first, last = start // PAGE, -(-min(end, LIMIT) // PAGE)
        alloc[first:last] = bytes(max(0, last - first))
    alloc[0] = 0
    free = alloc.count(1)

    ram = sum(e - b for b, e, t in mmap if t in RAM_TYPES)
    above = sum(max(0, e - max(b, LIMIT)) for b, e, t in mmap if t in RAM_TYPES)

    result(kernel["usable_pages"] == usable, f"usable pages {kernel['usable_pages']} (model: {usable})")
    result(kernel["free_pages"] == free, f"free pages {kernel['free_pages']} (model: {free})")
    result(kernel["reserved_pages"] == usable - free, f"reserved pages {kernel['reserved_pages']}")
    result(kernel["ram_bytes"] == ram, f"physical RAM {ram} bytes")
    result(kernel["above4g_bytes"] == above, f"RAM above 4 GiB {above} bytes")
    result(kernel["usable_bytes"] == usable * PAGE, "usable bytes = usable pages x 4096")

    names = [w for _, _, w in res]
    for need in ("low memory", "kernel image", "multiboot info", "multiboot memory map"):
        result(any(n.startswith(need) for n in names), f"reserved: {need}")
    low = [r for r in res if r[2].startswith("low memory")]
    result(bool(low) and low[0][0] == 0 and low[0][1] == 0x100000, "low 1 MiB fully reserved")
    if args.expect_module:
        result(any(n == "boot module" for n in names), "boot module reserved")

    if args.usable_pages is not None:
        result(usable == args.usable_pages, f"usable pages match pinned value {args.usable_pages}")
    if args.above4g_mb is not None:
        result(above >> 20 == args.above4g_mb, f"RAM above 4 GiB = {args.above4g_mb} MB")
    if args.ram_mb is not None:
        result((ram + (1 << 19)) >> 20 == args.ram_mb, f"physical RAM = {args.ram_mb} MB")

    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
