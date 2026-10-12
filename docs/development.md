# Development Guide

## Repository layout

```
Makefile                  build rules (Linux)
AGENTS.md                 working rules for every coding agent (0.9)
CLAUDE.md                 "@AGENTS.md": Claude Code loads AGENTS.md (0.9)
.devcontainer/            Codespace / Dev Container definition (runs install-deps.sh)
boot/boot.asm             Multiboot header (a.out kludge) + 32-bit stub: CPU checks,
                          temporary page tables, switch to 64-bit long mode, jump
                          to the higher half; boot page tables, kernel and IST1
                          stacks with guard pages
kernel/
  kernel.c/.h             start-up sequence, stack paint, device start-up, idle
                          loop, version; BOOTSCREEN_SAFE() (0.9)
  stackpeak.c/.h          stack high-water mark: paint and measure     (pure)
  logbuf.c/.h             kernel log ring buffer for dmesg (0.9.1)    (pure)
  rtc_time.c/.h           RTC date/time decoding and format (0.9.1)  (pure)
  multiboot.h             Multiboot 1 structures
  bootmod.c/.h            boot-module integrity check (test modules)  (pure)
  bootscreen.c/.h         boot screen layout: status column, memory panel, live lines
  console/
    textbuf.c/.h          80x25 console character grid, scrolling    (pure)
    lineedit.c/.h         prompt line editor (71 characters)        (pure)
    history.c/.h          command history, Up/Down browsing (0.9.1) (pure)
    scrollback.c/.h       scrollback: 1000 lines, paging, indicator (0.9.2) (pure)
    console.c/.h          console: VGA drawing, cursor, serial copy, self-test,
                          history keys, reboot confirmation (0.9.1)
  shell/
    parser.c/.h           command-line splitter (max 8 words)       (pure)
    shell.c/.h            the shell commands (seven; ps in 0.9; six more and
                          help/--help for every command in 0.9.1)   (pure)
    cpuinfo.c/.h          CPUID register decoding                   (pure)
  timer.c/.h              system tick (IRQ0), uptime, bounded waits, timer self-test
  devtest.c/.h            timer / keyboard / IRQ diagnostics (test=...)
  input/
    keyqueue.c/.h         64-entry key-event queue                (pure)
  logo.c/.h               Madire M logo (stroke geometry + drawing)
  kprintf.c/.h            kprintf (screen + serial), klogf (serial), ksnprintf;
                          the kernel log for dmesg (0.9.1)
  panic.c/.h              crash screen (panic, panicf, panic_exception)
  proc/                   kernel threads (0.9) and user processes (0.10)
    sched_core.c/.h       states, run queue, slices, sleep/join/exit, stack
                          slots, turn-order check                     (pure)
    thread.c/.h           thread API, stacks in slot 507, switches (RSP0 and
                          CR3 since 0.10), tick and preemption hooks, screen rule
    switch.asm            switch_to (RFLAGS + callee-saved), trampoline
    preempt.h             preemption-disable count, deferred switch (a)
    schedtest.c/.h        boot self-test and thread diagnostics (test=...)
    proc_core.c/.h        process table: 63 slots, PIDs, results (0.10) (pure)
    userimg.c/.h          built-in image table and checks (0.10)     (pure)
    userentry.h           the IRETQ frame into ring 3 (0.10)  (pure, header only)
    faultop.h             instruction bytes at a fault's RIP (0.10) (pure, header only)
    syscall_core.c/.h     system-call numbers, rules, write lines (0.10) (pure)
    process.c/.h          load, start, kill, wait, return-to-user hook,
                          fault containment (0.10)
    userenter.asm         enter_user: the first IRETQ into ring 3 (0.10)
    syscall_entry.asm     the SYSCALL entry (0.10)
    syscall.c/.h          SYSCALL set-up, dispatcher, write, nanosleep (0.10)
    usertest.c/.h         user-mode boot self-test and diagnostics (0.10)
  memory/
    mmap.c/.h             memory map validation + iteration      (pure)
    reserve.c/.h          memory that must never be allocated    (pure)
    pmm.c/.h              bitmap physical page allocator         (pure)
    memory.c/.h           set-up, statistics, alloc/free API
    memtest.c/.h          boot self-test + diagnostic memory tests
    vm_layout.h           every virtual-address layout constant; phys_view(),
                          kernel_phys() (higher-half layout since 0.8)
    pte.h                 4-level entry bits (incl. NX), index helpers  (pure)
    vmm_core.c/.h         map / unmap / translate via callbacks; the freeze
                          (0.10)                                   (pure)
    paging.c/.h           build, walk-check, load CR3; NX; guard pages; physical
                          view; empty lower half; self-test; window; freeze of
                          the kernel half (0.10)
    uvm_core.c/.h         a process's lower half: user pages (0.10) (pure)
    uspace.c/.h           address spaces: create, enter, map, destroy (0.10)
    usercopy_core.c/.h    user-range checks (0.10)                 (pure)
    usercopy.c/.h         copy_from_user, user_readable (0.10)
    heap.c/.h             first-fit boundary-tag allocator         (pure)
    kmalloc.c/.h          kernel heap: kmalloc / kcalloc / kfree
    vmtest.c/.h           page-fault (incl. pflow), NX, stack-overflow and heap
                          diagnostics
  arch/x86/
    io.h                  port I/O (inb/outb)
    gdt.c/.h, gdt_flush.asm   64-bit segment table + TSS (IST1 double-fault stack;
                          0.10: user segments, IST2, RSP0)
    gdt_desc.h            GDT layout, descriptors, STAR (0.10) (pure, header only)
    idt.c/.h              interrupt table (16-byte gates; vector 8 uses IST1,
                          2 and 18 use IST2)
    isr.asm               64-bit interrupt entry stubs (iretq; clac with SMAP)
    interrupts.c/.h       C interrupt dispatcher (exceptions; IRQs -> irq.c;
                          ring-3 exceptions -> process_fault)
    cpuprot.c/.h          SMEP, SMAP, CR0.EM at boot (0.10)
    irq.c/.h              IRQ handler registration, dispatch, EOI, counters
    pic.c/.h              8259 interrupt controller (remap, mask/unmask, EOI)
    cmos.c/.h             read-only RTC: seconds (timer diagnostic), date and
                          time registers (0.9.1, for date)
    reset.c/.h            restart: keyboard controller, triple fault (0.9.1)
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
user/                     built-in user programs (0.10)
  crt0.asm                _start: main(arg) in RDI, then exit(return value)
  link.ld                 .text at 0x400000, .data/.bss at 0x500000
  syscall.h               the system-call stubs (SYSCALL, Linux x86-64 numbers)
  ulib.h                  a tiny library (no C library yet): strings, numbers,
                          expectations; the kernel addresses tests aim at
  prog/*.c                one file per program (19 in 0.10)
linker/linker.ld          kernel memory layout (64-bit; loaded at 1 MiB, linked and
                          run at 0xFFFFFFFF80000000 + physical since 0.8)
iso/boot/limine/limine.conf  the boot menu (Limine, since 0.13; GRUB's grub.cfg
                          before): the default entry with the program modules,
                          and the Diagnostics directory; the default entry's title
                          holds @MADIRE_OS_VERSION@, replaced from kernel/kernel.h
                          into build/limine.conf (one version source, since 0.12)
scripts/
  install-deps.sh         installs the toolchain on Ubuntu (run by .devcontainer/)
  user-images.sh          checks each built-in program and writes the image
                          table, build/user/images.asm (0.10)
  build.ps1               Windows: build + test inside the GitHub Codespace
  actions-regress.ps1     Windows: the official regression on GitHub Actions
                          (dispatch, collect; 0.12)
  remote-regress.ps1      Windows: the full regression in the Codespace (fallback)
  run.ps1                 Windows: boot the ISO in VirtualBox
  vbox-acceptance.ps1     Windows: scripted VirtualBox acceptance test (0.9)
  check-bootscreen.sh     build check: boot screen only via BOOTSCREEN_SAFE() (0.9)
  logo-preview.py         reference model + PNG preview of the logo
tests/
  unit/                   host unit tests for the pure modules (`make unit`)
  qemu-tests.sh           automated QEMU tests (`make test`); since 0.12 the
                          checks are in parts, sourced in order:
  qemu-lib.sh             helpers and the normal-boot fixture that parts share
  qemu-part-*.sh          the parts: static, 1-4, final (`make test-shard
                          SHARD=<part>` runs one)
  expected-checks.txt     the agreed checks per tag and unit suites (0.12)
  check-counts.sh         compares a run's counts with expected-checks.txt
  combine-shards.sh       combines a sharded Actions run (0.12)
  helper-replay.sh        replays the pattern helpers under mawk and gawk
  memory_check.py         independent recomputation of the kernel's memory figures
  qmp_keys.py             types scripted keys or text into QEMU over QMP
  qmp_screen.py           reads QEMU's VGA text screen over QMP and checks rows (0.9.2)
docs/                     documentation (images/ holds reference screenshots;
                          PROGRESS.md: history and decisions; references.md:
                          external projects, reference only)
build/                    build output (git-ignored): madire.elf (symbols),
                          madire.kernel (bootable flat binary), the ISO, logs
```

`include/` (shared public headers) will be added when there is something to put in it.

**Change from the original plan:** the plan listed `iso/boot/` and `iso/grub/` separately. GRUB looks for its config at `/boot/grub/grub.cfg` inside the ISO, so the source tree mirrors that as `iso/boot/grub/grub.cfg`.

## Adding source files

The Makefile compiles every `.c` and `.asm` file under `kernel/` and `drivers/` automatically, so a new file needs no Makefile edit. Since 0.8 the source lists are sorted (`$(sort …)`), so the link order, and with it the binary, doesn't depend on the order in which the file system lists the files (see *Reproducible build*). Includes are written from the repository root, e.g. `#include "drivers/vga/vga_text.h"`. Header dependencies are tracked with `-MMD`. If you add a new top-level source folder, also add it to the upload list in `scripts/build.ps1`.

**Pure modules and unit tests.** A module that doesn't access hardware, print or panic can be unit-tested on the host. Currently these are:

- `kernel/memory/mmap.c`, `pmm.c`, `reserve.c`
- `kernel/memory/pte.h` (header only), `vmm_core.c`, `heap.c`
- `drivers/keyboard/scancode.c`
- `kernel/input/keyqueue.c`
- `drivers/pit/pit_divisor.h` (header only)
- `kernel/console/textbuf.c`, `lineedit.c`
- `kernel/shell/parser.c`, `shell.c`, `cpuinfo.c`
- `kernel/bootmod.c`, `kernel/stackpeak.c`
- `kernel/proc/sched_core.c` (0.9)
- `kernel/console/history.c`, `kernel/rtc_time.c`, `kernel/logbuf.c` (0.9.1)
- `kernel/console/scrollback.c` (0.9.2)
- 0.10: `kernel/memory/uvm_core.c`, `usercopy_core.c`; `kernel/proc/syscall_core.c`, `proc_core.c`, `userimg.c`; header only: `kernel/arch/x86/gdt_desc.h`, the SMEP/SMAP decision in `cpuid.h`, `kernel/proc/userentry.h`, `faultop.h`, the SMEP/SMAP cause in `pagefault.h`
- 0.11: `kernel/proc/elf_core.c`, `progmod_core.c`, `loadplan.c`; `kernel/console/outq.c`; the policy (b) decision and `sched_wake` in `sched_core.c`
- `kernel/arch/x86/pagefault.h` and the inline NX decision in `cpuid.h` (header only)
- the formatter in `kernel/kprintf.c` (`test_kprintf` includes the file, with the two output drivers stubbed)

**Adding a shell command.** Add an entry to the `commands[]` table in `kernel/shell/shell.c`: name, usage, one-line help, maximum number of arguments, and a function. A command must:

- read everything it prints from `struct shell_context`, adding a field there (filled in by `console.c`) if it needs new data;
- write only through the `shell_output` sink;
- stay pure, so its exact output can be unit-tested in `tests/unit/test_shell.c`.

Commands run in normal context, one at a time, and must return promptly.

Since 0.9.1, new commands go into the `extra_commands[]` table, which exists only when the context has `extras` (the console always sets it; the older unit tests don't, so their output stays pinned). Every command then gets `help <command>` and `<command> --help` automatically from its usage and help text, so keep those short: `help` lists the name padded to 11 columns and the help text, and every line must fit 79 columns (test it in `tests/unit/test_shell_091.c`'s width test). A destructive command asks first, as `reboot` does (the shell returns a result telling the console to ask). Adding, changing or removing a command also updates [commands.md](commands.md) and its MOCS version in the same milestone (see [AGENTS.md](../AGENTS.md)).

**Scrollback (0.9.2):** every character the console writes to its grid must go through `write_screen()` or `grid_putc()` in `console.c`, so that a line scrolling off the top is kept. Writing to the grid directly with `textbuf_putc()` would lose lines from the scrollback. `textbuf_put_at()` (no scrolling) is fine for the input line.

**Screen output after the console starts:** only the console writes to the screen. Use `klogf()` for serial-only diagnostics, and don't use `kprintf()` for screen output once the console is active (the crash screen is the exception). Since 0.9 only thread 0 may write to the screen at all; see *Kernel threads*.

To add one:

- Add it to `UNIT_MODULES` in the Makefile.
- Write `tests/unit/test_<name>.c` using `tests/unit/unit.h` (`CHECK`, `CHECK_EQ`, `RUN`). `make unit` finds new test files automatically.

Unit tests are built as ordinary 64-bit Linux programs (`gcc -m64 -no-pie`), so integer and pointer sizes match the kernel. They are linked at 256 MiB (`-Wl,-Ttext-segment=0x10000000`): the memory tests pass the addresses of their static test data as 32-bit physical addresses, so that data must lie below 4 GiB, and `test_reserve` also expects it above its fake 32 MiB machine (a known limitation, to be removed later).

**Linker symbols.** `linker/linker.ld` exports:

- `__kernel_start` / `__kernel_end` (page-aligned; `.bss` includes the boot page tables and the stacks);
- `__text_`, `__rodata_`, `__data_` and `__bss_` `start`/`end`;
- since 0.8, the physical addresses the Multiboot header and the boot stub need: `__kernel_phys_start`, `__data_end_phys`, `__kernel_end_phys`, `__bss_start_phys`.

All the other symbols are **virtual (higher-half) addresses**. Where C needs a physical address (for example to reserve the kernel's pages), convert with `kernel_phys()` from `vm_layout.h`. Only `.boot` (the Multiboot header and the 32-bit stub) is linked at its load address.

`boot/boot.asm` exports `stack_bottom` and `stack_top` (the 16 KiB kernel stack), `kstack_guard` (the unmapped page below it), `ist1_stack_bottom` and `ist1_stack_top` (the 8 KiB double-fault stack) and `ist1_guard`. In C, declare them as `extern char __kernel_end[];` and use only their addresses.

**Using memory.** The API is in `kernel/memory/memory.h`:

- `memory_alloc_page()` returns a physical page address, or **0 when memory is exhausted**, which the caller must handle.
- `memory_free_page(addr)` returns the page. It panics on an unaligned address, a reserved page or a double free, because those are bugs.

Pages are physical addresses. Since 0.6 paging is on and free RAM is **not mapped**, and since 0.8 **nothing in the lower half is mapped**: a physical address is never a usable pointer, and a page from `memory_alloc_page()` can't be used through its address:

- to read the low 1 MiB (VGA, BIOS area) or the bootloader's data (Multiboot info, memory map, command line, modules): `phys_view(phys)` from `vm_layout.h`. The physical view covers only those; it never maps free RAM, page 0 or the kernel image;

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
- `thread_sleep(n)` (0.9) blocks the current thread for n ticks and lets others run; thread 0 waits by polling instead.
- Device polling loops must always have a fixed bound (see `ps2kbd.c`).
- **Don't busy-poll `timer_ticks()` without real work between the reads** (0.9). QEMU's TCG (the emulator used by the tests) delivers interrupts only between translated blocks of guest code, so a tight loop that only reads the tick counter (with its brief `cli`/`sti`) can starve IRQ0 and hang the test. A test thread that spins until a tick must do some real work in each pass (as the `schedtest.c` spinners do).

**Kernel threads (0.9).** The API is in `kernel/proc/thread.h`: `thread_create(name, fn, arg, detached)` returns the tid (or −1 when no thread slot, stack slot or memory is free); `fn`'s return value is the exit code. A joinable thread must be joined (`thread_join`), which frees it; a detached thread is freed by thread 0. Rules:

- Shared kernel state without its own locking is changed only with preemption off: `preempt_disable()` … `preempt_enable()` (`kernel/proc/preempt.h`). The heap, the frame allocator, paging, the window and the output functions already do this inside; new shared state needs the same.
- Never yield, sleep, join or exit with preemption off: each panics at once. Keep guarded sections short. While the count is above zero no switch happens, and the deferred switch comes at the next interrupt after it is back at zero.
- Only thread 0 writes to the screen (`kputc` panics otherwise). Other threads log with `klogf()`.
- The boot screen (`bootscreen_*`) is used only by thread 0, through `BOOTSCREEN_SAFE(call)` in `kernel.c`. Direct calls are allowed only in the boot sequence (`kernel_main`, `start_devices`, `check_boot_modules`) and in `devtest.c`. `scripts/check-bootscreen.sh` runs in every `make iso` and fails the build on any other call; QEMU tests check that it passes on the sources and rejects both kinds of violation.
- A thread's stack is 16 KiB with an unmapped 16 KiB guard below it. An overflow ends on the crash screen, naming the thread. Each exiting thread logs its stack peak.
- Scheduler state is changed only with interrupts off (`irq_save`/`irq_restore`), because the timer tick changes it from IRQ context.

**User mode (0.10).** The design is in *User processes* in [architecture.md](architecture.md). Rules:

- **PTE_USER in the lower half only.** User pages are mapped only through `uspace_map_page` (`uvm_core`), into `[VM_USER_MIN, VM_USER_TOP)`. Nothing in the upper half ever has the user bit: `vmm_core` (and so `paging_map`) refuses it, and `test=uaspace` walks 500 PML4s to prove it.
- **Fill a frame before mapping it** into a process (`uspace_map_page` does it: copy or zero through the window). A freed frame keeps its old contents.
- **W^X:** request `PTE_WRITE` or executable, never both (`uvm_core` refuses a writable executable page when NX is in use). Data, bss and stacks get `PTE_WRITE | paging_nx_flag()`.
- **Every pointer a program passes goes through `copy_from_user`** (or `user_readable` first, when the whole buffer must be refused before any of it is used). Never dereference a user address in the kernel: with SMAP that faults, without it a bad pointer would crash the kernel. A system call that takes a buffer copies it into kernel memory first and works on the copy.
- **Check, then copy (binding, see [AGENTS.md](../AGENTS.md)):** the check and the copy are separate steps, which is safe only while nothing can change a process's mapping in between. Any milestone that adds memory-unmapping system calls or threads inside a process must make the user-pointer check and the copy atomic (or copy through the physical-access window), with a test that changes the mapping between check and copy.
- **No FPU in programs:** CR0.EM is set and no x87/MMX/SSE state is saved between threads, so programs are built with `-mgeneral-regs-only` like the kernel. `scripts/user-images.sh` fails the build if any program other than `fpu` uses an x87/MMX/SSE register (and if `fpu` doesn't).
- **The freeze:** after boot, no new kernel-half PML4 slot may be created (every process holds a copy of slots 256–511): `paging_map` into an empty kernel-half slot panics. A new kernel area must use a slot that exists at the freeze (add it to the boot set-up and to `paging_freeze_kernel_half`'s list, with a walk-check line), never one created later.
- **A process's address space is changed only by its own thread**, on its own CR3. Another thread asks (`process_kill`); the process acts at its next return to ring 3.
- **Kernel faults stay fatal**, also while a process's thread runs kernel code (a system call): a bug in a system call must end on the crash screen, never be "contained".
- A program's output (`write`) goes to the serial log and `dmesg`, never the screen, and must never contain anything secret.

**The built-in programs (`user/`).** `make` builds each `user/prog/<name>.c` with `USER_CFLAGS` (freestanding, no C library, `-mgeneral-regs-only`, `-Werror`), links it with `user/crt0.asm` by `user/link.ld` (`.text` at `0x400000`, `.data`/`.bss` at `0x500000`), then runs `scripts/user-images.sh`, which checks every ELF (only `.text`, `.data` and `.bss` loaded, at those addresses; `_start` inside `.text`; no x87/MMX/SSE registers except in `fpu`) and writes `build/user/images.asm`: the raw code and data blobs and a sorted `struct userimg` table (name, code, size, data, size, data + bss size, entry). The kernel links `build/user/images.o`; `make repro` copies `user/` and `scripts/user-images.sh` too, and the images are byte-identical across builds. The kernel checks every entry with `userimg_check` before use (`test_userimg`), and `process_start` finds an image by name.

**Adding a test program.** Write `user/prog/<name>.c` with `int main(long arg)`, using `user/syscall.h` and `user/ulib.h` only (no C library); names are `[a-z0-9_]`, at most 15 characters. Keep code and data under 64 KiB each. The Makefile and the image table pick it up automatically. Start it from a diagnostic in `kernel/proc/usertest.c` with `process_start("<name>", arg)` and `process_wait`, and check its exact result (exit status, or the fault's vector, address and instruction bytes). A program that deliberately faults must have its expected opcode checked (see opcode evidence in [testing.md](testing.md)).

**Adding a program module (0.11).** Every `user/prog/<name>.c` is also built as `build/user/<name>.elf`. To deliver one with the system, add two lines to the default entry in `iso/boot/limine/limine.conf` (since 0.13; before, a `module` line in `grub.cfg`): `module_path: boot():/boot/prog/<name>.elf` and `module_string: <name>` (Limine passes only the string to the kernel, so it must name the program): `make iso` copies exactly the programs named there (one list only), and `run <name>` can then start it. Keep within the loader's rules (static `ET_EXEC` x86-64, 1–4 loadable segments in `[0x400000, 0x40000000)`, page-aligned, never writable and executable, at most 1 MiB of file and of memory; the full list is in [architecture.md](architecture.md)); `make elfcheck` builds `tests/elfcheck.c`, which runs the kernel's own checks on the host. At most 8 program modules are kept, and with 14 or more boot modules of any kind the boot stops (the protected reservation table is full). If you change the default entry's program list, update the `[0.11]` ISO checks in `tests/qemu-tests.sh` (they compare it with the expected 7). Programs only for tests (like `tests/user/flood.c`) are built outside `build/user`, so they never join the ISO or the image table.

**Console output rule (0.11).** Only thread 0 writes to the screen. A program's output reaches it through the output queue (`outq`), which thread 0 drains in `console_poll`; nothing else may draw program text, and nothing may wait in thread 0 for a program. The serial log and `dmesg` keep exactly one `[pid N] …` record of each line: don't add a second serial copy of what the queue shows. Background programs never write to the screen.

**Adding a system call.**

1. Take the number and meaning from the documented Linux x86-64 interface (or, for a Madire-only call, a number in `0x10000000`–`0x1000FFFF`); add it to `kernel/proc/syscall_core.h` (number, `enum syscall_id`) and `syscall_lookup`, and to `user/syscall.h`.
2. Put every argument rule in the pure core (`syscall_core.c`) and unit-test it in `tests/unit/test_syscall.c`: valid and invalid values, limits, the full 64-bit compare.
3. Implement the kernel side in `kernel/proc/syscall.c`: arguments arrive in RDI, RSI, RDX, R10, R8, R9; return a value or `-errno`. Copy every pointer argument with `copy_from_user` before use (and follow the check-then-copy rule). A call that blocks runs with interrupts on, as any kernel code of the thread does.
4. Test it from a program and a diagnostic (QEMU checks), including every bad pointer and argument; update the table in [architecture.md](architecture.md).

## Toolchain

| Tool | Package (Ubuntu) | Used for |
|---|---|---|
| `gcc -m64` | `build-essential` | Compiling C into 64-bit freestanding objects |
| `ld -m elf_x86_64` | `binutils` | Linking with `linker/linker.ld` into `build/madire.elf` |
| `objcopy`, `objdump` | `binutils` | `objcopy -O binary` makes the bootable flat image `build/madire.kernel`; `objdump -d` checks that no SSE/x87 register is used |
| `nasm -f elf64` | `nasm` | Assembling `.asm` files (the boot stub mixes 32- and 64-bit code) |
| `limine` (host tool) and Limine's BIOS files | the pinned Limine v12.9.3 binary package, fetched and checked by `scripts/install-deps.sh` (SHA-256), not an Ubuntu package | `limine bios-install` and the boot menu (since 0.13; GRUB's `grub-mkrescue` before) |
| `xorriso`, `mtools` | `xorriso`, `mtools` | `xorriso -as mkisofs` builds the ISO (since 0.13 called directly) |
| `qemu-system-x86_64`, `qemu-system-i386` | `qemu-system-x86` | Headless boot and crash tests; `qemu-system-i386` for the "no 64-bit CPU" test |
| `python3` | `python3` | `scripts/logo-preview.py`, `tests/memory_check.py`, `tests/qmp_keys.py` (standard library only) |
| host `gcc -m64` | `build-essential` | host unit tests (`make unit`) |

**Compiler note:** the host GCC works because the kernel is built `-ffreestanding -nostdlib`, so nothing from the Linux C library ends up in it. A dedicated `x86_64-elf-gcc` cross-compiler would rule out host assumptions completely; it isn't needed so far. Two build checks guard against the host compiler's defaults: the link must leave no undefined symbols (no `libgcc` or libc helpers), and the build fails if `objdump` finds any SSE/x87 register in the kernel.

**Boot image:** Limine (since 0.13; GRUB before) and QEMU load `build/madire.kernel`, a flat binary, through the Multiboot 1 "a.out kludge" address fields in `boot/boot.asm` (load address, end of file, end of `.bss`, entry). Those fields are physical addresses (the `*_phys` linker symbols), so the higher-half link in 0.8 didn't change how the image is loaded. `build/madire.elf` has the same code with symbols, for `objdump`/`nm` and debugging.

**Reproducible build (0.8):** `make repro` copies the sources into two fresh directories and builds each from clean; the second build runs with a `find` that lists files in reverse order, so a link order that followed the file system would show up as a different binary. `madire.elf` and `madire.kernel` must be byte-identical in both, and identical to the kernel the QEMU tests boot. `make test` runs it, and `tests/qemu-tests.sh` turns the result into 3 `[0.8]` checks. The ISO isn't covered: `grub-mkrescue` records timestamps. The kernel was also byte-identical across two Ubuntu builds of GCC 13.3.0 (the old and the new Codespace).

## Build environments

1. **GitHub Codespace (current):** `.devcontainer/devcontainer.json` (Ubuntu 24.04, plus the `sshd` feature that `gh codespace ssh`/`cp` need) runs `scripts/install-deps.sh` when the Codespace is created. **Proven in 0.8** on a fresh Codespace (`gh codespace create -R mamishimadire/madire-os -b wip/0.8 -m basicLinux32gb`): set-up took about 83 s including the automatic `install-deps.sh`; every tool was present (GCC 13.3.0, NASM 2.16.01, GRUB 2.12, QEMU 8.2.2 with `qemu-system-i386`, Python 3.12.3, binutils 2.42); `gh codespace ssh` and `cp` worked; `make test` passed in its own checkout and `make repro` gave the same kernel as the older Codespace. No change to `.devcontainer/` was needed, and that Codespace is now the one in use. `scripts/build.ps1` copies the source to `/tmp/madire-os-build` in the Codespace, runs `make iso test` there, and downloads the ISO. It needs `gh auth refresh -h github.com -s codespace` once. A Codespace that restarts loses `/tmp`, so the next build uploads everything again.
2. **WSL2 Ubuntu:** `wsl --install -d Ubuntu` (admin, reboot), then `bash scripts/install-deps.sh` and `make`.
3. **Any Ubuntu machine or CI:** same as WSL.

## Running the full test suite (detached, on a hash-verified snapshot)

Since 0.9 the full suite (`make unit test iso`; in 0.10: 41 unit suites, `make repro`, 838 QEMU checks) takes about **15 minutes** (about **30** since 0.10, about **50** in 0.11). That is longer than the time limit of many terminals and coding tools, and a run cut off by such a limit can look like a hang or a pass. So the default procedure, for people and agents alike, is to run it **detached** inside the Codespace and to prove which sources it tested:

1. **Snapshot:** copy the files under test (`Makefile`, `boot/`, `kernel/`, `drivers/`, `linker/`, `iso/`, `scripts/`, `tests/`, and since 0.10 `user/`) to a snapshot folder outside the repository, and record the SHA-256 of every file.
2. **Upload** the snapshot to the Codespace, to a fresh `/tmp/madire-os-build` (`gh codespace cp -e -r …`, as `scripts/build.ps1` does).
3. **Verify:** list the SHA-256 of every file in the Codespace copy (`find . -type f | sort | xargs sha256sum`) and compare it with the snapshot's list. Any difference, missing or extra file means the run wouldn't test the snapshot: upload again.
4. **Start detached**, so the run doesn't depend on the session that started it:

   ```sh
   cd /tmp/madire-os-build && (nohup bash -c 'make unit test iso > /tmp/regression.txt 2>&1; echo "MAKE EXIT $?" >> /tmp/regression.txt' >/dev/null 2>&1 &)
   ```

5. **Poll** the log until its last line is `MAKE EXIT <code>` (a few short checks, not one long blocking command), then download it. The run passed only if the code is 0, the summary says `ALL TESTS PASSED (838 checks: …)` (0.10; 528 in 0.9) with the expected count per tag, and every unit suite says `PASS`. Since 0.10 it takes about 30 minutes.
6. Before trusting the result, confirm that the working tree still equals the snapshot (same hashes). A file changed since the snapshot means the run didn't test it.

From PowerShell, put the remote command in single quotes (or a script file): inside double quotes PowerShell expands `$?` locally before the command reaches the Codespace.

`scripts/build.ps1` still runs `make iso test` in the foreground. That's fine when nothing can cut it off, but its result is not the evidence for a stage or a milestone.

### GitHub Actions (since 0.12: the official regression environment)

Since 0.12 the official regression runs on GitHub-hosted runners (`.github/workflows/regression.yml` on `main`; owner's decision after stage 0, when the Codespace and Actions gave the same kernel and the same 1071 results on one commit). The Codespace (`scripts/remote-regress.ps1`, below) is the fallback: for environmental failures on Actions, or on the owner's request. From PowerShell, in the repository:

```powershell
.\scripts\actions-regress.ps1 -Dispatch              # sharded (default): about 20 minutes
.\scripts\actions-regress.ps1 -Dispatch -Mode full   # one job, as before: about 48 minutes
.\scripts\actions-regress.ps1 -Dispatch -Parts stress  # only the kill-path stress test (0.12 stage 2b)
.\scripts\actions-regress.ps1 -Collect <run id>      # after the run has finished
```

- **Dispatch** refuses a working tree with changes and a commit that isn't on GitHub, then starts the workflow with the commit as a full 40-character SHA (`gh workflow run regression.yml --ref main -f ref=<sha> -f mode=<mode>`), prints the run id and returns. It doesn't wait: collect the run when it has finished (or when asked for its status).
- **Collect** downloads the run's artifact into `build\regression\<run id>\results\` (git-ignored), checks the commit and prints the summary; the per-boot logs of the last 3 collected runs are kept. **Every Actions run used as evidence is collected.**
- **The workflow:** started by hand only (`workflow_dispatch`; no push, pull-request or scheduled trigger), `permissions: contents: read`, no secrets, `persist-credentials: false`, QEMU's software emulation only (TCG; KVM is never enabled), every action pinned to a full commit SHA with its version in a comment, the result artifact kept 7 days (the per-job artifacts of a sharded run, 1 day). Free plan: 2,000 minutes a month; a sharded run uses about 70 job minutes, a full run about 48. Changing the workflow is a change to `main` and needs the owner's approval.
- **Sharded mode** (the default): five jobs in parallel, one per part of the QEMU tests (`make test-shard SHARD=<part>`; the parts are listed by `bash tests/qemu-tests.sh --list-parts`): `static` (also `make unit`, `make repro` and the symbol, libgcc/libc, SSE/x87 and 2 MiB margin checks) and `1` to `4`. Every job checks out the same commit, builds the kernel itself and records its hashes and the SHA-256 manifest of the files under test. The job `combine` then runs `tests/combine-shards.sh`: the same commit, the same files and the same `madire.elf` and `madire.kernel` in every job (whether the ISO is identical too is reported, not required); every part run exactly once, in its own job, to its end; the part `final` over every job's serial logs; the unit suites and `make repro` run once; the counts against `tests/expected-checks.txt`; the helper replay under mawk and gawk. Its `regression.log` holds every job's log in order and the combined summary, with the same `ALL TESTS PASSED (… checks: …)` line as a full run.
- **Full mode:** `make unit test iso` in one job, as on the Codespace, plus the helper replay. Used to compare with a sharded run, and for commits from before the split.
- **Stress (0.12 stage 2b, on request only):** the workflow input `parts` is `all` by default (the regression above, unchanged) or `stress`: then a single job runs `make test-shard SHARD=stress` (`tests/qemu-part-stress.sh`: `test=killrace` boots and console kill sessions) and no other job runs. The part is not in `--list-parts`, so no full or sharded run includes it; its checks are tagged `[stress]` and kept out of `tests/expected-checks.txt` (the run prints `STRESS_COUNTS`). About 20 job minutes.

The split is proven before it is used (stage 0c): a one-off script checks that every line of the single script was moved, none deleted or changed; and one full and one sharded run on the same commit must give identical counts per tag, identical sorted lists of passed checks and the same kernel in every build.

### `scripts/remote-regress.ps1` (since 0.11; since 0.12 the fallback)

Since 0.11 the procedure above is automated, and the full suite takes **about 50 minutes** (0.11: 1071 QEMU checks, 51 unit suites). From PowerShell, in the repository:

```powershell
.\scripts\remote-regress.ps1          # Codespace target (the fallback since 0.12)
```

It requires the Codespace to be **Shutdown** and 2-core, then: hashes every file under test (`Makefile`, `boot/`, `drivers/`, `iso/`, `kernel/`, `linker/`, `scripts/`, `tests/`, `user/`) into a manifest; packs the snapshot and checks the archive against the manifest; **starts the Codespace**; checks that no earlier regression is still running; uploads the snapshot into a fresh `/workspaces/madire-regression-<run id>`; **proves its own hash guard** (a deliberately altered copy must be rejected before anything runs, and the correct copy must pass); starts `make unit test iso` detached; polls every 60 s until `MAKE EXIT`; runs the checks (the summary, every unit suite, no repro notices about a missing boot configuration (`limine.conf` since 0.13, `grub.cfg` before), no undefined symbols, no libgcc/libc helpers, 0 SSE/x87 uses, the kernel end and the 2 MiB margin, failing below 32 KiB); confirms the remote snapshot's hashes still match; downloads `regression.log` and the stage 5 screen evidence; and **stops the Codespace** (also after a failure), printing the session's length. The last line `RESULTS=<folder>` names the local copy (`build\regression\<run id>\results\`). Run it in the background and read its output when it ends; a pass prints `REMOTE_REGRESSION_RESULT=PASS` and `CODESPACE_SHUTDOWN=CONFIRMED`. It runs the full mode (`make unit test iso`, every part in order), which also checks the counts against `tests/expected-checks.txt`.

**The local Kali target (`-Target Kali`; optional, not used).** GitHub Actions is the official test environment (the Codespace until 0.12). The first full run on Kali (after 0.11) did not pass: VirtualBox runs every VM on this laptop through the Windows Hypervisor Platform (NEM, because Windows VBS is running; see [testing.md](testing.md)), and under that backend the Kali VM's CPUs were starved during the long run (boots too slow for the fixed time limits, RCU stalls in Kali). **Correction (0.12):** part of those failures were not the VM's slowness but a harness bug: Kali's `awk` is gawk, which processes escape sequences in `awk -v` values (`\(` became `(`, so some patterns were wrong or invalid), while the Codespace's mawk keeps them; the same failures appeared on GitHub Actions (also gawk). Since 0.12 the helpers read their patterns from the environment (see [testing.md](testing.md)). The time-outs and RCU stalls remain the NEM starvation. **Kali is for builds and unit tests only** (AGENTS.md, since 0.12): its QEMU timing, stress and kernel-margin results are never evidence; only GitHub Actions results (QEMU 8.2.2, gcc 13) count. Kali therefore stays powered off; the target stays in the script, documented here, and no step requires it. What it does: the same run on the local Kali Linux VM in VirtualBox (`kali-linux-2026.2-virtualbox-amd64`: 4 CPUs, 6 GB, NAT with SSH forwarded to `127.0.0.1:2222`), so builds and tests cost no Codespace time:

```powershell
.\scripts\remote-regress.ps1 -Target Kali
```

- **Login by key only:** the dedicated key `%USERPROFILE%\.ssh\madire_kali_ed25519` (no passphrase; in Kali's `authorized_keys` restricted with `from="10.0.2.2"`, the NAT gateway, so it works only through the port forward) and Kali's host key pinned in `%USERPROFILE%\.ssh\known_hosts_madire_kali` (`StrictHostKeyChecking=yes`, `BatchMode`, password login refused by the client). No password, token or GitHub credential is in any script, file or the VM: the snapshot is uploaded with `scp`, never cloned.
- **The VM:** the script refuses to run while `MadireOS-Test` is running; it starts Kali headless (unless it is already running, in which case it is left running afterwards), waits for SSH, runs in `/home/kali/madire-regression-<run id>`, and afterwards shuts Kali down with the ACPI power button (a hard power-off after 2 minutes if needed), also after a failure. The preflight logs every tool's version (gcc, ld, nasm, grub-mkrescue, QEMU, Python, the OS) and refuses with less than 2 GiB free.
- **Toolchain:** Kali is a rolling distribution, so its toolchain is held during a milestone (`apt-mark hold` on gcc, binutils, nasm, xorriso, qemu and python3; grub until 0.12) and upgraded only between milestones; every run logs the versions.
- **Kernel hashes depend on the toolchain:** each environment has its own byte-identical kernel (`make repro` within it). [PROGRESS.md](PROGRESS.md) records the reference environment, its tool versions and kernel hash per milestone.

Local results (both targets) go to `build\regression\<run id>\results\` (git-ignored), never to the system temp folder. Since 0.12 they include `boot-logs.tar.gz`: every per-boot QEMU serial log (`build/serial-*.log`), the key evidence (`*.keys.txt`, `*.irq1.txt`) and the stall captures; the tars of the last 3 runs are kept, older ones deleted. The tar also holds `helper-calls.trace` (every `expect_next`, `expect_in_order` and `max_width` call with its patterns and its log, saved in `helper-logs/` under its SHA-256 as it was at the call) and the output of `tests/helper-replay.sh`, which replays every call under mawk and gawk with the helpers before 0.12 (`awk -v`) and since (`ENVIRON`) and counts differing verdicts and awk errors; it decides no check. `scripts/compare-serial.py VBOX_DIR QEMU_DIR PREFIX` compares VirtualBox's serial logs (from `vbox-acceptance.ps1`) with the unpacked QEMU logs: result lines with numbers and addresses masked, listing what appears in only one of them. The checks script's summary (`UNDEFINED_SYMBOLS`, `SSE_X87_REGISTER_USES`, `KERNEL_END_*`, `TWO_MIB_MARGIN`) is now printed in the tool's output.

**Codespaces is a paid resource.** Start a Codespace for a task and stop it as soon as the results are collected (the tool does both); don't leave one idle, and keep extra sessions (for example to fetch an ISO) short. An idle Codespace also stops itself after a while, which once cut off a regression that nothing polled.

## Compiler flags

| Flag | Reason |
|---|---|
| `-m64` | 64-bit (x86-64) code |
| `-ffreestanding` | No hosted C library; don't assume `main`, and don't replace code with library builtins |
| `-mno-red-zone` | An interrupt pushes onto the stack just below RSP, so the 128-byte red zone would be overwritten |
| `-mgeneral-regs-only` | No SSE/MMX/x87/AVX registers (the FPU and SSE aren't set up; CR4.OSFXSR stays clear, so SSE would fault) |
| `-mcmodel=kernel` | The kernel is linked in the top 2 GiB of the address space (0.8: `0xFFFFFFFF80000000` + physical), where 32-bit sign-extended addresses reach every symbol. 0.7 used `-mcmodel=small` (linked at 1 MiB). Addresses outside the kernel image (heap, recursive mapping, physical view) are 64-bit constants in `vm_layout.h` |
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
- The kernel stack is 16 KiB with an unmapped guard page below it; a deeper stack ends on the crash screen (double fault). Keep large buffers off the stack (static, or `kmalloc`). `mem` shows the high-water mark (1480 bytes at 0.8, 1496 at 0.9, 1512 at 0.9.1, 1496 at 0.9.2, 1512 at 0.10 and 0.11); a change that raises it noticeably deserves a look. Each thread's stack is 16 KiB too; since 0.11 `write` keeps its 4 KiB copy buffer on the calling process thread's stack (peaks measured at most 5248 bytes).
- Never use a low (physical) address as a pointer: the lower half is empty (0.8). Use `phys_view()` for the low 1 MiB and boot data, the window for other frames, and `kernel_phys()` to turn a kernel symbol into a physical address.
- Never dereference a user (lower-half) address in the kernel (0.10): the lower half belongs to whichever process is running. Use `copy_from_user` (see *User mode*); with SMEP/SMAP on, executing or touching a user page directly ends on the crash screen with a `Cause:` line.
- Diagnostics that shouldn't appear on screen go to `klogf()` (serial only). Since 0.9.1 everything `klogf` and `kprintf` write is also kept in memory and shown by `dmesg`: never log secrets.
- **Key events are not logged by default** (since 0.9.2). Logging them is for tests and diagnostics only: the boot option `keylog` (a whole word on the kernel command line) or `test=keyboard`/`test=kbdflood`. A QEMU check that reads `key:` lines must boot with `keylog` (the boot-menu checks use the test-only keylog ISO the test script builds). Don't add another path that logs typed text.
- **Secret input (binding, see [AGENTS.md](../AGENTS.md)):** No milestone may add a password or other secret prompt without implementing secret-input mode in the same milestone: key events never logged (even with `keylog`), the line not echoed to serial or `dmesg`, not stored in history, and shown as `*` on screen.
- Never allocate memory before `memory_init()`, and never write to a physical page you didn't get from `memory_alloc_page()`.
- Kernel code and read-only data are read-only once paging is on (CR0.WP): writing to a `const` object, or to code, is a page fault.
- Address 0 is never mapped: a NULL dereference is a page fault, not a silent read of the BIOS data area. Since 0.8 the same is true for every address in 0–4 GiB.
- `kmalloc`, `kcalloc` and `kfree` only in normal context with interrupts enabled.
- The kernel owns VGA palette slots 1, 5, 6, 8, 9 and 13 for the logo. Don't use those colours for text.
- Hardware access goes through `kernel/arch/x86/io.h`, not inline `in`/`out` instructions.
- Screen and serial output only from normal context, never from an interrupt handler. Screen output only from thread 0 (0.9).
- Only IRQ0 (timer) and IRQ1 (keyboard) may be unmasked; see `IRQ_ALLOWED_MASK`.
- Shared state without locking is changed only with preemption disabled, and never yield, sleep, join or exit inside such a section (0.9; see *Kernel threads*).

## Conventions

- Every milestone must build, pass `make test`, boot in VirtualBox, and update these docs before it counts as done. The working rules for the process (stages, approvals, backups, commits) are in [AGENTS.md](../AGENTS.md).
- Architecture changes are explained in the docs before they are made.
- Kernel code prefixes output helpers with `k` (`kputs`) so they aren't confused with C library functions.
- Baseline ISOs of each finished milestone are kept outside the repo in `Desktop\madire-os-baseline\`, for regression comparison.
- CPU-specific code stays under `kernel/arch/` (other CPU families, likely ARM64 for phones, may come later).
- External projects are references only; see [references.md](references.md) for the rules.
- Commit and tag messages are plain UTF-8 **without a byte order mark** (PowerShell's default output adds one; write message files with `UTF8Encoding($false)` or use `git commit -m`).
