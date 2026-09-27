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
       2. check Multiboot magic    (wrong → crash screen)
       3. boot screen header       logo (custom palette), title, version
       4. gdt_init                 own GDT; CS=0x08, data=0x10; read back
       5. idt_init                 256-entry IDT, 48 handlers; read back
       6. pic_init                 PIC moved to vectors 32-47, all IRQs masked
       7. sti + int3 self-test     exception path works and returns
       8. crash test if requested  (test=… on the boot command line)
       9. "System ready."          CPU idles in a hlt loop
  any CPU exception → isr.asm stub → interrupt_dispatch → panic → crash screen
```

| Stage | File | Responsibility |
|---|---|---|
| Bootloader config | `iso/boot/grub/grub.cfg` | Normal entry plus a Diagnostics submenu (crash tests) |
| Multiboot header | `boot/boot.asm` (`.multiboot`) | Magic `0x1BADB002`, flags, checksum; must be within the first 8 KiB of the file |
| Entry point | `boot/boot.asm` (`_start`) | Stack setup; passes `eax` (magic) and `ebx` (info pointer) to C |
| Memory layout | `linker/linker.ld` | Kernel linked at 1 MiB; `.text`, `.rodata`, `.data`, `.bss` 4 KiB-aligned |
| Start-up sequence | `kernel/kernel.c` | Calls each subsystem in order and reports each result on the boot screen |

**Why GRUB/Multiboot:** GRUB already handles BIOS disk access, reading the ISO filesystem and switching the CPU into protected mode. That lets the project start with kernel code instead of a hand-written boot sector. A custom bootloader could replace it later without changing the kernel's entry contract.

**Why 32-bit first:** Multiboot (version 1) hands over in 32-bit protected mode. Moving to 64-bit long mode means setting up paging first, so it gets its own milestone.

## Source layout

```
boot/                 entry point (assembly)
kernel/               architecture-independent kernel code
  arch/x86/           everything specific to the x86 CPU
drivers/              hardware drivers
  vga/  serial/
```

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
- `panic(message)` shows a text message instead, for fatal start-up errors.
- Both copy everything to serial, then halt with `cli; hlt`.

**Known limit:** a fault on a corrupted kernel stack can't reach the handler, and the CPU triple-faults and resets. Handling that needs a separate stack for double faults (a TSS task gate), which comes with the TSS.

## Memory [planned: 0.3]

The kernel still uses only its own image at 1 MiB, a 16 KiB static stack, and the VGA buffer at `0xB8000`. The Multiboot information is read only for the command line. Milestone 0.3 will parse the memory map and build a physical frame allocator. The boot screen reports this honestly as `PENDING`.

## Drivers [built: minimal]

| Driver | File | Status |
|---|---|---|
| VGA text mode (80×25) | `drivers/vga/vga_text.c` | cursor off; blink off (all 16 background colours usable); direct cell writes; palette programming (attribute controller + DAC); no scrolling yet |
| Serial COM1 (38400 8N1, polled) | `drivers/serial/serial.c` | debug output and automated tests |
| Keyboard, timer | – | planned (0.4) |
| Mouse, storage, USB, network, audio | – | planned |
| Framebuffer / EFI GOP display | – | planned |

**Console:** `kernel/kprintf.c` (`kputc`, `kputs`, `kprintf`) writes every character to both drivers.

## Display and branding [built: 0.2]

The boot screen (`kernel/bootscreen.c`) has the logo on rows 0–11, the title and version on rows 13–14, the start-up status list on rows 16–21, and "System ready." on row 23.

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
