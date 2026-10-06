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
| **0.9.1** (next) | Shell commands that need no new kernel foundations: `history` (with Up/Down), `uname`, `hostname`, `date`, `dmesg`, `reboot` (with confirmation), `help <command>` and `<command> --help`. Planned and its decisions approved; it starts after 0.9 and the command standard (`docs/commands.md`) | planned |
| **0.10** | Processes, part 2: user processes. User mode (ring 3), one address space per process, `SYSCALL` with a subset of the Linux x86-64 system calls (for porting Linux applications, Chromium and Wine later), ELF programs loaded from boot modules, commands to run and kill programs | planned |
| 1.0 | Filesystem: a virtual filesystem layer and an initial RAM disk, then disk filesystems | planned |

Order decided in 0.7 planning: 64-bit long mode (0.7), then the higher-half kernel (0.8), then processes and the filesystem (1.0). In 0.9 planning, processes were split in two: kernel threads and scheduling (0.9), then user processes (0.10). The small 0.9.1 was added afterwards for shell commands.

Of the candidate items listed for 0.8, the reproducible build, the stack high-water mark and the boot-module result on screen were included. Making the `test_reserve` unit test independent of the test binary's load address was deferred to a later milestone.

## Later stages (direction only; planned, not committed)

- **Toolchain:** a dedicated `x86_64-elf` cross-compiler if the host compiler ever becomes a problem; CI build on every push (GitHub Actions); `test_reserve` independent of the test binary's load address
- **Memory:** 2 MiB pages for the kernel, a heap that can give memory back, faster `kfree` validation (today it checks the whole heap on every free), per-CPU or interrupt-safe allocation; use RAM above 4 GiB; reclaim ACPI-reclaimable memory after the ACPI tables are parsed; revisit the whole-low-1 MiB reservation when something needs low memory (e.g. starting other CPU cores)
- **Platform:** UEFI boot and an installer, framebuffer graphics (the logo's stroke geometry can then be drawn with smooth edges), SMP (multiple CPUs; real locks replacing the 0.9 preemption-disable count), Local APIC/IOAPIC instead of the 8259 PIC
- **Scheduling:** priorities, a configurable slice, shorter turns inside guarded sections (the 0.9 deferred-switch limitation)
- **Hardware and drivers:** storage (ATA/AHCI/NVMe), disk filesystems, mouse, USB, network, Wi-Fi, Bluetooth, audio, sensors, all behind a Hardware Abstraction Layer
- **Console:** tab completion, scrollback, word-aware wrapping of long output lines (command history is planned for 0.9.1)
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
