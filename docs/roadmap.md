# Roadmap

Each milestone must build, pass `make test`, boot in VirtualBox, and update the docs before the next one starts.

| Milestone | Goal | Status |
|---|---|---|
| **0.1** | Bootable ISO: GRUB, 32-bit kernel, boot screen | done (booted in VirtualBox 2026-09-27) |
| 0.2 | CPU foundation: own GDT, IDT, exception handlers (e.g. a readable "divide by zero" panic screen); split VGA/serial into `drivers/`; `kprintf` | planned |
| 0.3 | Memory: parse the Multiboot memory map, physical frame allocator, report real RAM size | planned |
| 0.4 | Interrupts: PIC remap, timer (PIT) tick, keyboard driver, scrolling text console | planned |
| 0.5 | Paging and kernel heap (`kmalloc`) | planned |
| 0.6 | Dedicated `i686-elf` cross-compiler; CI build on every push (GitHub Actions) | planned |
| 0.7 | Tasks and a simple round-robin scheduler (kernel threads) | planned |
| 0.8 | User mode, system calls, first user program loaded from a GRUB module | planned |
| 0.9 | VFS and initial RAM disk | planned |
| 1.0 | Interactive shell on a text console | planned |

## Later stages (order to be decided)

- **Platform:** 64-bit (x86_64) kernel, EFI boot + framebuffer graphics, SMP (multiple CPUs)
- **Drivers:** storage (ATA/AHCI), disk filesystem, mouse, USB, network (virtio/e1000), audio, all behind a Hardware Abstraction Layer
- **Security:** users, permissions, application permissions, secure boot and update architecture
- **Networking:** TCP/IP stack
- **User experience:** desktop environment, settings, application runtime
- **Distribution:** Madire Store, update system
