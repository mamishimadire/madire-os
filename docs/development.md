# Development Guide

## Repository layout

```
Makefile              build rules (Linux)
boot/boot.asm         Multiboot header + entry point (NASM)
kernel/kernel.c/.h    kernel (C)
linker/linker.ld      kernel memory layout
iso/boot/grub/        GRUB config copied into the ISO
scripts/
  install-deps.sh     installs the toolchain on Ubuntu
  build.ps1           Windows: build inside the GitHub Codespace
  run.ps1             Windows: boot the ISO in VirtualBox
docs/                 documentation
build/                build output (git-ignored)
```

`include/` (shared public headers) and `tests/` (test programs) will be added when there is something to put in them. For now the tests are the `make test` target.

**Change from the original plan:** the plan listed `iso/boot/` and `iso/grub/` separately. GRUB looks for its config at `/boot/grub/grub.cfg` inside the ISO, so the source tree mirrors that as `iso/boot/grub/grub.cfg`.

## Toolchain

| Tool | Package (Ubuntu) | Used for |
|---|---|---|
| `gcc -m32` | `build-essential`, `gcc-multilib` | Compiling C into 32-bit freestanding objects |
| `ld -m elf_i386` | `binutils` | Linking with `linker/linker.ld` |
| `nasm` | `nasm` | Assembling `boot/boot.asm` |
| `grub-mkrescue` | `grub-pc-bin`, `grub-common` | Building the BIOS-bootable ISO |
| `xorriso`, `mtools` | `xorriso`, `mtools` | Used internally by `grub-mkrescue` |
| `qemu-system-i386` | `qemu-system-x86` | Headless boot test |

**Compiler note:** the host GCC works because the kernel is built `-ffreestanding -nostdlib`, so nothing from the Linux C library ends up in it. The OSDev community recommends a dedicated `i686-elf-gcc` cross-compiler to rule out host assumptions completely. If one is on `PATH`, the Makefile picks it up automatically. Building it becomes worthwhile once the kernel needs `libgcc`.

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
| `-Wall -Wextra -Werror` | Warnings stop the build |

## Conventions

- Every milestone must build, boot in VirtualBox, and update these docs before it counts as done.
- Architecture changes are explained in the docs before they are made.
- Kernel code prefixes output helpers with `k` (`kputs`) so they aren't confused with C library functions.
