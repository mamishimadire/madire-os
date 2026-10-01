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
| **0.8** | this milestone's commit (tag `v0.8`) | 2026-09-30 | Higher-half kernel: kernel at `0xFFFFFFFF80000000` + physical, lower half empty, physical view for the low 1 MiB and boot data, `pflow` diagnostic, stack high-water mark, boot-module result on screen, reproducible build, `.devcontainer/` proven |

The 0.8 commit can't list its own hash; `git log -1 v0.8` gives it.

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

Each milestone also passed its VirtualBox acceptance test on the VM `MadireOS-Test` before it was committed.

**Stack peak baseline (0.8):** the kernel stack's high-water mark is **1480 of 16384 bytes**, the same after booting, after the keyboard flood and after the scripted console session, in QEMU and in VirtualBox. Future milestones can compare against it (processes in 0.9 will add their own stacks).

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

## 4. Roadmap

| Milestone | Goal | Status |
|---|---|---|
| **0.8** | Higher-half kernel: the kernel runs at `0xFFFFFFFF80000000` + physical (PML4 slot 511) and the lower half is empty | done |
| **0.9** (next) | Processes: tasks and a scheduler, user mode, system calls, a first user program | planned |
| **1.0** | Filesystem: a virtual filesystem layer and an initial RAM disk, then disk filesystems | planned |

The candidate items listed for 0.8 were decided in its plan. Delivered in 0.8:

- the boot-module integrity result on the boot screen's message line, for real hardware without a serial port;
- a stack high-water measurement (kernel stack), before processes arrive in 0.9;
- a reproducible build: the objects are linked in a sorted order, and `make repro` proves two clean builds byte-identical.

Deferred: making `test_reserve` independent of the host test binary's load address.

**0.9 planning must consider Linux/POSIX-compatible system calls**, so that Chromium, Linux applications and Wine could be ported later.

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
