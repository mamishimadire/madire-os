#!/usr/bin/env python3
"""Stall capture (0.11, a test-harness diagnostic; it decides no check).

Reads a QEMU guest's state through its HMP monitor socket: three samples,
one second apart, of "info registers", "info pic", "info irq" and
"info lapic", and kernel variables read from guest memory at their
symbols' addresses (the kernel's tick count, preempt_count_value,
preempt_resched_pending, the scheduler's current thread and need_resched).
Writes them in full to OUT, and prints one summary line: where the CPU was
(RIP resolved with nm against ELF), RSP, RFLAGS with IF, CR3, CS, HLT, the
kernel variables, the 8259's pic0 registers, the local APIC (SVR, LINT0,
LINT1, TPR, ISR, IRR) and how far IRQ 0 (the timer line) advanced.

    python3 tests/qemu_capture.py SOCKET OUT ELF LABEL
    python3 tests/qemu_capture.py --irq1 SOCKET OUT ELF   (IRQ 1 counts only)
"""
import os
import re
import socket
import struct
import subprocess
import sys
import time

ANSI = re.compile(r"\x1b\[[0-9;]*[A-Za-z]")

# struct sched (kernel/proc/sched_core.h): 64 thread entries of 56 bytes,
# then current (int32) at 3584 and need_resched (bool) at 3596 (checked
# with offsetof on the host, the same in 0.10 and 0.11).
KVARS = (("ticks", "ticks", 0, "g"),
         ("preempt_count", "preempt_count_value", 0, "w"),
         ("resched_pending", "preempt_resched_pending", 0, "b"),
         ("current", "sched", 3584, "w"),
         ("need_resched", "sched", 3596, "b"))


def hmp(sock, cmd, timeout=5.0):
    sock.sendall((cmd + "\n").encode())
    data = b""
    end = time.time() + timeout
    while time.time() < end:
        try:
            chunk = sock.recv(65536)
        except socket.timeout:
            continue
        if not chunk:
            break
        data += chunk
        if data.rstrip().endswith(b"(qemu)"):
            break
    text = ANSI.sub("", data.decode(errors="replace")).replace("\r", "")
    lines = [l for l in text.split("\n") if l.strip() not in ("(qemu)", "") and cmd not in l]
    return "\n".join(lines)


def nm(elf, *flags):
    try:
        return subprocess.run(["nm"] + list(flags) + [elf], capture_output=True, text=True,
                              timeout=20).stdout
    except Exception:
        return ""


def code_symbols(elf):
    syms = []
    for line in nm(elf, "-n").splitlines():
        parts = line.split()
        if len(parts) == 3 and parts[1].lower() in "tw":
            syms.append((int(parts[0], 16), parts[2]))
    return syms


def data_symbols(elf):
    d = {}
    for line in nm(elf).splitlines():
        parts = line.split()
        if len(parts) == 3:
            d[parts[2]] = int(parts[0], 16)
    return d


def resolve(syms, addr):
    best = None
    for a, name in syms:
        if a <= addr:
            best = (a, name)
        else:
            break
    if not best or addr - best[0] > 0x10000:
        return "user or unknown" if addr < 0x800000000000 else "unknown"
    return "%s+0x%x" % (best[1], addr - best[0])


def read_kvars(sock, dsyms):
    vals = []
    for label, sym, off, size in KVARS:
        if sym not in dsyms:
            vals.append("%s=-" % label)
            continue
        out = hmp(sock, "x /1%sx 0x%x" % (size, dsyms[sym] + off))
        m = re.search(r":\s*0x([0-9a-fA-F]+)", out)
        vals.append("%s=%s" % (label, int(m.group(1), 16) if m else "?"))
    return " ".join(vals)


SCHED_STATES = ("free", "new", "ready", "running", "blocked", "zombie")
PROC_STATES = ("free", "alive", "zombie")


def memsave(sock, addr, size, path=None):
    """Guest virtual memory (under the CPU's current CR3; the kernel half
    is the same in every address space), read with HMP "x /Ngx" in chunks
    of 64 quadwords (HMP memsave refuses these addresses on QEMU 11)."""
    del path                        # kept for the callers; no file is written
    words = (size + 7) // 8
    out = bytearray()
    while len(out) < words * 8:
        a = addr + len(out)
        n = min(64, words - len(out) // 8)
        text = hmp(sock, "x /%dgx 0x%x" % (n, a))
        vals = []
        for line in text.splitlines():
            m = re.match(r"\s*([0-9a-fA-F]+):((?:\s+0x[0-9a-fA-F]+)+)\s*$", line)
            if m:
                vals += [int(v, 16) for v in m.group(2).split()]
        if len(vals) != n:
            raise ValueError("x /%dgx 0x%x gave %d values" % (n, a, len(vals)))
        for v in vals:
            out += struct.pack("<Q", v)
    return bytes(out[:size])


def u32(b, o):
    return struct.unpack_from("<I", b, o)[0]


def i32(b, o):
    return struct.unpack_from("<i", b, o)[0]


def u64(b, o):
    return struct.unpack_from("<Q", b, o)[0]


def cstr(b, o, n):
    return b[o:o + n].split(b"\0")[0].decode(errors="replace")


def read_tables(sock, dsyms, tmp):
    """0.12 stage 2b: the thread table, the process table and the live
    PML4 table, decoded with the layouts the kernel publishes
    (thread_capture_layout, process_capture_layout in kernel/proc/).
    Returns (detail text, one summary part)."""
    if "thread_capture_layout" not in dsyms or "process_capture_layout" not in dsyms:
        return "tables: not published by this kernel", "tables -"
    tl = struct.unpack("<10Q", memsave(sock, dsyms["thread_capture_layout"], 80, tmp))
    t_addr, t_size, t_n, o_name, o_slot, o_cr3, o_pid, s_addr, st_size, o_state = tl
    pl = struct.unpack("<12Q", memsave(sock, dsyms["process_capture_layout"], 96, tmp))
    (p_addr, p_size, p_n, op_state, op_pid, op_tid, op_name, op_kill,
     sp_addr, sp_size, osp_pml4, sot_addr) = pl
    threads = memsave(sock, t_addr, t_size * t_n, tmp)
    sched_t = memsave(sock, s_addr, st_size * t_n, tmp)
    procs = memsave(sock, p_addr, p_size * p_n, tmp)
    spaces = memsave(sock, sp_addr, sp_size * p_n, tmp)
    slot_of = memsave(sock, sot_addr, t_n, tmp)
    lines, tparts, pparts = [], [], []
    for tid in range(t_n):
        st = u32(sched_t, tid * st_size + o_state)
        if st == 0:
            continue
        b = tid * t_size
        sname = SCHED_STATES[st] if st < len(SCHED_STATES) else str(st)
        line = "thread %d '%s' %s slot %d cr3 0x%x pid %d process-slot %d" % (
            tid, cstr(threads, b + o_name, 16), sname, i32(threads, b + o_slot),
            u64(threads, b + o_cr3), i32(threads, b + o_pid), slot_of[tid] - 1)
        lines.append(line)
        tparts.append("%d:%s/cr3=%x/pid=%d" % (tid, sname, u64(threads, b + o_cr3),
                                               i32(threads, b + o_pid)))
    for slot in range(p_n):
        b = slot * p_size
        st = u32(procs, b + op_state)
        if st == 0:
            continue
        sname = PROC_STATES[st] if st < len(PROC_STATES) else str(st)
        pml4 = u64(spaces, slot * sp_size + osp_pml4)
        lines.append("process slot %d pid %d tid %d '%s' %s kill %d pml4 0x%x" % (
            slot, i32(procs, b + op_pid), i32(procs, b + op_tid), cstr(procs, b + op_name, 16),
            sname, procs[b + op_kill], pml4))
        pparts.append("%d:%s/tid=%d/pml4=%x" % (i32(procs, b + op_pid), sname,
                                               i32(procs, b + op_tid), pml4))
    live = "-"
    if "uspace_live_pml4" in dsyms and "uspace_live_count" in dsyms:
        n = u32(memsave(sock, dsyms["uspace_live_count"], 4, tmp), 0)
        arr = memsave(sock, dsyms["uspace_live_pml4"], 8 * min(n, 80), tmp) if n else b""
        live = ",".join("%x" % u64(arr, 8 * i) for i in range(min(n, 80))) or "none"
        lines.append("live PML4 (%d): %s" % (n, live))
    summary = "threads [%s] processes [%s] live-PML4 [%s]" % (
        " ".join(tparts) or "none", " ".join(pparts) or "none", live)
    return "\n".join(lines), summary


def lapic_summary(text):
    def grab(pat):
        m = re.search(pat, text, re.M)
        return re.sub(r"\s+", " ", m.group(1).strip()) if m else "?"
    return "SVR %s; LINT0 %s; LINT1 %s; %s; ISR %s; IRR %s" % (
        grab(r"^SPIV\s+(0x[0-9a-f]+ APIC \w+)"), grab(r"^LVT0\s+(.*)$"),
        grab(r"^LVT1\s+(.*)$"), grab(r"(TPR 0x[0-9a-f]+)"), grab(r"^ISR\s+(.*)$"),
        grab(r"^IRR\s+(.*)$"))


def field(regs, name):
    m = re.search(r"\b" + name + r"=([0-9a-fA-F]+)", regs)
    return int(m.group(1), 16) if m else None


def irq0(text):
    m = re.search(r"^\s*0:\s*(\d+)", text, re.M)
    return int(m.group(1)) if m else None


def kernel_irq_counts(elf):
    """The address of irq.c's counts[] (two statics share the name), read
    from irq_count's own load: mov -0x...(,%rdi,4) or disp32(,%rdi,4)."""
    try:
        out = subprocess.run(["objdump", "-d", "--disassemble=irq_count", elf],
                             capture_output=True, text=True, timeout=30).stdout
    except Exception:
        return None
    m = re.search(r"mov\s+(-?0x[0-9a-f]+)\(,%rdi,4\)", out)
    if not m:
        return None
    return int(m.group(1), 16) & 0xFFFFFFFFFFFFFFFF


def irq1(path, out, elf):
    """0.11 (harness addition): QEMU's IRQ 1 count and the kernel's
    counts[1], one line, for a boot that typed keys."""
    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    s.settimeout(0.5)
    s.connect(path)
    time.sleep(0.2)
    try:
        s.recv(65536)
    except socket.timeout:
        pass
    m = re.search(r"^\s*1:\s*(\d+)", hmp(s, "info irq"), re.M)
    qemu = m.group(1) if m else "?"
    base = kernel_irq_counts(elf)
    kern = "?"
    if base is not None:
        k = re.search(r":\s*0x([0-9a-fA-F]+)", hmp(s, "x /1wx 0x%x" % (base + 4)))
        kern = str(int(k.group(1), 16)) if k else "?"
    line = "IRQ 1: QEMU raised %s, kernel counted %s" % (qemu, kern)
    with open(out, "w") as f:
        f.write(line + "\n")
    print(line)


def main():
    if sys.argv[1] == "--irq1":
        irq1(*sys.argv[2:5])
        return
    path, out, elf, label = sys.argv[1:5]
    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    s.settimeout(0.5)
    s.connect(path)
    time.sleep(0.2)
    try:
        s.recv(65536)                   # the banner and first prompt
    except socket.timeout:
        pass
    syms = code_symbols(elf)
    dsyms = data_symbols(elf)
    tmp = os.path.abspath(out) + ".mem"     # QEMU writes memsave files here
    samples = []
    with open(out, "w") as f:
        f.write("Stall capture: %s\n" % label)
        for i in range(3):
            if i:
                time.sleep(1.0)
            regs = hmp(s, "info registers")
            pic = hmp(s, "info pic")
            irq = hmp(s, "info irq")
            lapic = hmp(s, "info lapic")
            kv = read_kvars(s, dsyms)
            # 0.12 stage 2b: the thread, process and live-PML4 tables.
            try:
                tables, tsum = read_tables(s, dsyms, tmp)
            except Exception as e:
                tables, tsum = "tables: not read (%s)" % e, "tables ?"
            for name, text in (("info registers", regs), ("info pic", pic),
                               ("info irq", irq), ("info lapic", lapic),
                               ("kernel variables", kv), ("tables", tables)):
                f.write("--- sample %d: %s ---\n%s\n" % (i + 1, name, text))
            p0 = re.search(r"^pic0: (irr=\w+ imr=\w+ isr=\w+)", pic, re.M)
            p1 = re.search(r"^pic1: (irr=\w+ imr=\w+ isr=\w+)", pic, re.M)
            pics = "%s PIC1 %s" % (p0.group(1) if p0 else "?", p1.group(1) if p1 else "?")
            samples.append((regs, irq0(irq), kv, pics, lapic_summary(lapic), tsum))
    parts = []
    for i, (regs, ticks, kv, p0, lap, tsum) in enumerate(samples):
        rip, rsp, rfl = field(regs, "RIP"), field(regs, "RSP"), field(regs, "RFL")
        cr3 = field(regs, "CR3")
        cs = re.search(r"^CS =([0-9a-f]+)", regs, re.M)
        hlt = re.search(r"HLT=(\d)", regs)
        text = "s%d RIP=%s (%s) RSP=%s RFLAGS=%s IF=%s CR3=%s CS=%s HLT=%s IRQ0=%s %s PIC0 %s" % (
            i + 1,
            "%016x" % rip if rip is not None else "?",
            resolve(syms, rip) if rip is not None else "?",
            "%016x" % rsp if rsp is not None else "?",
            "%x" % rfl if rfl is not None else "?",
            (rfl >> 9) & 1 if rfl is not None else "?",
            "%x" % cr3 if cr3 is not None else "?",
            cs.group(1) if cs else "?",
            hlt.group(1) if hlt else "?",
            ticks if ticks is not None else "?", kv, p0)
        if i == 0:
            text += " LAPIC " + lap
        text += " " + tsum
        parts.append(text)
    t = [x[1] for x in samples]
    delta = "IRQ0 +%d, +%d" % (t[1] - t[0], t[2] - t[1]) if None not in t else "IRQ0 counts ?"
    summary = "STALL CAPTURE %s: %s; %s" % (label, " | ".join(parts), delta)
    with open(out, "a") as f:
        f.write(summary + "\n")
    print(summary)


if __name__ == "__main__":
    main()
