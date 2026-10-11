# Madire OS Command Standard (MOCS) v0.7

**Versions:** v0.1 (after Madire OS 0.9): the standard. v0.2 (Madire OS 0.9.1): `history`, `uname`, `hostname`, `date`, `dmesg` and `reboot` IMPLEMENTED, and `help <command>` and `<command> --help` for every implemented command. v0.3 (Madire OS 0.9.2): the "Console keys" section (scrollback); `dmesg` no longer shows key events by default. v0.4 (Madire OS 0.10): `run`, `kill` and the process list in `ps` move to 0.11 (0.10 brings user processes, 0.11 the programs and process management); new PLANNED commands `pushd`, `popd`, `prompt`, `pause`, `color`, `title`, `label`, `vol`, `attrib`, `defaults` and `bootcfg`; the "Windows equivalents" table; "Did you mean" suggestions and help sections (planned). v0.5 (Madire OS 0.10): naming rule (Madire's own or open Unix/POSIX names, no Microsoft/DOS-specific names); `pause` → `presskey`, `color` → `colors`, `title` → `wintitle`, `attrib` → `fileattr`; `label` and `vol` become the subcommands `disk label` and `disk info`; `bootcfg` (itself a Windows command name) → `bootconf`. v0.6 (Madire OS 0.11): `run` (with `run -b`) and `kill` IMPLEMENTED, `ps` lists processes too ("Show threads and processes"); Ctrl+C in the "Console keys" section; a non-interactive `kill -y <pid>` noted for scripts (planned). v0.7 (Madire OS 0.12): `prompt`, `alias`, `unalias`, `set`, `export`, `env` (listing only), `sleep`, `presskey` and `time` IMPLEMENTED, the new `unset` IMPLEMENTED, `kill -y <pid>` IMPLEMENTED; "Did you mean" suggestions and help sections (`help shell`, `help system`, `help programs`) IMPLEMENTED; Ctrl+C also cancels `sleep` and `presskey`; `env` running a program with a changed environment, and `$NAME` expansion, stay PLANNED.

This standard lists the commands Madire OS has and plans to have: one name, one meaning and one level for each, so that a new user can understand the system without already knowing Linux or Windows. Everything not marked IMPLEMENTED is a plan, not a commitment: each command still needs its milestone plan and approval before it is built.

## 1. Purpose and levels

| Level | Meaning |
|---|---|
| **CORE** | Built into the Madire shell itself, because it reads or changes the shell's own state (current folder, variables, aliases, history, background jobs) or is part of the shell language. A separate program couldn't do it |
| **SYSTEM** | Supplied with Madire OS as a system utility: in the long term a separate program, which the shell starts |
| **APPLICATION** | Installed separately (for example through `pkg install`) |

`which` is CORE because only the shell knows its own aliases and built-in commands, which `which` must report before searching for programs.

Madire OS provides the foundations that applications need (processes, files, devices, networking, permissions). It does not implement tools such as `nmap` or `git` itself: they are applications that run on those foundations.

## 2. Status key

| Status | Meaning |
|---|---|
| **IMPLEMENTED** | Exists today and is tested (with the Madire OS version that added it) |
| **PLANNED** | Planned; the entry names the foundation it needs first |
| **APPLICATION** | An installable application, not part of Madire OS |

**Implemented today (Madire OS 0.12):** 26 commands: `help`, `clear`, `echo`, `version`, `uptime`, `mem`, `cpu` (0.5), `ps` (0.9; processes too since 0.11), `history`, `uname`, `hostname`, `date`, `dmesg`, `reboot` (0.9.1), `run`, `kill` (0.11; `kill -y` 0.12), and `prompt`, `alias`, `unalias`, `set`, `unset`, `export`, `env`, `sleep`, `presskey`, `time` (0.12). Since 0.9.1 every implemented command also answers `help <command>` and `<command> --help`.

**In this standard (v0.7):** 183 commands: 26 IMPLEMENTED, 144 PLANNED and 13 APPLICATION (by level: 21 CORE, 149 SYSTEM, 13 APPLICATION).

**Notes on the 0.9.1 commands:**

- `hostname` is a fixed name, `madire`, until settings can be stored.
- `date` shows the real-time clock as it is, with **no time zone** ("time zone not set"): VirtualBox keeps the clock on the host's local time, QEMU on UTC. Years are assumed to be 2000–2099, and the date can't be set.
- `dmesg` shows only what the kernel wrote through `kprintf`/`klogf` (a 32 KiB log). The boot screen's status lines and the console's command output go to the serial port directly and aren't in it (a boot-screen line can therefore appear joined to the next one). Since 0.9.2 key events aren't logged by default, so `dmesg` holds no typed keys (they are logged only with the boot option `keylog`, for tests, or during the keyboard diagnostics).
- `help <unknown>` prints only `No help for 'X': unknown command.`, without a second sentence, so that every line fits 79 columns (names longer than 40 characters are shortened).

**Notes on the 0.12 commands** (the decisions are in the decision log of [PROGRESS.md](PROGRESS.md)):

- **Everything is in memory and lost at restart:** the prompt, the aliases and the variables. Every limit below is an error message, never a silent cut.
- **Help sections:** `help` keeps its list and order (the 0.12 commands are appended) and ends with `Sections: shell system programs (help <section>)`; `help <name>` looks up a section first, then a command. The full list is 28 lines since 0.12, so its first lines scroll into the scrollback (a one-screen design is a follow-up in PROGRESS.md).
- **"Did you mean":** an unknown command keeps its line `Unknown command: X. Type 'help' for a list of commands.` and gets a second line `Did you mean: …?` with up to 3 names at edit distance at most 2 (case-sensitive; nearest first, then alphabetical), from the commands offered and the current aliases. Nothing is run. `help <unknown>` suggests over sections and commands.
- **`prompt <text>`:** 1–8 printable ASCII characters, no spaces, not starting with `-`, filling the 8 prompt columns (so the 71-character input line still fits 79 columns); `prompt --reset` restores `madire> `.
- **`alias <name> <command...>`:** at most 16 aliases; names `[a-z0-9_]{1,15}`, never a built-in's name; the command at most 63 characters and starting with a built-in (so no alias names another); only the first word of a line is expanded, once, and the line is split again under the 8-word limit. `alias` lists them, `alias <name>` shows one.
- **`set NAME=value`, `set NAME`, `set`:** at most 32 variables; names `[A-Za-z_][A-Za-z0-9_]{0,15}`, case-sensitive, listed in byte order; values at most 63 characters (the value is the rest of the word and the words after it, joined with single spaces; `set A=` gives an empty value). `unset NAME` removes a variable and its export mark. `export NAME` marks a set variable (`export NAME=value` is refused); `env` lists the exported ones. **No variable reaches a program yet**, and there is no `$NAME` expansion: both are PLANNED (passing an environment needs the System V start-up layout, see the follow-ups in PROGRESS.md).
- **`sleep <seconds>`** waits 1–3600 whole seconds without blocking the kernel; **`presskey`** prints `Press any key to continue...` and waits for a key, which is not typed (Shift, Ctrl, Alt, the lock keys and the Windows keys alone don't count). Ctrl+C cancels either (`sleep: cancelled.`, `presskey: cancelled.`); both are logged only as fixed lines, never as typed keys.
- **`time <command...>`** runs a built-in command and then prints `time: N ticks (X.XX s)`, after a program's end line, a wait or a question's answer. It doesn't expand aliases (`time: 'll' is an alias; time runs built-in commands only.`), and `time time` is refused.
- **`kill -y <pid>`** ends a process without asking; the refusals are the same as for `kill <pid>` (usage `kill [-y] <pid>`).

**Today: where the commands run.** Since 0.11 `run` starts programs (ELF files delivered as boot modules), but all 26 implemented commands still run **inside the kernel console**, as part of the kernel's built-in shell, whatever their level. Now that programs can be loaded and managed, SYSTEM commands are expected to move out into separate user programs (once a filesystem holds them, 1.0); CORE commands stay in the shell.

| Command | Level | Today |
|---|---|---|
| `help`, `echo`, `history`, `prompt`, `alias`, `unalias`, `set`, `unset`, `export`, `time` | CORE | in the kernel console; stays in the shell |
| `clear`, `version`, `uptime`, `mem`, `cpu`, `ps`, `uname`, `hostname`, `date`, `dmesg`, `reboot`, `run`, `kill`, `env`, `sleep`, `presskey` | SYSTEM | in the kernel console; expected to become separate programs once a filesystem holds them |

**Foundations** named below: processes (0.10 user processes, 0.11 programs and process management), filesystem (1.0), pipes and the shell language, disk drivers, hardware drivers (PCI, USB and devices), network stack, users, power management (ACPI), graphics, settings storage, service manager, package manager, Madire AI.

## 3. Commands

Each entry: name, purpose, level, status (and, for PLANNED, the foundation it needs). Commands marked ⚠ change or remove data or interrupt the system; see the safety rule in section 6.

### Shell and console

| Command | Purpose | Level | Status |
|---|---|---|---|
| `help` | List the commands; `help <command>` shows one command's purpose and usage; `help <section>` lists one section (`shell`, `system`, `programs`) | CORE | IMPLEMENTED (0.5; `help <command>` and `<command> --help`: 0.9.1; sections: 0.12) |
| `clear` | Clear the screen | SYSTEM | IMPLEMENTED |
| `echo` | Print text | CORE | IMPLEMENTED |
| `history` | Show the commands entered since boot (the last 16, numbered); Up/Down recall them | CORE | IMPLEMENTED (0.9.1; kept across restarts once the filesystem exists) |
| `alias` | Create, list or show short names for commands: `alias [name [command...]]` | CORE | IMPLEMENTED (0.12, in memory; kept across restarts: PLANNED, filesystem) |
| `unalias` | Remove a short name: `unalias <name>` | CORE | IMPLEMENTED (0.12) |
| `which` | Show which program a command name runs | CORE | PLANNED (filesystem) |
| `man` | Show the manual page of a command | SYSTEM | PLANNED (filesystem) |
| `exit` | Leave the shell | CORE | PLANNED (processes: a shell running as a process) |
| `cd` | Change the current folder | CORE | PLANNED (filesystem) |
| `pwd` | Show the current folder | CORE | PLANNED (filesystem) |
| `sleep` | Wait for a given time: `sleep <seconds>` (1–3600; Ctrl+C cancels) | SYSTEM | IMPLEMENTED (0.12; fractions of a second: PLANNED) |
| `jobs` | List programs running in the background | CORE | PLANNED (processes, job control) |
| `fg` | Bring a background program to the foreground | CORE | PLANNED (processes, job control) |
| `bg` | Continue a stopped program in the background | CORE | PLANNED (processes, job control) |
| `time` | Measure how long a command takes to run: `time <command...>` (in ticks and seconds) | CORE | IMPLEMENTED (0.12; built-in commands, and `run` until the program's end) |
| `pushd` | Change to a folder and remember the previous one on a stack | CORE | PLANNED (filesystem; shell state) |
| `popd` | Return to the folder last remembered by `pushd` | CORE | PLANNED (filesystem; shell state) |
| `prompt` | Show or change the shell prompt: `prompt [text \| --reset]` | CORE | IMPLEMENTED (0.12, in memory) |
| `presskey` | Wait until a key is pressed (in scripts; Ctrl+C cancels) | SYSTEM | IMPLEMENTED (0.12) |
| `colors` | Show or set the console's colour scheme (text and background colours) | SYSTEM | PLANNED (terminal app, graphics) |
| `wintitle` | Set the terminal window's title | SYSTEM | PLANNED (terminal app, graphics) |

### System information

| Command | Purpose | Level | Status |
|---|---|---|---|
| `version` | Show the Madire OS version | SYSTEM | IMPLEMENTED |
| `uptime` | Show how long the system has been running | SYSTEM | IMPLEMENTED |
| `mem` | Show the full memory report (RAM, pages, paging, heap, stack) | SYSTEM | IMPLEMENTED (the alias `memory`: PLANNED) |
| `cpu` | Show processor information | SYSTEM | IMPLEMENTED |
| `uname` | Print a short, script-friendly identification of the system (`uname -a`: name, host, version, architecture) | SYSTEM | IMPLEMENTED (0.9.1) |
| `hostname` | Show the computer's name (a fixed name, `madire`, for now) | SYSTEM | IMPLEMENTED (0.9.1; changing it: PLANNED, settings storage) |
| `date` | Show the date and time (from the real-time clock; no time zone yet) | SYSTEM | IMPLEMENTED (0.9.1; time zones and setting the date: PLANNED, settings storage) |
| `system` | Show a readable summary of the whole system: OS, version, CPU, memory, disks, devices, uptime | SYSTEM | PLANNED (disk and hardware drivers) |
| `disk` | Show disks and how they are used; subcommands `disk label` (show or change a volume's name) and `disk info` (a volume's name and serial number, read only) | SYSTEM | PLANNED (disk drivers; `disk label` and `disk info` also need the filesystem) |
| `devices` | Show connected hardware | SYSTEM | PLANNED (hardware drivers) |
| `free` | Show memory use in one compact table (used, free, total) | SYSTEM | PLANNED (no new foundation) |
| `lscpu` | Show the processor's cores, caches and topology in detail | SYSTEM | PLANNED (multi-core support, ACPI tables) |
| `lspci` | List PCI devices | SYSTEM | PLANNED (hardware drivers: PCI) |
| `lsusb` | List USB devices | SYSTEM | PLANNED (hardware drivers: USB) |
| `drivers` | List loaded drivers and their devices | SYSTEM | PLANNED (hardware drivers) |

### Processes

| Command | Purpose | Level | Status |
|---|---|---|---|
| `ps` | List what is running: the kernel threads, then the processes (PID, state, TID, name) | SYSTEM | IMPLEMENTED (0.9; processes 0.11) |
| `run` | Start a program: `run <name> [n]` in the foreground (its output on the screen; Ctrl+C ends it), `run -b <name> [n]` in the background (output to the serial log and `dmesg` only) | SYSTEM | IMPLEMENTED (0.11) |
| `kill` ⚠ | End a process by its number: `kill <pid>` asks first (`yes/no>`; only exactly "yes" ends it); `kill -y <pid>` ends it without the question, for scripts | SYSTEM | IMPLEMENTED (0.11; `-y`: 0.12) |
| `pkill` ⚠ | End processes by name | SYSTEM | PLANNED (processes) |
| `top` | Show running processes and their resource use, updated live | SYSTEM | PLANNED (processes) |
| `nice` | Start a program with a given priority | SYSTEM | PLANNED (processes, scheduler priorities) |
| `renice` | Change a running program's priority | SYSTEM | PLANNED (processes, scheduler priorities) |

### Files and folders (needs the filesystem, 1.0)

| Command | Purpose | Level | Status |
|---|---|---|---|
| `ls` | List files and folders | SYSTEM | PLANNED (filesystem) |
| `mkdir` | Create a folder | SYSTEM | PLANNED (filesystem) |
| `rmdir` | Remove an empty folder | SYSTEM | PLANNED (filesystem) |
| `touch` | Create an empty file, or update a file's time | SYSTEM | PLANNED (filesystem) |
| `rm` ⚠ | Remove files or folders | SYSTEM | PLANNED (filesystem) |
| `cp` | Copy files or folders | SYSTEM | PLANNED (filesystem) |
| `mv` ⚠ | Move or rename files or folders (may replace an existing file) | SYSTEM | PLANNED (filesystem) |
| `ln` | Create a link to a file | SYSTEM | PLANNED (filesystem) |
| `tree` | Show a folder structure as a tree | SYSTEM | PLANNED (filesystem) |
| `file` | Identify a file's type | SYSTEM | PLANNED (filesystem) |
| `stat` | Show a file's details | SYSTEM | PLANNED (filesystem) |
| `find` | Search for files and folders by name or property | SYSTEM | PLANNED (filesystem) |
| `locate` | Find files quickly through an index | SYSTEM | PLANNED (filesystem) |
| `fileattr` | Show or change a file's attributes (read-only, hidden) | SYSTEM | PLANNED (filesystem) |

### Reading and text (needs the filesystem and pipes)

| Command | Purpose | Level | Status |
|---|---|---|---|
| `cat` | Show a file's contents | SYSTEM | PLANNED (filesystem) |
| `less` | View text page by page, backwards and forwards, with search | SYSTEM | PLANNED (filesystem) |
| `more` | View text page by page, forwards only (a minimal pager) | SYSTEM | PLANNED (filesystem) |
| `head` | Show the beginning of a file | SYSTEM | PLANNED (filesystem, pipes) |
| `tail` | Show the end of a file | SYSTEM | PLANNED (filesystem, pipes) |
| `grep` | Search for text | SYSTEM | PLANNED (filesystem, pipes) |
| `sort` | Sort lines | SYSTEM | PLANNED (filesystem, pipes) |
| `uniq` | Remove repeated lines | SYSTEM | PLANNED (filesystem, pipes) |
| `wc` | Count lines, words and characters | SYSTEM | PLANNED (filesystem, pipes) |
| `diff` | Show the differences between two files | SYSTEM | PLANNED (filesystem) |
| `cmp` | Compare two files byte by byte | SYSTEM | PLANNED (filesystem) |
| `cut` | Extract columns or parts of lines | SYSTEM | PLANNED (filesystem, pipes) |
| `paste` | Join lines of files side by side | SYSTEM | PLANNED (filesystem, pipes) |
| `tee` | Show output and save it to a file at the same time | SYSTEM | PLANNED (filesystem, pipes) |
| `sed` | Edit text in a stream (find and replace) | SYSTEM | PLANNED (filesystem, pipes) |
| `awk` | Process text by fields and patterns | SYSTEM | PLANNED (filesystem, pipes) |
| `xargs` | Build commands from input lines | SYSTEM | PLANNED (processes, pipes) |
| `printf` | Print formatted text | CORE | PLANNED (shell language) |
| `test` | Check a condition (in scripts) | CORE | PLANNED (shell language, filesystem) |
| `sha256sum` | Compute or check SHA-256 checksums | SYSTEM | PLANNED (filesystem) |
| `md5sum` | Compute or check MD5 checksums (legacy; not for security) | SYSTEM | PLANNED (filesystem) |
| `hexdump` | Show a file's bytes in hexadecimal | SYSTEM | PLANNED (filesystem) |
| `base64` | Encode or decode Base64 | SYSTEM | PLANNED (filesystem, pipes) |

### Editing

| Command | Purpose | Level | Status |
|---|---|---|---|
| `edit` (name to be decided) | A terminal text editor | SYSTEM | PLANNED (filesystem) |

### Environment

| Command | Purpose | Level | Status |
|---|---|---|---|
| `env` | Show the environment (the exported variables), or run a program with a changed one | SYSTEM | IMPLEMENTED (0.12, the listing; running a program with a changed environment: PLANNED, processes: the System V start-up layout) |
| `export` | Make a variable available to programs started from the shell: `export <NAME>` | CORE | IMPLEMENTED (0.12, marks the variable; programs receiving it: PLANNED, processes) |
| `set` | Show or change shell variables and options: `set [NAME[=value]]` | CORE | IMPLEMENTED (0.12, variables; options: PLANNED, shell language) |
| `unset` | Remove a shell variable (and its export mark): `unset <NAME>` | CORE | IMPLEMENTED (0.12) |

### Storage (needs disk drivers)

| Command | Purpose | Level | Status |
|---|---|---|---|
| `lsblk` | List storage devices and partitions | SYSTEM | PLANNED (disk drivers) |
| `mount` | Attach a filesystem | SYSTEM | PLANNED (disk drivers, filesystem) |
| `umount` | Detach a filesystem | SYSTEM | PLANNED (disk drivers, filesystem) |
| `df` | Show free space per filesystem | SYSTEM | PLANNED (disk drivers, filesystem) |
| `du` | Show the space files and folders use | SYSTEM | PLANNED (filesystem) |
| `fsck` ⚠ | Check and repair a filesystem | SYSTEM | PLANNED (disk drivers, filesystem) |
| `format` ⚠ | Prepare a storage device for use (erases it) | SYSTEM | PLANNED (disk drivers, filesystem) |
| `partition` ⚠ | Create or change disk partitions | SYSTEM | PLANNED (disk drivers) |

### Archives

| Command | Purpose | Level | Status |
|---|---|---|---|
| `tar` | Create or extract archives | SYSTEM | PLANNED (filesystem) |
| `zip` | Create ZIP archives | SYSTEM | PLANNED (filesystem) |
| `unzip` | Extract ZIP archives | SYSTEM | PLANNED (filesystem) |
| `gzip` | Compress files | SYSTEM | PLANNED (filesystem) |
| `gunzip` | Decompress files | SYSTEM | PLANNED (filesystem) |

### Users

| Command | Purpose | Level | Status |
|---|---|---|---|
| `whoami` | Show the current user | SYSTEM | PLANNED (users) |
| `who` | Show who is logged in | SYSTEM | PLANNED (users) |
| `id` | Show a user's user and group numbers | SYSTEM | PLANNED (users) |
| `groups` | Show the groups a user belongs to | SYSTEM | PLANNED (users) |
| `useradd` | Create a user | SYSTEM | PLANNED (users, filesystem) |
| `usermod` | Change a user | SYSTEM | PLANNED (users, filesystem) |
| `userdel` ⚠ | Delete a user | SYSTEM | PLANNED (users, filesystem) |
| `passwd` | Change a password | SYSTEM | PLANNED (users, filesystem) |
| `groupadd` | Create a group | SYSTEM | PLANNED (users, filesystem) |
| `groupdel` ⚠ | Delete a group | SYSTEM | PLANNED (users, filesystem) |

### Permissions

| Command | Purpose | Level | Status |
|---|---|---|---|
| `chmod` | Change who may read, write or run a file | SYSTEM | PLANNED (users, filesystem) |
| `chown` | Change a file's owner | SYSTEM | PLANNED (users, filesystem) |
| `chgrp` | Change a file's group | SYSTEM | PLANNED (users, filesystem) |
| `sudo` | Run one command with administrator rights, after confirmation | SYSTEM | PLANNED (users, processes) |
| `permissions` | Show or manage what apps may use (camera, microphone, location, files) | SYSTEM | PLANNED (users, the app permission model) |

### Networking (needs the network stack)

| Command | Purpose | Level | Status |
|---|---|---|---|
| `ip` | Show or configure network interfaces and addresses | SYSTEM | PLANNED (network stack) |
| `ping` | Test whether another computer can be reached | SYSTEM | PLANNED (network stack) |
| `traceroute` | Show the path to a network destination | SYSTEM | PLANNED (network stack) |
| `route` | Show or configure network routes | SYSTEM | PLANNED (network stack) |
| `arp` | Show network address mappings | SYSTEM | PLANNED (network stack) |
| `dns` | Look up names and addresses (DNS) | SYSTEM | PLANNED (network stack) |
| `netstat` | Show network connections and statistics | SYSTEM | PLANNED (network stack) |
| `ss` | Show network sockets in detail | SYSTEM | PLANNED (network stack) |
| `curl` | Transfer data to or from a network service | SYSTEM | PLANNED (network stack) |
| `wget` | Download files | SYSTEM | PLANNED (network stack, filesystem) |
| `ssh` | Connect securely to another computer | SYSTEM | PLANNED (network stack, users) |
| `scp` | Copy files securely between computers | SYSTEM | PLANNED (network stack, filesystem) |

`netstat` and `ss` overlap: `netstat` gives the familiar summary of connections and statistics, `ss` the detailed per-socket view.

### Modern hardware

| Command | Purpose | Level | Status |
|---|---|---|---|
| `wifi` | Show and join Wi-Fi networks | SYSTEM | PLANNED (hardware drivers: Wi-Fi, network stack) |
| `bluetooth` | Show and pair Bluetooth devices | SYSTEM | PLANNED (hardware drivers: Bluetooth) |
| `sensors` | Show temperatures, fans and other sensors | SYSTEM | PLANNED (hardware drivers, ACPI) |
| `display` | Show or change screens and resolutions | SYSTEM | PLANNED (graphics) |
| `volume` | Show or change the sound volume | SYSTEM | PLANNED (hardware drivers: audio) |
| `camera` | Show cameras and their permission state | SYSTEM | PLANNED (hardware drivers: camera, the app permission model) |

### Packages

| Command | Purpose | Level | Status |
|---|---|---|---|
| `pkg` | Manage installable software through subcommands: `pkg install`, `pkg remove` ⚠, `pkg update` (refresh the package list), `pkg upgrade` (install newer versions), `pkg search`, `pkg list`, `pkg info`, `pkg clean` (remove downloaded package files), `pkg autoremove` ⚠ (remove dependencies nothing uses) | SYSTEM | PLANNED (package manager, filesystem, network stack) |

These are subcommands of `pkg`, not separate commands. `pkg remove` is the one name for uninstalling.

### Services

| Command | Purpose | Level | Status |
|---|---|---|---|
| `services` | List the system's services and their state | SYSTEM | PLANNED (service manager) |
| `service` | Manage one service through subcommands: `start`, `stop`, `restart`, `enable` (start at boot), `disable`, `status`, `logs` (that service's log) | SYSTEM | PLANNED (service manager, processes) |

### Power

| Command | Purpose | Level | Status |
|---|---|---|---|
| `reboot` ⚠ | Restart the computer, after confirmation (type `yes`) | SYSTEM | IMPLEMENTED (0.9.1) |
| `shutdown` ⚠ | Shut down in an orderly way: warn, stop services, save data, then power off (optionally after a delay) | SYSTEM | PLANNED (power management, service manager) |
| `poweroff` ⚠ | Power off now (after saving data), with no delay or warning period | SYSTEM | PLANNED (power management) |
| `suspend` | Sleep: keep the session in memory, use very little power | SYSTEM | PLANNED (power management) |
| `hibernate` | Save the session to disk and power off | SYSTEM | PLANNED (power management, disk drivers) |
| `battery` | Show battery level and state | SYSTEM | PLANNED (power management) |

### Diagnostics

| Command | Purpose | Level | Status |
|---|---|---|---|
| `dmesg` | Show recent kernel messages (the kernel's log buffer): `dmesg [lines]` | SYSTEM | IMPLEMENTED (0.9.1; boot-screen and console output aren't in the log) |
| `logs` | Show the system log (all services and the kernel, kept on disk) | SYSTEM | PLANNED (filesystem, service manager) |
| `diagnose` | Find the likely cause of a problem in one area (e.g. `diagnose network`) | SYSTEM | PLANNED (system utilities) |
| `health` | Check the overall health of the system (disks, memory, temperatures, updates) | SYSTEM | PLANNED (system utilities) |
| `repair` ⚠ | Try to fix a problem found by `diagnose` or `health`, after confirmation | SYSTEM | PLANNED (system utilities) |
| `debug` | Show low-level state for developers | SYSTEM | PLANNED (system utilities) |
| `report` | Collect a diagnostic report into a file to share | SYSTEM | PLANNED (filesystem, system utilities) |

`dmesg`, `logs` and `service logs` differ: kernel messages only, the whole system log, one service's log.

### System management

| Command | Purpose | Level | Status |
|---|---|---|---|
| `madire update` | Update Madire OS itself (separate from applications, which `pkg` updates) | SYSTEM | PLANNED (package manager, network stack) |
| `backup` | Back up files and settings | SYSTEM | PLANNED (filesystem, disk drivers) |
| `restore` ⚠ | Restore files and settings from a backup | SYSTEM | PLANNED (filesystem, disk drivers) |
| `settings` | Show or change system settings | SYSTEM | PLANNED (settings storage) |
| `timezone` | Show or set the time zone | SYSTEM | PLANNED (settings storage) |
| `keyboard` | Show or set the keyboard layout | SYSTEM | PLANNED (settings storage) |
| `locale` | Show or set the language and regional formats | SYSTEM | PLANNED (settings storage) |
| `schedule` | Run commands at set times | SYSTEM | PLANNED (processes, filesystem) |
| `open` | Open a file or address with its default app | SYSTEM | PLANNED (graphics, apps) |
| `defaults` | Show or choose which app opens each file type | SYSTEM | PLANNED (graphics, apps) |
| `bootconf` ⚠ | Show or change the boot configuration | SYSTEM | PLANNED (UEFI / boot phase) |

### Madire Security Platform

| Command | Purpose | Level | Status |
|---|---|---|---|
| `shield` | Protection against malicious software (e.g. `shield scan`) | SYSTEM | PLANNED (filesystem, processes) |
| `firewall` | Show or change which network connections are allowed | SYSTEM | PLANNED (network stack) |
| `vault` | Encryption of disks and secrets (Madire Vault) | SYSTEM | PLANNED (disk drivers, filesystem, users) |
| `audit` | Show the security event log (logins, permission changes) | SYSTEM | PLANNED (users, filesystem) |
| `security` | Show the overall security state (e.g. `security status`) | SYSTEM | PLANNED (users, filesystem) |

**Naming rule:** only Windows- or Microsoft-branded names are renamed (Madire's equivalents get their own names, like Shield and Vault); standard, vendor-neutral names stay (Secure Boot, TPM).

### Madire AI

| Command | Purpose | Level | Status |
|---|---|---|---|
| `ai` | Open the Madire AI assistant | SYSTEM | PLANNED (Madire AI) |
| `ask` | Ask the AI a question | SYSTEM | PLANNED (Madire AI) |
| `explain` | Explain a command or a result (e.g. `explain ps`) | SYSTEM | PLANNED (Madire AI) |
| `suggest` | Ask the AI for a recommended action | SYSTEM | PLANNED (Madire AI) |
| `automate` | Create an automated task with the AI's help | SYSTEM | PLANNED (Madire AI, `schedule`) |
| `optimize` | Analyse the system and suggest improvements | SYSTEM | PLANNED (Madire AI) |

### Applications (installed separately)

| Command | Purpose | Level | Status |
|---|---|---|---|
| `git` | Version control | APPLICATION | APPLICATION |
| `gcc` | C compiler | APPLICATION | APPLICATION |
| `clang` | C and C++ compiler | APPLICATION | APPLICATION |
| `make` | Build software from build instructions | APPLICATION | APPLICATION |
| `cmake` | Configure software builds | APPLICATION | APPLICATION |
| `python` | Run Python programs | APPLICATION | APPLICATION |
| `gdb` | Debug programs | APPLICATION | APPLICATION |
| `nmap` | Scan networks and discover hosts and services | APPLICATION | APPLICATION |
| `tcpdump` | Capture and inspect network traffic | APPLICATION | APPLICATION |
| `wireshark` | Analyse network traffic graphically | APPLICATION | APPLICATION |
| `tshark` | Analyse network traffic in the terminal | APPLICATION | APPLICATION |
| `netcat` | Open network connections and test services | APPLICATION | APPLICATION |
| `openssl` | Work with encryption and certificates | APPLICATION | APPLICATION |

Each needs at least processes, the filesystem and a C library; the network tools also need the network stack, and `wireshark` graphics. Security tools are only ever run against systems the user owns or may test.

### Console keys (not commands)

Keys the console handles itself. They aren't commands, so they have no level or `--help`; they are listed here so the standard describes everything the user can do at the prompt.

| Keys | What they do | Since |
|---|---|---|
| Left, Right, Home, End, Backspace, Delete | edit the input line (at most 71 characters) | 0.5 |
| Up / Down | recall earlier commands from the history (the last 16); Down past the newest gives an empty line | 0.9.1 |
| Shift+PgUp / Shift+PgDn | page back and forward through the scrollback (the last 1000 lines that left the screen), 24 lines at a time; a reverse-colour indicator on the bottom row shows the position; paging back stops at line 1 | 0.9.2 |
| Any other key, **while scrolled back** | returns to the live screen and is then handled as usual (typed, or Enter runs the line); key releases and modifier keys on their own (Shift, Ctrl, Alt, the locks) don't return | 0.9.2 |
| PgUp / PgDn without Shift, **when not scrolled back** | do nothing | 0.9.2 |
| Ctrl+C, **while a program runs in the foreground** | ends the program (and wakes it if it sleeps); logged only as `console: Ctrl+C: kill requested for process N`, never as a key; at the prompt it does nothing | 0.11 |
| Ctrl+C, **during `sleep` or `presskey`** | cancels the wait (`sleep: cancelled.`, `presskey: cancelled.`); logged only as a fixed line | 0.12 |

Only the grey PgUp/PgDn keys page: the numeric keypad's keys type `9` and `3`, because Num Lock is assumed on. `clear` keeps the screen's lines in the scrollback. The scrollback and the history are lost at restart.

## 4. One meaning per name

- Every name has one meaning and appears once in this standard (`hostname`, `logs` and `diagnose` each appear once).
- Generic verbs are subcommands, not commands: `pkg install`, `pkg remove`, …, `service start`, `service stop`, … .
- One name per action: `pkg remove` (no `uninstall`), `which` (no `where`).
- `time` measures how long a command takes; `date` shows the date and time.
- Overlapping pairs are kept only where their meanings differ:

| Pair | The difference |
|---|---|
| `less` / `more` | `less` scrolls both ways and searches; `more` is a minimal forward-only pager |
| `shutdown` / `poweroff` | `shutdown` is orderly (warning, services stopped, optional delay); `poweroff` powers off at once |
| `system` / `uname` | `system` is a readable summary of the whole system; `uname` is a short identification line for scripts |
| `mem` / `free` | `mem` is the full memory report; `free` is one compact usage table |
| `cpu` / `lscpu` | `cpu` is a summary; `lscpu` lists cores, caches and topology in detail |
| `netstat` / `ss` | `netstat` summarises connections and statistics; `ss` shows each socket in detail |
| `dmesg` / `logs` | `dmesg` shows kernel messages only; `logs` the whole system log |
| `sleep` / `presskey` | `sleep` waits for a given time; `presskey` waits for a key press |
| `disk label` / `disk info` | `disk label` names a volume (shows or changes the name); `disk info` only shows a volume's name and serial number |
| `disk info` / `volume` | `disk info` is about storage volumes; `volume` is the sound volume |
| `chmod` / `fileattr` | `chmod` sets who may read, write or run a file (permissions); `fileattr` sets a file's own attributes (read-only, hidden) |
| `open` / `defaults` | `open` opens one file with its default app; `defaults` chooses which app is the default |

## 5. Shell language

These are features of the shell, not commands:

- variables (`name=value`, `$name`); since 0.12 the shell holds variables (`set NAME=value`), but `$name` expansion is still PLANNED;
- `if` / `else`;
- loops;
- pipes (`|`), passing one command's output to the next;
- redirection (`>`), sending output to a file;
- exit codes (0 for success, anything else for failure), usable in `if`.

**Open decision for the 1.0 era:** Bash-style text pipelines, PowerShell-style object pipelines, or a mix (Bash-compatible scripting with optional structured output, such as `ps --json`). Both shells are listed in [references.md](references.md) as references only.

## 6. Rules

- **Design principles:** simple, predictable, safe, scriptable, documented, consistent.
- **Names (since v0.5):** Madire commands use Madire's own names or open Unix/POSIX names, never Microsoft/DOS-specific command names. Windows names appear only in the "Windows equivalents" table (section 8).
- **Help everywhere:** every official command eventually supports `command --help` and `man command`. Both document the purpose, syntax, options, examples, exit status and the permissions required. Since 0.9.1 every implemented command answers `<command> --help` and `help <command>` (its purpose and usage, so far); `man` needs the filesystem.
- **"Did you mean" (IMPLEMENTED, 0.12):** when a command name is not found, the shell suggests the closest commands by edit distance (for example `hlep` → `help`; at most 3 names, distance at most 2, aliases included), in lines of at most 79 columns. A suggestion is only text: nothing is ever run automatically.
- **Help sections (IMPLEMENTED, 0.12; manual sections PLANNED):** since 0.12 `help` groups the commands into the sections `shell`, `system` and `programs` (`help <section>`). Later, like the Linux manual's sections (1 user commands, 8 administration commands, 2 system calls), `man` separates user commands, administration commands and the system-call reference (needs the filesystem).
- **Destructive commands** (marked ⚠: for example `rm`, `format`, `partition`, `userdel`, `reboot`) require confirmation or show a clear warning first.
- **Madire AI** never changes the system without informing the user first.
- **Keeping this standard current:** whenever a milestone adds, changes or removes a command, this file is updated in the same milestone (status, level, purpose, foundation) and the MOCS version goes up by one step (v0.1 → v0.2 → …). New commands follow these rules: one meaning per name, a level (CORE, SYSTEM or APPLICATION), `--help`, and a confirmation or a clear warning before destructive actions. The same rule is in [AGENTS.md](../AGENTS.md).
- **Licences:** porting a command toolkit (for example BusyBox, GPL-2.0, or toybox, 0BSD) requires the owner's approval and a licence check first. Madire OS has no licence of its own yet; see [references.md](references.md).

## 7. Priority order

1. Kernel, memory, shell: **done** (0.1–0.9; shell improvements in 0.12).
2. Processes: kernel threads (0.9), user processes (0.10), programs and process management (0.11): **done**.
3. Filesystem (1.0).
4. Drivers.
5. Networking.
6. System utilities.
7. Package manager.
8. Applications.
9. Madire AI.

The small 0.9.1 (shell commands that need no new foundation, done) and 0.9.2 (console scrollback, key logging off by default) come between 0.9 and 0.10; see [roadmap.md](roadmap.md).

## 8. Windows equivalents

For people who know the Windows command prompt: the Madire names for common Windows commands. Most are PLANNED; the status of each Madire command is in section 3.

| Windows | Madire OS |
|---|---|
| `DIR` | `ls` |
| `COPY` | `cp` |
| `DEL` | `rm` |
| `TYPE` | `cat` |
| `TASKLIST` | `ps` |
| `TASKKILL` | `kill` (by number), `pkill` (by name) |
| `SC` | `service` (one service), `services` (the list) |
| `SCHTASKS` | `schedule` |
| `CHKDSK` | `fsck` |
| `DISKPART` | `partition` (and `lsblk` to list disks) |
| `ICACLS` | `chmod`, `chown`, `chgrp` (file permissions and owners) |
| `SET` | `set` (shell variables), `env` / `export` (the environment) |
| `DOSKEY` | `alias` (macros), `history` (recall) |
| `START` | `open` (a file or address), `run` (a program) |
| `SYSTEMINFO` | `system` |
| `PAUSE` | `presskey` |
| `COLOR` | `colors` |
| `TITLE` | `wintitle` |
| `ATTRIB` | `fileattr` |
| `LABEL` | `disk label` |
| `VOL` | `disk info` |
| `BCDEDIT` | `bootconf` |
| `ASSOC`, `FTYPE` | `defaults` |
