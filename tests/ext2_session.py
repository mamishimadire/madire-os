#!/usr/bin/env python3
"""1.4 stage 8: a console session on the ext2 volume mounted read-only at
/home, compared with debugfs's reading of the same filesystem image (part
static; docs/plans/1.4.md, S8, D16, G1).

Usage: ext2_session.py <serial log> <filesystem image>

The debugfs program is $DEBUGFS (tests/disk-lib.sh's disk_tool debugfs),
run read only (-R, no -w). The session (tests/qemu-part-static.sh) starts
in /home and types, in this order: pwd; ls -l; ls -l docs; ls -l
/home/docs/nested; cat notes.txt; cat docs/a.txt; cat big.txt; stat
notes.txt; stat big.txt; stat docs; stat /home; du; du docs; the cd and
pwd steps; the write attempts; ls -l . (the same listing again). One line
per check, "CHECK <name> OK" or "CHECK <name> BAD <what differs>", and
the details above them. Facts only (debugfs is an outside program, run as
a tool; its source is never read).
"""

import hashlib
import os
import re
import subprocess
import sys

OTHER = re.compile(r"^(console: |\[pid \d+\] |\[process \d+ |uptime: \d+ s$|key: )")
ROW = re.compile(r"^([d-]) +(\d+) \d{4}-\d{2}-\d{2} \d{2}:\d{2} (.+)$")
# 1.4 stage 12 (changed, the owner's approval): du on the disk counts the
# KiB of the blocks the files really use ("<n> KiB on the disk in ...").
DU = re.compile(r"^(\d+) KiB on the disk in (\d+) files? and (\d+) folders?$")
WRITE_LINES = [
    ("touch new.txt", "touch: 'new.txt': the filesystem is read-only."),
    ("touch notes.txt", "touch: 'notes.txt': the filesystem is read-only."),
    ("mkdir newdir", "mkdir: 'newdir': the filesystem is read-only."),
    ("mkdir docs", "mkdir: 'docs': already exists."),
    ("rmdir empty", "rmdir: 'empty': the filesystem is read-only."),
    ("rm -y notes.txt", "rm: 'notes.txt': the filesystem is read-only."),
    ("echo hi > out.txt", "Output to 'out.txt': the filesystem is read-only."),
    ("echo hi >> notes.txt", "Output to 'notes.txt': the filesystem is read-only."),
    ("cp -y notes.txt copy.txt", "cp: 'copy.txt': the filesystem is read-only."),
    ("mv -y notes.txt n2.txt", "mv: 'notes.txt' -> 'n2.txt': the filesystem is read-only."),
]


def dbg(img, req):
    env = dict(os.environ, LC_ALL="C")
    r = subprocess.run([os.environ["DEBUGFS"], "-R", req, img], stdin=subprocess.DEVNULL,
                       stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, env=env, check=False)
    return r.stdout


def ls_p(img, folder):
    """The folder's entries in on-disk order: (name, inode, mode, size)."""
    out = []
    for line in dbg(img, "ls -p " + folder).decode("latin-1").splitlines():
        f = line.split("/")
        if len(f) < 7 or f[1] in ("", "0") or f[5] in (".", ".."):
            continue
        out.append((f[5], int(f[1]), int(f[2], 8), int(f[6]) if f[6] else 0))
    return out


def is_dir(mode):
    return mode & 0o170000 == 0o040000


def stat(img, path):
    t = dbg(img, "stat " + path).decode("latin-1")
    links = re.search(r"Links:\s*(\d+)", t)
    count = re.search(r"Blockcount:\s*(\d+)", t)
    size = re.search(r"Size: (\d+)", t)
    ino = re.search(r"Inode: (\d+)", t)
    return {"links": int(links.group(1)) if links else -1, "blocks512": int(count.group(1)) if count else -1,
            "size": int(size.group(1)) if size else -1, "ino": int(ino.group(1)) if ino else -1}


def walk(img, folder):
    """Every path under folder (itself not counted): (files, folders, pages,
    KiB). 1.4 stage 12: KiB is the files' i_blocks / 2 (the KiB of the
    blocks they really use, indirect blocks included), as debugfs reads
    them; folders' own blocks are not counted (as Madire's du)."""
    files = folders = pages = kib = 0
    for name, _, mode, _ in ls_p(img, folder):
        p = (folder.rstrip("/") or "") + "/" + name
        if is_dir(mode):
            folders += 1
            f, d, g, k = walk(img, p)
            files, folders, pages, kib = files + f, folders + d, pages + g, kib + k
        else:
            files += 1
            b = stat(img, p)["blocks512"]
            pages += (b + 7) // 8
            kib += b // 2
    return files, folders, pages, kib


def block(log, command):
    """The lines a command printed: after "madire> <command>" up to the
    next prompt, without other threads' and the console's own lines; and
    the console's lines (for cat's end line)."""
    try:
        i = log.index("madire> " + command)
    except ValueError:
        return None, []
    got, own = [], []
    for line in log[i + 1:]:
        if line.startswith("madire> "):
            break
        if OTHER.match(line):
            own.append(line)
            continue
        got.append(line)
    return got, own


def main():
    log = open(sys.argv[1], encoding="latin-1").read().split("\n")
    img = sys.argv[2]
    checks = []

    def check(name, ok, what):
        checks.append("CHECK %s %s" % (name, "OK" if ok else "BAD " + what))

    # ls -l of three folders: the on-disk order, types and sizes.
    bad = []
    for command, folder in (("ls -l", "/"), ("ls -l docs", "/docs"), ("ls -l /home/docs/nested", "/docs/nested")):
        got, _ = block(log, command)
        want = ["%s %d %s" % ("d" if is_dir(m) else "-", 0 if is_dir(m) else s, n) for n, _, m, s in ls_p(img, folder)]
        rows = []
        for line in got or []:
            m = ROW.match(line)
            rows.append("%s %s %s" % (m.group(1), m.group(2), m.group(3).rstrip("/")) if m else "?" + line)
        print("LS %s: %d rows: %s" % (folder, len(rows), " | ".join(rows)))
        if rows != want or not want:
            bad.append("%s: got %s, debugfs %s" % (folder, rows, want))
    check("ls", not bad, "; ".join(bad))

    # cat: the printed text is the file (lines under 79 columns, ASCII).
    bad = []
    for command, path in (("cat notes.txt", "/notes.txt"), ("cat docs/a.txt", "/docs/a.txt"), ("cat big.txt", "/big.txt")):
        got, own = block(log, command)
        data = dbg(img, "cat " + path)
        text = ("\n".join(got or []) + "\n").encode("latin-1") if got else b""
        end = [l for l in own if l.startswith("console: cat: ") and " bytes in " in l]
        sha_g, sha_w = hashlib.sha256(text).hexdigest(), hashlib.sha256(data).hexdigest()
        print("CAT %s: %d bytes printed, sha256 %s; debugfs %d bytes, sha256 %s; %s" % (
            path, len(text), sha_g[:16], len(data), sha_w[:16], end[0] if end else "no end line"))
        if sha_g != sha_w or not end or not end[0].startswith("console: cat: %d bytes in " % len(data)) or not data:
            bad.append(path)
    check("cat", not bad, " ".join(bad))

    # stat: size, pages, node and links.
    bad = []
    for command, path in (("stat notes.txt", "/notes.txt"), ("stat big.txt", "/big.txt"), ("stat docs", "/docs"),
                          ("stat /home", "/")):
        got, _ = block(log, command)
        s = stat(img, path)
        d = "Type:   folder" in (got or [])
        want = ["Size:   %d byte%s" % (0 if d else s["size"], "" if not d and s["size"] == 1 else "s"),
                "Pages:  %d (4 KiB each, the index page included)" % (0 if d else (s["blocks512"] + 7) // 8),
                "Node:   %d" % s["ino"], "Links:  %d" % (s["links"] if d else 1)]
        rows = [l for l in got or [] if l.split(":")[0] in ("Size", "Pages", "Node", "Links")]
        print("STAT %s: %s (debugfs: size %d, blocks %d, inode %d, links %d)" % (
            path, " | ".join(rows), s["size"], s["blocks512"], s["ino"], s["links"]))
        if rows != want:
            bad.append("%s: got %s, want %s" % (path, rows, want))
    check("stat", not bad, "; ".join(bad))

    # du: KiB (1.4 stage 12; pages until then), files and folders under /
    # and /docs, each from debugfs.
    bad = []
    for command, folder in (("du", "/"), ("du docs", "/docs")):
        got, _ = block(log, command)
        f, d, p, k = walk(img, folder)
        m = DU.match(got[0]) if got else None
        print("DU %s: %s (debugfs: %d KiB in the files' blocks, %d files, %d folders)" % (
            folder, got[0] if got else "none", k, f, d))
        if not m or (int(m.group(1)), int(m.group(2)), int(m.group(3))) != (k, f, d):
            bad.append(folder)
    check("du", not bad, " ".join(bad))

    # cd and pwd across the mount.
    steps = [("pwd", "/home"), ("pwd ", "/home/docs"), ("pwd  ", "/home"), ("pwd   ", "/"), ("pwd    ", "/home")]
    got = [(block(log, c)[0] or ["none"])[0] for c, _ in steps]
    print("PWD %s" % " | ".join(got))
    check("cd", got == [w for _, w in steps], "got %s" % got)

    # The writes: each refused, nothing changed (the listing again).
    bad = []
    for command, line in WRITE_LINES:
        got, _ = block(log, command)
        print("WRITE %s: %s" % (command, " | ".join(got or ["nothing"])))
        if got != [line]:
            bad.append(command)
    again, _ = block(log, "ls -l .")
    first, _ = block(log, "ls -l")
    if not again or again != first:
        bad.append("ls -l . differs from the first ls -l")
    check("writes", not bad, "; ".join(bad))

    for c in checks:
        print(c)


if __name__ == "__main__":
    main()
