#!/usr/bin/env python3
"""Checks a Multiboot 1 kernel image's header (0.13; Madire's own code).

Replaces `grub-file --is-x86-multiboot` in the tests, and is at least as
strict as it:
  - the header lies within the first 8192 bytes, 4-byte aligned;
  - the magic is 0x1BADB002 and magic + flags + checksum is 0 (mod 2^32);
  - no unknown required feature is asked for: flag bits 3-15 are refused
    (bits 0-2 are the defined requirements; 16 and up are optional);
  - with flag 16 (the a.out kludge) the five address fields are present and
    consistent: load_addr <= header_addr, the image starts at or after the
    file's first byte, load_end_addr (if set) is after load_addr and within
    the file, bss_end_addr (if set) is not before the end of the loaded
    data, the entry point lies inside the loaded image (bss included), and
    the image lies at or above 1 MiB and below 4 GiB;
  - without flag 16 the file must be an ELF file (what a loader then reads).

    python3 scripts/check-multiboot.py KERNEL [--mutate magic|checksum|aout-outside]

Prints one "MULTIBOOT OK: ..." or "MULTIBOOT BAD: <reason>" line and exits
with 0 or 1. --mutate checks a copy changed in memory (the file is never
written), so the tests can prove each refusal: a wrong magic, a wrong
checksum, or a.out fields that point outside the file.
"""
import argparse
import struct
import sys

MAGIC = 0x1BADB002
SEARCH = 8192
REQUIRED_UNKNOWN = 0x0000FFF8      # flag bits 3-15
AOUT_KLUDGE = 1 << 16
LOW_LIMIT = 0x100000               # 1 MiB
HIGH_LIMIT = 1 << 32


def find_header(data):
    """The first 4-byte aligned offset in the first 8192 bytes holding the
    magic (the loader takes the first; its checksum is checked after)."""
    for off in range(0, min(SEARCH, len(data) - 11), 4):
        if struct.unpack_from("<I", data, off)[0] == MAGIC:
            return off
    return None


def check(data):
    off = find_header(data)
    if off is None:
        return False, "no Multiboot magic (0x%08X) 4-byte aligned in the first %d bytes" % (MAGIC, SEARCH)
    magic, flags, checksum = struct.unpack_from("<III", data, off)
    if (magic + flags + checksum) & 0xFFFFFFFF:
        return False, "checksum 0x%08x at offset %d: magic + flags + checksum is not 0" % (checksum, off)
    if flags & REQUIRED_UNKNOWN:
        return False, "flags 0x%08x ask for unknown required features (bits 3-15: 0x%04x)" % (
            flags, flags & REQUIRED_UNKNOWN)
    if not flags & AOUT_KLUDGE:
        if data[:4] != b"\x7fELF":
            return False, "flags 0x%08x: no a.out kludge (bit 16), and the file is not ELF" % flags
        return True, "header at offset %d, flags 0x%08x, ELF image" % (off, flags)

    if off + 32 > len(data):
        return False, "a.out kludge fields past the end of the file"
    header_addr, load_addr, load_end_addr, bss_end_addr, entry_addr = struct.unpack_from("<5I", data, off + 12)
    if load_addr > header_addr:
        return False, "load_addr 0x%08x is after header_addr 0x%08x" % (load_addr, header_addr)
    start = off - (header_addr - load_addr)
    if start < 0:
        return False, "the image would start %d bytes before the file (header_addr - load_addr too large)" % -start
    if load_end_addr:
        if load_end_addr <= load_addr:
            return False, "load_end_addr 0x%08x is not after load_addr 0x%08x" % (load_end_addr, load_addr)
        load_size = load_end_addr - load_addr
        if start + load_size > len(data):
            return False, "load_end_addr 0x%08x is beyond the file (%d bytes needed from offset %d, file %d)" % (
                load_end_addr, load_size, start, len(data))
    else:
        load_size = len(data) - start
    loaded_end = load_addr + load_size
    if bss_end_addr and bss_end_addr < loaded_end:
        return False, "bss_end_addr 0x%08x is before the end of the loaded data 0x%08x" % (bss_end_addr, loaded_end)
    image_end = max(loaded_end, bss_end_addr)
    if not load_addr <= entry_addr < image_end:
        return False, "entry_addr 0x%08x is outside the image 0x%08x-0x%08x" % (entry_addr, load_addr, image_end)
    if load_addr < LOW_LIMIT or image_end > HIGH_LIMIT:
        return False, "the image 0x%08x-0x%08x is not within 1 MiB-4 GiB" % (load_addr, image_end)
    return True, ("header at offset %d, flags 0x%08x, a.out kludge: load 0x%08x-0x%08x, bss end 0x%08x, entry 0x%08x"
                  % (off, flags, load_addr, loaded_end, bss_end_addr, entry_addr))


def mutate(data, how):
    off = find_header(data)
    if off is None:
        return data
    d = bytearray(data)
    if how == "magic":
        struct.pack_into("<I", d, off, MAGIC ^ 0x00010000)           # 0x1BACB002
    elif how == "checksum":
        c = struct.unpack_from("<I", d, off + 8)[0]
        struct.pack_into("<I", d, off + 8, (c + 1) & 0xFFFFFFFF)
    elif how == "aout-outside":
        load_addr = struct.unpack_from("<I", d, off + 16)[0]
        struct.pack_into("<I", d, off + 20, (load_addr + len(d) + 0x1000) & 0xFFFFFFFF)  # load_end_addr
    return bytes(d)


def main():
    p = argparse.ArgumentParser()
    p.add_argument("kernel")
    p.add_argument("--mutate", choices=["magic", "checksum", "aout-outside"])
    a = p.parse_args()
    try:
        with open(a.kernel, "rb") as f:
            data = f.read()
    except OSError as e:
        print("MULTIBOOT BAD: %s" % e)
        return 1
    if a.mutate:
        data = mutate(data, a.mutate)
    ok, why = check(data)
    print("MULTIBOOT %s: %s" % ("OK" if ok else "BAD", why))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
