# Evaluation: replacing GRUB with Limine (for 0.13)

**Status:** written in 0.12 stage 5 as an evaluation; **corrected in 0.13 stage 0** (2026-10-12) against Limine v12.9.3's source. The owner decided to switch in 0.13 (decisions D1–D7 in [the 0.13 plan](../plans/0.13.md)). No code, build file or test was changed for this document.

**Sources (read on 2026-10-12):** Limine's repository, https://github.com/limine-bootloader/limine (latest release v12.9.3, published 2026-10-06), at the tag `v12.9.3` (commit `e3aa138d383ca4fe061f05931b5a54497cd502f2`): `README.md`, `USAGE.md`, `CONFIG.md`, `COPYING`, `3RDPARTY.md`, the Multiboot 1 loader `common/protos/multiboot1.c`, the placement code `common/lib/elsewhere.c` and the host tool `host/limine.c`; the release's assets and the tag's signature through GitHub's API. Limine is BSD-2-Clause, so it is a READ FULLY reference ([references.md](../references.md#ref-limine)); nothing from it is copied into Madire OS. Madire's side was read from this repository at `9db71e8` (0.12 stage 5) and `2b3522f` (0.13 stage 0; the code is that of `v0.12`).

## Corrections (0.13 stage 0)

What the first version (0.12 stage 5) got wrong or left out, corrected in the sections below:

1. **Placement (section 3, risk 1):** wrong. Limine does not put modules at the top of memory below 4 GiB, and the information structures are not in its own allocations. The Multiboot information block goes at `0x10000`, as GRUB does, and modules go right after the kernel image. The "more page tables near 4 GiB" risk mostly goes away.
2. **Distribution (section 3, item 3):** the binary assets are `limine-binary.tar.gz`, `.tar.xz` and `.zip` (not `limine-binary-*`, the wording Limine's own README still uses); there is no binary branch for v12 (the `vN.x-binary` branches end at `v11.x-binary`); every asset has a detached GPG signature (`.sig`), and the tag is GPG-signed.
3. **Section 2:** the boot loader name is `Limine 12.9.3`, not `Limine`; the loader refuses header flag bits 3–15 and a load range outside usable memory; ELF section information is passed only for ELF kernels, not for an a.out-kludge image like Madire's.
4. **Licence (risk 5):** "all appear permissive" left out two items: the stage-1 decoder is a modified decoder of CC-BY-3.0 origin, and one piece of cursor code has an unconfirmed licence (section 5).
5. **Command line and module strings (section 3; corrected in 0.13 stage 3a, from the stage 1 A/B comparison):** GRUB 2.12 does not pass the kernel's or a module's path; it passes an empty command line for the default entry and module strings without the path, as Limine does.

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
| Multiboot 1 header | Searched in the first 8192 bytes, 4-byte aligned; magic and checksum checked; **header flag bits 3–15 refused** ("requires unsupported features"). Madire's flags are `0x10003` (bits 0, 1 and 16), so they are accepted |
| Multiboot 1 a.out kludge (header flag 16: `load_addr`, `load_end_addr`, `bss_end_addr`, `entry_addr`) | **Supported** (`multiboot1.c`, lines 138–228), with checks (it panics on an illegal load address, a load past the file's end, an illegal `bss_end_addr`, an entry point outside the image, an image above 4 GiB, or a load range that is not usable memory) |
| Multiboot 1 information | command line (flag 2), modules (flag 3), memory map plus `mem_lower`/`mem_upper` (flags 0 and 6; the firmware's raw map, more than 256 entries is a panic), boot loader name `Limine 12.9.3` (flag 9), ELF sections only for ELF kernels, framebuffer only when the kernel asks for one (flag 12); EAX = `0x2BADB002` |
| Placement | The information block (the structure, the command line, the module table and strings, the memory map, the boot loader name) at **`0x10000`**, as GRUB does (moved after the kernel if they overlap); **modules right after the kernel image**, page-aligned, skipping unusable memory, below 4 GiB (`elsewhere.c`: "allocate after the top of all ranges"). Everything is staged first and moved into place by a small stub at the hand-off |
| Text mode | On BIOS, a kernel that asks for no video mode gets 80×25 VGA text mode at `0xB8000` (Madire's case); `textmode: yes` forces it |
| Before the hand-off | x2APIC switched back to xAPIC if the firmware enabled it, IOMMUs disabled, interrupts flushed for the 8259 PIC only |

## 3. What changes in the build and the ISO

**Today:** the Makefile makes `build/grub.cfg` from `iso/boot/grub/grub.cfg` (the version filled in from `kernel/kernel.h`), takes the program list from its `module /boot/prog/<name>.elf` lines (`ISO_PROGS`), copies `madire.kernel` and the 7 program files into `build/isodir`, and runs `grub-mkrescue`. `scripts/install-deps.sh` installs `grub-pc-bin`, `grub-common`, `xorriso` and `mtools`.

**With Limine:**

1. **A `limine.conf` instead of `grub.cfg`** (for example `iso/boot/limine/limine.conf`). Its format: a menu entry starts with `/` and its title; a `//` line is a sub-entry, so the Diagnostics menu becomes a directory entry with 27 sub-entries. Each entry has `protocol: multiboot1`, `path: boot():/boot/madire.kernel`, `cmdline: …`, and one `module_path:` plus `module_string:` per program. Global options include `timeout:` (GRUB's `set timeout=3`), `default_entry:` and `serial:`. The version keeps one source (`kernel/kernel.h`), as since 0.12.
2. **The ISO:** the files `limine-bios.sys`, `limine-bios-cd.bin` (and `limine-uefi-cd.bin` plus `BOOTX64.EFI` once UEFI comes) and `limine.conf` are copied into the ISO directory; `xorriso -as mkisofs … -b <limine-bios-cd.bin> -no-emul-boot -boot-load-size 4 -boot-info-table …` builds it; then `limine bios-install <iso>` makes it bootable from a disk image or USB stick too. `grub-mkrescue` goes.
3. **The `limine` host tool and binaries:** a pinned release (Limine has had major versions with changed configuration syntax, so the version is fixed and updated only deliberately). **Decided (D3):** the binary package `limine-binary.tar.xz` of v12.9.3, whose `.sig` is verified by hand once (0.13 stage 1) against the fingerprint `05D2 9860 D0A0 668A AEFB 9D69 1F3C 021B ECA2 3821` (the key that signed the tag `v12.9.3`; GitHub reports the tag as verified); `scripts/install-deps.sh` then checks only the **pinned SHA-256** on every fetch, and builds the host tool from the package's `limine.c`. GitHub lists `e02a473c6dc6df25159697f94930272397f21fd86e5d4eeee61bba72c95ec8d2` for that asset (GitHub's own figure: Limine publishes no checksum file); the pinned value is the one confirmed in stage 1. The release also has source tarballs (`limine-12.9.3.tar.{gz,bz2,xz}`), the binary package as `.tar.gz` and `.zip`, and a `.sig` for each. Whether Ubuntu 24.04 packages a usable version was not checked; the plan doesn't rely on it. The workflow's "Tool versions" step was changed on `main` (D6) so a missing `grub-mkrescue` or `limine` no longer fails it.
4. **Notices:** Limine's `COPYING` and `3RDPARTY.md` must go with its binaries (its own rule), so both go into the ISO and into a new `THIRD_PARTY_NOTICES` (D1; AGENTS.md section 11, item 5).
5. **GRUB packages removed** from `install-deps.sh` (xorriso stays). The Multiboot header check uses `grub-file` today and needs a replacement (below).
6. **Maybe a reproducible ISO:** GRUB's timestamps are why the ISO isn't reproducible (since 0.8). With a direct `xorriso` call the dates could perhaps be fixed. Worth trying in 0.13; not promised.

**The kernel, in principle unchanged** (Multiboot 1 with the a.out kludge stays), but three differences reach it:

- **The kernel command line (corrected in 0.13 stage 3a):** the first version said GRUB passes the kernel's path first (`/boot/madire.kernel keylog`); that was wrong. The 0.13 stage 1 A/B comparison read the Multiboot information of both boots: **GRUB 2.12 passes an empty command line** for the default entry (only what follows the kernel's path on the `multiboot` line, so `keylog` for the keylog entry), and Limine passes exactly `cmdline:` (or an empty string). The kernel matches whole words (`cmdline_word`), so the options work either way.
- **Module strings (corrected in 0.13 stage 3a):** likewise, **GRUB 2.12 passes module strings without the path** (`hello` for `module /boot/prog/hello.elf hello`), and Limine passes exactly `module_string:`, or an empty string when there is none. Every module therefore needs `module_string: <name>`; an empty string is refused cleanly as a bad name (the unit case `refused("")` in `test_progmod`, and a QEMU check since 0.13 stage 3a).
- **Where things are placed (corrected in 0.13 stage 0):** Limine puts the information block (the structure, the memory map, the command line, the module table and strings) at **`0x10000`**, as GRUB does, which lies inside the low 1 MiB the kernel reserves anyway, and loads the **modules right after the kernel image**, page-aligned, below 4 GiB. The kernel reads all of them through the physical view, which maps the boot-data reservations by address (`reserve.c`, `phys_view()`); the reservation list allows overlapping ranges, so nothing there changes. The free-page and reserved-range figures should match GRUB's or differ only slightly; the 0.13 stage 1 A/B comparison shows the exact differences. (The first version of this document said the modules go near 4 GiB and the structures into Limine's own allocations; that was wrong.)
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

Check names that contain "GRUB" would change too; renaming them keeps the per-tag counts but changes the PASS lists. **Decided (D4):** neutral names (for example "boot menu"), renamed in their own stage (0.13 stage 3b) with a full old-name → new-name table.

## 5. Risks

1. **Memory figures and placement (low; was medium before the 0.13 stage 0 correction):** the placement follows GRUB's (the information block at `0x10000`, modules after the kernel), so the figures should match or differ only slightly; the A/B comparison decides. If Limine's map or placement needs a change in a protected file (`mmap.c`, `reserve.c`, `multiboot.h`, `memory.c`), that needs its own justification and approval.
2. **Command-line and module-string formats (low):** see section 3; covered by unit tests.
3. **VirtualBox (medium, unproven here):** Limine's BIOS boot under VirtualBox on this laptop (which runs on the Windows Hypervisor Platform) hasn't been tried; the menu automation must be rewritten; text mode at the hand-off must be confirmed on the screen.
4. **Version churn and supply chain (medium):** a pinned version with a SHA-256 check; a deliberate update path; a change to the Actions build environment (approval).
5. **Licence (low; decided, D2):** Limine is BSD-2-Clause (`COPYING` at `v12.9.3`). Its binaries also contain parts under other licences, all listed in its `3RDPARTY.md`:
   - **In the BIOS binaries** (`limine-bios.sys`, `limine-bios-cd.bin`, `limine-bios-hdd.bin`): Apache-2.0 WITH LLVM-exception (cc-runtime), Apache-2.0 (PCG), MIT (the Multiboot 2 header, QOI, LAI, stb_image), BSD-2-Clause (Flanterm, libfdt), 0BSD (pdgzip, the freestanding C headers, the Limine protocol header) and CC0-1.0 (BLAKE2, OSDev Wiki code).
   - **In the `limine` host tool:** it embeds `limine-bios-hdd.bin`, so it carries cc-runtime, PCG, LAI, the stage-1 decoder and the A20 code.
   - **The stage-1 decoder** (in the BIOS boot sectors and the host tool) is a modified version of the Micro LZMA decoder, whose original is under **CC-BY-3.0**; Limine's modifications are BSD-2-Clause. Decided: covered by attribution through `3RDPARTY.md`, which goes into the ISO and `THIRD_PARTY_NOTICES`.
   - **Known item: unconfirmed licence.** The cursor-position code in Limine's `common/drivers/vga_textmode.c` (built into the BIOS binaries) comes from the OSDev Wiki before its 2011 relicensing to CC0; Limine's own `3RDPARTY.md` says it was "never confirmed to have been opted into CC0". Decided: accepted and recorded as a known item here and in `THIRD_PARTY_NOTICES`.
   - **0BSD and CC0-1.0** were added to the permitted licences in AGENTS.md section 11 (D2). There is no GPL, LGPL or APSL code.
6. **Behaviour at the hand-off (low):** x2APIC and IOMMUs are switched off. Madire uses the 8259 PIC and no IOMMU, so nothing changes today.

## 6. Effort, in stages (a proposal)

*Superseded by the approved plan, [docs/plans/0.13.md](../plans/0.13.md) (stages 0, 1, 2, 3a, 3b, 4 and 5). Kept as written in 0.12 stage 5.*

0. Approval of Limine as the boot tool (licence check, version, notices) and of the build-environment change; pin the version and its SHA-256.
1. **A/B first:** build a Limine ISO next to the GRUB ISO, from the same kernel, and compare the serial logs (`scripts/compare-serial.py`): memory map, reservations, module addresses, figures. Nothing is rewritten before the differences are known and reported.
2. Kernel-side differences, if any (module strings, placement), with unit tests; protected files only with approval.
3. The build: `limine.conf` (default entry, the keylog variant, the 27 Diagnostics), the Makefile's ISO rule with `xorriso` and `bios-install`, `ISO_PROGS`.
4. The tests: the changed checks above, each explained; `tests/expected-checks.txt`.
5. VirtualBox: the menu automation and the full acceptance.
6. Docs; GRUB removed from `install-deps.sh`; the AGENTS.md GPL exception ends.

Estimate: 5–6 stages, about 4–6 official Actions runs (15–20 minutes sharded each, about 50 minutes full), and one VirtualBox acceptance.

## 7. Recommendation

*Decided (2026-10-12): the owner approved the switch in 0.13, BIOS only and still Multiboot 1, with decisions D1–D7 recorded in [docs/plans/0.13.md](../plans/0.13.md). The text below is the 0.12 stage 5 recommendation as written.*

**Switch in 0.13, as its own small milestone, BIOS only and still with Multiboot 1**, so that only the bootloader changes: the kernel's boot protocol, the screen and the tests' facts stay the same. Start with the A/B comparison (stage 1) and stop to report it before rewriting any test. Limine's own protocol (framebuffer, all CPU cores, ACPI) and UEFI come later, with their own plans, on the modernisation track.

**Before the 0.13 plan, the owner decides:** Limine as the boot tool after the licence check; how it is fetched (release binaries or a source build, with a SHA-256 check); and whether the check names containing "GRUB" are renamed.

**The alternative:** keep GRUB until the UEFI milestone. That postpones the work, but keeps the GPL exception longer and means changing the bootloader and the firmware path at the same time later.
