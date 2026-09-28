# Development Guide

## Repository layout

```
Makefile                  build rules (Linux)
boot/boot.asm             Multiboot header + entry point (NASM)
kernel/
  kernel.c/.h             start-up sequence, device start-up, idle loop, version
  multiboot.h             Multiboot 1 structures
  bootscreen.c/.h         boot screen layout: status column, memory panel, live lines
  console/
    textbuf.c/.h          80x25 console character grid, scrolling    (pure)
    lineedit.c/.h         prompt line editor (71 characters)        (pure)
    console.c/.h          console: VGA drawing, cursor, serial copy, self-test
  shell/
    parser.c/.h           command-line splitter (max 8 words)       (pure)
    shell.c/.h            the seven shell commands                  (pure)
    cpuinfo.c/.h          CPUID register decoding                   (pure)
  timer.c/.h              system tick (IRQ0), uptime, bounded waits, timer self-test
  devtest.c/.h            timer / keyboard / IRQ diagnostics (test=...)
  input/
    keyqueue.c/.h         64-entry key-event queue                (pure)
  logo.c/.h               Madire M logo (stroke geometry + drawing)
  kprintf.c/.h            kprintf (screen + serial), klogf (serial), ksnprintf
  panic.c/.h              crash screen (panic, panicf, panic_exception)
  memory/
    mmap.c/.h             memory map validation + iteration      (pure)
    reserve.c/.h          memory that must never be allocated    (pure)
    pmm.c/.h              bitmap physical page allocator         (pure)
    memory.c/.h           set-up, statistics, alloc/free API
    memtest.c/.h          boot self-test + diagnostic memory tests
  arch/x86/
    io.h                  port I/O (inb/outb)
    gdt.c/.h, gdt_flush.asm   segment table
    idt.c/.h              interrupt table
    isr.asm               interrupt entry stubs
    interrupts.c/.h       C interrupt dispatcher (exceptions; IRQs -> irq.c)
    irq.c/.h              IRQ handler registration, dispatch, EOI, counters
    pic.c/.h              8259 interrupt controller (remap, mask/unmask, EOI)
    cmos.c/.h             read-only RTC seconds (timer diagnostic only)
    cpuid.c/.h            CPUID availability check + safe leaf reads, CR0
drivers/
  vga/vga_text.c/.h       VGA text mode
  serial/serial.c/.h      COM1
  pit/pit.c/.h            PIT channel 0 -> IRQ0
  pit/pit_divisor.h       rate -> divisor maths                 (pure)
  keyboard/ps2kbd.c/.h    i8042 + PS/2 keyboard -> IRQ1
  keyboard/scancode.c/.h  scancode set 1 decoder, US layout     (pure)
linker/linker.ld          kernel memory layout
iso/boot/grub/grub.cfg    GRUB menu (normal boot + Diagnostics)
scripts/
  install-deps.sh         installs the toolchain on Ubuntu
  build.ps1               Windows: build + test inside the GitHub Codespace
  run.ps1                 Windows: boot the ISO in VirtualBox
  logo-preview.py         reference model + PNG preview of the logo
tests/
  unit/                   host unit tests for the pure modules (`make unit`)
  qemu-tests.sh           automated QEMU tests (`make test`)
  memory_check.py         independent recomputation of the kernel's memory figures
  qmp_keys.py             types scripted keys or text into QEMU over QMP
docs/                     documentation (images/ holds reference screenshots)
build/                    build output (git-ignored)
```

`include/` (shared public headers) will be added when there is something to put in it.

**Change from the original plan:** the plan listed `iso/boot/` and `iso/grub/` separately. GRUB looks for its config at `/boot/grub/grub.cfg` inside the ISO, so the source tree mirrors that as `iso/boot/grub/grub.cfg`.

## Adding source files

The Makefile compiles every `.c` and `.asm` file under `kernel/` and `drivers/` automatically, so a new file needs no Makefile edit. Includes are written from the repository root, e.g. `#include "drivers/vga/vga_text.h"`. Header dependencies are tracked with `-MMD`. If you add a new top-level source folder, also add it to the upload list in `scripts/build.ps1`.

**Pure modules and unit tests.** A module that doesn't access hardware, print or panic can be unit-tested on the host. Currently these are:

- `kernel/memory/mmap.c`, `pmm.c`, `reserve.c`
- `drivers/keyboard/scancode.c`
- `kernel/input/keyqueue.c`
- `drivers/pit/pit_divisor.h` (header only)
- `kernel/console/textbuf.c`, `lineedit.c`
- `kernel/shell/parser.c`, `shell.c`, `cpuinfo.c`

**Adding a shell command.** Add an entry to the `commands[]` table in `kernel/shell/shell.c`: name, usage, one-line help, maximum number of arguments, and a function. A command must:

- read everything it prints from `struct shell_context`, adding a field there (filled in by `console.c`) if it needs new data;
- write only through the `shell_output` sink;
- stay pure, so its exact output can be unit-tested in `tests/unit/test_shell.c`.

Commands run in normal context, one at a time, and must return promptly.

**Screen output after the console starts:** only the console writes to the screen. Use `klogf()` for serial-only diagnostics, and don't use `kprintf()` for screen output once the console is active (the crash screen is the exception).

To add one:

- Add it to `UNIT_MODULES` in the Makefile.
- Write `tests/unit/test_<name>.c` using `tests/unit/unit.h` (`CHECK`, `CHECK_EQ`, `RUN`). `make unit` finds new test files automatically.

Unit tests are built with `gcc -m32`, so pointers and `uint32_t` addresses match the kernel.

**Linker symbols.** `linker/linker.ld` exports:

- `__kernel_start` / `__kernel_end` (page-aligned);
- `__text_`, `__rodata_`, `__data_` and `__bss_` `start`/`end`.

`boot/boot.asm` exports `stack_bottom` and `stack_top`. In C, declare them as `extern char __kernel_end[];` and use only their addresses.

**Using memory.** The API is in `kernel/memory/memory.h`:

- `memory_alloc_page()` returns a physical page address, or **0 when memory is exhausted**, which the caller must handle.
- `memory_free_page(addr)` returns the page. It panics on an unaligned address, a reserved page or a double free, because those are bugs.

Pages are physical addresses. Writing to them directly only works while paging is off.

**Adding an interrupt-driven device.** The API is in `kernel/arch/x86/irq.h`. Always follow this order:

1. `irq_register(irq, handler)`
2. set up the device
3. clear any pending data
4. `irq_enable_line(irq)`, which refuses a line with no handler and verifies the mask

Start-up panics if the final PIC mask isn't the expected one, so a new line must also be added to `IRQ_ALLOWED_MASK` in `kernel.c` (a deliberate, reviewed step).

**Handler rules** (IRQ context, interrupts off):

- **Allowed:** read the device, update counters and state, push to a queue.
- **Never:** write to VGA or serial, allocate memory, wait, loop without a bound, run diagnostics, or panic over a device problem.

Normal code picks up the results in the idle loop. Use `irq_save()`/`irq_restore()` around reads of multi-word state that a handler writes, such as the 64-bit tick counter or the key queue.

**Waiting.**

- `timer_wait_ticks(n)` sleeps with `hlt`. It returns `false` at once if the timer isn't verified or interrupts are off, so it can't hang.
- Device polling loops must always have a fixed bound (see `ps2kbd.c`).

## Toolchain

| Tool | Package (Ubuntu) | Used for |
|---|---|---|
| `gcc -m32` | `build-essential`, `gcc-multilib` | Compiling C into 32-bit freestanding objects |
| `ld -m elf_i386` | `binutils` | Linking with `linker/linker.ld` |
| `nasm` | `nasm` | Assembling `.asm` files |
| `grub-mkrescue` | `grub-pc-bin`, `grub-common` | Building the BIOS-bootable ISO |
| `xorriso`, `mtools` | `xorriso`, `mtools` | Used internally by `grub-mkrescue` |
| `qemu-system-i386` | `qemu-system-x86` | Headless boot and crash tests |
| `python3` | (preinstalled) | `scripts/logo-preview.py`, `tests/memory_check.py`, `tests/qmp_keys.py` (standard library only) |
| host `gcc -m32` + 32-bit libc | `gcc-multilib` | host unit tests (`make unit`) |

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
- No 64-bit division or modulo: it needs `libgcc`, which isn't linked. Use shifts (`>> 12` for pages, `>> 20` for MB). 64-bit multiply, add and compare are fine.
- No `__builtin_popcount` (needs `libgcc` on i386). `__builtin_ctz` is fine, because it compiles to `bsf`.
- Diagnostics that shouldn't appear on screen go to `klogf()` (serial only).
- Never allocate memory before `memory_init()`, and never write to a physical page you didn't get from `memory_alloc_page()`.
- The kernel owns VGA palette slots 1, 5, 6, 8, 9 and 13 for the logo. Don't use those colours for text.
- Hardware access goes through `kernel/arch/x86/io.h`, not inline `in`/`out` instructions.
- Screen and serial output only from normal context, never from an interrupt handler.
- Only IRQ0 (timer) and IRQ1 (keyboard) may be unmasked; see `IRQ_ALLOWED_MASK`.

## Conventions

- Every milestone must build, pass `make test`, boot in VirtualBox, and update these docs before it counts as done.
- Architecture changes are explained in the docs before they are made.
- Kernel code prefixes output helpers with `k` (`kputs`) so they aren't confused with C library functions.
- Baseline ISOs of each finished milestone are kept outside the repo in `Desktop\madire-os-baseline\`, for regression comparison.
