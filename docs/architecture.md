# Madire OS Architecture

Status key: **[built]** exists in the current version, **[planned]** is designed but not written yet.

## Boot process [built]

```
VirtualBox power-on
  -> BIOS firmware        finds the ISO in the optical drive (El Torito boot)
  -> GRUB 2               reads iso/boot/grub/grub.cfg, shows menu (3 s)
  -> Multiboot load       GRUB loads build/madire.kernel (a flat binary, through
                          the a.out kludge fields) at 1 MiB, in 32-bit protected mode
  -> boot/boot.asm        _start (32-bit stub, 0.7; runs at its load address):
                            zero .bss; serial marker "boot32: start"
                            CPUID present? long mode? PAE? — if not: message on
                              serial and screen, halt (no mode switch, no reset)
                            temporary tables, 2 MiB pages (0.8: three views):
                              0-4 GiB identity (page 0 included), the same
                              0-4 GiB in the physical view, and the kernel's
                              first 1 GiB in the higher half
                            CR4.PAE, EFER.LME, CR0.PG|WP → long mode; boot GDT;
                            far jump to 64-bit code; jump to the kernel's
                            higher-half address (0.8); "boot64" markers;
                            16 KiB kernel stack; call kernel_main(magic, info)
  -> kernel/kernel.c      kernel_main (64-bit, in the higher half):
       0. stack paint (0.8)        fill the unused kernel stack with 0xA5 (for
                                   the high-water mark); interrupts still off
       1. serial_init, vga_init    drivers ready; cursor off, blink off; VGA
                                   and boot data read through the physical view
       2. check Multiboot magic    and info pointer (wrong → crash screen)
       3. boot screen header       logo (custom palette), title, version
       4. gdt_init                 64-bit GDT: code 0x08, data 0x10, TSS 0x18
                                   (IST1 = double-fault stack); ltr; read back
       5. idt_init                 256-entry IDT (16-byte gates), vector 8 on
                                   IST1; read back; CPU mode logged (EFER,
                                   CR0, CR4, CS L-bit, TR, 64-bit arithmetic)
       6. pic_init                 PIC moved to vectors 32-47, all IRQs masked
       7. sti + int3 self-test     exception path works and returns
                                   (sti is safe: every IRQ is still masked)
       8. memory_init              validate memory map → reservations →
                                   kernel-in-RAM check → page allocator
       9. memtest_boot             allocate / write / read / free self-test
      10. status + memory panel    "Memory initialization: OK" only now
      10a. paging_init             NX detected and enabled (EFER.NXE); build
                                   the 4-level tables (still on the boot
                                   tables): kernel in the higher half,
                                   physical view, nothing in the lower half
                                   → walk-check them → load CR3
                                   (check fails → the boot tables stay, FAILED)
           paging_selftest         map/read back, TLB flush, window, no leak
                                   (fails once the tables are loaded → crash screen)
      10b. kheap_init + selftest   first 64 KiB of the heap; mixed allocations,
                                   growth, no leak (fails → kmalloc returns NULL)
           status                  "Paging & kernel heap: OK" (one screen line,
                                   two serial lines)
      10b'. thread_init (0.9)      kernel_main becomes thread 0 ("kernel");
                                   other threads can be created from now on
                                   (their stacks need paging)
      10c. check_boot_modules      a test module must be intact (serial log;
                                   since 0.8 also the screen's message line)
      11. start_devices:
            timer_init             register IRQ0 handler → program PIT →
                                   unmask IRQ0 → verify mask
            timer_selftest         PIT counting? IRQ0 delivering ticks?
            keyboard_init          probe/configure i8042 → echo → register
                                   IRQ1 handler → clear pending → unmask IRQ1
            keyboard_selftest      echo reply through IRQ1, decoder, queue
            PIC mask check         must be exactly 0xFFFC (only IRQ0/IRQ1)
      12. console_selftest         pure console/shell code (not hardware)
      12a. schedtest_boot (0.9)    scheduler self-test, serial only: two threads
                                   ping-pong through yield, preemption of a
                                   thread that never yields, sleep(10),
                                   everything returned (fails → crash screen;
                                   paging off → NOT APPLICABLE)
      13. diagnostics if requested (test=… on the boot command line)
      14. "System ready. Press any key to open the console."; the stack's
          high-water mark so far goes to serial (0.8)
      15. idle loop                handle queued keys, uptime, counters;
                                   first key press → console; then keys → shell;
                                   then thread_idle (0.9): free exited
                                   detached threads, yield if another thread
                                   is ready, else hlt
  any CPU exception → isr.asm stub → interrupt_dispatch → panic → crash screen
  IRQ0/IRQ1 → isr.asm stub → interrupt_dispatch → irq_dispatch → driver handler → EOI
            → (0.9) thread_tick (IRQ0 only) → thread_preempt_point (may switch threads)
```

**Why memory comes after the CPU set-up (steps 4–7):** if reading bootloader data hits a bad address, the IDT turns the fault into the crash screen rather than a silent reset. Nothing allocates memory before step 8, so the bootloader's structures can't be overwritten before they're reserved.

**Why paging comes here (step 10a):** after the IDT, so a mistake shows the crash screen instead of a reset; after the boot memory self-test, which reaches pages directly through the stub's temporary identity tables; and before any IRQ is unmasked, so no interrupt arrives while the address space is being switched. The heap comes right after, because it needs mappings.

**The temporary boot tables** map, with 2 MiB pages: 0–4 GiB identity (**including page 0**) in PML4[0]; the same 0–4 GiB in the physical view (PML4[256]); and the kernel's first 1 GiB at `0xFFFFFFFF80000000` (PML4[511]). They are only a bridge: long mode can't run with paging off, so the stub needs *some* tables before it can call C. The identity part lets the 32-bit stub keep running after paging is switched on, and lets memory set-up and the boot self-test use physical addresses directly. `paging_init` replaces them with the kernel's own 4-level tables, which map **nothing in the lower half** (0.8; page 0 was already unmapped since 0.6); the walk-check, the self-test and QEMU tests prove it.

**How the kernel gets to the higher half (0.8):** everything except `.boot` (the Multiboot header and the 32-bit stub) is linked at `0xFFFFFFFF80000000` + physical, but loaded at 1 MiB. The stub uses physical addresses (`symbol - 0xFFFFFFFF80000000`) for everything it touches outside `.boot`. After the far jump into 64-bit mode it is still running at its load address; it then jumps (through a 64-bit absolute address) to `long_entry` in `.text`, at its high address, which loads the data segments and the stack, logs `boot64: running at the higher half, RIP=0xffffffff80…`, and calls `kernel_main`. From the first C instruction on, the kernel runs high.

**Why devices come after memory (step 11):** memory set-up and its self-test behave exactly as in 0.3 (IRQs masked). Each IRQ line is unmasked only once its handler is registered and its device configured. The timer comes first, so the keyboard self-test can use it for bounded waits.

| Stage | File | Responsibility |
|---|---|---|
| Bootloader config | `iso/boot/grub/grub.cfg` | Normal entry plus a Diagnostics submenu (crash, page-fault, memory, timer, keyboard and, since 0.9, thread tests) |
| Multiboot header | `boot/boot.asm` (`.multiboot`) | Magic `0x1BADB002`, flags (page-aligned modules, memory map, a.out kludge), checksum, then the physical load addresses: `load_addr = __kernel_phys_start`, `load_end_addr = __data_end_phys`, `bss_end_addr = __kernel_end_phys`, `entry_addr = _start`; must be within the first 8 KiB of the file |
| Entry point | `boot/boot.asm` (`_start`, `.boot32`) | 32-bit stub: CPU checks, temporary tables, switch to long mode, jump to the higher half; passes `eax` (magic) and `ebx` (info pointer) to C as the first two arguments |
| Memory layout | `linker/linker.ld` | 64-bit ELF, loaded at 1 MiB; since 0.8 linked and run at `0xFFFFFFFF80000000` + physical (`.boot` alone is linked at its load address); `.text`, `.rodata`, `.data`, `.bss` 4 KiB-aligned; `.bss` ends with the boot tables and the stacks; exports `__kernel_start/end` and per-section start/end symbols (virtual), plus the `*_phys` symbols the loader and the stub need |
| Start-up sequence | `kernel/kernel.c` | Calls each subsystem in order and reports each result on the boot screen |

**Why GRUB/Multiboot:** GRUB already handles BIOS disk access, reading the ISO filesystem and switching the CPU into protected mode. That lets the project start with kernel code instead of a hand-written boot sector. A custom bootloader could replace it later without changing the kernel's entry contract.

**Why a 32-bit stub:** Multiboot (version 1) hands over in 32-bit protected mode, so the first instructions must be 32-bit. The stub does only what long mode needs: check the CPU, build temporary tables, switch. Everything else is 64-bit C. Up to 0.6 the whole kernel was 32-bit; 0.7 made it 64-bit and dropped the 32-bit build.

**Why Multiboot 1 and a flat binary:** most QEMU tests boot with `-kernel … -append … -initrd`, and QEMU's `-kernel` loader supports Multiboot 1 (not Multiboot 2) and refuses 64-bit ELF files. The a.out kludge fields give the physical load addresses and the end of `.bss`, so GRUB and QEMU both load `objcopy`'s flat output directly, whatever the format. The ELF (`build/madire.elf`) is kept for symbols and debugging. The approach still works with the higher-half link (0.8): the kludge fields are physical addresses, and a stage-3 probe confirmed GRUB and QEMU `-kernel` both load the high-linked flat binary.

**CPUs that can't run it:** without CPUID, long mode or PAE, the stub prints `Madire OS needs a 64-bit (x86-64) CPU: … not supported. Halted.` on serial and in white on red on the screen's top row, and halts. No mode switch is attempted, so there is no triple fault.

## Source layout

```
boot/                 entry point: 32-bit stub, switch to long mode (assembly)
kernel/               architecture-independent kernel code
  arch/x86/           everything specific to the x86 CPU (incl. IRQ layer, CMOS)
  memory/             memory map, reservations, page allocator, paging,
                      kernel heap, self-tests and diagnostics
  input/              key-event queue
  timer.c             system tick, uptime, bounded waits
  devtest.c           timer / keyboard / IRQ diagnostics
  console/            console: text grid, line editor, screen + serial output
  shell/              command parser, shell commands, CPUID decoding
  proc/               kernel threads: scheduler core, thread layer, context
                      switch, preemption-disable count, self-test (0.9)
drivers/              hardware drivers
  vga/  serial/  pit/  keyboard/
```

`kernel/memory/` doesn't depend on the display or the logo. The boot screen only displays the `struct memory_stats` that `kernel.c` hands it.

Code under `kernel/` (outside `arch/`) and `drivers/` doesn't use x86 instructions directly. Port I/O goes through `kernel/arch/x86/io.h`. That file is where a Hardware Abstraction Layer (HAL) boundary will go.

## CPU [built: 0.2, 64-bit since 0.7]

### Long mode (0.7)

The kernel runs in 64-bit long mode. At start-up it logs the proof to serial: EFER (LME and LMA set, NXE once paging is set up), CR0 (PE, PG, WP), CR4 (PAE), the code segment's descriptor (L = 1, D = 0), the task register (`0x18`) and a 64-bit multiply and divide on values above 2^32 (checked against known results; no `libgcc`). `cpu` shows `64-bit long mode` whenever EFER.LMA is set, and `version` says `64-bit x86-64`.

### GDT and TSS (`kernel/arch/x86/gdt.c`, `gdt_flush.asm`)

The GDT is the table of memory segments. The stub's temporary GDT is replaced at once.

| Selector | Segment | Notes |
|---|---|---|
| `0x00` | null | – |
| `0x08` | kernel code | 64-bit (L = 1), ring 0 |
| `0x10` | kernel data | ring 0, read/write |
| `0x18` | TSS (16-byte descriptor) | IST1 = top of the double-fault stack |

In long mode segmentation does no translation; protection comes from paging. `gdt_flush` loads the table, reloads the data segment registers and reloads `CS` with a far return; `ltr` loads the TSS. `gdt_init` then reads back `sgdt`, `CS`, `DS`, `SS` and `TR` to confirm the CPU is using the new values. User-mode segments come with user mode.

### IDT and interrupts (`idt.c`, `isr.asm`, `interrupts.c`)

- **IDT:** 256 16-byte gates. Vectors 0–31 (CPU exceptions) and 32–47 (hardware IRQs) point at stubs; 48–255 are marked not-present, and reaching one raises a #GP, which is reported. **Vector 8 (double fault) runs on IST1**, its own stack, so it works even when the kernel stack is unusable.
- **Entry path:** each stub pushes a dummy error code where the CPU doesn't push one, then its vector number, then jumps to `isr_common`. `isr_common` saves the 15 general registers, aligns RSP to 16 bytes and calls `interrupt_dispatch(struct interrupt_frame *)`. On return it restores everything and executes `iretq`. The CPU always saves SS:RSP in long mode, so the frame holds the interrupted stack pointer.
- **Policy:**
  - Breakpoint (`int3`) is counted and execution resumes. The boot self-test relies on this.
  - Any other exception (vectors 0–31) is fatal and goes to the crash screen. This behaviour is unchanged since 0.2.
  - Vectors 32–47 (hardware IRQs) go to `irq_dispatch` (below). Since 0.9, after `irq_dispatch` returns (the EOI is already sent), IRQ0 charges the tick to the scheduler, and every IRQ ends at the preemption point, which may switch to another thread (see *Kernel threads and scheduler*). Exceptions never switch threads.
- All gates are interrupt gates, so interrupts are disabled while a handler runs.

### Interrupt controller (`pic.c`)

The two 8259 PICs are moved from vectors 8–15 (which collide with CPU exceptions, e.g. 8 = Double Fault) to 32–47. Every IRQ is then masked, and `pic_init` confirms both masks read back as `0xFF`. Since 0.4:

- `pic_is_spurious` and `pic_send_eoi` are separate calls, so the dispatcher can check for a spurious IRQ *before* the handler runs and send the EOI *after* it.
- `pic_unmask`, `pic_mask` and `pic_get_mask` control individual lines.
- Spurious IRQs 7 and 15 are detected through the in-service register. They get no EOI, except that a spurious IRQ 15 still gets the master's cascade EOI.

### IRQ layer (`irq.c`) [built: 0.4]

- **Registration:** `irq_register(irq, handler)` allows one handler per line. It's static and never removed. It refuses lines ≥ 16, a null handler, or a line that already has one.
- **Enabling:** `irq_enable_line(irq)` refuses a line with no handler, unmasks it, and checks the mask actually changed. So a line can never be unmasked before its handler is ready.
- **Dispatch:** `irq_dispatch(irq)` is the only EOI point:
  1. spurious? count it and return, with no EOI;
  2. count the IRQ;
  3. call the handler, or count the IRQ as *unexpected* if there is none;
  4. send exactly one EOI.
- **Counters:** per line, spurious, unexpected (plus the line of the last unexpected one). Normal context reads and reports them; interrupt context never logs.
- **Critical sections:** `irq_save`/`irq_restore` save EFLAGS and disable interrupts, then re-enable them only if they were enabled before.
- **Masking:** only IRQ0 and IRQ1 are unmasked. Start-up panics if the final mask isn't exactly the expected value (`0xFFFC` when both devices work).

**Rules for interrupt handlers:**

- **Allowed:** reading the device, updating counters and state, pushing to a queue.
- **Never:** writing to VGA or serial, allocating memory, waiting, looping without a bound, running diagnostics, or panicking over a device problem.

### Crash screen (`kernel/panic.c`)

- `panic_exception(frame)` shows the vector, name, mnemonic, error code, `RIP/CS/RFLAGS`, `RAX`–`R15` (three per line), the interrupted `RSP`, segment registers, `CR0/CR2/CR3`, `CR4` and `EFER`, white on red.
- For a page fault (exception 14) it adds the decoded error code after it, on up to three lines (0.6; split in 0.7 so no line reaches the 80th column):

  ```
   Page fault: protection violation, read, kernel mode
   Details: instruction fetch (NX violation)
   Address: 0x000000000015b000
  ```

  `Details:` appears only when the reserved-bit or instruction-fetch bits are set. Error-code bits: 0 not present / protection violation, 1 read / write, 2 kernel / user mode, 3 reserved bit set, 4 instruction fetch; a protection violation on an instruction fetch is an **NX violation**. The address is CR2. Every page fault is fatal: nothing is loaded on demand.
- For a double fault (exception 8) it says why, when it can tell: `Double fault: kernel stack overflow (RSP is in the stack guard page)` with the RSP and the guard page's range; or that the stack reached its guard page (CR2 in the guard); since 0.9, that a kernel thread's stack overflowed, naming the thread (`Double fault: kernel thread stack overflow (thread 1 'overflow')`, then the RSP or CR2 and that thread's guard range); or that the IST1 stack overflowed; otherwise that it was handled on its own stack and RSP is not in the guard page.
- `panic(message)` and `panicf(format, ...)` show a text message instead, for fatal start-up errors and memory misuse (e.g. `Double free of physical page 0x0014c000`).
- Both copy everything to serial, then halt with `cli; hlt`. Since 0.9 the crash screen first sets `panic_active`, which exempts it from the rule that only thread 0 writes to the screen: whichever thread crashed, the screen shows it.
- **Line width:** every crash-screen line is at most 79 characters. The VGA driver moves to the next row after column 79, so an 80-character line would leave a blank line. A QEMU test checks every crash screen.

**Stack overflow (0.7):** the kernel stack (16 KiB) and the IST1 stack (8 KiB) each have an unmapped guard page directly below them. When the kernel stack runs into its guard page, the CPU can't push the page-fault frame there and raises a double fault instead, which runs on IST1 and reaches the crash screen. Before 0.7 this reset the machine.

**Stack high-water mark (0.8, `kernel/stackpeak.c`):** the guard page catches an overflow, but not how close the stack comes to one. So the very first thing `kernel_main` does is paint the unused part of the kernel stack with `0xA5`: from the stack's bottom up to 256 bytes below the current RSP. Interrupts are still off at that point (the stub cleared RFLAGS), so no interrupt frame can land in the painted area, and the 256-byte margin keeps the paint clear of the live frames (`stack_paint` is a leaf call that touches only its return address). Later, `stack_peak` finds the lowest byte that no longer holds the pattern; everything above it counts as used.

- The result goes to serial at "System ready." (`Kernel stack: 1496 bytes at peak, of 16384` in 0.9) and is the last row of `mem`. The paint itself is logged too (`painted 0x…-0x…, 256 bytes below RSP 0x…`).
- The 0.8 baseline is **1480 of 16384 bytes** after booting, after the keyboard flood and after the console session, in QEMU and VirtualBox alike; in 0.9 it is **1496**.
- Since 0.9 every kernel thread's stack is painted the same way when the thread is created, and the thread's final peak is logged when it exits (see *Kernel threads and scheduler*).
- It can read a few bytes too low (if the deepest byte ever written happened to equal `0xA5`), never too high. The top 736 bytes (the stub's frames plus the margin) are never painted and always count as used, so 736 is the smallest value it can report.
- It covers the kernel stack only; the IST1 stack isn't painted.
- `stackpeak.c` is pure and unit-tested (`test_stackpeak`).

## Memory [built: 0.3]

Physical memory management. It runs before the kernel's own page tables are loaded, on the boot stub's identity tables (since 0.7; with paging off up to 0.6), when a physical address is also a pointer. Every figure is calculated at runtime from the bootloader's memory map; nothing about the machine is hard-coded. Virtual memory is described in *Virtual memory and kernel heap* below.

| Module | Pure? | Responsibility |
|---|---|---|
| `kernel/multiboot.h` | – | Multiboot 1 structures, with field offsets checked at compile time |
| `kernel/memory/mmap.c` | yes | Validate the Multiboot memory map, then iterate over it in place |
| `kernel/memory/reserve.c` | yes | Build the list of memory that must never be allocated |
| `kernel/memory/pmm.c` | yes | Bitmap page allocator |
| `kernel/memory/memory.c` | no | Put the pieces together, compute statistics, write the serial log, panic on fatal errors; `memory_alloc_page` / `memory_free_page` |
| `kernel/memory/memtest.c` | no | Boot self-test and the Diagnostics memory tests |

A **pure** module doesn't access hardware, print or panic. That lets the host compile it and unit-test it with synthetic memory maps (`tests/unit/`).

### Assumptions

- **Multiboot 1** (spec 0.6.96): magic `0x2BADB002`, memory map present when info flag bit 6 is set. `boot.asm` asks for it with header flag `MEMINFO`. Multiboot 2 isn't supported.
- **Below 4 GiB only:** only physical addresses below 4 GiB are managed (the allocator's bitmaps cover the 32-bit space). RAM above 4 GiB is counted and shown, but not used yet.
- **Before the kernel's tables:** memory set-up and the boot self-test run on the stub's temporary tables, which identity-map 0–4 GiB, so a physical address below 4 GiB is also a pointer. Once the kernel's tables are loaded (0.6; 4-level since 0.7), nothing below 4 GiB is mapped any more (0.8). The Multiboot data stay reachable through the physical view (`phys_view()`), which maps every boot-data reservation; free pages are not mapped, so `memtest_exhaust` reaches them through the physical-access window.
- **Physical addresses of the kernel (0.8):** the linker symbols are high virtual addresses. `memory.c` and `memtest.c` convert them with `kernel_phys()` before using them as physical addresses (so the pure `reserve.c` still receives physical ranges and is unchanged), and `memory.c` reads the memory map through the physical view. `mmap_validate()` keeps validating the map's **physical** address (null, wrap past 4 GiB) but reads the bytes through a separate `view` pointer, and `mmap_iter_init()` iterates through that view; the checks are unchanged and the unit tests cover the two addresses separately.

### Memory map handling (`mmap.c`)

Entries are `{u32 size; u64 base; u64 length; u32 type}`. The next entry starts at `entry + size + 4`, and a `size` larger than 20 is allowed (the extra bytes are skipped). The whole buffer is validated **before any entry is used**:

| Problem | Result |
|---|---|
| Flag bit 6 missing | crash screen: "no memory map" |
| Length 0, or no non-empty entry | `memory map is empty` |
| Null address, or buffer wraps past 4 GiB | `buffer address is invalid` |
| Entry `size < 20` | `entry size is too small` |
| Entry extends past the buffer | `entry is truncated` |
| `base + length` overflows 64 bits | `entry overflows 64 bits` |

Zero-length entries are skipped. The map is never copied, so there's no entry limit that could silently drop a reserved region.

| Type | Meaning | 0.3 treatment |
|---|---|---|
| 1 | available | **usable** |
| 3 | ACPI reclaimable | RAM, but **not used**. It can be reclaimed once ACPI tables are parsed, in a later milestone. |
| 4 | ACPI NVS | RAM, never used |
| 5 | bad RAM | RAM, never used |
| 2 or unknown | reserved | not RAM, never used |

### Reserved memory (`reserve.c`)

| Range | Source of the addresses |
|---|---|
| `0x00000000–0x00100000` (low 1 MiB) | **Deliberate 0.3 safety decision.** This area holds the real-mode interrupt table and BIOS data, page 0, the EBDA (extended BIOS data area), **VGA memory `0xA0000–0xBFFFF`** (including the text buffer at `0xB8000`) and the BIOS ROM. It costs 159 usable pages (636 KiB). |
| Kernel image: `.text` (including the Multiboot header), `.rodata`, `.data`, `.bss` | linker symbols `__kernel_start` / `__kernel_end` (page-aligned) |
| Boot stack | inside `.bss`. `stack_bottom` and `stack_top` are exported from `boot.asm`, and start-up checks that they lie inside `__bss_start–__bss_end`. Since 0.7 `.bss` also holds the stub's boot page tables, the guard pages and the IST1 stack. |
| Allocator bitmaps (256 KiB) | static arrays in `.bss`, so covered by the kernel range |
| Multiboot info block | the `ebx` pointer; length is the size of the full Multiboot 1 structure (116 bytes) |
| Memory map buffer | `mmap_addr`, `mmap_length` |
| Command line | `cmdline` address, string length + 1 (must be terminated within 4 KiB) |
| Boot modules | module table (16 bytes × count), each `[mod_start, mod_end)`, each module name |

Anything that isn't type 1 (firmware, ACPI, the interrupt controllers at `0xFEC00000` and `0xFEE00000`, ROM) is never usable, and neither is anything missing from the map entirely. Start-up also checks that the kernel lies inside a single type-1 region.

**Why the explicit Multiboot reservations matter:** QEMU's loader puts the command line and modules immediately after the kernel (at `0x14A000`), and GRUB under VirtualBox also uses the two pages after the kernel. Both are ordinary usable RAM above 1 MiB, so reserving only the low 1 MiB and the kernel would not be enough.

### Page allocator (`pmm.c`)

A **bitmap** allocator with two 128 KiB bitmaps covering all 1,048,576 pages of the 32-bit space:

| Bitmap | 1 means |
|---|---|
| `allocatable` | the page may ever be handed out (usable and not reserved) |
| `used` | the page is allocated, or not allocatable |

A page is free when `allocatable = 1` and `used = 0`. The two maps let `free` tell a reserved page apart from a double free.

**Phased set-up. Reserved memory always wins, whatever order regions are discovered in:**

1. `pmm_begin()`: everything unavailable.
2. `pmm_add_usable()`: type-1 regions, rounded **inward** to whole pages and clipped at 4 GiB.
3. `pmm_reserve()`: every non-type-1 map region, then every reservation, rounded **outward**. After the first reservation, `pmm_add_usable()` is refused (`PMM_ERR_PHASE`).
4. `pmm_finish()`: page 0 removed, counts fixed, allocation enabled.

**Allocation and freeing:**

- **Allocate:** `pmm_alloc_page()` returns the **lowest** free page, so results are deterministic and easy to debug. Page 0 is never allocatable, so **0 means "out of memory"**. Exhaustion isn't an error: the caller decides what to do.
- **Free:** `pmm_free_page()` returns `PMM_ERR_UNALIGNED`, `PMM_ERR_RESERVED` or `PMM_ERR_DOUBLE_FREE` on misuse. The kernel wrapper `memory_free_page()` turns each of these into a crash screen that shows the address.
- **Speed:** a hint (the first bitmap word that might have a free page) makes allocation effectively constant-time.
- **No `libgcc`:** 64-bit values only use shifts, additions and comparisons, and bits are counted without `__builtin_popcount`.

**Why a bitmap rather than a free list:**

- Double frees are detected exactly.
- Nothing is written into free pages.
- The results are deterministic.
- The state is a plain array that's easy to dump.

A free list's only advantage is speed, which doesn't matter yet.

### Statistics (the boot screen's right column)

| Figure | Definition |
|---|---|
| Physical RAM detected | all RAM-type regions (types 1, 3, 4, 5), **including RAM above 4 GiB** |
| Usable RAM (<4 GiB) | usable pages × 4096 |
| RAM above 4 GiB | RAM the memory manager doesn't use yet (it manages the 32-bit physical space only). It's reported separately and never counted as usable. |
| Usable pages | whole type-1 pages below 4 GiB, minus pages touched by other map entries |
| Free pages | usable pages minus the reservations, counted from the bitmap after set-up |

MB values mean MiB (1,048,576 bytes), rounded to the nearest whole number. The serial log has the exact byte counts.

### Self-tests (`memtest.c`)

**Every boot**, before `Memory initialization: OK` is shown:

- allocate 3 pages, and check each is aligned, safe, marked used and in increasing order;
- write every word of the 3 pages and read them back;
- free the middle page, and check that reallocating returns the same page;
- free everything, and check the free count is restored;
- check that page 0, VGA, the first and last kernel pages, and the first and last page of every reservation are not allocatable.

"Safe" is decided by `memory_page_is_safe()`, which re-reads the map and the reservation list rather than trusting the allocator's own bitmaps. Any failure goes to the crash screen, so **`OK` means verified**.

**Diagnostics** (from the GRUB menu, or QEMU `-append`):

- `test=memexhaust`: allocate every free page, then check each is safe, strictly increasing and tagged. After that, allocation must keep returning 0. Each page's tag is verified, every page is freed, and the free count must be restored. Since 0.6 each page is reached through the physical-access window (mapped, used, unmapped). Pages already in use before the test (page tables, the top-level table (the page directory up to 0.6, the PML4 since 0.7), heap pages; at most 256) are recorded first and left alone; the kernel logs how many it expects, and the QEMU test requires the two counts to match exactly.
- `test=doublefree`: must end on the crash screen with `Double free of physical page 0x…`.
- `test=freereserved`: must end on the crash screen with `Attempt to free reserved page 0x00100000`.

### Fatal conditions

The following end on the crash screen with a specific message:

- no Multiboot info;
- no memory map, or a malformed one;
- a reservation that's invalid, or that overflows the reservation table (32 entries);
- a kernel outside usable RAM, or a boot stack outside `.bss`;
- no free pages;
- any self-test failure;
- any misuse of `memory_free_page`.

The memory subsystem is unchanged in 0.4. Its tests now also run with IRQ0 and IRQ1 live. In 0.6 only `memtest.c` changed (the window, and leaving pre-existing pages alone); `mmap.c`, `reserve.c`, `pmm.c` and `memory.c` are unchanged. In 0.7 none of these files changed; they are built as 64-bit code, and their unit tests run as 64-bit programs. In 0.8, `mmap.c`/`mmap.h` (the separate read view), `memory.c` (its call sites and the stale "(32-bit, no PAE)" serial text) and `memtest.c` (kernel symbols converted to physical) changed, each approved in the 0.8 plan; `reserve.c` and `pmm.c` are unchanged. In 0.9 only `memory.c` changed: `memory_alloc_page` and `memory_free_page` run with preemption off (a mechanical guard, approved in the 0.9 plan), so threads can allocate frames safely.

## Virtual memory and kernel heap [built: 0.6, 4-level since 0.7, higher half since 0.8]

| Module | Pure? | Responsibility |
|---|---|---|
| `kernel/memory/vm_layout.h` | – | Every virtual-layout constant; `phys_view(phys)` (address in the physical view) and `kernel_phys(virt)` (physical address of a kernel-image address) |
| `kernel/memory/pte.h` | yes | 4-level entry bits (present, write, user, NX), index splitting per level, canonical-address checks, alignment helpers, recursive-table addresses |
| `kernel/memory/vmm_core.c` | yes | Map / unmap / translate 4 KiB pages; creates PDPT, PD and PT on demand, all-or-nothing; refuses non-canonical addresses, page 0 and the recursive slot; accepts only the write and NX flags (never user pages); never replaces a mapping; flushes on unmap. Reaches the tables only through callbacks, so it is unit-tested on fake frames. |
| `kernel/memory/paging.c` | no | NX set-up, builds the tables (kernel image, physical view, nothing in the lower half), walk-checks them, loads CR3, self-test; `paging_map/unmap/translate`; the physical-access window |
| `kernel/memory/heap.c` | yes | First-fit allocator with boundary tags over one growable arena |
| `kernel/memory/kmalloc.c` | no | The kernel heap: `kmalloc`, `kcalloc`, `kfree`; grows by mapping new frames; self-test |
| `kernel/memory/vmtest.c` | no | Page-fault (including `pflow`), NX, stack-overflow and heap diagnostics |

**Layout (0.8: 4-level paging, higher-half kernel, empty lower half, 4 KiB pages only):**

| Virtual range | PML4 slot | Maps to | Access |
|---|---|---|---|
| `0x0000000000000000–0x00007FFFFFFFFFFF` (the whole lower half) | 0–255 | – | **unmapped** (0.8): PML4[0] is not present, so every address in 0–4 GiB faults: page 0 (NULL), the kernel's old address `0x100000`, VGA's physical address, free RAM |
| `0xFFFF800000001000–0xFFFF8000000FFFFF` | 256 | physical `0x1000–0xFFFFF` (pages 1–255) | physical view: read/write, no-execute (VGA text buffer at `0xFFFF8000000B8000`, EBDA, ROM, GRUB data) |
| `0xFFFF800000000000` + each boot-data reservation | 256 | that reservation | physical view: read/write, no-execute (Multiboot info, memory map, command line, module table, modules, module names) |
| rest of slot 256 | 256 | – | unmapped: page 0 (`0xFFFF800000000000`), **free RAM** and the **kernel image** are deliberately not in the view |
| `0xFFFFFD8000000000–0xFFFFFD80001FFFFF` (0.9) | 507 | thread stack frames | kernel thread stacks: 64 slots of 32 KiB; in each, a 16 KiB **unmapped guard**, then a 16 KiB stack (read/write, no-execute) mapped while its thread exists. Empty at boot; its page table is made at boot |
| `0xFFFFFE0000000000` | 508 | – | page used by the paging self-test |
| `0xFFFFFE0000001000` | 508 | any frame | physical-access window (one page) |
| `0xFFFFFE8000000000` → heap end | 509 | heap frames | read/write, no-execute; 64 KiB at start, at most 16 MiB; unmapped above the end |
| `0xFFFFFF0000000000–0xFFFFFF7FFFFFFFFF` | 510 | page tables | recursive mapping; `0xFFFFFF7FBFDFE000` is the PML4 |
| `0xFFFFFFFF80100000` = `__text_start` → `__rodata_start` | 511 | kernel code (physical `0x100000`…) | **read-only, executable**: kernel code, including the boot stub's page (CR0.WP makes read-only apply to the kernel too) |
| `__rodata_start–__data_start` | 511 | kernel rodata | read-only, no-execute |
| `__data_start–__kernel_end` | 511 | kernel data | read/write, no-execute: `.data`, `.bss`, allocator bitmaps, GDT, IDT, TSS, the kernel and IST1 stacks, **except the two guard pages, which are unmapped** |

Every kernel-image address is `0xFFFFFFFF80000000` + its physical address, and every physical-view address is `0xFFFF800000000000` + its physical address, so both translations are a single addition (`kernel_phys()`, `phys_view()`).

**The physical view (0.8)** exists because the kernel still has to read things the bootloader and the hardware put at fixed physical addresses (VGA, the Multiboot info, the memory map, the command line, modules), and the lower half is gone. It maps only those: the low 1 MiB without page 0, and every boot-data reservation. It deliberately leaves out:

- **page 0**, so `phys_view(0)` still faults like NULL;
- **free RAM**, so a stray pointer into the view can't silently corrupt memory the allocator hands out (free frames are reached one page at a time through the window, as since 0.6);
- **the kernel image**, so there is no second, writable alias of kernel code or read-only data. The walk-check and the self-test prove it (`kernel image not in the physical view … expected unmapped`), and a QEMU check requires both lines.

There is still no direct map of all physical memory.

**The empty lower half (0.8):** the kernel's tables never get a PML4[0] entry. Once CR3 is loaded, nothing below `0x0000800000000000` is mapped, which the walk-check and the self-test prove twice each: a fact line `lower half empty: PML4[0] not present`, and point translations of `0x0`, `0x100000`, `0xB8000`, `0xFFFFF000` and the Multiboot info's physical address, all expected unmapped. The `pflow` diagnostic shows what a stray low access looks like: reading `0x100000`, the kernel's old identity address, ends on the crash screen as a page fault (not present, read, CR2 `0x100000`).

**No-execute (NX), 0.7:** the CPU's NX support is read from CPUID `0x80000001` (EDX bit 20). If present, `paging_init` sets EFER.NXE (and reads it back; LMA must still be set), and every mapping except kernel code gets the NX bit. Callers take the bit from `paging_nx_flag()`, so on a CPU without NX the kernel runs the same way with no NX bits (the walk-check then expects none). Executing data ends on the crash screen with a page fault decoded as an **NX violation**.

**Loading the kernel's tables (`paging_init`):** the tables are built while the stub's temporary tables are still in use, reached through their physical addresses. Before anything is loaded, a **walk-check** translates everything the kernel is running on and compares it with what is expected, including the access (read-only or read/write, executable or no-execute):

- the current RIP and RSP; the interrupt entry stubs, `interrupt_dispatch`, `panic_exception` and `irq_dispatch` (all must stay executable);
- the kernel stack and the IST1 stack; **both guard pages must be unmapped**, and each must lie directly below its stack;
- the kernel's code, read-only data and writable parts, at their higher-half addresses (`expected kernel ro exec` and so on); VGA and the low 1 MiB in the physical view (`phys-view rw nx`);
- the GDT, IDT and TSS (found with `sgdt`, `sidt` and `str`), and that each lies inside the kernel's writable data;
- `.bss` (with the allocator bitmaps), and every reservation in the physical view;
- page 0, the table frames themselves, the heap area, the self-test page and the window must be unmapped; the window's page table must exist;
- 0.8: PML4[511] maps the kernel image and PML4[256] the physical view; the lower half is empty (PML4[0] not present, and the five low addresses above unmapped); the kernel image, page 0 and a free-RAM frame (the PML4's own frame and the page-table frames) are not in the physical view;
- 0.9: the kernel thread stack area (PML4[507]) is empty at boot and its page table exists;
- the recursive entry must point to the PML4 (read/write, no-execute);
- the table and page counts are recomputed independently.

On `MadireOS-Test` and in QEMU this is 76 items (79 with one boot module); it was 68 (71) in 0.8. The 8 new items are the two slot-507 facts above, plus "unmapped" and "not in the physical view" for each of the area's three new table frames.

Any mismatch frees the frames and leaves the temporary tables in place (`Paging & kernel heap: FAILED (paging)`, boot continues). Otherwise CR3 is loaded, and from then on the tables are reached only through the recursive mapping.

**Paging self-test** (a failure is fatal): CR0.PG and WP set, CR3 is the PML4, CR4.PAE and EFER.LMA set, the recursive entry reads back correctly, the kernel, stack, VGA and boot data still translate, page 0 and both guard pages don't; since 0.8 also, on the live tables, the lower half is empty, the low addresses are unmapped, and neither the kernel image nor page 0 is in the physical view (26 checks with a GRUB boot, 27 with QEMU's `-kernel`). Then a test page is mapped to frame A, written and read back, unmapped; remapped A → B → A → B, each frame keeping its own pattern (a missing TLB flush would put B's data into A); the window is mapped to A, read and written, unmapped and must no longer translate; the free page count must return exactly to its start value.

**TLB flushes:** `invlpg` on every unmap (one flush point, in `vmm_core`, counted by the unit tests). A new mapping needs no flush: the CPU never caches "not mapped".

**Physical-access window:** `vmm_window_map(phys)` maps one frame at `0xFFFFFE0000001000`; `vmm_window_unmap()` clears the entry, flushes it and checks it no longer translates. The frame is never freed by the window. One user at a time, normal context only: mapping while mapped, or unmapping while not mapped, panics. The window's page table is made at boot, so it works even when physical memory is exhausted.

**Kernel heap:** blocks are `[16-byte header: magic | size | prev_size | check][payload]`, all sizes multiples of 16 (magic `0xA110C8ED` used, `0xF4EEB10C` free; check = magic ^ size ^ prev_size ^ `0x5A5A5A5A`).

- `kmalloc`: first free block that fits; a remainder of 32 bytes or more is split off. If nothing fits, the heap grows by whole pages (at least 16 KiB) mapped at its end, up to 16 MiB. Out of room or out of frames → `NULL`, never a panic. `kmalloc(0)` returns `NULL`; `kcalloc` zeroes and refuses `n × size` overflow.
- `kfree`: checks the pointer is in the arena and 16-byte aligned, then validates the **whole heap** before changing anything, then merges with free neighbours on both sides. A pointer not from the heap, a misaligned one, a double free (also after the block merged) or a corrupted header ends on the crash screen, e.g. `Heap: double free of 0xfffffe8000000010.`
- `heap_check` (every `mem` report, the self-test and the diagnostics): every header valid, `prev_size` links consistent, sizes adding up to the arena, no two free blocks adjacent.
- **Context rule:** `kmalloc`, `kcalloc` and `kfree` panic if interrupts are disabled. Every IRQ and exception enters through an interrupt gate, which clears IF, so a call from a handler is always caught. This also refuses calls from normal code inside an `irq_save()` section (intended: the heap has no locking). It can't catch a handler that re-enables interrupts itself; none does.
- **Self-test at boot:** 200 mixed allocations from a fixed pseudo-random sequence, each filled with a pattern and checked before it is freed (in a different order); the heap must then be one free block; a request larger than the arena must grow it; the free page count must drop by exactly the pages the heap grew by (plus any new page table).

**Diagnostics:**

| `test=` | GRUB entry | Result |
|---|---|---|
| `pfnull` | Page-fault test: read from NULL | address and CR2 `0x0000000000000000`, `not present, read, kernel mode` |
| `pfunmapped` | Page-fault test: read from unmapped address | address and CR2 `0x0000000040000000`, `not present, read, kernel mode` |
| `pfreadonly` | Page-fault test: write to read-only kernel code | address and CR2 `0xffffffff80100000` (0.8: kernel code is high; `0x0000000000100000` up to 0.7), `protection violation, write, kernel mode` |
| `pfnx` (0.7) | Page-fault test: execute from no-execute data (NX) | first calls kernel code (must run), then calls a data page holding a `ret`: error code `0x11`, `protection violation, read, kernel mode`, `Details: instruction fetch (NX violation)`, address = the page. On a CPU without NX the call runs, and the test reports **NOT APPLICABLE** (never a pass) |
| `pflow` (0.8) | Page-fault test: read the kernel's old low address | reads `0x100000`, where the kernel ran up to 0.7: error code `0x0`, `not present, read, kernel mode`, address and CR2 `0x0000000000100000`, halted. Proves the low identity mapping is gone |
| `stackoverflow` (0.7) | Crash test: kernel stack overflow | recursion with a 512-byte frame until the kernel stack reaches its guard page: `CPU exception 8: Double Fault (#DF)`, `Double fault: kernel stack overflow (RSP is in the stack guard page)`, no reset. Without paging there is no guard page, and the test reports NOT APPLICABLE |
| `heapdouble` | (QEMU only) | `Heap: double free of 0xfffffe80000…` |
| `heapinvalid` | (QEMU only) | `Heap: invalid free of 0x… (pointer not from the heap).` (a stack address) |
| `heapcorrupt` | (QEMU only) | `Heap: corrupted block at 0xfffffe80000… (freeing 0xfffffe80000…).` after overwriting a header |
| `heapexhaust` | (QEMU only) | allocate until `NULL` (the 16 MiB cap, or physical memory), check, free everything; the pages used must be exactly the heap's growth plus its page tables |

**Under preemption (0.9):** `paging_map`, `paging_unmap`, `paging_translate`, `kmalloc`, `kcalloc`, `kfree` and `kheap_stats` each run with preemption off. The window is held with preemption off from `vmm_window_map` to `vmm_window_unmap`, so no other thread can map it in between, and yielding while holding it panics. The heap is still not interrupt-safe; the context rule above is unchanged.

**Known limits:** no direct map (the physical view covers only the low 1 MiB and the boot data); 4 KiB pages only, also for the kernel image; one address space, kernel mode only; until `paging_init` loads the kernel's tables the boot tables still map 0–4 GiB at 0, and if the walk-check fails the kernel stays on them; the heap never shrinks and isn't interrupt-safe; `kfree` validates the whole heap, which costs O(number of blocks) per free.

## Boot modules [built: 0.3 reserved, 0.7 checked, 0.8 on screen]

GRUB (`module` lines) or QEMU (`-initrd`) can load modules. Their ranges are reserved since 0.3. Since 0.7, after paging and the heap are set up, `check_boot_modules()` looks at each one: a **test module** (`kernel/bootmod.h`: `MADIRETM`, payload length, FNV-1a of the payload, then the payload) must still be intact, which proves the reservations kept it safe through memory set-up, the self-tests and the switch of page tables. The result goes to the serial log, e.g. `Boot module 1: 0x0016f000-0x001726c0 (14016 bytes): test pattern intact (FNV-1a 0x2feffe68, expected 0x2feffe68)`; a changed byte is reported as `CORRUPTED`. `kernel/bootmod.c` is pure and unit-tested. The module table and the modules are read through the physical view (0.8).

**On screen (0.8):** for machines without a serial port, the result also goes to the boot screen's message line (row 23), e.g. `Boot module 1: test pattern intact (14016 bytes)` or `… test pattern CORRUPTED (14016 bytes)`. Only when a module was loaded: a normal boot's screen is unchanged. With several modules, each result replaces the previous one on that line, so only the last stays visible; the serial log has all of them.

## Timer [built: 0.4]

| Item | Design |
|---|---|
| Hardware | PIT channel 0 → IRQ0 (vector 32); `drivers/pit/pit.c` |
| Mode | Mode 2 (rate generator), low byte then high byte, binary: command `0x34` |
| Rate | **100 Hz**. Divisor `round(1193182 / 100)` = **11932** (`pit_divisor_for()`, unit-tested), giving 99.998 Hz. |
| Ticks | 64-bit counter, incremented in the IRQ0 handler. It's read with interrupts briefly off: on 32-bit x86 (up to 0.6) that took two 32-bit loads, and the code is unchanged in 64-bit. It can't wrap in practice (5.8 billion years). |
| Seconds | A separate 32-bit counter, advanced every 100 ticks inside the handler, so nothing needs 64-bit division |
| Order | Register the handler, program the PIT, unmask IRQ0, verify the mask (`timer_init`) |
| Self-test | Two latched counter reads prove the PIT is counting. A **bounded busy-wait** (no `hlt`, so a dead timer can't hang the boot) must then see 3 ticks. The failure message tells "PIT not counting" apart from "counting but no IRQ0". |
| Waiting | `timer_wait_ticks(n)` uses `hlt`, but refuses immediately if the timer isn't verified or interrupts are off, so it can never hang |
| Reference clock | `kernel/arch/x86/cmos.c`: **read-only** RTC seconds, used only by the timer diagnostic. It waits out update-in-progress (bounded), reads until two reads agree, and handles BCD. No RTC interrupts. |

## Keyboard [built: 0.4]

| Item | Design |
|---|---|
| Hardware | i8042 controller (`0x60`/`0x64`), PS/2 port 1 → IRQ1 (vector 33); `drivers/keyboard/ps2kbd.c` |
| Set-up ("light touch") | ① Check the controller exists (status isn't `0xFF`). ② Empty the output buffer. ③ Config byte: IRQ1 on, **translation on (set 1)**, port 1 clock on, port 2 IRQ off; write it and verify by reading back. ④ Enable port 1. ⑤ **Echo** (`0xEE`) polled with IRQ1 masked, which proves a keyboard is present. ⑥ Register the IRQ1 handler, empty the buffer again, unmask IRQ1. No controller self-test, no keyboard reset, no LEDs. |
| Timeouts | Every controller wait is a bounded loop. Failures give a status (`no i8042 controller`, `controller timeout`, `configuration did not stick`, `no keyboard answered`, `IRQ1 could not be enabled`); the boot screen shows `FAILED` and boot continues. |
| IRQ handler | Checks the status port, then reads **one** byte. Port-2 bytes are ignored. Keyboard replies (`FA` ACK, `EE` echo, `FE` resend, `00`/`FF` errors) are recorded as responses, not keys. Everything else goes to the decoder, then the queue. |
| Decoder | `drivers/keyboard/scancode.c`, pure and unit-tested. Set 1 key presses and releases; `E0` extended keys; fake shifts (`E0 2A`/`E0 36` and their releases) ignored; the `E1` Pause sequence gives one event. Left and right Shift, Ctrl and Alt are tracked; Caps Lock toggles on the first press only (not on auto-repeat). Letters: Shift XOR Caps; symbols: Shift. US layout; keypad keys report their digit (Num Lock assumed on). |
| Queue | `kernel/input/keyqueue.c`, pure and unit-tested. 64 events; single producer (IRQ1) and single consumer (normal context, popping with interrupts off); free-running counters. **When full, the newest event is dropped and counted**, so queued events keep their order. |
| Self-test | ① Decoder: a known byte sequence (Shift+A, Up). ② Queue: a private instance filled, overflowed and drained in order. ③ **IRQ1 delivery:** `0xEE` is sent with IRQ1 unmasked, and the echo must come back *through the interrupt handler* (bounded wait). Only if all three pass does the screen show `Keyboard (PS/2): OK`. |

## Idle loop [built: 0.4, console added 0.5]

After "System ready.", `kernel.c` runs the idle loop on thread 0. It sleeps until any interrupt, then handles whatever the interrupt handlers queued or counted. Since 0.9 each pass ends with `thread_idle()` instead of a bare `hlt`: it frees exited detached threads, yields if another thread is ready, and otherwise halts until the next interrupt as before. The loop handles:

- **Key events** are logged to serial, as in 0.4.
  - While the boot screen shows, a pressed key updates "Last key" and **opens the console** (if it was offered). That key isn't typed.
  - Once the console is active, every key event goes to `console_handle_key()`.
- **Counters:** new drops and unexpected IRQs are reported to serial.
- **Uptime:** when the seconds counter changes, "Uptime" is redrawn while the boot screen shows, and every 5 s `uptime: N s` goes to serial.

All screen output happens here, on thread 0, and no output at all happens in interrupt context. The shell runs here too, one command at a time. A key arriving just before `hlt` waits at most one timer tick (10 ms).

## Console and shell [built: 0.5]

```
IRQ1 → 0.4 driver → decoder → key queue                          [unchanged]
idle loop → console_handle_key → line editor → (Enter) → parser → shell command
          → output sink → console text grid → vga_put_cell + hardware cursor
                                            → serial copy
```

| Module | Pure? | Responsibility |
|---|---|---|
| `kernel/console/textbuf.c` | yes | The console's own 80×25 grid (character plus colour per cell): writing, newline, wrap at column 80, scrolling, clear |
| `kernel/console/lineedit.c` | yes | Input line: insert at cursor, Backspace, Delete, Left/Right/Home/End, Enter. At most **71 characters** (8-character prompt + 71 = one row). Presses only; any key with Ctrl or Alt held is ignored; no history. |
| `kernel/shell/parser.c` | yes | Splits the line in place: spaces and tabs separate, repeats collapse, leading/trailing ignored; at most **8 words** |
| `kernel/shell/shell.c` | yes | The seven commands (eight with `ps`, 0.9), reading values from a read-only `shell_context` and writing through a `shell_output` sink, so every output is unit-tested exactly |
| `kernel/shell/cpuinfo.c` | yes | Decodes raw CPUID values: vendor, family/model/stepping (extended-field rules), feature names, brand string, hypervisor vendor |
| `kernel/arch/x86/cpuid.c` | no | Checks CPUID exists by toggling EFLAGS bit 21, then runs only the leaves the CPU lists; reads CR0 |
| `kernel/console/console.c` | no | Owns the grid and draws it to VGA (`vga_put_cell`); moves the hardware cursor; copies submitted lines (`madire> …`) and output to serial (each line with preemption off, 0.9); builds the shell context, including a snapshot of the threads for `ps` (0.9); runs the start-up self-test |

- **Screen:** the console takes over the whole 80×25 screen when it starts. It shows a banner (`M A D I R E   O S`, `Madire OS v0.9`, a hint) and the `madire> ` prompt.
  - Colours use only VGA slots the logo doesn't reprogram: grey text, white input, cyan prompt, red errors.
  - Output scrolls up at the bottom. There's no scrollback.
  - `clear` empties the console's own grid and redraws it; it doesn't use `vga_clear()`.
- **Cursor:** the VGA hardware cursor, a blinking underline, marks the edit position. The VGA driver gained two additive functions: `vga_cursor_show()` and `vga_cursor_move()`.
- **Commands:**

  | Command | Output |
  |---|---|
  | `help` | the command list (since 0.9 `ps` follows the seven commands) |
  | `clear` | clears the screen |
  | `version` | `Madire OS v0.9 (64-bit x86-64, Multiboot 1)` when EFER.LMA is set (since 0.7); a context without EFER (the host unit tests) keeps the old `(32-bit x86, Multiboot 1)` wording |
  | `uptime` | `Uptime: HH:MM:SS (N ticks at 100 Hz)`. Uses the 32-bit seconds counter with 32-bit division only. The tick count is shown only while it fits in 32 bits, and is never divided. |
  | `mem` | the 0.3 boot figures, plus the live `pmm_free_pages()`; since 0.6 also `Paging` (enabled/disabled), `Mapped pages` (the kernel's own mapping made at boot), `Page tables` (now), `Kernel heap` (KiB mapped, max), `Heap in use` (KiB, blocks), `Heap free` (KiB, largest block). The two 0.6 sections come from optional `shell_context` pointers; without them the output is exactly the 0.5 one. Since 0.7 the paging line is followed by an `NX:` row (`supported, enabled`, `supported, not enabled` or `not supported by this CPU`); since 0.8 it reads `enabled (4-level, 4 KiB, higher-half, NX on)` (or `NX off`; `identity-mapped` in 0.7), and a last row gives the stack's high-water mark, `Kernel stack: 1496 bytes at peak, of 16384` (only when the context has the stack fields; without them the output is unchanged). Every `mem` line fits in 79 columns (checked by a QEMU test and `test_shell_mem08`). |
  | `cpu` | CPUID information, plus the mode line: `64-bit long mode, paging on, 1 CPU in use` when EFER.LMA is set (0.7); otherwise the CR0-based wording. `cpu` and `version` use the same long-mode test. |
  | `echo <text>` | the words joined by single spaces |
  | `ps` (0.9) | one row per live thread: `TID`, `STATE` (`running`, `ready`, `blocked`, …), `TICKS` (timer ticks it has run; `large` once the count no longer fits in 32 bits), `STACK PEAK` (`used/size` in bytes) and `NAME`, then a count line, e.g. `  0  running      338     1496/16384   kernel` and `1 thread`. A snapshot taken when the command runs. It comes from an optional `shell_context` field: without it, `ps` is an unknown command and `help` is exactly the 0.8 output |

  An unknown name gives `Unknown command: …`. Extra arguments give `Usage: …`, and more than 8 words gives `Too many arguments (max 8).`
- **Self-test (`Console & shell` status line):** exercises the *pure* modules with known input: the line editor, the parser, `echo ok` through the shell, and scrolling in a private grid. It isn't a test of keyboard or VGA hardware; those are proven by the keyboard self-test and by the QEMU and VirtualBox console tests.
- **When the console is offered:** only if both the console self-test and the keyboard passed. Otherwise the bottom row says why, and the idle loop runs as in 0.4.
- **Unchanged in 0.5:** the keyboard driver, decoder, queue, IRQ layer, PIC, PIT, timer and memory manager. There's no dynamic allocation: every buffer is static (still true in 0.6; the console doesn't use the heap).

## Kernel threads and scheduler [built: 0.9]

0.9 is the kernel side of processes: several flows of control inside the kernel, each with its own stack, switched by a preemptive scheduler. User mode, per-process address spaces and system calls come in 0.10.

| Module | Pure? | Responsibility |
|---|---|---|
| `kernel/proc/sched_core.c` | yes | The bookkeeping: thread states, the run queue, slice accounting, sleep and join, exit and reaping, the stack-slot bitmap, the turn-order check used by the tests. No hardware, printing or panics, so it is unit-tested (`test_sched`, `test_kstack`) |
| `kernel/proc/thread.c` | no | Owns the stacks, does the switches, turns scheduler errors into panics; `thread_create`, `thread_yield`, `thread_sleep`, `thread_join`, `thread_exit`, the idle step, the tick and preemption hooks |
| `kernel/proc/switch.asm` | no | `switch_to` (the context switch) and `thread_trampoline` (a new thread's first instruction) |
| `kernel/proc/preempt.h` | – | The preemption-disable count |
| `kernel/proc/schedtest.c` | no | The boot self-test and the thread diagnostics |

### Threads

- **Thread 0** is `kernel_main`'s own flow: the boot sequence, then the console and idle loop. It exists from `thread_init()` on, runs on the boot stack, and **never blocks or exits**, so there is always a thread that can run. When it has to wait (joining, sleeping) it polls: it yields if another thread is ready, else halts until the next interrupt.
- **Other threads** (at most 63) are created with `thread_create(name, fn, arg, detached)` and run `fn(arg)`; its return value is the exit code. Threads exist only with paging on (their stacks need it).
- **States:** `FREE`, `NEW` (being set up), `READY` (on the run queue), `RUNNING` (exactly one), `BLOCKED` (sleeping until a tick, or waiting in a join), `ZOMBIE` (exited, exit code kept until it is reaped).
- **Joining and reaping:** a joinable thread is freed by the thread that joins it; a detached thread is freed by thread 0's idle step. A thread never frees its own stack. Joining yourself, joining a detached thread, two joiners, and blocking or exiting thread 0 are refused.
- **Sleeping:** `thread_sleep(n)` blocks until the first tick at or after now + n; a woken sleeper goes to the tail of the run queue and asks for a switch.

### Scheduling

- **Round robin, preemptive.** A thread runs at most `SCHED_SLICE_TICKS` = 5 ticks (50 ms at 100 Hz) while another thread is ready; then a switch is requested (`need_resched`). Yielding, sleeping, joining or exiting takes the thread off the CPU at once. No priorities.
- **One switch point for preemption:** at the end of an interrupt, in `interrupt_dispatch`, after `irq_dispatch` has returned. The EOI has already been sent, so the PIC can deliver the next IRQ0 to whichever thread runs next. IRQ0 first charges the tick (`thread_tick`: slice accounting, waking sleepers); then `thread_preempt_point` switches if a switch is due and preemption is enabled. `timer.c` and `irq.c` are unchanged, and exceptions never switch.
- **Scheduler state** (`struct sched`, the stack-slot bitmap) is changed only with interrupts off, because the tick updates it from IRQ context.

```
IRQ0 → isr.asm → interrupt_dispatch → irq_dispatch (handler + EOI)        [unchanged]
                                    → thread_tick (slice, wake sleepers)
                                    → thread_preempt_point: need_resched and count 0?
                                          → switch_to(current, next)
thread_create → stack slot + frames → paint → initial frame → READY
new thread: switch_to "returns" into thread_trampoline → sti → fn(arg) → thread_exit(code)
thread_exit → ZOMBIE → joiner woken (or thread 0 reaps it) → stack and frames freed
```

### The context switch (`switch_to`)

`switch_to(&old_rsp, new_rsp)` pushes **RFLAGS** and the callee-saved registers (`rbx`, `rbp`, `r12`–`r15`) on the current stack, saves RSP, loads the other thread's RSP, pops its registers and RFLAGS, and returns on its stack. The System V ABI lets a called function clobber every other general register, so nothing else needs saving (the kernel uses no SSE or x87 state).

- **RFLAGS is part of the switch (0.9 correction F4).** The `iretq` IF-state test found a flag leaking from one thread to the next when only the general registers were switched. Now no flag (DF, AC, …) crosses threads on any path.
- **The interrupt flag on every path:** every switch happens with interrupts off. A thread that yielded restores its own IF through `irq_restore`; a preempted thread returns through `isr_common`'s `iretq`, which restores its own RFLAGS; a new thread starts with RFLAGS `0x2` (every flag clear) and `thread_start` executes `sti`, so it always starts with IF=1. `test=ifstate` proves each path.
- A new thread's stack is built by `thread_create` so that the first `switch_to` "returns" into `thread_trampoline` with `r12` = the function and `r13` = its argument, RSP 16-byte aligned.

### Thread stacks (PML4 slot 507)

| Item | Value |
|---|---|
| Area | `0xFFFFFD8000000000`, PML4 slot 507, 2 MiB |
| Slots | 64 × 32 KiB; slot *i* at `0xFFFFFD8000000000 + i × 32 KiB` |
| In each slot | the lower 16 KiB are **never mapped** (the guard), the upper 16 KiB are the stack (4 frames, read/write, no-execute), growing down towards the guard |
| Page table | made at boot by `paging_init`, so creating a thread never needs a table frame (and thread churn returns every frame) and the slot exists before 0.10 copies the kernel half of the PML4; the walk-check proves the area empty and its table present |
| Life cycle | frames mapped when the thread is created (all or nothing), the stack painted for the high-water mark, frames unmapped and freed when the thread is reaped |

A thread that overflows its stack writes into its guard and faults; the CPU can't push the page-fault frame there, so the double fault runs on IST1. The crash screen recognises the guard of slot 507 and names the thread: `Double fault: kernel thread stack overflow (thread 1 'overflow')`, then RSP (or CR2) and the guard's range (`test=threadoverflow`). Each exiting thread logs its final stack peak: `Thread N 'name' exited: code C, stack peak P of 16384`.

### Preemption-disable count (`preempt.h`)

The kernel has **no locks**. Shared kernel state without locking is instead changed with preemption disabled: `preempt_disable()` raises a count, `preempt_enable()` lowers it, and while it is above zero the scheduler never switches. Interrupts keep running (it isn't `cli`): the timer, keyboard and uptime are unaffected. On one CPU this is enough, because no other thread can run inside the section and interrupt handlers never use these subsystems. The count nests, and an underflow panics.

Guarded: every heap operation (`kmalloc`, `kcalloc`, `kfree`, `kheap_stats`), the frame allocator (`memory_alloc_page`, `memory_free_page`), `paging_map`/`unmap`/`translate`, the window (held from map to unmap), every `kputc`/`kputs`/`kprintf`/`klogf` call, the console's serial copy of each line, and every boot-screen call made through `BOOTSCREEN_SAFE()`.

**Blocking with preemption disabled is a bug:** `thread_yield`, `thread_sleep`, `thread_join` and `thread_exit` panic at once if the count is above zero: `thread_yield: called with preemption disabled (count 1, thread 0 'kernel').` (`test=preemptyield`, `preemptjoin`, `preemptsleep`).

**Deferred switch, policy (a):** when a tick asks for a switch while the count is above zero, the request stays pending (`need_resched`) and `preempt_enable()` does **not** switch. The switch happens at the end of the first interrupt after the count is back at zero: at most one tick (10 ms) later, sooner if another interrupt comes first. There stays a single switch point (the end of an IRQ), and `preempt_enable()` is safe anywhere, even with interrupts off. `test=preemptoff` holds the count for 20 ticks and proves that no switch happens meanwhile and that the other thread runs within one tick of the release.

**Known limitation:** a thread that spends almost all its time inside guarded sections gets turns longer than the slice (`framethreads` has measured up to 77 ticks). Correctness and progress are unaffected; every scheduler diagnostic logs its longest turn.

### Concurrency audit (summary)

Before the first switch was written, every mutable file-scope variable in `kernel/`, `drivers/` and `boot/` was listed with its protection under preemption (31 rows, in the stage-2 report). In summary:

| Protection | Structures |
|---|---|
| **Preemption off** for each operation | kernel heap; frame allocator; paging, `vmm` and the recursive mapping; the physical-access window (held from map to unmap); `kprintf`/`klogf` output (VGA cursor, serial port); the console's serial copy (F1); boot-screen calls through `BOOTSCREEN_SAFE()` (F2) |
| **Interrupts off** | the scheduler's queues and thread records (the tick changes them from IRQ context) |
| **Already IRQ-safe** (unchanged) | the key queue (popped with interrupts off, one consumer: thread 0); the timer counters (64-bit read with `irq_save`, 32-bit reads atomic); the IRQ counters |
| **Thread 0 only** | the console (grid, line editor, shell), the boot screen, the logo, VGA writes outside `kprintf`; enforced by `kputc` (F3) and `BOOTSCREEN_SAFE()` |
| **Boot only**, read-only afterwards | memory map and reservations, page-table build and walk-check state, the self-test page, GDT/IDT/TSS, PIC masks, CPU information, the command line and Multiboot data, the existing diagnostics' state, the boot stack's paint data |
| **Single aligned reads** | the free-page count and paging figures read by `mem` (at most one operation stale) |
| Crash screen | `cli` first, then halts: no switch can happen during it |

### Who may write to the screen

- **Only thread 0** writes to the screen. `kputc` checks it and panics, naming the thread, if another thread writes (F3); other threads log to serial with `klogf`. The crash screen is exempt (`panic_active`).
- **The boot screen:** `bootscreen.c` is protected and unchanged, with no protection of its own. Every call that can happen while other threads exist (the idle loop's uptime and last-key updates, the diagnostics' result messages) goes through **one wrapper**, `BOOTSCREEN_SAFE(call)` in `kernel.c`: it panics unless the caller is thread 0, and runs the call with preemption off (F2). Direct calls are allowed only in the boot sequence (`kernel_main`, `start_devices`, `check_boot_modules`) and in `devtest.c`, whose diagnostics run at boot before any other thread exists. `scripts/check-bootscreen.sh` enforces this: the ISO build fails on any other call.

### Scheduler self-test (every boot)

Runs after the console self-test, with paging on (`NOT APPLICABLE` without paging, since there are no thread stacks). Serial only; a failure is fatal (`Scheduler self-test failed (details on the serial log).` on the crash screen). 9 checks:

1. two threads are created;
2. **cooperative ping-pong:** they alternate 100 times through `thread_yield`. The order check counts the preemptions during the ping-pong: with none (the normal case), the alternation must be strict (`100 yields, strictly alternating`); with any, each thread must still have its full number of turns and the order may differ only right after a preemption (option (A), because VirtualBox can deliver timer ticks in bursts). Anything else fails;
3. the number of preemptions during the ping-pong is logged;
4. both are joined with their exit codes (11 and 22);
5. IF was 1 in the threads at every check, and thread 0 keeps IF=1;
6. the context-switch count;
7. a thread that never yields is preempted (at least 3 times in 30 ticks);
8. `sleep(10)` wakes after 10 or 11 ticks;
9. everything returned: free pages, stack slots and live threads exactly as before.

It also logs the longest turn and the highest thread stack peak.

### Thread diagnostics (`test=…`)

Each logs its details on serial and its result on the boot screen's message line, and ends with "everything returned" (free pages, stack slots and threads as before) and its longest turn. GRUB entries exist for the two meant for real hardware; the rest run in QEMU.

| `test=` | GRUB entry | Proves |
|---|---|---|
| `threads` | – | 8 threads × 100 yields: strict round robin (or option (A) after a preemption); `rbx`, `rbp`, `r12`–`r15` and stack contents preserved across 800 switches; IF=1 after every switch |
| `join` | – | exact exit codes (0, 1, 255, `0x7fffffff`, −1, `INT32_MIN`); a thread other than 0 blocks in a join and is woken; a detached thread reaped by thread 0 |
| `ifstate` | – | the IF state on every path: a new thread starts with IF=1; yield with IF=1 and with IF=0 each resume with their own IF; a preempted thread resumes through `iretq` with its own flags (AC set in one thread never leaks into another) |
| `preempt` | **14: Thread test: preemption** | 3 threads that never yield, for 300 ticks: each gets 25–42 % of the CPU, about one preemption per slice (40–80), all make progress, the timer keeps ticking, keys typed meanwhile all arrive |
| `preemptoff` | – | the deferred switch: no switch while the count is held (20 ticks), the request stays pending, the other thread runs within one tick of the release |
| `sleep` | – | `sleep(1)`, `sleep(10)`, `sleep(100)` wake after n or n+1 ticks; a sleeping thread uses no CPU; thread 0 sleeps by polling |
| `heapthreads` | – | 4 threads using the heap under preemption: every tagged payload intact, `heap_check` OK, free pages exactly accounted for |
| `framethreads` | – | 4 threads allocating frames (through the window) under preemption: no frame handed out twice, free pages restored exactly |
| `threadleak` | – | 2000 threads created, run and freed (joined and detached): heap, free pages, stack slots and threads exactly as before; a deliberately deep thread reports a stack peak between 8 and 16 KiB |
| `pingpongpreempt` | – | forces one preemption in the middle of a ping-pong: accepted only through the relaxed branch, the order differing only right after it |
| `pingpongbad` | – | a skipped turn and a repeated turn without a preemption are both rejected (`out of order without a preemption`, `wrong number of turns`) |
| `threadoverflow` | **15: Crash test: kernel thread stack overflow** | a thread recurses into its guard: double fault on IST1, the thread named, RSP inside its guard, no reset |
| `preemptyield`, `preemptjoin`, `preemptsleep` | – | yield, join or sleep with preemption disabled panics, naming the call, the count and the thread |

**Known limits (0.9):** one CPU; no priorities; a fixed 50 ms slice; at most 64 threads with 16 KiB stacks; kernel threads only; thread 0 never blocks; longer turns inside guarded sections (policy (a)).

## Device diagnostics (`kernel/devtest.c`) [built: 0.4]

| `test=` | GRUB entry | Proves | Result |
|---|---|---|---|
| `timer` | Timer test: measure against RTC | IRQ0 rate matches an independent clock | Counts ticks across 3 RTC second boundaries (busy-polling the RTC for precise edges). `PASS` if 270–330 (±10 %); the exact count is shown. |
| `keyboard` | Keyboard test: show key codes | Decoding of every key, extended codes, modifiers | Shows each event (scancode, key, character, flags) in the panel area and on serial. Never returns; reboot to exit. |
| `kbdflood` | (QEMU only) | Overflow is safe and counted | Stops reading for 3 s. At the end of the window it takes the queue length and the drop count, then drains exactly that many. Events arriving later are handled by the idle loop. |
| `irqunexpected` | (QEMU only) | Unexpected IRQs are survived | `int 34` (IRQ2's vector, no handler) must be counted and acknowledged, and must not panic |

## Drivers [built: minimal]

| Driver | File | Status |
|---|---|---|
| VGA text mode (80×25) | `drivers/vga/vga_text.c` | blink off (all 16 background colours usable); direct cell writes; palette programming (attribute controller + DAC); hardware cursor off at start-up, switched on and moved by the console (0.5). Scrolling is done by the console's own grid, not the driver. |
| Serial COM1 (38400 8N1, polled) | `drivers/serial/serial.c` | debug output and automated tests |
| PIT timer | `drivers/pit/pit.c` | channel 0, 100 Hz, IRQ0 (0.4) |
| PS/2 keyboard | `drivers/keyboard/ps2kbd.c`, `scancode.c` | i8042 port 1, set 1, IRQ1, US layout (0.4) |
| Mouse, storage, USB, network, audio | – | planned |
| Framebuffer / EFI GOP display | – | planned |

**Console:** `kernel/kprintf.c` has these output functions:

- `kputc`, `kputs`, `kprintf`: write to both drivers.
- `klogf`: writes to serial only, for diagnostics that shouldn't appear on screen.
- `ksnprintf`: formats into a buffer.

All of them share one formatter. `panicf` builds on it. Since 0.9 each `kputc`, `kputs`, `kprintf` and `klogf` call runs with preemption off, so lines from different threads never interleave (`ksnprintf` writes only to the caller's buffer and needs no guard). `kputc` also enforces the screen rule: if a thread other than 0 writes to the screen, it panics, naming the thread (the crash screen is exempt). Since 0.7 it also takes the `l` and `ll` length modifiers (`%llx`, `%lu`, …) for 64-bit values, and `%p` prints all 16 hex digits; it has its own unit test (`test_kprintf`).

## Display and branding [built: 0.2, layout updated 0.3, 0.4, 0.5 and 0.6; unchanged in 0.7, 0.8 and 0.9]

The boot screen (`kernel/bootscreen.c`) is laid out in rows:

| Rows | Content |
|---|---|
| 0–11 | logo (unchanged since 0.2) |
| 12 | title and version on one row (0.6; still two lines on serial) |
| 13–22 | two columns. Left (columns 2–43): 10 start-up checks (GDT, IDT, interrupt controller, exception handling, memory, display, paging & kernel heap, timer, keyboard, console & shell). Right (from column 44): 6 memory figures, then **Uptime** and **Last key**, which the idle loop keeps up to date. |
| 23 | message line (Diagnostics results; since 0.8 also the boot-module result when a module was loaded) |
| 24 | "System ready." plus what happens next, e.g. "Press any key to open the console." |

The screen and the serial log are written separately. The screen shows only the status words, and text is clipped at the row end, never wrapped. The serial log gets one line per item, including the detail notes (`remapped, IRQs masked`, `int3 self-test`, the self-test details for memory, timer, keyboard and console), which is what the tests match against. The keyboard diagnostic reuses the panel rows as a 10-line event list, with `>` marking the newest line.

`Paging & kernel heap:` is one screen line for two checks: `OK`, or `FAILED (paging)` / `FAILED (heap)` naming the part that failed (paging first, since the heap needs it). The serial log keeps them as two lines, `Paging enabled:` and `Kernel heap:`, each with its own status and note.

Once the console starts it owns the whole screen (see *Console and shell*). The boot results stay in the serial log.

**The boot screen and threads (0.9):** `bootscreen.c` is unchanged and has no protection of its own. Once other threads can exist, every call goes through `BOOTSCREEN_SAFE()` in `kernel.c` (see *Kernel threads and scheduler*). The scheduler self-test adds no screen line: its result is on the serial log only, and a failure ends on the crash screen.

**Logo (`kernel/logo.c`):** a geometric M made of four rounded strokes. It's defined as geometry, not stored as an image:

1. Each stroke is a line segment plus a radius, in ⅛-pixel whole-number units.
2. For each of the 36×24 logo pixels, the pixel is lit if its centre lies within the radius of the segment. This uses whole-number maths only (64-bit multiplies, no division in the distance test, no FPU).
3. Strokes are drawn back to front. The up-strokes cover the down-strokes, which creates the folded-ribbon look.
4. Colour is one of six gradient steps, chosen by horizontal position within the stroke.
5. Output uses half-block cells: each character cell is `▀` (CP437 `0xDF`), with the foreground colour as the top pixel and the background colour as the bottom pixel. That gives 80×50 addressable pixels in text mode.
6. The six gradient colours are custom RGB values loaded into VGA palette slots 1, 5, 6, 8, 9 and 13, which the rest of the kernel doesn't use.

The same stroke data can later be drawn with smooth edges on a graphics framebuffer. `scripts/logo-preview.py` is the independent reference model used by the tests.

## Hardware Abstraction Layer [planned]

This will be the only layer that touches port I/O, MMIO and CPU-specific instructions. Today, `kernel/arch/x86/` plays that role for the CPU, and the drivers include `io.h` directly. Keeping CPU-specific code under `kernel/arch/` also leaves room for other CPU families later (phones, likely ARM64, come after computers and laptops).

## Security [planned]

Users, permissions, application permissions, and a secure boot/update chain: the planned Madire Security Platform (listed in [PROGRESS.md](PROGRESS.md)). Designing these needs process isolation (user mode, planned for 0.10) first. The kernel already protects itself in small ways: page 0 unmapped, code read-only, data no-execute (0.7), stack guard pages (0.7), an empty lower half and no writable alias of kernel code in the physical view (0.8), and an unmapped 16 KiB guard below every kernel thread's stack (0.9).

## Filesystem [planned]

A virtual filesystem (VFS) layer first, then an initial RAM disk loaded by GRUB as a Multiboot module, then disk filesystems.

## System services [planned]

User-mode services (init, logging, device manager) once user mode and system calls exist (user processes are planned for 0.10, on top of the 0.9 threads and scheduler).

## User interface [planned]

A text console with a small built-in command shell exists since 0.5 (see *Console and shell*); since 0.9 `ps` lists the kernel threads. Command history, more commands and running programs (0.10) come with later milestones. The desktop environment, settings, application runtime, Madire Store and update system come much later. They depend on the framebuffer driver, input drivers, a filesystem and processes.
