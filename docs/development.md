# Development Guide

## Repository layout

```
Makefile                  build rules (Linux)
boot/boot.asm             Multiboot header + entry point (NASM)
kernel/
  kernel.c/.h             start-up sequence, version, Multiboot info
  bootscreen.c/.h         boot screen layout and status lines
  logo.c/.h               Madire M logo (stroke geometry + drawing)
  kprintf.c/.h            console output to screen + serial
  panic.c/.h              crash screen
  arch/x86/
    io.h                  port I/O (inb/outb)
    gdt.c/.h, gdt_flush.asm   segment table
    idt.c/.h              interrupt table
    isr.asm               interrupt entry stubs
    interrupts.c/.h       C interrupt dispatcher
    pic.c/.h              8259 interrupt controller
drivers/
  vga/vga_text.c/.h       VGA text mode
  serial/serial.c/.h      COM1
linker/linker.ld          kernel memory layout
iso/boot/grub/grub.cfg    GRUB menu (normal boot + Diagnostics)
scripts/
  install-deps.sh         installs the toolchain on Ubuntu
  build.ps1               Windows: build + test inside the GitHub Codespace
  run.ps1                 Windows: boot the ISO in VirtualBox
  logo-preview.py         reference model + PNG preview of the logo
tests/qemu-tests.sh       automated QEMU tests (`make test`)
docs/                     documentation (images/ holds reference screenshots)
build/                    build output (git-ignored)
```

`include/` (shared public headers) will be added when there is something to put in it.

**Change from the original plan:** the plan listed `iso/boot/` and `iso/grub/` separately. GRUB looks for its config at `/boot/grub/grub.cfg` inside the ISO, so the source tree mirrors that as `iso/boot/grub/grub.cfg`.

## Adding source files

The Makefile compiles every `.c` and `.asm` file under `kernel/` and `drivers/` automatically, so a new file needs no Makefile edit. Includes are written from the repository root, e.g. `#include "drivers/vga/vga_text.h"`. Header dependencies are tracked with `-MMD`. If you add a new top-level source folder, also add it to the upload list in `scripts/build.ps1`.

## Toolchain

| Tool | Package (Ubuntu) | Used for |
|---|---|---|
| `gcc -m32` | `build-essential`, `gcc-multilib` | Compiling C into 32-bit freestanding objects |
| `ld -m elf_i386` | `binutils` | Linking with `linker/linker.ld` |
| `nasm` | `nasm` | Assembling `.asm` files |
| `grub-mkrescue` | `grub-pc-bin`, `grub-common` | Building the BIOS-bootable ISO |
| `xorriso`, `mtools` | `xorriso`, `mtools` | Used internally by `grub-mkrescue` |
| `qemu-system-i386` | `qemu-system-x86` | Headless boot and crash tests |
| `python3` | (preinstalled) | `scripts/logo-preview.py` only |

**Compiler note:** the host GCC works because the kernel is built `-ffreestanding -nostdlib`, so nothing from the Linux C library ends up in it. The OSDev community recommends a dedicated `i686-elf-gcc` cross-compiler to rule out host assumptions completely. If one is on `PATH`, the Makefile picks it up automatically. Building it becomes worthwhile once the kernel needs `libgcc`, for example for 64-bit division.

## Build environments

1. **GitHub Codespace (current):** `scripts/build.ps1` copies the source to `/tmp/madire-os-build` in the Codespace, runs `make iso test` there, and downloads the ISO. It needs `gh auth refresh -h github.com -s codespace` once.
2. **WSL2 Ubuntu:** `wsl --install -d Ubuntu` (admin, reboot), then `bash scripts/install-deps.sh` and `make`.
3. **Any Ubuntu machine or CI:** same as WSL.

## Compiler flags

| Flag | Reason |
|---|---|
| `-ffreestanding` | No hosted C library; don't assume `main`, and don't replace code with library builtins |
| `-fno-stack-protector` | No `__stack_chk_fail` exists in the kernel |
| `-fno-pic -fno-pie` | Kernel is linked at a fixed address |
| `-fno-tree-loop-distribute-patterns` | Stops GCC turning loops into `memset`/`memcpy` calls, which the kernel doesn't provide yet |
| `-Wall -Wextra -Werror` | Warnings stop the build |
| `-I.` | Includes are written from the repository root |

## Rules for kernel code

- No floating point: the FPU isn't set up. Use whole-number maths, as `logo.c` does.
- No 64-bit division or modulo: it needs `libgcc`, which isn't linked. 64-bit multiply and compare are fine.
- The kernel owns VGA palette slots 1, 5, 6, 8, 9 and 13 for the logo. Don't use those colours for text.
- Hardware access goes through `kernel/arch/x86/io.h`, not inline `in`/`out` instructions.

## Conventions

- Every milestone must build, pass `make test`, boot in VirtualBox, and update these docs before it counts as done.
- Architecture changes are explained in the docs before they are made.
- Kernel code prefixes output helpers with `k` (`kputs`) so they aren't confused with C library functions.
- The v0.1 baseline ISO is kept outside the repo in `Desktop\madire-os-baseline\`, for regression comparison.
