# Madire Filesystem Layout v0.1

**Versions:** v0.1 (Madire OS 1.1, stage 9b, docs only): the layout, the reason for it, what exists today and when each part arrives.

This document sets where things live in Madire OS's file tree. It is a plan, not a commitment: every folder not marked as existing today arrives with its milestone, which still needs its own plan and approval. The commands that work on files are in the command standard ([commands.md](commands.md)).

## 1. The layout

Madire OS uses the standard Linux layout (the Filesystem Hierarchy Standard, as Linux distributions use it):

| Path | What it holds |
|---|---|
| `/boot` | The boot loader's files and the kernel |
| `/dev` | Devices, as files (disks, the console, `null`, random numbers) |
| `/etc` | System-wide settings, as text files (for example `fstab` and `sudoers`, section 4) |
| `/home/<user>` | Each user's own files, with the folders `Desktop`, `Documents`, `Downloads`, `Music`, `Pictures`, `Videos` and `Projects` |
| `/media` | Removable media (USB drives, discs), attached automatically |
| `/mnt` | Filesystems attached by hand, for a while |
| `/opt` | Optional add-on applications installed as one folder each |
| `/proc` | Processes and kernel state, as files (made by the kernel, not stored) |
| `/root` | The administrator's home folder |
| `/run` | State of the running system (process numbers, sockets); emptied at every start |
| `/sys` | Devices, drivers and kernel settings, as files (made by the kernel, not stored) |
| `/tmp` | Temporary files; may be emptied at every start |
| `/usr/bin` | Programs: the SYSTEM commands and installed applications |
| `/usr/lib` | Libraries the programs use |
| `/usr/share` | Data the programs share, independent of the processor (manual pages, icons, fonts, the time zones) |
| `/var` | Data that changes while the system runs (caches, queues, the package manager's state) |
| `/var/log` | The system log (`logs`) and the services' logs |

`/bin` and `/lib` are links into `/usr` (`/bin` → `/usr/bin`, `/lib` → `/usr/lib`), as in current Linux distributions, so programs are found under either name.

## 2. Why the Linux layout

- **Madire implements Linux system calls** (numbers and behaviour, since 0.10), so Linux programs can run on it in the Linux-compatibility milestone ([roadmap.md](roadmap.md)).
- **Linux programs hard-code these paths:** the C library reads `/etc` (for example the time zone and name lookup), programs look for `/tmp`, `/dev/null` and `/proc/self`, scripts start with `#!/bin/sh`, and a dynamically linked program names its loader under `/lib`. A different layout would break them, or would need a translation layer in the kernel.
- **Friendly names belong in the graphical file manager**, not in the tree: it can show `/home/<user>/Documents` as "Documents" and `/media` as "Removable drives", and hide the system folders from users who don't need them. The paths stay the same for programs and the command line.

## 3. What exists today, and when each part arrives

**Today (1.0, Live mode):** a RAM filesystem with `/`, `/home` and `/tmp`, made at boot; every file is in memory only and is lost at restart. The shell starts in `/home`. There are no users yet, so there is no `/home/<user>`.

| Part | Arrives with | Note |
|---|---|---|
| `/`, `/home`, `/tmp` | 1.0 (done) | In memory (Live mode) |
| The whole tree on disk (`/boot`, `/etc`, `/home`, `/opt`, `/root`, `/usr`, `/var`, `/var/log`, the `/bin` and `/lib` links, and the empty `/media`, `/mnt`, `/run`) | The storage milestone (likely v1.3, after graphics in v1.2) | The disk filesystem creates the tree; files kept across restarts. Live mode keeps the tree in memory |
| `/home/<user>` and its folders, `/root` as the administrator's home | Users | Created when a user is created |
| `/dev` | A virtual filesystem for devices | Needs the disk and hardware drivers to have devices to show |
| `/proc`, `/sys` | Virtual filesystems made by the kernel | Needed by many Linux programs; with or before the Linux-compatibility milestone |
| `/etc` with real settings | Settings storage | Settings kept as text files under `/etc` |
| `/usr/bin` with programs | The SYSTEM commands as separate programs (today they run inside the kernel console), then the package manager | Programs are boot modules until then |
| `/var/log` with the system log | The service manager and `logs` | `dmesg` stays the kernel's own log |

## 4. Configuration files under `/etc`

Configuration that Linux keeps in files stays in files, under `/etc`. They are not commands (see [commands.md](commands.md), section 9):

| File | What it holds | Read by |
|---|---|---|
| `/etc/fstab` | The filesystems attached at start, and where | The start-up; `mount` and `umount` attach and detach filesystems by hand |
| `/etc/sudoers` | Who may run commands with administrator rights | `sudo` |

More files are added to this table as their milestones arrive (for example the host name and the time zone, with settings storage).
