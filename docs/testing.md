# Testing

## 1. Automated tests (`make test`)

These run in the Linux build environment, and `scripts/build.ps1` runs them on every build. `make test` runs the host unit tests first (`make unit`), then [`tests/qemu-tests.sh`](../tests/qemu-tests.sh).

### 1a. Host unit tests (`make unit`, `tests/unit/`)

The pure modules have no hardware access, printing or panics:

- memory: `mmap.c`, `pmm.c`, `reserve.c`
- paging and heap (0.6): `pte.h`, `vmm_core.c` (on fake physical frames), `heap.c`
- keyboard decoder and queue: `scancode.c`, `keyqueue.c`
- PIT divisor maths: `pit_divisor.h`
- console and shell: `textbuf.c`, `lineedit.c`, `parser.c`, `shell.c`, `cpuinfo.c`

They're compiled with the host GCC as ordinary 32-bit Linux programs (`-m32`, same warnings-as-errors options) and tested with synthetic data: cases real hardware can't produce on demand.

| Suite | Since | Cases |
|---|---|---|
| `test_mmap` (12) | 0.3 | QEMU- and VirtualBox-shaped maps; type classification (1/2/3/4/5/unknown); entry `size > 20` skipped correctly; `size < 20`, truncated entry, 64-bit overflow (including an end of exactly 2^64), empty map, null or wrapping buffer all rejected; zero-length entries skipped; unsorted and overlapping entries; 150 entries (no silent limit) |
| `test_pmm` (20) | 0.3 | first allocation = lowest free page; distinct allocations; exhaustion (then 0, repeatedly); free and reallocate; free after exhaustion; double free, reserved or kernel page, VGA page, page 0 and unaligned address all rejected; sub-page region and page-boundary crossing; reservations round outward; reserved wins over overlapping usable; phase enforcement; RAM above 4 GiB ignored or clipped (including near 2^64); no memory; a single usable page; multiple regions in order; the full 4 GiB space drained (1,048,320 pages) |
| `test_reserve` (9) | 0.3 | low 1 MiB, kernel, info block, map, command line; modules (table, range, name; empty module); invalid module range; table full; null or wrapping pointers; unterminated string; kernel and stack layout checks; end to end: every page from an exhausted allocator is checked against every reserved byte |
| `test_pit` (5) | 0.4 | 100 Hz → 11932, 1000 → 1193, 50 → 23864; extremes (1193182 Hz → 1, 19 Hz → 62799); out-of-range rejected (18 Hz, 0, above the base clock) with output untouched; rounding in both directions; every rate from 19 to 10,000 Hz within 0.5 % |
| `test_scancode` (17) | 0.4 | letter press and release; left, right and both Shifts; Caps Lock toggles once while held (auto-repeat ignored); Caps+Shift; shifted symbols; Ctrl/Alt flags; `E0` extended keys (Up, right Ctrl, keypad Enter, right Alt, Delete, Left), including the same byte without a prefix; fake shifts around Print Screen ignored; full Pause sequence → one event; unknown codes; stray release; Enter/Backspace/Tab/Space/Esc; F1–F12; prefix then release; keyboard replies are not keys; **all 256 bytes after every prefix state** decode safely; every key has a name |
| `test_keyqueue` (8) | 0.4 | empty pop; FIFO order; fill to 64; overflow drops the newest and counts it (the first 64 kept, in order); space frees after a pop; 1,000 uneven push/pop rounds with wrap-around; counters wrapping past 2^32; re-init |
| `test_textbuf` (9) | 0.5 | writing and position; newline; wrap at column 80 (an 80-character line plus newline gives no blank line); scrolling at the bottom; 1,000 scrolls; clear; Backspace (never past column 0); tab and non-printable characters; cursor clamping and direct cell writes |
| `test_lineedit` (12) | 0.5 | insert; Shift/Caps characters passed through; Backspace at start and in the middle; Delete; Left/Right/Home/End at the edges; insert at cursor; **exactly 71 characters accepted, the 72nd refused** (also mid-line); Enter and keypad Enter; releases ignored; Ctrl/Alt ignored; Up/Down/F-keys/Esc/modifiers ignored (no history); Tab inserted |
| `test_parser` (9) | 0.5 | one word; multiple spaces; leading/trailing spaces; tabs; empty and blank lines; exactly 8 words accepted, a 9th refused; a 71-character word; case preserved; nothing outside the line touched |
| `test_cpuinfo` (8) | 0.5 | CPUID unavailable; vendor strings; family/model/stepping with extended family and model (families 6, 0xF, 5); no leaf 1; brand string trimmed, absent, or exactly 48 characters; hypervisor vendor with NUL padding; feature list, "none" and safe truncation; non-printable bytes sanitised |
| `test_shell` (12) | 0.5 | **exact output** of `help`, `version`, `uptime` (0 s up to 2^32 − 1 s; ticks hidden once they no longer fit in 32 bits; timer unavailable), `mem` (MadireOS-Test figures), `cpu` (full, minimal, unavailable, paging on), `echo` (spaces, tabs, empty, 8 words); unknown commands and case-sensitivity; usage errors for all six fixed-argument commands; too many words; empty lines; `clear`; repeated commands give identical output |
| `test_pte` (5) | 0.6 | directory/table index split at the 4 MiB boundaries and `0xFFFFF000`; entry encode/decode (offset bits never leak into the frame); alignment helpers (`page_ceil` saturates); layout constants consistent; page 0 (and the BIOS data area in it) is the NULL guard |
| `test_vmm` (15) | 0.6 | on fake frames: map, translate, unmap (frame returned); tables created on demand, counted and cleared; `ensure_table`; out of frames → `NO_FRAME` with nothing half-done; unaligned; only the write flag accepted (never user pages); page 0 and the recursive slot refused; no mapping replaced (`EXISTS`); unmap of an unmapped page (`NOT_MAPPED`); **exactly one flush per unmap, for that address**; remap after unmap; read-only kept; 1,024 mappings across a 4 MiB boundary; directory entries kernel-only and writable; the recursive entry (pointing to the directory) never touched; error strings |
| `test_heap` (25) | 0.6 | init and bad parameters; basic alloc/free; **16-byte alignment for every size 1–4,096**; `alloc(0)`; 1,000 small blocks packed and intact; whole arena without growth; growth (page-sized, at least 16 KiB), refused growth, cap reached (never beyond); **repeated exhaustion → NULL, no corruption**; fragmentation then merging; merge with next, previous and both; split threshold (16 left → no split, 32 → split); **double free** (also after a merge); **invalid pointers** (outside, header, inside a block, misaligned); **corruption** (magic, check value, `prev_size`, size, payload overrun) caught by `heap_check`, `free` and `alloc`; `calloc` zeroes and refuses overflow; statistics consistent; **10,000 pseudo-random steps** with tagged payloads and overlap checks |
| `test_shell_mem06` (5) | 0.6 | `mem` without the 0.6 pointers is exactly the 0.5 output and a prefix of the extended one; exact paging and heap lines; paging-disabled and heap-disabled wording; singular `block`, a heap at the 16 MiB cap |

### 1b. QEMU tests (`tests/qemu-tests.sh`)

These boot the kernel headless in QEMU and check the serial output. The kernel copies its screen text to COM1 and logs details there. Each run writes `build/serial-*.log`.

Checks added in 0.4, 0.5 and 0.6 are tagged `[0.4]`, `[0.5]` and `[0.6]`, so the summary line separates them from the 0.3 regression checks:
`ALL TESTS PASSED (261 checks: 150 0.3 regression + 30 from 0.4 + 32 from 0.5 + 49 new in 0.6)`.

Existing checks updated in 0.6 (the count of existing checks is unchanged):

- the version checks: `v0.5` → `v0.6`;
- `[0.5] cpu reports CPUID information`: the mode line must now say `paging on` (only that);
- `exhaust 16M/128M: all N free pages allocated…`: N is now the `free_pages` figure from memory set-up minus the frames paging and the heap hold before the test. The kernel logs them (`frames in use before the test: T page tables + 1 directory + H heap pages = F`); the parts must add up, and the test's own count of pages left alone (taken from the allocator's bitmaps) must be exactly F, so an unaccounted page still fails.

| Group | How it boots | What it checks |
|---|---|---|
| Multiboot header | (not booted) | `grub-file --is-x86-multiboot` accepts the kernel |
| Normal boot | the real ISO, through GRUB (128 MB), and QMP types `x` after "System ready." | logo lit-pixel count = **293**; title; version `v0.6`; `GDT`, `IDT`, `Interrupt controller`, `Exception handling`, `Memory initialization`, `Display` all `OK`; `Memory self-test: PASS`; every memory panel line; no `PENDING`, `FAILED` or `PANIC`; memory model check. **[0.4]** Timer and keyboard self-tests `PASS` and status `OK`; `IRQ mask: 0xfffc`; `uptime: 5 s`; the typed key arrives (press and release); no unexpected IRQs or dropped keys. **[0.5]** Console self-test `PASS`, `Console & shell: OK`, `System ready. Press any key to open the console.`, and the typed `x` opens the console (`console: started`). |
| RAM sizes | `-kernel` at `-m 16M`, `32M`, `1G`, `4G` | boots; self-test passes; memory `OK`; panel shows the expected MB above 4 GiB; memory model check with pinned values |
| Boot module | `-m 32M -kernel … -initrd module.bin` | module range reserved; model check |
| Exhaustion | `-append "test=memexhaust"` at 16M and 128M | every free page allocated, verified and freed (through the physical-access window since 0.6; the frames held by paging and the heap are left alone and must match the kernel's count exactly); `System ready` afterwards; no panic. **[0.4]** ran with IRQ0/IRQ1 live. |
| Double free / free reserved | `test=doublefree`, `test=freereserved` | crash screen with the page address |
| Crash tests | `test=divzero`, `ud`, `gpf` | vector, name, error code (`0x28` for #GP), register dump with `CS=0x0008`, halted. **[0.4]** raised with IRQ0/IRQ1 live. |
| **[0.4] Timer rate** | `-m 32M -kernel … -append "test=timer"` | `Timer diagnostic: PASS`: 270–330 ticks across 3 RTC seconds. The measured value is printed (typically 293–300). |
| **[0.4] Keyboard, scripted** | `test=keyboard`, with `tests/qmp_keys.py` typing `a, Shift+B, Caps Lock, a, Caps Lock, Shift+1, Up, right Ctrl, Enter, keypad Enter` | every event decoded exactly, **in order**: scancode, key name, character and flags. For example `'B'` with `flags=0x03`, `'A'` with Caps (`0x11`), `'!'`, `UP` extended (`0x21`), `RCTRL` (`0x25`), `KP_ENTER` (`0x21`), and the extended release. Nothing dropped or unknown. |
| **[0.4] Queue overflow** | `test=kbdflood`, with QMP typing 50 keys (100 events) quickly | queue full (**64 kept**), overflow dropped and counted, **kept + dropped + late = 100** (no event lost unaccounted), drop count reported from normal context, kept events are the oldest in order, system ready afterwards |
| **[0.4] Unexpected IRQ** | `test=irqunexpected` | IRQ2 counted and acknowledged, reported from normal context, system ready, no panic |
| **[0.5] Console session** | `-kernel`, then `tests/qmp_keys.py --text` types a 32-line session: `x` (opens the console), then commands, edits and errors | first key opens the console and isn't typed; all 32 lines submitted; empty and blank lines give no output; `help` (7 lines, in order); exact `version`; `uptime` twice, advancing; `mem` figures; `cpu` lines (mode `paging on`); Shift and multiple spaces; tabs; Caps Lock (and case-sensitive names); Backspace; Left+Delete; Home/End/insert; Right/insert; Ctrl/Alt ignored; unknown command; usage error; more than 8 words refused; exactly 8 accepted; **71-character limit** (refusal logged, 66 `a`s echoed); 7 identical `version` outputs; scrolling; `clear`; no dropped keys, panic or unexpected IRQs |
| **[0.6] Paging and heap at boot** | the normal-boot log | `Paging walk-check: PASS`; page 0 left unmapped; paging self-test: TLB flush on remap, window map/access/unmap, **no page leak** (free pages equal before and after), `PASS`; CR0.PG and CR0.WP set and CR3 page-aligned (bits checked by the script); `Paging enabled: OK`; heap self-test no page leak and `PASS`; `Kernel heap: OK`; no `MISMATCH` anywhere |
| **[0.6] RAM sizes** | the 16M, 32M, 1G, 4G and module logs | `Paging enabled: OK` and `Kernel heap: OK` at every size |
| **[0.6] Page faults** | `test=pfnull`, `pfunmapped`, `pfreadonly` (32M) | `CPU exception 14: Page Fault (#PF)`; `CR2=0x00000000` / `0x40000000` / `0x00100000`; decoded `not present, read, kernel mode` / the same / `protection violation, write, kernel mode`; halted |
| **[0.6] Heap misuse** | `test=heapdouble`, `heapinvalid`, `heapcorrupt` (32M) | crash screen with `Heap: double free of 0xd000…`, `Heap: invalid free of 0x… (pointer not from the heap).`, `Heap: corrupted block at 0xd000… (while freeing 0xd000…)`; halted |
| **[0.6] Heap exhaustion** | `test=heapexhaust` (32M, so the 16 MiB cap is reachable) | `NULL` at the 16 MiB cap and still `NULL` afterwards; everything freed and `heap_check` OK; **free pages before − after = heap pages grown + page tables** (recomputed by the script); System ready; no panic |
| **[0.6] Console `mem`** | the console session | after the 0.5 lines, in order: `Paging: enabled (4 KiB pages, identity-mapped kernel)`, `Mapped pages`, `Page tables`, `Kernel heap: … KiB mapped, max 16384 KiB`, `Heap in use`, `Heap free` |

**How the console session is checked:** the console copies each submitted line to serial as `madire> <line>`, followed by the command's output lines, so the test reads exactly what was typed and printed. Only the final, edited line is logged, not the individual keystrokes (those still appear as 0.4 `key:` lines). Every key goes QMP → i8042 → IRQ1 → the unchanged 0.4 driver, decoder and queue → idle loop → console: the keyboard driver is never bypassed.

Every QEMU run uses `-no-reboot`. If the CPU triple-faults (an exception that can't be handled, which resets the CPU), QEMU exits, the expected text never appears, and the test fails. A crash can't pass by accident.

**Keyboard injection (`tests/qmp_keys.py`):** connects to QEMU's QMP socket using only the Python standard library. It waits until a "ready" line appears in the serial log, then sends each combination with `send-key` (QEMU sends a make and a break for each key, like a real keyboard).

- **Text mode (0.5):** `--text` types a string, using Shift for capitals and symbols; `{ret}`, `{bs}`, `{del}`, `{left}`, `{home}`, `{caps}`, `{ctrl+c}` and similar stand for special keys, and `{wait:N}` pauses.
- **Finishing (0.5):** QEMU queues `send-key` requests and delivers them at its own pace (each key is held for 30 ms), so the last keys can reach the guest many seconds after the script has sent them. `--done TEXT` makes the script wait until TEXT (the output of a final `echo end-of-console-test`) is in the log before QEMU is stopped.

**Why the flood test checks conservation, not a fixed drop count:** how many of the 100 events arrive inside the kernel's 3-second window depends on host timing (runs have given dropped = 35 with 1 late, and dropped = 36 with 0 late). The invariants that must always hold: the queue ends full (64), every other event is either counted as dropped or delivered afterwards, and nothing disappears.

**Timer timing: deterministic mode tried, fallback used.** The plan was to run the timer test under `-icount shift=0,sleep=off -rtc clock=vm`, so the PIT and the RTC would both follow QEMU's virtual clock and the count would be exactly repeatable. This was tested explicitly, 3 runs, and **it never reached a result within 30 s**. The diagnostic busy-polls the RTC for precise second boundaries, and under `-icount` each emulated port access costs far more real time than the virtual time it advances, so 4 virtual seconds would take minutes. The documented fallback is used instead: QEMU's normal clock with the kernel's ±10 % band. Host-clock runs measured 293–300 ticks.

**Memory model check (`tests/memory_check.py`):** reads the memory map and reservations the kernel logged, and recomputes usable, free and reserved pages, physical RAM and RAM above 4 GiB with separate per-page Python code. Every figure must equal the kernel's `memory:` summary line.

**Pinned values.** These come from the memory maps **QEMU 8.2.2** reported (inspected with GRUB 2.12 `lsmmap`). They're regression expectations for that environment only. The kernel never uses them: it always calculates from the map.

| `-m` | Usable pages | Physical RAM | Above 4 GiB |
|---|---|---|---|
| 16M | 3967 | 15 MB | 0 |
| 32M | 8063 | 31 MB | 0 |
| 128M (ISO) | 32639 | 127 MB | 0 |
| 1G | 262015 | 1023 MB | 0 |
| 4G | 786303 | 4095 MB | 1024 MB |

**Logo reference:** `python scripts/logo-preview.py preview.png` runs the same stroke data and whole-number maths as `kernel/logo.c`, independently. If you change the logo, update both files and `LOGO_PIXELS` in the test script.

## 2. VirtualBox tests (manual acceptance)

### One-time VM configuration: `MadireOS-Test`

| Setting | Value |
|---|---|
| Type | Other / Unknown |
| Firmware | **BIOS** (not EFI) |
| RAM | 4096 MB |
| CPUs | 2 |
| Video memory | 128 MB |
| Network | NAT |
| Storage | SATA port 0: 30 GB disk (unused for now); SATA port 1: optical drive |

Commands that were run (`VBoxManage` is in `C:\PFiles\Oracle\VirtualBox\`):

```
VBoxManage modifyvm MadireOS-Test --firmware bios --ostype Other
```

### Procedure

```powershell
.\scripts\build.ps1     # build + automated tests, produces build\madire-os.iso
.\scripts\run.ps1       # powers off / discards a saved state, attaches the ISO,
                        # sets boot order DVD -> disk, starts the VM
```

To do it by hand instead: VM **Settings > Storage**, select the optical drive, choose `build\madire-os.iso`, then **Start**.

**Close the VM (Power off) before running `build.ps1`.** A running VM keeps the ISO open, and copying the new ISO back then fails with `scp: open local "build/madire-os.iso": Broken pipe`. The build and tests still ran in the Codespace.

Keys can be typed into the VM from the host, through VirtualBox's PS/2 keyboard emulation (this is how the tests below were run):

- `VBoxManage controlvm MadireOS-Test keyboardputscancode 1e 9e` sends the `a` key;
- `VBoxManage controlvm MadireOS-Test keyboardputstring "help"` types text; Enter is `keyboardputscancode 1c 9c`.

**Wait about 3 seconds after power-on before sending keys.** Keys that arrive while the BIOS is still starting make it stop with `FATAL: Keyboard error:995`, before GRUB even loads.

### Test A: normal boot

The GRUB menu shows **Madire OS v0.6** and **Diagnostics**. After 3 seconds it boots the first entry:

![Madire OS v0.6 boot screen](images/boot-screen-v0.6.png)

- The M logo is drawn in the blue-to-cyan gradient; `M A D I R E   O S` and `Madire OS v0.6` share one row.
- Left column: **10 status lines, all `OK`**, including `Paging & kernel heap` (0.6), `Timer (PIT, 100 Hz)`, `Keyboard (PS/2)` and `Console & shell` (a self-test of the console code, not of hardware).
- Right column: memory figures, then **Uptime**, which counts up every second, and **Last key**, which shows the last key pressed (`none` until then). For `MadireOS-Test` (4096 MB):

  | Line | Value |
  |---|---|
  | Physical RAM detected | **4096 MB** |
  | Usable RAM (<4 GiB) | **3584 MB** |
  | RAM above 4 GiB | **512 MB** |
  | Page size | **4096 bytes** |
  | Usable pages | **917391** |
  | Free pages | **917140** (usable minus the low 1 MiB, the kernel and GRUB's boot data; changes when the kernel's size changes). This is taken before paging and the heap start; the console's `mem` then shows 48 fewer (**917092**: 3 page tables, the page directory and 44 heap pages). |

- Bottom row: `System ready. Press any key to open the console.`

### Test F: console and shell

Press any key on the boot screen. That key opens the console and **isn't typed**: the prompt is empty.

![Console](images/console-v0.5.png)

| Check | Expected |
|---|---|
| Console opens | Banner `M A D I R E   O S` / `Madire OS v0.6` / `Type 'help' …`, then `madire>` with a **blinking underline cursor** |
| `help`, `version`, `uptime` | the 7-command list; `Madire OS v0.6 (32-bit x86, Multiboot 1)`; `Uptime: HH:MM:SS (N ticks at 100 Hz)`, advancing |
| `mem`, `cpu` | 4096 / 3584 / 512 MB, 917391 usable pages, then (0.6) `Paging: enabled (4 KiB pages, identity-mapped kernel)`, `Mapped pages: 347`, `Page tables: 3`, `Kernel heap: 176 KiB mapped, max 16384 KiB`, `Heap in use: 0 KiB in 0 blocks`, `Heap free: 176 KiB, largest block 175 KiB`; CPUID vendor, brand (VirtualBox shows the host CPU, e.g. `13th Gen Intel(R) Core(TM) i5-1335U`), family/model, features, mode `32-bit protected mode, paging on, 1 CPU in use` |
| Editing | `echo hellx`, Backspace, `o` → `hello`; `echo abXc`, Left, Left, Delete → `abc`; `cho world`, Home, `e`, End, `!` → `world!`; Right and insert → `hello` |
| Shift, Caps Lock, Tab | `echo   Hello   World!` → `Hello World!`; Caps Lock, `echo abc` → `ECHO ABC` (unknown: names are lowercase); `echo`, Tab, `a`, Tab, Tab, `b` → `a b` |
| Errors (red) | `foo` → `Unknown command: foo. …`; `version x` → `Usage: version`; `echo 1 2 3 4 5 6 7 8` → `Too many arguments (max 8).` |
| Limits | `echo` + 100 × `a`: the line stops at 71 characters (66 `a`s echoed) |
| Empty lines | Enter alone, or spaces then Enter: just a new prompt |
| Scrolling, clear | several `help`s scroll the screen, with the prompt on the bottom row; `clear` leaves only the prompt on row 0 |

![mem with paging and heap (0.6)](images/console-mem-v0.6.png)

![mem and cpu (0.5)](images/console-mem-cpu-v0.5.png)

![Editing, errors and the 71-character limit](images/console-editing-v0.5.png)

### Test B: crash screen

In the GRUB menu, press **↓** to highlight **Diagnostics**, press **Enter**, then choose **Crash test: divide by zero**:

![Madire OS crash screen](images/crash-screen-v0.2.png)

- White on red, headed `*** MADIRE OS - KERNEL PANIC ***`.
- `CPU exception 0: Divide Error (#DE)`, `EAX=0x0000000a`, `ECX=0x00000000`, `CS=0x0008`, `DS=0x0010`.
- Since 0.4, EFLAGS has bit 9 set (e.g. `0x00210246`): the exception happened with hardware interrupts live.

The other two crash entries should show `Invalid Opcode (#UD)` and `General Protection Fault (#GP)` with error code `0x00000028`.

### Test C: memory diagnostics

| Entry | Expected |
|---|---|
| **Memory test: allocate every page** | after **about 90 seconds** on a 4 GB VM (each page goes through the physical-access window since 0.6), `Memory exhaustion test: PASS (917092 pages)` and `System ready.` That is the boot panel's free pages minus the 48 frames paging and the heap already hold. Afterwards the console's `mem` shows free pages back at 917092 and the heap figures unchanged. |
| **Memory test: double free** | crash screen: `Double free of physical page 0x…` |
| **Memory test: free a kernel page** | crash screen: `Attempt to free reserved page 0x00100000` |

![Memory exhaustion test in VirtualBox (0.3)](images/memexhaust-v0.3.png)

### Test D: timer diagnostic

**Diagnostics → Timer test: measure against RTC.** After about 4 seconds:
`Timer diagnostic: PASS (300 ticks in 3 RTC seconds, expected 270-330)`, then `System ready.`

![Timer diagnostic](images/timer-diag-v0.4.png)

### Test E: keyboard diagnostic

**Diagnostics → Keyboard test: show key codes.** Each key event appears in the panel area, with `>` marking the newest line. After 10 lines it wraps to the top. Reboot to exit. (This diagnostic never opens the console.)

![Keyboard diagnostic: Shift+A, Up, Caps Lock](images/keyboard-diag-v0.4.png)

Check: Shift+A gives `LSHIFT` and then `'A'` with `flags=0x03`; Up is `UP` with `flags=0x21` (extended); Caps Lock toggles (`0x11` on, `0x01` off), and `a` gives `'A'` while it's on.

### Test G: page-fault diagnostics (0.6)

**Diagnostics → Page-fault test: …**. Each ends on the crash screen with `CPU exception 14: Page Fault (#PF)` and a decoded line under the error code:

| Entry | Error code | Decoded line | CR2 |
|---|---|---|---|
| read from NULL | `0x00000000` | `Page fault: not present, read, kernel mode, address 0x00000000` | `0x00000000` |
| read from unmapped address | `0x00000000` | `Page fault: not present, read, kernel mode, address 0x40000000` | `0x40000000` |
| write to read-only kernel code | `0x00000003` | `Page fault: protection violation, write, kernel mode, address 0x00100000` | `0x00100000` |

![Page fault: write to read-only kernel code](images/pagefault-v0.6.png)

The four heap diagnostics (`heapdouble`, `heapinvalid`, `heapcorrupt`, `heapexhaust`) have no GRUB entries; they run in QEMU only (section 1b).

### Troubleshooting by layer

| Symptom | Layer | Check |
|---|---|---|
| "No bootable medium found" | ISO / VM config | ISO attached? Boot order has DVD first? |
| UEFI shell, or a blank screen before GRUB | VM config | Firmware must be BIOS |
| GRUB shows `error: no multiboot header found` | Kernel / linker | Multiboot test in `make test`; `.multiboot` must come first in `linker.ld` |
| GRUB `invalid arch-dependent ELF magic` | Toolchain | Kernel must be 32-bit (`file build/madire.kernel` should show ELF 32-bit) |
| Screen blank after GRUB | Kernel | Check `build/serial-boot.log` from `make test` |
| VM resets straight back to GRUB/BIOS | CPU tables | Triple fault. Run `make test`: whichever status line is missing from `build/serial-boot.log` shows the failing step. |
| A status line says `FAILED` | That subsystem | The serial log has the reason (`Timer self-test: FAIL (…)`, `Keyboard self-test: FAIL (…)`, …) |
| `Timer … FAILED (PIT counting but IRQ0 never delivered)` | IRQ0 / PIC | Mask or EOI problem: check `IRQ mask:` in the serial log |
| `Keyboard … FAILED (no keyboard answered)` | i8042 / VM | VM has no PS/2 keyboard (e.g. USB-only input); check the VM's input settings |
| Uptime stays at 0 | Timer | Timer status will also be `FAILED`; see above |
| Keys don't change "Last key" | Keyboard / IRQ1 | Keyboard status; `key:` lines in the serial log |
| Bottom row says `console disabled` | Keyboard or console self-test | The console is offered only if both `Keyboard (PS/2)` and `Console & shell` are `OK` |
| Console open but typing does nothing | Keyboard / IRQ1 | `key:` lines in the serial log; keys with Ctrl or Alt held are ignored by design |
| `FATAL: Keyboard error:995` before GRUB | VirtualBox BIOS | Keys were sent too soon after power-on; wait about 3 s |
| Crash screen: `PIC mask is 0x…, expected 0x…` | IRQ layer | An IRQ line other than 0/1 was unmasked: a kernel bug |
| Logo in wrong colours | VGA palette | `drivers/vga/vga_text.c` `vga_set_palette()` |
| Crash screen: `Memory: …` or `Memory self-test failed: …` | Memory | The message names the problem; see `docs/architecture.md` |
| `Paging & kernel heap: FAILED (paging)` | Paging | The walk-check found a mismatch (or the CPU has no CPUID): the `Paging check: … MISMATCH` line in the serial log names the address; paging stays off, the heap is disabled, boot continues |
| `Paging & kernel heap: FAILED (heap)` | Kernel heap | `Kernel heap self-test:` lines in the serial log; `kmalloc` returns `NULL` from then on |
| Crash screen: `Paging self-test failed` | Paging | Paging was on but the self-test failed; `Paging self-test: … MISMATCH` in the serial log |
| Crash screen: exception 14 outside the page-fault tests | A kernel bug | The `Page fault:` line gives the access type and address (CR2); EIP is the faulting instruction |
| Crash screen: `Heap: …` | Kernel heap misuse | Double free, invalid free or a corrupted block: a kernel bug; the message gives the pointer |
| Crash screen: `kmalloc: called with interrupts disabled` | Kernel heap | `kmalloc`/`kcalloc`/`kfree` called from an IRQ/exception handler or with interrupts off |
| Panel shows less usable RAM than the VM has | Expected | RAM above 4 GiB and firmware-reserved areas aren't usable by a 32-bit kernel |
| `build.ps1`: `open local "build/madire-os.iso": Broken pipe` | VM running | Power off `MadireOS-Test`, then build again |
| `run.ps1`: "state is Saved" | VM state | Use the current `run.ps1`, which discards saved states automatically (since commit `f07b517`) |
