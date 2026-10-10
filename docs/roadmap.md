# Roadmap

Each milestone must build, pass `make test`, boot in VirtualBox, and update the docs before the next one starts. [PROGRESS.md](PROGRESS.md) has the commit history, the tests at each milestone, the decision log and the longer-term direction.

| Milestone | Goal | Status |
|---|---|---|
| **0.1** | Bootable ISO: GRUB, 32-bit kernel, boot screen | done (booted in VirtualBox 2026-09-27) |
| **0.2** | CPU foundation: own GDT, IDT, exception handlers, crash screen; PIC remapped and masked; VGA/serial split into `drivers/`; `kprintf`; Madire M logo drawn in text mode | done (booted in VirtualBox 2026-09-27) |
| **0.3** | Memory foundation: validated Multiboot memory map, reservations (low 1 MiB, kernel, boot data, modules), bitmap physical page allocator, boot self-test, real RAM figures on screen | done (booted in VirtualBox 2026-09-28) |
| **0.4** | Hardware interrupts: IRQ layer (registration, mask/unmask, single EOI point); PIT timer at 100 Hz on IRQ0 (uptime, bounded waits); PS/2 keyboard on IRQ1 (set 1 decoder, modifiers, extended keys, 64-event queue); idle event loop; timer/keyboard/IRQ self-tests and diagnostics | done (booted in VirtualBox 2026-09-28) |
| **0.5** | Kernel console and basic command shell: scrolling 80×25 console with hardware cursor; line editor (insert, Backspace, Delete, Left/Right/Home/End, 71 characters); parser; `help`, `clear`, `version`, `uptime`, `mem`, `cpu` (CPUID), `echo`; pure modules unit-tested; console session tested through the real keyboard path | done (booted in VirtualBox 2026-09-28) |
| **0.6** | Paging (virtual memory foundation) and kernel heap: identity-mapped kernel with page 0 unmapped (NULL guard), kernel code read-only (CR0.WP), free RAM unmapped; page tables walk-checked before paging is enabled; recursive page-table access; map/unmap/translate with per-page TLB flushes; physical-access window; decoded page-fault crash line; `kmalloc`/`kcalloc`/`kfree`; `mem` shows paging and heap figures | done (booted in VirtualBox 2026-09-28) |
| **0.7** | 64-bit long mode, kernel still identity-mapped at 1 MiB: 32-bit boot stub (CPU checks, clear message and halt without long mode or PAE, temporary tables, switch to long mode); 64-bit GDT, TSS and IDT; 4-level paging, walk-checked before it is loaded; NX on everything except kernel code; IST1 double-fault stack and unmapped guard pages below the kernel and IST1 stacks; heap moved to `0xFFFFFE8000000000`; 64-bit crash screen; `pfnx` and `stackoverflow` diagnostics; boot-module integrity check; the 32-bit build dropped | done (booted in VirtualBox 2026-09-29) |
| **0.8** | Higher-half kernel: reproducible build (sorted link order, `make repro`); `.devcontainer/` proven on a fresh Codespace; kernel linked and run at `0xFFFFFFFF80000000` + physical (PML4 slot 511, `-mcmodel=kernel`); physical view at `0xFFFF800000000000` (slot 256) for the low 1 MiB and the boot data only; the lower half empty once the kernel's tables are loaded, proven by the walk-check, the self-test and the new `pflow` diagnostic; kernel stack high-water mark (`mem` row); boot-module result on the screen | done (booted in VirtualBox 2026-09-30) |
| **0.9** | Processes, part 1: kernel threads and a preemptive scheduler. Threads with their own 16 KiB stacks and unmapped guards (PML4 slot 507); context switches that keep RFLAGS; round robin with a 50 ms slice, preempting at the end of an interrupt; sleep, join, exit codes, detached threads; a preemption-disable count that makes the heap, frame allocator, paging and output safe between threads; the screen kept to thread 0; a scheduler self-test at every boot; `ps`; two new Diagnostics entries; scripted VirtualBox acceptance (`scripts/vbox-acceptance.ps1`) | done (booted in VirtualBox 2026-10-05) |
| **0.9.1** | Shell commands that need no new kernel foundations: `history` (the last 16 lines, Up/Down recall), `uname [-a]`, `hostname` (fixed), `date` (from the RTC, no time zone), `dmesg [lines]` (a 32 KiB kernel log), `reboot` (with a `yes` confirmation; keyboard-controller reset with a triple-fault fallback), `help <command>` and `<command> --help` for every command; the command standard `docs/commands.md` at MOCS v0.2 | done (booted in VirtualBox 2026-10-06) |
| **0.9.2** | Console scrollback: the last 1000 lines with their colours, Shift+PgUp / Shift+PgDn (24 lines a page), a reverse-colour indicator on the last row while scrolled back, `clear` keeping the screen in scrollback. Key-event logging off by default: only with the boot option `keylog` or during the keyboard diagnostics, before any password input exists; a binding secret-input rule for future prompts. `commands.md` at MOCS v0.3 | done (booted in VirtualBox 2026-10-06) |
| **0.10** | Processes, part 2: user processes. User mode (ring 3) with SMEP, SMAP and CR0.EM; one address space per process, the kernel half frozen at boot; `SYSCALL` with six Linux x86-64 system calls (`write`, `sched_yield`, `nanosleep`, `getpid`, `exit`, `exit_group`; for porting Linux applications, Chromium and Wine later), returning with `IRETQ`; `copy_from_user`; faults in a program contained to that program; 19 built-in test programs; a user-mode self-test at every boot; Diagnostics entries 16–26; the crash screen names SMEP/SMAP faults; the tests read the version once; `commands.md` at MOCS v0.5 | done (booted in VirtualBox 2026-10-09) |
| **0.11** | Processes, part 3: programs and process management. A strict ELF64 loader; programs delivered as boot modules (7 on the default GRUB entry) and loaded through one path; `run`, `run -b`, `kill` (asks first), `ps` with process numbers, Ctrl+C; program output on the screen through a queue drained by thread 0, with back-pressure; policy (b) (turns at most 5 ticks); `timer_ticks()` without `cli`/`sti`; a stall-capture test harness and `scripts/remote-regress.ps1`; `commands.md` at MOCS v0.6 | done (booted in VirtualBox 2026-10-10) |
| (option) | A shell milestone before 1.0: scripts, a non-interactive `kill -y <pid>`, "Did you mean" suggestions, help in sections | option (needs its own plan and approval) |
| 1.0 | Filesystem: a virtual filesystem layer from the start and an initial RAM disk, then disk filesystems | next (needs its own plan and approval) |

**Next options after 0.11:** an optional shell milestone first (below), then **1.0, the filesystem**, designed with a virtual file system (VFS) layer from the start: every filesystem (the initial RAM disk first, disk filesystems later) is reached through it, never directly. Programs could then also be loaded from files, not only from boot modules.

**Candidates for the optional shell milestone** (not yet planned; recorded in [commands.md](commands.md)): a non-interactive `kill -y <pid>` for scripts; "Did you mean" suggestions when a command name is not found (the closest implemented commands by edit distance, for example `hlep` → `help`; at most 79 columns; nothing run automatically); and help organised in sections like the Linux manual's (user commands, administration commands, the system-call reference).

**Test infrastructure milestone** (planned for when a full regression exceeds about 60 minutes; 0.11's runs took 46–53): parallel QEMU runs, test tiers (a quick set per stage, the full set before TESTED), consider a larger Codespace or a KVM-capable runner (or the local Kali build VM), and screenshot-comparison tests for graphics. Also: `scripts/remote-regress.ps1` downloading the per-boot serial logs, and bounded waits in every diagnostic.

Order decided in 0.7 planning: 64-bit long mode (0.7), then the higher-half kernel (0.8), then processes and the filesystem (1.0). In 0.9 planning, processes were split in two: kernel threads and scheduling (0.9), then user processes (0.10); in 0.10 planning, programs and process management were split off into 0.11 (done 2026-10-10). The small 0.9.1 (shell commands) and 0.9.2 (scrollback, key logging off by default) were added afterwards, before 0.10.

Of the candidate items listed for 0.8, the reproducible build, the stack high-water mark and the boot-module result on screen were included. Making the `test_reserve` unit test independent of the test binary's load address was deferred to a later milestone.

## Later stages (direction only; planned, not committed)

- **Toolchain:** a dedicated `x86_64-elf` cross-compiler if the host compiler ever becomes a problem; CI build on every push (GitHub Actions); `test_reserve` independent of the test binary's load address
- **Memory:** 2 MiB pages for the kernel, a heap that can give memory back, faster `kfree` validation (today it checks the whole heap on every free), per-CPU or interrupt-safe allocation; use RAM above 4 GiB; reclaim ACPI-reclaimable memory after the ACPI tables are parsed; revisit the whole-low-1 MiB reservation when something needs low memory (e.g. starting other CPU cores)
- **Platform:** UEFI boot and an installer, framebuffer graphics (the logo's stroke geometry can then be drawn with smooth edges), SMP (multiple CPUs; real locks replacing the 0.9 preemption-disable count), Local APIC/IOAPIC instead of the 8259 PIC
- **Scheduling:** priorities, a configurable slice (shorter turns inside guarded sections came with policy (b) in 0.11)
- **Processes (after 0.11):** saving FPU/SSE state so programs can use it (CR0.EM is set in 0.10); threads inside a process, signals, `fork`/`exec`; returning with `SYSRET` where it is safe; growing user stacks; atomic user copies before any unmapping system call (the binding check-then-copy rule)
- **Hardware and drivers:** storage (ATA/AHCI/NVMe), disk filesystems, mouse, USB, network, Wi-Fi, Bluetooth, audio, sensors, all behind a Hardware Abstraction Layer
- **Console:** tab completion, word-aware wrapping of long output lines, history and scrollback kept across restarts (command history exists since 0.9.1, scrollback since 0.9.2)
- **Input and time:** keyboard LEDs, non-US layouts and input methods, Num Lock state; RTC interrupts, HPET or a calibrated TSC for finer timing
- **Security:** the Madire Security Platform (see [PROGRESS.md](PROGRESS.md))
- **User experience:** graphics and desktop, settings, accessibility, languages, power management, backup and recovery, built-in apps, system utilities
- **Apps and distribution:** application runtime, the Madire Browser, the Madire Store, update system
- **Later:** AI features, virtualisation and containers, speech, enterprise management, media codecs (licensing required)

Phones (likely ARM64) come after computers and laptops, which is why CPU-specific code stays under `kernel/arch/`.

## Modernisation track (planned, not committed to milestones yet)

### 1. Legacy pieces used today, and their modern replacements

| Today (simple, legacy) | Modern replacement (later) |
|---|---|
| BIOS boot | UEFI boot, plus Secure Boot |
| PIC interrupt controller (8259) | APIC / x2APIC |
| PIT timer | APIC timer / HPET / TSC |
| One CPU core | Multi-core (SMP) |
| VGA text mode (80×25) | A graphics framebuffer, then GPU acceleration |
| PS/2 keyboard | USB keyboards, mice and touchpads; Bluetooth |
| RAM below 4 GiB only | All RAM |
| No storage, no network | NVMe and SATA disks, Wi-Fi, Ethernet, Bluetooth |

These legacy pieces were chosen **deliberately, as the simplest starting point**: each is small, well documented and works in QEMU and VirtualBox without drivers for complex hardware, so the kernel's foundations could be built and tested one step at a time. They are temporary by design: each one must be replaced over time.

### 2. What makes Madire OS feel modern to its users (goals)

- A fast, polished graphical desktop, with touch and high-resolution screen support.
- App sandboxing and permissions (camera, microphone, location).
- Encryption by default (Madire Vault), Secure Boot, and automatic security updates.
- Power management for laptops (sleep, battery life).
- Madire AI built in, safely: it never changes the system without informing the user first.
- A Store with web apps, open-source apps and Madire's own apps from day one.

### 3. Open decision for the drivers phase

- Whether new drivers and components are written in **Rust** (for memory safety), alongside the existing C kernel. To be decided when the drivers phase is planned, not now.

## Applications and compatibility strategy (planned, not tied to milestones yet)

**Goal:** Madire OS runs most of the applications its users need: its own and native apps, plus four compatibility layers.

### 1. Compatibility layers

| Layer | How | Depends on |
|---|---|---|
| **Windows apps** | Port Wine (LGPL; licence to be verified), with Proton-style improvements for games, to run Windows `.exe` and `.msi` programs | Linux/POSIX compatibility (the next layer), graphics, audio |
| **Linux apps** | Run Linux programs through the Linux x86-64 system-call interface chosen in 0.10, extended over time | Processes and system calls (0.10), the filesystem, then more of the Linux interface |
| **Virtualisation** | Run a complete Windows or Linux guest inside Madire OS (with the user's own licence), for the apps nothing else can run | Hardware virtualisation support (Intel VT-x / AMD-V) |
| **Web apps** | The Madire Browser (Chromium-based) and installable web apps, such as Teams, Outlook, Gmail, Microsoft 365 online, WhatsApp and Zoom | Graphics, networking, the browser port (through the Linux layer) |

- **Known exception:** macOS-only apps can't be supported, because of Apple's licence terms and its private frameworks. Most macOS apps have Windows or web equivalents, which the other layers cover.
- **Design rule:** later milestones (processes, filesystem, graphics, audio, networking) must not make choices that block these layers. Any such conflict is raised before it is implemented.

### 2. Where applications come from, over time

| Stage | Sources |
|---|---|
| Early | Madire's own apps, open-source apps, web apps |
| Middle | The early sources, plus Linux apps and Windows apps through the compatibility layers |
| Mature | All of the above, plus commercial apps made for Madire OS |

### 3. Open-source policy

- Open source is the main early source of apps, for example Chromium, LibreOffice, VLC, Thunderbird, GIMP and development tools.
- Every project included needs a licence check and approval first. GPL source obligations and trademark rules (for example Firefox's) are respected.
- The browser and every other internet-facing app must receive regular security updates.
- Madire's identity lives in the system: the kernel, the desktop, the Madire Store, the security platform, Madire AI and the settings.

### 4. Licences for commercial software

- Users bring their own licences for commercial services (for example Microsoft 365) and for Windows in a virtual machine.
- Madire OS never redistributes a proprietary app without the vendor's agreement.

### 5. Open decision: Madire OS's own licence (HIGH PRIORITY)

Madire OS has no licence yet. The candidates include MIT, Apache-2.0, GPL-3.0 or a proprietary licence. **It must be decided before any outside code is included**, because the choice decides which open-source code can be combined with the kernel and how.
