# Roadmap

Each milestone must build, pass `make test`, boot in VirtualBox, and update the docs before the next one starts. [PROGRESS.md](PROGRESS.md) has the commit history, the tests at each milestone, the decision log and the longer-term direction.

| Milestone | Goal | Status |
|---|---|---|
| **0.1** | Bootable ISO: GRUB, 32-bit kernel, boot screen | done (booted in VirtualBox 2026-09-27) |
| **0.2** | CPU foundation: own GDT, IDT, exception handlers, crash screen; PIC remapped and masked; VGA/serial split into `drivers/`; `kprintf`; Madire M logo drawn in text mode | done (booted in VirtualBox 2026-09-27) |
| **0.3** | Memory foundation: validated Multiboot memory map, reservations (low 1 MiB, kernel, boot data, modules), bitmap physical page allocator, boot self-test, real RAM figures on screen | done (booted in VirtualBox 2026-09-28) |
| **0.4** | Hardware interrupts: IRQ layer (registration, mask/unmask, single EOI point); PIT timer at 100 Hz on IRQ0 (uptime, bounded waits); PS/2 keyboard on IRQ1 (set 1 decoder, modifiers, extended keys, 64-event queue); idle event loop; timer/keyboard/IRQ self-tests and diagnostics | done (booted in VirtualBox 2026-09-28) |
| **0.5** | Kernel console and basic command shell: scrolling 80×25 console with hardware cursor; line editor (insert, Backspace, Delete, Left/Right/Home/End, 71 characters); parser; `help`, `clear`, `version`, `uptime`, `mem`, `cpu` (CPUID), `echo`; pure modules unit-tested; console session tested through the real keyboard path | done (booted in VirtualBox 2026-09-28) |
| **0.6** | Paging (virtual memory foundation) and kernel heap: identity-mapped kernel with page 0 unmapped (NULL guard), kernel code read-only (CR0.WP), free RAM unmapped; page tables walk-checked before paging is enabled; recursive page-table access; map/unmap/translate with per-page TLB flushes; physical-access window; decoded page-fault crash line; `kmalloc`/`kcalloc`/`kfree`; `mem` shows paging and heap figures | done (booted in VirtualBox 2026-09-28) |
| **0.7** | 64-bit long mode, kernel still identity-mapped at 1 MiB: 32-bit boot stub (CPU checks, clear message and halt without long mode or PAE, temporary tables, switch to long mode); 64-bit GDT, TSS and IDT; 4-level paging, walk-checked before it is loaded; NX on everything except kernel code; IST1 double-fault stack and unmapped guard pages below the kernel and IST1 stacks; heap moved to `0xFFFFFE8000000000`; 64-bit crash screen; `pfnx` and `stackoverflow` diagnostics; boot-module integrity check; the 32-bit build dropped | done (booted in VirtualBox 2026-09-29) |
| **0.8** | Higher-half kernel: reproducible build (sorted link order, `make repro`); `.devcontainer/` proven on a fresh Codespace; kernel linked and run at `0xFFFFFFFF80000000` + physical (PML4 slot 511, `-mcmodel=kernel`); physical view at `0xFFFF800000000000` (slot 256) for the low 1 MiB and the boot data only; the lower half empty once the kernel's tables are loaded, proven by the walk-check, the self-test and the new `pflow` diagnostic; kernel stack high-water mark (`mem` row); boot-module result on the screen | done (booted in VirtualBox 2026-09-30) |
| **0.9** | Processes, part 1: kernel threads and a preemptive scheduler. Threads with their own 16 KiB stacks and unmapped guards (PML4 slot 507); context switches that keep RFLAGS; round robin with a 50 ms slice, preempting at the end of an interrupt; sleep, join, exit codes, detached threads; a preemption-disable count that makes the heap, frame allocator, paging and output safe between threads; the screen kept to thread 0; a scheduler self-test at every boot; `ps`; two new Diagnostics entries; scripted VirtualBox acceptance (`scripts/vbox-acceptance.ps1`) | done (booted in VirtualBox 2026-10-05) |
| **0.9.1** | Shell commands that need no new kernel foundations: `history` (the last 16 lines, Up/Down recall), `uname [-a]`, `hostname` (fixed), `date` (from the RTC, no time zone), `dmesg [lines]` (a 32 KiB kernel log), `reboot` (with a `yes` confirmation; keyboard-controller reset with a triple-fault fallback), `help <command>` and `<command> --help` for every command; the command standard `docs/commands.md` at MOCS v0.2 | done (booted in VirtualBox 2026-10-06) |
| **0.9.2** | Console scrollback: the last 1000 lines with their colours, Shift+PgUp / Shift+PgDn (24 lines a page), a reverse-colour indicator on the last row while scrolled back, `clear` keeping the screen in scrollback. Key-event logging off by default: only with the boot option `keylog` or during the keyboard diagnostics, before any password input exists; a binding secret-input rule for future prompts. `commands.md` at MOCS v0.3 | done (booted in VirtualBox 2026-10-06) |
| **0.10** | Processes, part 2: user processes. User mode (ring 3) with SMEP, SMAP and CR0.EM; one address space per process, the kernel half frozen at boot; `SYSCALL` with six Linux x86-64 system calls (`write`, `sched_yield`, `nanosleep`, `getpid`, `exit`, `exit_group`; for porting Linux applications, Chromium and Wine later), returning with `IRETQ`; `copy_from_user`; faults in a program contained to that program; 19 built-in test programs; a user-mode self-test at every boot; Diagnostics entries 16–26; the crash screen names SMEP/SMAP faults; the tests read the version once; `commands.md` at MOCS v0.5 | done (booted in VirtualBox 2026-10-09) |
| **0.11** | Processes, part 3: programs and process management. A strict ELF64 loader; programs delivered as boot modules (7 on the default GRUB entry) and loaded through one path; `run`, `run -b`, `kill` (asks first), `ps` with process numbers, Ctrl+C; program output on the screen through a queue drained by thread 0, with back-pressure; policy (b) (turns at most 5 ticks); `timer_ticks()` without `cli`/`sti`; a stall-capture test harness and `scripts/remote-regress.ps1`; `commands.md` at MOCS v0.6 | done (booted in VirtualBox 2026-10-10) |
| **0.12** | Shell improvements, no filesystem: "Did you mean" suggestions, help in sections, `prompt`, `alias`/`unalias`, `set`/`unset`/`export`/`env` (in memory), `sleep`, `presskey`, `time`, `kill -y`; bounded diagnostic waits; GitHub Actions as the official, sharded regression; an automated VirtualBox `shell` group; `commands.md` at MOCS v0.7 | done (tag `v0.12`, `879342e`; booted in VirtualBox 2026-10-12) |
| **0.13** | The bootloader: GRUB → Limine v12.9.3 (BIOS only, still Multiboot 1; [plans/0.13.md](plans/0.13.md)): the ISO built with `xorriso` and `limine bios-install` from the pinned binary package (SHA-256 checked); `limine.conf` with the default entry and the Diagnostics directory; the kernel unchanged except its version string; `scripts/check-multiboot.py` instead of `grub-file`; the checks renamed "boot menu"; GRUB removed from the build; the VirtualBox automation for Limine's menu; `THIRD_PARTY_NOTICES` | done (tag `v0.13`; booted in VirtualBox 2026-10-12) |
| **1.0** | The filesystem ([plans/1.0.md](plans/1.0.md)): a VFS layer and a RAM filesystem (1024 nodes, 2 MiB per file, a budget of up to 32 MiB of file pages), the **Live mode** (files in memory only, labelled on the console, in `version` and `df`); 21 new Linux x86-64 system calls for files and folders, `write` through descriptors (16 per process, 256 open files); the VFS lock (a FIFO ticket lock, holders with preemption off); System V program start-up (argc, argv, envp, auxv); the console's file commands `pwd`, `cd`, `ls`, `mkdir`, `rmdir`, `touch`, `stat`, `cat`, `rm`, `cp`, `mv`, `df`, `du` and `>`/`>>`; `help` on one screen; the kernel gate moved to 4 MiB; Madire's licence files at the ISO root; the VirtualBox `files` group; `commands.md` at MOCS v0.8 | done (tag `v1.0`; booted in VirtualBox 2026-10-17) |

The milestones after 0.12 are listed below under [Planned milestones](#planned-milestones-after-012).

**Test infrastructure milestone** (planned for when a full regression exceeds about 60 minutes; 0.11's runs took 46–53): test tiers (a quick set per stage, the full set before TESTED), consider a KVM-capable runner, and screenshot-comparison tests for graphics. **Done in 0.12:** the regression runs on GitHub Actions, sharded in parallel jobs (about 15–20 minutes); `scripts/remote-regress.ps1` downloads the per-boot serial logs; every diagnostic wait for ticks is bounded (the documented exceptions are in [PROGRESS.md](PROGRESS.md)). The local Kali VM is for builds and unit tests only ([AGENTS.md](../AGENTS.md)).

## Planned milestones after 0.12

In this order. 0.13 is done; the others are unscheduled. Each needs its own plan and the owner's approval. The references are in [references.md](references.md), each read under its rule (READ FULLY, STUDY ONLY or INSPIRATION ONLY).

1. **0.13: the bootloader, GRUB → Limine: done** (tag `v0.13`; [plans/0.13.md](plans/0.13.md), [evaluations/limine.md](evaluations/limine.md)). Reference: [Limine](references.md#ref-limine). Limine replaced GRUB as the only separate boot tool (AGENTS.md section 11, item 5).
2. **1.0: the filesystem: done** (tag `v1.0`; [plans/1.0.md](plans/1.0.md); what it delivered is in the table above and in [architecture.md](architecture.md), *Filesystem*). As planned: a VFS layer with a RAM filesystem first. A **Live mode**: booting from the ISO with a RAM filesystem, clearly labelled as Live, with no saving. New system calls use Linux numbers and behaviour from the start. Decided at the 1.0 planning: whether program start-up moves to the System V layout (argc, argv, envp, auxv; see the follow-ups in [PROGRESS.md](PROGRESS.md)), and how to handle the kernel size (the margin below 2 MiB fell from 164 KiB to 128 KiB during 0.12). References: [xv6](references.md#ref-xv6), [SerenityOS](references.md#ref-serenity), [Linux](references.md#ref-linux) (documents only).
3. **Graphics.** A design stage first: the **Madire Design Guide**, written clean-room from published human interface guidelines and screenshots. Then a framebuffer, fonts and text rendering, and the mouse.
   - **Desktop:** taskbar, start menu with app search, widgets, notifications, system tray, file manager, right-click menus.
   - **Settings categories:** Home, System, Display, Sound, Network, Bluetooth & devices, Apps, Accounts, Personalization, Time & language, Accessibility, Privacy & security (Madire Security Center), Updates, Developer.
   - References: [elementary](references.md#ref-elementary) (HIG), [KDE](references.md#ref-kde), [GNOME](references.md#ref-gnome), [XFCE](references.md#ref-xfce) and the other desktop entries, [Apple Open Source](references.md#ref-apple), [SerenityOS](references.md#ref-serenity), [Dunst](references.md#ref-dunst) (notifications), [Plank](references.md#ref-plank) (dock); icons: [Fluent UI System Icons](references.md#ref-fluent) (inspiration only) and [Papirus](references.md#ref-papirus) (study only, never shipped); Madire designs its own icon set.
   - References added in 1.0 (see [references.md](references.md), each under its rule): [LVGL](references.md#ref-lvgl) and [SDL](references.md#ref-sdl) (ideas; possible libraries only with approval), [ToaruOS](references.md#ref-toaruos) and SerenityOS's mouse and GUI code (ideas only), [Nano-X](references.md#ref-nanox) (study only), the [QEMU standard VGA](references.md#ref-qemu-vga) and [Bochs](references.md#ref-bochs) display interface and [Linux DRM](references.md#ref-linux-drm) (documentation only), the [OSDev PS/2 mouse page](references.md#ref-osdev-mouse) (ideas only); fonts: [Terminus](references.md#ref-terminus) as the default first bitmap font, [Spleen](references.md#ref-spleen) a candidate only, [Noto](references.md#ref-noto) and [Inter](references.md#ref-inter), rasterized by [stb_truetype](references.md#ref-stb) if approved; [Mesa](references.md#ref-mesa), [Vulkan](references.md#ref-vulkan) and [OpenXR](references.md#ref-openxr) for the far future.
4. **Storage:** a disk driver, a writable disk filesystem and partitioning; later the disk filesystem keeps files across reboots.
5. **Installer.**
   - First a **text installer**: erase-the-whole-disk only, with a typed confirmation, on **VirtualBox virtual disks only**. It creates a small FAT boot partition (Limine reads only FAT and ISO 9660) and a Madire filesystem partition.
   - Then real-hardware USB boot (after UEFI), Secure Boot later, a graphical installer, and dual boot next to Windows last.
   - Installer and disk-writing code only ever runs against virtual disks, never the development laptop's real disks ([AGENTS.md](../AGENTS.md)).
6. **Networking:** a virtual network card driver, TCP/IP, DNS, HTTP. The first stack candidate is [lwIP](references.md#ref-lwip) (BSD), used **only behind Madire's own Linux-compatible socket layer**: applications never call lwIP directly, so it can be replaced later.
7. **The Linux-compatible foundation** (the C library / POSIX milestone): system calls that match Linux exactly (numbers and behaviour), [musl](references.md#ref-musl) (MIT) as the C library, System V program start-up, and timeouts or non-blocking forms for `thread_join` and `process_wait`. Reference: [Linux](references.md#ref-linux) (man pages and the system-call table only).
8. **TLS:** [Mbed TLS](references.md#ref-mbedtls) (Apache-2.0), licence-checked, in `third_party/`.
9. **Audio and video playback:** a sound driver and decoders. H.264 needs patent licensing; DRM (Widevine) is a business matter, much later.
10. **The Madire Browser:** a text-mode browser (Madire's own), then [Ladybird](references.md#ref-ladybird), then a [Chromium](references.md#ref-chromium)-based Madire Browser. At that milestone: choose between a native Chromium port and running Linux Chrome through the compatibility layer. [WebKit](references.md#ref-webkit) and [NetSurf](references.md#ref-netsurf) are study only (LGPL/GPL).
11. **Linux apps** through the same compatibility layer, to fill the Madire Store.
12. **Long-term direction: Windows apps** through [Wine](references.md#ref-wine) running **on top of the Linux layer**. Wine is installed separately by users and is never part of Madire's code. Reference for Windows itself: Microsoft's public documentation ([ReactOS](references.md#ref-reactos) entry).

**The modernisation track continues alongside** (see below): UEFI (with Limine's own protocol: framebuffer, all CPU cores, ACPI), APIC, multi-core, USB, all RAM, Wi-Fi and Bluetooth; ARM64 later, for the phone version.

Order decided in 0.7 planning: 64-bit long mode (0.7), then the higher-half kernel (0.8), then processes and the filesystem (1.0). In 0.9 planning, processes were split in two: kernel threads and scheduling (0.9), then user processes (0.10); in 0.10 planning, programs and process management were split off into 0.11 (done 2026-10-10). The small 0.9.1 (shell commands) and 0.9.2 (scrollback, key logging off by default) were added afterwards, before 0.10.

Of the candidate items listed for 0.8, the reproducible build, the stack high-water mark and the boot-module result on screen were included. Making the `test_reserve` unit test independent of the test binary's load address was deferred to a later milestone.

## Boot experience, diagnostics and system information (planned, unscheduled; added after 0.13)

Each item needs its own plan and the owner's approval; none is scheduled.

1. **Boot branding (a small interim step, optional before 1.0).** Limine's colours in Madire blue, a "Madire OS" branding line and a shorter timeout, set in `limine.conf`. No layout change. Every boot menu check that changes is listed with its before and after.
2. **A branded boot with the Madire Design Guide (the graphics milestone).**
   - A Limine wallpaper with the Madire logo and title in the image (original artwork only).
   - The Diagnostics menu hidden behind a key, for developers, and regrouped (CPU, Memory, Scheduling, User mode, and later groups as their features arrive). The position checks and the VirtualBox automation are updated in the same stage.
   - Auto-boot for normal users.
   - Later: a Madire splash screen, then a login screen, then the desktop.
3. **Diagnostics grow with each milestone.** Storage tests with 1.0 and the disk driver; graphics tests with graphics; PCI scan and disk detection with the disk driver; network tests with networking; USB and multi-core tests with the modernisation track; larger stress tests (10,000 processes, memory pressure) at any time. No empty diagnostic group before its feature exists.
4. **A hardware information command:** CPU, RAM, boot loader, kernel version, uptime and the memory map; later PCI devices. The command standard already plans a name for this summary, `system` ([commands.md](commands.md): "a readable summary of the whole system: OS, version, CPU, memory, disks, devices, uptime"), and `devices` and `lspci` for hardware lists; no new command name is added without the owner's approval.
5. **Study-only references for boot and desktop design:** Plymouth (a boot splash system; GPL-2.0-or-later) and COSMIC (a desktop; its components are GPL-3.0, its toolkit `libcosmic` MPL-2.0). Both are **study only**: their design may be studied, no code, text or assets are copied (AGENTS.md section 11). They are added to [references.md](references.md), with their read rule, before they are first used.

## Later stages (direction only; planned, not committed)

- **Toolchain:** a dedicated `x86_64-elf` cross-compiler if the host compiler ever becomes a problem; CI build on every push (GitHub Actions); `test_reserve` independent of the test binary's load address
- **Memory:** 2 MiB pages for the kernel, a heap that can give memory back, faster `kfree` validation (today it checks the whole heap on every free), per-CPU or interrupt-safe allocation; use RAM above 4 GiB; reclaim ACPI-reclaimable memory after the ACPI tables are parsed; revisit the whole-low-1 MiB reservation when something needs low memory (e.g. starting other CPU cores)
- **Platform:** UEFI boot and an installer, framebuffer graphics (the logo's stroke geometry can then be drawn with smooth edges), SMP (multiple CPUs; real locks replacing the 0.9 preemption-disable count), Local APIC/IOAPIC instead of the 8259 PIC
- **Scheduling:** priorities, a configurable slice (shorter turns inside guarded sections came with policy (b) in 0.11)
- **Processes (after 0.11):** saving FPU/SSE state so programs can use it (CR0.EM is set in 0.10); threads inside a process, signals, `fork`/`exec`; returning with `SYSRET` where it is safe; growing user stacks; atomic user copies before any unmapping system call (the binding check-then-copy rule)
- **Hardware and drivers:** storage (ATA/AHCI/NVMe), disk filesystems, mouse, USB, network, Wi-Fi, Bluetooth, audio, sensors, all behind a Hardware Abstraction Layer
- **Console:** tab completion, word-aware wrapping of long output lines, history and scrollback kept across restarts (command history exists since 0.9.1, scrollback since 0.9.2)
- **Input and time:** keyboard LEDs, non-US layouts and input methods, Num Lock state; RTC interrupts, HPET or a calibrated TSC for finer timing
- **Security:** the Madire Security Platform (see [PROGRESS.md](PROGRESS.md))
- **User experience:** graphics and desktop, settings, accessibility, languages, power management, backup and recovery, built-in apps, system utilities
- **Apps and distribution:** application runtime, the Madire Browser, the Madire Store, update system
- **Later:** AI features, virtualisation and containers, speech, enterprise management, media codecs (licensing required)

Phones (likely ARM64) come after computers and laptops, which is why CPU-specific code stays under `kernel/arch/`.

## Modernisation track (planned, not committed to milestones yet)

### 1. Legacy pieces used today, and their modern replacements

| Today (simple, legacy) | Modern replacement (later) |
|---|---|
| GRUB, Multiboot 1 (until 0.12; since 0.13 Limine with Multiboot 1) | Limine's own boot protocol: framebuffer, all CPU cores, ACPI |
| BIOS boot | UEFI boot, plus Secure Boot |
| PIC interrupt controller (8259) | APIC / x2APIC |
| PIT timer | APIC timer / HPET / TSC |
| One CPU core | Multi-core (SMP) |
| VGA text mode (80×25) | A graphics framebuffer, then GPU acceleration |
| PS/2 keyboard | USB keyboards, mice and touchpads; Bluetooth |
| RAM below 4 GiB only | All RAM |
| No storage, no network | NVMe and SATA disks, Wi-Fi, Ethernet, Bluetooth |
| x86-64 only | ARM64 later, for the phone version |

These legacy pieces were chosen **deliberately, as the simplest starting point**: each is small, well documented and works in QEMU and VirtualBox without drivers for complex hardware, so the kernel's foundations could be built and tested one step at a time. They are temporary by design: each one must be replaced over time.

### 2. What makes Madire OS feel modern to its users (goals)

- A fast, polished graphical desktop, with touch and high-resolution screen support.
- App sandboxing and permissions (camera, microphone, location).
- Encryption by default (Madire Vault), Secure Boot, and automatic security updates.
- Power management for laptops (sleep, battery life).
- Madire AI built in, safely: it never changes the system without informing the user first.
- A Store with web apps, open-source apps and Madire's own apps from day one.

### 3. Open decision for the drivers phase

- Whether new drivers and components are written in **Rust** (for memory safety), alongside the existing C kernel. To be decided when the drivers phase is planned, not now.

## Applications and compatibility strategy (planned, not tied to milestones yet)

**Goal:** Madire OS runs most of the applications its users need: its own and native apps, plus four compatibility layers.

### 1. Compatibility layers

| Layer | How | Depends on |
|---|---|---|
| **Windows apps** | Wine (LGPL-2.1-or-later), with Proton-style improvements for games, running **on top of the Linux layer** to run Windows `.exe` and `.msi` programs. Users install Wine separately; it is never part of Madire's code (changed in 0.12 stage 5 from "port Wine") | Linux/POSIX compatibility (the next layer), graphics, audio |
| **Linux apps** | Run Linux programs through the Linux x86-64 system-call interface chosen in 0.10, extended over time | Processes and system calls (0.10), the filesystem, then more of the Linux interface |
| **Virtualisation** | Run a complete Windows or Linux guest inside Madire OS (with the user's own licence), for the apps nothing else can run | Hardware virtualisation support (Intel VT-x / AMD-V) |
| **Web apps** | The Madire Browser (Chromium-based) and installable web apps, such as Teams, Outlook, Gmail, Microsoft 365 online, WhatsApp and Zoom | Graphics, networking, the browser port (through the Linux layer) |

- **Known exception:** macOS-only apps can't be supported, because of Apple's licence terms and its private frameworks. Most macOS apps have Windows or web equivalents, which the other layers cover.
- **Design rule:** later milestones (processes, filesystem, graphics, audio, networking) must not make choices that block these layers. Any such conflict is raised before it is implemented.

### 2. Where applications come from, over time

| Stage | Sources |
|---|---|
| Early | Madire's own apps, open-source apps, web apps |
| Middle | The early sources, plus Linux apps and Windows apps through the compatibility layers |
| Mature | All of the above, plus commercial apps made for Madire OS |

### 3. Open-source policy

- Open source is the main early source of apps, for example Chromium, LibreOffice, VLC, Thunderbird, GIMP and development tools.
- Inside Madire OS itself, the code-origin rules of [AGENTS.md](../AGENTS.md) apply: Madire's own code is written from scratch, and only approved libraries with a permissive licence are included (in `third_party/`). Applications that users install are separate programs.
- Every project included needs a licence check and approval first. GPL source obligations and trademark rules (for example Firefox's) are respected.
- The browser and every other internet-facing app must receive regular security updates.
- Madire's identity lives in the system: the kernel, the desktop, the Madire Store, the security platform, Madire AI and the settings.

### 4. Licences for commercial software

- Users bring their own licences for commercial services (for example Microsoft 365) and for Windows in a virtual machine.
- Madire OS never redistributes a proprietary app without the vendor's agreement.

### 5. Madire OS's own licence (decided)

**Licence decided 12 Oct 2026: Apache 2.0 + trademark note, DCO** (see [LICENSE](../LICENSE)). The candidates were MIT, Apache-2.0, GPL-3.0 or a proprietary licence; it had to be decided before any outside code is included, because the choice decides which open-source code can be combined with the kernel and how.
