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

## Read fully (permissive licences)

| Project | Link | Licence (verified 2026-10-12) | Studied for | Roadmap |
|---|---|---|---|---|
| <a id="ref-xv6"></a>**xv6** (x86 version) | https://github.com/mit-pdos/xv6-public | MIT (the `LICENSE` text; GitHub names no licence). The README says the x86 version is **no longer maintained**; the current one is `xv6-riscv` (below) | A small, complete Unix-like kernel: processes, `exec`, a file system with a log, pipes, system-call dispatch on x86 | 1.0 (file system), the C-library milestone |
| <a id="ref-serenity"></a>**SerenityOS** | https://github.com/SerenityOS/serenity | BSD-2-Clause | A from-scratch OS with a kernel, POSIX-like system calls, a C library, a GUI toolkit and a desktop | 1.0, graphics and desktop, the C-library milestone |
| <a id="ref-musl"></a>**musl** | https://musl.libc.org/ (official git: https://git.musl-libc.org/cgit/musl) | MIT (`COPYRIGHT`: "musl as a whole is licensed under the following standard MIT license"). **The proposed mirror `https://github.com/bminor/musl` returned 404 (2026-10-12)**, so it isn't listed; the official git is used instead | A C library on the Linux system-call interface; **allowed as a library** (with approval, in `third_party/musl/`) | The C-library milestone |
| <a id="ref-lwip"></a>**lwIP** | https://github.com/lwip-tcpip/lwip (a mirror of Savannah; official: https://savannah.nongnu.org/projects/lwip/) | BSD-3-Clause (`COPYING`) | A small TCP/IP stack; the first candidate for networking, **only behind Madire's own Linux-compatible socket layer** | Networking |
| <a id="ref-ladybird"></a>**Ladybird** | https://github.com/LadybirdBrowser/ladybird | BSD-2-Clause | An independent browser engine; what a browser needs from the OS | Madire Browser |
| <a id="ref-chromium"></a>**Chromium** | https://chromium.googlesource.com/chromium/src | BSD-3-Clause (`LICENSE`); its `third_party/` components keep their own licences, to be checked one by one | The Chromium-based Madire Browser: what it needs from the OS (browse, never clone) | Madire Browser |
| <a id="ref-mbedtls"></a>**Mbed TLS** | https://github.com/Mbed-TLS/mbedtls | Dual **Apache-2.0 OR GPL-2.0-or-later** (`LICENSE`); Madire would use it under Apache-2.0 | TLS; a candidate library in `third_party/mbedtls/` (with approval) | TLS |
| <a id="ref-limine"></a>**Limine** | https://github.com/limine-bootloader/limine (redirects to `github.com/Limine-Bootloader/Limine`) | BSD-2-Clause (`COPYING`); its bundled parts are listed in its `3RDPARTY.md` (see [evaluations/limine.md](evaluations/limine.md)). **Since 0.13 also used as a tool:** the unmodified binary package of v12.9.3, fetched at build time with a pinned SHA-256 (AGENTS.md section 11, item 5); being used doesn't change its READ FULLY rule: its code is still never copied into Madire OS | The bootloader to replace GRUB; later its own boot protocol (framebuffer, all CPU cores, ACPI) | 0.13, UEFI |
| <a id="ref-dunst"></a>**Dunst** | https://github.com/dunst-project/dunst | BSD-3-Clause (`LICENSE`; GitHub names no licence) | A notification daemon: notification queues, timeouts, urgency, grouping | Desktop (notifications) |

## Study only (README, licence and design documents; not the source)

| Project | Link | Licence (verified 2026-10-12) | Studied for | Roadmap |
|---|---|---|---|---|
| <a id="ref-linux"></a>**Linux** | https://github.com/torvalds/linux; man pages: https://man7.org/linux/man-pages/ | GPL-2.0 WITH Linux-syscall-note (`COPYING`). **man7.org refused the connection from the verification host on 2026-10-12** (status unverified; the man-pages project itself is kept at kernel.org, `pub/scm/docs/man-pages/man-pages.git`) | The Linux system-call numbers and behaviour (Madire's system calls match Linux), from the man pages, `Documentation/` and the system-call table only | 1.0, the C-library milestone, Linux apps |
| <a id="ref-wine"></a>**Wine** | https://github.com/wine-mirror/wine (a mirror; official: https://gitlab.winehq.org/wine/wine) | LGPL-2.1-or-later (`LICENSE`) | How Windows programs run on top of a POSIX/Linux layer; Wine is **installed separately by users**, never part of Madire's code | Windows apps (long term) |
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

## Other references (listed before 0.12)

These entries come from earlier milestones. They were **not re-verified or reclassified** in 0.12 stage 5: until the owner classifies them, they are read under the STUDY ONLY rule, and their licences are as understood when they were listed (to be verified).

### Operating systems

| Project | Link | Could be useful for | Licence (to be verified) |
|---|---|---|---|
| <a id="ref-xv6-riscv"></a>xv6-riscv (the current xv6) | https://github.com/mit-pdos/xv6-riscv | Concepts: `copyin`/`copyout` (user pointers checked page by page when used), system-call dispatch and argument fetching, `exec` (for the 0.11 program loader); cross-checked against 0.10's system calls (stage 5b): no change needed. Its code targets RISC-V and doesn't apply to x86-64 | MIT |
| ToaruOS | https://github.com/klange/toaruos | A small from-scratch x86-64 OS: bootloader, kernel, compositor, userland; thread and process structures, scheduler, user-mode entry (0.9 study reference) | NCSA (University of Illinois/NCSA) |
| Redox OS | https://github.com/redox-os/redox | A Rust microkernel: its design, memory isolation and security; relevant to the open decision on Rust for drivers ([roadmap.md](roadmap.md#modernisation-track-planned-not-committed-to-milestones-yet)) | MIT |
| OSDev Wiki | https://wiki.osdev.org | User mode, system calls, paging, process management, context switching, ELF | **Check the wiki's content licence before quoting anything** |

**Linux documentation used since 0.10** (study only; see Linux above): `Documentation/arch/x86/` (x86-64 entry, the IST stacks), `Documentation/security/self-protection.rst` (SMEP, SMAP, W^X), `Documentation/mm/` (address spaces), `Documentation/scheduler/`, `Documentation/filesystems/` (the VFS, for 1.0) and `Documentation/driver-api/` (drivers). **System-call behaviour:** the Linux man pages (for example `syscall(2)`, `syscalls(2)`, `write(2)`, `nanosleep(2)`, `getpid(2)`, `sched_yield(2)`, `exit(2)`) are the reference for numbers, arguments, return values and error codes; only these facts are used.

### Windows compatibility

| Project | Link | Could be useful for | Licence (to be verified) |
|---|---|---|---|
| Proton | https://github.com/ValveSoftware/Proton | Wine with improvements for games (graphics translation, controller and performance work) and how it is packaged and tested | Valve's code and each bundled component have their own licences, all to be verified |

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
