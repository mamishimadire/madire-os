#!/usr/bin/env python3
"""Madire OS: debugfs's view of an ext2 image, and the test images of the ext2
read core (1.4 stage 6; docs/plans/1.4.md, S6).

debugfs (e2fsprogs, an unmodified outside program on the test machine, run
read only here; docs/references.md, "Test tools") is the independent reader
that tests/ext2dump.c, built from kernel/fs/ext2_core.c, is compared with.
Run it with PYTHONDONTWRITEBYTECODE=1 and python3 -I.

  ext2_view.py tree <dir> <kind>      a fixed sample tree (full, corpus, large,
                                      dirindex, xattr); every byte, mode and
                                      time fixed
  ext2_view.py view <fs image>        debugfs's view in ext2dump's format: the
                                      "EXT2 rev=..." line, then one "P <path>
                                      ino= mode= size= links= flags= acl= sha="
                                      line per path (the root "/" first)
  ext2_view.py blocks <fs image>      "BLOCKMAP <path> <ino> ind=<n> dind=<n>
                                      tind=<n>" per file (debugfs stat)
  ext2_view.py corrupt <fs image> <out dir>   the fixed set of corrupted copies
                                      (seeded): one line "CORRUPT <file> <area>
                                      <what>" per copy
  ext2_view.py set <fs image> <out> <offset> <bytes> <value> [or]
                                      a copy with one superblock field (at the
                                      partition's byte <offset>) set, or ORed

The view: the folders are walked from the root by debugfs's "ls -p" (inode
numbers, so names are never parsed as paths), "." and ".." and unused slots
left out; each inode's type, permission bits, size, link count, flags and
attribute block come from debugfs's "stat"; a regular file's SHA-256 from
"cat", a symbolic link's from its "Fast link dest" (or "cat" for a slow
link). The output is sorted (LC_ALL=C) by the caller.
"""
import hashlib
import os
import random
import re
import shutil
import struct
import subprocess
import sys
import tempfile

TIME = 1791763200       # tests/disk-lib.sh DISK_TIME
TYPES = {"regular": 0o100000, "directory": 0o040000, "symlink": 0o120000, "FIFO": 0o010000,
         "socket": 0o140000, "character special": 0o020000, "block special": 0o060000}


def debugfs_path():
    p = shutil.which("debugfs", path=os.environ.get("PATH", "") + ":/usr/sbin:/sbin")
    if not p:
        raise SystemExit("ext2_view.py: no debugfs")
    return p


def env():
    e = dict(os.environ)
    e.update(LC_ALL="C", DEBUGFS_PAGER="__none__", PAGER="__none__")
    return e


def debugfs(img, request):
    r = subprocess.run([debugfs_path(), "-R", request, img], stdout=subprocess.PIPE,
                       stderr=subprocess.DEVNULL, stdin=subprocess.DEVNULL, env=env())
    return r.stdout.decode("latin-1")


def debugfs_batch(img, requests):
    """Several requests in one debugfs run (a command file, -f)."""
    with tempfile.NamedTemporaryFile("w", suffix=".cmd", delete=False) as f:
        f.write("\n".join(requests) + "\n")
        name = f.name
    try:
        r = subprocess.run([debugfs_path(), "-f", name, img], stdout=subprocess.PIPE,
                           stderr=subprocess.DEVNULL, stdin=subprocess.DEVNULL, env=env())
    finally:
        os.unlink(name)
    return r.stdout.decode("latin-1")


def debugfs_sha(img, ino):
    h = hashlib.sha256()
    p = subprocess.Popen([debugfs_path(), "-R", "cat <%d>" % ino, img], stdout=subprocess.PIPE,
                         stderr=subprocess.DEVNULL, stdin=subprocess.DEVNULL, env=env())
    while True:
        b = p.stdout.read(1 << 20)
        if not b:
            break
        h.update(b)
    p.wait()
    return h.hexdigest()


def stats(img):
    s = debugfs(img, "stats")

    def field(name, default):
        m = re.search(r"^%s:\s*(\d+)" % re.escape(name), s, re.M)
        return int(m.group(1)) if m else default
    return s, {"rev": field("Filesystem revision #", -1), "block": field("Block size", -1),
               "inode": field("Inode size", 128), "blocks": field("Block count", -1),
               "inodes": field("Inode count", -1), "first_ino": field("First inode", 11),
               "reserved_gdt": field("Reserved GDT blocks", 0)}


def walk(img):
    """[(path, ino)] for every path, the root first, from ls -p."""
    out = [("/", 2)]
    todo = [("/", 2)]
    seen = {2}
    while todo:
        d, dino = todo.pop(0)
        for line in debugfs(img, "ls -p <%d>" % dino).splitlines():
            f = line.strip().split("/")
            if len(f) < 8 or f[0] != "" or not f[1].isdigit():
                continue
            ino, mode, name = int(f[1]), int(f[2], 8), f[5]
            if ino == 0 or name in (".", ".."):
                continue
            p = (d if d != "/" else "") + "/" + name
            out.append((p, ino))
            if mode & 0o170000 == 0o040000 and ino not in seen:
                seen.add(ino)
                todo.append((p, ino))
    return out


def stat_all(img, inos):
    """{ino: stat text} from one debugfs run."""
    text = debugfs_batch(img, ["stat <%d>" % i for i in sorted(set(inos))])
    res = {}
    for part in re.split(r"(?m)^(?=Inode: \d+)", text):
        m = re.match(r"Inode: (\d+)", part)
        if m:
            res[int(m.group(1))] = part
    return res


def parse_stat(t):
    m = re.search(r"Type: (.+?)\s+Mode:\s*(\d+)\s+Flags:\s*(0x[0-9a-fA-F]+)", t)
    links = re.search(r"Links:\s*(\d+)", t)
    size = re.search(r"\sSize:\s*(\d+)", t)
    acl = re.search(r"File ACL:\s*(\d+)", t)
    fast = re.search(r'Fast link dest: "(.*)"', t)
    if not (m and links and size and acl):
        return None
    return {"mode": TYPES.get(m.group(1), 0) | int(m.group(2), 8), "flags": int(m.group(3), 16),
            "links": int(links.group(1)), "size": int(size.group(1)), "acl": int(acl.group(1)),
            "fast": fast.group(1).encode("latin-1") if fast else None,
            "ind": [int(x) for x in re.findall(r"\(IND\):(\d+)", t)],
            "dind": [int(x) for x in re.findall(r"\(DIND\):(\d+)", t)],
            "tind": [int(x) for x in re.findall(r"\(TIND\):(\d+)", t)],
            "data": [(int(a), int(b or a)) for a, b in re.findall(r"\(\d+(?:-\d+)?\):(\d+)(?:-(\d+))?", t)]}


def cmd_view(img):
    _, st = stats(img)
    print("EXT2 rev=%d block=%d inode=%d blocks=%d inodes=%d first_ino=%d reserved_gdt=%d" % (
        st["rev"], st["block"], st["inode"], st["blocks"], st["inodes"], st["first_ino"], st["reserved_gdt"]))
    paths = walk(img)
    info = stat_all(img, [i for _, i in paths])
    for p, ino in paths:
        s = parse_stat(info.get(ino, ""))
        if s is None:
            print("P %s ino=%d STAT-UNREADABLE" % (p, ino))
            continue
        kind = s["mode"] & 0o170000
        sha = "-"
        if kind == 0o100000:
            sha = debugfs_sha(img, ino)
        elif kind == 0o120000:
            sha = hashlib.sha256(s["fast"]).hexdigest() if s["fast"] is not None else debugfs_sha(img, ino)
        print("P %s ino=%d mode=%06o size=%d links=%d flags=0x%x acl=%d sha=%s" % (
            p, ino, s["mode"], s["size"], s["links"], s["flags"], s["acl"], sha))


def cmd_blocks(img):
    paths = walk(img)
    info = stat_all(img, [i for _, i in paths])
    for p, ino in paths:
        s = parse_stat(info.get(ino, ""))
        if s:
            print("BLOCKMAP %s %d ind=%d dind=%d tind=%d" % (p, ino, len(s["ind"]), len(s["dind"]), len(s["tind"])))


# ---- the sample trees ----

def put(path, data, mode=0o644):
    with open(path, "wb") as f:
        f.write(data)
    os.chmod(path, mode)


def sparse(path, size, pieces):
    """A sparse file of `size` bytes with `pieces` [(offset, bytes)]."""
    with open(path, "wb") as f:
        f.truncate(size)
        for off, b in pieces:
            f.seek(off)
            f.write(b)
    os.chmod(path, 0o644)


def cmd_tree(top, kind):
    rnd = random.Random(1406)
    os.makedirs(top)
    j = lambda *a: os.path.join(top, *a)      # noqa: E731
    mib = 1 << 20
    if kind in ("full", "corpus"):
        put(j("hello.txt"), b"Hello from the Madire OS ext2 test image.\n")
        put(j("empty.dat"), b"")
        put(j("big.bin"), rnd.randbytes(300000))                    # 1 KiB blocks: double indirect
        sparse(j("sparse-tind.bin"), 70 * mib + 1024,                 # 1 KiB blocks: triple indirect
               [(0, b"head" * 256), (mib, b"mid." * 256), (70 * mib, b"tail" * 256)])
        os.makedirs(j("docs", "deep", "a", "b", "c"))
        put(j("docs", "deep", "a", "b", "c", "deep.txt"), b"five folders deep\n")
        n = 120 if kind == "full" else 80
        for i in range(n):
            put(j("docs", "a-longer-file-name-number-%03d.txt" % i), b"note %d\n" % i)
        os.symlink("docs/a-longer-file-name-number-001.txt", j("link-fast"))
        os.symlink("docs/deep/a/b/c/" + "../" * 4 + "deep/a/b/c/deep.txt" + "/." * 20, j("link-slow"))
        put(j("hard-1.txt"), b"one inode, two names\n")
        os.link(j("hard-1.txt"), j("hard-2.txt"))
    if kind == "full":
        put(j("twelve.bin"), rnd.randbytes(12 * 1024))              # 1 KiB blocks: exactly the direct ones
        put(j("thirteen.bin"), rnd.randbytes(13 * 1024))            # 1 KiB blocks: + the single indirect
        put(j("double.bin"), rnd.randbytes(5 * mib), 0o600)          # 4 KiB blocks: double indirect
        put(j("run.sh"), b"#!/bin/sh\necho madire\n", 0o755)
        put(j("n" * 251 + ".txt"), b"a 255-byte name\n")
        os.mkfifo(j("pipe"), 0o644)
        os.chmod(j("pipe"), 0o644)
        os.makedirs(j("empty-folder"))
    elif kind == "large":
        sparse(j("large.bin"), 2 * 1024 * mib + 1024, [(0, b"LARGE" * 200), (2 * 1024 * mib, b"END!" * 256)])
        put(j("small.txt"), b"beside a file above 2 GiB\n")
    elif kind == "dirindex":
        os.makedirs(j("many"))
        for i in range(400):
            put(j("many", "entry-%04d-with-a-long-name.txt" % i), b"%d\n" % i)
        put(j("top.txt"), b"top\n")
    elif kind == "xattr":
        os.makedirs(j("attr", "folder"))
        put(j("attr", "a.txt"), b"a file with an extended attribute\n")
        put(j("attr", "b.txt"), b"another\n")
        put(j("plain.txt"), b"no attribute\n")
    elif kind != "corpus" and kind != "full":
        raise SystemExit("ext2_view.py: unknown tree kind " + kind)
    for root, dirs, files in os.walk(top):
        for name in dirs + files:
            os.utime(os.path.join(root, name), (TIME, TIME), follow_symlinks=False)
    os.utime(top, (TIME, TIME))


# ---- changed copies ----

def cmd_set(img, out, off, size, value, how):
    data = bytearray(open(img, "rb").read())
    fmt = {1: "<B", 2: "<H", 4: "<I"}[size]
    old = struct.unpack_from(fmt, data, off)[0]
    new = old | value if how == "or" else value
    struct.pack_into(fmt, data, off, new)
    with open(out, "wb") as f:
        f.write(data)
    print("SET %s offset=%d bytes=%d 0x%x -> 0x%x" % (os.path.basename(out), off, size, old, new))


def cmd_corrupt(img, outdir):
    """The fixed set: bit flips (seeded) and chosen bad values in the
    superblock, the group descriptors, the inode table, folder blocks and
    indirect blocks."""
    base = open(img, "rb").read()
    rnd = random.Random(1406)
    sb = 1024
    bs = 1024 << struct.unpack_from("<I", base, sb + 24)[0]
    blocks = struct.unpack_from("<I", base, sb + 4)[0]
    isz = struct.unpack_from("<H", base, sb + 88)[0]
    fdb = struct.unpack_from("<I", base, sb + 20)[0]
    gdt = (fdb + 1) * bs
    itable = struct.unpack_from("<I", base, gdt + 8)[0] * bs
    paths = walk(img)
    info = {i: parse_stat(t) for i, t in stat_all(img, [i for _, i in paths]).items()}
    ino = {p: i for p, i in paths}
    dirblocks = sorted({b for p, i in paths if info[i] and info[i]["mode"] & 0o170000 == 0o040000
                        for a, z in info[i]["data"] for b in range(a, z + 1)})
    inds = sorted({b for i in info if info[i] for b in info[i]["ind"] + info[i]["dind"] + info[i]["tind"]})
    big, sp = info[ino["/big.bin"]], info[ino["/sparse-tind.bin"]]
    root_block = info[2]["data"][0][0] * bs
    os.makedirs(outdir, exist_ok=True)
    n = [0]

    def emit(area, what, changes):
        d = bytearray(base)
        for off, b in changes:
            d[off:off + len(b)] = b
        n[0] += 1
        name = "%03d-%s.img" % (n[0], area)
        with open(os.path.join(outdir, name), "wb") as f:
            f.write(d)
        print("CORRUPT %s %s %s" % (name, area, what))

    def flips(lo, hi, k):
        ch = []
        for _ in range(k):
            off = rnd.randrange(lo, hi)
            bit = rnd.randrange(8)
            cur = ch[-1][1][0] if ch and ch[-1][0] == off else base[off]
            ch.append((off, bytes((cur ^ (1 << bit),))))
        return ch

    u32 = lambda v: struct.pack("<I", v & 0xFFFFFFFF)     # noqa: E731
    u16 = lambda v: struct.pack("<H", v & 0xFFFF)         # noqa: E731

    def flip_set(area, lo, hi, count, most):
        for _ in range(count):
            k = rnd.randint(1, most)
            ch = flips(lo, hi, k)
            emit(area, "flips " + ",".join("%d" % o for o, _ in ch), ch)

    # The superblock (its first 208 bytes: every field the core reads).
    flip_set("superblock", sb, sb + 208, 12, 3)
    for what, off, val in (("log_block_size=3", 24, u32(3)), ("log_block_size=0xffffffff", 24, u32(-1)),
                           ("blocks_per_group=0", 32, u32(0)), ("inodes_per_group=0", 40, u32(0)),
                           ("inodes_per_group=0xffffffff", 40, u32(-1)), ("inodes_count=0xffffffff", 0, u32(-1)),
                           ("blocks_count=0xffffffff", 4, u32(-1)), ("first_data_block=7", 20, u32(7)),
                           ("rev_level=2", 76, u32(2)), ("inode_size=200", 88, u16(200)),
                           ("first_ino=0xffffffff", 84, u32(-1)), ("magic=0", 56, u16(0))):
        emit("superblock", what, [(sb + off, val)])
    # The group descriptors (group 0's).
    flip_set("groupdesc", gdt, gdt + 32, 10, 3)
    for what, off, val in (("inode_table=0xffffffff", 8, u32(-1)), ("inode_table=last block", 8, u32(blocks - 1)),
                           ("inode_table=0", 8, u32(0)), ("block_bitmap=0xffffffff", 0, u32(-1))):
        emit("groupdesc", what, [(gdt + off, val)])
    # The inode table: the first 32 inodes.
    flip_set("inodes", itable, itable + 32 * isz, 16, 4)
    ri = itable + (2 - 1) * isz
    bi = itable + (ino["/big.bin"] - 1) * isz
    si = itable + (ino["/sparse-tind.bin"] - 1) * isz
    di = itable + (ino["/docs"] - 1) * isz
    for what, ch in (("root mode=regular", [(ri, u16(0o100644))]),
                     ("root size=0xffffffff", [(ri + 4, u32(-1))]),
                     ("root size=3 blocks (holes)", [(ri + 4, u32(3 * bs))]),
                     ("root block[0]=0xffffffff", [(ri + 40, u32(-1))]),
                     ("big.bin IND=0xffffffff", [(bi + 40 + 4 * 12, u32(-1))]),
                     ("big.bin DIND=block count", [(bi + 40 + 4 * 13, u32(blocks))]),
                     ("big.bin size_high=0x7fffffff", [(bi + 108, u32(0x7FFFFFFF))]),
                     ("sparse TIND=1 (the superblock's block)", [(si + 40 + 4 * 14, u32(1))]),
                     ("docs mode=fast link", [(di, u16(0o120777)), (di + 4, u32(10)), (di + 28, u32(0))]),
                     ("docs mode=0 links=0", [(di, u16(0)), (di + 26, u16(0))])):
        emit("inodes", what, ch)
    # Folder blocks.
    for _ in range(16):
        b = rnd.choice(dirblocks) * bs
        ch = flips(b, b + bs, rnd.randint(1, 3))
        emit("folder", "flips " + ",".join("%d" % o for o, _ in ch), ch)
    sub = None
    off = 0
    while off + 8 <= bs:            # the root's first entry naming a folder (not . or ..)
        e_ino, rec, nl, ft = struct.unpack_from("<IHBB", base, root_block + off)
        if rec < 12:
            break
        name = base[root_block + off + 8:root_block + off + 8 + nl]
        if e_ino and ft == 2 and name not in (b".", b".."):
            sub = off
            break
        off += rec
    for what, ch in (("first rec_len=0", [(root_block + 4, u16(0))]),
                     ("first rec_len=13", [(root_block + 4, u16(13))]),
                     ("first rec_len=0xfffc", [(root_block + 4, u16(0xFFFC))]),
                     ("second name_len=255", [(root_block + 12 + 6, bytes((255,)))]),
                     ("third inode=0xffffffff", [(root_block + 24, u32(-1))]),
                     ("third name has '/'", [(root_block + 24 + 8, b"/")]),
                     ("a folder entry names the root (a loop)", [(root_block + (sub or 0), u32(2))]),
                     ("the block zeroed", [(root_block, bytes(bs))])):
        emit("folder", what, ch)
    # Indirect blocks.
    for _ in range(12):
        b = rnd.choice(inds) * bs
        ch = flips(b, b + 64, rnd.randint(1, 4))
        emit("indirect", "flips " + ",".join("%d" % o for o, _ in ch), ch)
    for what, ch in (("big.bin IND[0]=0xffffffff", [(big["ind"][0] * bs, u32(-1))]),
                     ("big.bin DIND[0]=the DIND itself", [(big["dind"][0] * bs, u32(big["dind"][0]))]),
                     ("sparse TIND[0]=block count", [(sp["tind"][0] * bs, u32(blocks))]),
                     ("big.bin IND all 0xff", [(big["ind"][0] * bs, b"\xff" * bs)])):
        emit("indirect", what, ch)


def main():
    a = sys.argv[1:]
    if len(a) == 3 and a[0] == "tree":
        cmd_tree(a[1], a[2])
    elif len(a) == 2 and a[0] == "view":
        cmd_view(a[1])
    elif len(a) == 2 and a[0] == "blocks":
        cmd_blocks(a[1])
    elif len(a) == 3 and a[0] == "corrupt":
        cmd_corrupt(a[1], a[2])
    elif len(a) in (6, 7) and a[0] == "set":
        cmd_set(a[1], a[2], int(a[3], 0), int(a[4], 0), int(a[5], 0), a[6] if len(a) == 7 else "set")
    else:
        raise SystemExit(__doc__)


if __name__ == "__main__":
    main()
