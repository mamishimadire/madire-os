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
       8. memory_init              validate memory map → reservations →
                                   kernel-in-RAM check → page allocator
       9. memtest_boot             allocate / write / read / free self-test
      10. status + memory panel    "Memory initialization: OK" only now
      11. diagnostics if requested (test=… on the boot command line)
      12. "System ready."          CPU idles in a hlt loop
  any CPU exception → isr.asm stub → interrupt_dispatch → panic → crash screen
```

**Why memory comes after the CPU set-up (steps 4–7):** if reading bootloader data hits a bad address, the IDT turns the fault into the crash screen rather than a silent reset. Nothing allocates memory before step 8, so the bootloader's structures can't be overwritten before they're reserved.

| Stage | File | Responsibility |
|---|---|---|
| Bootloader config | `iso/boot/grub/grub.cfg` | Normal entry plus a Diagnostics submenu (crash and memory tests) |
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
  arch/x86/           everything specific to the x86 CPU
  memory/             memory map, reservations, page allocator, self-tests
drivers/              hardware drivers
  vga/  serial/
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
- **Policy in 0.2:**
  - Breakpoint (`int3`) is counted and execution resumes. The boot self-test relies on this.
  - Any other exception is fatal and goes to the crash screen.
  - IRQs are acknowledged and ignored. All are masked, so only spurious IRQ 7/15 could arrive.
- All gates are interrupt gates, so interrupts are disabled while a handler runs.

### Interrupt controller (`pic.c`)

The two 8259 PICs are moved from vectors 8–15 (which collide with CPU exceptions, e.g. 8 = Double Fault) to 32–47. Every IRQ is then masked, and `pic_init` confirms both masks read back as `0xFF`. Spurious IRQs 7 and 15 are detected through the in-service register and get no end-of-interrupt signal. Timer and keyboard interrupts are unmasked in milestone 0.4.

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

## Drivers [built: minimal]

| Driver | File | Status |
|---|---|---|
| VGA text mode (80×25) | `drivers/vga/vga_text.c` | cursor off; blink off (all 16 background colours usable); direct cell writes; palette programming (attribute controller + DAC); no scrolling yet |
| Serial COM1 (38400 8N1, polled) | `drivers/serial/serial.c` | debug output and automated tests |
| Keyboard, timer | – | planned (0.4) |
| Mouse, storage, USB, network, audio | – | planned |
| Framebuffer / EFI GOP display | – | planned |

**Console:** `kernel/kprintf.c` has these output functions:

- `kputc`, `kputs`, `kprintf`: write to both drivers.
- `klogf`: writes to serial only, for diagnostics that shouldn't appear on screen.
- `ksnprintf`: formats into a buffer.

All of them share one formatter. `panicf` builds on it.

## Display and branding [built: 0.2, layout updated 0.3]

The boot screen (`kernel/bootscreen.c`) is laid out in rows:

| Rows | Content |
|---|---|
| 0–11 | logo |
| 13–14 | title and version |
| 16–21 | two columns: start-up checks on the left, memory figures on the right |
| 22 | message line (Diagnostics results) |
| 23 | "System ready." |

The screen and the serial log are written separately. The screen shows only the status words. The serial log gets one line per item, including the detail notes (`remapped, IRQs masked`, `int3 self-test`, the self-test check count), which is what the tests match against.

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

Text console first (keyboard input plus a scrolling terminal, milestone 0.4). The desktop environment, settings, application runtime, Madire Store and update system come much later. They depend on the framebuffer driver, input drivers, a filesystem and processes.
