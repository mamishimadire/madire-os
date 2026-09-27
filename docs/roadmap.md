# Roadmap

Each milestone must build, pass `make test`, boot in VirtualBox, and update the docs before the next one starts.

| Milestone | Goal | Status |
|---|---|---|
| **0.1** | Bootable ISO: GRUB, 32-bit kernel, boot screen | done (booted in VirtualBox 2026-09-27) |
| **0.2** | CPU foundation: own GDT, IDT, exception handlers, crash screen; PIC remapped and masked; VGA/serial split into `drivers/`; `kprintf`; Madire M logo drawn in text mode | done (booted in VirtualBox 2026-09-27) |
| **0.3** | Memory foundation: validated Multiboot memory map, reservations (low 1 MiB, kernel, boot data, modules), bitmap physical page allocator, boot self-test, real RAM figures on screen | done (booted in VirtualBox 2026-09-28) |
| 0.4 | Interrupts: timer (PIT) tick, keyboard driver, scrolling text console (unmask IRQs on the PIC set up in 0.2) | planned |
| 0.5 | Paging and kernel heap (`kmalloc`). The memory self-tests write to physical addresses directly; they must go through mappings once paging is on. | planned |
| 0.6 | Dedicated `i686-elf` cross-compiler; CI build on every push (GitHub Actions) | planned |
| 0.7 | Tasks and a simple round-robin scheduler (kernel threads) | planned |
| 0.8 | User mode, system calls, first user program loaded from a GRUB module | planned |
| 0.9 | VFS and initial RAM disk | planned |
| 1.0 | Interactive shell on a text console | planned |

## Later stages (order to be decided)

- **Platform:** 64-bit (x86_64) kernel, EFI boot + framebuffer graphics (the logo's stroke geometry can then be drawn with smooth edges), SMP (multiple CPUs), double-fault stack (TSS)
- **Memory:** use RAM above 4 GiB (needs PAE or 64-bit; 512 MB on `MadireOS-Test` today); reclaim ACPI-reclaimable memory after the ACPI tables are parsed; revisit the whole-low-1 MiB reservation when something needs low memory (e.g. starting other CPU cores)
- **Drivers:** storage (ATA/AHCI), disk filesystem, mouse, USB, network (virtio/e1000), audio, all behind a Hardware Abstraction Layer
- **Security:** users, permissions, application permissions, secure boot and update architecture
- **Networking:** TCP/IP stack
- **User experience:** desktop environment, settings, application runtime
- **Distribution:** Madire Store, update system
