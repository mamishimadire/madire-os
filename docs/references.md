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
| SerenityOS | https://github.com/SerenityOS/serenity | A complete hobby OS written from scratch: kernel, userland, GUI, a POSIX-like system-call layer | BSD-2-Clause |
| ToaruOS | https://github.com/klange/toaruos | A small from-scratch x86-64 OS: bootloader, kernel, compositor, userland | NCSA (University of Illinois/NCSA) |
| ReactOS | https://github.com/reactos/reactos | Windows-compatible kernel and driver model; running Windows applications | GPL-2.0 (some parts LGPL) |
| Linux | https://github.com/torvalds/linux | Drivers, the POSIX/Linux system-call interface, x86-64 boot and memory management | GPL-2.0 with the system-call exception |
| Apple open source (apple-oss-distributions) | https://github.com/apple-oss-distributions | XNU kernel and system components, for how a desktop/laptop OS is structured | Mixed (APSL 2.0, BSD and others, per repository) |

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
