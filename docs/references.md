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
| <a id="ref-sdl"></a>**SDL** | https://github.com/libsdl-org/SDL | Zlib (GitHub; verified 2026-10-16) | A portable layer for windows, input, audio and timers that games use; what such a layer needs from the OS. Ideas; **a possible library only if approved** | Graphics, audio, games as separate applications |
| <a id="ref-stb"></a>**stb_truetype** (in stb) | https://github.com/nothings/stb (`stb_truetype.h`) | Dual: MIT or the Unlicense (public domain), the user's choice (`LICENSE`; GitHub names none; verified 2026-10-16) | A TrueType rasterizer; **the candidate rasterizer** (a library, with approval). **If it is ever used, only fonts shipped with Madire OS are loaded:** its own header warns "NO SECURITY GUARANTEE -- DO NOT USE THIS ON UNTRUSTED FONT FILES" | Graphics (fonts) |
| <a id="ref-vulkan"></a>**Vulkan-Samples** | https://github.com/KhronosGroup/Vulkan-Samples | Apache-2.0 (GitHub; verified 2026-10-16) | Learning Vulkan; far future | Graphics (far future) |
| **Vulkan-Loader** | https://github.com/KhronosGroup/Vulkan-Loader | Mostly Apache-2.0 (`LICENSE.txt`: "The majority of files in this project use the Apache 2.0 License … Any license deviations from Apache 2.0 are 'more permissive' licenses"; verified 2026-10-16) | How Vulkan drivers are found and loaded; learning, far future | Graphics (far future) |
| <a id="ref-openxr"></a>**OpenXR SDK** | https://github.com/KhronosGroup/OpenXR-SDK | Apache-2.0 (GitHub; verified 2026-10-16) | The standard VR/AR interface; far future | VR (far future) |
| <a id="ref-tinyusb"></a>**TinyUSB** | https://github.com/hathach/tinyusb | MIT (GitHub; verified 2026-10-16) | A small USB stack: host and device classes. Ideas, or **a possible library for the USB milestone if approved** | USB (modernisation track) |
| <a id="ref-musl"></a>**musl** | https://musl.libc.org/ (official git: https://git.musl-libc.org/cgit/musl) | MIT (`COPYRIGHT`: "musl as a whole is licensed under the following standard MIT license"). **The proposed mirror `https://github.com/bminor/musl` returned 404 (2026-10-12)**, so it isn't listed; the official git is used instead | A C library on the Linux system-call interface; **allowed as a library** (with approval, in `third_party/musl/`) | The C-library milestone |
| <a id="ref-lwip"></a>**lwIP** | https://github.com/lwip-tcpip/lwip (a mirror of Savannah; official: https://savannah.nongnu.org/projects/lwip/) | BSD-3-Clause (`COPYING`) | A small TCP/IP stack; the first candidate for networking, **only behind Madire's own Linux-compatible socket layer** | Networking |
| <a id="ref-ladybird"></a>**Ladybird** | https://github.com/LadybirdBrowser/ladybird | BSD-2-Clause | An independent browser engine; what a browser needs from the OS | Madire Browser |
| <a id="ref-chromium"></a>**Chromium** | https://chromium.googlesource.com/chromium/src | BSD-3-Clause (`LICENSE`); its `third_party/` components keep their own licences, to be checked one by one | The Chromium-based Madire Browser: what it needs from the OS (browse, never clone) | Madire Browser |
| <a id="ref-mbedtls"></a>**Mbed TLS** | https://github.com/Mbed-TLS/mbedtls | Dual **Apache-2.0 OR GPL-2.0-or-later** (`LICENSE`); Madire would use it under Apache-2.0 | TLS; a candidate library in `third_party/mbedtls/` (with approval) | TLS |
| <a id="ref-limine"></a>**Limine** | https://github.com/limine-bootloader/limine (redirects to `github.com/Limine-Bootloader/Limine`) | BSD-2-Clause (`COPYING`); its bundled parts are listed in its `3RDPARTY.md` (see [evaluations/limine.md](evaluations/limine.md)). **Since 0.13 also used as a tool:** the unmodified binary package of v12.9.3, fetched at build time with a pinned SHA-256 (AGENTS.md section 11, item 5); being used doesn't change its READ FULLY rule: its code is still never copied into Madire OS. **Boot branding (study only, added 2026-10-16):** its configuration documentation, https://github.com/Limine-Bootloader/Limine/blob/v12.x/CONFIG.md, describes `interface_branding` (and its colour) and `wallpaper` / `wallpaper_style` | The bootloader to replace GRUB; later its own boot protocol (framebuffer, all CPU cores, ACPI); the menu's branding options for the boot-branding step | 0.13, UEFI, boot branding |
| <a id="ref-dunst"></a>**Dunst** | https://github.com/dunst-project/dunst | BSD-3-Clause (`LICENSE`; GitHub names no licence) | A notification daemon: notification queues, timeouts, urgency, grouping | Desktop (notifications) |

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
| **Nautilus** | https://github.com/GNOME/nautilus | GPL-3.0 (GitHub) | A file manager | Desktop (file manager) |
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
| <a id="ref-fluent"></a>**Fluent UI System Icons** | https://github.com/microsoft/fluentui-system-icons | MIT (GitHub) | The feel of a modern icon set only. **Madire designs its own icon set**, so that it doesn't look like Windows | Graphics (icons) |

## Specifications and learning (added in 1.0)

Read for facts and ideas. Nothing from them is copied into Madire OS.

| Reference | Link | Licence (verified 2026-10-16) | Used for | Roadmap |
|---|---|---|---|---|
| <a id="ref-virtio"></a>**VIRTIO specification** | https://docs.oasis-open.org/virtio/virtio/v1.3/virtio-v1.3.html (repository: https://github.com/oasis-tcs/virtio-spec) | OASIS copyright, under OASIS's Non-Assertion Mode IPR policy (the document's notices). Version 1.3 is a Committee Specification Draft (CSD01, 2023-10-06), not yet an OASIS Standard | Virtual devices (disk, network, GPU, input) in QEMU and other virtual machines. **Documentation only** | Storage, networking, graphics |
| <a id="ref-osdev-mouse"></a>**OSDev wiki: PS/2 Mouse** | https://wiki.osdev.org/PS/2_Mouse | **Mixed:** content added since 2011 must be compatible with the public domain (CC0); older content may not be. **The site answered HTTP 403** to the automated check, so the status comes from search results quoting its licence page (https://wiki.osdev.org/OSDev_Wiki:License) and its forum. **Ideas only**; nothing quoted | The PS/2 mouse protocol (packets, initialisation) for the first mouse driver | Graphics (the mouse) |
| <a id="ref-multiboot1"></a>**Multiboot Specification, version 0.6.96** (Multiboot 1; added in 1.2) | https://www.gnu.org/software/grub/manual/multiboot/multiboot.html | The document's own notice (verified 2026-10-23): permission to make and distribute verbatim copies, and modified versions under an identical notice; © 1995–96 Bryan Ford and Erich Stefan Boleyn, © 1999–2010 Free Software Foundation. Not a licence for code; its example header `multiboot.h` carries its own MIT-style notice. **Facts only**: nothing copied or closely reproduced | The header (magic, flags, the a.out kludge fields, the graphics fields at 32–44 enabled by flag bit 2) and the boot information (memory map, modules, command line, the framebuffer fields of flag bit 12, types 0/1/2). Its table gives the colour fields at byte 110; the 1.2 stage 3 probe, Limine's `common/protos/multiboot1.h` and the document's own example header put them at 112 | Boot (since 0.1); 1.2 graphics |
| <a id="ref-vkguide"></a>**Vulkan Guide** | https://vkguide.dev/ (source: https://github.com/vblanco20-1/vulkan-guide) | **Not confirmed.** The site says "Distributed by a MIT license", but that link and the repository's `LICENSE.txt` are the licence of its documentation theme (just-the-docs, by Patrick Marsceill); the licence of the guide's text and code is not stated | **Learning only** | Graphics (far future) |

## <a id="ref-filesystems"></a>Filesystems: reading only (added in 1.1 stage 9b)

For the storage milestone (likely v1.3, after graphics in v1.2) and its decision between Madire's own disk filesystem and ext2 ([roadmap.md](roadmap.md)). **Reading only:** facts and ideas; nothing is copied, quoted or closely reproduced, and an ext2 implementation would be written from the specification alone, never from GPL code. Verified on 2026-10-22 the same way as the entries above.

| Reference | Link | Licence (verified 2026-10-22) | Used for | Roadmap |
|---|---|---|---|---|
| <a id="ref-xv6-book"></a>**xv6 book**, chapter 8 "File system" (Cox, Kaashoek, Morris; edition of 2024-08-31) | https://pdos.csail.mit.edu/6.1810/2024/xv6/book-riscv-rev4.pdf (from https://pdos.csail.mit.edu/6.1810/2024/xv6.html) | **No licence stated in the book.** The course page's footer shows a CC BY 3.0 US badge, but the page doesn't say that it covers the book. Reading only | The layers of a small disk filesystem: buffer cache, logging (crash safety), block allocator, inodes, folders, path names, the descriptor layer | The storage milestone |
| <a id="ref-osdev-ext2"></a>**OSDev Wiki: Ext2** | https://wiki.osdev.org/Ext2 | **Mixed** (as the OSDev wiki entry above: content since 2011 public-domain compatible, older content not necessarily). **The site answered HTTP 403** to the automated check again; not re-read. Ideas only; nothing quoted | The ext2 on-disk format: superblock, block groups, inodes, folders; a starting point for the specification | The storage milestone, the ext2 decision |
| <a id="ref-osdev-notes"></a>**OSDev Notes**, the VFS chapter | https://github.com/dreamportdev/Osdev-Notes (`08_VirtualFileSystem/02_VirtualFileSystem.md`) | **CC BY-NC 4.0** (the README and `LICENSE.md`; GitHub names no licence). Non-commercial, not on any permitted list: **reading only**, nothing quoted | How a VFS layer joins several filesystems (mount points, the node interface), as a comparison with Madire's 1.0 VFS | The storage milestone |
| <a id="ref-littlefs"></a>**littlefs** | https://github.com/littlefs-project/littlefs | BSD-3-Clause (GitHub) | A small filesystem for flash memory that survives power loss (copy-on-write, wear levelling). **Only if flash storage is ever targeted** | Storage (only for flash) |

## Fonts (assets; AGENTS.md section 11, item 6)

Fonts are assets: by default only original work, CC0, public domain, CC-BY or the SIL OFL. None is shipped yet; the graphics plan decides, and `THIRD_PARTY_ASSETS.md` is created when the first one ships.

| Font | Link | Licence (verified 2026-10-16) | Status |
|---|---|---|---|
| <a id="ref-terminus"></a>**Terminus** (bitmap) | https://terminus-font.sourceforge.net/ | SIL OFL 1.1 (the project's page: "Terminus Font is licensed under the SIL Open Font License, Version 1.1") | **The default first bitmap font** (candidate for the graphics plan) |
| <a id="ref-spleen"></a>**Spleen** (bitmap) | https://github.com/fcambus/spleen | BSD-2-Clause (GitHub) | **Candidate only:** BSD-2-Clause is not on the asset list of section 11, item 6; the rule decision belongs to the graphics plan |
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
| PowerShell | https://github.com/PowerShell/PowerShell | Design inspiration: pipelines of structured objects, consistent verb-noun naming, built-in help for every command. It needs .NET, so it isn't a porting target for now | MIT |

### Terminal, desktop and app-platform references (Windows)

These are Windows-specific technologies: none of them can run on Madire OS. They are read for ideas only, for later phases. Microsoft names and logos are trademarks; Madire's equivalents get their own names under the naming rule.

| Project | Link | Could be useful for | Phase | Licence (to be verified) |
|---|---|---|---|---|
| Windows Terminal | https://github.com/microsoft/terminal | A future Madire terminal app: parsing terminal control codes, fast text rendering, tabs, themes | GUI | MIT |
| PowerToys | https://github.com/microsoft/PowerToys | Feature ideas for the Madire desktop: window snapping, a quick launcher, a colour picker, keyboard remapping, extracting text from the screen | Desktop | MIT |
| WinUI | https://github.com/microsoft/microsoft-ui-xaml | Designing Madire's own UI toolkit: controls, layout, theming, accessibility | GUI toolkit | MIT |
| Windows universal samples | https://github.com/microsoft/Windows-universal-samples | A checklist of the app capabilities a modern OS provides (sensors, Bluetooth, camera, notifications, permissions), for Madire's app interface and permission model | Apps | MIT |

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
