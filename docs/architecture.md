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
  -> kernel/kernel.c      kernel_main:
       1. serial_init, vga_init    drivers ready; cursor off, blink off
       2. check Multiboot magic    and info pointer (wrong → crash screen)
       3. boot screen header       logo (custom palette), title, version
       4. gdt_init                 own GDT; CS=0x08, data=0x10; read back
       5. idt_init                 256-entry IDT, 48 handlers; read back
       6. pic_init                 PIC moved to vectors 32-47, all IRQs masked
       7. sti + int3 self-test     exception path works and returns
                                   (sti is safe: every IRQ is still masked)
       8. memory_init              validate memory map → reservations →
                                   kernel-in-RAM check → page allocator
       9. memtest_boot             allocate / write / read / free self-test
      10. status + memory panel    "Memory initialization: OK" only now
      11. start_devices:
            timer_init             register IRQ0 handler → program PIT →
                                   unmask IRQ0 → verify mask
            timer_selftest         PIT counting? IRQ0 delivering ticks?
            keyboard_init          probe/configure i8042 → echo → register
                                   IRQ1 handler → clear pending → unmask IRQ1
            keyboard_selftest      echo reply through IRQ1, decoder, queue
            PIC mask check         must be exactly 0xFFFC (only IRQ0/IRQ1)
      12. console_selftest         pure console/shell code (not hardware)
      13. diagnostics if requested (test=… on the boot command line)
      14. "System ready. Press any key to open the console."
      15. idle loop                hlt → handle queued keys, uptime, counters;
                                   first key press → console; then keys → shell
  any CPU exception → isr.asm stub → interrupt_dispatch → panic → crash screen
  IRQ0/IRQ1 → isr.asm stub → interrupt_dispatch → irq_dispatch → driver handler → EOI
```

**Why memory comes after the CPU set-up (steps 4–7):** if reading bootloader data hits a bad address, the IDT turns the fault into the crash screen rather than a silent reset. Nothing allocates memory before step 8, so the bootloader's structures can't be overwritten before they're reserved.

**Why devices come after memory (step 11):** memory set-up and its self-test behave exactly as in 0.3 (IRQs masked). Each IRQ line is unmasked only once its handler is registered and its device configured. The timer comes first, so the keyboard self-test can use it for bounded waits.

| Stage | File | Responsibility |
|---|---|---|
| Bootloader config | `iso/boot/grub/grub.cfg` | Normal entry plus a Diagnostics submenu (crash, memory, timer and keyboard tests) |
| Multiboot header | `boot/boot.asm` (`.multiboot`) | Magic `0x1BADB002`, flags, checksum; must be within the first 8 KiB of the file |
| Entry point | `boot/boot.asm` (`_start`) | Stack setup; passes `eax` (magic) and `ebx` (info pointer) to C |
| Memory layout | `linker/linker.ld` | Kernel linked at 1 MiB; `.text`, `.rodata`, `.data`, `.bss` 4 KiB-aligned; exports `__kernel_start/end` and per-section start/end symbols |
| Start-up sequence | `kernel/kernel.c` | Calls each subsystem in order and reports each result on the boot screen |

**Why GRUB/Multiboot:** GRUB already handles BIOS disk access, reading the ISO filesystem and switching the CPU into protected mode. That lets the project start with kernel code instead of a hand-written boot sector. A custom bootloader could replace it later without changing the kernel's entry contract.

**Why 32-bit first:** Multiboot (version 1) hands over in 32-bit protected mode. Moving to 64-bit long mode means setting up paging first, so it gets its own milestone.

## Source layout

```
boot/                 entry point (assembly)
kernel/               architecture-independent kernel code
  arch/x86/           everything specific to the x86 CPU (incl. IRQ layer, CMOS)
  memory/             memory map, reservations, page allocator, self-tests
  input/              key-event queue
  timer.c             system tick, uptime, bounded waits
  devtest.c           timer / keyboard / IRQ diagnostics
  console/            console: text grid, line editor, screen + serial output
  shell/              command parser, shell commands, CPUID decoding
drivers/              hardware drivers
  vga/  serial/  pit/  keyboard/
```

`kernel/memory/` doesn't depend on the display or the logo. The boot screen only displays the `struct memory_stats` that `kernel.c` hands it.

Code under `kernel/` (outside `arch/`) and `drivers/` doesn't use x86 instructions directly. Port I/O goes through `kernel/arch/x86/io.h`. That file is where a Hardware Abstraction Layer (HAL) boundary will go.

## CPU [built: 0.2]

### GDT (`kernel/arch/x86/gdt.c`, `gdt_flush.asm`)

The GDT is the table of memory segments. GRUB's is replaced, because the Multiboot spec says not to rely on it.

| Selector | Segment | Base / limit | Access |
|---|---|---|---|
| `0x00` | null | – | – |
| `0x08` | kernel code | 0 / 4 GiB | ring 0, execute/read |
| `0x10` | kernel data | 0 / 4 GiB | ring 0, read/write |

The segments are flat: segmentation does no translation, and memory protection will come from paging. `gdt_flush` loads the table, reloads the data segment registers, and far-jumps to reload `CS`. `gdt_init` then reads back `sgdt`, `CS`, `DS` and `SS` to confirm the CPU is using the new values. User-mode segments and the task-state segment (TSS) come with user mode.

### IDT and interrupts (`idt.c`, `isr.asm`, `interrupts.c`)

- **IDT:** 256 entries. Vectors 0–31 (CPU exceptions) and 32–47 (hardware IRQs) point at stubs; 48–255 are marked not-present, and reaching one raises a #GP, which is reported.
- **Entry path:** each stub pushes a dummy error code where the CPU doesn't push one, then its vector number, then jumps to `isr_common`. `isr_common` saves all registers (`pusha` plus `ds/es/fs/gs`), loads the kernel data segment and calls `interrupt_dispatch(struct interrupt_frame *)`. On return it restores everything and executes `iret`.
- **Policy:**
  - Breakpoint (`int3`) is counted and execution resumes. The boot self-test relies on this.
  - Any other exception (vectors 0–31) is fatal and goes to the crash screen. This behaviour is unchanged since 0.2.
  - Vectors 32–47 (hardware IRQs) go to `irq_dispatch` (below).
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

- `panic_exception(frame)` shows the vector, name, mnemonic, error code, `EIP/CS/EFLAGS`, general registers, the interrupted `ESP`, segment registers and `CR0/CR2/CR3`, white on red.
- `panic(message)` and `panicf(format, ...)` show a text message instead, for fatal start-up errors and memory misuse (e.g. `Double free of physical page 0x0014c000`).
- Both copy everything to serial, then halt with `cli; hlt`.

**Known limit:** a fault on a corrupted kernel stack can't reach the handler, and the CPU triple-faults and resets. Handling that needs a separate stack for double faults (a TSS task gate), which comes with the TSS.

## Memory [built: 0.3]

Physical memory management. Paging is still off, so a physical address is also a pointer. Every figure is calculated at runtime from the bootloader's memory map; nothing about the machine is hard-coded.

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
- **32-bit, no PAE:** only physical addresses below 4 GiB are used.
- **Paging is off:** Multiboot addresses can be read directly, and the self-tests write to pages through their physical addresses. This must change when paging arrives (0.5).

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
| Boot stack | inside `.bss`. `stack_bottom` and `stack_top` are exported from `boot.asm`, and start-up checks that they lie inside `__bss_start–__bss_end`. |
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
| RAM above 4 GiB | RAM a 32-bit kernel without PAE can't address. It's reported separately and never counted as usable. |
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

- `test=memexhaust`: allocate every free page, then check each is safe, strictly increasing and tagged. After that, allocation must keep returning 0. Each page's tag is verified, every page is freed, and the free count must be restored.
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

The memory subsystem is unchanged in 0.4. Its tests now also run with IRQ0 and IRQ1 live.

## Timer [built: 0.4]

| Item | Design |
|---|---|
| Hardware | PIT channel 0 → IRQ0 (vector 32); `drivers/pit/pit.c` |
| Mode | Mode 2 (rate generator), low byte then high byte, binary: command `0x34` |
| Rate | **100 Hz**. Divisor `round(1193182 / 100)` = **11932** (`pit_divisor_for()`, unit-tested), giving 99.998 Hz. |
| Ticks | 64-bit counter, incremented in the IRQ0 handler. It's read with interrupts briefly off, because it's two 32-bit loads on i386. It can't wrap in practice (5.8 billion years). |
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

After "System ready.", `kernel.c` runs the idle loop. It isn't a scheduler: it sleeps with `hlt` until any interrupt, then handles whatever the interrupt handlers queued or counted:

- **Key events** are logged to serial, as in 0.4.
  - While the boot screen shows, a pressed key updates "Last key" and **opens the console** (if it was offered). That key isn't typed.
  - Once the console is active, every key event goes to `console_handle_key()`.
- **Counters:** new drops and unexpected IRQs are reported to serial.
- **Uptime:** when the seconds counter changes, "Uptime" is redrawn while the boot screen shows, and every 5 s `uptime: N s` goes to serial.

All screen and serial output happens here, never in interrupt context. The shell runs here too, one command at a time. A key arriving just before `hlt` waits at most one timer tick (10 ms).

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
| `kernel/shell/shell.c` | yes | The seven commands, reading values from a read-only `shell_context` and writing through a `shell_output` sink, so every output is unit-tested exactly |
| `kernel/shell/cpuinfo.c` | yes | Decodes raw CPUID values: vendor, family/model/stepping (extended-field rules), feature names, brand string, hypervisor vendor |
| `kernel/arch/x86/cpuid.c` | no | Checks CPUID exists by toggling EFLAGS bit 21, then runs only the leaves the CPU lists; reads CR0 |
| `kernel/console/console.c` | no | Owns the grid and draws it to VGA (`vga_put_cell`); moves the hardware cursor; copies submitted lines (`madire> …`) and output to serial; builds the shell context; runs the start-up self-test |

- **Screen:** the console takes over the whole 80×25 screen when it starts. It shows a banner (`M A D I R E   O S`, `Madire OS v0.5`, a hint) and the `madire> ` prompt.
  - Colours use only VGA slots the logo doesn't reprogram: grey text, white input, cyan prompt, red errors.
  - Output scrolls up at the bottom. There's no scrollback.
  - `clear` empties the console's own grid and redraws it; it doesn't use `vga_clear()`.
- **Cursor:** the VGA hardware cursor, a blinking underline, marks the edit position. The VGA driver gained two additive functions: `vga_cursor_show()` and `vga_cursor_move()`.
- **Commands:**

  | Command | Output |
  |---|---|
  | `help` | the command list |
  | `clear` | clears the screen |
  | `version` | `Madire OS v0.5 (32-bit x86, Multiboot 1)` |
  | `uptime` | `Uptime: HH:MM:SS (N ticks at 100 Hz)`. Uses the 32-bit seconds counter with 32-bit division only. The tick count is shown only while it fits in 32 bits, and is never divided. |
  | `mem` | the 0.3 boot figures, plus the live `pmm_free_pages()` |
  | `cpu` | CPUID information, plus the CR0 mode line |
  | `echo <text>` | the words joined by single spaces |

  An unknown name gives `Unknown command: …`. Extra arguments give `Usage: …`, and more than 8 words gives `Too many arguments (max 8).`
- **Self-test (`Console & shell` status line):** exercises the *pure* modules with known input: the line editor, the parser, `echo ok` through the shell, and scrolling in a private grid. It isn't a test of keyboard or VGA hardware; those are proven by the keyboard self-test and by the QEMU and VirtualBox console tests.
- **When the console is offered:** only if both the console self-test and the keyboard passed. Otherwise the bottom row says why, and the idle loop runs as in 0.4.
- **Unchanged in 0.5:** the keyboard driver, decoder, queue, IRQ layer, PIC, PIT, timer and memory manager. There's no dynamic allocation: every buffer is static.

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

All of them share one formatter. `panicf` builds on it.

## Display and branding [built: 0.2, layout updated 0.3, 0.4 and 0.5]

The boot screen (`kernel/bootscreen.c`) is laid out in rows:

| Rows | Content |
|---|---|
| 0–11 | logo (unchanged since 0.2) |
| 12–13 | title and version |
| 14–22 | two columns. Left: 9 start-up checks (GDT, IDT, interrupt controller, exception handling, memory, display, timer, keyboard, console & shell). Right: 6 memory figures, then **Uptime** and **Last key**, which the idle loop keeps up to date. |
| 23 | message line (Diagnostics results) |
| 24 | "System ready." plus what happens next, e.g. "Press any key to open the console." |

The screen and the serial log are written separately. The screen shows only the status words, and text is clipped at the row end, never wrapped. The serial log gets one line per item, including the detail notes (`remapped, IRQs masked`, `int3 self-test`, the self-test details for memory, timer, keyboard and console), which is what the tests match against. The keyboard diagnostic reuses the panel rows as a 9-line event list, with `>` marking the newest line.

Once the console starts it owns the whole screen (see *Console and shell*). The boot results stay in the serial log.

**Logo (`kernel/logo.c`):** a geometric M made of four rounded strokes. It's defined as geometry, not stored as an image:

1. Each stroke is a line segment plus a radius, in ⅛-pixel whole-number units.
2. For each of the 36×24 logo pixels, the pixel is lit if its centre lies within the radius of the segment. This uses whole-number maths only (64-bit multiplies, no division in the distance test, no FPU).
3. Strokes are drawn back to front. The up-strokes cover the down-strokes, which creates the folded-ribbon look.
4. Colour is one of six gradient steps, chosen by horizontal position within the stroke.
5. Output uses half-block cells: each character cell is `▀` (CP437 `0xDF`), with the foreground colour as the top pixel and the background colour as the bottom pixel. That gives 80×50 addressable pixels in text mode.
6. The six gradient colours are custom RGB values loaded into VGA palette slots 1, 5, 6, 8, 9 and 13, which the rest of the kernel doesn't use.

The same stroke data can later be drawn with smooth edges on a graphics framebuffer. `scripts/logo-preview.py` is the independent reference model used by the tests.

## Hardware Abstraction Layer [planned]

This will be the only layer that touches port I/O, MMIO and CPU-specific instructions. Today, `kernel/arch/x86/` plays that role for the CPU, and the drivers include `io.h` directly.

## Security [planned]

Users, permissions, application permissions, and a secure boot/update chain. Designing these needs process isolation (user mode and paging) first.

## Filesystem [planned]

A virtual filesystem (VFS) layer first, then an initial RAM disk loaded by GRUB as a Multiboot module, then disk filesystems.

## System services [planned]

User-mode services (init, logging, device manager) once user mode and system calls exist.

## User interface [planned]

A text console with a small built-in command shell exists since 0.5 (see *Console and shell*). Command history, more commands and running programs come with later milestones. The desktop environment, settings, application runtime, Madire Store and update system come much later. They depend on the framebuffer driver, input drivers, a filesystem and processes.
