#!/usr/bin/env python3
"""Reference screens for the VirtualBox graphics group (1.2 stage 10b;
standard library only, so it also runs on the Windows host).

    screen_ref.py splash WIDTH HEIGHT OUTDIR
    screen_ref.py console WIDTH HEIGHT OUT
    screen_ref.py check-console SOCKET

splash: writes the splash's five screens as raw RGB files (3 bytes a
pixel, rows top to bottom), rendered by scripts/make-wallpaper.py, the one
reference algorithm: OUTDIR/splash-dots0.rgb ... splash-dots3.rgb (that
many of its dots lit) and OUTDIR/splash-ready.rgb (its ready state at
System ready).

console: writes the screen of the framebuffer console right after the
command "clear" as a raw RGB file: tests/fbcon_ref.py's rendering of the
cell grid the console then holds (console_cleared below: the prompt
"madire> " on row 0 in light cyan, the input cells after it in white, the
cursor at row 0, column 8, every other cell blank), with the BIOS's 16
colours (the console uses only slots the boot logo never reprograms).

check-console: the same reference against a running QEMU guest's screen,
over QMP (one screendump; the guest must be showing the console just after
"clear"). Prints "CONSOLE size=<w>x<h> differ=<pixels> (...)"; the
QEMU check that this reference is the kernel's screen, so the VirtualBox
group's comparison with it is a comparison with the kernel's rendering.

1.3 stage 8, the switch (the real ISO shows the desktop at System ready):

    screen_ref.py desktop WIDTH HEIGHT OUT
    screen_ref.py desktop-console WIDTH HEIGHT OUT
    screen_ref.py check-desktop SOCKET CLOCK_PHYS
    screen_ref.py check-desktop-console SOCKET CLOCK_PHYS
    screen_ref.py desktop-plan WIDTH HEIGHT
    screen_ref.py desktop-render WIDTH HEIGHT OUTDIR USABLE_MB

desktop, desktop-console: the VirtualBox graphics group's references
(tests/desktop_ref.py): the desktop at System ready, and the desktop with
the Console window (opened by a key, focused) right after "clear", both
with the clock "00:00"; each also prints "MASK x y w h", the clock's
cells, which VirtualBox masks (decision 12; its time is not known there).
check-desktop, check-desktop-console: the same against a running QEMU
guest's screen over QMP, with no mask: the clock is read from the
kernel's own string (desktop.c's clock, at physical CLOCK_PHYS from
madire.elf) while the guest is stopped for the screendump (the real ISO
logs no clock; the stage 8 decision Q3); one retry, only if the clock
string read again has changed. Prints "DESKTOP size=<w>x<h> differ=<n>
clock=<HH:MM> tries=<k> (...)" or "DESKTOP_CONSOLE ...".
desktop-plan, desktop-render: the VirtualBox desktop group (VBOX_DESKTOP
below): the events to send (MOUSE dx dy buttons, KEYS scancodes, TYPE
text, SHOT name, RESTART), and after the run each shot's reference as
OUTDIR/desktop-<name>.rgb, the About window's values from kernel/kernel.h,
scripts/install-deps.sh (Limine's pinned version) and the run's usable MB.

Each prints one line per file or result; any error prints
"screen_ref: ERROR: ..." and exits 1.
"""
import importlib.util
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
# No __pycache__/ from these imports (tests/ and scripts/ are files under test).
sys.dont_write_bytecode = True
import fbcon_ref  # noqa: E402

FONTDIR = os.path.join(HERE, "..", "third_party", "terminus")

# drivers/vga/vga_text.c's vga_shadow palette: the BIOS's 16 text colours
# (6-bit DAC values).
BIOS_PALETTE = [
    (0x00, 0x00, 0x00), (0x00, 0x00, 0x2A), (0x00, 0x2A, 0x00), (0x00, 0x2A, 0x2A),
    (0x2A, 0x00, 0x00), (0x2A, 0x00, 0x2A), (0x2A, 0x15, 0x00), (0x2A, 0x2A, 0x2A),
    (0x15, 0x15, 0x15), (0x15, 0x15, 0x3F), (0x15, 0x3F, 0x15), (0x15, 0x3F, 0x3F),
    (0x3F, 0x15, 0x15), (0x3F, 0x15, 0x3F), (0x3F, 0x3F, 0x15), (0x3F, 0x3F, 0x3F),
]
# kernel/console/console.c and lineedit.h: the attributes, the prompt's
# width and the input line's length.
ATTR_TEXT, ATTR_INPUT, ATTR_PROMPT = 0x07, 0x0F, 0x0B
PROMPT = "madire> "
LINE_MAX = 71


def console_cleared():
    """The vga_shadow bytes (fbcon_ref.SHADOW_SIZE) of the console right
    after "clear": cells, palette, cursor (shown, row, column)."""
    cells = [ord(" ") | ATTR_TEXT << 8] * 2000
    for i, ch in enumerate(PROMPT):
        cells[i] = ord(ch) | ATTR_PROMPT << 8
    for i in range(len(PROMPT), len(PROMPT) + LINE_MAX):
        cells[i] = ord(" ") | ATTR_INPUT << 8
    shadow = struct.pack("<2000H", *cells) + bytes(v for rgb in BIOS_PALETTE for v in rgb)
    return shadow + bytes([1, 0, len(PROMPT)])


# ---- 1.3 stage 8: the desktop on the real ISO (the default path) ----

def desktop_mod():
    import desktop_ref  # noqa: E402
    return desktop_ref


def clock_mask(w, h):
    """The taskbar clock's cells (x, y, w, h): masked in VirtualBox, where
    the clock's time is not known (decision 12)."""
    D = desktop_mod()
    L = D.layout(w, h)
    return (L["clock"][0], L["clock"][1], 5 * L["font"][1], L["font"][2])


def console_window_desktop(w, h, clock, pointer_at=None):
    """The desktop with the Console window opened by a key from no focus
    (tests/desktop_ref.py's WM), focused, holding the console's grid right
    after "clear"; the pointer drawn at pointer_at if given."""
    D = desktop_mod()
    s = D.scale_of(w, h)
    wm = D.WM(w, h, s)
    wm.key(D.console_size(s), True, False)
    wm.key(D.console_size(s), False, False)
    shown = [{"r": x["r"], "active": x is wm.focus, "close": x["close"], "values": None,
              "grid": D.shadow_grid(console_cleared())} for x in wm.z]
    pix = D.desktop_windows(w, h, clock, shown)
    if pointer_at is not None:
        import pointer_ref  # noqa: E402
        pix, _ = pointer_ref.paint(pix, w, h, *pointer_at)
    return pix, "the Console window (%d, %d) %dx%d, focused, after clear" % tuple(wm.z[-1]["r"])


def guest_clock(qmp, phys):
    """desktop.c's clock string (6 bytes, "HH:MM" and its NUL) at physical
    address phys, read while the guest is stopped."""
    mem = os.path.abspath(qmp_dump_base + ".clock")
    if os.path.exists(mem):
        os.remove(mem)
    qmp.command("pmemsave", val=int(phys, 16), size=6, filename=mem)
    raw = open(mem, "rb").read()
    os.remove(mem)
    return raw[:5]


def check_desktop(sock, phys, with_console):
    """One screen of a default-path desktop (the desktop alone, or with the
    Console window after clear) against its reference, the clock read from
    the kernel's memory at the same stop (no mask). One retry, only when the
    clock string read again differs (the minute changed)."""
    from qmp_menu import Qmp  # noqa: E402
    D = desktop_mod()
    qmp = Qmp(sock)
    global qmp_dump_base
    qmp_dump_base = sock
    dump = sock + ".ppm"
    tries, last = 0, None
    while True:
        tries += 1
        qmp.command("stop")
        clock = guest_clock(qmp, phys)
        w, h, pix = qmp.screendump(dump)
        qmp.command("cont")
        os.remove(dump)
        if with_console:
            want, desc = console_window_desktop(w, h, clock)
        else:
            want, desc = D.desktop(w, h, clock), "the desktop alone"
        differ, first = fbcon_ref.compare(pix, want, w)
        if differ == 0 or tries == 2:
            break
        qmp.command("stop")
        again = guest_clock(qmp, phys)
        qmp.command("cont")
        if again == clock:
            break                           # the clock did not change: the difference stands
        last = clock
    extra = "" if first is None else ", first at %d,%d: screen %s, reference %s" % first
    if last is not None:
        extra += ", retried: the clock changed from %s" % last.decode("latin-1")
    return "size=%dx%d differ=%d clock=%s tries=%d (%s%s)" % (w, h, differ, clock.decode("latin-1"), tries, desc,
                                                          extra)


qmp_dump_base = ""

# The VirtualBox desktop group (scripts/vbox-acceptance.ps1, stage 9): one
# normal boot of the real ISO, these steps, each screenshot against its
# reference with the clock's cells masked. The pointer moves only by
# relative events (IMouse.PutMouseEvent; parked first by moves the clamp
# stops at 0, 0), at most 127 each (one PS/2 packet). The steps' places come
# from tests/desktop_ref.py's model (WM, Menus), the one tests/qmp_desktop.py
# proves against the kernel in QEMU; a Console window is shot only right
# after "clear" (its grid known without vga_shadow).
VBOX_DESKTOP = [
    "shot:ready", "park", "rel:127,100", "shot:pointer",
    "to:m", "click", "shot:menu", "to:row@About", "click", "shot:about",
    "to:title", "drag:-120,-60", "shot:about-dragged", "to:close", "click", "shot:about-closed",
    "key:meta_l", "shot:winkey-menu", "key:esc", "shot:winkey-closed",
    "to:desktop", "key:spc", "type:clear", "key:ret", "shot:console-cleared",
    "type:version", "key:ret", "type:clear", "key:ret",
    "to:desktop", "rpress", "rrelease", "shot:rightclick-menu", "key:esc",
    "to:body@Console", "click", "key:meta_l", "shot:winkey-console", "key:esc",
    "type:echo ok", "key:ret", "type:clear", "key:ret",
    "to:m", "click", "to:row@Restart", "click", "shot:restart-dialog",
    "to:btn@Cancel", "click", "shot:restart-cancelled",
    "to:m", "click", "to:row@Restart", "click", "to:btn@Restart", "click", "restart",
]
SCANCODES = {"meta_l": "e0 5b e0 db", "esc": "01 81", "spc": "39 b9", "ret": "1c 9c"}


def vbox_session(w, h, about=None, on_shot=None):
    """Replays VBOX_DESKTOP through the model. Returns the plan's lines
    (MOUSE dx dy buttons / KEYS scancodes / TYPE text / SHOT name /
    RESTART); on_shot(name, pixels) for each shot when `about` (the About
    window's values: version, loader, display, memory as bytes) is given."""
    from qmp_desktop import Windows  # noqa: E402
    from qmp_keys import text_to_actions  # noqa: E402
    D = desktop_mod()
    win = Windows((w, h), False)
    x, y, moved, held = w // 2, h // 2, False, 0
    plan = []

    def ev(dx, dy, down=False, up=False, rdown=False, rup=False):
        """One packet: a move, or a button's edge (the buttons held are
        sent with every packet; the model sees the edges, as from
        tests/qmp_desktop.py's packets)."""
        nonlocal x, y, moved, held
        x, y = min(max(x + dx, 0), w - 1), min(max(y + dy, 0), h - 1)
        if dx or dy:
            moved = True
        held = (held | (1 if down else 0) | (2 if rdown else 0)) & ~((1 if up else 0) | (2 if rup else 0))
        plan.append("MOUSE %d %d %d" % (dx, dy, held))
        win.menus.pointer(x, y, down, up, rdown)

    def rel(dx, dy):
        while dx or dy:
            sx, sy = max(-127, min(127, dx)), max(-127, min(127, dy))
            ev(sx, sy)
            dx, dy = dx - sx, dy - sy

    def keys(qcodes):
        for k in qcodes:
            win.key(k, True)
        for k in qcodes:
            win.key(k, False)

    def render():
        shown = []
        values = D.about_values(win.s, *about) if any(z["name"] == "About" for z in win.wm.z) else None
        for z in win.wm.z:
            one = {"r": z["r"], "active": z is win.wm.focus, "close": z["close"], "values": values}
            if z["name"] == "Console":
                one["grid"] = D.shadow_grid(console_cleared())
            shown.append(one)
        pix = D.desktop_windows(w, h, b"00:00", shown, win.menus.view())
        if moved:
            import pointer_ref  # noqa: E402
            pix, _ = pointer_ref.paint(pix, w, h, x, y)
        return pix

    for step in VBOX_DESKTOP:
        if step.startswith("shot:"):
            plan.append("SHOT " + step[5:])
            if about is not None and on_shot:
                on_shot(step[5:], render())
        elif step == "park":
            rel(-(w + 127), -(h + 127))
        elif step.startswith("rel:"):
            rel(*(int(v) for v in step[4:].split(",")))
        elif step.startswith("to:"):
            tx, ty = win.target(step[3:])
            rel(tx - x, ty - y)
        elif step == "click":
            ev(0, 0, down=True)
            ev(0, 0, up=True)
        elif step == "rpress":
            ev(0, 0, rdown=True)
        elif step == "rrelease":
            ev(0, 0, rup=True)
        elif step.startswith("drag:"):
            dx, dy = (int(v) for v in step[5:].split(","))
            ev(0, 0, down=True)
            if win.wm.drag is None:
                raise RuntimeError("drag: the press was not on a title bar (model)")
            rel(dx, dy)
            ev(0, 0, up=True)
        elif step.startswith("key:"):
            plan.append("KEYS " + SCANCODES[step[4:]])
            keys([step[4:]])
        elif step.startswith("type:"):
            plan.append("TYPE " + step[5:])
            for kind, value in text_to_actions(step[5:]):
                keys(value)
        elif step == "restart":
            plan.append("RESTART")
        else:
            raise ValueError("unknown step %r" % step)
    return plan


def limine_name():
    """The boot loader's name the About window shows: "Limine <the pinned
    version>" (scripts/install-deps.sh, LIMINE_VERSION)."""
    with open(os.path.join(HERE, "..", "scripts", "install-deps.sh")) as f:
        for line in f:
            if line.startswith("LIMINE_VERSION="):
                return b"Limine " + line.strip().split("=", 1)[1].encode("ascii")
    raise ValueError("no LIMINE_VERSION in scripts/install-deps.sh")


def kernel_version():
    import re  # noqa: E402
    with open(os.path.join(HERE, "..", "kernel", "kernel.h")) as f:
        m = re.search(r'^#define MADIRE_OS_VERSION "([0-9.]+)"$', f.read(), re.M)
    if not m:
        raise ValueError("no MADIRE_OS_VERSION in kernel/kernel.h")
    return m.group(1).encode("ascii")


def load_make_wallpaper():
    spec = importlib.util.spec_from_file_location("make_wallpaper",
                                                  os.path.join(HERE, "..", "scripts", "make-wallpaper.py"))
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def main():
    a = sys.argv[1:]
    try:
        if len(a) == 4 and a[0] == "splash":
            w, h, out = int(a[1]), int(a[2]), a[3]
            phases = load_make_wallpaper().splash_phases(w, h, [0, 1, 2, 3, "ready"], FONTDIR)
            for p, pix in phases.items():
                name = "splash-ready.rgb" if p == "ready" else f"splash-dots{p}.rgb"
                with open(os.path.join(out, name), "wb") as f:
                    f.write(pix)
                print(f"REFERENCE {name} {w}x{h} ({'its ready state' if p == 'ready' else f'{p} of the 3 dots lit'}; "
                      f"scripts/make-wallpaper.py)", flush=True)
            return 0
        if len(a) == 4 and a[0] == "console":
            w, h, out = int(a[1]), int(a[2]), a[3]
            pix, desc = fbcon_ref.render(FONTDIR, console_cleared(), w, h)
            with open(out, "wb") as f:
                f.write(pix)
            print(f"REFERENCE {os.path.basename(out)} {w}x{h} (the console after clear: {desc}; tests/fbcon_ref.py)",
                  flush=True)
            return 0
        if len(a) == 4 and a[0] in ("desktop", "desktop-console"):      # 1.3 stage 8
            w, h, out = int(a[1]), int(a[2]), a[3]
            if a[0] == "desktop":
                pix, desc = desktop_mod().desktop(w, h, b"00:00"), "the desktop at System ready"
            else:
                pix, desc = console_window_desktop(w, h, b"00:00")
            with open(out, "wb") as f:
                f.write(pix)
            print(f"REFERENCE {os.path.basename(out)} {w}x{h} ({desc}; tests/desktop_ref.py)", flush=True)
            print("MASK %d %d %d %d (the clock's cells)" % clock_mask(w, h), flush=True)
            return 0
        if len(a) == 3 and a[0] in ("check-desktop", "check-desktop-console"):      # 1.3 stage 8
            out = check_desktop(a[1], a[2], a[0] == "check-desktop-console")
            print(("DESKTOP_CONSOLE " if a[0] == "check-desktop-console" else "DESKTOP ") + out, flush=True)
            return 0
        if len(a) == 3 and a[0] == "desktop-plan":                      # 1.3 stage 8
            for line in vbox_session(int(a[1]), int(a[2])):
                print(line, flush=True)
            print("MASK %d %d %d %d (the clock's cells)" % clock_mask(int(a[1]), int(a[2])), flush=True)
            return 0
        if len(a) == 5 and a[0] == "desktop-render":                    # 1.3 stage 8
            w, h, outdir, mb = int(a[1]), int(a[2]), a[3], int(a[4])
            about = (kernel_version(), limine_name(), b"%dx%dx32" % (w, h), b"%d MB" % mb)

            def save(name, pix):
                with open(os.path.join(outdir, "desktop-%s.rgb" % name), "wb") as f:
                    f.write(pix)
                print(f"REFERENCE desktop-{name}.rgb {w}x{h} (the VirtualBox desktop group; tests/desktop_ref.py)",
                      flush=True)
            vbox_session(w, h, about, save)
            print("MASK %d %d %d %d (the clock's cells)" % clock_mask(w, h), flush=True)
            return 0
        if len(a) == 2 and a[0] == "check-console":
            from qmp_menu import Qmp  # noqa: E402
            qmp = Qmp(a[1])
            dump = a[1] + ".ppm"
            w, h, pix = qmp.screendump(dump)
            os.remove(dump)
            want, desc = fbcon_ref.render(FONTDIR, console_cleared(), w, h)
            differ, first = fbcon_ref.compare(pix, want, w)
            extra = "" if first is None else ", first at %d,%d: screen %s, reference %s" % first
            print(f"CONSOLE size={w}x{h} differ={differ} (the console after clear: {desc}{extra})", flush=True)
            return 0
        print(__doc__)
        return 1
    except Exception as e:                   # reported as a test failure by the caller
        print(f"screen_ref: ERROR: {e}")
        return 1


if __name__ == "__main__":
    sys.exit(main())
