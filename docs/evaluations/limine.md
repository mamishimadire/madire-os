# Evaluation: replacing GRUB with Limine (for 0.13)

**Status:** an evaluation only (0.12 stage 5). No code, build file or test was changed for it. The owner decides on 0.13 after the v0.12 tag.

**Sources (read on 2026-10-12):** Limine's repository, https://github.com/limine-bootloader/limine (latest release v12.9.3, published 2026-10-06): `README.md`, `USAGE.md`, `CONFIG.md`, `COPYING`, `3RDPARTY.md` and the Multiboot 1 loader `common/protos/multiboot1.c`. Limine is BSD-2-Clause, so it is a READ FULLY reference ([references.md](../references.md#ref-limine)); nothing from it is copied into Madire OS. Madire's side was read from this repository at `9db71e8`.

## 1. Why

- **Licence:** GRUB (GPL) is today's only GPL exception, allowed only as a separate, unmodified boot tool ([AGENTS.md](../../AGENTS.md), section 11). Limine is BSD-2-Clause.
- **The road ahead:** Limine boots BIOS and UEFI machines from the same configuration, supports Secure Boot (a signed binary plus an enrolled hash of its configuration), and has its own boot protocol (framebuffer, all CPU cores, ACPI tables, the memory map), which the UEFI and multi-core work on the modernisation track can use later.
- **The same kernel to begin with:** Limine supports Multiboot 1, the protocol the kernel uses today, so the first switch needs no new boot protocol.

## 2. What Limine supports (verified)

| Item | Limine |
|---|---|
| Boot protocols | Linux, Limine, Multiboot 1, Multiboot 2, chainloading |
| Architectures | IA-32, x86-64, aarch64, riscv64, loongarch64 |
| Firmware | BIOS and UEFI; a hybrid BIOS/UEFI ISO from one directory |
| Filesystems it reads | **FAT12/16/32 and ISO 9660 only** (so an installed system needs a small FAT boot partition) |
| Partitioning | MBR, GPT, unpartitioned media |
| Multiboot 1 a.out kludge (header flag 16: `load_addr`, `load_end_addr`, `bss_end_addr`, `entry_addr`) | **Supported**, with checks (it panics on an illegal load address, a load past the file's end, an entry point outside the image, or an image above 4 GiB) |
| Multiboot 1 information | command line (flag 2), modules (flag 3), memory map plus `mem_lower`/`mem_upper` (flags 0 and 6), boot loader name `Limine` (flag 9), ELF sections, framebuffer only when the kernel asks for one (flag 12); EAX = `0x2BADB002` |
| Text mode | On BIOS, a kernel that asks for no video mode gets 80×25 VGA text mode at `0xB8000` (Madire's case); `textmode: yes` forces it |
| Before the hand-off | x2APIC switched back to xAPIC if the firmware enabled it, IOMMUs disabled, interrupts flushed for the 8259 PIC only |

## 3. What changes in the build and the ISO

**Today:** the Makefile makes `build/grub.cfg` from `iso/boot/grub/grub.cfg` (the version filled in from `kernel/kernel.h`), takes the program list from its `module /boot/prog/<name>.elf` lines (`ISO_PROGS`), copies `madire.kernel` and the 7 program files into `build/isodir`, and runs `grub-mkrescue`. `scripts/install-deps.sh` installs `grub-pc-bin`, `grub-common`, `xorriso` and `mtools`.

**With Limine:**

1. **A `limine.conf` instead of `grub.cfg`** (for example `iso/boot/limine/limine.conf`). Its format: a menu entry starts with `/` and its title; a `//` line is a sub-entry, so the Diagnostics menu becomes a directory entry with 27 sub-entries. Each entry has `protocol: multiboot1`, `path: boot():/boot/madire.kernel`, `cmdline: …`, and one `module_path:` plus `module_string:` per program. Global options include `timeout:` (GRUB's `set timeout=3`), `default_entry:` and `serial:`. The version keeps one source (`kernel/kernel.h`), as since 0.12.
2. **The ISO:** the files `limine-bios.sys`, `limine-bios-cd.bin` (and `limine-uefi-cd.bin` plus `BOOTX64.EFI` once UEFI comes) and `limine.conf` are copied into the ISO directory; `xorriso -as mkisofs … -b <limine-bios-cd.bin> -no-emul-boot -boot-load-size 4 -boot-info-table …` builds it; then `limine bios-install <iso>` makes it bootable from a disk image or USB stick too. `grub-mkrescue` goes.
3. **The `limine` host tool and binaries:** a pinned release (Limine has had major versions with changed configuration syntax, so the version is fixed and updated only deliberately), either built from source or taken from the release's `limine-binary-*` assets, **checked against a recorded SHA-256** in `scripts/install-deps.sh`. Whether Ubuntu 24.04 packages a usable version was not checked; the plan shouldn't rely on it. This changes the Actions build environment: it needs the owner's approval (the workflow rules in AGENTS.md).
4. **Notices:** Limine's `COPYING` and `3RDPARTY.md` must go with its binaries (its own rule), so both go into the ISO and `THIRD_PARTY_NOTICES` (AGENTS.md section 11).
5. **GRUB packages removed** from `install-deps.sh` (xorriso stays). The Multiboot header check uses `grub-file` today and needs a replacement (below).
6. **Maybe a reproducible ISO:** GRUB's timestamps are why the ISO isn't reproducible (since 0.8). With a direct `xorriso` call the dates could perhaps be fixed. Worth trying in 0.13; not promised.

**The kernel, in principle unchanged** (Multiboot 1 with the a.out kludge stays), but three differences reach it:

- **The kernel command line:** GRUB passes the kernel's path first (`/boot/madire.kernel keylog`), Limine passes only `cmdline:`. The kernel matches whole words (`cmdline_word`), so the options still work; any check that reads the command line as a whole changes.
- **Module strings:** GRUB passes the module line (`/boot/prog/hello.elf hello`), Limine only `module_string:`, or an empty string when there is none. The program name comes from the string or the file name (`test_progmod`), so every module needs `module_string: <name>`; an empty string must still be refused cleanly (to be checked in the plan).
- **Where things are placed:** Limine loads modules **at the top of memory below 4 GiB** (no placement preference), and puts the information structures (memory map, command line, module table and strings) in its own allocations. GRUB puts them low, just after the kernel. The kernel reads all of them through the physical view, which maps the boot-data reservations by address (`reserve.c`, `phys_view()`); that works wherever they are, but **mapping them near 4 GiB needs more page tables**, so the page-table, mapped-page and free-page figures change.
- **The memory map:** Limine passes the firmware's raw map ("Multiboot is bad and passes raw memmap"), at most 256 entries. GRUB also passes the firmware's E820 map, so the result should be the same; to be proven by comparing boots.

## 4. Existing checks that change

The exact count goes in the 0.13 plan (many checks are made in loops). By file:

| Where | Checks | Change |
|---|---|---|
| `tests/qemu-part-static.sh` | `kernel has a valid Multiboot header` (`grub-file --is-x86-multiboot`) | a Madire-written header check (magic, flags, checksum, the a.out fields), or keeping `grub-common` for this one tool (keeps a GPL tool installed) |
| | `[0.10] GRUB default entry is titled 'Madire OS v$VERSION'` | the first `/` entry of `limine.conf` |
| `tests/qemu-part-1.sh`, `tests/qemu-lib.sh` | the normal boot fixture through GRUB; `[0.7] module (GRUB)` (2) and `[0.8] module (GRUB): result on the message line`; `[0.9.2] keylog test ISO: … differs … only by 'keylog'` and the 3 GRUB-boot key checks that use the keylog ISO | the same facts through Limine (test ISOs built with `xorriso`) |
| `tests/qemu-part-3.sh`, `Makefile` | `[0.11] ISO: grub.cfg loads the 7 programs …`; `ISO_PROGS` read from `grub.cfg` | read from `limine.conf` |
| `tests/qemu-part-4.sh` | the whole `[0.10] GRUB Diagnostics` group: 27 entries, their positions, each entry booted (`smep`/`smap` twice); a test ISO per entry with `set default="1>$pos"` | Limine's entry selection (`default_entry` with sub-entries, to be verified) or a test ISO with that entry first |
| `scripts/vbox-acceptance.ps1` | menu key presses per Diagnostics position, the screenshots `grub-menu` and `grub-diagnostics-*`, the wait for GRUB's 3 s | rewritten for Limine's menu |
| Unit tests | `test_progmod`'s "GRUB: the path" case stays valid | a Limine-form case added (`module_string` only) |
| Not affected | every QEMU `-kernel` boot (most of the suite: QEMU's own Multiboot loader); `scripts/check-bootscreen.sh`; `make repro` | — |

Check names that contain "GRUB" would change too; renaming them keeps the per-tag counts but changes the PASS lists, so the plan must decide between renaming and keeping the names.

## 5. Risks

1. **Memory figures and placement (medium):** modules and boot data near 4 GiB change the page-table, mapped-page and free-page figures and the reserved ranges' addresses. The tests recompute most figures from the log, but the documented VirtualBox figures change. If Limine's map or placement needs a change in a protected file (`mmap.c`, `reserve.c`, `multiboot.h`, `memory.c`), that needs its own justification and approval.
2. **Command-line and module-string formats (low):** see section 3; covered by unit tests.
3. **VirtualBox (medium, unproven here):** Limine's BIOS boot under VirtualBox on this laptop (which runs on the Windows Hypervisor Platform) hasn't been tried; the menu automation must be rewritten; text mode at the hand-off must be confirmed on the screen.
4. **Version churn and supply chain (medium):** a pinned version with a SHA-256 check; a deliberate update path; a change to the Actions build environment (approval).
5. **Licence (low):** Limine is BSD-2-Clause, but its binaries also contain parts under Apache-2.0 WITH LLVM-exception, Apache-2.0, 0BSD, MIT, BSD variants, CC0-1.0, and one decoder under CC-BY-3.0 modified under BSD-2-Clause terms (all listed in its `3RDPARTY.md`). All appear permissive; the owner's licence check confirms that before approval.
6. **Behaviour at the hand-off (low):** x2APIC and IOMMUs are switched off. Madire uses the 8259 PIC and no IOMMU, so nothing changes today.

## 6. Effort, in stages (a proposal)

0. Approval of Limine as the boot tool (licence check, version, notices) and of the build-environment change; pin the version and its SHA-256.
1. **A/B first:** build a Limine ISO next to the GRUB ISO, from the same kernel, and compare the serial logs (`scripts/compare-serial.py`): memory map, reservations, module addresses, figures. Nothing is rewritten before the differences are known and reported.
2. Kernel-side differences, if any (module strings, placement), with unit tests; protected files only with approval.
3. The build: `limine.conf` (default entry, the keylog variant, the 27 Diagnostics), the Makefile's ISO rule with `xorriso` and `bios-install`, `ISO_PROGS`.
4. The tests: the changed checks above, each explained; `tests/expected-checks.txt`.
5. VirtualBox: the menu automation and the full acceptance.
6. Docs; GRUB removed from `install-deps.sh`; the AGENTS.md GPL exception ends.

Estimate: 5–6 stages, about 4–6 official Actions runs (15–20 minutes sharded each, about 50 minutes full), and one VirtualBox acceptance.

## 7. Recommendation

**Switch in 0.13, as its own small milestone, BIOS only and still with Multiboot 1**, so that only the bootloader changes: the kernel's boot protocol, the screen and the tests' facts stay the same. Start with the A/B comparison (stage 1) and stop to report it before rewriting any test. Limine's own protocol (framebuffer, all CPU cores, ACPI) and UEFI come later, with their own plans, on the modernisation track.

**Before the 0.13 plan, the owner decides:** Limine as the boot tool after the licence check; how it is fetched (release binaries or a source build, with a SHA-256 check); and whether the check names containing "GRUB" are renamed.

**The alternative:** keep GRUB until the UEFI milestone. That postpones the work, but keeps the GPL exception longer and means changing the bootloader and the firmware path at the same time later.
