# Roadmap

Each milestone must build, pass `make test`, boot in VirtualBox, and update the docs before the next one starts.

| Milestone | Goal | Status |
|---|---|---|
| **0.1** | Bootable ISO: GRUB, 32-bit kernel, boot screen | done (booted in VirtualBox 2026-09-27) |
| **0.2** | CPU foundation: own GDT, IDT, exception handlers, crash screen; PIC remapped and masked; VGA/serial split into `drivers/`; `kprintf`; Madire M logo drawn in text mode | done (booted in VirtualBox 2026-09-27) |
| **0.3** | Memory foundation: validated Multiboot memory map, reservations (low 1 MiB, kernel, boot data, modules), bitmap physical page allocator, boot self-test, real RAM figures on screen | done (booted in VirtualBox 2026-09-28) |
| **0.4** | Hardware interrupts: IRQ layer (registration, mask/unmask, single EOI point); PIT timer at 100 Hz on IRQ0 (uptime, bounded waits); PS/2 keyboard on IRQ1 (set 1 decoder, modifiers, extended keys, 64-event queue); idle event loop; timer/keyboard/IRQ self-tests and diagnostics | done (booted in VirtualBox 2026-09-28) |
| **0.5** | Kernel console and basic command shell: scrolling 80×25 console with hardware cursor; line editor (insert, Backspace, Delete, Left/Right/Home/End, 71 characters); parser; `help`, `clear`, `version`, `uptime`, `mem`, `cpu` (CPUID), `echo`; pure modules unit-tested; console session tested through the real keyboard path | done (booted in VirtualBox 2026-09-28) |
| **0.6** | Paging (virtual memory foundation) and kernel heap: identity-mapped kernel with page 0 unmapped (NULL guard), kernel code read-only (CR0.WP), free RAM unmapped; page tables walk-checked before paging is enabled; recursive page-table access; map/unmap/translate with per-page TLB flushes; physical-access window; decoded page-fault crash line; `kmalloc`/`kcalloc`/`kfree` (first-fit, boundary tags, 16-byte alignment, double/invalid free and corruption detection, grows to a 16 MiB cap); `mem` shows paging and heap figures | done (booted in VirtualBox 2026-09-28) |
| **0.7** (next) | 64-bit (x86_64) and higher-half kernel. All 0.6 layout constants are in `kernel/memory/vm_layout.h`, so the move changes that file, the linker script and the boot code. | planned |
| 0.8 | Processes: tasks and a round-robin scheduler, then user mode, system calls and a first user program loaded from a GRUB module | planned |
| 0.9 | Filesystem: VFS and an initial RAM disk | planned |
| 1.0 | Interactive shell that can run programs. The console, line editing and built-in commands exist since 0.5; history, more commands and running user programs build on them. | planned |

Order decided in 0.6: paging and heap (0.6), then the 64-bit, higher-half kernel, then processes and the filesystem. The earlier "0.6: cross-compiler and CI" item moved to the later stages below.

## Later stages (order to be decided)

- **Toolchain:** dedicated `i686-elf` (or `x86_64-elf`) cross-compiler; CI build on every push (GitHub Actions)
- **Memory (after 0.6):** stack guard page (needs a separate double-fault stack to report it), 2/4 MiB pages, a heap that can give memory back, faster `kfree` validation (today it checks the whole heap on every free), per-CPU or interrupt-safe allocation
- **Platform:** EFI boot + framebuffer graphics (the logo's stroke geometry can then be drawn with smooth edges), SMP (multiple CPUs), double-fault stack (TSS)
- **Memory:** use RAM above 4 GiB (needs PAE or 64-bit; 512 MB on `MadireOS-Test` today); reclaim ACPI-reclaimable memory after the ACPI tables are parsed; revisit the whole-low-1 MiB reservation when something needs low memory (e.g. starting other CPU cores)
- **Drivers:** storage (ATA/AHCI), disk filesystem, mouse, USB, network (virtio/e1000), audio, all behind a Hardware Abstraction Layer
- **Console (after 0.5):** command history, tab completion, scrollback, word-aware wrapping of long output lines
- **Input and time (after 0.4):** keyboard LEDs (needs the command/ACK protocol), non-US layouts, Num Lock state; Local APIC/IOAPIC instead of the 8259 PIC; RTC interrupts, HPET or a calibrated TSC if finer timing is needed
- **Security:** users, permissions, application permissions, secure boot and update architecture
- **Networking:** TCP/IP stack
- **User experience:** desktop environment, settings, application runtime
- **Distribution:** Madire Store, update system
