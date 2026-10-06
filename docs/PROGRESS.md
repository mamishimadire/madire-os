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
| **0.9.1** | this milestone's commit (tag `v0.9.1`) | 2026-10-06 | Shell commands: `history` (with Up/Down recall), `uname`, `hostname`, `date` (from the RTC), `dmesg` (a 32 KiB kernel log), `reboot` (with a confirmation; keyboard-controller reset, triple-fault fallback), `help <command>` and `<command> --help` for every command; MOCS v0.2 |

The 0.9.1 commit can't list its own hash; `git log -1 v0.9.1` gives it.

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

Each milestone also passed its VirtualBox acceptance test on the VM `MadireOS-Test` before it was committed. Since 0.9 that test is scripted (`scripts/vbox-acceptance.ps1`, see [testing.md](testing.md)). In 0.9, 33 of 33 checks passed, plus 20 extra normal boots, each with the scheduler self-test `PASS` and 0 preemptions during its ping-pong.

**0.9 test details:** the 139 `[0.9]` checks are in 19 groups (listed in [testing.md](testing.md)). Of the existing 389 checks only the 4 version checks changed (`v0.8` → `v0.9`); none was removed or weakened. The new unit suites make 32,782 checks (`test_sched`), 1,190 (`test_kstack`) and 23 (`test_shell_ps09`). The full suite takes about 15 minutes, so it runs detached in the Codespace (see [development.md](development.md)).

**0.9.1 test details:** the 32 `[0.9.1]` checks are in 3 groups: a scripted console session with every new command (22), `reboot` then `yes` through the keyboard controller (5) and the triple-fault fallback `test=reboottriple` (5). Of the existing 528 checks only the 4 version checks changed (`v0.9` → `v0.9.1`); none was removed or weakened. The new unit suites make 110 checks (`test_history`), 51 (`test_rtc`), 62 (`test_logbuf`) and 164 (`test_shell_091`); `test_shell.c` and `test_shell_ps09.c` are unchanged and still pass, proving that every 0.9 output is the same without the new context field. VirtualBox: 49 of 49 expected lines (the 33 of 0.9 plus 16 for the new commands and the reboot).

**Stack peak baseline (0.8):** the kernel stack's high-water mark is **1480 of 16384 bytes**, the same after booting, after the keyboard flood and after the scripted console session, in QEMU and in VirtualBox. Future milestones can compare against it (processes in 0.9 will add their own stacks).

**Stack peaks (0.9):** the boot stack's peak is **1496 bytes** (1480 in 0.8). It is again the same after booting, after the keyboard flood and after the console session, in QEMU and in VirtualBox. Each kernel thread has its own 16 KiB stack and logs its final peak when it exits. The test threads use 192 to 1000 bytes, except the churn test's deliberately deep thread (8496 bytes).

**Stack peak (0.9.1):** **1512 bytes** at "System ready.", after the keyboard flood and after the scripted console session, in QEMU and VirtualBox alike. A VirtualBox console session with `ps`, `version` and `mem` reached 1544.

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
  - **Longer turns inside guarded sections (deferred switch, policy (a)).** A switch that falls due while preemption is disabled waits for the next interrupt after the count is back at 0. A thread that spends almost all its time inside guarded sections (for example allocating frames in a loop) can therefore run longer than the 5-tick slice. Correctness and progress are unaffected. Every scheduler diagnostic logs its longest turn: `framethreads` measured up to **77 ticks** during the stages (30 in the final regression run), `heapthreads` 14, and `preemptoff` 21 (it holds the guard for 20 ticks on purpose). To be measured again in 0.10.
  - **VirtualBox delivers timer ticks in bursts** after the host has delayed the VM, so a 5-tick slice can run out within microseconds of guest time, even in the middle of a short cooperative turn. This is how the emulated PIT behaves, not a kernel fault. The boot self-test and `test=threads` allow for it (decision "option (A)" in the decision log). In all 34 VirtualBox boots of the final acceptance run the ping-pong saw 0 preemptions, so the relaxed branch is proven in QEMU (`test=pingpongpreempt`) but not yet observed in VirtualBox.
  - **The full test suite takes about 15 minutes** and must be run detached in the Codespace, on a hash-verified snapshot (see [development.md](development.md)), so that a tool's time limit can't cut it off.
  - Kernel threads only: no user mode, per-process address spaces or system calls yet (0.10).
  - One CPU only: the preemption-disable count can replace locks only because nothing else runs at the same time. There is no SMP support.
  - Round robin only, no priorities; a fixed 50 ms slice; at most 64 threads (thread 0 plus 63), each with a 16 KiB stack.
  - Thread 0 (the console and idle loop) never blocks: it waits by polling (yield, or `hlt` when nothing else can run).
  - Only thread 0 writes to the screen; other threads log to serial with `klogf` (a breach panics).
  - The heap is still not interrupt-safe (it is now safe between threads).
  - Thread stack peaks have the same limits as the boot stack's: they can read a few bytes too low, never too high.
  - Still open from 0.8: RAM above 4 GiB; no direct map; 4 KiB pages only; every page fault is fatal; the ISO isn't reproducible; `test_reserve` depends on the load address; the `cpu` "Features:" line can wrap; the "no CPUID" path isn't executed.
- **0.9.1:**
  - **HIGH PRIORITY (security): every key press is logged.** Since 0.4 the kernel writes each key event (scancode, key and character) to the serial log, and since 0.9.1 that log is also kept in memory and shown by `dmesg`. Once passwords or other secrets can be typed (`passwd`, `sudo`, a login), this would record them. **Key-event logging must be off by default (enabled only for tests and diagnostics) before any password or secret input exists.** It stays as it is in 0.9.1, because existing tests depend on these lines; the fix is planned for 0.9.2, without weakening any existing test.
  - **The RTC's clock basis differs between machines:** VirtualBox keeps the hardware clock on the host's local time (2 hours ahead of UTC on the test host), QEMU on UTC. `date` shows the RTC as it is, labelled "time zone not set". A future time-zone setting should let the user choose whether the hardware clock holds UTC or local time.
  - `date` assumes the century is 20 (years 2000–2099): the CMOS century register is only found through ACPI, which isn't read yet. The date can't be set.
  - **`dmesg` shows only what `kprintf`/`klogf` wrote.** The boot screen's status lines (`GDT loaded: OK` …) and the console's command output go to the serial port directly and aren't in it (a documented gap; changing it would mean changing the protected `serial.c`). Because every key press is logged, the newest lines of `dmesg` are usually the keystrokes of the command itself.
  - `hostname` is fixed (`madire`) until settings can be stored; `history` keeps the last 16 lines and is lost at restart.
  - `reboot` needs the keyboard controller (or the triple-fault fallback); there is no ACPI reset, `shutdown` or `poweroff` yet.
  - The console still has no scrollback: output that scrolls off the top is lost (scrollback is planned for 0.9.2).

## 4. Roadmap

| Milestone | Goal | Status |
|---|---|---|
| **0.8** | Higher-half kernel: the kernel runs at `0xFFFFFFFF80000000` + physical (PML4 slot 511) and the lower half is empty | done |
| **0.9** | Processes, part 1: kernel threads and a preemptive scheduler | done |
| **0.9.1** | Shell commands needing no new kernel foundations: `history`, `uname`, `hostname`, `date`, `dmesg`, `reboot`, `help <command>`, `<command> --help` | done |
| **0.9.2** (next) | Console scrollback (the last 1000 lines, Shift+PgUp/PgDn) and key-event logging off by default | planned (scrollback decisions approved; the key-logging approach is decided in its plan) |
| **0.10** | Processes, part 2: user processes (user mode, one address space per process, system calls, ELF programs) | planned |
| **1.0** | Filesystem: a virtual filesystem layer and an initial RAM disk, then disk filesystems | planned |

### 0.10 items (decided in principle in the 0.9 plan; 0.10 needs its own plan and approval)

- User mode (ring 3): user code and data segments in the GDT, and the TSS's RSP0 set for each thread when it is switched in.
- One PML4 per process, sharing the kernel half: entries 256–511 copied from the kernel's PML4, except slot 510 (the recursive mapping), which points to the process's own PML4. Slot 507 (the thread stacks) exists from boot, so it is shared too.
- `SYSCALL`/`SYSRET` with a subset of the Linux x86-64 system-call numbers and conventions: `write` (1), `sched_yield` (24), `nanosleep` (35), `getpid` (39), `exit` (60), `exit_group` (231); every other number returns `-ENOSYS`.
- An ELF loader for programs delivered as boot modules; shell commands to run and kill programs; `ps` showing processes.
- SMEP and SMAP, considered together with user mode.
- **The `gpf` diagnostic (condition C of the 0.9 plan):** it loads selector `0x28` as "past the end of the GDT". Once user segments are added, `0x28` becomes a real entry, so the 0.10 plan must re-check the diagnostic and say how it still raises a #GP.
- **Measure the longer turns under policy (a) again** (the 0.9 limitation above) once user processes run.
- Still excluded from 0.10: the filesystem, signals, threads inside user processes, SMP, fork/exec, priorities.

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
| **Open decision, HIGH PRIORITY: Madire OS's own licence** (for example MIT, Apache-2.0, GPL-3.0 or proprietary), to be decided before any outside code is included | 0.9.1 | Restates the 0.7-planning rule above as an open decision with high priority |
