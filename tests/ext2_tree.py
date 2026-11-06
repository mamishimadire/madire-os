#!/usr/bin/env python3
"""1.4 stage 10: the write session's checks (part static; docs/plans/1.4.md,
S10, C5, D14). Run with python3 -I -B.

  ext2_tree.py compare <fs image> <expected dir>
      debugfs's view of the image (tests/ext2_view.py view: every path with
      its mode, size and the SHA-256 of a file's bytes) against a tree on
      the host: the same paths (lost+found aside), each a folder or a file
      on both sides, every file the same size and SHA-256.
      "TREE ok <n> paths" or "TREE BAD <what differs>".
  ext2_tree.py session <serial log> <expected dir>
      the persistence boot's console session: each "ls -l <folder>" lists
      the expected folder's entries (names, types, sizes; ext2 lists in its
      own order, so as sets) and each "cat <path>" prints the expected
      file exactly (its SHA-256, and the console's "cat: N bytes" line).
      "SESSION ok <n> commands" or "SESSION BAD <what differs>".

Facts only: debugfs is an outside program, run read only through
tests/ext2_view.py.
"""
import hashlib
import os
import re
import subprocess
import sys

OTHER = re.compile(r"^(console: |\[pid \d+\] |\[process \d+ |uptime: \d+ s$|key: |ext2: |blk: |ahci: )")
ROW = re.compile(r"^([d-]) +(\d+) \d{4}-\d{2}-\d{2} \d{2}:\d{2} (.+)$")


def sha(data):
    return hashlib.sha256(data).hexdigest()


def image_view(img):
    here = os.path.dirname(os.path.abspath(__file__))
    r = subprocess.run([sys.executable, "-I", "-B", os.path.join(here, "ext2_view.py"), "view", img],
                       stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, stdin=subprocess.DEVNULL)
    out = {}
    for line in r.stdout.decode("latin-1").splitlines():
        f = line.split()
        if not f or f[0] != "P":
            continue
        d = dict(x.split("=", 1) for x in f[2:] if "=" in x)
        if "mode" not in d:
            out[f[1]] = ("?", "?", "?")
            continue
        kind = "d" if int(d["mode"], 8) & 0o170000 == 0o040000 else "f"
        out[f[1]] = (kind, d["size"] if kind == "f" else "-", d["sha"] if kind == "f" else "-")
    return out


def host_view(top):
    out = {}
    for base, dirs, files in os.walk(top):
        rel = os.path.relpath(base, top)
        pre = "" if rel == "." else "/" + rel.replace(os.sep, "/")
        for d in dirs:
            out[pre + "/" + d] = ("d", "-", "-")
        for f in files:
            with open(os.path.join(base, f), "rb") as fh:
                data = fh.read()
            out[pre + "/" + f] = ("f", str(len(data)), sha(data))
    return out


def compare(img, top):
    got = {p: v for p, v in image_view(img).items() if p not in ("/", "/lost+found")
           and not p.startswith("/lost+found/")}
    want = host_view(top)
    only_img = sorted(set(got) - set(want))
    only_host = sorted(set(want) - set(got))
    differ = sorted(p for p in set(got) & set(want) if got[p] != want[p])
    for p in sorted(want):
        print("  want %s %s" % (p, " ".join(want[p][:2])))
    if not got or only_img or only_host or differ:
        print("TREE BAD on the disk only %s; expected only %s; differ %s" % (
            only_img[:8], only_host[:8], ["%s disk %s want %s" % (p, got[p], want[p]) for p in differ[:8]]))
        return 1
    print("TREE ok %d paths" % len(got))
    return 0


def block(log, command):
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


def session(path, top):
    log = open(path, encoding="latin-1").read().split("\n")
    want = host_view(top)
    bad = []
    n = 0
    for line in log:
        if not line.startswith("madire> "):
            continue
        cmd = line[len("madire> "):]
        got, own = block(log, cmd)
        if cmd.startswith("ls -l"):
            n += 1
            folder = cmd[5:].strip()
            pre = "" if folder in ("", ".") else "/" + folder.strip("/")
            exp = set()
            for p, (kind, size, _) in want.items():
                if p.rsplit("/", 1)[0] == pre:
                    exp.add("%s %s %s" % (kind if kind == "d" else "-", "0" if kind == "d" else size,
                                          p.rsplit("/", 1)[1]))
            rows = set()
            for r in got or []:
                m = ROW.match(r)
                if m:
                    name = m.group(3).rstrip("/")
                    if name != "lost+found":
                        rows.add("%s %s %s" % (m.group(1), m.group(2), name))
                else:
                    rows.add("?" + r)
            print("LS %s: %d rows" % (folder or ".", len(rows)))
            if rows != exp:
                bad.append("ls %s: got %s want %s" % (folder, sorted(rows), sorted(exp)))
        elif cmd.startswith("cat "):
            n += 1
            p = "/" + cmd[4:].strip()
            text = ("\n".join(got or []) + "\n").encode("latin-1") if got else b""
            end = [l for l in own if l.startswith("console: cat: ") and " bytes in " in l]
            w = want.get(p)
            print("CAT %s: %d bytes printed (%s); want %s" % (p, len(text), end[0] if end else "no end line",
                                                            w[1] if w else "none"))
            if not w or sha(text) != w[2] or not end or not end[0].startswith("console: cat: %s bytes in " % w[1]):
                bad.append("cat " + p)
    if bad or n == 0:
        print("SESSION BAD %s" % ("; ".join(bad) if bad else "no ls or cat command found"))
        return 1
    print("SESSION ok %d commands" % n)
    return 0


def main():
    if len(sys.argv) == 4 and sys.argv[1] == "compare":
        return compare(sys.argv[2], sys.argv[3])
    if len(sys.argv) == 4 and sys.argv[1] == "session":
        return session(sys.argv[2], sys.argv[3])
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main())
