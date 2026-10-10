#!/usr/bin/env python3
"""Malformed ELF fixtures for Madire OS 0.11 (standard library only).

    python3 tests/elf-fixtures.py <good.elf> <output dir>

Starts from a real built program (build/user/hello.elf: a static x86-64
ET_EXEC with a read+execute PT_LOAD, a read+write PT_LOAD and PT_GNU_STACK)
and writes one file per broken rule, plus an unchanged copy, into the output
directory, with manifest.txt: one line per file, "<name>\t<expected result>",
the result being exactly what the kernel's elf_core prints (elf_result_str).
File names are valid module names ([a-z0-9_], at most 15 characters), so
later stages can load them as boot modules. The output depends only on the
input file.
"""
import os
import struct
import sys

PT_LOAD, PT_DYNAMIC, PT_INTERP, PT_TLS, PT_GNU_STACK = 1, 2, 3, 7, 0x6474E551
PF_X, PF_W, PF_R = 1, 2, 4
MIB = 1024 * 1024


def phdr_offsets(img):
    phoff = struct.unpack_from("<Q", img, 32)[0]
    phnum = struct.unpack_from("<H", img, 56)[0]
    return [phoff + 56 * i for i in range(phnum)]


def find(img, ptype, flags_set=0, flags_clear=0):
    for at in phdr_offsets(img):
        t, f = struct.unpack_from("<II", img, at)
        if t == ptype and (f & flags_set) == flags_set and not (f & flags_clear):
            return at
    sys.exit("elf-fixtures.py: the input does not have the expected program headers")


def put(img, at, fmt, value):
    struct.pack_into(fmt, img, at, value)
    return img


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    good = bytearray(open(sys.argv[1], "rb").read())
    out = sys.argv[2]
    os.makedirs(out, exist_ok=True)

    text = find(good, PT_LOAD, PF_X)
    data = find(good, PT_LOAD, PF_W)
    stack = find(good, PT_GNU_STACK)
    text_off, text_va, text_filesz = (struct.unpack_from("<Q", good, text + o)[0] for o in (8, 16, 32))
    entry = struct.unpack_from("<Q", good, 24)[0]

    def mod(fn):
        img = bytearray(good)
        return fn(img) or img

    fixtures = [
        ("good", bytearray(good), "OK"),
        ("empty", bytearray(), "empty file"),
        ("short", bytearray(good[:40]), "shorter than an ELF header"),
        ("badmagic", mod(lambda i: put(i, 1, "<B", ord("X"))), "not an ELF file"),
        ("class32", mod(lambda i: put(i, 4, "<B", 1)), "not a 64-bit ELF file"),
        ("bigendian", mod(lambda i: put(i, 5, "<B", 2)), "not little-endian"),
        ("osabi", mod(lambda i: put(i, 7, "<B", 3)), "not the System V ABI"),
        ("etdyn", mod(lambda i: put(i, 16, "<H", 3)), "not a static executable (ET_EXEC)"),
        ("etrel", mod(lambda i: put(i, 16, "<H", 1)), "not a static executable (ET_EXEC)"),
        ("i386", mod(lambda i: put(i, 18, "<H", 3)), "not an x86-64 program"),
        ("phentsize", mod(lambda i: put(i, 54, "<H", 32)), "wrong header or program-header size"),
        ("phnum0", mod(lambda i: put(i, 56, "<H", 0)), "no program header, or more than 16"),
        ("phnum17", mod(lambda i: put(i, 56, "<H", 17)), "no program header, or more than 16"),
        ("phoffpast", mod(lambda i: put(i, 32, "<Q", len(good) - 8)), "program headers outside the file"),
        ("truncated", bytearray(good[:text_off + text_filesz - 1]), "segment outside the file"),
        ("interp", mod(lambda i: put(i, stack, "<I", PT_INTERP)), "unsupported segment type"),
        ("dynamic", mod(lambda i: put(i, stack, "<I", PT_DYNAMIC)), "unsupported segment type"),
        ("tls", mod(lambda i: put(i, stack, "<I", PT_TLS)), "unsupported segment type"),
        ("execstack", mod(lambda i: put(i, stack + 4, "<I", PF_R | PF_W | PF_X)), "asks for an executable stack"),
        ("wxtext", mod(lambda i: put(i, text + 4, "<I", PF_R | PF_W | PF_X)), "segment both writable and executable"),
        ("noread", mod(lambda i: put(i, text + 4, "<I", PF_X)), "segment not readable, or unknown flags"),
        ("sizes", mod(lambda i: put(i, text + 40, "<Q", text_filesz - 1)),
         "segment empty, or file size above memory size"),
        ("align", mod(lambda i: put(i, text + 48, "<Q", 0x200000)), "segment not page-aligned"),
        ("lowaddr", mod(lambda i: put(put(i, text + 16, "<Q", text_va - 0x100000), 24, "<Q", entry - 0x100000)),
         "segment outside the program area"),
        ("kerneladdr", mod(lambda i: put(i, data + 16, "<Q", 0xFFFFFFFF80000000)), "segment outside the program area"),
        ("overlap", mod(lambda i: put(i, data + 16, "<Q", text_va)),
         "segments out of order, overlapping or sharing a page"),
        ("huge", mod(lambda i: put(i, data + 40, "<Q", 2 * MIB)), "more than 1 MiB of memory"),
        ("nocode", mod(lambda i: put(i, text + 4, "<I", PF_R)), "no executable segment"),
        ("entrydata", mod(lambda i: put(i, 24, "<Q", 0x500000)), "entry point outside the code"),
        ("toobig", bytearray(good) + bytes(MIB + 1 - len(good)), "file larger than 1 MiB"),
    ]

    with open(os.path.join(out, "manifest.txt"), "w", newline="\n") as m:
        for name, img, expected in fixtures:
            assert len(name) <= 15 and name.replace("_", "").isalnum() and name.islower()
            with open(os.path.join(out, name + ".elf"), "wb") as f:
                f.write(img)
            m.write(f"{name}\t{expected}\n")
    print(f"elf-fixtures.py: {len(fixtures)} files in {out}")


if __name__ == "__main__":
    main()
