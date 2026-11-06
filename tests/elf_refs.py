#!/usr/bin/env python3
"""1.4 stage 10 (the owner's path proof, indirect calls): where madire.elf
stores the address of a function as data. Run with python3 -I -B.

  elf_refs.py <elf> <function>...

For each function (its address from .symtab), every allocated, non-code
section (.rodata, .data and the like) is searched for the address as an
8-byte little-endian value; each hit is printed with the data symbol that
holds it:
  DATA <function> <section>+0x<offset> in <symbol or ?>
and at the end "FUNCS <n found of n asked>". A function missing from the
symbol table: "MISSING <function>". Facts from the ELF64 format only.
"""
import struct
import sys


def main():
    path, names = sys.argv[1], sys.argv[2:]
    data = open(path, "rb").read()
    if data[:4] != b"\x7fELF" or data[4] != 2:
        print("NOT-ELF64")
        return 2
    shoff, = struct.unpack_from("<Q", data, 0x28)
    shentsize, shnum, shstrndx = struct.unpack_from("<HHH", data, 0x3A)
    secs = []
    for i in range(shnum):
        name, typ, flags, addr, off, size, link, info, align, entsize = struct.unpack_from(
            "<IIQQQQIIQQ", data, shoff + i * shentsize)
        secs.append((name, typ, flags, addr, off, size, link, entsize))
    strtab = secs[shstrndx]

    def sname(i):
        o = strtab[4] + secs[i][0]
        return data[o:data.index(b"\0", o)].decode()

    syms = []
    for i, s in enumerate(secs):
        if s[1] != 2:                   # SHT_SYMTAB
            continue
        stro = secs[s[6]][4]
        for k in range(s[5] // 24):
            st_name, st_info, st_other, st_shndx, st_value, st_size = struct.unpack_from(
                "<IBBHQQ", data, s[4] + k * 24)
            n = data[stro + st_name:data.index(b"\0", stro + st_name)].decode()
            syms.append((n, st_info & 0xF, st_value, st_size))
    funcs = {}
    for n, t, v, z in syms:
        base = n.split(".")[0]
        if t == 2 and base in names and base not in funcs:  # STT_FUNC
            funcs[base] = v
    objs = [(v, z, n) for n, t, v, z in syms if t == 1 and z]  # STT_OBJECT
    found = 0
    for f in names:
        if f not in funcs:
            print("MISSING %s" % f)
            continue
        found += 1
        pat = struct.pack("<Q", funcs[f])
        for i, s in enumerate(secs):
            name, typ, flags, addr, off, size = s[:6]
            if not (flags & 0x2) or (flags & 0x4) or typ == 8:   # alloc, not exec, not NOBITS
                continue
            blob = data[off:off + size]
            j = blob.find(pat)
            while j >= 0:
                va = addr + j
                holder = next((n for v, z, n in objs if v <= va < v + z), "?")
                print("DATA %s %s+0x%x in %s" % (f, sname(i), j, holder))
                j = blob.find(pat, j + 1)
    print("FUNCS %d of %d" % (found, len(names)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
