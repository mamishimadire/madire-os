# External references

These projects are **reference only, not copied into Madire OS**. They may be read to learn how others solved a problem. No code, text or assets from them go into this repository without explicit approval and a licence check. Madire OS has no licence of its own yet, and one must be decided before any outside open-source code is included.

The licences below are as understood when this list was written, **to be verified** against each project before any use.

## Rules

1. Reference only; never copied into `madire-os` without explicit approval and a licence check.
2. To study a project, read specific files on GitHub, or shallow-clone it (`git clone --depth 1`) into a directory **outside** the repository, for example `/workspaces/references` in the Codespace. Never clone inside the repository.
3. The security tools below may be read as references only. They are **never run against any system**.
4. Chromium and Gecko are not cloned (they are very large); browse them on GitHub if needed.
5. When a milestone plan uses one of these projects, it names the project, says what was studied and why, and states its licence.

## Operating systems

| Project | Link | Could be useful for | Licence (to be verified) |
|---|---|---|---|
| xv6 (MIT's teaching kernel; the current version targets RISC-V, the older `xv6-public` 32-bit x86) | https://github.com/mit-pdos/xv6-riscv | A small Unix-like teaching kernel: processes, context switching, a round-robin scheduler, sleep and wakeup, system calls; short enough to read in full | MIT |
| SerenityOS | https://github.com/SerenityOS/serenity | A complete hobby OS written from scratch: kernel, userland, GUI, a POSIX-like system-call layer | BSD-2-Clause |
| ToaruOS | https://github.com/klange/toaruos | A small from-scratch x86-64 OS: bootloader, kernel, compositor, userland | NCSA (University of Illinois/NCSA) |
| ReactOS | https://github.com/reactos/reactos | Windows-compatible kernel and driver model; running Windows applications. A reference for Windows compatibility (system interfaces, the driver model); no code copied | GPL-2.0 (some parts LGPL) |
| Linux | https://github.com/torvalds/linux | Drivers, the POSIX/Linux system-call interface, x86-64 boot and memory management | GPL-2.0 with the system-call exception |
| Apple open source (apple-oss-distributions) | https://github.com/apple-oss-distributions | XNU kernel and system components, for how a desktop/laptop OS is structured | Mixed (APSL 2.0, BSD and others, per repository) |

### Study references for kernel threads and scheduling (0.9)

For 0.9 and the coming user processes (0.10), three of the projects above are listed as **study-only** references: places to read how other kernels structure threads, context switches, scheduling and the path to user mode:

| Project | Worth reading for | Licence (to be verified) |
|---|---|---|
| xv6 | the structure of a minimal context switch, a run queue, sleep/wakeup and the kernel/user boundary | MIT |
| ToaruOS | an x86-64 kernel's thread and process structures, its scheduler and user-mode entry | NCSA |
| SerenityOS | a larger kernel's threads, scheduler and POSIX-like system calls | BSD-2-Clause |

No code, text or assets from them are in Madire OS: the 0.9 scheduler, context switch and tests were written for this project. The licences are as understood now and must be checked against each project before any use, under the rules above.

## Windows compatibility layer (study-only)

References for the Windows-compatibility layer of the applications and compatibility strategy ([roadmap.md](roadmap.md#applications-and-compatibility-strategy-planned-not-tied-to-milestones-yet)). Study-only: nothing is copied; porting either project would need the owner's approval, a licence check and the Linux/POSIX compatibility it depends on.

| Project | Link | Could be useful for | Licence (to be verified) |
|---|---|---|---|
| Wine | https://gitlab.winehq.org/wine/wine | Running Windows programs on a POSIX-like system: the Windows API implemented on top of it, the PE loader, the registry, graphics and audio translation | LGPL (to be verified) |
| Proton | https://github.com/ValveSoftware/Proton | Wine with improvements for games (graphics translation, controller and performance work) and how it is packaged and tested | Valve's code and each bundled component have their own licences, all to be verified |

## Shells

| Project | Link | Could be useful for | Licence (to be verified) |
|---|---|---|---|
| GNU Bash (a mirror of GNU Bash) | https://github.com/bminor/bash | Shell-language design: variables, `if` and loops, pipes, redirection, quoting, job control. Possibly a future installable APPLICATION (a separate program, with the GPL's source obligations). No code may be copied into Madire OS | GPL-3.0 |
| PowerShell | https://github.com/PowerShell/PowerShell | Design inspiration: pipelines of structured objects, consistent verb-noun naming, built-in help for every command. It needs .NET, so it isn't a porting target for now | MIT |

## Terminal, desktop and app-platform references (Windows)

These are Windows-specific technologies: none of them can run on Madire OS. They are read for ideas only, for later phases. Microsoft names and logos are trademarks; Madire's equivalents get their own names under the naming rule.

| Project | Link | Could be useful for | Phase | Licence (to be verified) |
|---|---|---|---|---|
| Windows Terminal | https://github.com/microsoft/terminal | A future Madire terminal app: parsing terminal control codes, fast text rendering, tabs, themes | GUI | MIT |
| PowerToys | https://github.com/microsoft/PowerToys | Feature ideas for the Madire desktop: window snapping, a quick launcher, a colour picker, keyboard remapping, extracting text from the screen | Desktop | MIT |
| WinUI | https://github.com/microsoft/microsoft-ui-xaml | Designing Madire's own UI toolkit: controls, layout, theming, accessibility | GUI toolkit | MIT |
| Windows universal samples | https://github.com/microsoft/Windows-universal-samples | A checklist of the app capabilities a modern OS provides (sensors, Bluetooth, camera, notifications, permissions), for Madire's app interface and permission model | Apps | MIT |

## Browser engines

| Project | Link | Could be useful for | Licence (to be verified) |
|---|---|---|---|
| Ladybird | https://github.com/LadybirdBrowser/ladybird | An independent browser engine; what a browser needs from the OS | BSD-2-Clause |
| Chromium | https://github.com/chromium/chromium | The planned Chromium-based Madire Browser: what it needs from the OS (browse only, never clone) | BSD-3-Clause (plus third-party licences) |
| Gecko (mozilla/gecko-dev) | https://github.com/mozilla/gecko-dev | Another browser engine for comparison (browse only, never clone) | MPL-2.0 |

## Future Madire Security Toolkit

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
