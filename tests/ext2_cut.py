#!/usr/bin/env python3
"""Madire OS: the host cut sweep and the host sequences' checks (1.4 stage 9;
docs/plans/1.4.md, S9, section D: the allowed e2fsck list A1-A13).

tests/ext2write.c (built from the pure ext2 write core and the block layer's
save order) runs a fixed operation sequence on an image file; with -c N the
disk takes only its first N write commands (a power cut after write N).
e2fsck -fn and debugfs (e2fsprogs, unmodified outside programs, run read
only) are the independent check. Run with PYTHONDONTWRITEBYTECODE=1 and
python3 -I.

  ext2_cut.py final <ext2write> <fs image> <sequence> <work dir>
      one uncut run: e2fsck -fn must report nothing and debugfs's view must
      equal the tool's expected tree (paths, modes, sizes, link counts,
      SHA-256 of the contents)
  ext2_cut.py sweep <ext2write> <fs image> <sequence> <work dir> [--san <ext2write built with sanitizers>]
      the uncut run as above, then a cut after write N for EVERY N from 0 to
      the last write but one; after each cut e2fsck -fn may report only the
      allowed list (section D) and every file saved before the cut must read
      back exactly. --san: every N again with the sanitized build, which
      must leave byte-identical images and report nothing.
  ext2_cut.py readonly <ext2write> <fs image> <work dir>
      a volume the policy makes read-only: no write, the image unchanged

Stage 11 (R5, A14, the narrowed A9; docs/plans/1.4.md, section D):
  ext2_cut.py usweep <ext2write> <fs image> <sequence> <work dir>
      the unordered sweep: a real disk may store the writes between two
      FLUSHes in any order. For each flush interval (from the uncut run's
      W and F lines) whose writes cover two or more filesystem blocks
      ("units"), cut inside it with a subset of its units on the disk:
      every proper non-empty subset when it has at most 8 units, otherwise
      a fixed sample (each single unit, each all-but-one, each prefix and
      suffix, and 64 subsets from a fixed-seed generator). The checks after
      each cut are the sweep's (the allowed list in its contexts, every
      saved file exact, the owner's case rules).
  ext2_cut.py qmodel <ext2write> <fs image> <sequence> <work dir>
      the host model of a QEMU cut sequence: the uncut run as "final",
      then N1 and N2 of the owner's case (section D) from its writes
  ext2_cut.py qcompare <work dir> <serial log>
      the kernel's "blk: write" lines (test=diskcut) against the model's
      write commands, one by one (number, class, offset, length)
  ext2_cut.py qjudge <work dir> <sequence> <cut fs image> <N>
      the section D checks on a partition the kernel wrote with
      test=diskcut=N, in the model's contexts
  ext2_cut.py qview <fs image> <serial log> <name>...
      Madire's "diskcheck:" lines (the remount boot) against debugfs's
      reading of the same names in the root folder (present, size, CRC32)
  ext2_cut.py fsckstrict <fs image>
      e2fsck -fn may report only leaks and counts (A1-A7, A12)

Every saved path the cut save renames must read back exactly at its old
path or its new path, everything inside a moved folder included (stage 11;
the stage 9 checker accepted "at neither path" for renamed paths).

The image given is never changed: every run works on a copy in <work dir>.
Every line e2fsck prints that is not on the list (or not in its context)
fails and is printed word for word.
"""
import hashlib
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time
import zlib

LABEL = "madire-test"


def tool(name):
    p = shutil.which(name, path=os.environ.get("PATH", "") + ":/usr/sbin:/sbin")
    if not p:
        raise SystemExit("ext2_cut.py: no " + name)
    return p


def env():
    e = dict(os.environ)
    e.update(LC_ALL="C", E2FSCK_CONFIG="/dev/null", DEBUGFS_PAGER="__none__", PAGER="__none__")
    return e


def sha_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        while True:
            b = f.read(1 << 20)
            if not b:
                break
            h.update(b)
    return h.hexdigest()


def fsck(img):
    r = subprocess.run([tool("e2fsck"), "-fn", img], stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                       stdin=subprocess.DEVNULL, env=env())
    return r.returncode, r.stdout.decode("latin-1").splitlines()


def debugfs(img, request):
    r = subprocess.run([tool("debugfs"), "-R", request, img], stdout=subprocess.PIPE,
                       stderr=subprocess.DEVNULL, stdin=subprocess.DEVNULL, env=env())
    return r.stdout.decode("latin-1")


def debugfs_batch(img, requests):
    with tempfile.NamedTemporaryFile("w", suffix=".cmd", delete=False) as f:
        f.write("\n".join(requests) + "\n")
        name = f.name
    try:
        r = subprocess.run([tool("debugfs"), "-f", name, img], stdout=subprocess.PIPE,
                           stderr=subprocess.DEVNULL, stdin=subprocess.DEVNULL, env=env())
    finally:
        os.unlink(name)
    return r.stdout.decode("latin-1")


# ---- e2fsck's output: the frame and the allowed list (section D) ----

FRAME = [
    re.compile(r"^e2fsck \d+\.\d+.*$"),
    re.compile(r"^Pass [1-5]: [A-Za-z ,]+$"),
    re.compile(r"^$"),
    re.compile(r"^\S+: \d+/\d+ files \(.*\), \d+/\d+ blocks$"),
    re.compile(r"^(\S+: )?\*+ WARNING: Filesystem still has errors \*+$"),
    re.compile(r"^(Fix|Clear|Connect to /lost\+found)\? no$"),
]

BITMAP = re.compile(r"^(Block|Inode) bitmap differences:(.*)$")
ITEM = re.compile(r"^([-+])\(?(\d+)(?:--(\d+))?\)?$")
RULES = [
    ("A3", re.compile(r"^Free blocks count wrong for group #\d+ \(\d+, counted=\d+\)\.$")),
    ("A4", re.compile(r"^Free blocks count wrong \(\d+, counted=\d+\)\.$")),
    ("A5", re.compile(r"^Free inodes count wrong for group #\d+ \(\d+, counted=\d+\)\.$")),
    ("A6", re.compile(r"^Free inodes count wrong \(\d+, counted=\d+\)\.$")),
    ("A7", re.compile(r"^Directories count wrong for group #\d+ \(\d+, counted=\d+\)\.$")),
    ("A8", re.compile(r"^Unattached inode (\d+)$")),
    ("A8", re.compile(r"^Unattached zero-length inode (\d+)\.(  Clear\? no)?$")),
    ("A9", re.compile(r"^Unconnected directory inode (\d+) \(.*\)$")),
    ("A10", re.compile(r"^Inode (\d+) ref count is (\d+), should be (\d+)\.(  Fix\? no)?$")),
    ("A11", re.compile(r"^'\.\.' in .* \((\d+)\) is .* \(\d+\), should be .* \(\d+\)\.(  Fix\? no)?$")),
    ("A12", re.compile(r"^\S+ was not cleanly unmounted, check forced\.$")),
    ("A13", re.compile(r"^Extended attribute block (\d+) has reference count (\d+), should be (\d+)\.(  .*)?$")),
    # Stage 11 (R5): a folder under two names (its new name on the disk,
    # the old one not yet removed). Its inode is the last number.
    ("A14", re.compile(r"^Entry '.*' in .* \(\d+\) is a link to directory .* \((\d+)\)\.(  Clear\? no)?$")),
]
RULES_BY_NAME = {name: rx for name, rx in RULES if name != "A8"}
ALL_RULES = ("A1", "A2", "A3", "A4", "A5", "A6", "A7", "A8", "A9", "A10", "A11", "A12", "A13", "A14")


def classify(lines, ctx):
    """(counts per rule, bad lines, accepted A8-A11 lines as (rule, inode,
    window), every accepted line verbatim as (rule, inode or None, text)). ctx: the
    inode sets of the cut save and the save before, and the strict flag. A11 needs
    to know which folders A9 accepted in the same output, so A9 is judged first."""
    counts, bad, accepts, raws = {}, [], [], []
    a9 = set()
    for line in lines:
        m = RULES_BY_NAME["A9"].match(line)
        if m and context_ok("A9", m, ctx)[0]:
            a9.add(int(m.group(1)))
    ctx = dict(ctx, a9_ok=a9)
    i = 0
    while i < len(lines):
        line = lines[i]
        i += 1
        if any(f.match(line) for f in FRAME):
            continue
        m = BITMAP.match(line)
        if m:
            items = m.group(2).split()
            while i < len(lines) and lines[i].startswith(" ") and lines[i].strip() and \
                    all(ITEM.match(t) for t in lines[i].split()):
                items += lines[i].split()
                i += 1
            rule = "A1" if m.group(1) == "Block" else "A2"
            if items and all(ITEM.match(t) and t.startswith("-") for t in items):
                counts[rule] = counts.get(rule, 0) + 1
                raws.append((rule, None, line))
            else:
                bad.append(line + "   [not only '-' items: something in use is marked free]")
            continue
        for rule, rx in RULES:
            m = rx.match(line)
            if not m:
                continue
            ok, why, window = context_ok(rule, m, ctx)
            if ok:
                counts[rule] = counts.get(rule, 0) + 1
                ino = int(m.group(1)) if rule in ("A8", "A9", "A10", "A11", "A13", "A14") else None
                if rule in ("A8", "A9", "A10", "A11", "A14"):
                    accepts.append((rule, ino, window))
                raws.append((rule, ino, line))
            else:
                bad.append(line + "   [" + why + "]")
            break
        else:
            bad.append(line)
    return counts, bad, accepts, raws


def context_ok(rule, m, ctx):
    """(accepted, why not, the window that accepted it)."""
    if rule in ("A3", "A4", "A5", "A6", "A7", "A12"):
        return True, "", "always"
    if ctx["strict"]:
        return False, "after saves 3-4 only leaks and counts are allowed", ""
    if rule == "A8":
        # changed in the cut save or the save before
        ino = int(m.group(1))
        if ino in ctx["t_cut"]:
            return True, "", "cut save"
        if ino in ctx["t_before"]:
            return True, "", "save before"
        return False, "inode not changed in the cut save or the save before", ""
    if rule == "A9":
        # (narrowed by the owner in stage 11: R5 keeps a moved folder under
        # at least one name) the folder was made or removed in the cut save,
        # or its name was removed in the save before (R3)
        ino = int(m.group(1))
        if ino in ctx.get("made_cut", set()):
            return True, "", "made in the cut save"
        if ino in ctx.get("r3_dir_cut", set()):
            return True, "", "removed in the cut save"
        if ino in ctx["r3_dir_before"]:
            return True, "", "R3 set, save before"
        return False, "folder not made or removed in the cut save, nor its name removed in the save before", ""
    if rule == "A14":
        # (stage 11, R5) a folder under two names: moved to another folder
        # in the cut save (its new name on the disk, the old not yet removed)
        ino = int(m.group(1))
        if ino in ctx.get("moved_cut", set()):
            return True, "", "moved in the cut save"
        return False, "folder not moved to another folder in the cut save", ""
    if rule == "A10":
        # the inode was changed in the cut save, or it belongs to an R3 step
        # started in the save before (a hard-linked name removed, a folder
        # removed, or that folder's parent)
        ino = int(m.group(1))
        if ino in ctx["t_cut"]:
            return True, "", "cut save"
        if ino in ctx["r3_before"]:
            return True, "", "R3 set, save before"
        return False, "inode not changed in the cut save, nor in an R3 step started in the save before", ""
    if rule == "A11":
        # (approved by the owner in S9) the folder was moved to another
        # folder in the cut save, or the same output reports that folder as
        # unconnected under A9 (no folder names it, so its ".." cannot match)
        ino = int(m.group(1))
        if ino in ctx.get("moved_cut", set()):
            return True, "", "moved in the cut save"
        if ino in ctx.get("a9_ok", set()):
            return True, "", "the same folder accepted under A9"
        return False, "folder not moved to another folder in the cut save, nor unconnected (A9) in this output", ""
    if rule == "A13":
        if int(m.group(2)) > int(m.group(3)):
            return True, "", "always"
        return False, "reference count lower than the uses", ""
    return False, "unknown rule", ""


# ---- the tool's log ----

class Log:
    def __init__(self, text):
        self.text = text
        self.wsave = {}             # write number -> save
        self.state = {}             # save -> {path: (mode, size, sha)}
        self.changed = {}           # save -> {path: set(kinds)}
        self.touched = {}           # save -> set(ino)
        self.r3 = {}                # save -> set(ino): R3 steps started (hard, dir, parent)
        self.r3_dir = {}            # save -> set(ino): folders whose name was removed
        self.blocks = {}            # path -> [blocks]
        self.moved = {}             # save -> set(ino): folders moved to another folder
        self.paths = {}             # ino -> path (as the tool reports it)
        self.made = {}              # save -> set(ino): created or made (stage 11)
        self.mv = {}                # save -> [(from, to)] in order (stage 11)
        self.writes = []            # (n, save, class, off, len) in order (stage 11)
        self.intervals = []         # flush intervals: lists of writes (stage 11)
        self.bs = 1024
        self.ucut = None            # (interval, mask, units) of an unordered cut
        self.want = {}
        self.done = None
        self.cut = None
        self.fail = None
        after_done = False
        cur = []
        for line in text.splitlines():
            f = line.split()
            if not f:
                continue
            if after_done and f[0] == "P":
                self.want[f[1]] = proj(f)
            elif f[0] == "W":
                self.wsave[int(f[1])] = int(f[3])
                wr = (int(f[1]), int(f[3]), int(f[5]), int(f[7]), int(f[9]))
                self.writes.append(wr)
                cur.append(wr)
            elif f[0] == "F":
                self.intervals.append(cur)
                cur = []
            elif f[0] == "G":
                self.bs = int(f[1].split("=")[1])
            elif f[0] == "N":
                self.made.setdefault(int(f[1]), set()).add(int(f[2]))
            elif f[0] == "MV":
                self.mv.setdefault(int(f[1]), []).append((f[2], f[3]))
            elif f[0] == "UCUT":
                self.ucut = (int(f[2]), f[4], int(f[6]))
            elif f[0] == "K":
                self.state.setdefault(int(f[1]), {})[f[3]] = proj(f[2:])
            elif f[0] == "C":
                self.changed.setdefault(int(f[1]), {}).setdefault(f[3], set()).add(f[2])
            elif f[0] == "T":
                self.touched.setdefault(int(f[1]), set()).add(int(f[2]))
            elif f[0] == "R":
                self.r3.setdefault(int(f[1]), set()).add(int(f[3]))
                if f[2] == "dir":
                    self.r3_dir.setdefault(int(f[1]), set()).add(int(f[3]))
            elif f[0] == "M":
                self.moved.setdefault(int(f[1]), set()).add(int(f[2]))
            elif f[0] == "I":
                self.paths.setdefault(int(f[1]), f[2])
            elif f[0] == "B":
                self.blocks[f[1]] = [int(x) for x in f[2:]]
            elif f[0] == "DONE":
                self.done = int(f[1].split("=")[1])
                after_done = True
            elif f[0] == "CUT":
                self.cut = int(f[2])
            elif f[0] == "FAIL":
                self.fail = line
        if cur:
            self.intervals.append(cur)

    def save_writes(self, k):
        ws = [n for n, s in self.wsave.items() if s == k]
        return (min(ws), max(ws)) if ws else (None, None)


def proj(f):
    """(mode, size, links, sha) from 'P <path> mode= size= links= sha=' fields."""
    d = dict(x.split("=", 1) for x in f[2:] if "=" in x)
    return (d.get("mode"), d.get("size"), d.get("links"), d.get("sha"))


def run_tool(exe, img, seq, cut=None, extra_env=None, unordered=None):
    cmd = [exe] + (["-c", str(cut)] if cut is not None else []) + (["-u", unordered] if unordered else []) + [img, seq]
    e = dict(os.environ)
    if extra_env:
        e.update(extra_env)
    r = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, stdin=subprocess.DEVNULL, env=e,
                       timeout=120)
    return r.returncode, r.stdout.decode("latin-1"), r.stderr.decode("latin-1")


# ---- debugfs's reading of an image ----

def walk(img):
    """{path: (ino, mode)} from ls -p, the root first."""
    out = {"/": (2, 0o40755)}
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
            out[p] = (ino, mode)
            if mode & 0o170000 == 0o040000 and ino not in seen and len(seen) < 4096:
                seen.add(ino)
                todo.append((p, ino))
    return out


def contents(img, inos, work):
    """{ino: (size, sha)} of regular files, from one debugfs run (dump)."""
    d = os.path.join(work, "dump")
    shutil.rmtree(d, ignore_errors=True)
    os.makedirs(d)
    debugfs_batch(img, ["dump <%d> %s/%d" % (i, d, i) for i in sorted(set(inos))])
    res = {}
    for i in set(inos):
        p = os.path.join(d, str(i))
        if os.path.exists(p):
            res[i] = (os.path.getsize(p), sha_file(p))
    return res


BLOCKS = re.compile(r"\((?:\d+(?:-\d+)?|IND|DIND|TIND)\):(\d+)(?:-(\d+))?")


def blocks_of(img, ino):
    text = debugfs(img, "stat <%d>" % ino)
    part = text.split("BLOCKS:", 1)[1] if "BLOCKS:" in text else ""
    out = set()
    for a, b in BLOCKS.findall(part):
        out.update(range(int(a), int(b or a) + 1))
    return out


def view(img):
    """debugfs's view through tests/ext2_view.py (the S6 reader): {path: (mode, size, links, sha)}."""
    here = os.path.dirname(os.path.abspath(__file__))
    r = subprocess.run([sys.executable, "-I", "-B", os.path.join(here, "ext2_view.py"), "view", img],
                       stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, stdin=subprocess.DEVNULL)
    res = {}
    for line in r.stdout.decode("latin-1").splitlines():
        f = line.split()
        if f and f[0] == "P":
            res[f[1]] = proj(f)
    return res


# ---- the checks ----

def check_final(exe, pristine, seq, work, tag):
    os.makedirs(work, exist_ok=True)
    img = os.path.join(work, "uncut.img")
    shutil.copyfile(pristine, img)
    t0 = time.time()
    rc, out, err = run_tool(exe, img, seq)
    with open(os.path.join(work, "uncut.log"), "w") as f:
        f.write(out)
    log = Log(out)
    problems = []
    if rc != 0 or log.done is None:
        problems.append("ext2write exit %d: %s %s" % (rc, log.fail or "", err.strip()[:300]))
    frc, flines = fsck(img)
    counts, bad, _, _ = classify(flines, {"strict": True, "t_cut": set(), "t_before": set(), "r3_before": set(),
                                       "r3_dir_before": set(), "moved_cut": set()})
    if frc != 0 or bad or counts:
        problems.append("e2fsck -fn exit %d: %s" % (frc, " | ".join(bad + ["%s x%d" % kv for kv in counts.items()])))
    v = view(img)
    if v != log.want or not v:
        miss = sorted(set(log.want) ^ set(v))[:5]
        diff = [p for p in sorted(set(log.want) & set(v)) if log.want[p] != v[p]][:5]
        problems.append("debugfs's view differs: only one side %s; differ %s" % (
            miss, ["%s want %s got %s" % (p, log.want[p], v[p]) for p in diff]))
    print("FINAL %s: %s; ext2write exit %d, %d writes; e2fsck exit %d; view %d paths, expected %d; %.1f s" % (
        tag, "ok" if not problems else "BAD", rc, log.done or 0, frc, len(v), len(log.want), time.time() - t0))
    for p in problems:
        print("  " + p)
    for line in flines:
        print("  e2fsck: " + line)
    return (not problems), log, img


def check_cut(exe, pristine, seq, work, log, n):
    img = os.path.join(work, "cut.img")
    shutil.copyfile(pristine, img)
    rc, out, err = run_tool(exe, img, seq, cut=n)
    clog = Log(out)
    reasons = []
    if rc != 0 or clog.cut != n:
        reasons.append("ext2write exit %d, cut %s (want CUT after %d) %s" % (rc, clog.cut, n, err.strip()[:200]))
    s = log.wsave.get(n + 1)
    if s is None:
        reasons.append("no write %d in the uncut log" % (n + 1))
        return reasons, {}, sha_file(img), [], []
    more, counts, accepts, raws = judge(img, log, seq, work, s, n)
    return reasons + more, counts, sha_file(img), accepts, raws


def renamed_to(log, s, p):
    """The path p (saved before save s) has after save s's renames, in order
    (a path inside a moved folder follows it)."""
    q = p
    for fr, to in log.mv.get(s, []):
        if q == fr or q.startswith(fr + "/"):
            q = to + q[len(fr):]
    return q


def judge(img, log, seq, work, s, n):
    """The section D checks on an image cut inside save s, `n` writes known
    to be on the disk (the owner's case rules): (reasons, counts, accepted
    A8-A11 and A14 lines, every accepted line)."""
    reasons = []
    strict = seq == "owner" and s >= 4
    ctx = {"strict": strict, "t_cut": log.touched.get(s, set()), "t_before": log.touched.get(s - 1, set()),
           "r3_before": log.r3.get(s - 1, set()), "r3_dir_before": log.r3_dir.get(s - 1, set()),
           "moved_cut": log.moved.get(s, set()), "made_cut": log.made.get(s, set()),
           "r3_dir_cut": log.r3_dir.get(s, set())}
    frc, flines = fsck(img)
    counts, bad, accepts, raws = classify(flines, ctx)
    if frc not in (0, 4):
        reasons.append("e2fsck exit %d" % frc)
    for b in bad:
        reasons.append("e2fsck: " + b)
    # Every file saved before the cut reads back exactly; a saved path the
    # cut save renames reads back exactly at its old path or its new path
    # (stage 11, R5).
    state = log.state.get(s - 1, {})
    changed = log.changed.get(s, {})
    tree = walk(img)
    isfile = lambda q: q in tree and tree[q][1] & 0o170000 == 0o100000
    want = set(q for q in state if isfile(q))
    want |= set(renamed_to(log, s, q) for q in state if isfile(renamed_to(log, s, q)))
    got = contents(img, [tree[q][0] for q in want], work)

    def exact(q, mode, size, sha):
        if q not in tree or "%06o" % tree[q][1] != mode:
            return False
        if tree[q][1] & 0o170000 != 0o100000:
            return True
        g = got.get(tree[q][0])
        return g is not None and str(g[0]) == size and g[1] == sha

    for p, (mode, size, _, sha) in sorted(state.items()):
        if p == "/":
            continue
        kinds = changed.get(p, set())
        if "from" in kinds and "del" not in kinds:
            new = renamed_to(log, s, p)
            nkinds = changed.get(new, set())
            if exact(p, mode, size, sha) or exact(new, mode, size, sha):
                continue
            if "del" in nkinds and p not in tree and new not in tree:
                continue                        # renamed, then removed in the same save
            if "data" in nkinds and new in tree:
                continue                        # renamed, then changed in the same save
            reasons.append("%s (saved; renamed to %s in the cut save) is exactly at neither path" % (p, new))
            continue
        if kinds and "del" not in kinds:
            continue                            # changed by the cut save: may be old, new or partial
        if p not in tree:
            if not kinds:
                reasons.append("%s (saved, unchanged) is missing" % p)
            continue
        ino, dmode = tree[p]
        if "%06o" % dmode != mode:
            reasons.append("%s mode %06o, saved %s" % (p, dmode, mode))
            continue
        if dmode & 0o170000 == 0o100000:
            g = got.get(ino)
            if g is None or str(g[0]) != size or g[1] != sha:
                reasons.append("%s does not read back exactly (%s, saved size %s)" % (p, g, size))
    # The owner's case: before save 1 has finished, A is absent or exactly
    # its intended content; from save 1's last write until save 2 begins, A
    # is present (exact: the general check above); gone once save 2 has
    # finished; B never holds a block of A.
    if seq == "owner":
        s_new = next((k for k, c in sorted(log.changed.items()) if "new" in c.get("/A", set())), None)
        s_del = next((k for k, c in sorted(log.changed.items()) if "del" in c.get("/A", set())), None)
        _, last_new = log.save_writes(s_new) if s_new is not None else (None, None)
        first, last = log.save_writes(s_del) if s_del is not None else (None, None)
        if last is not None and n >= last and "/A" in tree:
            reasons.append("/A still there after save %d finished (cut after %d >= %d)" % (s_del, n, last))
        if last_new is not None and first is not None and last_new <= n < first and "/A" not in tree:
            reasons.append("/A gone between save %d's end and save %d's start" % (s_new, s_del))
        if last_new is not None and n < last_new and "/A" in tree:
            want = log.state.get(s_new, {}).get("/A")
            g = contents(img, [tree["/A"][0]], work).get(tree["/A"][0])
            if want is None or g is None or str(g[0]) != want[1] or g[1] != want[3]:
                reasons.append("/A present before save %d finished but not exactly its content (%s)" % (s_new, g))
        a_blocks = set(log.blocks.get("/A", []))
        if "/B" in tree and a_blocks:
            shared = blocks_of(img, tree["/B"][0]) & a_blocks
            if shared:
                reasons.append("/B holds blocks of /A: %s" % sorted(shared))
    return reasons, counts, accepts, raws


def sweep(exe, pristine, seq, work, san):
    t0 = time.time()
    ok, log, uncut_img = check_final(exe, pristine, seq, work, seq + " (uncut)")
    total = log.done or 0
    if not ok or total == 0:
        print("SWEEP %s: the uncut run failed; no cuts run" % seq)
        return False
    shas = {total: sha_file(uncut_img)}
    allc, bad_n, nbad, acc, first = {}, [], 0, {}, {}
    for n in range(total):
        reasons, counts, h, accepts, raws = check_cut(exe, pristine, seq, work, log, n)
        shas[n] = h
        for rule, ino, window in accepts:
            acc.setdefault((rule, window, ino), []).append(n)
        for rule, ino, text in raws:
            first.setdefault(rule, (n, ino, text))
        for k, v in counts.items():
            allc[k] = allc.get(k, 0) + v
        if reasons:
            nbad += 1
            if len(bad_n) < 6:
                bad_n.append((n, reasons[:8]))
    saves = sorted(set(log.wsave.values()))
    per = " ".join("s%d:%d-%d" % ((k,) + log.save_writes(k)) for k in saves)
    rules = " ".join("%s=%d" % (r, allc.get(r, 0)) for r in ALL_RULES)
    print("SWEEP %s: %s; writes %d, cuts N=0..%d (%d), bad %d; writes per save %s; allowed lines seen: %s; %.1f s" % (
        seq, "ok" if nbad == 0 else "BAD", total, total - 1, total, nbad, per, rules, time.time() - t0))
    for n, rs in bad_n:
        for r in rs:
            print("  N=%d: %s" % (n, r))
    # Every accepted A8-A11 (and A14) line: the inode, its path, the window.
    for (rule, window, ino), ns in sorted(acc.items(), key=lambda kv: (int(kv[0][0][1:]), kv[0][2], kv[0][1])):
        print("ACCEPT %s %s ino=%d path=%s window=%s lines=%d N=%s" % (
            seq, rule, ino, log.paths.get(ino, "?"), window.replace(" ", "_"), len(ns),
            ",".join(str(x) for x in ns[:12]) + (",..." if len(ns) > 12 else "")))
    # The first raw e2fsck text of each allowed line that appeared (verbatim).
    for rule in ALL_RULES:
        if rule in first:
            n, ino, text = first[rule]
            print("FIRST %s %s N=%d ino=%s | %s" % (seq, rule, n, "-" if ino is None else ino, text))
    good = nbad == 0
    if san:
        t1 = time.time()
        sbad = []
        for n in range(total + 1):
            img = os.path.join(work, "san.img")
            shutil.copyfile(pristine, img)
            rc, out, err = run_tool(san, img, seq, cut=(n if n < total else None),
                                    extra_env={"ASAN_OPTIONS": "detect_leaks=1:exitcode=70",
                                               "UBSAN_OPTIONS": "halt_on_error=1:print_stacktrace=1:exitcode=71"})
            h = sha_file(img)
            if rc != 0 or "Sanitizer" in err or "runtime error" in err or h != shas[n]:
                sbad.append("N=%d exit %d image %s %s" % (n, rc, "same" if h == shas[n] else "DIFFERS",
                                                           err.strip()[:200]))
        print("SANITIZE %s: %s; %d runs (N=0..%d and uncut), images byte-identical to the plain build's: %d; %.1f s" % (
            seq, "ok" if not sbad else "BAD", total + 1, total - 1, total + 1 - len(sbad), time.time() - t1))
        for b in sbad[:6]:
            print("  " + b)
        good = good and not sbad
    return good


# ---- stage 11: the unordered sweep ----

def unit_count(writes, bs):
    """Units of a flush interval: one per filesystem block a write covers
    (the 1 KiB superblock of a larger-block volume is one unit)."""
    n = 0
    for (_, _, _, off, ln) in writes:
        pos, end = off, off + ln
        while pos < end:
            pos = min((pos // bs + 1) * bs, end)
            n += 1
    return n


def unit_masks(m):
    """The subsets of an interval's m units on the disk at a cut ('1': the
    unit landed): every proper non-empty subset for m <= 8; otherwise each
    single unit, each all-but-one, each prefix and suffix, and 64 subsets
    from a fixed-seed generator (the same every run)."""
    if m <= 8:
        return ["".join("1" if x >> i & 1 else "0" for i in range(m)) for x in range(1, (1 << m) - 1)]
    out = []
    for i in range(m):
        out.append("".join("1" if j == i else "0" for j in range(m)))
        out.append("".join("0" if j == i else "1" for j in range(m)))
    for k in range(1, m):
        out.append("1" * k + "0" * (m - k))
        out.append("0" * (m - k) + "1" * k)
    x = 0x4D61646972650B   # the fixed seed
    for _ in range(64):
        bits = []
        for _ in range(m):
            x = (x * 6364136223846793005 + 1442695040888963407) & 0xFFFFFFFFFFFFFFFF
            bits.append("1" if x >> 63 else "0")
        out.append("".join(bits))
    seen, res = set(), []
    for s in out:
        if "1" in s and "0" in s and s not in seen:
            seen.add(s)
            res.append(s)
    return res


def usweep(exe, pristine, seq, work):
    t0 = time.time()
    ok, log, _ = check_final(exe, pristine, seq, work, seq + " (uncut, for the unordered sweep)")
    if not ok or not log.intervals:
        print("UNORDERED %s: the uncut run failed; no cuts run" % seq)
        return False
    allc, bad_k, nbad, acc, first = {}, [], 0, {}, {}
    cuts = every = sampled = 0
    for k, ws in enumerate(log.intervals, start=1):
        m = unit_count(ws, log.bs)
        if m < 2:
            continue
        masks = unit_masks(m)
        every += m <= 8
        sampled += m > 8
        s, n_lo = ws[0][1], ws[0][0] - 1
        for mask in masks:
            cuts += 1
            img = os.path.join(work, "ucut.img")
            shutil.copyfile(pristine, img)
            rc, out, err = run_tool(exe, img, seq, unordered="%d:%s" % (k, mask))
            clog = Log(out)
            reasons = []
            if rc != 0 or clog.ucut != (k, mask, m) or clog.cut is None:
                reasons.append("ext2write exit %d, %s (want interval %d mask %s units %d, then CUT) %s %s" % (
                    rc, clog.ucut, k, mask, m, clog.fail or "", err.strip()[:200]))
            if any(w[1] != s for w in ws):
                reasons.append("interval %d holds writes of more than one save" % k)
            more, counts, accepts, raws = judge(img, log, seq, work, s, n_lo)
            reasons += more
            tag = "%d:%s" % (k, mask)
            for rule, ino, window in accepts:
                acc.setdefault((rule, window, ino), []).append(tag)
            for rule, ino, text in raws:
                first.setdefault(rule, (tag, ino, text))
            for r, v in counts.items():
                allc[r] = allc.get(r, 0) + v
            if reasons:
                nbad += 1
                if len(bad_k) < 6:
                    bad_k.append((tag, reasons[:8]))
    rules = " ".join("%s=%d" % (r, allc.get(r, 0)) for r in ALL_RULES)
    print("UNORDERED %s: %s; %d flush intervals, %d with 2 or more units (%d with every subset, %d sampled); "
          "%d cuts, bad %d; allowed lines seen: %s; %.1f s" % (
              seq, "ok" if nbad == 0 and cuts > 0 else "BAD", len(log.intervals), every + sampled, every, sampled,
              cuts, nbad, rules, time.time() - t0))
    for tag, rs in bad_k:
        for r in rs:
            print("  cut %s: %s" % (tag, r))
    for (rule, window, ino), tags in sorted(acc.items(), key=lambda kv: (int(kv[0][0][1:]), kv[0][2], kv[0][1])):
        print("UACCEPT %s %s ino=%d path=%s window=%s lines=%d cuts=%s" % (
            seq, rule, ino, log.paths.get(ino, "?"), window.replace(" ", "_"), len(tags),
            ",".join(tags[:6]) + (",..." if len(tags) > 6 else "")))
    for rule in ALL_RULES:
        if rule in first:
            tag, ino, text = first[rule]
            print("UFIRST %s %s cut=%s ino=%s | %s" % (seq, rule, tag, "-" if ino is None else ino, text))
    return nbad == 0 and cuts > 0


# ---- stage 11: the QEMU cut points (test=diskcut=N) ----

KWRITE = re.compile(r"^blk: write (\d+) class (\d+): partition byte (\d+), (\d+) bytes$")
DCHECK = re.compile(r"^diskcheck: /home/(\S+): (?:absent \(.*\)|(\d+) bytes, CRC32 0x([0-9a-f]{8}))$")


def model_writes(log):
    """The model's write commands before the unmount's save (the kernel's
    sequence ends with its last sync, without an unmount)."""
    last = max(log.wsave.values()) if log.wsave else None
    return [w for w in log.writes if w[1] != last]


def qmodel(exe, pristine, seq, work):
    ok, log, _ = check_final(exe, pristine, seq, work, seq + " (the QEMU cut model)")
    ws = model_writes(log)
    n1 = max((w[0] for w in ws if w[1] == 2 and w[2] == 1), default=None)
    f5 = min((w[0] for w in ws if w[1] == 2 and w[2] == 5), default=None)
    n2 = f5 - 1 if f5 else None
    per = " ".join("s%d:%d-%d" % ((k,) + log.save_writes(k)) for k in sorted(set(w[1] for w in ws)))
    print("QMODEL %s: %s; %d writes before the unmount (%s); N1=%s (the last file-data write of save 2) N2=%s (the "
          "last write before save 2's first folder write)" % (seq, "ok" if ok and n1 and n2 else "BAD", len(ws), per,
                                                               n1, n2))
    for w in ws:
        print("QWRITE %d save %d class %d off %d len %d" % w)
    return bool(ok and n1 and n2)


def serial_lines(path):
    with open(path, "rb") as f:
        return [l.decode("latin-1").rstrip("\r\n") for l in f.read().split(b"\n")]


def qcompare(work, serial):
    with open(os.path.join(work, "uncut.log")) as f:
        log = Log(f.read())
    model = [(n, c, off, ln) for (n, s, c, off, ln) in model_writes(log)]
    kern = [tuple(int(x) for x in m.groups()) for m in (KWRITE.match(l) for l in serial_lines(serial)) if m]
    if kern == model and model:
        print("QCOMPARE ok; the kernel's %d writes equal the model's (number, class, partition offset, length)" % len(kern))
        return True
    diff = next((i for i in range(min(len(kern), len(model))) if kern[i] != model[i]), min(len(kern), len(model)))
    print("QCOMPARE BAD; kernel %d writes, model %d; the first difference at index %d: kernel %s, model %s" % (
        len(kern), len(model), diff, kern[diff] if diff < len(kern) else "-", model[diff] if diff < len(model) else "-"))
    return False


def qjudge(work, seq, img, n):
    with open(os.path.join(work, "uncut.log")) as f:
        log = Log(f.read())
    s = log.wsave.get(n + 1)
    if s is None:
        print("QCUT %s N=%d: BAD; no write %d in the model" % (seq, n, n + 1))
        return False
    reasons, counts, accepts, raws = judge(img, log, seq, os.path.join(work, "qjudge"), s, n)
    first, last = log.save_writes(s)
    rules = " ".join("%s=%d" % (r, counts.get(r, 0)) for r in ALL_RULES if counts.get(r))
    print("QCUT %s N=%d: %s; inside save %d (its writes %d-%d); allowed lines seen: %s" % (
        seq, n, "ok" if not reasons else "BAD", s, first, last, rules or "none"))
    for r in reasons:
        print("  " + r)
    for rule, ino, window in accepts:
        print("QACCEPT %s N=%d %s ino=%d path=%s window=%s" % (seq, n, rule, ino, log.paths.get(ino, "?"),
                                                              window.replace(" ", "_")))
    for rule, ino, text in raws:
        print("QLINE %s N=%d %s | %s" % (seq, n, rule, text))
    return not reasons


def crc_of(img, ino, work):
    d = os.path.join(work, "qview")
    os.makedirs(d, exist_ok=True)
    out = os.path.join(d, "f")
    if os.path.exists(out):
        os.unlink(out)
    debugfs(img, "dump <%d> %s" % (ino, out))
    if not os.path.exists(out):
        return None
    with open(out, "rb") as f:
        b = f.read()
    return len(b), zlib.crc32(b) & 0xFFFFFFFF


def qview(img, serial, names):
    lines = serial_lines(serial)
    mad = {}
    for l in lines:
        m = DCHECK.match(l)
        if m:
            mad[m.group(1)] = None if m.group(2) is None else (int(m.group(2)), int(m.group(3), 16))
    tree = walk(img)
    work = os.path.dirname(os.path.abspath(img))
    ok = "diskcheck: DONE (3 names looked up)" in lines
    parts = []
    for name in names:
        p = "/" + name
        dbg = None
        if p in tree and tree[p][1] & 0o170000 == 0o100000:
            dbg = crc_of(img, tree[p][0], work)
        elif p in tree:
            dbg = "not a file"
        if name not in mad:
            ok = False
            parts.append("%s: no diskcheck line" % name)
            continue
        good = mad[name] == dbg
        ok = ok and good
        fmt = lambda v: "absent" if v is None else v if isinstance(v, str) else "%d bytes CRC32 0x%08x" % v
        parts.append("%s: Madire %s, debugfs %s%s" % (name, fmt(mad[name]), fmt(dbg), "" if good else " (DIFFER)"))
    print("QVIEW %s; %s" % ("ok" if ok else "BAD", "; ".join(parts)))
    return ok


def fsckstrict(img):
    frc, flines = fsck(img)
    counts, bad, _, _ = classify(flines, {"strict": True, "t_cut": set(), "t_before": set(), "r3_before": set(),
                                          "r3_dir_before": set(), "moved_cut": set()})
    ok = not bad and frc in (0, 4)
    rules = " ".join("%s=%d" % (r, counts.get(r, 0)) for r in ALL_RULES if counts.get(r))
    print("FSCK %s; e2fsck exit %d; allowed lines seen: %s" % ("ok" if ok else "BAD", frc, rules or "none"))
    for b in bad:
        print("  not allowed: " + b)
    for line in flines:
        print("  e2fsck: " + line)
    return ok


def readonly(exe, pristine, work, tag):
    os.makedirs(work, exist_ok=True)
    img = os.path.join(work, "ro.img")
    shutil.copyfile(pristine, img)
    before = sha_file(img)
    rc, out, err = run_tool(exe, img, "readonly")
    after = sha_file(img)
    mount = next((l for l in out.splitlines() if l.startswith("MOUNT")), "MOUNT ?")
    ok = rc == 0 and before == after and "O create /refused -> EROFS" in out
    print("READONLY %s: %s; %s; create -> %s; image %s" % (
        tag, "ok" if ok else "BAD", mount, "EROFS" if "-> EROFS" in out else "?",
        "unchanged" if before == after else "CHANGED"))
    return ok


def main():
    a = sys.argv[1:]
    if len(a) >= 5 and a[0] == "final":
        ok, _, _ = check_final(a[1], a[2], a[3], a[4], a[3])
        sys.exit(0 if ok else 1)
    if len(a) >= 5 and a[0] == "sweep":
        san = a[6] if len(a) >= 7 and a[5] == "--san" else None
        sys.exit(0 if sweep(a[1], a[2], a[3], a[4], san) else 1)
    if len(a) >= 4 and a[0] == "readonly":
        sys.exit(0 if readonly(a[1], a[2], a[3], os.path.basename(a[2])) else 1)
    if len(a) >= 5 and a[0] == "usweep":
        os.makedirs(a[4], exist_ok=True)
        sys.exit(0 if usweep(a[1], a[2], a[3], a[4]) else 1)
    if len(a) >= 5 and a[0] == "qmodel":
        os.makedirs(a[4], exist_ok=True)
        sys.exit(0 if qmodel(a[1], a[2], a[3], a[4]) else 1)
    if len(a) >= 3 and a[0] == "qcompare":
        sys.exit(0 if qcompare(a[1], a[2]) else 1)
    if len(a) >= 5 and a[0] == "qjudge":
        sys.exit(0 if qjudge(a[1], a[2], a[3], int(a[4])) else 1)
    if len(a) >= 4 and a[0] == "qview":
        sys.exit(0 if qview(a[1], a[2], a[3:]) else 1)
    if len(a) >= 2 and a[0] == "fsckstrict":
        sys.exit(0 if fsckstrict(a[1]) else 1)
    raise SystemExit(__doc__)


if __name__ == "__main__":
    main()
