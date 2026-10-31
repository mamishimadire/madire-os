#!/usr/bin/env python3
"""Madire OS test disks: a partition table and the test marker (1.4 stage 2).

Written from the specifications, facts only (docs/references.md, "Storage:
specifications and test tools"): the MBR partition table and GPT from UEFI
2.11 chapter 5, CRC32 as GPT uses it (the reflected polynomial 0xEDB88320,
initial value and final XOR 0xFFFFFFFF). The ext2 filesystem itself is made
by mke2fs (tests/disk-lib.sh); this helper only puts a filesystem image
into a partitioned disk image, writes the Madire test marker, reads them
back and extracts the partition again. Run it with PYTHONDONTWRITEBYTECODE=1
(the files under test stay unchanged).

  disk_image.py mbr <fs image> <disk image>      MBR, one partition (type 0x83)
  disk_image.py gpt <fs image> <disk image>      GPT, one partition (Linux data)
  disk_image.py verify <disk image>              the table and the marker, read back
  disk_image.py extract <disk image> <fs image>  the partition's bytes
  disk_image.py crc <file>...                    CRC32 of each file (own, zlib)
  disk_image.py tree <dir>                       the fixed sample files; prints their listing

The sample tree (for mke2fs -d) has a file in the root, an empty file, a
file of five 1 KiB blocks and one of 300,000 bytes (at 1 KiB blocks it
needs the double indirect block), in folders two deep; "tree" prints one
line per folder ("DIR <path>") and per file ("FILE <path> <size> <sha256>"),
the listing debugfs must give back.

The partition starts at LBA 2048 (1 MiB) and holds the filesystem image
exactly (its size must be a multiple of 512). Every byte the helper writes
is fixed (disk signature, GUIDs, names), so the same filesystem image gives
the same disk image every time.

The Madire test marker (docs/plans/1.4.md, C4, condition 2) is written in
the first 512 bytes of the partition, which ext2 never uses (its superblock
starts at byte 1024 of the partition). Little-endian; the rest of the
sector is zero:
  offset  0  16 bytes  "MADIRE-TEST-DISK" (ASCII, no terminator)
  offset 16  u32       format version (1)
  offset 20  u32       record length in bytes (48)
  offset 24  u64       the partition's first LBA (as in the partition table)
  offset 32  u64       the partition's length in sectors (as in the table)
  offset 40  u32       CRC32 of bytes 0-39
  offset 44  u32       zero
"""
import hashlib
import os
import struct
import sys
import uuid
import zlib

SECTOR = 512
START_LBA = 2048
GPT_TAIL = 2048                 # sectors after the partition on a GPT disk (holds the backup)
GPT_ENTRIES = 128
GPT_ENTRY_SIZE = 128
GPT_ENTRY_SECTORS = GPT_ENTRIES * GPT_ENTRY_SIZE // SECTOR      # 32
MARKER_MAGIC = b"MADIRE-TEST-DISK"
MARKER_VERSION = 1
MARKER_LEN = 48
MBR_DISK_SIGNATURE = 0x4D414452          # fixed, so the image is the same every time
MBR_TYPE_LINUX = 0x83
MBR_TYPE_PROTECTIVE = 0xEE
GPT_TYPE_LINUX_DATA = uuid.UUID("0FC63DAF-8483-4772-8E79-3D69D8477DE4")
GPT_DISK_GUID = uuid.UUID("4d616469-7265-4f53-8000-746573746469")       # fixed
GPT_PART_GUID = uuid.UUID("4d616469-7265-4f53-8001-746573747031")       # fixed
GPT_PART_NAME = "madire-test"


def crc32(data, crc=0):
    """CRC32 (reflected, polynomial 0xEDB88320), bit by bit, as specified."""
    crc ^= 0xFFFFFFFF
    for byte in data:
        crc ^= byte
        for _ in range(8):
            crc = (crc >> 1) ^ (0xEDB88320 if crc & 1 else 0)
    return crc ^ 0xFFFFFFFF


def marker(first_lba, sectors):
    head = MARKER_MAGIC + struct.pack("<IIQQ", MARKER_VERSION, MARKER_LEN, first_lba, sectors)
    rec = head + struct.pack("<II", crc32(head), 0)
    return rec + bytes(SECTOR - len(rec))


def chs(lba):
    """CHS bytes of an LBA for 255 heads and 63 sectors; beyond 1023 cylinders the maximum."""
    c, rest = divmod(lba, 255 * 63)
    h, s = divmod(rest, 63)
    if c > 1023:
        c, h, s = 1023, 254, 62
    return bytes((h, ((s + 1) & 0x3F) | ((c >> 2) & 0xC0), c & 0xFF))


def mbr_entry(status, ptype, first_lba, sectors):
    last = first_lba + sectors - 1
    return (bytes((status,)) + chs(first_lba) + bytes((ptype,)) + chs(last)
            + struct.pack("<II", first_lba, sectors))


def mbr_sector(entries):
    sec = bytearray(SECTOR)
    struct.pack_into("<I", sec, 440, MBR_DISK_SIGNATURE)
    for i, e in enumerate(entries):
        sec[446 + 16 * i:446 + 16 * (i + 1)] = e
    sec[510:512] = b"\x55\xaa"
    return bytes(sec)


def read_fs(path):
    with open(path, "rb") as f:
        fs = f.read()
    if not fs or len(fs) % SECTOR:
        raise SystemExit("disk_image.py: %s: size %d is not a positive multiple of %d" % (path, len(fs), SECTOR))
    return fs


def with_marker(fs, first_lba):
    return marker(first_lba, len(fs) // SECTOR) + fs[SECTOR:]


def make_mbr(fs_path, out):
    fs = read_fs(fs_path)
    n = len(fs) // SECTOR
    mbr = mbr_sector([mbr_entry(0x00, MBR_TYPE_LINUX, START_LBA, n)])
    with open(out, "wb") as f:
        f.write(mbr + bytes((START_LBA - 1) * SECTOR) + with_marker(fs, START_LBA))


def gpt_entry_array(first_lba, last_lba):
    name = GPT_PART_NAME.encode("utf-16-le")
    e = (GPT_TYPE_LINUX_DATA.bytes_le + GPT_PART_GUID.bytes_le
         + struct.pack("<QQQ", first_lba, last_lba, 0) + name + bytes(72 - len(name)))
    return e + bytes(GPT_ENTRIES * GPT_ENTRY_SIZE - len(e))


def gpt_header(my_lba, alt_lba, first_usable, last_usable, entries_lba, entries_crc):
    def pack(crc):
        return (b"EFI PART" + struct.pack("<IIII", 0x00010000, 92, crc, 0)
                + struct.pack("<QQQQ", my_lba, alt_lba, first_usable, last_usable)
                + GPT_DISK_GUID.bytes_le
                + struct.pack("<QIII", entries_lba, GPT_ENTRIES, GPT_ENTRY_SIZE, entries_crc))
    h = pack(crc32(pack(0)))
    return h + bytes(SECTOR - len(h))


def make_gpt(fs_path, out):
    fs = read_fs(fs_path)
    n = len(fs) // SECTOR
    total = START_LBA + n + GPT_TAIL
    last = total - 1
    first_usable = 2 + GPT_ENTRY_SECTORS
    last_usable = last - 1 - GPT_ENTRY_SECTORS
    array = gpt_entry_array(START_LBA, START_LBA + n - 1)
    acrc = crc32(array)
    pmbr = mbr_sector([mbr_entry(0x00, MBR_TYPE_PROTECTIVE, 1, min(total - 1, 0xFFFFFFFF))])
    primary = gpt_header(1, last, first_usable, last_usable, 2, acrc)
    backup = gpt_header(last, 1, first_usable, last_usable, last - GPT_ENTRY_SECTORS, acrc)
    gap1 = START_LBA - 2 - GPT_ENTRY_SECTORS
    gap2 = GPT_TAIL - 1 - GPT_ENTRY_SECTORS
    with open(out, "wb") as f:
        f.write(pmbr + primary + array + bytes(gap1 * SECTOR) + with_marker(fs, START_LBA)
                + bytes(gap2 * SECTOR) + array + backup)


class Bad(Exception):
    pass


def parse(disk):
    """The partition table and the marker, read back. Returns (lines, (first, sectors))."""
    lines = []
    if len(disk) < 2 * SECTOR or disk[510:512] != b"\x55\xaa":
        raise Bad("no MBR signature 0x55AA")
    e = disk[446:462]
    ptype = e[4]
    first, count = struct.unpack_from("<II", e, 8)
    if ptype == MBR_TYPE_LINUX:
        lines.append("TABLE mbr start=%d sectors=%d type=0x%02x signature=0x%08x"
                     % (first, count, ptype, struct.unpack_from("<I", disk, 440)[0]))
    elif ptype == MBR_TYPE_PROTECTIVE:
        first, count = parse_gpt(disk, lines)
    else:
        raise Bad("first MBR entry has type 0x%02x" % ptype)
    off = first * SECTOR
    rec = disk[off:off + MARKER_LEN]
    if len(rec) < MARKER_LEN or off + count * SECTOR > len(disk):
        raise Bad("the partition lies beyond the disk")
    ver, rlen, mfirst, mcount = struct.unpack_from("<IIQQ", rec, 16)
    stored = struct.unpack_from("<I", rec, 40)[0]
    own, zl = crc32(rec[:40]), zlib.crc32(rec[:40]) & 0xFFFFFFFF
    lines.append("MARKER magic=%s version=%d length=%d start=%d sectors=%d crc=0x%08x own=0x%08x zlib=0x%08x"
                 % (rec[:16].decode("ascii", "replace"), ver, rlen, mfirst, mcount, stored, own, zl))
    ok = (rec[:16] == MARKER_MAGIC and ver == MARKER_VERSION and rlen == MARKER_LEN
          and mfirst == first and mcount == count and stored == own == zl
          and not any(disk[off + 44:off + SECTOR]))
    lines.append("MARKER_RESULT %s" % ("ok" if ok else "BAD"))
    return lines, (first, count), ok


def parse_gpt(disk, lines):
    def header(lba):
        h = disk[lba * SECTOR:lba * SECTOR + 92]
        if len(h) < 92 or h[:8] != b"EFI PART":
            raise Bad("no GPT header at LBA %d" % lba)
        stored = struct.unpack_from("<I", h, 16)[0]
        zeroed = h[:16] + bytes(4) + h[20:]
        return h, stored, crc32(zeroed), zlib.crc32(zeroed) & 0xFFFFFFFF
    total = len(disk) // SECTOR
    h, hs, ho, hz = header(1)
    my, alt, fu, lu = struct.unpack_from("<QQQQ", h, 24)
    elba, ecount, esize, ecrc = struct.unpack_from("<QIII", h, 72)
    arr = disk[elba * SECTOR:elba * SECTOR + ecount * esize]
    ao, az = crc32(arr), zlib.crc32(arr) & 0xFFFFFFFF
    b, bs, bo, bz = header(alt)
    barr_lba = struct.unpack_from("<Q", b, 72)[0]
    barr = disk[barr_lba * SECTOR:barr_lba * SECTOR + ecount * esize]
    tguid = uuid.UUID(bytes_le=arr[:16])
    first, last = struct.unpack_from("<QQ", arr, 32)
    name = arr[56:128].decode("utf-16-le").rstrip("\x00")
    lines.append("TABLE gpt start=%d sectors=%d type=%s name=%s first_usable=%d last_usable=%d"
                 % (first, last - first + 1, str(tguid).upper(), name, fu, lu))
    lines.append("GPT header_crc=0x%08x own=0x%08x zlib=0x%08x entries_crc=0x%08x own=0x%08x zlib=0x%08x"
                 % (hs, ho, hz, ecrc, ao, az))
    lines.append("GPT backup at=%d header_crc=0x%08x own=0x%08x zlib=0x%08x entries_equal=%s"
                 % (alt, bs, bo, bz, "yes" if barr == arr else "no"))
    ok = (my == 1 and alt == total - 1 and hs == ho == hz and ecrc == ao == az and bs == bo == bz
          and barr == arr and esize == GPT_ENTRY_SIZE and tguid == GPT_TYPE_LINUX_DATA
          and fu <= first <= last <= lu)
    lines.append("GPT_RESULT %s" % ("ok" if ok else "BAD"))
    if not ok:
        raise Bad("GPT does not read back (see the lines above)")
    return first, last - first + 1


def sample_bytes(n, seed):
    """n bytes from a fixed linear congruential sequence (the same every time)."""
    out = bytearray(n)
    x = seed
    for i in range(n):
        x = (x * 1103515245 + 12345) & 0x7FFFFFFF
        out[i] = (x >> 16) & 0xFF
    return bytes(out)


def sample_tree(root):
    notes = b"".join(b"Madire test notes, line %04d.\n" % i for i in range(1, 168))[:5000]
    files = (("hello.txt", b"Madire OS test disk\n"),
             ("docs/notes.txt", notes),
             ("docs/empty.txt", b""),
             ("data/big.bin", sample_bytes(300000, 1)),
             ("data/deep/x.txt", b"two folders deep\n"))
    lines = []
    for d in ("docs", "data", "data/deep"):
        os.makedirs(os.path.join(root, d), exist_ok=True)
        lines.append("DIR /%s" % d)
    for name, data in files:
        with open(os.path.join(root, name), "wb") as f:
            f.write(data)
        lines.append("FILE /%s %d %s" % (name, len(data), hashlib.sha256(data).hexdigest()))
    return lines


def main(argv):
    if len(argv) == 3 and argv[1] == "tree":
        print("\n".join(sample_tree(argv[2])))
        return 0
    if len(argv) == 4 and argv[1] in ("mbr", "gpt"):
        (make_mbr if argv[1] == "mbr" else make_gpt)(argv[2], argv[3])
        return 0
    if len(argv) == 3 and argv[1] == "verify":
        with open(argv[2], "rb") as f:
            disk = f.read()
        try:
            lines, _, ok = parse(disk)
        except Bad as e:
            print("VERIFY_ERROR %s" % e)
            return 1
        print("\n".join(lines))
        return 0 if ok else 1
    if len(argv) == 4 and argv[1] == "extract":
        with open(argv[2], "rb") as f:
            disk = f.read()
        try:
            _, (first, count), _ = parse(disk)
        except Bad as e:
            print("VERIFY_ERROR %s" % e)
            return 1
        with open(argv[3], "wb") as f:
            f.write(disk[first * SECTOR:(first + count) * SECTOR])
        return 0
    if len(argv) >= 3 and argv[1] == "crc":
        for p in argv[2:]:
            with open(p, "rb") as f:
                d = f.read()
            print("CRC32 %s own=0x%08x zlib=0x%08x" % (p, crc32(d), zlib.crc32(d) & 0xFFFFFFFF))
        return 0
    sys.stderr.write(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
