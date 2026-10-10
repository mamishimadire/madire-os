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
import re
import socket
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
            for name, text in (("info registers", regs), ("info pic", pic),
                               ("info irq", irq), ("info lapic", lapic),
                               ("kernel variables", kv)):
                f.write("--- sample %d: %s ---\n%s\n" % (i + 1, name, text))
            p0 = re.search(r"^pic0: (irr=\w+ imr=\w+ isr=\w+)", pic, re.M)
            samples.append((regs, irq0(irq), kv, p0.group(1) if p0 else "?",
                            lapic_summary(lapic)))
    parts = []
    for i, (regs, ticks, kv, p0, lap) in enumerate(samples):
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
        parts.append(text)
    t = [x[1] for x in samples]
    delta = "IRQ0 +%d, +%d" % (t[1] - t[0], t[2] - t[1]) if None not in t else "IRQ0 counts ?"
    summary = "STALL CAPTURE %s: %s; %s" % (label, " | ".join(parts), delta)
    with open(out, "a") as f:
        f.write(summary + "\n")
    print(summary)


if __name__ == "__main__":
    main()
