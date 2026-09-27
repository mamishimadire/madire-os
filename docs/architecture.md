# Madire OS Architecture

Status key: **[built]** exists in the current version, **[planned]** is designed but not written yet.

## Boot process [built]

```
VirtualBox power-on
  -> BIOS firmware        finds the ISO in the optical drive (El Torito boot)
  -> GRUB 2               reads iso/boot/grub/grub.cfg, shows menu (3 s)
  -> Multiboot load       GRUB loads build/madire.kernel (ELF) at 1 MiB,
                          switches the CPU to 32-bit protected mode
  -> boot/boot.asm        _start: sets up a 16 KiB stack, calls kernel_main
  -> kernel/kernel.c      kernel_main: checks Multiboot magic, initialises
                          serial + VGA text output, prints the boot screen
  -> halt                 CPU halts (cli; hlt) - nothing to run yet
```

| Stage | File | Responsibility |
|---|---|---|
| Bootloader config | `iso/boot/grub/grub.cfg` | Menu entry, `multiboot` command |
| Multiboot header | `boot/boot.asm` (`.multiboot`) | Magic `0x1BADB002`, flags, checksum; must be within the first 8 KiB of the file |
| Entry point | `boot/boot.asm` (`_start`) | Stack setup; passes `eax` (magic) and `ebx` (info pointer) to C |
| Memory layout | `linker/linker.ld` | Kernel linked at 1 MiB; `.text`, `.rodata`, `.data`, `.bss` 4 KiB-aligned |
| Kernel | `kernel/kernel.c` | Boot screen output |

**Why GRUB/Multiboot:** GRUB already handles BIOS disk access, reading the ISO filesystem and switching the CPU into protected mode. That lets the project start with kernel code instead of a hand-written boot sector. A custom bootloader could replace it later without changing the kernel's entry contract.

**Why 32-bit first:** Multiboot (version 1) hands over in 32-bit protected mode. Moving to 64-bit long mode means setting up paging first, so it gets its own milestone.

## Kernel [built: entry only]

Planned layout as the kernel grows:

| Subsystem | Directory | Status |
|---|---|---|
| CPU management (GDT, IDT, exceptions) | `kernel/arch/x86/` | planned (0.2) |
| Interrupt management (PIC/APIC, IRQs) | `kernel/arch/x86/` | planned (0.2) |
| Memory management (physical frames, paging, heap) | `kernel/mm/` | planned (0.3) |
| Process management and scheduler | `kernel/proc/` | planned |
| System calls | `kernel/syscall/` | planned |

## Memory [planned]

Right now the kernel uses only its own image at 1 MiB, a 16 KiB static stack, and the VGA buffer at `0xB8000`. The Multiboot information pointer (which includes the BIOS memory map) is passed to `kernel_main` but not read yet. Milestone 0.3 will build the physical frame allocator from it.

## CPU [planned]

The kernel still runs on GRUB's GDT, and interrupts are disabled. Milestone 0.2 installs its own GDT and IDT and adds exception handlers.

## Drivers [minimal]

| Driver | Status |
|---|---|
| VGA text mode (80x25) | minimal, inside `kernel.c` |
| Serial COM1 | minimal, inside `kernel.c` (used by the automated test and for debug logs) |
| Keyboard, mouse, storage, USB, network, audio | planned |
| Framebuffer / EFI GOP display | planned |

The drivers move out of `kernel.c` into `drivers/` behind a Hardware Abstraction Layer once there is more than one of each kind.

## Hardware Abstraction Layer [planned]

This will be the only layer that touches port I/O, MMIO and CPU-specific instructions. Drivers and the kernel call HAL interfaces instead of `inb`/`outb` directly.

## Security [planned]

Users, permissions, application permissions, and a secure boot/update chain. Designing these needs process isolation (user mode and paging) first.

## Filesystem [planned]

A virtual filesystem (VFS) layer first, then an initial RAM disk loaded by GRUB as a Multiboot module, then disk filesystems.

## System services [planned]

User-mode services (init, logging, device manager) once user mode and system calls exist.

## User interface [planned]

Text console first (keyboard input plus a scrolling terminal). The desktop environment, settings, application runtime, Madire Store and update system come much later. They depend on the framebuffer driver, input drivers, a filesystem and processes.
