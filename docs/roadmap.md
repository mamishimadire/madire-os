# Roadmap

Each milestone must build, pass `make test`, boot in VirtualBox, and update the docs before the next one starts.

| Milestone | Goal | Status |
|---|---|---|
| **0.1** | Bootable ISO: GRUB, 32-bit kernel, boot screen | done (booted in VirtualBox 2026-09-27) |
| **0.2** | CPU foundation: own GDT, IDT, exception handlers, crash screen; PIC remapped and masked; VGA/serial split into `drivers/`; `kprintf`; Madire M logo drawn in text mode | done (booted in VirtualBox 2026-09-27) |
| **0.3** | Memory foundation: validated Multiboot memory map, reservations (low 1 MiB, kernel, boot data, modules), bitmap physical page allocator, boot self-test, real RAM figures on screen | done (booted in VirtualBox 2026-09-28) |
| **0.4** | Hardware interrupts: IRQ layer (registration, mask/unmask, single EOI point); PIT timer at 100 Hz on IRQ0 (uptime, bounded waits); PS/2 keyboard on IRQ1 (set 1 decoder, modifiers, extended keys, 64-event queue); idle event loop; timer/keyboard/IRQ self-tests and diagnostics | done (booted in VirtualBox 2026-09-28) |
| **0.5** | Kernel console and basic command shell: scrolling 80×25 console with hardware cursor; line editor (insert, Backspace, Delete, Left/Right/Home/End, 71 characters); parser; `help`, `clear`, `version`, `uptime`, `mem`, `cpu` (CPUID), `echo`; pure modules unit-tested; console session tested through the real keyboard path | done (booted in VirtualBox 2026-09-28) |
| next | Paging and kernel heap (`kmalloc`), listed as 0.5 before the console was brought forward; milestone number to be decided. The memory self-tests write to physical addresses directly; they must go through mappings once paging is on. | planned |
| 0.6 | Dedicated `i686-elf` cross-compiler; CI build on every push (GitHub Actions) | planned |
| 0.7 | Tasks and a simple round-robin scheduler (kernel threads) | planned |
| 0.8 | User mode, system calls, first user program loaded from a GRUB module | planned |
| 0.9 | VFS and initial RAM disk | planned |
| 1.0 | Interactive shell that can run programs. The console, line editing and built-in commands exist since 0.5; history, more commands and running user programs build on them. | planned |

## Later stages (order to be decided)

- **Platform:** 64-bit (x86_64) kernel, EFI boot + framebuffer graphics (the logo's stroke geometry can then be drawn with smooth edges), SMP (multiple CPUs), double-fault stack (TSS)
- **Memory:** use RAM above 4 GiB (needs PAE or 64-bit; 512 MB on `MadireOS-Test` today); reclaim ACPI-reclaimable memory after the ACPI tables are parsed; revisit the whole-low-1 MiB reservation when something needs low memory (e.g. starting other CPU cores)
- **Drivers:** storage (ATA/AHCI), disk filesystem, mouse, USB, network (virtio/e1000), audio, all behind a Hardware Abstraction Layer
- **Console (after 0.5):** command history, tab completion, scrollback, word-aware wrapping of long output lines
- **Input and time (after 0.4):** keyboard LEDs (needs the command/ACK protocol), non-US layouts, Num Lock state; Local APIC/IOAPIC instead of the 8259 PIC; RTC interrupts, HPET or a calibrated TSC if finer timing is needed
- **Security:** users, permissions, application permissions, secure boot and update architecture
- **Networking:** TCP/IP stack
- **User experience:** desktop environment, settings, application runtime
- **Distribution:** Madire Store, update system
