#!/usr/bin/env python3
"""Madire OS 1.4 stage 13: the VirtualBox storage group's fixed data
(docs/plans/1.4.md, C13, G6; docs/testing.md, "In 1.4 (stage 13)").

One source for what the marked test disk holds and what the storage
group's session leaves on it, used on Kali (scripts/kali-disk.sh: the
marked image and the check of a used disk) and on Windows
(scripts/vbox-acceptance.ps1 -Only storage: the session's check and the
reference screens). Standard library only; run it with python3 -I (on
Windows python -I).

  vbox-storage.py seed <dir>        the tree the marked image is made from
                                    (mke2fs -d): /seed with three files
  vbox-storage.py expected <dir>    the tree after the storage group's
                                    first boot: the home folders (D18) and
                                    the session's files, as its commands
                                    leave them (scripts/vbox-acceptance.ps1)
  vbox-storage.py plan <w> <h>      the storage group's events, one boot's
                                    (MOUSE dx dy buttons, KEYS scancodes,
                                    TYPE text, SHOT name, SESSION, RESTART),
                                    then "MASK x y w h" (the clock's cells)
  vbox-storage.py render <w> <h> <outdir> <usable MB>
                                    each SHOT's reference screen in Disk
                                    mode, <outdir>/storage-<name>.rgb

seed and expected refuse a folder that exists and is not empty; each
prints one line per folder ("DIR <path>") and file ("FILE <path> <size>
<sha256>"), sorted. plan and render reuse tests/screen_ref.py's model of
the desktop (its vbox_session, the VirtualBox desktop group's), loaded by
path and given this group's steps; tests/ is not changed. The session's
own commands are typed by scripts/vbox-acceptance.ps1 at the SESSION line.

Any error prints "vbox-storage: ERROR: ..." and exits 1.
"""
import hashlib
import importlib.util
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
TESTS = os.path.join(HERE, "..", "tests")

# The marked image (scripts/kali-disk.sh image): mke2fs -d of this tree.
# big.txt needs the single indirect block at 1 KiB blocks (13,720 bytes, 14
# blocks); the session copies it and appends it to the copy (27,440 bytes).
BIG = "".join("big %04d: abcdefghijklmnopqrstuvwxyz0123456789ABCD\n" % i for i in range(1, 281)).encode("ascii")
NOTES = "".join("note %02d: the marked VirtualBox disk (stage 13)\n" % i for i in range(1, 41)).encode("ascii")
SEED_FILES = {
    "seed/big.txt": BIG,
    "seed/notes.txt": NOTES,
    "seed/old.txt": b"old\n",
}
SEED_DIRS = ["seed"]

# The home folders the first read-write mount makes (D18, C11).
HOME = ["Desktop", "Documents", "Downloads", "Pictures", "Music", "Videos", "Trash"]

# What the storage group's first-boot session leaves (its commands, in
# scripts/vbox-acceptance.ps1, from /home):
#   mkdir user/Documents/work
#   echo first line > user/Documents/work/one.txt
#   echo second line >> user/Documents/work/one.txt
#   cp -y seed/big.txt user/Documents/big2.txt
#   cat seed/big.txt >> user/Documents/big2.txt      (through the single indirect block)
#   echo picture > user/Pictures/p.txt
#   mv -y user/Pictures/p.txt user/Desktop/p-moved.txt     (between folders)
#   mv -y seed/notes.txt user/Documents/notes.txt          (between folders)
#   mkdir user/Music/gone ; rmdir user/Music/gone
#   rm -y seed/old.txt
#   time sync
#   echo after sync > user/Documents/work/late.txt         (saved by Restart, or the automatic save)
EXPECTED_DIRS = ["seed", "user"] + ["user/" + d for d in HOME] + ["user/Documents/work"]
EXPECTED_FILES = {
    "seed/big.txt": BIG,
    "user/Documents/work/one.txt": b"first line\nsecond line\n",
    "user/Documents/work/late.txt": b"after sync\n",
    "user/Documents/big2.txt": BIG + BIG,
    "user/Documents/notes.txt": NOTES,
    "user/Desktop/p-moved.txt": b"picture\n",
}

# One boot's events (tests/screen_ref.py's step names): the desktop at
# System ready; the pointer parked and moved; the M menu's About (the Mode
# row says Disk mode); closed; a key opens the Console window; SESSION (the
# group types its commands); clear; the Restart dialog ("Files in /home
# are saved first."), its Restart button. The same steps run in both boots
# (each starts with a fresh pointer at the screen's centre).
STEPS = [
    "shot:ready", "park", "rel:127,100",
    "to:m", "click", "to:row@About", "click", "shot:about", "to:close", "click",
    "to:desktop", "key:spc", "shot:session", "type:clear", "key:ret", "shot:console-cleared",
    "to:m", "click", "to:row@Restart", "click", "shot:restart-dialog",
    "to:btn@Restart", "click", "restart",
]


def listing(top):
    out = []
    for base, dirs, files in os.walk(top):
        rel = os.path.relpath(base, top)
        pre = "" if rel == "." else "/" + rel.replace(os.sep, "/")
        for d in dirs:
            out.append("DIR %s/%s" % (pre, d))
        for f in files:
            with open(os.path.join(base, f), "rb") as fh:
                data = fh.read()
            out.append("FILE %s/%s %d %s" % (pre, f, len(data), hashlib.sha256(data).hexdigest()))
    return sorted(out)


def make(top, dirs, files):
    if os.path.exists(top) and os.listdir(top):
        raise ValueError("%s exists and is not empty" % top)
    os.makedirs(top, exist_ok=True)
    for d in dirs:
        os.makedirs(os.path.join(top, *d.split("/")), exist_ok=True)
    for p, data in files.items():
        with open(os.path.join(top, *p.split("/")), "wb") as fh:
            fh.write(data)
    for line in listing(top):
        print(line, flush=True)


def screen_ref():
    """tests/screen_ref.py, loaded by path, with this group's steps."""
    sys.dont_write_bytecode = True
    spec = importlib.util.spec_from_file_location("screen_ref", os.path.join(TESTS, "screen_ref.py"))
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    mod.VBOX_DESKTOP = list(STEPS)
    return mod


def main():
    a = sys.argv[1:]
    try:
        if len(a) == 2 and a[0] == "seed":
            make(a[1], SEED_DIRS, SEED_FILES)
            return 0
        if len(a) == 2 and a[0] == "expected":
            make(a[1], EXPECTED_DIRS, EXPECTED_FILES)
            return 0
        if len(a) == 3 and a[0] == "plan":
            S = screen_ref()
            w, h = int(a[1]), int(a[2])
            for line in S.vbox_session(w, h):
                print("SESSION" if line == "SHOT session" else line, flush=True)
            print("MASK %d %d %d %d (the clock's cells)" % S.clock_mask(w, h), flush=True)
            return 0
        if len(a) == 5 and a[0] == "render":
            S = screen_ref()
            w, h, outdir, mb = int(a[1]), int(a[2]), a[3], int(a[4])
            S.desktop_mod().set_disk_mode("rw")      # the About Mode row and the Restart line of Disk mode (C10)
            about = (S.kernel_version(), S.limine_name(), b"%dx%dx32" % (w, h), b"%d MB" % mb)

            def save(name, pix):
                if name == "session":
                    return
                with open(os.path.join(outdir, "storage-%s.rgb" % name), "wb") as f:
                    f.write(pix)
                print("REFERENCE storage-%s.rgb %dx%d (the storage group, Disk mode; tests/desktop_ref.py)" % (name, w, h),
                      flush=True)
            S.vbox_session(w, h, about, save)
            print("MASK %d %d %d %d (the clock's cells)" % S.clock_mask(w, h), flush=True)
            return 0
        print(__doc__)
        return 1
    except Exception as e:                   # reported as a failure by the caller
        print("vbox-storage: ERROR: %s" % e)
        return 1


if __name__ == "__main__":
    sys.exit(main())
