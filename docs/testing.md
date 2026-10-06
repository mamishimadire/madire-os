# Testing

## 1. Automated tests (`make test`)

These run in the Linux build environment, and `scripts/build.ps1` runs them on every build. `make test` runs the host unit tests first (`make unit`), then [`tests/qemu-tests.sh`](../tests/qemu-tests.sh).

### 1a. Host unit tests (`make unit`, `tests/unit/`)

The pure modules have no hardware access, printing or panics:

- memory: `mmap.c`, `pmm.c`, `reserve.c`
- paging and heap (0.6; 4-level since 0.7): `pte.h`, `vmm_core.c` (on fake physical frames), `heap.c`
- keyboard decoder and queue: `scancode.c`, `keyqueue.c`
- PIT divisor maths: `pit_divisor.h`
- console and shell: `textbuf.c`, `lineedit.c`, `parser.c`, `shell.c`, `cpuinfo.c`
- 0.7: the boot-module check `bootmod.c`; the NX decision in `cpuid.h` and the page-fault decoding in `pagefault.h`; the formatter in `kprintf.c`
- 0.8: the stack high-water mark `stackpeak.c`
- 0.9: the scheduler core `sched_core.c` (threads, run queue, slices, sleep, join, stack slots, the turn-order check), and the shell's `ps`

They're compiled with the host GCC as ordinary 64-bit Linux programs (`-m64 -no-pie`, same warnings-as-errors options), linked at 256 MiB so their static test data lies below 4 GiB (the memory modules take 32-bit physical addresses), and tested with synthetic data: cases real hardware can't produce on demand. Known limitation: `test_reserve` also relies on that data lying above its fake 32 MiB machine, so it depends on the test binary's load address.

| Suite | Since | Cases |
|---|---|---|
| `test_mmap` (14) | 0.3, view API in 0.8 | QEMU- and VirtualBox-shaped maps; type classification (1/2/3/4/5/unknown); entry `size > 20` skipped correctly; `size < 20`, truncated entry, 64-bit overflow (including an end of exactly 2^64), empty map, null or wrapping buffer all rejected; zero-length entries skipped; unsorted and overlapping entries; 150 entries (no silent limit). 0.8: the physical address that is validated and the view the bytes are read through are separate: a map validated at a fake physical address (`0x9000`) and read through the test's buffer, with error offsets relative to the buffer; a null or wrapping physical address is rejected even with a valid view, and a missing view is rejected too |
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
| `test_pte` (7) | 0.6, 4-level in 0.7 | index split at every level (PML4, PDPT, PD, PT); canonical and non-canonical addresses, sign extension; entry encode/decode including the NX bit (offset bits never leak into the frame); alignment helpers; the 64-bit layout constants consistent (slots 508–511); page 0 (and the BIOS data area in it) is the NULL guard. 0.8: the higher-half helpers: the kernel offset is PML4 slot 511 (PDPT slot 510) and canonical up to 1 GiB of physical addresses; `kernel_phys()` gives the physical address (`0xFFFFFFFF80100000` → `0x100000`); the physical view is slot 256, `phys_view(0xB8000)` = `0xFFFF8000000B8000`, all of 0–4 GiB stays inside slot 256, and slot 256 is apart from every other area |
| `test_vmm` (17) | 0.6, 4-level in 0.7 | on fake frames: map, translate, unmap (frame returned); PDPT, PD and PT created on demand per level, counted and cleared; `ensure_table`; out of frames **at each level** → `NO_FRAME` with nothing half-done; unaligned addresses and frames, and only the write and NX flags accepted (never user pages); page 0, the recursive slot and non-canonical addresses refused; no mapping replaced (`EXISTS`); unmap of an unmapped page (`NOT_MAPPED`); **exactly one flush per unmap, for that address**; remap after unmap; leaf flags and intermediate entries; many mappings across table boundaries; upper-half addresses; a 2 MiB page translated, and never replaced or split; the recursive entry never touched; recursive-table addresses; error strings |
| `test_heap` (25) | 0.6 | init and bad parameters; basic alloc/free; **16-byte alignment for every size 1–4,096**; `alloc(0)`; 1,000 small blocks packed and intact; whole arena without growth; growth (page-sized, at least 16 KiB), refused growth, cap reached (never beyond); **repeated exhaustion → NULL, no corruption**; fragmentation then merging; merge with next, previous and both; split threshold (16 left → no split, 32 → split); **double free** (also after a merge); **invalid pointers** (outside, header, inside a block, misaligned); **corruption** (magic, check value, `prev_size`, size, payload overrun) caught by `heap_check`, `free` and `alloc`; `calloc` zeroes and refuses overflow; statistics consistent; **10,000 pseudo-random steps** with tagged payloads and overlap checks |
| `test_shell_mem06` (5) | 0.6 | `mem` without the 0.6 pointers is exactly the 0.5 output and a prefix of the extended one; exact paging and heap lines (0.8: `enabled (4-level, 4 KiB, higher-half, NX on/off)`, `identity-mapped` in 0.7; and the `NX:` row: supported/enabled, supported/not enabled, not supported); paging-disabled and heap-disabled wording; singular `block`, a heap at the 16 MiB cap |
| `test_bootmod` (5) | 0.7 | FNV-1a against published test vectors; an intact test module; every single changed byte detected; truncated modules and modules without the header; status strings |
| `test_kprintf` (7) | 0.7 | the existing formats unchanged; `%llx`/`%llu`/`%lld` and `%lx`/`%lu` up to the full 64-bit range; `%p` at 16 digits; every power of ten; truncation to the buffer size. 0.9: every output call leaves the preemption-disable count as it found it, also when nested inside a guarded section (`ksnprintf` doesn't touch it) |
| `test_nx` (3) | 0.7 | NX capability decision from CPUID (no extended leaf, bit clear, bit set); the NX page-table flag (bit 63, or 0 without NX); page-fault error-code decoding, including the NX-violation case |
| `test_shell_cpu07` (8) | 0.7 | `cpu` mode line `64-bit long mode` only when EFER.LMA is set (LME alone is not enough; no EFER keeps the 32-bit wording); `version` says `64-bit x86-64` exactly when `cpu` says long mode; `version x` still gives its usage |
| `test_stackpeak` (5) | 0.8 | a freshly painted 16 KiB stack reports 0 and every byte holds the pattern; used 100 bytes deep, then 5000 (the peak grows), then 40 (the peak stays at 5000); a fully used stack, also when only the bottom byte changed; written bytes equal to the pattern at the deepest point make it read 2 bytes low, never high; length 0 and 1, an unaligned start and length, bytes outside the range ignored |
| `test_shell_mem08` (4) | 0.8 | without the stack fields `mem` has no stack row (the 0.5/0.6 output unchanged); the exact row `Kernel stack:            5120 bytes at peak, of 16384`, and it is the last line; peaks of 0, 16384 and 123456 (wider than the column) formatted exactly; every `mem` line at most 79 characters |
| `test_sched` (17) | 0.9 | initial state (thread 0 running, nothing else); allocation up to the 64-thread limit and the lowest free tid; round-robin order; yield alone and with others; the slice: `need_resched` only after 5 ticks **and** only when another thread is ready; preemption disabled blocks every switch and nests; yield, sleep and join refused with preemption off, changing nothing; sleepers wake at exactly their tick, in tick order, at the tail of the queue, also across a tick-counter wrap; thread 0 never blocks or exits (it gets "poll" instead); join on a zombie and on a live thread, with the refusals (self, detached, second joiner); exit codes including −1, `INT32_MIN` and `INT32_MAX`; detached threads reaped by thread 0; the full table filled and emptied repeatedly; **the turn-order check** (strict order, also when preemptions didn't change it; relaxed only when every break comes right after a preemption, including a shifted rotation of three threads and a first turn after a preemption; a break without a preemption, a repeated or skipped turn, too few turns and an unknown thread all rejected); every state and result string |
| `test_kstack` (4) | 0.9 | stack slots: lowest free first, all 64, then exhaustion, freeing out of range or twice refused; the sizes (16 KiB stack, 16 KiB guard, 32 KiB slot, 2 MiB area); the layout: every slot inside PML4 slot 507, canonical and page-aligned, the guard directly below its stack, no gap and no overlap, one page table for the whole area, apart from slots 508–511; looking up the slot from an address in a guard or a stack (and nothing for addresses outside) |
| `test_shell_ps09` (5) | 0.9 | without the thread fields every command and `help` is exactly as before and `ps` is unknown; with them `help` lists `ps` last; the exact `ps` output (header, rows, `1 thread` / `N threads`); edge cases (tid 63, a 10-digit tick count, a 16384/16384 peak, a 15-character name, a tick count beyond 32 bits shown as `large`, an empty table, `ps x` → `Usage: ps`); every line at most 79 characters |

`test_pte` is unchanged in 0.9: the slot-507 layout cases that the plan put there are in `test_kstack`, next to the stack-slot code they check.

### 1b. QEMU tests (`tests/qemu-tests.sh`)

These boot the kernel headless in QEMU and check the serial output. The kernel copies its screen text to COM1 and logs details there. Each run writes `build/serial-*.log`.

Checks added in 0.4 to 0.9 are tagged `[0.4]` to `[0.9]`, so the summary line separates them from the 0.3 regression checks:
`ALL TESTS PASSED (528 checks: 150 0.3 regression + 30 from 0.4 + 32 from 0.5 + 49 from 0.6 + 96 from 0.7 + 32 from 0.8 + 139 new in 0.9)`.

`make test` also runs `make repro` before the QEMU tests; its result becomes 3 of the `[0.8]` checks. The whole suite takes about 15 minutes; run it detached on a hash-verified snapshot (see [development.md](development.md)).

Existing checks updated in 0.9: only the version checks (4), `v0.8` → `v0.9`. The count of existing checks is unchanged and none was weakened. The extra boot-time page tables 0.9 adds needed no change either: the exhaustion checks recompute their page counts from the log, and the walk-check and `mem` checks accept any count.

Existing checks updated in 0.8 (the count of existing checks is unchanged; each still proves the same fact, at the new address or in the new wording):

- the version checks (4): `v0.7` → `v0.8`;
- `[0.6] pfreadonly`: CR2 and the address are `0xffffffff80100000` (still a write to read-only kernel code, now at its high address);
- `[0.6] mem` and 3 rows of `test_shell_mem06`: the paging line reads `enabled (4-level, 4 KiB, higher-half, NX on|off)`;
- `[0.7] kernel runs at its identity address` became `… at its higher-half address`: RIP inside kernel code, which starts at `0xffffffff80100000`;
- the walk-check wording in 12 `[0.7]` regexes (code, stubs, handlers, rodata, data/bss with its 2 guard holes, IST1, RSP, the stack sizes, the no-NX run): `expected identity …` → `expected kernel …`, with the same range and access checked.

Existing checks updated in 0.7 (the count of existing checks is unchanged; each still proves the same thing):

- every boot uses `qemu-system-x86_64` instead of `qemu-system-i386`;
- the version checks (4): `v0.6` → `v0.7`, and `version` prints `Madire OS v0.7 (64-bit x86-64, Multiboot 1)` (the `[0.5] version` check and the count of 7 identical outputs);
- the crash tests' register dump: `EIP=0x…` (8 digits) → `RIP=0x…` (16 digits), still with `CS=0x0008`;
- `[0.5] cpu reports CPUID information`: the mode line must say `64-bit long mode, paging on, 1 CPU in use`;
- `[0.6] CR0.PG, CR0.WP, CR4.PAE and EFER.LMA set, CR3 holds a page-aligned PML4`: CR4 and EFER added, CR3 printed 16 digits wide;
- `[0.6]` page faults: CR2 and the address are 16 digits; the decoded line and the address are now two lines (`Page fault: …`, then `Address: 0x…`), checked in order;
- `[0.6]` heap misuse: heap addresses start with `0xfffffe80000`, other pointers are 16 digits, and the corrupted-block message says `(freeing 0x…)`;
- `[0.6] mem`: the paging line is `enabled (4-level, 4 KiB, identity-mapped, NX on|off)`;
- the boot-module test (`-initrd`) now loads a 0.7 **test module** (a header with an FNV-1a checksum, then 14,000 random bytes) instead of plain random bytes, so its contents can also be checked.

Existing checks updated in 0.6:

- the version checks: `v0.5` → `v0.6`;
- `[0.5] cpu reports CPUID information`: the mode line must now say `paging on` (only that);
- `exhaust 16M/128M: all N free pages allocated…`: N is now the `free_pages` figure from memory set-up minus the frames paging and the heap hold before the test. The kernel logs them (`frames in use before the test: T page tables + 1 directory + H heap pages = F`); the parts must add up, and the test's own count of pages left alone (taken from the allocator's bitmaps) must be exactly F, so an unaccounted page still fails.

| Group | How it boots | What it checks |
|---|---|---|
| Multiboot header | (not booted) | `grub-file --is-x86-multiboot` accepts the kernel |
| Normal boot | the real ISO, through GRUB (128 MB), and QMP types `x` after "System ready." | logo lit-pixel count = **293** (0.8: VGA reached through the physical view); title; version `v0.9`; `GDT`, `IDT`, `Interrupt controller`, `Exception handling`, `Memory initialization`, `Display` all `OK`; `Memory self-test: PASS`; every memory panel line; no `PENDING`, `FAILED` or `PANIC`; memory model check. **[0.4]** Timer and keyboard self-tests `PASS` and status `OK`; `IRQ mask: 0xfffc`; `uptime: 5 s`; the typed key arrives (press and release); no unexpected IRQs or dropped keys. **[0.5]** Console self-test `PASS`, `Console & shell: OK`, `System ready. Press any key to open the console.`, and the typed `x` opens the console (`console: started`). |
| RAM sizes | `-kernel` at `-m 16M`, `32M`, `1G`, `4G` | boots; self-test passes; memory `OK`; panel shows the expected MB above 4 GiB; memory model check with pinned values |
| Boot module | `-m 32M -kernel … -initrd module.bin` | module range reserved; model check |
| Exhaustion | `-append "test=memexhaust"` at 16M and 128M | every free page allocated, verified and freed (through the physical-access window since 0.6; the frames held by paging and the heap are left alone and must match the kernel's count exactly); `System ready` afterwards; no panic. **[0.4]** ran with IRQ0/IRQ1 live. |
| Double free / free reserved | `test=doublefree`, `test=freereserved` | crash screen with the page address |
| Crash tests | `test=divzero`, `ud`, `gpf` | vector, name, error code (`0x28` for #GP), 64-bit register dump (`RIP=0x…` 16 digits) with `CS=0x0008`, halted. **[0.4]** raised with IRQ0/IRQ1 live. |
| **[0.4] Timer rate** | `-m 32M -kernel … -append "test=timer"` | `Timer diagnostic: PASS`: 270–330 ticks across 3 RTC seconds. The measured value is printed (typically 293–300). |
| **[0.4] Keyboard, scripted** | `test=keyboard`, with `tests/qmp_keys.py` typing `a, Shift+B, Caps Lock, a, Caps Lock, Shift+1, Up, right Ctrl, Enter, keypad Enter` | every event decoded exactly, **in order**: scancode, key name, character and flags. For example `'B'` with `flags=0x03`, `'A'` with Caps (`0x11`), `'!'`, `UP` extended (`0x21`), `RCTRL` (`0x25`), `KP_ENTER` (`0x21`), and the extended release. Nothing dropped or unknown. |
| **[0.4] Queue overflow** | `test=kbdflood`, with QMP typing 50 keys (100 events) quickly | queue full (**64 kept**), overflow dropped and counted, **kept + dropped + late = 100** (no event lost unaccounted), drop count reported from normal context, kept events are the oldest in order, system ready afterwards |
| **[0.4] Unexpected IRQ** | `test=irqunexpected` | IRQ2 counted and acknowledged, reported from normal context, system ready, no panic |
| **[0.5] Console session** | `-kernel`, then `tests/qmp_keys.py --text` types a 32-line session: `x` (opens the console), then commands, edits and errors | first key opens the console and isn't typed; all 32 lines submitted; empty and blank lines give no output; `help` (7 lines, in order); exact `version` (`64-bit x86-64`); `uptime` twice, advancing; `mem` figures; `cpu` lines (mode `64-bit long mode, paging on`); Shift and multiple spaces; tabs; Caps Lock (and case-sensitive names); Backspace; Left+Delete; Home/End/insert; Right/insert; Ctrl/Alt ignored; unknown command; usage error; more than 8 words refused; exactly 8 accepted; **71-character limit** (refusal logged, 66 `a`s echoed); 7 identical `version` outputs; scrolling; `clear`; no dropped keys, panic or unexpected IRQs |
| **[0.6] Paging and heap at boot** | the normal-boot log | `Paging walk-check: PASS`; page 0 left unmapped; paging self-test: TLB flush on remap, window map/access/unmap, **no page leak** (free pages equal before and after), `PASS`; CR0.PG, CR0.WP, CR4.PAE and EFER.LMA set and CR3 page-aligned (bits checked by the script); `Paging enabled: OK`; heap self-test no page leak and `PASS`; `Kernel heap: OK`; no `MISMATCH` anywhere |
| **[0.6] RAM sizes** | the 16M, 32M, 1G, 4G and module logs | `Paging enabled: OK` and `Kernel heap: OK` at every size |
| **[0.6] Page faults** | `test=pfnull`, `pfunmapped`, `pfreadonly` (32M) | `CPU exception 14: Page Fault (#PF)`; CR2 `0x0000000000000000` / `0x0000000040000000` / `0xffffffff80100000` (0.8; `0x0000000000100000` up to 0.7); decoded `Page fault: not present, read, kernel mode` / the same / `Page fault: protection violation, write, kernel mode`, then `Address: 0x…` = CR2; halted |
| **[0.6] Heap misuse** | `test=heapdouble`, `heapinvalid`, `heapcorrupt` (32M) | crash screen with `Heap: double free of 0xfffffe80000…`, `Heap: invalid free of 0x… (pointer not from the heap).`, `Heap: corrupted block at 0xfffffe80000… (freeing 0xfffffe80000…)`; halted |
| **[0.6] Heap exhaustion** | `test=heapexhaust` (32M, so the 16 MiB cap is reachable) | `NULL` at the 16 MiB cap and still `NULL` afterwards; everything freed and `heap_check` OK; **free pages before − after = heap pages grown + page tables** (recomputed by the script); System ready; no panic |
| **[0.6] Console `mem`** | the console session | after the 0.5 lines, in order: `Paging: enabled (4-level, 4 KiB, higher-half, NX on)` (or `NX off`; `identity-mapped` in 0.7), `Mapped pages`, `Page tables`, `Kernel heap: … KiB mapped, max 16384 KiB`, `Heap in use`, `Heap free` |
| **[0.7] Boot module integrity** | `-initrd` with a test module; the same module corrupted in one byte; a GRUB ISO with a `module` line | `test pattern intact` through QEMU's loader and through GRUB; one changed byte reported as `CORRUPTED`; System ready |
| **[0.7] Long mode** | the normal-boot log | the stub's markers in order (`boot32: start`, `CPUID ok, long mode and PAE present`, `boot64: entered 64-bit long mode`); EFER.LME and LMA, CR0.PE and PG, CR4.PAE (bits checked by the script); CS `0x0008` is a 64-bit code segment (descriptor bits L = 1, D = 0 checked by the script); task register `0x18`; `64-bit long mode active; 64-bit arithmetic PASS`; the 4-level walk's independent recount matches |
| **[0.7] Layout** | the normal-boot log | the kernel runs at its higher-half address (0.8: RIP inside kernel code, which starts at `0xffffffff80100000`; up to 0.7 its identity address at 1 MiB); PML4[508] in use (self-test page mapped and read back, window table present), PML4[509] (heap at `0xfffffe8000000000`, heap self-test PASS), PML4[510] (recursive entry points to the PML4); each slot index is computed by the script from the logged address |
| **[0.7] NX** | the normal-boot log and the console session | NX supported and EFER.NXE enabled with LMA kept (EFER bits checked); kernel code, interrupt stubs, the page-fault handler and IRQ handlers executable; rodata, data/bss, the IST1 stack, the current stack and the heap no-execute; `mem` shows `NX: supported, enabled` |
| **[0.7] `pfnx`** | `test=pfnx` (32M) | kernel code ran first (`0x5a5a5a5a`, no NX bit); the data page has the NX bit; exception 14, error code `0x11`; `Page fault: protection violation, read, kernel mode`, `Details: instruction fetch (NX violation)`, `Address:` = the page = CR2; halted |
| **[0.7] CPU without NX** | `-cpu qemu64,-nx` (32M), normal boot and `test=pfnx` | reported as not supported, EFER.NXE left clear; walk-check and self-test PASS with no NX bits; paging and heap OK; System ready; `pfnx` reports **NOT APPLICABLE** on serial and on screen (never a pass), then System ready, no panic |
| **[0.7] Stack guard pages** | the normal-boot log | kernel stack 16 KiB and IST1 stack 8 KiB, both read/write no-execute; both guard pages **unmapped** in the walk-check and the self-test, each directly below its stack; the data/bss range has exactly those 2 holes; the boot, keyboard-flood, console, heap-exhaustion and memory-exhaustion runs never hit a guard page (no exception) |
| **[0.7] `stackoverflow`** | `test=stackoverflow` (32M) | the guard page is not mapped; exception 8 (#DF), error code 0; `Double fault: kernel stack overflow (RSP is in the stack guard page)`; RSP inside the walk-checked guard page (checked numerically); full register dump (the handler ran on IST1); halted; **booted once (no reset)** |
| **[0.7] Screen width** | the crash logs and the console session | every crash-screen line (divzero, ud, gpf, doublefree, freereserved, pfnull, pfunmapped, pfreadonly, pfnx, stackoverflow, heapdouble, heapinvalid, heapcorrupt) and every `mem` line is at most **79** characters (an 80-character line would leave a blank line on the crash screen) |
| **[0.8] Higher-half kernel and physical view** | the normal-boot log (and the no-NX log) | `boot64: running at the higher half, RIP=0xffffffff80…`; PML4[511] maps the kernel image and PML4[256] the physical view; **lower half empty**: `PML4[0] not present` in the walk-check and the self-test (2 lines); `0x0`, `0x100000`, `0xB8000`, `0xFFFFF000` and the Multiboot info's physical address all unmapped (10 lines: both checks); **no kernel-image page in the physical view** (2 lines); page 0 not in the view (2 lines); a free-RAM frame (the PML4's) not in the view; VGA and the Multiboot info reached through the view (`phys-view rw nx`); the no-NX run also has an empty lower half |
| **[0.8] `pflow`** | `test=pflow` (32M) | `Page-fault test: read at 0x0000000000100000 (paging on; not mapped)`; exception 14, error code `0x00000000`; `Page fault: not present, read, kernel mode`, then `Address: 0x0000000000100000`, in order; CR2 `0x0000000000100000`; halted; booted once (no reset); the read really faulted (no `System ready`); crash-screen lines at most 79 characters |
| **[0.8] Stack high-water mark** | the boot, keyboard-flood and console logs | the stack was painted 256 bytes below RSP (`live frames untouched`); the peak is between 0 and 16384 bytes at "System ready.", after the keyboard flood and after the console session (1480 in each in 0.8, 1496 in 0.9, printed by the script); `mem`'s last row is `Kernel stack: … bytes at peak, of 16384` |
| **[0.8] Boot module on screen** | the QEMU module, GRUB module and corrupted-module logs, and the normal boot | `Boot module 1: test pattern intact (14016 bytes)` on the message line through QEMU and through GRUB; `… test pattern CORRUPTED (14016 bytes)` for the corrupted module; **no** module message on a normal boot (screen unchanged) |
| **[0.8] Reproducible build** | `make repro` (its `sha256.txt`) | `madire.elf` and `madire.kernel` byte-identical in two clean builds (one listing files in reverse order); the kernel under test is that same reproducible binary |
| **[0.9] Scheduler self-test, thread stack area** (11) | the normal-boot and no-NX logs | `Scheduler self-test: PASS`; the ping-pong strictly alternating; frames, stack slots and threads all returned; the preemptions during the ping-pong logged; a thread that never yields preempted at least 3 times in 30 ticks; `sleep(10)` after 10 or 11 ticks; the walk-check finds PML4[507] empty at boot and its page table present; no screen-rule violation; PASS without NX too; longest turn and highest stack peak logged |
| **[0.9] Cooperative switches** (11) | `test=threads` (32M; each thread diagnostic below boots `-m 32M -kernel … -append "test=…"`) | 8 threads created; 800 turns in strict round-robin order; `rbx`, `rbp`, `r12`–`r15` preserved across every switch; each thread's stack contents intact; IF=1 after every switch; everything returned; preemptions during the cooperative phase logged; PASS; plus the 3 checks every thread diagnostic gets: no panic or mismatch, System ready afterwards, longest turn and highest stack peak logged |
| **[0.9] Turn order, relaxed branch** (8) | `test=pingpongpreempt` | one turn deliberately runs past its slice, so a preemption lands mid-ping-pong: the order differs only right after it; accepted through the relaxed branch; both threads finish (codes 11 and 22); everything returned; PASS; the 3 common checks |
| **[0.9] Turn order, negative test** (7) | `test=pingpongbad` | a skipped turn without a preemption is rejected (`out of order without a preemption`) and a repeated one too (`wrong number of turns`); everything returned; PASS; the 3 common checks |
| **[0.9] Join** (8) | `test=join` | 6 exit codes exact (0, 1, 255, `0x7fffffff`, −1, `INT32_MIN`); a thread other than 0 blocks in a join and is woken; a detached thread reaped by thread 0; everything returned; PASS; the 3 common checks |
| **[0.9] Interrupt flag** (9) | `test=ifstate` | a new thread starts with IF=1; yield with IF=1 resumes with IF=1; yield with IF=0 resumes with IF=0 while another thread ran with IF=1 and took a tick; thread 0 keeps IF=1; a preempted thread resumes through `iretq` with its own flags (AC set in one thread, never in the others); PASS; the 3 common checks |
| **[0.9] Blocking with preemption disabled** (3 × 5) | `test=preemptyield`, `preemptjoin`, `preemptsleep` | the crash screen names the call, the count and the thread (`thread_yield: called with preemption disabled (count 1, thread 0 'kernel').`); halted; booted once; the call never returned; crash-screen lines at most 79 characters |
| **[0.9] Preemption** (11) | `test=preempt`, with QMP typing 8 keys during the test | 3 threads that never yield, 300 ticks: each gets 25–42 % of the CPU; 40–80 preemptions (one per 5-tick slice); every thread made progress; the timer kept ticking; everything returned; PASS; the 8 keys all arrive, in order (IRQ1 isn't starved); no key dropped; the 3 common checks |
| **[0.9] Deferred switch** (8) | `test=preemptoff` | no switch while preemption is held for 20 ticks; the request stays pending; the other thread runs at most one tick after the release; everything returned; PASS; the 3 common checks |
| **[0.9] Sleep** (10) | `test=sleep` | `sleep(1)`, `sleep(10)`, `sleep(100)` each wake after n or n+1 ticks; the spinning thread gets at least 95 % while the other sleeps; thread 0 sleeps 10 ticks by polling; everything returned; PASS; the 3 common checks |
| **[0.9] Heap under preemption** (8) | `test=heapthreads` | 4 threads, hundreds of thousands of `kmalloc`/`kfree` operations, preempted meanwhile; every tagged payload intact; `heap_check` OK and nothing left in use; free pages = before − heap growth − new tables, exactly; PASS; the 3 common checks |
| **[0.9] Frames and the window under preemption** (7) | `test=framethreads` | 4 threads, hundreds of thousands of allocations, preempted meanwhile; no frame handed out twice (every tag intact); free pages restored exactly; PASS; the 3 common checks |
| **[0.9] Thread churn and stack peaks** (11) | `test=threadleak` | a thread more than 8 KiB deep reports a peak of 8192–16383 bytes, and its exit line shows it; 2000 threads created, run and freed (joined and detached); every exit code right, never over the limits; heap use unchanged; free pages, stack slots and threads exactly as before; no exit line per churn thread (logging off); PASS; the 3 common checks |
| **[0.9] Exit lines** (1) | every thread-diagnostic log | every `Thread N '…' exited` line has a stack peak with 0 < peak < 16384 |
| **[0.9] Thread stack overflow** (7) | `test=threadoverflow` | exception 8 (#DF); `Double fault: kernel thread stack overflow (thread N 'overflow')`; RSP inside that thread's guard in slot 507 (checked numerically); halted; booted once (no reset); the thread never returned; crash-screen lines at most 79 characters |
| **[0.9] Console `ps`** (4) | a short console session (`x`, `ps`, `help`) | `ps` prints the header, thread 0 `kernel` `running` and the count; `help` lists `ps` after the seven commands; every line at most 79 characters; no panic and no screen-rule violation |
| **[0.9] Boot-screen check** (3) | `scripts/check-bootscreen.sh` on the sources and on two altered copies | passes on the sources; rejects a `bootscreen_*` call from another file; rejects an unwrapped call in `kernel.c` outside the boot sequence |
| **[0.7] No 64-bit CPU** | `-cpu qemu32`, `qemu-system-i386`, `-cpu 486`, `-cpu qemu64,-lm`, `-cpu qemu64,-pae` | `boot32: start`, then `Madire OS needs a 64-bit (x86-64) CPU: long mode not supported. Halted.` (`PAE not supported` for `-pae`); no mode switch attempted; **QEMU still running at the timeout** (a triple fault would have ended the run under `-no-reboot`) |

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
| Type | Other / Unknown (64-bit) (since 0.7) |
| Firmware | **BIOS** (not EFI) |
| RAM | 4096 MB |
| CPUs | 2 |
| Video memory | 128 MB |
| Network | NAT |
| Storage | SATA port 0: 30 GB disk (unused for now); SATA port 1: optical drive |
| Serial port (since 0.7) | COM1 (`0x3F8`, IRQ 4), output to the file `build\vbox-serial.log` (git-ignored) |
| PAE/NX setting | left at its default (`--pae off`): VirtualBox still shows the guest long mode and NX |

Commands that were run (`VBoxManage` is in `C:\PFiles\Oracle\VirtualBox\`):

```
VBoxManage modifyvm MadireOS-Test --firmware bios --ostype Other
VBoxManage modifyvm MadireOS-Test --ostype Other_64                      # 0.7
VBoxManage modifyvm MadireOS-Test --uart1 0x3F8 4 --uartmode1 file C:\Users\Mamishi.Madire\Desktop\madire-os\build\vbox-serial.log   # 0.7
```

**The serial log is the VirtualBox evidence** (since 0.7): the kernel writes the same lines QEMU's tests read (boot markers, CPU mode, walk-check, self-tests, crash details, the boot-module result). VirtualBox recreates the file at every power-on, so copy it after each test (`scripts/vbox-acceptance.ps1` does this). Compare it with the QEMU run of the same kernel. The only differences, found in 0.7 and again in 0.8 and 0.9, and why they are expected:

- the code-segment descriptor shows `0x00af9b…` in VirtualBox and `0x00af9a…` in QEMU: VirtualBox sets the descriptor's "accessed" bit when CS is loaded (L = 1 and D = 0 are identical);
- boots through GRUB give **26** paging self-test checks and **380** mapped pages, boots with QEMU's `-kernel` give **27** and **381** (0.8: 364/365; 0.7: 19/20 and 360/361): the two loaders place boot data differently (one more reservation above 1 MiB, which the physical view maps and the self-test checks). VirtualBox and QEMU booting the same ISO through GRUB give identical figures;
- counts that depend on RAM size (free pages, memory-exhaustion pages) differ between the 4 GB VM and QEMU's smaller test machines;
- timing figures vary a little from run to run: the timer diagnostic (300 ticks in VirtualBox, 299 in QEMU in the 0.9 run) and the preempt test's shares (VirtualBox 33.3/33.6/33.0 % with 59 preemptions, QEMU 33.3 % each with 60), all inside their bands.

Everything else matches line for line in 0.9: the stub markers (including `boot64: running at the higher half, RIP=0xffffffff80101000`), the CPU mode, the walk-check (76 items, 79 with a module), both `lower half empty` lines, the slot-507 lines, every crash test's exception, error code, decoded lines and CR2, the scheduler self-test (9 checks), the stack peak (1496 bytes) and the boot-module result.

**VirtualBox delivers timer ticks in bursts (0.9).** After the host has delayed the VM (other host load, the VM being descheduled), VirtualBox's emulated PIT can catch up by delivering several timer interrupts almost at once. A 5-tick slice can then run out within microseconds of guest time. This is a known environment behaviour, not a kernel fault, and it is why the scheduler's cooperative order checks count preemptions (option (A); see *Kernel threads and scheduler* in [architecture.md](architecture.md)).

**Correction of a stage-3 statement (0.9).** During stage 3 the claim was made that a thread's slice "never runs out mid-turn" in the cooperative ping-pong, because each turn is far shorter than a slice. That holds in QEMU, but **not in VirtualBox**: in the first stage-8 run, 1 boot in 14 failed the self-test because a tick burst preempted a thread in the middle of the ping-pong (154 switches instead of 153). The kernel was correct; the test's assumption was wrong. After the option (A) fix, all 34 VirtualBox boots of the final acceptance run passed, each with **0 preemptions** during its ping-pong. So the relaxed branch is proven in QEMU (`test=pingpongpreempt`) but hasn't been observed in VirtualBox yet.

### Diagnostics menu positions (0.9)

`pflow` was inserted after `pfnx` in 0.8, which moved the entries after it down by one. The two 0.9 entries were appended at the end, so no position moved in 0.9. To reach an entry by keyboard (as the scripted VirtualBox tests do): on the main menu press ↓ once (Diagnostics) and Enter, then press ↓ the number of times shown, then Enter.

| ↓ presses | Entry | `test=` |
|---|---|---|
| 0 | Crash test: divide by zero | `divzero` |
| 1 | Crash test: invalid opcode | `ud` |
| 2 | Crash test: general protection fault | `gpf` |
| 3 | Page-fault test: read from NULL | `pfnull` |
| 4 | Page-fault test: read from unmapped address | `pfunmapped` |
| 5 | Page-fault test: write to read-only kernel code | `pfreadonly` |
| 6 | Page-fault test: execute from no-execute data (NX) | `pfnx` |
| **7** | **Page-fault test: read the kernel's old low address** (new in 0.8) | `pflow` |
| 8 | Crash test: kernel stack overflow (was 7) | `stackoverflow` |
| 9 | Memory test: allocate every page (was 8) | `memexhaust` |
| 10 | Memory test: double free (was 9) | `doublefree` |
| 11 | Memory test: free a kernel page (was 10) | `freereserved` |
| 12 | Timer test: measure against RTC (was 11) | `timer` |
| 13 | Keyboard test: show key codes (was 12) | `keyboard` |
| **14** | **Thread test: preemption** (new in 0.9) | `preempt` |
| **15** | **Crash test: kernel thread stack overflow** (new in 0.9) | `threadoverflow` |

![GRUB Diagnostics submenu scrolled to the end, with the two 0.9 entries (0.9)](images/grub-diagnostics-v0.9.png)

![GRUB Diagnostics submenu with pflow highlighted (0.8)](images/grub-diagnostics-v0.8.png)

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

### Scripted acceptance test: `scripts/vbox-acceptance.ps1` (0.9)

Since 0.9 the tests below are run by a script, on Windows, with the VM powered off:

```powershell
.\scripts\vbox-acceptance.ps1                          # every test
.\scripts\vbox-acceptance.ps1 -Only boot,preempt       # some tests
.\scripts\vbox-acceptance.ps1 -Only repeat -Repeat 20  # 20 extra normal boots
```

**It checks the VM and refuses to change it.** Before anything else it reads the VM's settings (`VBoxManage showvminfo`) and compares them with the configuration above: OS type `Other_64`, BIOS firmware, 4096 MB, PAE off, DVD first in the boot order, COM1 at `0x3F8` IRQ 4 writing to `build\vbox-serial.log`, and the VM powered off. On any difference it lists what differs and **stops without changing anything**. It never changes a VM setting; the only change it makes is attaching the ISO under test to the optical drive (as `scripts\run.ps1` does), and it reads the attachment back to verify it. Report a difference rather than "fixing" it; a settings change needs approval.

**What it does:** it prints the SHA-256 of the ISOs it tests (`build\madire-os.iso`, and `build\madire-os-module.iso` for the module test, built as described in Test I), then boots the VM headless once per test, sends keys through VirtualBox's emulated PS/2 keyboard no sooner than 3.5 s after power-on (`-KeyDelayMs`), waits for the line that ends each test in the serial log, takes a screenshot, powers off, keeps a copy of the serial log and checks it for each expected line. Everything goes to `build\vbox-acceptance\` (`-OutDir`), with a summary file; the script exits with 1 if any expected line is missing.

| `-Only` group | What it checks |
|---|---|
| `menu` | screenshots of the GRUB menu, the top of the Diagnostics submenu and its end (entries 14 and 15) |
| `boot` | the normal boot: `Scheduler self-test: PASS`, `System ready.`, then in the console `ps` (thread 0 `kernel` `running`), `version` (`Madire OS v0.9 (64-bit x86-64, Multiboot 1)`) and `mem` (the `Kernel stack:` row) |
| `preempt` | Diagnostics entry 14: `Preempt test: fair shares: …: OK` and `Preempt test: PASS` |
| `threadoverflow` | entry 15: `Double fault: kernel thread stack overflow (thread N 'overflow')`, halted |
| `pf` | the five page-fault entries and the kernel stack overflow: each one's address, NX or double-fault line, halted |
| `diag` | divide by zero (`#DE`), the timer diagnostic (`Timer diagnostic: PASS`) and the keyboard diagnostic (`a`, `Shift+A` with `flags=0x03`, Up) |
| `memexhaust` | `Memory exhaustion test: PASS`, then `mem` in the console |
| `module` | the module ISO: `Boot module 1: test pattern intact (14016 bytes)` |
| `repeat` (with `-Repeat N`) | N extra normal boots; for each, the self-test result, the preemptions during its ping-pong and the ping-pong verdict |

In 0.9 the full run gave 33 of 33 expected lines, and 20 extra normal boots each gave `self-test PASS; ping-pong preemptions 0; 100 yields, strictly alternating: OK`. The serial logs are then compared with QEMU's, as described above.

### Test A: normal boot

The GRUB menu shows **Madire OS v0.9** and **Diagnostics**:

![GRUB menu (0.9)](images/grub-menu-v0.9.png)

After 3 seconds it boots the first entry:

![Madire OS v0.9 boot screen](images/boot-screen-v0.9.png)

- The M logo is drawn in the blue-to-cyan gradient; `M A D I R E   O S` and `Madire OS v0.9` share one row. The screen's layout is unchanged in 0.9: the scheduler self-test reports on the serial log only.
- Left column: **10 status lines, all `OK`**, including `Paging & kernel heap` (0.6), `Timer (PIT, 100 Hz)`, `Keyboard (PS/2)` and `Console & shell` (a self-test of the console code, not of hardware).
- Right column: memory figures, then **Uptime**, which counts up every second, and **Last key**, which shows the last key pressed (`none` until then). For `MadireOS-Test` (4096 MB):

  | Line | Value |
  |---|---|
  | Physical RAM detected | **4096 MB** |
  | Usable RAM (<4 GiB) | **3584 MB** |
  | RAM above 4 GiB | **512 MB** |
  | Page size | **4096 bytes** |
  | Usable pages | **917391** |
  | Free pages | **917105** in 0.9 (917121 in 0.8, 917124 in 0.7; usable minus the low 1 MiB, the kernel and GRUB's boot data; changes when the kernel's size changes). This is taken before the kernel's page tables and the heap are set up; the console's `mem` then shows 60 fewer (**917045**: 15 page tables, the PML4 and 44 heap pages; 0.9 adds the 3 tables of the thread stack area). |

- Bottom row: `System ready. Press any key to open the console.`
- Serial log: `boot32: start`, `boot32: CPUID ok, long mode and PAE present`, `boot64: entered 64-bit long mode`, `boot64: running at the higher half, RIP=0xffffffff80101000` (0.8), the stack paint line, `CPU mode: … 64-bit long mode active; 64-bit arithmetic PASS`, `NX: supported by the CPU …; EFER.NXE enabled`, `Paging check: lower half empty: PML4[0] not present …: OK` (twice), `Paging check: kernel thread stack area (PML4[507]), empty at boot …: OK` (0.9), `Paging walk-check: PASS (76 items, 0 mismatches)`, both guard pages `unmapped`, `Paging self-test: PASS (26 checks, 0 mismatches)`, `Kernel heap self-test: PASS`, then the scheduler self-test (0.9): `cooperative ping-pong: 100 yields, strictly alternating: OK`, `preemptions during the ping-pong: 0: OK`, the exit lines `Thread 1 'pingA' exited: code 11, stack peak 192 of 16384` and `Thread 2 'pingB' …`, `Scheduler self-test: PASS (9 checks, 0 mismatches)`; and after "System ready." `Kernel stack: 1496 bytes at peak, of 16384`.

### Test F: console and shell

Press any key on the boot screen. That key opens the console and **isn't typed**: the prompt is empty.

![Console](images/console-v0.5.png)

| Check | Expected |
|---|---|
| Console opens | Banner `M A D I R E   O S` / `Madire OS v0.9` / `Type 'help' …`, then `madire>` with a **blinking underline cursor** |
| `help`, `version`, `uptime` | the 7-command list, then `ps` (0.9); `Madire OS v0.9 (64-bit x86-64, Multiboot 1)`; `Uptime: HH:MM:SS (N ticks at 100 Hz)`, advancing |
| `ps` (0.9) | `TID  STATE      TICKS    STACK PEAK    NAME`, then `  0  running   …   1496/16384   kernel` and `1 thread` (only thread 0 exists after boot; no line wraps) |
| `mem`, `cpu` | 4096 / 3584 / 512 MB, 917391 usable pages, 917045 free, 5 reserved ranges, then `Paging: enabled (4-level, 4 KiB, higher-half, NX on)`, `Mapped pages: 380`, `Page tables: 15`, `NX: supported, enabled`, `Kernel heap: 176 KiB mapped, max 16384 KiB`, `Heap in use: 0 KiB in 0 blocks`, `Heap free: 176 KiB, largest block 175 KiB`, and last `Kernel stack: 1496 bytes at peak, of 16384` (0.9; no line wraps); CPUID vendor, brand (VirtualBox shows the host CPU, e.g. `13th Gen Intel(R) Core(TM) i5-1335U`), family/model, features, mode `64-bit long mode, paging on, 1 CPU in use`. On this CPU the "Features:" line is longer than 80 columns and wraps (known since 0.5). |
| Editing | `echo hellx`, Backspace, `o` → `hello`; `echo abXc`, Left, Left, Delete → `abc`; `cho world`, Home, `e`, End, `!` → `world!`; Right and insert → `hello` |
| Shift, Caps Lock, Tab | `echo   Hello   World!` → `Hello World!`; Caps Lock, `echo abc` → `ECHO ABC` (unknown: names are lowercase); `echo`, Tab, `a`, Tab, Tab, `b` → `a b` |
| Errors (red) | `foo` → `Unknown command: foo. …`; `version x` → `Usage: version`; `echo 1 2 3 4 5 6 7 8` → `Too many arguments (max 8).` |
| Limits | `echo` + 100 × `a`: the line stops at 71 characters (66 `a`s echoed) |
| Empty lines | Enter alone, or spaces then Enter: just a new prompt |
| Scrolling, clear | several `help`s scroll the screen, with the prompt on the bottom row; `clear` leaves only the prompt on row 0 |

![help, version and uptime (0.8)](images/console-v0.8.png)

![ps and version (0.9)](images/console-ps-v0.9.png)

![mem with the higher-half kernel, NX and the stack peak (0.9)](images/console-mem-v0.9.png)

![cpu and echo (0.7)](images/console-cpu-v0.7.png)

![Editing, errors and the 71-character limit](images/console-editing-v0.5.png)

### Test B: crash screen

In the GRUB menu, press **↓** to highlight **Diagnostics**, press **Enter**, then choose **Crash test: divide by zero**:

![Madire OS crash screen](images/crash-screen-v0.2.png)

- White on red, headed `*** MADIRE OS - KERNEL PANIC ***`.
- `CPU exception 0: Divide Error (#DE)`, `RAX=0x000000000000000a`, `RCX=0x0000000000000000`, `CS=0x0008`, `DS=0x0010`, then `CR0`/`CR2`/`CR3`, `CR4` and `EFER` (the 64-bit dump since 0.7; the 0.2 screenshot shows the older 32-bit layout). Since 0.8, RIP and RSP are higher-half addresses (`0xffffffff80…`).
- Since 0.4, RFLAGS has bit 9 set (e.g. `0x0000000000010246`): the exception happened with hardware interrupts live.
- No line is longer than 79 characters (since 0.7).

The other two crash entries should show `Invalid Opcode (#UD)` and `General Protection Fault (#GP)` with error code `0x00000028`.

### Test C: memory diagnostics

| Entry | Expected |
|---|---|
| **Memory test: allocate every page** | after **about 50–90 seconds** on a 4 GB VM (each page goes through the physical-access window since 0.6; 50 s in the 0.8 run), `Memory exhaustion test: PASS (… pages)` and `System ready.` That is the boot panel's free pages minus the frames paging and the heap already hold (0.9: 917045 pages; 60 = 15 page tables + the PML4 + 44 heap pages, which the serial log lists; 0.8: 57; 0.7: 54). Afterwards the console's `mem` shows free pages back at that figure and the heap figures unchanged. |
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

### Test G: page-fault diagnostics (0.6; NX added in 0.7, `pflow` in 0.8)

**Diagnostics → Page-fault test: …**. Each ends on the crash screen with `CPU exception 14: Page Fault (#PF)` and the decoded error code under it, on up to three lines (since 0.7: `Page fault: …`, `Details: …` only when needed, `Address: 0x…`):

| Entry | Error code | Decoded lines | CR2 |
|---|---|---|---|
| read from NULL | `0x00000000` | `Page fault: not present, read, kernel mode` / `Address: 0x0000000000000000` | `0x0000000000000000` |
| read from unmapped address | `0x00000000` | `Page fault: not present, read, kernel mode` / `Address: 0x0000000040000000` | `0x0000000040000000` |
| write to read-only kernel code | `0x00000003` | `Page fault: protection violation, write, kernel mode` / `Address: 0xffffffff80100000` (0.8; `0x0000000000100000` up to 0.7) | `0xffffffff80100000` |
| execute from no-execute data (NX) | `0x00000011` | `Page fault: protection violation, read, kernel mode` / `Details: instruction fetch (NX violation)` / `Address:` the data page (0.9: `0xffffffff80169000`; 0.8: `0xffffffff8015e000`) | the data page |
| read the kernel's old low address (0.8) | `0x00000000` | `Page fault: not present, read, kernel mode` / `Address: 0x0000000000100000` | `0x0000000000100000` |

![Page fault: write to read-only kernel code, at its higher-half address (0.8)](images/pagefault-v0.8.png)

![Page fault: execute from no-execute data](images/pfnx-v0.7.png)

**`pflow` (0.8)** reads `0x100000`, where the kernel ran up to 0.7. With the lower half empty that address is not mapped, so it must end here, not read kernel code:

![Page fault: read the kernel's old low address (0.8)](images/pflow-v0.8.png)

For `pfnx`, the serial log shows first that kernel code ran (`returned 0x5a5a5a5a (correct)`), then that the data page has the NX bit. On a CPU without NX the data page runs and the test says **NOT APPLICABLE** (serial and screen) instead of passing; the VM exposes NX, so it faults.

The four heap diagnostics (`heapdouble`, `heapinvalid`, `heapcorrupt`, `heapexhaust`) have no GRUB entries; they run in QEMU only (section 1b).

### Test H: kernel stack overflow (0.7)

**Diagnostics → Crash test: kernel stack overflow.** The kernel recurses until its stack reaches the unmapped guard page:

![Kernel stack overflow: double fault on IST1 (0.8, higher-half addresses)](images/stackoverflow-v0.8.png)

- `CPU exception 8: Double Fault (#DF)`, error code `0x00000000`;
- `Double fault: kernel stack overflow (RSP is in the stack guard page)`, then `RSP=0x…` and the guard page's range (the RSP lies inside it);
- the full register dump (the handler ran on its own IST1 stack), then `System halted.`;
- the VM **doesn't reset**: the screen stays up, and the serial log shows `boot32: start` once.

### Test I: boot module through GRUB (0.7; on screen since 0.8)

The normal ISO loads no module, so this uses a scratch ISO built exactly like the QEMU test's (a `grub.cfg` with a `module /boot/test-module.bin` line, and a test module made by `make_test_module` in `tests/qemu-tests.sh`); it isn't part of the repository. The serial log has the full result:
`Boot module 1: 0x0016f000-0x001726c0 (14016 bytes): test pattern intact (FNV-1a …, expected …)`. Since 0.8 the boot screen's message line also shows `Boot module 1: test pattern intact (14016 bytes)`, for machines without a serial port; otherwise it looks like a normal boot, with one more reserved range:

![Boot module result on the message line (0.8)](images/module-v0.8.png)

### Test J: preemption (0.9)

**Diagnostics → Thread test: preemption** (entry 14). Three kernel threads that never yield spin for 300 ticks (3 seconds), and the scheduler must share the CPU between them by preemption alone. The boot screen's message line shows `Preempt test: 3 threads that never yield, for 3 seconds...`, then `Preempt test: PASS`, and the boot finishes normally (`System ready.`):

![Preempt test: PASS on the message line (0.9)](images/preempt-v0.9.png)

The serial log has the figures. From the 0.9 run:

- `Preempt test: fair shares: spin0 33.3% (100 ticks), spin1 33.6% (101), spin2 33.0% (99) of 300 (each 25-42%): OK`
- `Preempt test: preemptions: 59 preemptions in 300 ticks (about 60 expected: one per 5-tick slice): OK`
- every thread made progress, the timer kept ticking (311 ticks elapsed), and everything returned (free pages, stack slots, live threads);
- `Preempt test: longest turn: 5 ticks (slice 5)`, then `Preempt test: PASS (6 checks, 0 mismatches)`.

The exact shares and counts vary slightly from run to run; each share must stay within 25–42 %.

### Test K: kernel thread stack overflow (0.9)

**Diagnostics → Crash test: kernel thread stack overflow** (entry 15). A kernel thread named `overflow` recurses until its stack runs into the unmapped 16 KiB guard below it:

![Kernel thread stack overflow: double fault naming the thread (0.9)](images/threadoverflow-v0.9.png)

- `CPU exception 8: Double Fault (#DF)`, error code `0x00000000`;
- `Double fault: kernel thread stack overflow (thread 1 'overflow')`, then `RSP=0xfffffd8000003ff0  guard 0xfffffd8000000000-0xfffffd8000003fff`: the RSP lies inside that thread's guard, in slot 507;
- the full register dump (the handler ran on IST1), then `System halted.`;
- the VM **doesn't reset**: the serial log shows `boot32: start` once.

### Troubleshooting by layer

| Symptom | Layer | Check |
|---|---|---|
| "No bootable medium found" | ISO / VM config | ISO attached? Boot order has DVD first? |
| UEFI shell, or a blank screen before GRUB | VM config | Firmware must be BIOS |
| GRUB shows `error: no multiboot header found` | Kernel / linker | Multiboot test in `make test`; `.multiboot` must come first in `linker.ld` |
| GRUB `invalid arch-dependent ELF magic` | Toolchain | GRUB must be given the flat binary `build/madire.kernel` (loaded through the a.out kludge fields), not the 64-bit ELF `build/madire.elf` |
| White on red on the top row: `Madire OS needs a 64-bit (x86-64) CPU: … Halted.` | CPU / VM | The (virtual) CPU has no long mode or no PAE: in VirtualBox, choose a 64-bit OS type (e.g. `Other_64`) and a CPU with hardware virtualization |
| Screen blank after GRUB | Kernel | Check the serial log (`build/serial-boot.log` from `make test`, or `build\vbox-serial.log` in VirtualBox): the last `boot32:`/`boot64:` marker shows how far the stub got |
| VM resets straight back to GRUB/BIOS | CPU tables | Triple fault. Run `make test`: whichever marker or status line is missing from `build/serial-boot.log` shows the failing step. |
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
| Crash screen: exception 14 outside the page-fault tests | A kernel bug | The `Page fault:` lines give the access type and the address (CR2); RIP is the faulting instruction. `Details: instruction fetch (NX violation)` means code ran from a no-execute page (data, stack or heap) |
| Crash screen: exception 14 with a low address (below `0x0000800000000000`) outside `pflow` | A kernel bug (0.8) | The lower half is empty: code used a physical address as a pointer. Use `phys_view()` (low 1 MiB, boot data), the window (other frames) or `kernel_phys()` (see `docs/development.md`) |
| Screen blank or reset right after `boot64: entered 64-bit long mode`, with no `running at the higher half` line | Boot stub / linker (0.8) | The jump to the higher half failed: check the stub's temporary PML4[511] mapping and that `.boot` is linked at 1 MiB and everything else at `0xFFFFFFFF80000000` + physical |
| Crash screen: exception 8, `Double fault: kernel stack overflow …` outside the stack test | A kernel bug | The kernel stack (16 KiB) ran into its guard page: too-deep recursion or a large local buffer |
| Crash screen: `Heap: …` | Kernel heap misuse | Double free, invalid free or a corrupted block: a kernel bug; the message gives the pointer |
| Crash screen: `kmalloc: called with interrupts disabled` | Kernel heap | `kmalloc`/`kcalloc`/`kfree` called from an IRQ/exception handler or with interrupts off |
| Crash screen: `Scheduler self-test failed (details on the serial log).` | Scheduler (0.9) | The `Scheduler self-test:` lines name the failing check. If it's the ping-pong order, check the line `preemptions during the ping-pong`: a reordering right after a preemption is accepted (option (A)); one without a preemption is a real fault |
| Crash screen: `… called with preemption disabled (count N, thread T '…').` outside the `preempt*` tests | A kernel bug (0.9) | A thread yielded, slept, joined or exited inside a `preempt_disable()` section (or while holding the window). The message names the call and the thread |
| Crash screen: `Screen output from thread N '…': only thread 0 writes to the screen` | A kernel bug (0.9) | A thread other than 0 used `kprintf`/`kputs`; it must use `klogf` |
| Crash screen: `Boot screen used from thread N '…'` | A kernel bug (0.9) | A boot-screen call outside thread 0; every such call must go through `BOOTSCREEN_SAFE()` on thread 0 |
| Crash screen: `Double fault: kernel thread stack overflow (thread N '…')` outside Test K | A kernel bug (0.9) | That thread used more than its 16 KiB stack: too-deep recursion or a large local buffer. Exit lines (`Thread N '…' exited: …, stack peak P of 16384`) show how close other threads came |
| `make iso` stops with `error: … calls bootscreen_*()` | Build check (0.9) | `scripts/check-bootscreen.sh`: a boot-screen call outside the boot sequence; use `BOOTSCREEN_SAFE()` in `kernel/kernel.c` |
| A thread test hangs in QEMU but passes in VirtualBox | Test code (0.9) | A loop that only polls `timer_ticks()` can starve IRQ0 under QEMU's TCG; do real work between the reads (see [development.md](development.md)) |
| `vbox-acceptance.ps1`: `The VM does not match the documented settings; refusing to run` | VM config | The lines above it name each difference. Nothing was changed. Don't change a VM setting without approval; report the difference |
| Panel shows less usable RAM than the VM has | Expected | RAM above 4 GiB isn't used yet, and firmware-reserved areas never are |
| `build\vbox-serial.log` is empty or missing | VM config | COM1 must be set to file mode (see the VM configuration above); the file is recreated at every power-on |
| `build.ps1`: `open local "build/madire-os.iso": Broken pipe` | VM running | Power off `MadireOS-Test`, then build again |
| `run.ps1`: "state is Saved" | VM state | Use the current `run.ps1`, which discards saved states automatically (since commit `f07b517`) |
