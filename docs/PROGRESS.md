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
| **0.7** | this milestone's commit (tag `v0.7`) | 2026-09-29 | 64-bit long mode: 4-level paging, NX, TSS with an IST1 double-fault stack, stack guard pages |

The 0.7 commit can't list its own hash; `git log -1 v0.7` gives it.

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

Each milestone also passed its VirtualBox acceptance test on the VM `MadireOS-Test` before it was committed.

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
  - The kernel is still identity-mapped at 1 MiB; the higher-half move is 0.8.
  - RAM above 4 GiB is still not used.
  - No direct map of physical memory: one page at a time, through the physical-access window.
  - The stack guard pages prove normal use never reaches them, but there is no stack high-water measurement (how close the stack gets). To be considered before processes.
  - `test_reserve` depends on the host test binary's load address (the unit tests are linked at 256 MiB). To be made independent of it in a later milestone.
  - The boot-module integrity result is on the serial log only, so a machine without a serial port can't show it.
  - The `cpu` command's "Features:" line can be longer than 80 columns and wrap (as in 0.5 and 0.6).
  - The stub's "no CPUID" path can't be run in QEMU (every QEMU CPU model has CPUID); it has been reviewed but not executed.
  - The build isn't bit-for-bit reproducible: the Makefile links the objects in the order `find` returns the sources, which depends on the file system, so two builds of the same sources can differ in layout (a few bytes of alignment padding). Each test run tests the binary it built.
  - `.devcontainer/` hasn't been used to create a Codespace yet (the current Codespace predates it).

## 4. Roadmap

| Milestone | Goal | Status |
|---|---|---|
| **0.8** (next) | Higher-half kernel: the kernel moves to `0xFFFFFFFF80000000` (PML4 slot 511, reserved for it in 0.7) and the low identity mapping is removed | planned |
| **0.9** | Processes: tasks and a scheduler, user mode, system calls, a first user program | planned |
| **1.0** | Filesystem: a virtual filesystem layer and an initial RAM disk, then disk filesystems | planned |

Candidate items for 0.8 (to be decided in its plan):

- Show the boot-module integrity result on the boot screen's message line, for real hardware without a serial port (in 0.7 it goes to the serial log only).
- A stack high-water measurement, before processes arrive in 0.9.
- Make `test_reserve` independent of the host test binary's load address.
- A reproducible build: link the objects in a fixed (sorted) order.

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
| Toolchain: Ubuntu GCC in freestanding mode (`-m64 -ffreestanding -mno-red-zone -mgeneral-regs-only -mcmodel=small …`), no cross-compiler; the build fails if any SSE/x87 register is used | 0.7 | `.devcontainer/` and `scripts/install-deps.sh` set up the build environment |
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
