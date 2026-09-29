# Development Guide

## Repository layout

```
Makefile                  build rules (Linux)
.devcontainer/            Codespace / Dev Container definition (runs install-deps.sh)
boot/boot.asm             Multiboot header (a.out kludge) + 32-bit stub: CPU checks,
                          temporary page tables, switch to 64-bit long mode;
                          boot page tables, kernel and IST1 stacks with guard pages
kernel/
  kernel.c/.h             start-up sequence, device start-up, idle loop, version
  multiboot.h             Multiboot 1 structures
  bootmod.c/.h            boot-module integrity check (test modules)  (pure)
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
    vm_layout.h           every virtual-address layout constant (64-bit layout)
    pte.h                 4-level entry bits (incl. NX), index helpers  (pure)
    vmm_core.c/.h         map / unmap / translate via callbacks    (pure)
    paging.c/.h           build, walk-check, load CR3; NX; guard pages; self-test; window
    heap.c/.h             first-fit boundary-tag allocator         (pure)
    kmalloc.c/.h          kernel heap: kmalloc / kcalloc / kfree
    vmtest.c/.h           page-fault, NX, stack-overflow and heap diagnostics
  arch/x86/
    io.h                  port I/O (inb/outb)
    gdt.c/.h, gdt_flush.asm   64-bit segment table + TSS (IST1 double-fault stack)
    idt.c/.h              interrupt table (16-byte gates; vector 8 uses IST1)
    isr.asm               64-bit interrupt entry stubs (iretq)
    interrupts.c/.h       C interrupt dispatcher (exceptions; IRQs -> irq.c)
    irq.c/.h              IRQ handler registration, dispatch, EOI, counters
    pic.c/.h              8259 interrupt controller (remap, mask/unmask, EOI)
    cmos.c/.h             read-only RTC seconds (timer diagnostic only)
    cpuid.c/.h            CPUID availability check + safe leaf reads; CR0, CR4,
                          EFER; NX detection and enabling
    pagefault.h           page-fault error-code decoding       (pure, header only)
drivers/
  vga/vga_text.c/.h       VGA text mode
  serial/serial.c/.h      COM1
  pit/pit.c/.h            PIT channel 0 -> IRQ0
  pit/pit_divisor.h       rate -> divisor maths                 (pure)
  keyboard/ps2kbd.c/.h    i8042 + PS/2 keyboard -> IRQ1
  keyboard/scancode.c/.h  scancode set 1 decoder, US layout     (pure)
linker/linker.ld          kernel memory layout (64-bit, loaded and run at 1 MiB)
iso/boot/grub/grub.cfg    GRUB menu (normal boot + Diagnostics)
scripts/
  install-deps.sh         installs the toolchain on Ubuntu (run by .devcontainer/)
  build.ps1               Windows: build + test inside the GitHub Codespace
  run.ps1                 Windows: boot the ISO in VirtualBox
  logo-preview.py         reference model + PNG preview of the logo
tests/
  unit/                   host unit tests for the pure modules (`make unit`)
  qemu-tests.sh           automated QEMU tests (`make test`)
  memory_check.py         independent recomputation of the kernel's memory figures
  qmp_keys.py             types scripted keys or text into QEMU over QMP
docs/                     documentation (images/ holds reference screenshots;
                          PROGRESS.md: history and decisions; references.md:
                          external projects, reference only)
build/                    build output (git-ignored): madire.elf (symbols),
                          madire.kernel (bootable flat binary), the ISO, logs
```

`include/` (shared public headers) will be added when there is something to put in it.

**Change from the original plan:** the plan listed `iso/boot/` and `iso/grub/` separately. GRUB looks for its config at `/boot/grub/grub.cfg` inside the ISO, so the source tree mirrors that as `iso/boot/grub/grub.cfg`.

## Adding source files

The Makefile compiles every `.c` and `.asm` file under `kernel/` and `drivers/` automatically, so a new file needs no Makefile edit. They are linked in the order `find` lists them, which depends on the file system, so two builds of the same sources can differ by a few bytes of layout (a known limitation; a fixed order is a candidate for 0.8). Includes are written from the repository root, e.g. `#include "drivers/vga/vga_text.h"`. Header dependencies are tracked with `-MMD`. If you add a new top-level source folder, also add it to the upload list in `scripts/build.ps1`.

**Pure modules and unit tests.** A module that doesn't access hardware, print or panic can be unit-tested on the host. Currently these are:

- `kernel/memory/mmap.c`, `pmm.c`, `reserve.c`
- `kernel/memory/pte.h` (header only), `vmm_core.c`, `heap.c`
- `drivers/keyboard/scancode.c`
- `kernel/input/keyqueue.c`
- `drivers/pit/pit_divisor.h` (header only)
- `kernel/console/textbuf.c`, `lineedit.c`
- `kernel/shell/parser.c`, `shell.c`, `cpuinfo.c`
- `kernel/bootmod.c`
- `kernel/arch/x86/pagefault.h` and the inline NX decision in `cpuid.h` (header only)
- the formatter in `kernel/kprintf.c` (`test_kprintf` includes the file, with the two output drivers stubbed)

**Adding a shell command.** Add an entry to the `commands[]` table in `kernel/shell/shell.c`: name, usage, one-line help, maximum number of arguments, and a function. A command must:

- read everything it prints from `struct shell_context`, adding a field there (filled in by `console.c`) if it needs new data;
- write only through the `shell_output` sink;
- stay pure, so its exact output can be unit-tested in `tests/unit/test_shell.c`.

Commands run in normal context, one at a time, and must return promptly.

**Screen output after the console starts:** only the console writes to the screen. Use `klogf()` for serial-only diagnostics, and don't use `kprintf()` for screen output once the console is active (the crash screen is the exception).

To add one:

- Add it to `UNIT_MODULES` in the Makefile.
- Write `tests/unit/test_<name>.c` using `tests/unit/unit.h` (`CHECK`, `CHECK_EQ`, `RUN`). `make unit` finds new test files automatically.

Unit tests are built as ordinary 64-bit Linux programs (`gcc -m64 -no-pie`), so integer and pointer sizes match the kernel. They are linked at 256 MiB (`-Wl,-Ttext-segment=0x10000000`): the memory tests pass the addresses of their static test data as 32-bit physical addresses, so that data must lie below 4 GiB, and `test_reserve` also expects it above its fake 32 MiB machine (a known limitation, to be removed later).

**Linker symbols.** `linker/linker.ld` exports:

- `__kernel_start` / `__kernel_end` (page-aligned; `.bss` includes the boot page tables and the stacks);
- `__text_`, `__rodata_`, `__data_` and `__bss_` `start`/`end`.

`boot/boot.asm` exports `stack_bottom` and `stack_top` (the 16 KiB kernel stack), `kstack_guard` (the unmapped page below it), `ist1_stack_bottom` and `ist1_stack_top` (the 8 KiB double-fault stack) and `ist1_guard`. In C, declare them as `extern char __kernel_end[];` and use only their addresses.

**Using memory.** The API is in `kernel/memory/memory.h`:

- `memory_alloc_page()` returns a physical page address, or **0 when memory is exhausted**, which the caller must handle.
- `memory_free_page(addr)` returns the page. It panics on an unaligned address, a reserved page or a double free, because those are bugs.

Pages are physical addresses. Since 0.6 paging is on and free RAM is **not mapped**, so a page from `memory_alloc_page()` can't be used through its address:

- to touch one physical page briefly: `p = vmm_window_map(phys)`, use `p`, then `vmm_window_unmap()` (one user at a time; normal context only);
- to give it a virtual address: `paging_map(va, phys, PTE_WRITE | paging_nx_flag())` (drop `PTE_WRITE` for read-only; `paging_nx_flag()` is the NX bit when the CPU has NX, else 0; data is never mapped executable), and `paging_unmap(va, &phys)` when done, then free the frame yourself;
- for ordinary kernel data: `kmalloc(size)` / `kcalloc(n, size)` / `kfree(p)` (`kernel/memory/kmalloc.h`). They return `NULL` when the heap can't grow; handle it. A bad `kfree` panics.

Virtual addresses the kernel uses are all in `kernel/memory/vm_layout.h`; add new fixed addresses there, never as literals elsewhere.

**Adding an interrupt-driven device.** The API is in `kernel/arch/x86/irq.h`. Always follow this order:

1. `irq_register(irq, handler)`
2. set up the device
3. clear any pending data
4. `irq_enable_line(irq)`, which refuses a line with no handler and verifies the mask

Start-up panics if the final PIC mask isn't the expected one, so a new line must also be added to `IRQ_ALLOWED_MASK` in `kernel.c` (a deliberate, reviewed step).

**Handler rules** (IRQ context, interrupts off):

- **Allowed:** read the device, update counters and state, push to a queue.
- **Never:** write to VGA or serial, allocate memory (`memory_alloc_page`, `kmalloc`, `kcalloc`, `kfree`; the heap functions panic if called with interrupts off), use the physical-access window, wait, loop without a bound, run diagnostics, or panic over a device problem.

Normal code picks up the results in the idle loop. Use `irq_save()`/`irq_restore()` around reads of multi-word state that a handler writes, such as the 64-bit tick counter or the key queue.

**Waiting.**

- `timer_wait_ticks(n)` sleeps with `hlt`. It returns `false` at once if the timer isn't verified or interrupts are off, so it can't hang.
- Device polling loops must always have a fixed bound (see `ps2kbd.c`).

## Toolchain

| Tool | Package (Ubuntu) | Used for |
|---|---|---|
| `gcc -m64` | `build-essential` | Compiling C into 64-bit freestanding objects |
| `ld -m elf_x86_64` | `binutils` | Linking with `linker/linker.ld` into `build/madire.elf` |
| `objcopy`, `objdump` | `binutils` | `objcopy -O binary` makes the bootable flat image `build/madire.kernel`; `objdump -d` checks that no SSE/x87 register is used |
| `nasm -f elf64` | `nasm` | Assembling `.asm` files (the boot stub mixes 32- and 64-bit code) |
| `grub-mkrescue` | `grub-pc-bin`, `grub-common` | Building the BIOS-bootable ISO |
| `xorriso`, `mtools` | `xorriso`, `mtools` | Used internally by `grub-mkrescue` |
| `qemu-system-x86_64`, `qemu-system-i386` | `qemu-system-x86` | Headless boot and crash tests; `qemu-system-i386` for the "no 64-bit CPU" test |
| `python3` | `python3` | `scripts/logo-preview.py`, `tests/memory_check.py`, `tests/qmp_keys.py` (standard library only) |
| host `gcc -m64` | `build-essential` | host unit tests (`make unit`) |

**Compiler note:** the host GCC works because the kernel is built `-ffreestanding -nostdlib`, so nothing from the Linux C library ends up in it. A dedicated `x86_64-elf-gcc` cross-compiler would rule out host assumptions completely; it isn't needed so far. Two build checks guard against the host compiler's defaults: the link must leave no undefined symbols (no `libgcc` or libc helpers), and the build fails if `objdump` finds any SSE/x87 register in the kernel.

**Boot image:** GRUB and QEMU load `build/madire.kernel`, a flat binary, through the Multiboot 1 "a.out kludge" address fields in `boot/boot.asm` (load address, end of file, end of `.bss`, entry). `build/madire.elf` has the same code with symbols, for `objdump`/`nm` and debugging.

## Build environments

1. **GitHub Codespace (current):** `.devcontainer/devcontainer.json` (Ubuntu 24.04) runs `scripts/install-deps.sh` when the Codespace is created. `scripts/build.ps1` copies the source to `/tmp/madire-os-build` in the Codespace, runs `make iso test` there, and downloads the ISO. It needs `gh auth refresh -h github.com -s codespace` once. A Codespace that restarts loses `/tmp`, so the next build uploads everything again.
2. **WSL2 Ubuntu:** `wsl --install -d Ubuntu` (admin, reboot), then `bash scripts/install-deps.sh` and `make`.
3. **Any Ubuntu machine or CI:** same as WSL.

## Compiler flags

| Flag | Reason |
|---|---|
| `-m64` | 64-bit (x86-64) code |
| `-ffreestanding` | No hosted C library; don't assume `main`, and don't replace code with library builtins |
| `-mno-red-zone` | An interrupt pushes onto the stack just below RSP, so the 128-byte red zone would be overwritten |
| `-mgeneral-regs-only` | No SSE/MMX/x87/AVX registers (the FPU and SSE aren't set up; CR4.OSFXSR stays clear, so SSE would fault) |
| `-mcmodel=small` | The kernel is linked below 2 GiB (at 1 MiB) |
| `-fno-stack-protector` | No `__stack_chk_fail` exists in the kernel |
| `-fno-pic -fno-pie` | Kernel is linked at a fixed address |
| `-fno-asynchronous-unwind-tables` | No `.eh_frame` unwind data in the kernel |
| `-fno-tree-loop-distribute-patterns` | Stops GCC turning loops into `memset`/`memcpy` calls, which the kernel doesn't provide yet |
| `-Wall -Wextra -Werror` | Warnings stop the build |
| `-I.` | Includes are written from the repository root |

## Rules for kernel code

- No floating point and no SSE: neither is set up (`-mgeneral-regs-only`; the build checks the result). Use whole-number maths, as `logo.c` does.
- 64-bit division and modulo are native instructions since 0.7 and fine. 128-bit arithmetic (`__int128`) isn't: it would need `libgcc`, which isn't linked.
- No `__builtin_popcount` (without a POPCNT target it calls `libgcc`). `__builtin_ctz` is fine, because it compiles to `bsf`.
- Data is never executable: map pages with `paging_nx_flag()` (see *Using memory*). Only kernel code (`.text`) runs.
- The kernel stack is 16 KiB with an unmapped guard page below it; a deeper stack ends on the crash screen (double fault). Keep large buffers off the stack (static, or `kmalloc`).
- Diagnostics that shouldn't appear on screen go to `klogf()` (serial only).
- Never allocate memory before `memory_init()`, and never write to a physical page you didn't get from `memory_alloc_page()`.
- Kernel code and read-only data are read-only once paging is on (CR0.WP): writing to a `const` object, or to code, is a page fault.
- Address 0 is never mapped: a NULL dereference is a page fault, not a silent read of the BIOS data area.
- `kmalloc`, `kcalloc` and `kfree` only in normal context with interrupts enabled.
- The kernel owns VGA palette slots 1, 5, 6, 8, 9 and 13 for the logo. Don't use those colours for text.
- Hardware access goes through `kernel/arch/x86/io.h`, not inline `in`/`out` instructions.
- Screen and serial output only from normal context, never from an interrupt handler.
- Only IRQ0 (timer) and IRQ1 (keyboard) may be unmasked; see `IRQ_ALLOWED_MASK`.

## Conventions

- Every milestone must build, pass `make test`, boot in VirtualBox, and update these docs before it counts as done.
- Architecture changes are explained in the docs before they are made.
- Kernel code prefixes output helpers with `k` (`kputs`) so they aren't confused with C library functions.
- Baseline ISOs of each finished milestone are kept outside the repo in `Desktop\madire-os-baseline\`, for regression comparison.
- CPU-specific code stays under `kernel/arch/` (other CPU families, likely ARM64 for phones, may come later).
- External projects are references only; see [references.md](references.md) for the rules.
- Commit and tag messages are plain UTF-8 **without a byte order mark** (PowerShell's default output adds one; write message files with `UTF8Encoding($false)` or use `git commit -m`).
