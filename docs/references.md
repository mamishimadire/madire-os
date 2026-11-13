# External references

These projects are **references, not part of Madire OS**. They may be read to learn how others solved a problem; **nothing from them is copied or closely reproduced** (code, text or assets). The binding code-origin rules are in [AGENTS.md](../AGENTS.md) (section 11): Madire's own code is written from scratch; a third-party library is allowed only with a permissive licence, the owner's explicit approval per library, its own folder `third_party/<name>/` with its licence file, and an entry in `THIRD_PARTY_NOTICES`. Madire OS is licensed under Apache 2.0 (see [LICENSE](../LICENSE)).

## Rules

1. **Read rules.** Every project below has one:
   - **READ FULLY** (permissive licence: MIT, BSD, Apache 2.0, zlib and the like): the source, documentation and design may be read in full, for ideas, architecture and design. Nothing is copied or closely reproduced. A project that is also a candidate library (musl, lwIP, mbedTLS) may enter Madire OS only as an approved library under AGENTS.md section 11, never as copied code.
   - **STUDY ONLY** (GPL, LGPL, APSL, or no licence): read the README, the licence and the design documentation only, **not the source code**. For Linux: the man pages, the `Documentation/` tree and the system-call table (`arch/x86/entry/syscalls/syscall_64.tbl`, for its numbers) only.
   - **INSPIRATION ONLY:** look at the published result (for example an icon set) for the feel; Madire makes its own, so that it doesn't look like the original.
2. To study a project, read specific files on the web, or shallow-clone it (`git clone --depth 1`) into a directory **outside** the repository (for example `/workspaces/references` in the Codespace). Never clone inside the repository.
3. Chromium and Gecko are not cloned (they are very large); browse them on the web.
4. The security tools at the end may be read as references only. They are **never run against any system**.
5. When a milestone plan uses one of these projects, it names the project, says what was studied and why, and states its licence.

**Verified on 2026-10-12 (0.12 stage 5).** Each link below was opened, and each licence was read from the project itself: from GitHub's licence field where it names one, otherwise from the licence file (`LICENSE`, `COPYING` or `COPYRIGHT`) or the project's own page. Where a link failed, the entry says so. A licence must be checked again before any use, since projects can change it.

**Entries added in 1.0 (stage 12) were verified the same way on 2026-10-16** (marked "verified 2026-10-16"). Two sites refused the automated check that day: GitLab instances behind a bot filter (`gitlab.freedesktop.org`, `gitlab.winehq.org`) and `wiki.osdev.org` (HTTP 403); those entries say where their licence was read instead. `github.com/thomasahle/sunfish` (a chess engine) and `github.com/notaz/mesa` (a fork) are not references for Madire OS and are deliberately not listed.

## Read fully (permissive licences)

| Project | Link | Licence (verified 2026-10-12) | Studied for | Roadmap |
|---|---|---|---|---|
| <a id="ref-xv6"></a>**xv6** (x86 version) | https://github.com/mit-pdos/xv6-public | MIT (the `LICENSE` text; GitHub names no licence). The README says the x86 version is **no longer maintained**; the current one is `xv6-riscv` (below) | A small, complete Unix-like kernel: processes, `exec`, a file system with a log, pipes, system-call dispatch on x86 | 1.0 (file system), the C-library milestone |
| <a id="ref-serenity"></a>**SerenityOS** | https://github.com/SerenityOS/serenity | BSD-2-Clause | A from-scratch OS with a kernel, POSIX-like system calls, a C library, a GUI toolkit and a desktop. **For the mouse and GUI (ideas only, checked 2026-10-16):** the input devices in `Kernel/Devices/Input` (PS/2 mouse and keyboard; there is no `Kernel/Devices/HID`), the toolkit `Userland/Libraries/LibGUI` and the compositor `Userland/Services/WindowServer` | 1.0, graphics and desktop, the C-library milestone |
| <a id="ref-toaruos"></a>**ToaruOS** | https://github.com/klange/toaruos | NCSA (GitHub; verified 2026-10-16). **Reclassified in 1.0** from the "Other references" list below: ideas only, nothing copied | A small from-scratch x86-64 OS: kernel, compositor, userland; thread and process structures (0.9), its window system and mouse handling (graphics) | Graphics and desktop |
| <a id="ref-lvgl"></a>**LVGL** | https://github.com/lvgl/lvgl | MIT (GitHub; verified 2026-10-16) | A small embedded GUI library: widgets, styles, input devices on a framebuffer. Ideas; **a possible library only if the owner approves it** (AGENTS.md section 11, item 2) | Graphics |
| <a id="ref-mesa"></a>**Mesa** | https://gitlab.freedesktop.org/mesa/mesa | Mostly MIT: "The core Mesa library is licensed according to the terms of the MIT license" (https://docs.mesa3d.org/license.html, verified 2026-10-16); the GLX client code under SGI Free Software License B, some drivers and files under their own licences (each file's SPDX identifier). **The GitLab page refused the automated check** (bot filter), so the licence was read from Mesa's own documentation | How OpenGL and Vulkan drivers are built on a kernel's graphics interface; ideas and documentation only | Graphics (far future) |
| <a id="ref-sdl"></a>**SDL** | https://github.com/libsdl-org/SDL | Zlib (GitHub; verified 2026-10-16 and 2026-11-12) | A portable layer for windows, input, audio and timers that games use; what such a layer needs from the OS. Ideas; **a possible library only if approved** | Graphics, audio, games as separate applications |
| <a id="ref-stb"></a>**stb_truetype** (in stb) | https://github.com/nothings/stb (`stb_truetype.h`) | Dual: MIT or the Unlicense (public domain), the user's choice (`LICENSE`; GitHub names none; verified 2026-10-16) | A TrueType rasterizer; **the candidate rasterizer** (a library, with approval). **If it is ever used, only fonts shipped with Madire OS are loaded:** its own header warns "NO SECURITY GUARANTEE -- DO NOT USE THIS ON UNTRUSTED FONT FILES" | Graphics (fonts) |
| <a id="ref-vulkan"></a>**Vulkan-Samples** | https://github.com/KhronosGroup/Vulkan-Samples | Apache-2.0 (GitHub; verified 2026-10-16) | Learning Vulkan; far future | Graphics (far future) |
| **Vulkan-Loader** | https://github.com/KhronosGroup/Vulkan-Loader | Mostly Apache-2.0 (`LICENSE.txt`: "The majority of files in this project use the Apache 2.0 License … Any license deviations from Apache 2.0 are 'more permissive' licenses"; verified 2026-10-16) | How Vulkan drivers are found and loaded; learning, far future | Graphics (far future) |
| <a id="ref-openxr"></a>**OpenXR SDK** | https://github.com/KhronosGroup/OpenXR-SDK | Apache-2.0 (GitHub; verified 2026-10-16) | The standard VR/AR interface; far future | VR (far future) |
| <a id="ref-tinyusb"></a>**TinyUSB** | https://github.com/hathach/tinyusb | MIT (GitHub; verified 2026-10-16) | A small USB stack: host and device classes. Ideas, or **a possible library for the USB milestone if approved** | USB (modernisation track) |
| <a id="ref-musl"></a>**musl** | https://musl.libc.org/ (official git: https://git.musl-libc.org/cgit/musl) | MIT (`COPYRIGHT`: "musl as a whole is licensed under the following standard MIT license"). **The proposed mirror `https://github.com/bminor/musl` returned 404 (2026-10-12)**, so it isn't listed; the official git is used instead | A C library on the Linux system-call interface; **allowed as a library** (with approval, in `third_party/musl/`) | The C-library milestone |
| <a id="ref-lwip"></a>**lwIP** | https://github.com/lwip-tcpip/lwip (a mirror of Savannah; official: https://savannah.nongnu.org/projects/lwip/) | BSD-3-Clause (`COPYING`; verified 2026-11-12) | A small TCP/IP stack; the first candidate for networking, **only behind Madire's own Linux-compatible socket layer** | Networking |
| <a id="ref-ladybird"></a>**Ladybird** | https://github.com/LadybirdBrowser/ladybird | BSD-2-Clause | An independent browser engine; what a browser needs from the OS | Madire Browser |
| <a id="ref-chromium"></a>**Chromium** | https://chromium.googlesource.com/chromium/src | BSD-3-Clause (`LICENSE`); its `third_party/` components keep their own licences, to be checked one by one | The Chromium-based Madire Browser: what it needs from the OS (browse, never clone) | Madire Browser |
| <a id="ref-mbedtls"></a>**Mbed TLS** | https://github.com/Mbed-TLS/mbedtls | Dual **Apache-2.0 OR GPL-2.0-or-later** (`LICENSE`); Madire would use it under Apache-2.0 | TLS; a candidate library in `third_party/mbedtls/` (with approval) | TLS |
| <a id="ref-limine"></a>**Limine** | https://github.com/limine-bootloader/limine (redirects to `github.com/Limine-Bootloader/Limine`) | BSD-2-Clause (`COPYING`); its bundled parts are listed in its `3RDPARTY.md` (see [evaluations/limine.md](evaluations/limine.md)). **Since 0.13 also used as a tool:** the unmodified binary package of v12.9.3, fetched at build time with a pinned SHA-256 (AGENTS.md section 11, item 5); being used doesn't change its READ FULLY rule: its code is still never copied into Madire OS. **Boot branding (study only, added 2026-10-16):** its configuration documentation, https://github.com/Limine-Bootloader/Limine/blob/v12.x/CONFIG.md, describes `interface_branding` (and its colour) and `wallpaper` / `wallpaper_style` | The bootloader to replace GRUB; later its own boot protocol (framebuffer, all CPU cores, ACPI); the menu's branding options for the boot-branding step | 0.13, UEFI, boot branding |
| <a id="ref-dunst"></a>**Dunst** | https://github.com/dunst-project/dunst | BSD-3-Clause (`LICENSE`; GitHub names no licence; verified 2026-11-12) | A notification daemon: notification queues, timeouts, urgency, grouping | Desktop (notifications) |

## Study only (README, licence and design documents; not the source)

| Project | Link | Licence (verified 2026-10-12) | Studied for | Roadmap |
|---|---|---|---|---|
| <a id="ref-linux"></a>**Linux** | https://github.com/torvalds/linux; man pages: https://man7.org/linux/man-pages/ | GPL-2.0 WITH Linux-syscall-note (`COPYING`). **man7.org refused the connection from the verification host on 2026-10-12** (status unverified; the man-pages project itself is kept at kernel.org, `pub/scm/docs/man-pages/man-pages.git`) | The Linux system-call numbers and behaviour (Madire's system calls match Linux), from the man pages, `Documentation/` and the system-call table only | 1.0, the C-library milestone, Linux apps |
| <a id="ref-wine"></a>**Wine** | https://github.com/wine-mirror/wine (a mirror; official: https://gitlab.winehq.org/wine/wine) | LGPL-2.1-or-later (`LICENSE`) | How Windows programs run on top of a POSIX/Linux layer; Wine is **installed separately by users**, never part of Madire's code. **A separate application only: never code in this repository** (restated 1.0) | Windows apps (long term) |
| <a id="ref-linux-drm"></a>**Linux DRM** (the GPU driver developer's guide) | https://docs.kernel.org/gpu/index.html | GPL-2.0 (part of the kernel's documentation; verified 2026-10-16) | How a kernel exposes displays and GPUs (DRM/KMS) to user space. **Documentation only, no code** | Graphics |
| <a id="ref-bochs"></a>**Bochs** | https://bochs.sourceforge.io/ (source: https://github.com/bochs-emu/Bochs) | LGPL-2.1 (GitHub; the home page names no licence; verified 2026-10-16). No document of its own for its display interface was found; QEMU's (below) describes it | An x86 PC emulator; its VBE "dispi" display interface, which QEMU's standard VGA also offers | Graphics (first framebuffer) |
| <a id="ref-qemu-vga"></a>**QEMU standard VGA** | https://www.qemu.org/docs/master/specs/standard-vga.html | The manual is GPL-2.0 ("QEMU and this manual are released under the GNU General Public License, version 2"; verified 2026-10-16) | The Bochs display interface: its I/O ports (`0x1CE`–`0x1D0`) and MMIO registers, for a first framebuffer in QEMU and VirtualBox. **Documentation only** (facts: ports, registers, values) | Graphics (first framebuffer) |
| <a id="ref-nanox"></a>**Nano-X / Microwindows** | https://www.microwindows.org/ (source: https://github.com/ghaerr/microwindows) | MPL-1.1, with the option to convert to GPL-2.0 (`LICENSE`; verified 2026-10-16). MPL is not on the permitted list (AGENTS.md section 11), so **study only** | A small windowing system for embedded devices: its design, as background | Graphics and desktop |
| <a id="ref-libusb"></a>**libusb** | https://github.com/libusb/libusb | LGPL-2.1 (GitHub; verified 2026-10-16) | A user-space USB access library: what applications expect from a USB interface | USB |
| <a id="ref-openal"></a>**OpenAL Soft** | https://github.com/kcat/openal-soft | LGPL (`COPYING`: the GNU Library GPL version 2; GitHub names none; verified 2026-10-16) | A 3D audio library that games use: what it needs from an audio interface | Audio, games as separate applications |
| <a id="ref-catppuccin-limine"></a>**Catppuccin for Limine** | https://github.com/catppuccin/limine | MIT (GitHub; verified 2026-10-16). Listed as **study only by the owner's decision** (a styling reference, not a source of files) | How a Limine menu theme is set: colours, branding, wallpaper; for the boot-branding step. Madire makes its own | Boot branding |
| <a id="ref-reactos"></a>**ReactOS** | https://github.com/reactos/reactos; Windows reference: https://learn.microsoft.com/ | GPL-2.0 (GitHub). **Microsoft's public documentation is the main Windows reference; leaked Windows sources are never used** | The structure of a Windows-compatible system (documents only) | Windows apps (long term) |
| <a id="ref-webkit"></a>**WebKit** | https://github.com/WebKit/WebKit | Mixed: LGPL-2/LGPL-2.1 and a BSD-style Apple licence (`Source/WebCore/LICENSE-LGPL-2`, `LICENSE-LGPL-2.1`, `LICENSE-APPLE`; `Source/JavaScriptCore/COPYING.LIB`); GitHub names no single licence | Browser-engine architecture, as background | Madire Browser |
| <a id="ref-netsurf"></a>**NetSurf** | https://www.netsurf-browser.org/ | GPL-2.0 (stated on the project's page) | A small browser with its own layout engine, as background for a text-mode or small browser | Madire Browser (text mode) |
| <a id="ref-apple"></a>**Apple Open Source** | https://opensource.apple.com/ (XNU: https://github.com/apple-oss-distributions/xnu) | APSL 2.0 (`APPLE_LICENSE` of XNU); other projects per repository | How a desktop OS is structured: Darwin/XNU, WebKit | Graphics and desktop, as background |
| <a id="ref-kde"></a>**KDE Plasma Desktop** | https://github.com/KDE/plasma-desktop | Per file (REUSE `LICENSES/`: mainly GPL and LGPL, some BSD, MIT, CC0) | Desktop shell, taskbar, launcher | Desktop |
| **KDE Plasma Workspace** | https://github.com/KDE/plasma-workspace | Per file (REUSE `LICENSES/`, mainly GPL and LGPL) | Panels, notifications, system tray | Desktop |
| **KWin** | https://github.com/KDE/kwin | Per file (REUSE `LICENSES/`, mainly GPL and LGPL) | A window manager and compositor | Graphics and desktop |
| **Dolphin** | https://github.com/KDE/dolphin | Per file (REUSE `LICENSES/`, mainly GPL and LGPL) | A file manager | Desktop (file manager) |
| **KDE System Settings** | https://github.com/KDE/systemsettings | Per file (REUSE `LICENSES/`, mainly GPL and LGPL) | How settings are grouped and searched | Settings |
| <a id="ref-gnome"></a>**GNOME Shell** | https://github.com/GNOME/gnome-shell | GPL-2.0 (GitHub) | A desktop shell, its overview and search | Desktop |
| **Nautilus** | https://github.com/GNOME/nautilus | GPL-3.0 (GitHub; verified 2026-11-12) | A file manager (study only: its README and design; feature ideas for the Files app) | Desktop (file manager) |
| <a id="ref-elementary"></a>**elementary** (Gala, Granite, AppCenter, HIG) | https://github.com/elementary | Gala GPL-3.0, Granite LGPL-3.0, AppCenter GPL-3.0 (GitHub); the HIG (https://github.com/elementary/hig) has **no licence file**, so it is read as guidance only | A consistent design language and human interface guidelines; an app centre | Graphics (design stage), Madire Store |
| <a id="ref-plank"></a>**Plank** | https://github.com/elementary/plank | **Returned 404 (2026-10-12): the link is dead.** A search finds the project on Launchpad (https://launchpad.net/plank) and at https://github.com/ricotz/plank (GPL-3.0, last change 2024-07-16; not confirmed as the official home) | A simple dock | Desktop (taskbar, as background) |
| <a id="ref-xfce"></a>**XFCE** | https://gitlab.xfce.org/xfce | Per project (the group page loads: "Xfce core components and applications"; the projects' licences are not verified one by one) | A light desktop: panel, window manager, settings | Desktop |
| **Whisker Menu** | https://github.com/gottcode/xfce4-whiskermenu-plugin | GPL-2.0 (GitHub) | A start menu with search | Desktop (start menu) |
| **PCManFM** | https://github.com/lxde/pcmanfm | GPL-2.0 (GitHub) | A light file manager | Desktop (file manager) |
| **Openbox** | https://github.com/danakj/openbox | GPL-2.0 (GitHub) | A small window manager, right-click menus | Desktop (right-click menus) |
| <a id="ref-papirus"></a>**Papirus icons** | https://github.com/PapirusDevelopmentTeam/papirus-icon-theme | GPL-3.0 (GitHub). **Never shipped** in Madire OS | How a complete icon theme is organised | Graphics (icons) |

## Inspiration only

| Project | Link | Licence (verified 2026-10-12) | Used for | Roadmap |
|---|---|---|---|---|
| <a id="ref-fluent"></a>**Fluent UI System Icons** | https://github.com/microsoft/fluentui-system-icons | MIT (GitHub; verified 2026-11-12) | The feel of a modern icon set only. **Madire designs its own icon set**, so that it doesn't look like Windows | Graphics (icons) |

## Specifications and learning (added in 1.0)

Read for facts and ideas. Nothing from them is copied into Madire OS.

| Reference | Link | Licence (verified 2026-10-16) | Used for | Roadmap |
|---|---|---|---|---|
| <a id="ref-virtio"></a>**VIRTIO specification** | https://docs.oasis-open.org/virtio/virtio/v1.3/virtio-v1.3.html (repository: https://github.com/oasis-tcs/virtio-spec) | OASIS copyright, under OASIS's Non-Assertion Mode IPR policy (the document's notices). Version 1.3 is a Committee Specification Draft (CSD01, 2023-10-06), not yet an OASIS Standard | Virtual devices (disk, network, GPU, input) in QEMU and other virtual machines. **Documentation only** | Storage, networking, graphics |
| <a id="ref-osdev-mouse"></a>**OSDev wiki: PS/2 Mouse** | https://wiki.osdev.org/PS/2_Mouse | **Mixed:** content added since 2011 must be compatible with the public domain (CC0); older content may not be. **The site answered HTTP 403** to the automated check, so the status comes from search results quoting its licence page (https://wiki.osdev.org/OSDev_Wiki:License) and its forum. **Ideas only**; nothing quoted | The PS/2 mouse protocol (packets, initialisation) for the first mouse driver | Graphics (the mouse) |
| <a id="ref-multiboot1"></a>**Multiboot Specification, version 0.6.96** (Multiboot 1; added in 1.2) | https://www.gnu.org/software/grub/manual/multiboot/multiboot.html | The document's own notice (verified 2026-10-23): permission to make and distribute verbatim copies, and modified versions under an identical notice; © 1995–96 Bryan Ford and Erich Stefan Boleyn, © 1999–2010 Free Software Foundation. Not a licence for code; its example header `multiboot.h` carries its own MIT-style notice. **Facts only**: nothing copied or closely reproduced | The header (magic, flags, the a.out kludge fields, the graphics fields at 32–44 enabled by flag bit 2) and the boot information (memory map, modules, command line, the framebuffer fields of flag bit 12, types 0/1/2). Its table gives the colour fields at byte 110; the 1.2 stage 3 probe, Limine's `common/protos/multiboot1.h` and the document's own example header put them at 112 | Boot (since 0.1); 1.2 graphics |
| <a id="ref-bdf"></a>**Glyph Bitmap Distribution Format (BDF) Specification, version 2.2** (added in 1.2) | https://adobe-type-tools.github.io/font-tech-notes/pdfs/5005.BDF_Spec.pdf | Adobe's copyright notice (verified 2026-10-23): "Copyright 1987–1989, 1992-1993 by Adobe Systems Incorporated. All rights reserved"; no part may be reproduced. **Facts only**: nothing copied or closely reproduced | The file format read by `scripts/make-font.py` and `tests/fbcon_ref.py` (1.2 stage 6): `FONTBOUNDINGBOX` (width, height, the lower-left offset), `ENCODING`, `BBX`, and `BITMAP` (h lines of hex, each padded on the right with zeros to a whole byte, so the leftmost pixel is the top bit) | 1.2 graphics (fonts) |
| <a id="ref-cp437"></a>**Code page 437** (Wikipedia; added in 1.2) | https://en.wikipedia.org/wiki/Code_page_437 | CC BY-SA 4.0 (Wikipedia's licence for its text; the page itself was read on 2026-10-23, its licence footer not re-checked). **Facts only**: the mapping, not the text | Checked against it (1.2 stage 6): the Unicode code points of the graphic characters 0x01–0x1F and 0x7F (U+263A … U+25BC, U+2302), and 0x00 and 0xFF drawn blank. 0x20–0xFE come from Python's own `cp437` codec in both `scripts/make-font.py` and `tests/fbcon_ref.py` | 1.2 graphics (console) |
| <a id="ref-vkguide"></a>**Vulkan Guide** | https://vkguide.dev/ (source: https://github.com/vblanco20-1/vulkan-guide) | **Not confirmed.** The site says "Distributed by a MIT license", but that link and the repository's `LICENSE.txt` are the licence of its documentation theme (just-the-docs, by Patrick Marsceill); the licence of the guide's text and code is not stated | **Learning only** | Graphics (far future) |

## <a id="ref-filesystems"></a>Filesystems: reading only (added in 1.1 stage 9b)

For the storage milestone (likely v1.3, after graphics in v1.2) and its decision between Madire's own disk filesystem and ext2 ([roadmap.md](roadmap.md)). **Reading only:** facts and ideas; nothing is copied, quoted or closely reproduced, and an ext2 implementation would be written from the specification alone, never from GPL code. Verified on 2026-10-22 the same way as the entries above.

| Reference | Link | Licence (verified 2026-10-22) | Used for | Roadmap |
|---|---|---|---|---|
| <a id="ref-xv6-book"></a>**xv6 book**, chapter 8 "File system" (Cox, Kaashoek, Morris; edition of 2024-08-31) | https://pdos.csail.mit.edu/6.1810/2024/xv6/book-riscv-rev4.pdf (from https://pdos.csail.mit.edu/6.1810/2024/xv6.html) | **No licence stated in the book.** The course page's footer shows a CC BY 3.0 US badge, but the page doesn't say that it covers the book. Reading only | The layers of a small disk filesystem: buffer cache, logging (crash safety), block allocator, inodes, folders, path names, the descriptor layer | The storage milestone |
| <a id="ref-osdev-ext2"></a>**OSDev Wiki: Ext2** | https://wiki.osdev.org/Ext2 | **Mixed** (as the OSDev wiki entry above: content since 2011 public-domain compatible, older content not necessarily). **The site answered HTTP 403** to the automated check again; not re-read. Ideas only; nothing quoted | The ext2 on-disk format: superblock, block groups, inodes, folders; a starting point for the specification | The storage milestone, the ext2 decision |
| <a id="ref-osdev-notes"></a>**OSDev Notes**, the VFS chapter | https://github.com/dreamportdev/Osdev-Notes (`08_VirtualFileSystem/02_VirtualFileSystem.md`) | **CC BY-NC 4.0** (the README and `LICENSE.md`; GitHub names no licence). Non-commercial, not on any permitted list: **reading only**, nothing quoted | How a VFS layer joins several filesystems (mount points, the node interface), as a comparison with Madire's 1.0 VFS | The storage milestone |
| <a id="ref-littlefs"></a>**littlefs** | https://github.com/littlefs-project/littlefs | BSD-3-Clause (GitHub) | A small filesystem for flash memory that survives power loss (copy-on-write, wear levelling). **Only if flash storage is ever targeted** | Storage (only for flash) |

## <a id="ref-storage"></a>Storage: specifications and test tools (added in 1.4 stage 0)

For the storage milestone ([plans/1.4.md](plans/1.4.md)): the disk controller, partitions and ext2. **ext2 is written from its specification only** (AGENTS.md section 11): the documents below are read for facts (field offsets, sizes, flag values, command codes); nothing is copied, quoted or closely reproduced, and no source code of any filesystem or driver is read. Verified on 2026-10-31.

| Reference | Link | Licence (verified 2026-10-31) | Read rule | Used for |
|---|---|---|---|---|
| <a id="ref-ext2-doc"></a>**The Second Extended File System: Internal Layout** (Dave Poirier) | https://www.nongnu.org/ext2-doc/ext2.html (PDF: https://www.nongnu.org/ext2-doc/ext2.pdf) | GNU Free Documentation License, version 1.1 or any later version, no Invariant Sections or Cover Texts (the HTML's notice: "Copyright © 2001-2002 by Dave Poirier"; read from the GWDG mirror of the same page, since nongnu.org answered HTTP 429; the PDF is said to carry 2001-2019). The FDL is not on AGENTS.md section 11's lists: **facts only, nothing quoted** | Specification (facts only) | The main ext2 specification: superblock, block group descriptors, bitmaps, inode table, inodes and their block pointers (direct, single, double, triple indirect), directory entries (`rec_len`, `name_len`, `file_type`), the revision 0 and 1 values, the feature flags |
| <a id="ref-linux-ext2-docs"></a>**Linux kernel documentation: `filesystems/ext2`** and the on-disk layout chapter of `filesystems/ext4` | https://docs.kernel.org/filesystems/ext2.html, https://docs.kernel.org/filesystems/ext4/index.html | Part of the Linux kernel's documentation (GPL-2.0 kernel tree; the page's footer: "©The kernel development community"); see [Linux](#ref-linux) | **STUDY ONLY** (documentation; never the source code under `fs/`) | A second reading of the feature-flag rules (an unknown INCOMPAT flag: no mount; an unknown RO_COMPAT flag: read-only), the clean and error states, the reserved inodes, the field list of the superblock and the inode |
| <a id="ref-ahci"></a>**Serial ATA AHCI Specification, revision 1.3.1** (Intel) | https://www.intel.com/content/www/us/en/io/serial-ata/serial-ata-ahci-spec-rev1-3-1.html | © Intel Corporation; a public download page with no specification-specific licence (the site's general terms of use apply). **Facts only** | Specification (facts only) | The AHCI controller: its registers (CAP, GHC, PI, the port registers PxCLB, PxFB, PxIS, PxCMD, PxTFD, PxSIG, PxSSTS, PxSERR, PxCI), the command list, the received-FIS area, the command table and its physical region descriptors, port start and stop, the error recovery steps |
| <a id="ref-ata-acs"></a>**ATA/ATAPI Command Set (ACS-3), T13/2161-D** (working drafts; the standard is ANSI INCITS 522-2014) | https://www.t13.org/ (document archive; ACS-3 draft d2161) | T13/INCITS copyright; working drafts published by T13, the final standard sold by ANSI. **Facts only** | Specification (facts only) | The commands Madire uses: IDENTIFY DEVICE (0xEC: the model string, the 48-bit sector count, the logical sector size), READ DMA EXT (0x25), WRITE DMA EXT (0x35), FLUSH CACHE EXT (0xEA); the status and error bits |
| <a id="ref-uefi-gpt"></a>**UEFI Specification 2.11**, chapter 5 "GUID Partition Table (GPT) Disk Layout" (with the legacy MBR description) | https://uefi.org/specs/UEFI/2.11/ | © the UEFI Forum; published on uefi.org (its pages refused the automated check with HTTP 403 on 2026-10-31; the chapter's title and number were read from search results and Wikipedia's GPT article). **Facts only** | Specification (facts only) | The MBR partition table (four entries, the 0x55AA signature, the protective MBR's type 0xEE), the GPT header and its CRC32s, the partition entries (type GUID, name in UTF-16LE) |
| **PCI configuration space** (the configuration mechanism through ports 0xCF8/0xCFC; header type 0; class codes; BAR5) | [OSDev wiki](https://wiki.osdev.org) (see *Other references*); the PCI-SIG's specifications are for members | OSDev: mixed (see above); **ideas only, nothing quoted** | Ideas only | Bus, device and function numbers, the vendor, device and class fields, the command register (memory space, bus master, interrupt disable), BAR decoding, PCI-to-PCI bridges' secondary bus |

**Test tools (programs run on the test machines only; never in the repository or the ISO).** Approved by the owner in 1.4 stage 0 (the design report's question 2), with the same status as QEMU, `xorriso` and `mtools`: unmodified programs installed by the operating system's own packages on the GitHub Actions runner or on the local Kali VM, called by the test scripts. Their source code is **STUDY ONLY** and is never read; only their manual pages and documented options are used.

| Tool | Where | Licence (verified 2026-10-31) | Used for |
|---|---|---|---|
| <a id="ref-e2fsprogs"></a>**e2fsprogs** (`mke2fs`, `e2fsck`, `debugfs`) | https://e2fsprogs.sourceforge.net/ (the project's site; refused an automated check with 403 on 2026-11-12, a bot block), the repository https://git.kernel.org/pub/scm/fs/ext2/e2fsprogs.git (opened 2026-11-12) and its mirror https://github.com/tytso/e2fsprogs (the Ubuntu and Kali packages `e2fsprogs`) | The programs GPL-2.0; `lib/ext2fs` and `lib/e2p` LGPL-2.0; `lib/uuid` BSD-style; `lib/et` and `lib/ss` MIT-style (its `NOTICE` file; verified 2026-11-12). **STUDY ONLY** (manual pages only, `e2fsck.conf(5)` included; the source is never read) | The independent check of Madire's ext2 (decision D13, option A): `mke2fs` makes the test images Madire reads; `e2fsck -fn` checks every image Madire writes; `debugfs` lists names, sizes and contents |
| **util-linux `sfdisk`** | https://github.com/util-linux/util-linux | Per file; GPL-2.0-or-later is the default for code without its own licence (`README.licensing`). **STUDY ONLY** (manual page only) | If present on the runner (the stage 2 probe): a cross-check of the partition tables the test helper writes (`sfdisk --dump`) |
| **`qemu-img`** (QEMU) | https://www.qemu.org/ | QEMU: GPL-2.0, parts under their own licences (its `LICENSE`, per the QEMU documentation's licence page). **STUDY ONLY** | On Kali only: converting the VirtualBox disk saved by the `storage` group from VDI to raw, for `e2fsck` and `debugfs` (owner-run) |

## Fonts (assets; AGENTS.md section 11, item 6)

Fonts are assets: by default only original work, CC0, public domain, CC-BY or the SIL OFL. **Since 1.2 stage 6 one is shipped: Terminus** (decision 8 of [plans/1.2.md](plans/1.2.md)), listed in [THIRD_PARTY_ASSETS.md](../THIRD_PARTY_ASSETS.md) (created then).

| Font | Link | Licence (verified 2026-10-16) | Status |
|---|---|---|---|
| <a id="ref-terminus"></a>**Terminus** (bitmap) | https://terminus-font.sourceforge.net/ (files: https://sourceforge.net/projects/terminus-font/files/terminus-font-4.49/) | SIL OFL 1.1 (the project's page: "Terminus Font is licensed under the SIL Open Font License, Version 1.1"; **checked again on 2026-10-23 in the release package**: its `OFL.TXT`, its `README` section 7.1, and every BDF file's `NOTICE` property; the package's configure files and Python and JavaScript sources are GPL-2.0-or-later and are not used) | **Shipped since 1.2 stage 6** (decision 8): version **4.49.1** (`terminus-font-4.49.1.tar.gz`, SHA-256 `d961c1b7…ef79`, equal to SourceForge's published hash). Kept in `third_party/terminus/`: `ter-u16n.bdf`, `ter-u16b.bdf` (8×16), `ter-u20n.bdf`, `ter-u20b.bdf` (10×20), `ter-u24n.bdf`, `ter-u24b.bdf` (12×24), `ter-u32n.bdf`, `ter-u32b.bdf` (16×32), regular and bold, unmodified, and `OFL.txt`; each file's SHA-256 in `third_party/terminus/README.md`. Converted to C at build time by Madire's own `scripts/make-font.py`; never called "Terminus Font" in the interface (a Modified Version under the OFL) |
| <a id="ref-spleen"></a>**Spleen** (bitmap) | https://github.com/fcambus/spleen | BSD-2-Clause (GitHub) | **Excluded** (decision 9 of [plans/1.2.md](plans/1.2.md)): BSD-2-Clause is not on the asset list of AGENTS.md section 11, item 6, and the rule stays unchanged; Terminus is used instead |
| <a id="ref-noto"></a>**Noto Sans, Noto Sans Mono** | https://github.com/notofonts/latin-greek-cyrillic | SIL OFL 1.1 (`OFL.txt`; GitHub) | Candidates for the desktop's text (rasterized, see stb_truetype) |
| <a id="ref-inter"></a>**Inter** | https://github.com/rsms/inter (https://rsms.me/inter/) | SIL OFL 1.1 (GitHub) | Candidate for the desktop's text |
| <a id="ref-dina"></a>**Dina** (bitmap) | https://www.donationcoder.com/Software/Jibz/Dina | **Licence not confirmed — do not use.** The author's page gives terms of its own, without a licence name (packagers label it MIT) | Not to be used unless a licence check confirms it |

## Applications (separate applications only)

These are **never code in this repository**: each could later run on Madire OS as a separate application, installed separately, once the Linux-compatible foundation exists (they need its system calls, graphics, audio and input). Their licences (mostly GPL) are read only to know what installing them would mean. **STUDY ONLY** for their sources.

| Application | Link | Licence (verified 2026-10-16) | Roadmap |
|---|---|---|---|
| **SuperTuxKart** | https://github.com/supertuxkart/stk-code | GPL-3.0-or-later for the code (`COPYING`; GitHub names none); the data files each have their own licence | Linux apps (games) |
| **Luanti** (formerly Minetest) | https://github.com/luanti-org/luanti | LGPL-2.1-or-later for the source code; textures and sounds CC BY-SA 3.0 (`LICENSE.txt`; GitHub names none) | Linux apps (games) |
| **0 A.D.** | https://gitea.wildfiregames.com/0ad/0ad (official). The GitHub repository https://github.com/0ad/0ad is **archived**: "migrated to https://gitea.wildfiregames.com on August 20th 2024" | To be read at the official home before any use (the archived mirror's licence file was not readable) | Linux apps (games) |
| **Xonotic** | https://gitlab.com/xonotic/xonotic | GPL-3.0 (the GitLab page) | Linux apps (games) |
| **OpenTTD** | https://github.com/OpenTTD/OpenTTD | GPL-2.0, except some third-party modules (`COPYING.md`; GitHub names none) | Linux apps (games) |
| **ioquake3** | https://github.com/ioquake/ioq3 | GPL-2.0 (GitHub) | Linux apps (games) |
| **Chocolate Doom** | https://github.com/chocolate-doom/chocolate-doom | GPL-2.0 (GitHub) | Linux apps (games) |
| **doomgeneric** | https://github.com/ozkl/doomgeneric | GPL-2.0 (GitHub) | A Doom port reduced to a few platform functions: a separate application for an early graphics test, never code in the repository |
| **Wine, Proton** | see Wine above and Proton below | LGPL-2.1-or-later; Proton per component | Windows apps (long term) |

## Other references (listed before 0.12)

These entries come from earlier milestones. They were **not re-verified or reclassified** in 0.12 stage 5: until the owner classifies them, they are read under the STUDY ONLY rule, and their licences are as understood when they were listed (to be verified).

### Operating systems

| Project | Link | Could be useful for | Licence (to be verified) |
|---|---|---|---|
| <a id="ref-xv6-riscv"></a>xv6-riscv (the current xv6) | https://github.com/mit-pdos/xv6-riscv | Concepts: `copyin`/`copyout` (user pointers checked page by page when used), system-call dispatch and argument fetching, `exec` (for the 0.11 program loader); cross-checked against 0.10's system calls (stage 5b): no change needed. Its code targets RISC-V and doesn't apply to x86-64 | MIT |
| Redox OS | https://github.com/redox-os/redox | A Rust microkernel: its design, memory isolation and security; relevant to the open decision on Rust for drivers ([roadmap.md](roadmap.md#modernisation-track-planned-not-committed-to-milestones-yet)) | MIT |
| OSDev Wiki | https://wiki.osdev.org | User mode, system calls, paging, process management, context switching, ELF; since 1.0 the PS/2 mouse (see *Specifications and learning*) | **Mixed** (1.0 check: content since 2011 public-domain compatible, older content not necessarily). **Ideas only; nothing quoted** |

**Linux documentation used since 0.10** (study only; see Linux above): `Documentation/arch/x86/` (x86-64 entry, the IST stacks), `Documentation/security/self-protection.rst` (SMEP, SMAP, W^X), `Documentation/mm/` (address spaces), `Documentation/scheduler/`, `Documentation/filesystems/` (the VFS, for 1.0) and `Documentation/driver-api/` (drivers). **System-call behaviour:** the Linux man pages (for example `syscall(2)`, `syscalls(2)`, `write(2)`, `nanosleep(2)`, `getpid(2)`, `sched_yield(2)`, `exit(2)`) are the reference for numbers, arguments, return values and error codes; only these facts are used.

### Windows compatibility

| Project | Link | Could be useful for | Licence (to be verified) |
|---|---|---|---|
| Proton | https://github.com/ValveSoftware/Proton | Wine with improvements for games (graphics translation, controller and performance work) and how it is packaged and tested. **Study only, for the Windows-apps milestone; a separate application, never code in this repository** (1.0) | 1.0 check (2026-10-16): the top-level `LICENSE.proton` is a BSD-style licence (Valve); the bundled components (Wine and others) keep their own licences, to be read one by one |

### Shells

| Project | Link | Could be useful for | Licence (to be verified) |
|---|---|---|---|
| GNU Bash (a mirror of GNU Bash) | https://github.com/bminor/bash | Shell-language design: variables, `if` and loops, pipes, redirection, quoting, job control. Possibly a future installable APPLICATION (a separate program, with the GPL's source obligations). No code may be copied into Madire OS | GPL-3.0 |
| PowerShell | https://github.com/PowerShell/PowerShell | Design inspiration: pipelines of structured objects, consistent verb-noun naming, built-in help for every command. It needs .NET, so it isn't a porting target for now | MIT (GitHub; verified 2026-11-12) |

### Terminal, desktop and app-platform references (Windows)

These are Windows-specific technologies: none of them can run on Madire OS. They are read for ideas only, for later phases. Microsoft names and logos are trademarks; Madire's equivalents get their own names under the naming rule.

| Project | Link | Could be useful for | Phase | Licence (to be verified) |
|---|---|---|---|---|
| Windows Terminal | https://github.com/microsoft/terminal | A future Madire terminal app: parsing terminal control codes, fast text rendering, tabs, themes | GUI | MIT (GitHub; verified 2026-11-12) |
| PowerToys | https://github.com/microsoft/PowerToys | Feature ideas for the Madire desktop: window snapping, a quick launcher, a colour picker, keyboard remapping, extracting text from the screen | Desktop | MIT (GitHub; verified 2026-11-12) |
| WinUI | https://github.com/microsoft/microsoft-ui-xaml | Designing Madire's own UI toolkit: controls, layout, theming, accessibility | GUI toolkit | MIT (GitHub; verified 2026-11-12) |
| Windows universal samples | https://github.com/microsoft/Windows-universal-samples | A checklist of the app capabilities a modern OS provides (sensors, Bluetooth, camera, notifications, permissions), for Madire's app interface and permission model | Apps | MIT (GitHub; verified 2026-11-12) |

### Browser engines

| Project | Link | Could be useful for | Licence (to be verified) |
|---|---|---|---|
| Gecko (mozilla/gecko-dev) | https://github.com/mozilla/gecko-dev | Another browser engine for comparison (browse only, never clone) | MPL-2.0 |

### Future Madire Security Toolkit

Reference only. **Never run against any system.**

| Project | Link | Could be useful for | Licence (to be verified) |
|---|---|---|---|
| nmap | https://github.com/nmap/nmap | Network discovery and service detection | Nmap Public Source License (based on GPL-2.0) |
| sqlmap | https://github.com/sqlmapproject/sqlmap | SQL-injection testing of web applications | GPL-2.0 |
| ffuf | https://github.com/ffuf/ffuf | Web content and parameter fuzzing | MIT |
| John the Ripper | https://github.com/JohnTheRipper/JohnTheRipper | Password strength auditing | GPL-2.0 (with more permissive parts) |
| hashcat | https://github.com/hashcat/hashcat | Password hash auditing | MIT |
| aircrack-ng | https://github.com/aircrack-ng/aircrack-ng | Wi-Fi security auditing | GPL-2.0 |
| WPScan | https://github.com/wpscanteam/wpscan | WordPress security scanning | WPScan Public Source License (restricts commercial use) |
| nuclei | https://github.com/projectdiscovery/nuclei | Template-based vulnerability scanning | MIT |

## <a id="ref-owner-list"></a>The owner's classified repository list (added in 1.4 stage 15a)

The owner supplied this list on 12 November 2026 with the source's licence claims marked as unverified. **Every link was opened and every licence checked on the repository itself on 2026-11-12**: "GitHub" is GitHub's detection of the repository's own licence file, "file" means the licence file was read, "README" the README's licence statement, "model card" the Hugging Face model card. A moved repository is listed at its new address with the old one named; a dead link says so. Projects already listed above keep their entries there (dated 2026-11-12) and are named here with a link to them.

**The licence rule (AGENTS.md section 11):** only **MIT, BSD, Apache-2.0, zlib, 0BSD and CC0-1.0** may ever be used, and a library only with the owner's approval per library (`third_party/`, its licence, `THIRD_PARTY_NOTICES`). **GPL, LGPL, MPL, APSL, MS-PL, the Unicode licence, proprietary licences, "free SDKs" and no licence** mean study only or avoid. Nothing below is copied or closely reproduced; READ FULLY means ideas, architecture and design only.

**The classes:** **READ FULLY** (permissive; ideas only); **STUDY ONLY** (README, licence and design documents, man pages; never the source); **INSPIRATION ONLY** (the published result, for the feel); **LIBRARY CANDIDATE** (permissive; usable only with the owner's approval per library); **NOT APPLICABLE** (Windows-only, .NET, web or Python stacks Madire can't use as code; a permissive one may still give feature ideas); **AVOID**.

### Avoid

| Item | Link | Licence (2026-11-12) | Note |
|---|---|---|---|
| freellmapi | https://github.com/tashfeenahmed/freellmapi/releases/tag/v0.16.21 | MIT (GitHub) | **The owner's decision**, whatever its licence |
| Listary | https://www.listary.com/ | proprietary | Closed source |
| Tobii SDK | https://developer.tobii.com/ (refused an automated check with 403) | commercial | Closed source |
| Microsoft Defender APIs | https://learn.microsoft.com/defender-endpoint/api/apis-intro | Microsoft licence | Proprietary service |
| Xbox GDK samples | https://github.com/microsoft/Xbox-GDK-Samples | MIT (GitHub) | The samples need the proprietary GDK |
| Microsoft Keyboard Layout Creator | https://www.microsoft.com/en-us/download/details.aspx?id=102134 (refused an automated check with 403) | proprietary | Closed source |
| Everything and its SDK (voidtools) | https://www.voidtools.com/support/everything/sdk/; the listed https://github.com/voidtools/Everything was **404 on 2026-11-12** | the program closed source; https://github.com/voidtools/everything_sdk3 MIT (file) | It talks to a closed program |
| Sysinternals Autoruns | https://learn.microsoft.com/sysinternals/downloads/autoruns | Sysinternals licence | Proprietary |
| Florence (on-screen keyboard) | https://florence.sourceforge.net/ (refused an automated check with 403); the listed https://github.com/florence-osk/florence was **404 on 2026-11-12** | **not verified** | Avoided until its licence is confirmed |

### AI and voice (Madire AI, Mamishi v1)

| Item | Link | Licence (2026-11-12) | Class | How we may use it |
|---|---|---|---|---|
| Open Voice OS | https://github.com/OpenVoiceOS (organisation); https://github.com/OpenVoiceOS/ovos-core | ovos-core Apache-2.0 (GitHub); per repository | READ FULLY | Assistant architecture: wake word, speech to text, intents, skills, text to speech |
| Mycroft AI | https://github.com/MycroftAI (organisation); https://github.com/MycroftAI/mycroft-core | Apache-2.0 (GitHub); **archived** | READ FULLY | Design and history; unmaintained |
| Rhasspy | https://github.com/rhasspy (organisation); https://github.com/rhasspy/rhasspy3 | MIT (GitHub); **archived** | READ FULLY | An offline voice pipeline's design |
| Piper TTS | https://github.com/rhasspy/piper (**archived**; "Development has moved") → https://github.com/OHF-Voice/piper1-gpl | the old repository MIT (GitHub); the current one **GPL-3.0** (GitHub) | STUDY ONLY | Its design only; not a library choice |
| Open Interpreter | https://github.com/openinterpreter/openinterpreter (moved from OpenInterpreter/open-interpreter) | Apache-2.0 (GitHub) | NOT APPLICABLE | Ideas: tools that ask before they act |
| OpenHands | https://github.com/OpenHands/OpenHands (moved from All-Hands-AI/OpenHands) | MIT (file) | NOT APPLICABLE | – |
| CrewAI | https://github.com/crewAIInc/crewAI | MIT (GitHub) | NOT APPLICABLE | – |
| AutoGen | https://github.com/microsoft/autogen | code MIT (`LICENSE-CODE`), documents CC-BY-4.0 (README) | NOT APPLICABLE | – |
| Ollama | https://github.com/ollama/ollama | MIT (GitHub) | NOT APPLICABLE | Ideas for running a local model |
| LangChain | https://github.com/langchain-ai/langchain | MIT (GitHub) | NOT APPLICABLE | – |
| Vosk | https://github.com/alphacep/vosk-api | Apache-2.0 (GitHub); its models have their own licences | LIBRARY CANDIDATE | Offline speech to text, later; each model checked separately |
| Whisper | https://github.com/openai/whisper | MIT (GitHub) | READ FULLY | The model's design |
| openWakeWord | https://github.com/dscripka/openWakeWord | code Apache-2.0; **its pre-trained models CC BY-NC-SA 4.0** (README) | LIBRARY CANDIDATE (the code only) | The bundled models are **not usable** (non-commercial); a "Hey Mamishi" model would be trained by Madire |
| Faster-Whisper | https://github.com/SYSTRAN/faster-whisper | MIT (GitHub) | NOT APPLICABLE | – |
| Phi-3 Mini (model) | https://huggingface.co/microsoft/Phi-3-mini-4k-instruct; cookbook https://github.com/microsoft/PhiCookBook (moved from microsoft/Phi-3CookBook) | the model MIT (model card); the cookbook MIT (GitHub) | LIBRARY CANDIDATE (a model) | With the owner's approval, under the model rule ([roadmap.md](roadmap.md), *Madire AI*) |
| Qwen (models) | https://github.com/QwenLM/Qwen3; https://huggingface.co/Qwen/Qwen3-4B; the older https://github.com/QwenLM/Qwen | Qwen3's open-weight models Apache-2.0 (README, model card); the older Qwen-7B/14B/72B under the **Tongyi Qianwen licence** (README) | LIBRARY CANDIDATE (Qwen3 only, each model checked) | The older Qwen models are avoided |

### UI, settings, themes and shell (Windows)

| Item | Link | Licence (2026-11-12) | Class | How we may use it |
|---|---|---|---|---|
| Windows App SDK | https://github.com/microsoft/WindowsAppSDK | MIT (GitHub) | NOT APPLICABLE | – |
| WinUI Gallery | https://github.com/microsoft/WinUI-Gallery | MIT (GitHub) | NOT APPLICABLE | A catalogue of controls, for ideas |
| WinUI | listed above (*Terminal, desktop and app-platform references*) | MIT (GitHub) | NOT APPLICABLE | Toolkit ideas |
| Windows Community Toolkit | https://github.com/CommunityToolkit/Windows | MIT (file, `License.md`) | NOT APPLICABLE | – |
| **Files** | https://github.com/files-community/Files | MIT (GitHub) | READ FULLY (feature ideas only) | Tabs, the sidebar, Trash restore, previews; **not its Windows 11 look** |
| ModernWpf | https://github.com/Kinnara/ModernWpf | MIT (GitHub) | NOT APPLICABLE | – |
| FluentAvalonia | https://github.com/amwx/FluentAvalonia | MIT (GitHub) | NOT APPLICABLE | – |
| Rectify11 Installer | https://github.com/Rectify11/Installer | GPL-3.0 (GitHub) | NOT APPLICABLE (study only) | – |
| WinDynamicDesktop | https://github.com/t1m0thyj/WinDynamicDesktop | MPL-2.0 (GitHub) | NOT APPLICABLE (study only) | The idea of a wallpaper that follows the time of day |
| Lively Wallpaper | https://github.com/lively-community/lively (moved from rocksdanister/lively) | GPL-3.0 (GitHub) | NOT APPLICABLE (study only) | – |
| OpenRGB | https://github.com/CalcProgrammer1/OpenRGB | GPL-2.0 (GitHub) | NOT APPLICABLE (study only) | – |
| Cairo Desktop | https://github.com/cairoshell/cairoshell | Apache-2.0 (GitHub) | NOT APPLICABLE | Shell and taskbar ideas |
| ExplorerPatcher | https://github.com/valinet/ExplorerPatcher | GPL-2.0 (GitHub) | NOT APPLICABLE (study only) | – |
| PowerToys | listed above | MIT (GitHub) | NOT APPLICABLE | Feature ideas |
| Flow Launcher | https://github.com/Flow-Launcher/Flow.Launcher | MIT (GitHub) | NOT APPLICABLE | Launcher and search-box ideas |
| Wox | https://github.com/Wox-launcher/Wox | GPL-3.0 (GitHub) | NOT APPLICABLE (study only) | – |
| Windows Terminal | listed above | MIT (GitHub) | READ FULLY | Terminal parsing and rendering ideas |
| PowerShell | listed above (*Shells*) | MIT (GitHub) | NOT APPLICABLE | Design ideas |
| NanaZip | https://github.com/M2Team/NanaZip | its own code MIT; the 7-Zip parts under the 7-Zip licence; its logo CC BY-ND 4.0 (file) | NOT APPLICABLE | – |
| 7-Zip | https://github.com/ip7z/7zip | LGPL-2.1 with the unRAR restriction; some files BSD or public domain (file, `DOC/License.txt`) | STUDY ONLY | Possibly a separate Store app later |
| fonttools | https://github.com/fonttools/fonttools | MIT (GitHub) | NOT APPLICABLE | A possible host build tool, with its own approval |
| Google Fonts | https://github.com/google/fonts | no repository licence; per font, in the folders `ofl/`, `apache/`, `ufl/`, `cc-by-sa/` (file) | asset source, per font | AGENTS.md section 11, item 6: OFL and Apache fonts allowed; UFL reviewed first; CC BY-SA only with the owner's approval |

### Devices, audio, drivers and hardware monitoring

| Item | Link | Licence (2026-11-12) | Class | How we may use it |
|---|---|---|---|---|
| Windows universal samples | listed above | MIT (GitHub) | NOT APPLICABLE | – |
| Windows classic samples | https://github.com/microsoft/Windows-classic-samples | MIT (file) | NOT APPLICABLE | – |
| Windows driver samples | https://github.com/microsoft/Windows-driver-samples | MS-PL (GitHub) | NOT APPLICABLE | MS-PL isn't on the allowed list |
| DriverStoreExplorer | https://github.com/lostindark/DriverStoreExplorer | GPL-2.0 (GitHub) | NOT APPLICABLE (study only) | – |
| EarTrumpet | https://github.com/File-New-Project/EarTrumpet | MIT **with a clause excluding named companies** (file) | NOT APPLICABLE | A modified MIT; per-app volume ideas |
| AudioSwitcher | https://github.com/xenolightning/AudioSwitcher | MS-PL (GitHub) | NOT APPLICABLE | – |
| LibreHardwareMonitor | https://github.com/LibreHardwareMonitor/LibreHardwareMonitor | MPL-2.0 (GitHub) | STUDY ONLY | – |
| OpenHardwareMonitor | https://github.com/openhardwaremonitor/openhardwaremonitor | MPL-2.0 (file, `Licenses/License.html`) | STUDY ONLY | – |
| GPUOpen libraries and SDKs | https://github.com/GPUOpen-LibrariesAndSDKs (organisation, 85 repositories) | per repository, not checked one by one | NOT APPLICABLE (now) | Each repository checked when one is used |
| PresentMon | https://github.com/GameTechDev/PresentMon | MIT (GitHub) | NOT APPLICABLE | – |
| System Informer | https://github.com/winsiderss/systeminformer | MIT (GitHub) | READ FULLY | Task manager and system-information ideas (Windows-specific) |
| psutil | https://github.com/giampaolo/psutil | BSD-3-Clause (GitHub) | READ FULLY | Which system figures a `system` command and Mamishi's System tool show |

### Networking

| Item | Link | Licence (2026-11-12) | Class | How we may use it |
|---|---|---|---|---|
| SharpPcap | https://github.com/dotpcap/sharppcap | MIT (file, `LICENSES/MIT.txt`) | NOT APPLICABLE | – |
| Pcap.Net | https://github.com/PcapDotNet/Pcap.Net | BSD-3-Clause (GitHub) | NOT APPLICABLE | – |
| lwIP | [listed above](#ref-lwip) | BSD-3-Clause (file) | LIBRARY CANDIDATE | The networking stack candidate, behind Madire's own socket layer |
| smoltcp | https://github.com/smoltcp-rs/smoltcp | 0BSD (GitHub) | READ FULLY | How a small TCP/IP stack is structured (Rust) |
| Fuchsia | https://fuchsia.googlesource.com/fuchsia | BSD-3-Clause style (file, `LICENSE`) | READ FULLY | Netstack and component ideas |
| gVisor | https://github.com/google/gvisor | Apache-2.0 (GitHub) | READ FULLY | Its netstack, and its Linux system-call compatibility layer (Go) |
| Apache Mynewt NimBLE | https://github.com/apache/mynewt-nimble | Apache-2.0 (GitHub) | LIBRARY CANDIDATE | Bluetooth LE, later |

### Packages, updates and the store

| Item | Link | Licence (2026-11-12) | Class | How we may use it |
|---|---|---|---|---|
| winget-cli | https://github.com/microsoft/winget-cli | MIT (GitHub) | READ FULLY | Package-manager design |
| winget-pkgs | https://github.com/microsoft/winget-pkgs | MIT (GitHub) | READ FULLY | The manifest format |
| UniGetUI | https://github.com/Devolutions/UniGetUI (moved from marticliment/UniGetUI) | MIT (GitHub) | NOT APPLICABLE | – |
| Squirrel.Windows | https://github.com/Squirrel/Squirrel.Windows | MIT (GitHub) | NOT APPLICABLE | Update-flow ideas |
| NetSparkle | https://github.com/NetSparkleUpdater/NetSparkle | MIT (GitHub) | NOT APPLICABLE | – |
| semver (C#) | https://github.com/WalkerCodeRanger/semver (moved from maxhauser/semver) | MIT (GitHub) | NOT APPLICABLE | The semver.org specification is the reference |
| Scoop | https://github.com/ScoopInstaller/Scoop | Unlicense or MIT (file) | NOT APPLICABLE | – |
| Chocolatey | https://github.com/chocolatey/choco | Apache-2.0 (file) | NOT APPLICABLE | – |

### Accounts, security and privacy

| Item | Link | Licence (2026-11-12) | Class | How we may use it |
|---|---|---|---|---|
| Azure AD native UWP sample | https://github.com/Azure-Samples/active-directory-dotnet-native-uwp-v2 | MIT (GitHub); **archived** | NOT APPLICABLE | – |
| fido2-net-lib | https://github.com/passwordless-lib/fido2-net-lib | MIT (GitHub) | NOT APPLICABLE | The WebAuthn specification is the reference later |
| passwordless-lib | https://github.com/passwordless-lib (organisation, 2 repositories) | per repository | NOT APPLICABLE | – |
| OneDrive API docs | https://github.com/OneDrive/onedrive-api-docs | MIT (GitHub) | NOT APPLICABLE | A proprietary cloud service |
| Microsoft Graph .NET SDK | https://github.com/microsoftgraph/msgraph-sdk-dotnet | MIT (file) | NOT APPLICABLE | – |
| LiteDB | https://github.com/litedb-org/LiteDB | MIT (GitHub) | NOT APPLICABLE | Embedded-database ideas |
| CredentialManagement | https://github.com/ilyalozovyy/credentialmanagement (the probable original; the owner's link https://github.com/davotronic5000/CredentialManagement was **404 on 2026-11-12**) | Apache-2.0 (GitHub) | NOT APPLICABLE | – |
| ASP.NET Core | https://github.com/dotnet/aspnetcore | MIT (GitHub) | NOT APPLICABLE | – |
| SecretManagement | https://github.com/PowerShell/SecretManagement | MIT (GitHub); **archived** | NOT APPLICABLE | – |
| BouncyCastle C# | https://github.com/bcgit/bc-csharp | MIT (GitHub) | NOT APPLICABLE | Cryptography in C stays [Mbed TLS](#ref-mbedtls) |
| KeePassXC | https://github.com/keepassxreboot/keepassxc | GPL-2.0/3.0 (file, `COPYING`); some parts BSD or CC0 | STUDY ONLY | – |

### Language, typing and input

| Item | Link | Licence (2026-11-12) | Class | How we may use it |
|---|---|---|---|---|
| Hunspell | https://github.com/hunspell/hunspell | LGPL-2.1 (GitHub) | STUDY ONLY | – |
| LibreOffice dictionaries | https://github.com/LibreOffice/dictionaries | per dictionary (English: its own licence and WordNet's; German: GPL, LGPL and OASIS) (file) | STUDY ONLY | Each dictionary checked as data if one is ever used |
| LanguageTool | https://github.com/languagetool-org/languagetool | LGPL-2.1 (GitHub) | STUDY ONLY | – |
| ibus | https://github.com/ibus/ibus | LGPL-2.1 (GitHub) | STUDY ONLY | – |
| Fcitx5 | https://github.com/fcitx/fcitx5 | LGPL-2.1-or-later (file, `LICENSES/`) | STUDY ONLY | – |
| ICU | https://github.com/unicode-org/icu | Unicode License v3 (file) | STUDY ONLY | Not on the allowed list; the owner decides if it is ever needed |

### Gaming

| Item | Link | Licence (2026-11-12) | Class | How we may use it |
|---|---|---|---|---|
| SDL | [listed above](#ref-sdl) | Zlib (GitHub) | LIBRARY CANDIDATE | Only if approved |
| GLFW | https://github.com/glfw/glfw | Zlib (GitHub) | LIBRARY CANDIDATE | Later; needs a window-system backend |
| Playnite | https://github.com/JosefNemec/Playnite | MIT (GitHub) | NOT APPLICABLE | – |
| Heroic Games Launcher | https://github.com/Heroic-Games-Launcher/HeroicGamesLauncher | GPL-3.0 (GitHub) | NOT APPLICABLE (study only) | – |
| Steamworks.NET | https://github.com/rlabrecque/Steamworks.NET | MIT (GitHub) | NOT APPLICABLE | It wraps the proprietary Steamworks |
| discord-rpc-csharp | https://github.com/Lachee/discord-rpc-csharp | MIT (GitHub) | NOT APPLICABLE | – |
| DirectX Graphics Samples | https://github.com/microsoft/DirectX-Graphics-Samples | MIT (GitHub) | NOT APPLICABLE | – |

### Accessibility

| Item | Link | Licence (2026-11-12) | Class | How we may use it |
|---|---|---|---|---|
| NVDA | https://github.com/nvaccess/nvda | GPL-2.0-or-later with two exceptions (file, `copying.txt`) | STUDY ONLY | Screen-reader design from its documents |
| liblouis | https://github.com/liblouis/liblouis | LGPL-2.1 (GitHub) | STUDY ONLY | – |
| OpenGazer | https://github.com/opengazer/OpenGazer | GPL-2.0 (GitHub) | STUDY ONLY | – |
| SharpHook | https://github.com/TolikPylypchuk/SharpHook | MIT (GitHub) | NOT APPLICABLE | – |
| Tesseract OCR | https://github.com/tesseract-ocr/tesseract | Apache-2.0 (GitHub); its trained data licensed separately | LIBRARY CANDIDATE | Much later |
| Color Oracle | https://github.com/nvkelso/color-oracle-java | MIT (GitHub) | READ FULLY | Colour-blindness simulation, to check the Design Guide's contrast |

### Location and maps

| Item | Link | Licence (2026-11-12) | Class | How we may use it |
|---|---|---|---|---|
| GPS.NET | https://github.com/jperson2000/GPS.Net (probable; the owner's link https://github.com/indigo744/Gps.Net was **404 on 2026-11-12**) | LGPL-2.1 (file) | STUDY ONLY | – |
| Mapsui | https://github.com/Mapsui/Mapsui | MIT (GitHub) | NOT APPLICABLE | – |
| OsmSharp | https://github.com/OsmSharp/core | MIT (file) | NOT APPLICABLE | – |
| NetTopologySuite | https://github.com/NetTopologySuite/NetTopologySuite | BSD-3-Clause (file) | NOT APPLICABLE | – |

### Notifications, calendar, clock and focus

| Item | Link | Licence (2026-11-12) | Class | How we may use it |
|---|---|---|---|---|
| Dunst | [listed above](#ref-dunst) | BSD-3-Clause (file) | READ FULLY | Notification ideas |
| Mako | https://github.com/emersion/mako | MIT (GitHub) | READ FULLY | Notification ideas (C) |
| SwayNotificationCenter | https://github.com/ErikReider/SwayNotificationCenter | GPL-3.0 (GitHub) | STUDY ONLY | – |
| Evolution Data Server | https://github.com/GNOME/evolution-data-server | LGPL-2.0 (file, `COPYING`) | STUDY ONLY | – |
| Kalendar / Merkuro (KDE) | https://github.com/KDE/kalendar (last change 2023-07-12); the current https://github.com/KDE/merkuro | mixed under REUSE: GPL and LGPL, with BSD and CC0 parts (file, `LICENSES/`) | STUDY ONLY | – |
| Thunderbird (comm-central) | https://github.com/mozilla/releases-comm-central | MPL-2.0 (file headers; no licence file at the root) | STUDY ONLY | – |
| GNOME Clocks | https://github.com/GNOME/gnome-clocks | GPL-2.0 (GitHub) | STUDY ONLY | – |
| KClock | https://github.com/KDE/kclock | GPL-2.0 (GitHub) | STUDY ONLY | – |
| Super Productivity | https://github.com/super-productivity/super-productivity (moved from johannesjo/super-productivity) | MIT (GitHub) | NOT APPLICABLE | Focus-timer ideas |
| ActivityWatch | https://github.com/ActivityWatch/activitywatch | MPL-2.0 (GitHub) | STUDY ONLY | A privacy-first activity tracker's design |
| Pomatez | https://github.com/zidoro/pomatez | MIT (GitHub) | NOT APPLICABLE | – |

### Already agreed (1.4 stage 15a)

| Item | Link | Licence (2026-11-12) | Class | How we may use it |
|---|---|---|---|---|
| Files | in *UI, settings, themes and shell* above | MIT (GitHub) | READ FULLY (feature ideas only) | – |
| <a id="ref-trash-spec"></a>FreeDesktop Trash specification | https://specifications.freedesktop.org/trash/latest/ (version 1.0, 2014-01-02); the agreed https://specifications.freedesktop.org/trash-spec/latest/ was **404 on 2026-11-12** | its section 10.1: "Use and distribute as you wish … Implementation under any license at all is explicitly allowed" (read) | specification, facts only | `files/` and `info/` with each item's original path; Restore and Empty (1.5's Trash) |
| e2fsprogs (manual pages only) | [listed above](#ref-e2fsprogs): https://e2fsprogs.sourceforge.net/ and https://git.kernel.org/pub/scm/fs/ext2/e2fsprogs.git | GPL-2.0 for the programs (file, `NOTICE`) | STUDY ONLY | The manual pages, `e2fsck.conf(5)` included; the tools are run, never read |
| Nautilus | https://github.com/GNOME/nautilus (in the *Study only* table above) | GPL-3.0 (GitHub) | STUDY ONLY | Its README and design; feature ideas for the Files app |
| Fluent UI System Icons | [listed above](#ref-fluent) | MIT (GitHub) | INSPIRATION ONLY | Madire designs its own icons |

The source list's other claims (copying another project's Settings architecture, keeping code closed) do not apply: Madire's code is written from scratch and is open under Apache 2.0 (AGENTS.md section 11).

## <a id="ref-15"></a>Added in 1.5 stage 0 (the Files app, the Trash, Shut down, the ISO dates)

**Verified on 2026-11-13** by the builder, each as stated in its row: GitHub's API (`gh api repos/<owner>/<repo>` for the licence field, `gh api repos/<owner>/<repo>/license` for the licence file's name and first lines), or the page itself opened by an automated fetch, or, where a site refused the automated check, a web search and a second source, said so. Every entry follows the rules at the top: nothing is copied or closely reproduced.

| Reference | Link | Licence (2026-11-13) and how it was verified | Rule | What 1.5 uses |
|---|---|---|---|---|
| <a id="ref-files"></a>**Files** (files-community) | https://github.com/files-community/Files | MIT: GitHub's licence field `MIT`; the licence file is `LICENSE-MIT` ("MIT License", "Copyright (c) 2018 - present Files Community"); not archived, last commit 2026-11-11 (GitHub API). Already in the owner's list above (1.4 stage 15a) | READ FULLY (feature ideas only) | The sidebar of home folders, the Trash's restore actions, the status line; **not its Windows 11 look** |
| **FreeDesktop Trash specification** ([entry above](#ref-trash-spec)) | https://specifications.freedesktop.org/trash/latest/ | Version 1.0, 2014-01-02 (opened 2026-11-13): "The license: Use and distribute as you wish. … Implementation under any license at all is explicitly allowed." | specification, facts only | `$XDG_DATA_HOME/Trash` with `files/` and `info/`; the `[Trash Info]` file with `Path` (percent-encoded per RFC 2396 section 2) and `DeletionDate` (local time); the info file created first with `O_EXCL`; `directorysizes` optional ("should") |
| <a id="ref-xdg-basedir"></a>**XDG Base Directory specification** | https://specifications.freedesktop.org/basedir/latest/ | Version 0.8, 8 May 2021 (opened 2026-11-13); the page states **no licence**; its only notice is the site footer "© 2026 Freedesktop.org" | specification, facts only (no text quoted) | `$XDG_DATA_HOME` defaults to `$HOME/.local/share`, so the home Trash is `/home/user/.local/share/Trash` |
| <a id="ref-acpi"></a>**ACPI specification** (UEFI Forum) | https://uefi.org/specifications; the HTML edition https://uefi.org/specs/ACPI/6.6/ | **uefi.org refused the automated check (HTTP 403) on 2026-11-13**, for the specifications page and the 6.5 and 6.6 HTML pages, so the builder could not read its terms; a web search the same day confirmed that version 6.6 is published (HTML chapters indexed at the address above; Ubuntu's FWTS 25.11.00 release notes support "all new features and changes in ACPI 6.6"). **Background, found by web search on 2026-11-13** (the pages not opened directly): the UEFI Forum's FAQ (https://archive.uefi.org/faq) says the specification itself costs nothing to use and that IP needed to implement it is offered on reasonable and non-discriminatory terms; the ACPI 6.5 HTML edition's acknowledgements (https://uefi.org/specs/ACPI/6.5) allow unmodified copies only for implementing the specification and grant no IP licence (the owner's finding; the builder's search did not show this text); an older notice (https://archive.uefi.org/node/164) says a licence to implement comes with the free UEFI Adopter Membership Agreement. **The governing statement: the owner opened https://uefi.org/specifications by hand on 2026-11-13** (its section "Access to the UEFI Specifications"); what it says, in our own words: the UEFI Forum's specifications may be downloaded and read only; downloading them grants no licence to implement them; a licence to implement is available through the free UEFI Adopter Membership Agreement, returned to UEFI Administration. The same page lists ACPI as maintained by the UEFI Forum's ACPI Specification Working Group. **Madire's ACPI power-off code (plan 1.5, S6) waits until the owner confirms the signed Adopter Membership Agreement** (plan risk 65) | specification, facts only (no text, tables or figures reproduced) | The RSDP, RSDT/XSDT, FADT (PM1a/PM1b_CNT_BLK, SMI_CMD, ACPI_ENABLE, SCI_EN) and DSDT layouts, checksums, the `\_S5` sleep object and SLP_TYP/SLP_EN (plan C3) |
| <a id="ref-xorriso"></a>**`xorriso` manual page** (GNU xorriso, libisoburn) | https://www.gnu.org/software/xorriso/man_1_xorriso.html | The program is GPL: Debian's copyright file for libisoburn (https://metadata.ftp-master.debian.org/changelogs/main/libi/libisoburn/stable_copyright, opened 2026-11-13) gives `Files: *` "GPL-2+"; the separately distributed GNU xorriso is GPL version 3 or later (its README, found by a web search; `www.gnu.org/software/xorriso/` refused the connection on 2026-11-13). The manual page was opened on 2026-11-13 | STUDY ONLY (the manual page and documented options only; the tool runs on the test machines and in the build, as before; its source is never read) | "SOURCE_DATE_EPOCH … sets … -volume_date "all_file_dates" to "set_to_mtime""; `-volume_date all_file_dates` with `set_to_mtime` gives each file's atime and ctime its mtime (plan C9) |

**Considered and not applicable:** **React and npm file-manager libraries** (web user-interface components in JavaScript; Madire's desktop is its own C toolkit in the kernel, with no browser engine or JavaScript runtime) and **elFinder** (https://github.com/Studio-42/elFinder: a web file manager in JavaScript and PHP; BSD-3-Clause by its `LICENSE.md`, "elFinder is issued under a 3-clauses BSD license", GitHub's field `NOASSERTION`; verified 2026-11-13 through the GitHub API). Neither is read for 1.5 or used.
