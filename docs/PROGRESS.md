# Madire OS: Progress

This page records what has been built, the tests each milestone passed, the known limitations each one left, and the decisions behind the project. Everything under "Roadmap" and "Future phases" is **planned, not committed**: it gives the direction, and each item needs its own plan and approval before any work starts.

## 1. Milestone history

Each milestone is one commit on `main`. The hashes are from `git log`; tags exist from 0.6 on.

| Milestone | Commit | Date | What it delivered |
|---|---|---|---|
| **0.1** | `99326c3` | 2026-09-27 | Bootable ISO: GRUB, a 32-bit kernel, the boot screen |
| | `529456f` | 2026-09-27 | Follow-up fix: `.gitattributes`, to keep LF line endings |
| | `f07b517` | 2026-09-27 | Follow-up fix: `run.ps1` discards a saved VM state before attaching the ISO |
| **0.2** | `0fb1c21` | 2026-09-27 | CPU foundation (own GDT, IDT, exception handlers, crash screen; PIC remapped and masked), drivers split out, `kprintf`, the Madire M logo |
| **0.3** | `3872df3` | 2026-09-28 | Memory foundation: validated memory map, reservations, bitmap page allocator, boot self-test, real RAM figures |
| **0.4** | `c146655` | 2026-09-28 | Hardware interrupts: IRQ layer, PIT timer at 100 Hz, PS/2 keyboard, idle loop, timer/keyboard diagnostics |
| **0.5** | `f73a0ea` | 2026-09-28 | Kernel console and command shell (`help`, `clear`, `version`, `uptime`, `mem`, `cpu`, `echo`) |
| **0.6** | `77ae5c1` (tag `v0.6`) | 2026-09-28 | Paging (identity-mapped, walk-checked before it is enabled) and the kernel heap |
| **0.7** | `1044dd7` (tag `v0.7`) | 2026-09-29 | 64-bit long mode: 4-level paging, NX, TSS with an IST1 double-fault stack, stack guard pages |
| **0.8** | `f38f4c6` (tag `v0.8`) | 2026-09-30 | Higher-half kernel: kernel at `0xFFFFFFFF80000000` + physical, lower half empty, physical view for the low 1 MiB and boot data, `pflow` diagnostic, stack high-water mark, boot-module result on screen, reproducible build, `.devcontainer/` proven |
| **0.9** | `e328cc7` (tag `v0.9`) | 2026-10-05 | Kernel threads and a preemptive scheduler: threads with their own guarded stacks (PML4 slot 507), context switches, round robin with a 50 ms slice, sleep and join, a preemption-disable count that makes the kernel safe under preemption, a scheduler self-test at every boot, the `ps` command, the thread diagnostics, `scripts/vbox-acceptance.ps1`, `AGENTS.md` |
| | `3b01a8a` | 2026-10-06 | Documentation commit: the Madire OS Command Standard, `docs/commands.md` (MOCS v0.1; no code) |
| **0.9.1** | `1300fb5` (tag `v0.9.1`) | 2026-10-06 | Shell commands: `history` (with Up/Down recall), `uname`, `hostname`, `date` (from the RTC), `dmesg` (a 32 KiB kernel log), `reboot` (with a confirmation; keyboard-controller reset, triple-fault fallback), `help <command>` and `<command> --help` for every command; MOCS v0.2 |
| **0.9.2** | `14339ff` (tag `v0.9.2`) | 2026-10-06 | Console scrollback (the last 1000 lines with their colours, Shift+PgUp/PgDn, an indicator on the last row, `clear` keeps the screen) and key-event logging off by default (boot option `keylog` for tests; the keyboard diagnostics turn it on); MOCS v0.3 |
| **0.10** | this milestone's commit (tag `v0.10`) | 2026-10-09 | User processes: ring 3 with user segments, IST2 and RSP0 on every switch; SMEP, SMAP and CR0.EM; one address space per process, the kernel half frozen at boot; `SYSCALL` with six Linux x86-64 system calls (`write`, `sched_yield`, `nanosleep`, `getpid`, `exit`, `exit_group`), returning with `IRETQ`; `copy_from_user`; faults contained to the program; 19 built-in test programs; a user-mode self-test at every boot; 11 new Diagnostics entries (16–26); the crash screen's `Cause:` line for SMEP/SMAP; the test harness reads the version once; MOCS v0.4 and v0.5 |


## 2. Tests passing at each milestone

| Milestone | Host unit suites | QEMU checks |
|---|---|---|
| 0.1 | – | Multiboot header check and one headless boot test |
| 0.2 | – | 26 (boot, logo, GDT/IDT/PIC, 3 crash tests) |
| 0.3 | 3 (`mmap`, `pmm`, `reserve`) | 150 |
| 0.4 | 6 (+ `pit`, `scancode`, `keyqueue`) | 180 (150 + 30 tagged `[0.4]`) |
| 0.5 | 11 (+ `textbuf`, `lineedit`, `parser`, `cpuinfo`, `shell`) | 212 (+ 32 `[0.5]`) |
| 0.6 | 15 (+ `pte`, `vmm`, `heap`, `shell_mem06`) | 261 (+ 49 `[0.6]`) |
| 0.7 | 19 (+ `bootmod`, `kprintf`, `nx`, `shell_cpu07`; `pte` and `vmm` rewritten for 4 levels) | 357 (150 + 30 + 32 + 49 + 96 `[0.7]`) |
| 0.8 | 21 (+ `stackpeak`, `shell_mem08`; new cases in `mmap` and `pte`) | 389 (150 + 30 + 32 + 49 + 96 + 32 `[0.8]`), plus `make repro` (two clean builds byte-identical) |
| 0.9 | 24 (+ `sched`, `kstack`, `shell_ps09`; a new case in `kprintf`) | 528 (150 + 30 + 32 + 49 + 96 + 32 + 139 `[0.9]`), plus `make repro` |
| 0.9.1 | 28 (+ `history`, `rtc`, `logbuf`, `shell_091`) | 560 (150 + 30 + 32 + 49 + 96 + 32 + 139 + 32 `[0.9.1]`), plus `make repro` |
| 0.9.2 | 29 (+ `scrollback`) | 585 (150 + 30 + 32 + 49 + 96 + 32 + 139 + 32 + 25 `[0.9.2]`), plus `make repro` |
| 0.10 | 41 (+ `gdt010`, `cpuprot010`, `vmm010`, `uvm`, `usercopy`, `proc`, `userimg`, `userentry`, `faultop`, `syscall`, `pfcause010`, `sched010`) | 838 (150 + 30 + 32 + 49 + 96 + 32 + 139 + 32 + 25 + 253 `[0.10]`), plus `make repro` |

Each milestone also passed its VirtualBox acceptance test on the VM `MadireOS-Test` before it was committed. Since 0.9 that test is scripted (`scripts/vbox-acceptance.ps1`, see [testing.md](testing.md)). In 0.9, 33 of 33 checks passed, plus 20 extra normal boots, each with the scheduler self-test `PASS` and 0 preemptions during its ping-pong.

**0.9 test details:** the 139 `[0.9]` checks are in 19 groups (listed in [testing.md](testing.md)). Of the existing 389 checks only the 4 version checks changed (`v0.8` → `v0.9`); none was removed or weakened. The new unit suites make 32,782 checks (`test_sched`), 1,190 (`test_kstack`) and 23 (`test_shell_ps09`). The full suite takes about 15 minutes, so it runs detached in the Codespace (see [development.md](development.md)).

**0.9.1 test details:** the 32 `[0.9.1]` checks are in 3 groups: a scripted console session with every new command (22), `reboot` then `yes` through the keyboard controller (5) and the triple-fault fallback `test=reboottriple` (5). Of the existing 528 checks only the 4 version checks changed (`v0.9` → `v0.9.1`); none was removed or weakened. The new unit suites make 110 checks (`test_history`), 51 (`test_rtc`), 62 (`test_logbuf`) and 164 (`test_shell_091`); `test_shell.c` and `test_shell_ps09.c` are unchanged and still pass, proving that every 0.9 output is the same without the new context field. VirtualBox: 49 of 49 expected lines (the 33 of 0.9 plus 16 for the new commands and the reboot).

**0.9.2 test details:** the 25 `[0.9.2]` checks are in 2 groups: key logging (11: the default line and no key event on a normal boot, in the serial log and in `dmesg`; `nokeylog` doesn't turn it on; `keylog`, `test=keyboard` and `test=kbdflood` do) and scrollback (14, proven on the real VGA screen with the new `tests/qmp_screen.py`). The new unit suite `test_scrollback` makes 174 checks, including the unchanged decoder's Shift+PgUp/PgDn. **Existing checks:** the boots of 36 checks changed and none of their assertions did: the 3 GRUB-boot key checks read a test-only ISO whose `grub.cfg` adds `keylog` to the default entry, and the `[0.9] preempt` group (11) and the `[0.9.1]` console session (22) boot with `keylog`. **5 version checks** changed to `0.9.2`: the 4 listed in the plan and `[0.9.1] uname -a`, a fifth version pin the plan missed (lesson: list every check containing a version string). VirtualBox: every group passes; the `scrollback` page check, created in this milestone, first expected exactly 24 lines on the second page and was corrected to the approved rule ("24 further back, but not past line 1").

**0.10 test details:** the 253 `[0.10]` checks are in 20 groups (listed in [testing.md](testing.md)): the version read once (2), the GDT/IDT/TSS for user mode (7), SMEP/SMAP/CR0.EM/freeze on the default CPU (8) and on `qemu64,+smep,+smap` (8), SMEP and SMAP enforced (25), RSP0 (3), `gpflimit` (6), the freeze (3), address spaces (10), the first program in ring 3 (12), kill and teardown (10), fault containment in three parts (20, 26, 20), the first system calls (19), system calls with pointers (27), the boot self-test (8), preemption of user processes (6), churn (7) and the GRUB Diagnostics entries (26). The 12 new unit suites make 6,524 checks (`proc` 5,133, `uvm` 968, `syscall` 160, `userentry` 44, `userimg` 40, `gdt010` 38, `usercopy` 37, `pfcause010` 30, `sched010` 23, `vmm010` 22, `faultop` 15, `cpuprot010` 14); every existing suite is unchanged. **Existing checks:** 7 version checks are now built from `kernel/kernel.h` (5 QEMU, 2 VirtualBox; proven byte-identical to the old fixed regexes at 0.9.2), and the `[0.7]` guard-page reject pattern (one pattern, 6 runs) was narrowed to `CPU exception [0-9]+:` so that a contained fault in a program isn't taken for a kernel exception; none was removed or weakened (details in [testing.md](testing.md)). Every fault check also compares the instruction bytes at RIP with the expected opcode (28 opcodes). `make repro`: identical (kernel `734df709…`); no undefined symbols, no libgcc/libc helper, no SSE/x87 register in the kernel. **VirtualBox:** every existing group, the boot self-test and positions 16–26 pass (25 and 26 NOT APPLICABLE: the VM offers no SMEP or SMAP); every result line matches QEMU (the 35 fault lines exactly), apart from timing figures and the VM's CPU features; two script-only fixes (see testing.md); one `userkill` boot measured a 10-tick turn (watch item below; 10 re-runs and 2 other runs measured 5).

**Policy (a) measured again (0.10):** the plan's rule is that a user process's longest turn is at most 6 ticks (else stop). In QEMU every user process stayed at or below 5 ticks in every run; kernel threads still take longer turns inside guarded sections (final regression: `framethreads` 44 ticks, `heapthreads` 11, `preemptoff` 21 by design, the kernel frame thread in `test=userpreempt` 43; in VirtualBox that thread took 125 and 189). Stage 6a's run gave `framethreads` 33 and `heapthreads` 13; 0.9 measured up to 77.

**Stack peak and figures (0.10):** the boot stack's peak is **1512 bytes** at "System ready." and after `ps`/`version` in the console, in QEMU and VirtualBox alike (1496 in 0.9.2); each process's thread has its own stack (2376 bytes for the boot self-test's programs). On `MadireOS-Test`: free pages **917028** at boot and **916968** in `mem` (20 fewer than 0.9.2), **457** mapped pages (QEMU `-kernel`: 458), **15** page tables; the memory exhaustion test allocates 916968 pages. The kernel image ends at physical `0x1CC000`, **208 KiB below the 2 MiB boundary**.

**Stack peak baseline (0.8):** the kernel stack's high-water mark is **1480 of 16384 bytes**, the same after booting, after the keyboard flood and after the scripted console session, in QEMU and in VirtualBox. Future milestones can compare against it (processes in 0.9 will add their own stacks).

**Stack peaks (0.9):** the boot stack's peak is **1496 bytes** (1480 in 0.8). It is again the same after booting, after the keyboard flood and after the console session, in QEMU and in VirtualBox. Each kernel thread has its own 16 KiB stack and logs its final peak when it exits. The test threads use 192 to 1000 bytes, except the churn test's deliberately deep thread (8496 bytes).

**Stack peak (0.9.1):** **1512 bytes** at "System ready.", after the keyboard flood and after the scripted console session, in QEMU and VirtualBox alike. A VirtualBox console session with `ps`, `version` and `mem` reached 1544.

**Stack peak and figures (0.9.2):** the peak is **1496 bytes** at "System ready." (and after the console session) in QEMU and VirtualBox alike. On `MadireOS-Test` (4 GB): free pages **917048** at boot and **916988** in `mem`, **437** mapped pages (QEMU `-kernel`: 438), **15** page tables; the 156 KiB scrollback costs 39 pages.

In 0.7 the boot memory self-test reports "35 checks" where 0.6's early logs showed 3104. That comes from a 0.6 change to `memtest.c` (one check counted per page instead of per word, with every word still compared). It isn't lost coverage.

## 3. Known limitations, by milestone

What each milestone left open when it was committed, and where it was resolved.

- **0.1:** 32-bit only; BIOS only; no interrupts, memory management, keyboard, filesystem or processes; placeholder status lines; the screen didn't scroll. (Interrupts: 0.2 and 0.4. Memory: 0.3. Keyboard: 0.4. Scrolling: the 0.5 console.)
- **0.2:** no memory management; all hardware interrupts masked; the logo is blocky (text mode gives 80×50 "pixels"). (Memory: 0.3. Interrupts: 0.4. The logo is still blocky.)
- **0.3:** only RAM below 4 GiB is used; the whole low 1 MiB is reserved; ACPI-reclaimable memory isn't reclaimed; physical pages only, no paging or heap. (Paging and heap: 0.6. The rest is still open.)
- **0.4:** PS/2 keyboard only, US layout, no LEDs, keypad assumes Num Lock; only IRQ0 and IRQ1, through the 8259 PIC; fixed 100 Hz timer. (All still open.)
- **0.5:** seven built-in commands; lowercase, case-sensitive, at most 8 words and 71 characters; no history, tab completion, scrollback or pipes; Ctrl and Alt keys do nothing; long output lines wrap at column 80. (All still open.)
- **0.6:** identity-mapped kernel, 4 KiB pages, one address space, kernel mode only; every page fault is fatal; no stack guard page; the heap never shrinks, isn't interrupt-safe, and `kfree` checks the whole heap. (Stack guard: 0.7. The rest is still open.)
- **0.7:**
  - The kernel is still identity-mapped at 1 MiB; the higher-half move is 0.8. (Resolved in 0.8.)
  - RAM above 4 GiB is still not used. (Still open.)
  - No direct map of physical memory: one page at a time, through the physical-access window. (Still open; 0.8's physical view covers only the low 1 MiB and the boot data.)
  - The stack guard pages prove normal use never reaches them, but there is no stack high-water measurement (how close the stack gets). To be considered before processes. (Resolved in 0.8.)
  - `test_reserve` depends on the host test binary's load address (the unit tests are linked at 256 MiB). To be made independent of it in a later milestone. (Still open; deferred in 0.8.)
  - The boot-module integrity result is on the serial log only, so a machine without a serial port can't show it. (Resolved in 0.8.)
  - The `cpu` command's "Features:" line can be longer than 80 columns and wrap (as in 0.5 and 0.6). (Still open.)
  - The stub's "no CPUID" path can't be run in QEMU (every QEMU CPU model has CPUID); it has been reviewed but not executed. (Still open.)
  - The build isn't bit-for-bit reproducible: the Makefile links the objects in the order `find` returns the sources, which depends on the file system, so two builds of the same sources can differ in layout (a few bytes of alignment padding). Each test run tests the binary it built. (Resolved in 0.8 for the kernel.)
  - `.devcontainer/` hasn't been used to create a Codespace yet (the current Codespace predates it). (Resolved in 0.8.)
- **0.8:**
  - RAM above 4 GiB is still not used, and there is still no direct map: the physical view maps only the low 1 MiB and the boot data; any other frame is reached one page at a time through the physical-access window.
  - One address space, kernel mode only, 4 KiB pages (also for the kernel image); every page fault is fatal.
  - Until `paging_init` loads the kernel's tables, the boot stub's temporary tables still map 0–4 GiB at address 0 (memory set-up and the boot self-test run on them). If the walk-check fails, the kernel keeps running on those tables, so the lower half stays mapped in that failure case (the screen shows `FAILED (paging)`).
  - The stack high-water mark covers the kernel stack only, not the IST1 double-fault stack. It can read a few bytes too low (if the deepest byte written happens to equal the paint pattern), never too high, and the top 736 bytes (the boot frames and the 256-byte margin, which are never painted) always count as used.
  - The screen has one message line: with several boot modules, only the last result stays visible (the serial log has all of them).
  - The ISO isn't reproducible (`grub-mkrescue` records timestamps); `make repro` covers `madire.elf` and `madire.kernel` only.
  - `test_reserve` still depends on the host test binary's load address (deferred).
  - The `cpu` "Features:" line can still wrap, and the stub's "no CPUID" path is still reviewed but not executed.
- **0.9:**
  - **Longer turns inside guarded sections (deferred switch, policy (a)).** A switch that falls due while preemption is disabled waits for the next interrupt after the count is back at 0. A thread that spends almost all its time inside guarded sections (for example allocating frames in a loop) can therefore run longer than the 5-tick slice. Correctness and progress are unaffected. Every scheduler diagnostic logs its longest turn: `framethreads` measured up to **77 ticks** during the stages (30 in the final regression run), `heapthreads` 14, and `preemptoff` 21 (it holds the guard for 20 ticks on purpose). To be measured again in 0.10. (Measured in 0.10: user processes at most 5 ticks; kernel threads still longer, see the 0.10 test details.)
  - **VirtualBox delivers timer ticks in bursts** after the host has delayed the VM, so a 5-tick slice can run out within microseconds of guest time, even in the middle of a short cooperative turn. This is how the emulated PIT behaves, not a kernel fault. The boot self-test and `test=threads` allow for it (decision "option (A)" in the decision log). In all 34 VirtualBox boots of the final acceptance run the ping-pong saw 0 preemptions, so the relaxed branch is proven in QEMU (`test=pingpongpreempt`) but not yet observed in VirtualBox.
  - **The full test suite takes about 15 minutes** and must be run detached in the Codespace, on a hash-verified snapshot (see [development.md](development.md)), so that a tool's time limit can't cut it off.
  - Kernel threads only: no user mode, per-process address spaces or system calls yet (0.10). (Resolved in 0.10.)
  - One CPU only: the preemption-disable count can replace locks only because nothing else runs at the same time. There is no SMP support.
  - Round robin only, no priorities; a fixed 50 ms slice; at most 64 threads (thread 0 plus 63), each with a 16 KiB stack.
  - Thread 0 (the console and idle loop) never blocks: it waits by polling (yield, or `hlt` when nothing else can run).
  - Only thread 0 writes to the screen; other threads log to serial with `klogf` (a breach panics).
  - The heap is still not interrupt-safe (it is now safe between threads).
  - Thread stack peaks have the same limits as the boot stack's: they can read a few bytes too low, never too high.
  - Still open from 0.8: RAM above 4 GiB; no direct map; 4 KiB pages only; every page fault is fatal; the ISO isn't reproducible; `test_reserve` depends on the load address; the `cpu` "Features:" line can wrap; the "no CPUID" path isn't executed.
- **0.9.1:**
  - **HIGH PRIORITY (security): every key press is logged.** Since 0.4 the kernel writes each key event (scancode, key and character) to the serial log, and since 0.9.1 that log is also kept in memory and shown by `dmesg`. Once passwords or other secrets can be typed (`passwd`, `sudo`, a login), this would record them. **Key-event logging must be off by default (enabled only for tests and diagnostics) before any password or secret input exists.** It stays as it is in 0.9.1, because existing tests depend on these lines; the fix is planned for 0.9.2, without weakening any existing test. (**Resolved in 0.9.2:** key events are logged only with the boot option `keylog` or during the keyboard diagnostics; a normal boot logs none, in the serial log or in `dmesg`. Secret input is covered by a binding rule in AGENTS.md.)
  - **The RTC's clock basis differs between machines:** VirtualBox keeps the hardware clock on the host's local time (2 hours ahead of UTC on the test host), QEMU on UTC. `date` shows the RTC as it is, labelled "time zone not set". A future time-zone setting should let the user choose whether the hardware clock holds UTC or local time.
  - `date` assumes the century is 20 (years 2000–2099): the CMOS century register is only found through ACPI, which isn't read yet. The date can't be set.
  - **`dmesg` shows only what `kprintf`/`klogf` wrote.** The boot screen's status lines (`GDT loaded: OK` …) and the console's command output go to the serial port directly and aren't in it (a documented gap; changing it would mean changing the protected `serial.c`). Because every key press is logged, the newest lines of `dmesg` are usually the keystrokes of the command itself (no longer since 0.9.2, which stops key logging by default). **Joined lines:** the boot screen writes a status line's newline to the serial port only, so in `dmesg` that line can run into the next one (seen in 0.9.2's VirtualBox screenshot: `…divisor 11932)Keyboard self-test: …`). Proposed fix for a later milestone (not done yet): the boot-sequence calls in `kernel.c` and the `BOOTSCREEN_SAFE()` wrapper also write the line ending into the kernel log, through a log-only call in `kprintf.c`, leaving the protected `bootscreen.c` and `serial.c` unchanged.
  - `hostname` is fixed (`madire`) until settings can be stored; `history` keeps the last 16 lines and is lost at restart.
  - `reboot` needs the keyboard controller (or the triple-fault fallback); there is no ACPI reset, `shutdown` or `poweroff` yet.
  - The console still has no scrollback: output that scrolls off the top is lost (scrollback is planned for 0.9.2). (Resolved in 0.9.2.)
- **0.9.2:**
  - The scrollback keeps the last 1000 lines and is lost at restart; it holds what the console drew, not the serial log.
  - Shift+PgUp/PgDn work with the grey PgUp/PgDn keys (and laptops' Fn keys that send them); the numeric keypad's PgUp/PgDn type `9`/`3`, because Num Lock is assumed on (a 0.4 limitation).
  - Key events are still logged during the keyboard diagnostics (`test=keyboard`, `test=kbdflood`) and with `keylog`: by design, for tests. `keylog` must never be used where secrets are typed.
  - **No secret-input mode exists yet.** No prompt asks for a secret today; the binding rule in AGENTS.md says the first milestone that adds one must implement it (no key logging even with `keylog`, no echo to serial or `dmesg`, no history, `*` on screen).
  - The `dmesg` joined-lines effect described under 0.9.1 is still there (a fix is proposed above).
- **0.10:**
  - **Built-in programs only.** The 19 test programs are compiled with the kernel; ELF programs from boot modules, and `run`, `kill` and `ps` with process numbers, come in 0.11. One thread per process; no signals, `fork` or `exec`; at most 63 processes; a fixed 32 KiB user stack; at most 64 KiB of code and 64 KiB of data per program.
  - **No FPU state is saved.** CR0.EM is set, so x87, MMX and SSE instructions end a program (#NM / #UD); programs are built without them.
  - **`write` reaches the serial log and `dmesg` only** (as `[pid N] …` lines, `?` for control characters, at most 4096 bytes per call), never the screen; screen output comes with `run` in 0.11.
  - **`IRETQ` only:** every return to ring 3, also from a system call, uses `IRETQ`, not the faster `SYSRET` (one return path, and none of `SYSRET`'s non-canonical-RIP pitfalls). `nanosleep` never writes `rem` (nothing interrupts a sleep).
  - **The SMEP/SMAP crash path is proven in QEMU only** (`qemu64,+smep,+smap`): the VirtualBox VM doesn't offer SMEP or SMAP to the guest, so there `test=smep` and `test=smap` report NOT APPLICABLE. The "not supported" path is proven in both.
  - **QEMU's `emms` quirk:** QEMU 8.2.2's TCG doesn't raise #UD for `emms` with CR0.EM set, real CPUs do. The tests use `pxor %mm0,%mm0` for MMX (it faults in both); no test runs `emms` since stage 5a, so the real CPU's behaviour wasn't observed.
  - **Without NX, user data is executable:** W^X for user pages holds only on CPUs with NX (code pages are never writable either way).
  - **Check, then copy:** `copy_from_user` checks the range, then copies. That is safe only while nothing can change a process's mapping in between (true in 0.10: one CPU, one thread per process, no unmapping system call). A binding rule in AGENTS.md covers the first milestone that changes this.
  - **The kernel half is frozen at boot:** no new kernel-half PML4 slot can be added afterwards (by design; a new kernel area needs a slot that exists at boot).
  - Kernel threads still get turns longer than the slice inside guarded sections (policy (a)); user processes stayed within the 6-tick rule (one VirtualBox exception, a watch item).
  - Still open from before: RAM above 4 GiB; no direct map; 4 KiB pages only; the ISO isn't reproducible; `test_reserve` depends on the load address; the `cpu` "Features:" line can wrap; the "no CPUID" path isn't executed; the `dmesg` joined-lines effect; no secret-input mode yet.

### Incidents and corrections in 0.10

- **The stage-4d MMX false pass.** `userpriv`'s MMX case used `emms` and "passed" in stage 4d with #UD. In stage 5a, once `SYSCALL` was enabled, the case failed: QEMU 8.2.2's TCG doesn't fault `emms` under CR0.EM, and the 4d #UD had come from the program's next `syscall` instruction (at `0x40008d`, with EFER.SCE still clear), not from `emms`. Found by the stage-5a regression (5 failed checks), raised before any change. Fix (approved): the case uses `pxor %mm0,%mm0`, which faults in QEMU and on real CPUs, and **every fault check now proves which instruction faulted** (the bytes at RIP read with `copy_from_user`, compared with the expected opcode; `faultop.h`, `test_faultop`). Every other fault RIP of stages 4c–4e was then verified as the intended instruction.
- **The accidental merge of PR #1.** On 2026-10-06 at 21:18 UTC, pull request #1 (`wip/0.10` into `main`) was merged on GitHub: `main` became `5e76918`, a merge of `14339ff` and the stage-2 backup `0f07be1`, which was marked NOT TESTED. Found on 2026-10-07; with the owner's authorisation `main` was restored: an annotated tag `archive/pr1-merge` keeps `5e76918`, `main` was moved back to `14339ff` with `--force-with-lease`, and the tags `v0.6`–`v0.9.2` and `wip/0.10` were unchanged. GitHub keeps `refs/pull/1/head`, which can't be deleted by a push. Branch protection isn't available for this private repository on the free plan, so a rule was added to AGENTS.md instead: `wip/` branches are backups only and are never merged into `main`.
- **Stage 4 and stage 6b were stopped by the coding assistant's automated safety filter.** On 2026-10-07 three attempts at stage 4 (the first ring-3 entry) were stopped before any code was written; the owner paused stage 4 and resumed it in a fresh session in five small steps (4a–4e), each with its own stop point. On 2026-10-09 a stage-6b response was stopped at the `test=smep`/`test=smap` item; the owner split 6b, finished items 2–3 (the GRUB entries) first, and ran item 1 as a separate step in a fresh session.
- **An unexplained stall (stage 2):** one regression run had `test=preempt` stop after creating its threads (8 failed checks). It didn't recur in 400 later boots with automatic stall capture, nor in any later full regression; no stage-2 code is implicated.
- **The `[0.7]` guard-page reject (stage 6a):** the boot self-test's contained fault line (`… killed by a CPU exception: #PF …`) matched the pattern `CPU exception`, giving 6 false failures; the pattern was narrowed to the crash screen's `CPU exception N:` (approved option (A)); see *0.10 test details*.
- **`scripts/build.ps1` didn't upload `user/` (found in the final review).** Since stage 4a the kernel links the built-in programs from `user/`, but `build.ps1` still uploaded only `Makefile boot kernel drivers linker iso include scripts tests`, so a build through it failed in the Codespace. Every stage's evidence came from the detached regression procedure, which uploads `user/`, so no result was affected. Fixed with approval (`user` added to the upload list); verified by a clean `build.ps1` run: build and tests passed, and `madire.kernel` was identical to the final-review build (`734df709…`).
- **VirtualBox script fixes (stage 7):** the new `user` and `smx` groups first reported 11 false MISSING rows: a case-insensitive "no PANIC" check and a PowerShell variable (`$T`) that overwrote the loop variable (`$t`); both fixed and approved, then the groups passed.

**Approved deviation: the 0.9 turn-order rule changed in 0.10** (stage 2). `sched_order_check()` now accepts a wrong turn when the preemption count rose within the previous k turns (k = the cycle's length), instead of the previous turn only; see the decision log.

### Watch items

- **Preemptions inside the boot self-test's ping-pong.** In 200 boots each, the stage-2 kernel saw a preemption inside the cooperative ping-pong 5 times and the 0.9.2 kernel once (not significant: about an 11% chance if both were equally likely). Stage 6a's regression: 1 in 85 logs; the final regression: 2 preemptions in 102 logs; VirtualBox: 0 in 41 boots. Such a preemption needs a burst of timer ticks. Re-check in later milestones.
- **The VirtualBox 10-tick `userkill` turn.** One VirtualBox boot of `test=userkill` (stage 7) measured a longest turn of 10 ticks, beyond the 6-tick rule; every functional result was correct. 10 re-runs (no other VM running, host 0–23 % busy) and 2 other boots all measured 5. Probably a VirtualBox tick burst, unconfirmed: the turn record doesn't say which thread took it. If it recurs, the record gets the thread's identity (TID, and whether it is a user process or the kernel test thread) first; a kernel test thread over 6 ticks isn't a policy violation, but must be shown, not assumed.
- **The QMP stall capture (proposal, later, not now):** add the capture used for the stage-2 stall investigation (CPU registers, interrupt-controller state and IRQ counts, taken automatically when a boot stops producing output) to the test harness as a diagnostic aid that changes no checks.
- **The 2 MiB boundary.** One page table maps 2 MiB, and the kernel image is loaded at 1 MiB. It ended at physical `0x1B8000` in 0.9.2 (288 KiB below the boundary) and ends at `0x1CC000` in 0.10: **208 KiB (52 pages) below it**. Future growth will cross it, and the kernel's tables will then need one more page table: the page-table count, the mapped-page and free-page figures and the walk-check's table count will change. The tests recompute these figures from the log, and the docs must expect the change.

## 4. Roadmap

| Milestone | Goal | Status |
|---|---|---|
| **0.8** | Higher-half kernel: the kernel runs at `0xFFFFFFFF80000000` + physical (PML4 slot 511) and the lower half is empty | done |
| **0.9** | Processes, part 1: kernel threads and a preemptive scheduler | done |
| **0.9.1** | Shell commands needing no new kernel foundations: `history`, `uname`, `hostname`, `date`, `dmesg`, `reboot`, `help <command>`, `<command> --help` | done |
| **0.9.2** | Console scrollback (the last 1000 lines, Shift+PgUp/PgDn) and key-event logging off by default | done |
| **0.10** | Processes, part 2: user processes (user mode, one address space per process, system calls, faults contained to the program; built-in test programs) | done |
| **0.11** | Processes, part 3: ELF programs from boot modules, `run`, `kill`, `ps` with process numbers, Ctrl+C | next (needs its own plan and approval) |
| **1.0** | Filesystem: a virtual filesystem layer and an initial RAM disk, then disk filesystems | planned |

### 0.10 items: how they were delivered

- User mode (ring 3): user code and data segments in the GDT (`0x30`, `0x38`; `0x28` an all-zero placeholder), the TSS's RSP0 set on every switch, IST2 for NMI and machine check. **Done.**
- One PML4 per process, sharing the kernel half: entries 256–511 copied from the kernel's PML4, except slot 510 (the recursive mapping), which points to the process's own PML4; the kernel half frozen at boot. **Done.**
- The Linux x86-64 subset `write` (1), `sched_yield` (24), `nanosleep` (35), `getpid` (39), `exit` (60), `exit_group` (231); every other number `-ENOSYS`. **Done, with one change from the 0.9 plan:** `SYSCALL` in, but `IRETQ` out, never `SYSRET` (decided in the 0.10 plan).
- An ELF loader for programs delivered as boot modules; shell commands to run and kill programs; `ps` showing processes. **Moved to 0.11** (the 0.10 plan's split); 0.10 runs built-in programs.
- SMEP and SMAP. **Done** (used when the CPU has them; proven by `test=smep`, `test=smap` and `test=clac`).
- **The `gpf` diagnostic:** `0x28` is now the all-zero placeholder, inside the GDT's limit but not a loadable segment, so `gpf` still raises #GP(0x28); the old "past the end" case moved to `test=gpflimit` (#GP(0x40)). **Done.**
- **Measure the longer turns under policy (a) again.** **Done** (see *0.10 test details*).
- **Read the version once in the tests.** **Done** (7 checks).
- Still excluded: the filesystem, signals, threads inside user processes, SMP, fork/exec, priorities.

### 0.11 items (planned; 0.11 needs its own plan and approval)

- An ELF loader with strict checks, programs delivered as boot modules, and malformed-ELF test fixtures.
- `run`, `kill` and `ps` with process numbers (MOCS: PLANNED for 0.11); Ctrl+C for the foreground program; a program's output on the screen.
- If the VirtualBox `userkill` turn recurs: the longest-turn record names the thread (TID, user process or kernel thread).

The candidate items listed for 0.8 were decided in its plan. Delivered in 0.8:

- the boot-module integrity result on the boot screen's message line, for real hardware without a serial port;
- a stack high-water measurement (kernel stack), before processes arrive in 0.9;
- a reproducible build: the objects are linked in a sorted order, and `make repro` proves two clean builds byte-identical.

Deferred: making `test_reserve` independent of the host test binary's load address.

The 0.9 plan considered Linux/POSIX-compatible system calls, as 0.7 planning asked: 0.10 uses the Linux x86-64 system-call numbers and conventions (above), so that Chromium, Linux applications and Wine could be ported later.

**Commands:** the Madire OS Command Standard (MOCS), with every implemented and planned command, its level and status, is in [commands.md](commands.md).

**Modernisation track:** the legacy hardware paths Madire OS uses today (BIOS, the 8259 PIC, the PIT, one CPU, VGA text mode, PS/2, RAM below 4 GiB) and their modern replacements, the goals that will make it feel modern to users, and the open decision on Rust for drivers are listed in [roadmap.md](roadmap.md#modernisation-track-planned-not-committed-to-milestones-yet). All planned, not yet committed to milestones.

**Applications and compatibility strategy:** how Madire OS is to run most applications users need (Windows programs through a Wine port, Linux programs through the Linux system-call interface, virtual machines, web apps, plus its own and native apps), where apps come from over time, the open-source policy, licences for commercial software, and the **high-priority open decision on Madire OS's own licence** are in [roadmap.md](roadmap.md#applications-and-compatibility-strategy-planned-not-tied-to-milestones-yet). Planned, not tied to milestones yet.

### Future phases (direction only; planned, not committed)

- Hardware and drivers (storage, USB, audio, a Hardware Abstraction Layer)
- Networking, Wi-Fi, Bluetooth
- Sensors
- Security and permissions (see the Madire Security Platform below)
- Graphics and desktop
- Apps, and the Madire Store
- AI features
- UEFI boot and an installer
- Accessibility: screen reader, magnifier, voice control, high contrast, captions
- Languages and input: multiple languages including the South African official languages, keyboard layouts, input methods, right-to-left text
- Power management: sleep, hibernate, battery saver
- Backup, snapshots, system restore and a recovery mode
- Built-in apps: web browser, text editor, calculator, photos, media player, mail, calendar, screenshots
- System utilities: task manager, search, task scheduler, services manager, clipboard, notifications
- Virtualisation and containers
- Speech: dictation, text-to-speech
- Enterprise management: directory, central policy, remote lock and wipe
- Media codecs and content protection (licensing required)

### Madire Security Platform (planned)

- Users, permissions and application permissions
- Password manager / keychain
- Admin permission prompts
- App signing and download checks
- System file protection
- Exploit protection
- Ransomware folder protection
- Mandatory access control
- VPN client
- Automatic security updates
- Camera and microphone privacy indicators
- Find my device
- A future Madire Security Toolkit. Its reference projects are listed in [references.md](references.md), for reading only; none of them is ever run against any system.

## 5. Decision log

| Decision | When | Notes |
|---|---|---|
| Milestone split: 0.7 64-bit long mode (identity-mapped), 0.8 higher-half, 0.9 processes, 1.0 filesystem | 0.7 planning | Keeps each change testable on its own |
| **64-bit only**: the 32-bit build is dropped | 0.7 | The stub refuses a CPU without long mode or PAE with a message and halts (no triple fault) |
| Toolchain: Ubuntu GCC in freestanding mode (`-m64 -ffreestanding -mno-red-zone -mgeneral-regs-only -mcmodel=small …`), no cross-compiler; the build fails if any SSE/x87 register is used | 0.7 | `.devcontainer/` and `scripts/install-deps.sh` set up the build environment (proven on a fresh Codespace in 0.8). 0.8 switched `-mcmodel=small` to `-mcmodel=kernel` |
| Multiboot 1, booting a flat binary through the a.out kludge | 0.7 | GRUB and QEMU `-kernel` both accept it; the ELF is kept for debugging |
| BIOS now, UEFI later | 0.1 | VGA text mode needs BIOS firmware |
| Computers and laptops first; phones later (likely ARM64) | 0.7 planning | So CPU-specific code stays under `kernel/arch/` |
| 64-bit layout: window and self-test page in PML4[508], heap at `0xFFFFFE8000000000` (PML4[509]), recursive mapping in PML4[510], PML4[511] reserved for the higher-half kernel; no direct map | 0.7 | |
| NX on everything except kernel code | 0.7 | EFER.NXE is set by `paging_init` (moved from the boot stub during 0.7) |
| TSS with an IST1 stack for double faults; unmapped guard pages below the kernel and IST1 stacks | 0.7 | A kernel stack overflow ends on the crash screen instead of resetting the machine |
| RAM above 4 GiB out of scope for 0.7 | 0.7 | |
| VM `MadireOS-Test`: OS type Other_64; COM1 serial output to a file under the git-ignored `build/` folder, as acceptance evidence | 0.7 | `--pae` is left unchanged |
| **App strategy (planned):** a Chromium-based **Madire Browser** is a priority once graphics and networking exist. The **Madire Store** starts with web apps (WhatsApp Web, Teams, Zoom, Outlook/Gmail, Office on the web), open-source apps (LibreOffice, Thunderbird, VLC) and Madire's own apps. Official third-party apps need the vendors' agreement. | 0.7 planning | Direction only |
| 0.9 planning must consider Linux/POSIX-compatible system calls | 0.7 planning | For porting Chromium, Linux apps and Wine |
| **The licence must be decided before any outside open-source code is included** | 0.7 planning | The repository has no licence yet |
| External projects are references only: never copied without explicit approval and a licence check; cloned (shallow) only outside the repository; security tools are never run against any system; Chromium and Gecko are browsed, not cloned | 0.7 | See [references.md](references.md) |
| Higher-half kernel: virtual = `0xFFFFFFFF80000000` + physical (PML4[511]), `-mcmodel=kernel`; the stub maps the kernel low and high and jumps high | 0.8 | Still booted as a flat binary through the a.out kludge (physical load addresses) |
| Physical view at `0xFFFF800000000000` (PML4[256]) for the low 1 MiB (pages 1–255) and the boot-data reservations only; no direct map; free RAM, page 0 and the kernel image stay out of it | 0.8 | The walk-check and the self-test prove the kernel image has no alias there (no writable view of kernel code) |
| The lower half is empty in the kernel's tables (no PML4[0]); new `pflow` diagnostic | 0.8 | Any stray low address, including the kernel's old address `0x100000`, faults |
| Protected memory modules changed in 0.8, each justified in the plan: `memory.c`, `mmap.c`/`mmap.h`/`test_mmap.c`, `memtest.c`, one line of `vga_text.c`; `reserve.c` and `pmm.c` unchanged | 0.8 | Boot data and VGA are reached through the physical view |
| Reproducible kernel build (sorted link order, `make repro`); the ISO is excluded | 0.8 | `grub-mkrescue` records timestamps |
| Kernel stack high-water mark and the boot-module result on screen included; `test_reserve` independence deferred | 0.8 | |
| Milestone split: **0.9** kernel threads and a preemptive scheduler; **0.10** user mode, one PML4 per process, `SYSCALL` with a Linux x86-64 subset, ELF programs from boot modules; 1.0 stays the filesystem | 0.9 planning | Each half can be tested on its own; 0.9 is built in the shape 0.10 needs |
| Preemptive round robin with the slice `SCHED_SLICE_TICKS` = 5 ticks (50 ms) as a named constant; at most 64 threads; thread 0 is `kernel_main`'s flow (boot, console, idle) and never blocks or exits | 0.9 | |
| **No locks: a preemption-disable count.** The heap, the frame allocator, paging and the window, `kprintf`/`klogf` and the console's serial copy run each operation with preemption off; the scheduler's own state is changed with interrupts off | 0.9 | Enough on one CPU; interrupt handlers still never use these subsystems |
| The protected `memory.c` gets only a mechanical guard in `memory_alloc_page` and `memory_free_page` | 0.9 | Approved in the plan; the only protected file changed in 0.9 |
| One switch point: the end of an IRQ, after `irq_dispatch` (so after the EOI), in `interrupts.c`; `timer.c` and `irq.c` unchanged; exceptions never switch | 0.9 | |
| **Deferred switch, policy (a):** a switch requested while preemption is off stays pending and happens at the first interrupt after the count is back at 0 (at most one tick later); `preempt_enable()` itself never switches | 0.9, stage 3 | `test=preemptoff` measures the bound (1 tick) |
| Kernel thread stacks in PML4 slot 507 (`0xFFFFFD8000000000`): 64 slots of 32 KiB, each a 16 KiB stack above a 16 KiB unmapped guard; the area's page table is made at boot | 0.9 | Creating a thread never needs a table frame, and slot 507 exists before 0.10 copies the kernel half |
| F1: the console's per-line serial copy runs with preemption off | 0.9, stage 2 | So a thread's `klogf` line can't land inside a console line |
| F2: the protected `bootscreen.c` stays unchanged; every boot-screen call that can happen while other threads exist goes through `BOOTSCREEN_SAFE()` in `kernel.c` (thread 0 only, preemption off); `scripts/check-bootscreen.sh` fails the ISO build on any other call (the boot sequence and the boot-only `devtest.c` excepted) | 0.9, stages 2–3 | |
| F3: `kputc` panics when a thread other than 0 writes to the screen; the crash screen is exempt (`panic_active`) | 0.9, stages 2–3 | Other threads use `klogf` |
| **F4: `switch_to` saves and restores RFLAGS**; a new thread starts with RFLAGS `0x2`, then `sti` | 0.9, stage 4 | An accepted design correction found by an approved test: the `iretq` IF-state test caught a flag leaking from one thread to the next. It was implemented before being raised; the rule stays that design changes are raised first |
| F5: policy (a) kept; its longer turns are a documented limitation, and every scheduler diagnostic logs the longest turn | 0.9, stage 4 | To be measured again in 0.10 |
| F6: test threads do real work between `timer_ticks()` reads | 0.9, stage 4 | QEMU's TCG delivers interrupts only between translated blocks, so a tight polling loop starved IRQ0 |
| The scheduler self-test runs at every boot (serial only; the boot screen is unchanged), and a failure is fatal | 0.9 | So it must never fail on a correct kernel (see option (A)) |
| **Option (A): the cooperative order checks count preemptions.** With no preemption the order must be strict, exactly as before. With any preemption every thread must still have its full number of turns, and the order may differ only right after a preemption. `test=pingpongpreempt` forces the relaxed branch in QEMU; `test=pingpongbad` proves that a wrong order without a preemption still fails | 0.9, stage 8 | Found when 1 VirtualBox boot in 14 failed the self-test after a timer-tick burst. A test-only change: the kernel was correct |
| The full regression runs detached in the Codespace, on a hash-verified snapshot | 0.9, stage 3 | It takes about 15 minutes; see [development.md](development.md) |
| `scripts/vbox-acceptance.ps1`: a scripted VirtualBox acceptance test. It checks the VM's settings and refuses to run on any difference; attaching the ISO is the only change it makes | 0.9, stage 8 | |
| Two Diagnostics entries appended at positions 14 and 15, so no existing position moves | 0.9 | |
| `ps` comes through an optional shell-context field, so `test_shell.c` and every existing command's output stay unchanged | 0.9 | |
| `AGENTS.md` holds the working rules for every coding agent; `CLAUDE.md` is the single import line `@AGENTS.md` | 0.9 | |
| **0.9.1**, a separate small milestone for shell commands; its plan's decisions: an additive `cmos_rtc_read()` in the protected `cmos.c/.h` (`cmos_rtc_seconds()` unchanged byte for byte); `dmesg` captures `kprintf`/`klogf` only, with the serial-only lines documented as a gap; `<command> --help` for every command; the triple-fault reset test in QEMU only | after 0.9 (planning) | Order: 0.9 commit and tag, then `docs/commands.md`, then 0.9.1 |
| Modernisation track recorded; legacy hardware paths (BIOS, PIC, PIT, single CPU, VGA text, PS/2, <4 GiB RAM) are temporary by design | 0.9 | See [roadmap.md](roadmap.md#modernisation-track-planned-not-committed-to-milestones-yet); Rust for new drivers is an open decision for the drivers phase |
| Applications and compatibility strategy recorded: four compatibility layers (Windows programs through a Wine port, Linux programs through the 0.10 system-call interface, virtualisation, web apps) plus Madire's own and native apps; macOS-only apps are a known exception; later milestones must not block these layers (a conflict is raised before implementation); app sources grow from own, open-source and web apps to compatibility and commercial apps; every included open-source project needs a licence check and approval; users bring their own licences for commercial software, and proprietary apps are never redistributed without the vendor's agreement | 0.9.1 | See [roadmap.md](roadmap.md#applications-and-compatibility-strategy-planned-not-tied-to-milestones-yet) |
| The 0.9.1 commands come through one optional shell-context field (`extras`): without it every 0.9 output, `help` and every usage error are unchanged, so `test_shell.c` and `test_shell_ps09.c` stay as they are | 0.9.1 | The same pattern as `ps` in 0.9 |
| `history`: the console keeps the last 16 lines (numbered since boot) and handles Up/Down itself, filling the input line through the line editor, which is unchanged | 0.9.1 | `lineedit.c` and `test_lineedit.c` (protected) still ignore Up/Down |
| `dmesg`: `kputc` and `klogf` also copy every character into a 32 KiB ring buffer in `kprintf.c`; the console reads a snapshot with preemption off | 0.9.1 | Serial-only lines (boot screen, console output) are a documented gap |
| `reboot` asks for confirmation with a `yes/no> ` prompt; only exactly `yes` restarts. The reset pulses the CPU reset line through the keyboard controller, then forces a triple fault if that didn't work; `test=reboottriple` tests the fallback in QEMU only | 0.9.1 | |
| `date` reads the RTC through the additive `cmos_rtc_read()` (protected `cmos.c/.h`; `cmos_rtc_seconds()` unchanged byte for byte) and the pure `rtc_time.c` | 0.9.1 | No time zone; century 20 |
| **Deviation:** `help <unknown>` prints only `No help for 'X': unknown command.` instead of also "Type 'help' for a list of commands." | 0.9.1 | Every line must fit 79 columns: names are shown up to 40 characters (37 plus `...` beyond), which makes 72 columns; the second sentence would add 36 |
| 0.9.2 decisions: scrollback of 1000 lines (about 156 KiB), Shift+PgUp/PgDn, any other key returns to the bottom, a "scrolled back: line X of Y" indicator on the last row; `clear` pushes the screen into scrollback first; a new `tests/qmp_screen.py` reads the VGA text buffer through QMP to check the indicator; no protected file changes | 0.9.1 (planning) | The key-logging fix is decided in the 0.9.2 plan |
| Console scrollback: a pure `kernel/console/scrollback.c` (1000 lines × 80 cells with colours, about 156 KiB) and `console.c` glue; one write wrapper keeps each line that scrolls off (`textbuf.c` unchanged); Shift+PgUp/PgDn page 24 lines; while scrolled back any other key returns to the live screen (not scrolled back, plain PgUp/PgDn still do nothing); a `-- scrolled back: line X of Y … --` indicator in reverse colours on the last row; `clear` keeps the screen's used rows | 0.9.2 | No protected file changed; the decoder already delivered Shift+PgUp/PgDn |
| **Key-event logging off by default:** the idle loop in `kernel.c` logs key events only with the boot option `keylog` (matched as a whole word, so `nokeylog` doesn't count) or during `test=keyboard`/`test=kbdflood`; a `Key events: …` line at boot says which | 0.9.2 | Resolves the 0.9.1 HIGH-PRIORITY item; `devtest.c` (protected) unchanged |
| The 36 existing checks that read key lines keep their assertions: the 3 GRUB-boot checks read a test-only ISO with `keylog` on its default entry, the preempt group and the 0.9.1 console session boot with `keylog` | 0.9.2 | Approved in the plan |
| **Secret input (binding):** no milestone may add a password or other secret prompt without implementing secret-input mode in the same milestone | 0.9.2 | In AGENTS.md and development.md |
| A fifth version check (`[0.9.1] uname -a`) was missed in the plan and approved during stage 3; future plans list every check containing a version string, and 0.10's plan proposes building the version regexes from `kernel/kernel.h` | 0.9.2 | |
| The VirtualBox `scrollback` page check, created in this milestone, was corrected to "24 further back, but not past line 1" | 0.9.2 | A test fix, not a kernel change |
| `commands.md` MOCS v0.3: a "Console keys" section | 0.9.2 | |
| 0.10 split from 0.11: 0.10 = ring 3, system calls, address spaces and fault containment with built-in test programs; 0.11 = the ELF loader, boot-module programs, `run`, `kill`, `ps` with process numbers, Ctrl+C, malformed-ELF tests | 0.10 planning | |
| **Approved deviation: the 0.9 turn-order rule changed in 0.10.** `sched_order_check()` accepts a wrong turn when the preemption count rose within the previous k turns (k = the cycle's length: 2 for the boot self-test's ping-pong, 8 for `test=threads`); 0.9 looked at the previous turn only. A thread preempted after logging its turn and before yielding shows the wrong turn only after every other thread has logged again, so the fatal boot self-test failed on a correct kernel (5 of 200 stage-2 boots) | 0.10, stage 2 | Reproduced first by `test_sched010` (fails on the old rule); `pingpongbad` still fails, `pingpongpreempt` still passes; the log text "right after a preemption" is kept because an existing check matches it |
| `commands.md` MOCS v0.4: `run`, `kill` and the process list in `ps` moved to 0.11; 11 new PLANNED commands; a "Windows equivalents" table; "Did you mean" suggestions and help sections planned | 0.10 | Documentation only |
| 0.10 plan decisions: GDT `0x08` kcode, `0x10` kdata, `0x18` TSS, `0x28` all-zero placeholder, `0x30` user data, `0x38` user code, STAR = `(0x28\|3)<<48 \| 0x08<<32` (Q2); a fatal but fully deterministic boot user-mode self-test (Q3); `write` to serial and `dmesg` only, with `[pid N]` and `?` for control characters, at most 4096 bytes a call (Q4); `run`, `kill` and `ps` processes in 0.11, MOCS v0.4 (Q5); CR0.EM = 1, no FPU in programs (Q6); IST2 for NMI and #MC (Q7); `0x10000000`–`0x1000FFFF` reserved for Madire calls, none implemented (Q8); `IRETQ` return only, SFMASK TF\|DF\|IF\|IOPL\|NT\|AC, RFLAGS cleaned, `clac` on every entry with SMAP; user layout (code `0x400000`, data `0x500000`, a 32 KiB stack ending at `0x7FFFFFFFF000`, nothing below 4 MiB); a static table of 63 processes, PIDs never reused; a kill takes effect at the next return to ring 3; the version read once by the tests; policy (a) re-measured with a stop if a user process's turn exceeds 6 ticks | 0.10 planning (approved 2026-10-06) | No protected file changes; no expected failures in any stage |
| 0.10 stage 4 paused, to be resumed in a fresh session | 0.10 | Stages 0–3 tested; no stage-4 code exists. Resumed on 2026-10-07 in five small steps, 4a–4e |
| Stage 4 in five steps: 4a the first ring-3 entry (`spin`), 4b kill and the first teardown, 4c contained faults, 4d privileged instructions and gates, 4e kernel memory out of reach (and `sti`, `int 0x0e`) | 0.10, stage 4 | Each with its own stop point and backup |
| The process model: one thread per process, which builds, enters and tears down its own address space on its own CR3 (through slot 510), and frees its PML4 after switching back; a kill is only requested and ends the process at its next return to ring 3 | 0.10, stages 3–4 | No other thread ever edits a process's address space |
| Fault containment: an exception from ring 3 ends only that process (result "fault", code = the vector, one line on serial and in `dmesg`); NMI, double fault, machine check and every kernel fault still end on the crash screen | 0.10, stages 4c–4e | Process-ended messages go to serial and `dmesg` only (confirmed) |
| MMX case `emms` → `pxor %mm0,%mm0`; opcode evidence (the bytes at RIP) for every fault check | 0.10, stage 5a | After the stage-4d false pass (see *Incidents*) |
| Binding rule: any milestone that adds memory-unmapping system calls or threads inside a process must make the user-pointer check and the copy atomic (or copy through the physical-access window), with a test that changes the mapping between check and copy | 0.10, stage 5b | In AGENTS.md and development.md |
| `usersys`'s counts `write 4, -ENOSYS 1028` (the 4 writes were `-ENOSYS` before `write` existed) | 0.10, stage 5b | A 0.10 check, changed before the commit |
| The `[0.7]` guard-page reject narrowed to `CPU exception [0-9]+:` (option (A); option (B), rewording the user-fault messages, not taken); `userkill`'s PIDs `4-63` | 0.10, stage 6a | Proven on 20 crash logs and 13 user-fault logs |
| GRUB Diagnostics entries 16–24 (the user-mode tests) appended; no existing entry moved | 0.10, stage 6b | |
| Stage 6b split after a stopped response: the GRUB entries first, `test=smep`/`test=smap` as a separate step in a fresh session | 0.10, stage 6b | |
| `test=smep` and `test=smap` (one deliberate kernel access to a user page each, NOT APPLICABLE without the feature, a panic if the CPU allowed the access) and the crash screen's `Cause:` line (only kernel-mode faults on user addresses with SMEP/SMAP on) | 0.10, stage 6b | |
| GRUB entries 25 (`smep`) and 26 (`smap`) appended; the entry-count check 25 → 27 | 0.10, stage 7 | |
| VirtualBox acceptance with positions 16–26 (`user` and `smx` groups); no VM setting changed; the two script fixes accepted | 0.10, stage 7 | |
| The 10-tick `userkill` turn in VirtualBox: stop under the plan's rule; (a) 10 re-runs (all 5 ticks) → a watch item and stage 7 TESTED; (b) naming the thread only if it recurs; the bound and the test design unchanged | 0.10, stage 7 | |
| `wip/` branches are backups only: never opened or merged as a pull request into `main`, which receives only the single milestone commit | 0.10 (2026-10-07) | After the PR #1 incident; branch protection is unavailable on the free plan. In AGENTS.md |
| MOCS v0.5: Madire's own or open Unix/POSIX command names, no Microsoft/DOS-specific names (`presskey`, `colors`, `wintitle`, `fileattr`, `disk label`, `disk info`, `bootconf`) | 0.10 | Documentation only |
| Study-only references added: Linux kernel documentation and man-pages (GPL-2.0-only, nothing copied), OSDev (licence check before quoting), Redox OS and xv6-riscv (MIT, concepts only); 1.0 to design a VFS layer from the start | 0.10 | See references.md and roadmap.md |
| **Open decision, HIGH PRIORITY: Madire OS's own licence** (for example MIT, Apache-2.0, GPL-3.0 or proprietary), to be decided before any outside code is included | 0.9.1 | Restates the 0.7-planning rule above as an open decision with high priority |
