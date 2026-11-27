#!/usr/bin/env python3
"""The Files and Trash cores' reference model (1.5 stage 11; standard
library only).

Writes again, from the rules in kernel/files/files_core.h and
kernel/files/trash_core.h (docs/plans/1.5.md, C5, C6, C6c), what the pure
cores must answer, and checks the real cores (built into the host tool
tests/filesdump.c, once plainly and once with AddressSanitizer and
UndefinedBehaviorSanitizer) against it on fixed, generated inputs (a fixed
seed: every run sends the same inputs). It never calls or translates the
C cores; the fake filesystem (tests/files_fakefs.h) is modelled here.

    files_ref.py --static FILESDUMP FILESDUMP_ASAN [--break NAME]

prints one line per check, "FILESREF <name> PASS|FAIL <detail>", for the
names asan, trash, encode, sort, limit, pathbar, history, walk and clip.
--break NAME spoils that check's expectation on purpose (or, for asan, adds
a sanitizer line), to prove the check can fail (T2).
"""

import datetime
import os
import random
import subprocess
import sys

SEED = 20261120

# ---------------------------------------------------------------- Trash

INFO_MAX, LINE_MAX, PATH_MAX, NAME_MAX = 8192, 4096 + 64, 4096, 255
HOME, TRASH = b"/home/user/", b"/home/user/.local/share/Trash"
HEXD = b"0123456789abcdefABCDEF"
KEEP = set(b"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_.!~*'()/")


def month_days(y, m):
    if m == 2:
        return 29 if (y % 4 == 0 and y % 100 != 0) or y % 400 == 0 else 28
    return 30 if m in (4, 6, 9, 11) else 31


def date_valid(y, mo, d, h, mi, s):
    return 1 <= y <= 9999 and 1 <= mo <= 12 and 1 <= d <= month_days(y, mo) and h <= 23 and mi <= 59 and s <= 59


def date_parse(v):
    if len(v) != 19 or v[4:5] != b"-" or v[7:8] != b"-" or v[10:11] != b"T" or v[13:14] != b":" or v[16:17] != b":":
        return None
    parts = (v[0:4], v[5:7], v[8:10], v[11:13], v[14:16], v[17:19])
    if not all(all(0x30 <= c <= 0x39 for c in p) for p in parts):
        return None
    nums = [int(p) for p in parts]
    return v.decode() if date_valid(*nums) else None


def ti_decode(v):
    out = bytearray()
    i = 0
    while i < len(v):
        c = v[i]
        if c == 0x25:
            if len(v) - i < 3 or v[i + 1] not in HEXD or v[i + 2] not in HEXD:
                return "bad-escape", None
            c = int(v[i + 1:i + 3], 16)
            i += 2
            if c == 0:
                return "nul", None
            if c == 0x2F:
                return "escaped-slash", None
        elif c == 0:
            return "nul", None
        if len(out) == PATH_MAX:
            return "path-too-long", None
        out.append(c)
        i += 1
    return "ok", bytes(out)


def ti_check(p):
    if len(p) > PATH_MAX:
        return "path-too-long"
    if not p or p[0] != 0x2F:
        return "relative"
    if len(p) > 1 and p[-1] == 0x2F:
        return "trailing-slash"
    for comp in p[1:].split(b"/") if len(p) > 1 else []:
        if not comp:
            return "empty-component"
        if comp in (b".", b".."):
            return "dot-component"
        if len(comp) > NAME_MAX:
            return "component-too-long"
    if len(p) <= len(HOME) or not p.startswith(HOME):
        return "outside-home"
    if p == TRASH or p.startswith(TRASH + b"/"):
        return "inside-trash"
    return "ok"


def ti_parse(buf):
    """(reason, path or None, date text or None)."""
    if len(buf) > INFO_MAX:
        return "file-too-large", None, None
    header = bad = have_path = have_date = False
    date = None
    pval = b""
    i, n = 0, len(buf)
    while i < n:
        j = buf.find(b"\n", i)
        e = n if j < 0 else j
        line = buf[i:e]
        i = e + 1
        if len(line) > LINE_MAX:
            return "line-too-long", None, None
        if bad:
            continue
        if not header:
            if not line:
                continue
            if line == b"[Trash Info]":
                header = True
            else:
                bad = True
            continue
        if not have_path and line.startswith(b"Path="):
            have_path, pval = True, line[5:]
        elif not have_date and line.startswith(b"DeletionDate="):
            have_date, date = True, date_parse(line[13:])
    if not header:
        return "bad-header", None, None
    if not have_path:
        return "no-path", None, None
    r, p = ti_decode(pval)
    if r != "ok":
        return r, None, None
    r = ti_check(p)
    if r != "ok":
        return r, None, None
    return "ok", p, date


def ti_encode(p):
    return b"".join(bytes([c]) if c in KEEP else b"%%%02X" % c for c in p)


def ti_build(p, date):
    """The .trashinfo text, or None (trash_info_build's 0)."""
    if ti_check(p) != "ok" or 0 in p or date_parse(date.encode()) is None:
        return None
    enc = ti_encode(p)
    if 5 + len(enc) > LINE_MAX:
        return None
    return b"[Trash Info]\nPath=" + enc + b"\nDeletionDate=" + date.encode() + b"\n"


def ti_slot(name, n):
    if not 1 <= n <= 999 or not 1 <= len(name) <= NAME_MAX or b"/" in name or 0 in name:
        return None
    suffix = b"" if n == 1 else b".%d" % n
    room = NAME_MAX - 10 - len(suffix)
    return name[:room] + suffix


# ---------------------------------------------------------------- Files: folder, sort, texts

K_NONE, K_DIR, K_FILE, K_LINK, K_ERROR = 0, 1, 2, 3, 4


def fold(b):
    return bytes(c + 32 if 0x41 <= c <= 0x5A else c for c in b)


def sort_key(kind, name):
    return (0 if kind == K_DIR else 1, fold(name), name)


def folder_add(state, name, kind, cap=1000):
    """state: {"show": bool, "names": [...], "truncated": bool}; the code O, S, F or B."""
    if name in (b".", b".."):
        return "S"
    if not name or len(name) > NAME_MAX or kind not in (K_DIR, K_FILE, K_LINK):
        return "B"
    if b"/" in name or 0 in name:
        return "B"
    if name[0] == 0x2E and not state["show"]:
        return "S"
    if len(state["names"]) >= cap:
        state["truncated"] = True
        return "F"
    state["names"].append((kind, name))
    return "O"


TEXT_EXTS = {b"txt", b"md", b"log", b"conf", b"cfg", b"ini", b"csv", b"json", b"sh", b"c", b"h"}


def type_text(kind, name):
    if kind == K_DIR:
        return "Folder"
    if kind == K_LINK:
        return "Link"
    d = name.rfind(b".")
    if d >= 1 and fold(name[d + 1:]) in TEXT_EXTS:
        return "Text file"
    return "File"


def size_text(b):
    if b < 1024:
        return "%d B" % b
    unit, u = (1 << 30, "G") if b >= 1 << 30 else (1 << 20, "M") if b >= 1 << 20 else (1024, "K")
    return "%d.%d %siB" % (b // unit, (b % unit) * 10 // unit, u)


def usage_text(used, total):
    """1.5 stage 14b (This PC): "<U> used, <X> free of <Y>" (files_usage_text)."""
    return "%s used, %s free of %s" % (size_text(used), size_text(max(total - used, 0)), size_text(total))


def bar_fill(used, total, w):
    """1.5 stage 14b: files_bar_fill: the used pixels of a bar w long (both
    halved together while used x w does not fit 64 bits; at most w - 1
    below full)."""
    if w <= 0 or total == 0:
        return 0
    if used >= total:
        return w
    while used > (2 ** 64 - 1) // w:
        used, total = used >> 1, total >> 1
    return min(used * w // total, w - 1)


def date_text(s):
    s = max(0, min(s, 253402300799))
    t = datetime.datetime(1970, 1, 1) + datetime.timedelta(seconds=s)
    return "%04d-%02d-%02d %02d:%02d" % (t.year, t.month, t.day, t.hour, t.minute)


# ---------------------------------------------------------------- Files: history

V_FOLDER, V_TRASH, V_THISPC = 0, 1, 2


class History:
    def __init__(self):
        self.e, self.pos = [], 0

    def cur(self):
        return self.e[self.pos] if self.e else None

    def go(self, view, path):
        if view == V_FOLDER and (not path or len(path) > PATH_MAX):
            return False
        if view != V_FOLDER:
            path = b""
        if self.e:
            if self.e[self.pos] == (view, path):
                return True
            del self.e[self.pos + 1:]
            if len(self.e) == 64:
                del self.e[0]
        self.e.append((view, path))
        self.pos = len(self.e) - 1
        return True

    def can_back(self):
        return bool(self.e) and self.pos > 0

    def can_forward(self):
        return self.pos + 1 < len(self.e)

    def can_up(self):
        return bool(self.e) and self.e[self.pos][0] == V_FOLDER and self.e[self.pos][1] != b"/"

    def back(self, missing):
        removed = 0
        while self.e and self.pos > 0:
            self.pos -= 1
            v, p = self.e[self.pos]
            if v != V_FOLDER or p not in missing:
                return True, removed
            del self.e[self.pos]
            removed += 1
        return False, removed

    def forward(self, missing):
        removed = 0
        while self.pos + 1 < len(self.e):
            v, p = self.e[self.pos + 1]
            if v != V_FOLDER or p not in missing:
                self.pos += 1
                return True, removed
            del self.e[self.pos + 1]
            removed += 1
        return False, removed

    def up(self):
        if not self.can_up():
            return False
        p = self.e[self.pos][1]
        cut = p.rfind(b"/") + 1
        return self.go(V_FOLDER, p[:cut - 1] if cut > 1 else b"/")


# ---------------------------------------------------------------- Files: path bar

PB_MAX = 32


def pathbar(view, path, cells):
    """(n, clipped, used cells, [(text, x, target)])."""
    if view in (V_TRASH, V_THISPC):
        root = (b"Trash" if view == V_TRASH else b"This PC", 0)
        comps = []
    else:
        if path == b"/home/user" or path.startswith(b"/home/user/"):
            root, rest, base = (b"Home", 10), path[11:], 11
        else:
            root, rest, base = (b"/", 1), path[1:], 1
        comps, at = [], base
        if rest:
            for c in rest.split(b"/"):
                comps.append((c, at + len(c)))
                at += len(c) + 1
    parts = [root] + comps
    total = sum(len(t) for t, _ in parts) + 3 * (len(parts) - 1)
    if total <= cells and len(parts) <= PB_MAX:
        out, x = [], 0
        for i, (t, g) in enumerate(parts):
            out.append((t, x, g))
            x += len(t) + 3
        return len(parts), 0, total, out
    k, w = 0, 3
    while k + 1 < len(parts) and k < PB_MAX:
        t = parts[-1 - k][0]
        if w + 3 + len(t) > cells:
            break
        w += 3 + len(t)
        k += 1
    if k:
        out, x = [], 6
        for t, g in parts[len(parts) - k:]:
            out.append((t, x, g))
            x += len(t) + 3
        return k, 1, w, out
    if cells < 4:
        return 0, 1, 0, []
    t, g = parts[-1]
    shown = min(len(t), cells - 3)
    return 1, 1, 3 + shown, [(t[len(t) - shown:], 3, g)]


# ---------------------------------------------------------------- Files: names

def name_variant(name, folder, restored, n):
    if not 1 <= len(name) <= NAME_MAX or not 1 <= n <= 999:
        return None
    if not restored and n == 1:
        return name
    base = len(name)
    if not folder:
        d = name.rfind(b".")
        if d >= 1:
            base = d
    ext = name[base:]
    if restored:
        suffix = b" (restored)" if n == 1 else b" (restored %d)" % n
    else:
        suffix = b" %d" % n
    if len(suffix) + len(ext) + 1 > NAME_MAX:
        return None
    b = min(base, NAME_MAX - len(suffix) - len(ext))
    return name[:b] + suffix + ext


def unique_name(name, folder, restored, taken):
    for n in range(1, 1000):
        v = name_variant(name, folder, restored, n)
        if v is None:
            return None
        if v not in taken:
            return v
    return None


# ---------------------------------------------------------------- Files: the fake filesystem and the walk

ENOENT, ENOMEM, EEXIST, ENOTDIR, EISDIR, ENOTEMPTY = -2, -12, -17, -20, -21, -39
FFS_NODES, CHUNK = 6000, 4096


class FakeFS:
    """tests/files_fakefs.h, written again."""

    def __init__(self):
        self.nodes = [{"used": True, "parent": -1, "kind": K_DIR, "name": b"", "size": 0, "seed": 0}]
        self.fail_at, self.calls = 0, 0

    def add(self, parent, kind, name, size, seed):
        if len(self.nodes) >= FFS_NODES or len(name) > NAME_MAX:
            return -1
        self.nodes.append({"used": True, "parent": parent, "kind": kind, "name": name, "size": size, "seed": seed})
        return len(self.nodes) - 1

    def child(self, d, name):
        for i in range(1, len(self.nodes)):
            n = self.nodes[i]
            if n["used"] and n["parent"] == d and n["name"] == name:
                return i
        return -1

    def lookup(self, path):
        """(node or error, parent)."""
        if not path.startswith(b"/"):
            return ENOENT, -1
        if path == b"/":
            return 0, -1
        comps = path[1:].split(b"/")
        cur = 0
        for i, c in enumerate(comps):
            if self.nodes[cur]["kind"] != K_DIR:
                return ENOTDIR, -1
            ch = self.child(cur, c)
            if i == len(comps) - 1:
                return (ENOENT if ch < 0 else ch), cur
            if ch < 0:
                return ENOENT, -1
            cur = ch
        return ENOENT, -1

    def change(self):
        self.calls += 1
        return self.fail_at and self.calls == self.fail_at

    def lstat(self, path):
        n, _ = self.lookup(path)
        if n == ENOENT:
            return K_NONE
        if n < 0:
            return K_ERROR
        return self.nodes[n]["kind"]

    def read_dir(self, path, cookie):
        n, _ = self.lookup(path)
        if n < 0:
            return n, cookie, None
        if self.nodes[n]["kind"] != K_DIR:
            return ENOTDIR, cookie, None
        for i in range(cookie + 1, len(self.nodes)):
            x = self.nodes[i]
            if x["used"] and x["parent"] == n:
                return 1, i, x["name"]
        return 0, cookie, None

    def mkdir(self, path):
        n, parent = self.lookup(path)
        if self.change():
            return ENOMEM
        if n >= 0:
            return EEXIST
        if parent < 0:
            return n
        return ENOMEM if self.add(parent, K_DIR, path.rsplit(b"/", 1)[1], 0, 0) < 0 else 0

    def unlink(self, path):
        n, _ = self.lookup(path)
        if self.change():
            return ENOMEM
        if n < 0:
            return n
        if self.nodes[n]["kind"] == K_DIR:
            return EISDIR
        self.nodes[n]["used"] = False
        return 0

    def rmdir(self, path):
        n, _ = self.lookup(path)
        if self.change():
            return ENOMEM
        if n < 0:
            return n
        if self.nodes[n]["kind"] != K_DIR:
            return ENOTDIR
        if n == 0:
            return ENOTEMPTY
        if any(x["used"] and x["parent"] == n for x in self.nodes[1:]):
            return ENOTEMPTY
        self.nodes[n]["used"] = False
        return 0

    def copy_chunk(self, src, dst, off):
        s, _ = self.lookup(src)
        d, dp = self.lookup(dst)
        if self.change():
            return ENOMEM
        if s < 0:
            return s
        if self.nodes[s]["kind"] != K_FILE:
            return EISDIR
        if off == 0:
            if d >= 0:
                return EEXIST
            if dp < 0:
                return d
            d = self.add(dp, K_FILE, dst.rsplit(b"/", 1)[1], 0, self.nodes[s]["seed"])
            if d < 0:
                return ENOMEM
        elif d < 0:
            return d
        size = self.nodes[s]["size"]
        if off >= size:
            return 0
        n = min(size - off, CHUNK)
        self.nodes[d]["size"] += n
        return n

    def path(self, i):
        parts = []
        while i > 0:
            parts.append(self.nodes[i]["name"])
            i = self.nodes[i]["parent"]
        return b"/" + b"/".join(reversed(parts))

    def dump(self):
        rows = [(self.path(i), x["kind"], x["size"], x["seed"]) for i, x in enumerate(self.nodes)
                if i and x["used"]]
        return ["%s %d %d %d" % (p.hex(), k, s, sd) for p, k, s, sd in sorted(rows)]


WALK_NAMES = ["more", "done", "deep", "too-long", "link", "error"]
DEPTH_MAX = 64


def walk(fs, op, src, dst):
    """The walk, written again from files_core.h: (result, done, links, link name, err, steps)."""
    state = {"done": 0, "links": 0, "link": b"", "err": 0}
    if len(src) > PATH_MAX or (op == "C" and len(dst) > PATH_MAX):
        return "too-long", 0, 0, b"", 0, 0
    if len(src) < 2 or src[:1] != b"/" or (op == "C" and (len(dst) < 2 or dst[:1] != b"/")):
        return "error", 0, 0, b"", 0, 0
    steps = 0
    levels = []          # [cookie, src, dst]

    def fin(r):
        return r, state["done"], state["links"], state["link"], state["err"], steps

    steps += 1
    k = fs.lstat(src)
    if k not in (K_DIR, K_FILE, K_LINK):
        return fin("error")
    if op == "D":
        if k == K_DIR:
            levels.append([0, src, dst])
        else:
            r = fs.unlink(src)
            if r < 0:
                state["err"] = r
                return fin("error")
            state["done"] = 1
            return fin("done")
    else:
        if k == K_LINK:
            state["link"] = src.rsplit(b"/", 1)[1][:NAME_MAX]
            return fin("link")
        if k == K_DIR:
            r = fs.mkdir(dst)
            if r < 0:
                state["err"] = r
                return fin("error")
            levels.append([0, src, dst])
        else:
            pass
    copying = (op == "C" and k == K_FILE)
    csrc, cdst, off = src, dst, 0
    while True:
        steps += 1
        if steps > 2000000:
            return fin("runaway")
        if copying:
            r = fs.copy_chunk(csrc, cdst, off)
            if r < 0:
                fs.unlink(cdst)
                state["err"] = r
                return fin("error")
            if r > 0:
                off += r
                continue
            copying = False
            state["done"] += 1
            if not levels:
                return fin("done")
            continue
        lv = levels[-1]
        if op == "D":
            rd, _, name = fs.read_dir(lv[1], 0)
        else:
            rd, lv[0], name = fs.read_dir(lv[1], lv[0])
        if rd < 0:
            state["err"] = rd
            return fin("error")
        if rd == 0:
            if op == "D":
                r = fs.rmdir(lv[1])
                if r < 0:
                    state["err"] = r
                    return fin("error")
            state["done"] += 1
            levels.pop()
            if not levels:
                return fin("done")
            continue
        if not name or len(name) > NAME_MAX:
            return fin("error")
        esrc = lv[1] + b"/" + name
        edst = lv[2] + b"/" + name if op == "C" else b""
        if len(esrc) > PATH_MAX or (op == "C" and len(edst) > PATH_MAX):
            return fin("too-long")
        k = fs.lstat(esrc)
        if k not in (K_DIR, K_FILE, K_LINK):
            return fin("error")
        if k == K_DIR:
            if len(levels) == DEPTH_MAX + 1:
                return fin("deep")
            if op == "C":
                r = fs.mkdir(edst)
                if r < 0:
                    state["err"] = r
                    return fin("error")
            levels.append([0, esrc, edst])
            continue
        if op == "D":
            r = fs.unlink(esrc)
            if r < 0:
                state["err"] = r
                return fin("error")
            state["done"] += 1
            continue
        if k == K_LINK:
            if state["links"] == 0:
                state["link"] = name
            state["links"] += 1
            continue
        copying, csrc, cdst, off = True, esrc, edst, 0


def check_target(fs, path):
    if not path or len(path) > PATH_MAX or path[:1] != b"/":
        return "error", 0
    for j in range(1, len(path) + 1):
        if j < len(path) and path[j] != 0x2F:
            continue
        k = fs.lstat(path[:j])
        if k == K_NONE:
            return "missing", j
        if k == K_ERROR:
            return "error", j
        if k != K_DIR:
            return "not-folder", j
    return "ok", len(path)


# ---------------------------------------------------------------- the inputs (fixed seed)

def gen_trash(rnd):
    def comp():
        alpha = [b"a", b"B", b"7", b" ", b".", b"-", b"%", b"\xc3\xa9", b"_", b"~", b"(", b"#", b"\t", b"\xff"]
        return b"".join(rnd.choice(alpha) for _ in range(rnd.randint(1, 12)))

    def good_path():
        if rnd.random() < 0.02:
            return TRASH + rnd.choice((b"", b"/x", b"/files/y", b"x"))
        return b"/home/user/" + b"/".join(comp() for _ in range(rnd.randint(1, 4)))

    def date():
        return b"%04d-%02d-%02dT%02d:%02d:%02d" % (rnd.randint(1960, 2100), rnd.randint(1, 12), rnd.randint(1, 28),
                                                  rnd.randint(0, 23), rnd.randint(0, 59), rnd.randint(0, 59))

    tricks = [b"%2F", b"%2f", b"%00", b"%G1", b"%4", b"%", b"/../", b"/./", b"//", b"/.local/share/Trash/x",
              b"\x00", b"%41", b"\n", b"Path=/etc/x\n", b"[Trash Info]\n", b"DeletionDate=bad\n"]
    out = []
    for i in range(20000):
        p = good_path()
        enc = ti_encode(p) if rnd.random() < 0.7 else p
        lines = [b"[Trash Info]", b"Path=" + enc, b"DeletionDate=" + date()]
        if rnd.random() < 0.2:
            rnd.shuffle(lines)
        if rnd.random() < 0.1:
            lines.insert(rnd.randint(0, len(lines)), b"Path=" + ti_encode(good_path()))
        if rnd.random() < 0.1:
            lines.insert(0, b"")
        buf = b"\n".join(lines) + (b"\n" if rnd.random() < 0.8 else b"")
        r = rnd.random()
        if r < 0.35:
            for _ in range(rnd.randint(1, 3)):
                at = rnd.randint(0, len(buf))
                buf = buf[:at] + rnd.choice(tricks) + buf[at:]
        elif r < 0.5:
            b = bytearray(buf)
            for _ in range(rnd.randint(1, 4)):
                b[rnd.randrange(len(b))] = rnd.randrange(256)
            buf = bytes(b)
        elif r < 0.55:
            buf = buf[:rnd.randint(0, len(buf))]
        elif r < 0.56:
            buf = bytes(rnd.randrange(256) for _ in range(rnd.randint(0, 200)))
        if i % 400 == 1:
            buf = b"[Trash Info]\nPath=/home/user/" + b"a" * rnd.randint(4140, 4170) + b"\n"
        elif i % 400 == 2:
            buf = b"[Trash Info]\nPath=/home/user/x\n#" + b"z" * rnd.randint(8150, 8200) + b"\n"
        elif i % 400 == 3:
            buf = b"[Trash Info]\nPath=/home/user/" + b"/".join(b"q" * 250 for _ in range(rnd.randint(15, 17))) + b"\n"
        elif i % 400 == 4:
            buf = b"[Trash Info]\nPath=/home/user/" + b"c" * rnd.randint(254, 257) + b"\n"
        out.append(buf)
    return out


def gen_encode(rnd):
    out = []
    for i in range(5000):
        r = rnd.random()
        if r < 0.7:
            comps = [bytes(rnd.choice(b"aZ9 %.-~\x01\x7f\x80\xfe\xff()*'!") for _ in range(rnd.randint(1, 20)))
                     for _ in range(rnd.randint(1, 5))]
            p = b"/home/user/" + b"/".join(comps)
        elif r < 0.8:
            p = bytes(rnd.randrange(256) for _ in range(rnd.randint(1, 60)))
        else:
            n = rnd.randint(1, 16)
            p = b"/home/user/" + b"/".join(bytes(rnd.choice(b"\xff a") for _ in range(250)) for _ in range(n))
        d = "%04d-%02d-%02dT%02d:%02d:%02d" % (rnd.randint(0, 10000), rnd.randint(0, 13), rnd.randint(0, 32),
                                               rnd.randint(0, 24), rnd.randint(0, 60), rnd.randint(0, 60))
        if rnd.random() < 0.6:
            d = "2026-%02d-%02dT10:20:30" % (rnd.randint(1, 12), rnd.randint(1, 28))
        out.append((p, d))
    return out


def gen_names(rnd, n, hidden=0.0):
    names, seen = [], set()
    while len(names) < n:
        nm = bytes(rnd.choice(b"aAbBzZ_.-0~ \xc3\xa9\xff") for _ in range(rnd.randint(1, 8)))
        if rnd.random() < hidden:
            nm = b"." + nm
        if nm in seen or nm in (b".", b"..") or b"/" in nm:
            continue
        seen.add(nm)
        names.append(nm)
    return names


def gen_sort(rnd):
    out = []
    for i in range(200):
        n = 1000 if i in (0, 1) else rnd.randint(0, 150)
        out.append([(rnd.choice((K_DIR, K_FILE, K_LINK)), nm) for nm in gen_names(rnd, n, 0.1)])
    return out


def gen_limit(rnd):
    out = []
    for i in range(30):
        n = rnd.choice((0, 5, 999, 1000, 1001, 1200, 1500))
        rows = [(rnd.choice((K_DIR, K_FILE, K_LINK)), nm) for nm in gen_names(rnd, n, 0.3)]
        for _ in range(rnd.randint(0, 4)):
            rows.insert(rnd.randint(0, len(rows)), rnd.choice([(K_DIR, b"."), (K_DIR, b".."), (K_FILE, b"a/b"),
                                                              (K_FILE, b""), (K_FILE, b"x" * 256), (K_FILE, b"n\x00"),
                                                              (K_ERROR, b"err"), (K_NONE, b"none")]))
        out.append((rnd.random() < 0.5, rows))
    return out


def gen_pathbar(rnd):
    out = []
    for i in range(600):
        r = rnd.random()
        if r < 0.05:
            view, path = rnd.choice((V_TRASH, V_THISPC)), b""
        else:
            comps = [bytes(rnd.choice(b"abcXYZ.-") for _ in range(rnd.randint(1, 12)))
                     for _ in range(rnd.choice((0, 1, 2, 3, 5, 10, 40)))]
            head = rnd.choice((b"/home/user", b"/home/user", b"", b"/tmp", b"/home"))
            path = head + b"".join(b"/" + c for c in comps) or b"/"
            view = V_FOLDER
        out.append((view, rnd.choice((0, 2, 3, 4, 5, 8, 12, 20, 40, 80, 100, 160, 400)), path))
    return out


def gen_history(rnd):
    pool = [b"/home/user"] + [b"/home/user/d%d" % i for i in range(12)] + [b"/", b"/tmp", b"/home"]
    ops = []
    for i in range(2000):
        r = rnd.random()
        if i % 100 == 0:
            ops.append(("RESET",))
        if r < 0.45:
            v = V_FOLDER if rnd.random() < 0.9 else rnd.choice((V_TRASH, V_THISPC))
            ops.append(("G", v, rnd.choice(pool) if v == V_FOLDER else b""))
        elif r < 0.62:
            ops.append(("B",))
        elif r < 0.77:
            ops.append(("F",))
        elif r < 0.85:
            ops.append(("U",))
        elif r < 0.93:
            ops.append(("X", rnd.choice(pool)))
        else:
            ops.append(("Y", rnd.choice(pool)))
    for i in range(70):                   # past 64 entries in a row
        ops.append(("G", V_FOLDER, b"/home/user/n%d" % i))
    ops += [("B",)] * 70 + [("F",)] * 70
    return ops


def gen_walk(rnd):
    cases = []
    for i in range(100):
        nodes = []      # (parent, kind, name, size, seed)

        def add(parent, kind, name, size=0, seed=0):
            nodes.append((parent, kind, name, size, seed))
            return len(nodes)

        top = add(0, K_DIR, b"src")
        add(0, K_DIR, b"dst")
        out = add(0, K_DIR, b"outside")
        add(out, K_FILE, b"keep.txt", 100, 7)
        dirs = [top]
        for _ in range(rnd.randint(1, 60)):
            parent = rnd.choice(dirs)
            kind = rnd.choice((K_DIR, K_FILE, K_FILE, K_LINK))
            name = bytes(rnd.choice(b"abcdef") for _ in range(rnd.randint(1, 4))) + b"%d" % len(nodes)
            n = add(parent, kind, name, rnd.choice((0, 1, 4095, 4096, 4097, 10000)) if kind == K_FILE else 0,
                    rnd.randint(1, 999))
            if kind == K_DIR:
                dirs.append(n)
        if i % 10 == 3:                   # a chain 63 to 66 folders below src
            cur = top
            for d in range(rnd.randint(63, 66)):
                cur = add(cur, K_DIR, b"n%d" % d)
            add(cur, K_FILE, b"deep.txt", 10, 3)
        if i % 10 == 5:                   # names long enough to pass 4,096 bytes
            cur = top
            for d in range(17):
                cur = add(cur, K_DIR, bytes([0x61 + d % 26]) * 250)
        op = "D" if i % 2 == 0 else "C"
        src = b"/src" if i % 7 else b"/src/" + nodes[4][2] if len(nodes) > 4 and nodes[4][0] == top else b"/src"
        dst = b"/dst/copy" if op == "C" else b""
        fail_at = rnd.choice((0, 0, 0, 1, 2, 5, 17))
        checks = [b"/src", b"/outside", b"/outside/keep.txt", b"/dst", b"/"]
        links = [n for n in range(len(nodes)) if nodes[n][1] == K_LINK]
        if links:
            ln = links[0]
            p = []
            j = ln + 1
            while j:
                par, _, nm, _, _ = nodes[j - 1]
                p.append(nm)
                j = par
            lp = b"/" + b"/".join(reversed(p))
            checks += [lp, lp + b"/x"]
        cases.append((nodes, op, src, dst, fail_at, checks))
    return cases


def gen_clip(rnd):
    ops = []
    for i in range(400):
        r = rnd.random()
        if r < 0.1:
            ops.append(("B", rnd.randint(0, 1)))
        elif r < 0.5:
            ops.append(("A", b"/home/user/" + b"x" * rnd.choice((1, 5, 300, 4085, 4086, 4090))))
        elif r < 0.55:
            ops.append(("C",))
        elif r < 0.7:
            ops.append(("P", rnd.randint(0, 70)))
        elif r < 0.85:
            nm = rnd.choice((b"a.txt", b"Report", b".bashrc", b"a.b.c", b"x" * 250 + b".txt", b"y" * 255,
                             b"z" * 240 + b"." + b"e" * 14, b"n."))
            ops.append(("V", rnd.randint(0, 1), rnd.randint(0, 1), rnd.choice((0, 1, 2, 9, 10, 99, 999, 1000)), nm))
        else:
            nm = rnd.choice((b"a.txt", b"Folder", b"q" * 254))
            taken = set()
            for n in range(1, rnd.choice((1, 3, 999, 1000)) + 1):
                v = name_variant(nm, False, False, n)
                if v:
                    taken.add(v)
            ops.append(("U", 0, 0, nm, sorted(taken)))
    for i in range(70):
        ops.append(("A", b"/home/user/f%d" % i))
    return ops


# ---------------------------------------------------------------- running the tool

def hx(b):
    return b.hex() if b else "-"


def run(tool, mode, lines):
    data = (mode + "\n" + "\n".join(lines) + "\n").encode()
    p = subprocess.run([tool], input=data, capture_output=True, timeout=600)
    return p.returncode, p.stdout.decode(errors="replace").splitlines(), p.stderr.decode(errors="replace")


def trash_case(buf):
    r, p, d = ti_parse(buf)
    return "%s %s %s" % (r, hx(p) if r == "ok" else "-", (d or "-") if r == "ok" else "-")


def encode_case(p, d):
    enc = ti_encode(p)
    enc_s = enc.decode() if len(enc) + 1 <= 3 * PATH_MAX + 1 else None
    if enc_s:
        r, dec = ti_decode(enc)
        dec_s = "%s %s" % (r, hx(dec) if r == "ok" else "-")
    else:
        dec_s = "- -"
    b = ti_build(p, d)
    if b is None:
        bl = "0 - 0"
    else:
        r, pp, dd = ti_parse(b)
        bl = "%d %s %d" % (len(b), r, 1 if (pp == p and dd == d) else 0)
    return "%s %s %s" % (hx(enc_s.encode()) if enc_s else "-", dec_s, bl)


def models(rnd):
    """The inputs and the expected lines of each mode."""
    m = {}
    tr = gen_trash(rnd)
    m["trash"] = ([hx(b) for b in tr], [trash_case(b) for b in tr])
    en = gen_encode(rnd)
    m["encode"] = (["%s %s" % (p.hex(), d) for p, d in en], [encode_case(p, d) for p, d in en])
    so = gen_sort(rnd)
    lines, exp = [], []
    for rows in so:
        lines.append("N %d" % len(rows))
        lines += ["%d %s" % (k, hx(nm)) for k, nm in rows]
        exp += [nm.hex() for _, nm in sorted(rows, key=lambda r: sort_key(*r))] + ["END"]
    m["sort"] = (lines, exp)
    li = gen_limit(rnd)
    lines, exp = [], []
    for show, rows in li:
        lines.append("L %d %d" % (show, len(rows)))
        lines += ["%d %s" % (k, hx(nm)) for k, nm in rows]
        st, codes = {"show": show, "names": [], "truncated": False}, ""
        for k, nm in rows:
            c = folder_add(st, nm, k)
            codes += c
            if c == "F":
                break
        exp.append("%s %d %d %s" % (codes or "-", len(st["names"]), st["truncated"],
                                    ",".join(nm.hex() for _, nm in st["names"]) or "-"))
    m["limit"] = (lines, exp)
    pb = gen_pathbar(rnd)
    lines, exp = [], []
    for view, cells, path in pb:
        lines.append("%d %d %s" % (view, cells, hx(path)))
        n, clipped, used, parts = pathbar(view, path, cells)
        exp.append("%d %d %d" % (n, clipped, used) + "".join(" %s,%d,%d" % (hx(t), x, g) for t, x, g in parts))
    m["pathbar"] = (lines, exp)
    hi = gen_history(rnd)
    lines, exp = [], []
    h, missing = History(), set()
    for op in hi:
        if op[0] == "RESET":
            lines.append("RESET")
            h, missing = History(), set()
            continue
        if op[0] in ("X", "Y"):
            lines.append("%s %s" % (op[0], op[1].hex()))
            (missing.add if op[0] == "X" else missing.discard)(op[1])
            continue
        removed = 0
        if op[0] == "G":
            lines.append("G %d %s" % (op[1], hx(op[2])))
            ret = h.go(op[1], op[2])
        elif op[0] == "B":
            lines.append("B")
            ret, removed = h.back(missing)
        elif op[0] == "F":
            lines.append("F")
            ret, removed = h.forward(missing)
        else:
            lines.append("U")
            ret = h.up()
        c = h.cur()
        exp.append("%d %d %d %d %d %d %d %s %s" % (ret, removed, len(h.e), h.pos if h.e else 0, h.can_back(),
                                                    h.can_forward(), h.can_up(), c[0] if c else 0,
                                                    hx(c[1]) if c else "-"))
    m["history"] = (lines, exp)
    wk = gen_walk(rnd)
    lines, exp = [], []
    for nodes, op, src, dst, fail_at, checks in wk:
        lines.append("T %d" % len(nodes))
        lines += ["%d %d %s %d %d" % (p, k, nm.hex(), s, sd) for p, k, nm, s, sd in nodes]
        fs = FakeFS()
        for p, k, nm, s, sd in nodes:
            fs.add(p, k, nm, s, sd)
        for c in checks:
            lines.append("K %s" % c.hex())
            r, at = check_target(fs, c)
            exp.append("%s %d" % (r, at))
        lines.append("W %s %s %s %d" % (op, src.hex(), hx(dst), fail_at))
        fs.fail_at = fail_at
        r, done, links, lname, err, steps = walk(fs, op, src, dst)
        exp.append("%s %d %d %s %d" % (r, done, links, hx(lname), err))
        exp += fs.dump() + ["END"]
    m["walk"] = (lines, exp)
    cl = gen_clip(rnd)
    lines, exp = [], []
    clip, cut = [], False
    for op in cl:
        if op[0] == "B":
            lines.append("B %d" % op[1])
            clip, cut = [], bool(op[1])
            exp.append("1 %d %d %d" % (len(clip), cut, not clip))
        elif op[0] == "A":
            lines.append("A %s" % op[1].hex())
            p = op[1]
            if not p or len(p) > PATH_MAX:
                ret = 2
            elif len(clip) == 64:
                ret = 1
            else:
                clip.append(p)
                ret = 0
            exp.append("%d %d %d %d" % (ret, len(clip), cut, not clip))
        elif op[0] == "C":
            lines.append("C")
            clip, cut = [], False
            exp.append("1 %d %d %d" % (len(clip), cut, not clip))
        elif op[0] == "P":
            lines.append("P %d" % op[1])
            exp.append(clip[op[1]].hex() if op[1] < len(clip) else "-")
        elif op[0] == "V":
            _, folder, restored, n, nm = op
            lines.append("V %d %d %d %s" % (folder, restored, n, nm.hex()))
            v = name_variant(nm, folder, restored, n)
            exp.append(hx(v))
        else:
            _, folder, restored, nm, taken = op
            lines.append("U %d %d %s %s" % (folder, restored, nm.hex(), ",".join(t.hex() for t in taken) or "-"))
            exp.append(hx(unique_name(nm, folder, restored, set(taken))))
    m["clip"] = (lines, exp)
    return m


def static(tool, asan, broken):
    rnd = random.Random(SEED)
    m = models(rnd)
    bad_asan = []
    for mode, (lines, exp) in m.items():
        if broken == mode and exp:
            exp[0] = exp[0] + "#"
        rc, out, _ = run(tool, mode, lines)
        arc, aout, aerr = run(asan, mode, lines)
        if broken == "asan" and mode == "trash":
            aerr += "\nruntime error: (spoiled on purpose)\n"
        if arc != 0 or "runtime error" in aerr or "Sanitizer" in aerr or aout != out:
            bad_asan.append("%s (exit %d%s%s)" % (mode, arc, ", a sanitizer report" if ("runtime error" in aerr or
                                                                                    "Sanitizer" in aerr) else "",
                                                ", output differs from the plain build" if aout != out else ""))
        diff = next((i for i, (a, b) in enumerate(zip(out, exp)) if a != b), None)
        if rc == 0 and diff is None and len(out) == len(exp):
            print("FILESREF %s PASS %d inputs, %d lines equal the model" % (mode, len(lines), len(exp)))
        else:
            where = diff if diff is not None else min(len(out), len(exp))
            print("FILESREF %s FAIL exit %d; %d lines, expected %d; first difference at line %d: got %r, expected %r"
                  % (mode, rc, len(out), len(exp), where, out[where] if where < len(out) else None,
                     exp[where] if where < len(exp) else None))
    if bad_asan:
        print("FILESREF asan FAIL " + "; ".join(bad_asan))
    else:
        print("FILESREF asan PASS every mode under AddressSanitizer and UndefinedBehaviorSanitizer: exit 0, no "
              "report, the same output as the plain build")


def main(argv):
    if len(argv) >= 3 and argv[0] == "--static":
        broken = argv[4] if len(argv) >= 5 and argv[3] == "--break" else None
        static(argv[1], argv[2], broken)
        return 0
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
