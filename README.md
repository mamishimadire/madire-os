# Madire OS

Madire OS is an operating system written from scratch: its own kernel and architecture, not a Linux distribution.

**Current version:** 1.0 (Milestone 1.0: the filesystem; tag `v1.0`)

![Madire OS v0.13 boot screen](docs/images/v0.13-boot.png)

1.0 gives Madire OS **files and folders**:

- **A filesystem:** a virtual filesystem (VFS) layer with a **RAM filesystem** behind it: up to 1024 files and folders, 2 MiB per file, and up to 32 MiB of file data, taken from free memory only as files grow.
- **Live mode:** booted from the ISO, every file lives in memory and nothing is saved. The console says so when it opens, and `version` and `df` show it; there is no disk driver yet.
- **File commands in the console:** `pwd`, `cd`, `ls` (`-l`), `mkdir`, `rmdir`, `touch`, `stat`, `cat`, `rm`, `cp`, `mv`, `df` and `du`, in the new help section `files`; `rm`, and `cp` or `mv` over an existing file, ask first (`-y` doesn't); a built-in command's output goes into a file with `>` or `>>`.
- **Files for programs:** 21 new Linux x86-64 system calls (`open`, `read`, `close`, `stat`, `lseek`, `mkdir`, `rename`, `getdents64`, the `*at` forms and more), with Linux's numbers, structures and error codes; 16 descriptors per program; large reads and writes return the full count, as on Linux.
- **Programs start as on Linux:** with `argc`, `argv`, the environment (the shell's exported variables) and the auxiliary vector on their stack, so `run <program> [args…]` passes arguments; a program starts in the shell's current folder.
- **One lock for the filesystem**, fair (first come, first served) and never blocking the console; a lock holder can't be switched away in the middle.
- **`help` fits one screen** again: the commands' names by section; `help <command>` and `help <section>` give the details.
- **Madire's own licence files** (`LICENSE`, `NOTICE`, `THIRD_PARTY_NOTICES`) at the root of every ISO.
- **Tests:** 260 new QEMU checks, 13 new unit-test suites, a stress part (the lock under load, the kill race), and a VirtualBox `files` group.

0.13 replaced the bootloader: the ISO now boots through **Limine** (v12.9.3, BSD-2-Clause) instead of GRUB:

- **Limine's boot menu:** the default entry and a **Diagnostics** directory with the 27 test boots; it waits 3 s, as GRUB did.
- **The same kernel:** still Multiboot 1; `madire.kernel` differs from v0.12's only in the three bytes of its version string.
- **A pinned, checked tool:** the build fetches Limine's unmodified binary package and checks its SHA-256 every time; GRUB is no longer used.
- **`THIRD_PARTY_NOTICES`:** Limine's licence and notices, which every ISO also carries.
- **Tests:** a Madire-written Multiboot header check replaces GRUB's `grub-file`; new checks prove where Limine places the boot data and that a module without a name is refused; the VirtualBox automation drives Limine's menu.

![Limine's boot menu (0.13)](docs/images/v0.13-boot-menu.png)

0.12 made the console shell easier to use, and the kernel checks itself:

- **"Did you mean":** a mistyped command suggests up to three close names (`hlep` → `Did you mean: help, sleep?`); nothing is run for you.
- **Help in sections:** `help shell`, `help system` and `help programs` list one group each; `help` still lists everything.
- **`prompt`:** change the prompt (up to 8 characters) or go back with `prompt --reset`.
- **`alias` and `unalias`:** short names for commands (up to 16).
- **`set`, `unset`, `export` and `env`:** shell variables, kept in memory until restart.
- **`sleep`, `presskey` and `time`:** wait for a number of seconds, wait for a key, or time a command; Ctrl+C cancels a wait.
- **`kill -y <pid>`:** ends a process without asking (plain `kill` still asks first).
- **Always-on kernel self-checks:** at every thread switch, every address-space (CR3) load and every return to user mode, the kernel checks that the thread, the address space and the process belong together; a broken rule stops the kernel with a panic instead of carrying on.

Since 0.11 you **run programs from the console**: `run hello` loads an ELF program that the bootloader delivered as a boot module and shows its output on the screen; `run -b spin` runs one in the background; `ps` lists threads and processes; `kill <pid>` ends one (it asks first; `kill -y <pid>` doesn't); **Ctrl+C** ends the program in the foreground:

![run hello: the program's output on the screen (0.13)](docs/images/v0.13-run-hello.png)

Programs run in **user mode** (since 0.10), each in its own address space, and talk to the kernel only through system calls; a program that makes a mistake is ended alone, and the kernel carries on. The Diagnostics menu runs the built-in test programs:

![User fault test: PASS (0.10)](docs/images/userfault-v0.10.png)

After the boot checks, press any key to open the console. Output that scrolls off the top isn't lost: **Shift+PgUp** and **Shift+PgDn** page back through the last 1000 lines (0.9.2), with an indicator on the bottom row; any other key returns:

![Madire OS v0.9.2 console: scrolled back](docs/images/scrollback-page1-v0.9.2.png)

`help` lists the commands and ends with the sections; `help <command>` or `<command> --help` explains one:

![Madire OS v0.13 console: help](docs/images/v0.13-help.png)

`uname -a`, `hostname`, `date`, `dmesg`, Up-arrow recall and `history` (0.9.1):

![Madire OS v0.9.1 console: the new commands](docs/images/console-commands-v0.9.1.png)

`ps` lists the threads and processes (after boot only thread 0, the console itself, is left, and no process), and `mem` shows the higher-half kernel, NX and the kernel stack's high-water mark:

![Madire OS v0.13 console: ps and version](docs/images/v0.13-ps-version.png)

![Madire OS v0.13 console: mem](docs/images/v0.13-mem.png)

It boots through Limine (GRUB until 0.12) into a 64-bit (x86-64) kernel written in C and NASM. So far the kernel:

- checks that the CPU can run it (CPUID, long mode, PAE) and switches from the bootloader's 32-bit protected mode into 64-bit long mode; a CPU without long mode gets a clear message and halts;
- runs in the **higher half** (since 0.8): it is loaded at 1 MiB but runs at `0xFFFFFFFF80000000` and above, and once its own page tables are loaded **nothing in the lower half is mapped**, so any stray low address faults;
- reaches the low 1 MiB (VGA) and the bootloader's data through a small **physical view**, which never includes free RAM, page 0 or the kernel image;
- installs its own segment table (GDT, with a task-state segment) and interrupt table (IDT);
- catches CPU exceptions and shows them on a readable crash screen with the 64-bit registers;
- reads and validates the firmware's memory map, and protects the kernel and boot data;
- runs a physical page allocator, which it tests on real RAM at every boot;
- runs with 4-level paging: the page tables are checked before they are loaded, page 0 is left unmapped so NULL pointers fault, kernel code is read-only, and everything except kernel code is **no-execute (NX)**;
- has unmapped **guard pages** below its stacks: a kernel stack overflow becomes a double fault, handled on its own stack (IST1), and is reported instead of resetting the machine;
- measures how deep its stack has ever been used (a **high-water mark**, shown by `mem`: 1832 of 16384 bytes at "System ready." in 1.0);
- runs **kernel threads** (since 0.9), each with its own 16 KiB stack above an unmapped guard, so a thread's stack overflow is caught and the crash screen names the thread;
- **schedules them preemptively**: round robin with a 50 ms time slice, switching at the end of a timer interrupt (and, since 0.11, as soon as a thread leaves a section where preemption was off, so every turn stays within about a slice), plus sleep, join and exit codes; a thread that never gives up the CPU can't starve the others;
- keeps itself safe under preemption without locks: the heap, the page allocator, paging and screen/serial output each run with preemption briefly disabled, and only the console thread writes to the screen;
- tests its scheduler at every boot (two threads taking turns, preemption, sleep, everything freed afterwards);
- runs **user processes** (since 0.10): programs in ring 3, each in **its own address space** (the kernel's half shared, and frozen at boot), preempted like any thread, killed safely at their next return to user mode, and torn down exactly;
- offers **system calls** through `SYSCALL`, with Linux x86-64 numbers and behaviour: six since 0.10 (`write`, `sched_yield`, `nanosleep`, `getpid`, `exit`, `exit_group`) and, since 1.0, 21 file and folder calls (27 in all; every other number answers `-ENOSYS`), and checks every pointer a program passes before touching it;
- **contains faults:** a program that reads NULL, runs an invalid or privileged instruction, touches kernel memory, overflows its stack, writes its code or executes its data (W^X) is ended alone, with one log line naming the exception and the exact instruction;
- uses **SMEP and SMAP** when the CPU has them, so the kernel itself can't run or touch program memory by mistake (and the crash screen names such a fault); x87/MMX/SSE are off for programs (CR0.EM);
- tests user mode at every boot (three programs as processes 1–3: output, an exit status, a contained NULL read);
- **loads ELF programs** (since 0.11) delivered by the bootloader as boot modules (7 on the default entry), after strict checks at boot and again at load (static x86-64 only, no writable code, at most 4 segments and 1 MiB), copying them into fresh pages through one loading path;
- **runs and manages them from the console** (since 0.11): `run` and `run -b`, `ps` with process IDs, `kill` (which asks first) and Ctrl+C, which also wakes a sleeping program at once; a foreground program's output appears on the screen, drawn only by the console thread from a queue, and a program that floods it is slowed down rather than the console;
- keeps **files and folders** (since 1.0) in a RAM filesystem behind a VFS layer, in **Live mode** (nothing is saved), with descriptors for programs, one fair lock that never blocks the console, and a self-test at every boot;
- starts programs with the **System V layout** (since 1.0): `argc`, `argv`, `envp` and the auxiliary vector;
- has a kernel heap (`kmalloc`, `kcalloc`, `kfree`) that catches double frees, invalid frees and corrupted blocks;
- explains page faults on the crash screen (what kind of access, and the faulting address);
- checks that boot modules loaded by the bootloader arrive intact, and shows the result on the screen as well as the serial log;
- handles hardware interrupts safely: only the timer (IRQ0) and keyboard (IRQ1) are enabled, each only after its handler is ready;
- keeps time with the PIT at 100 Hz (uptime is shown on screen);
- reads the PS/2 keyboard: key presses and releases, Shift/Ctrl/Alt/Caps Lock and extended keys, queued for the kernel;
- provides a text console with a scrolling screen, a blinking cursor and an editable input line (insert, Backspace, Delete, Left/Right/Home/End), and since 0.9.2 a **scrollback** of the last 1000 lines (Shift+PgUp / Shift+PgDn);
- **doesn't log what you type** (since 0.9.2): key events reach the logs only with the boot option `keylog` (for tests) or during the keyboard diagnostics;
- runs a small command shell: `help`, `clear`, `version`, `uptime`, `mem`, `cpu`, `echo`, since 0.9 `ps`, since 0.9.1 `history` (with Up/Down recall), `uname`, `hostname`, `date`, `dmesg` and `reboot` (which asks first), since 0.11 `run` and `kill`, since 0.12 `prompt`, `alias`, `unalias`, `set`, `unset`, `export`, `env`, `sleep`, `presskey`, `time` and `kill -y`, and since 1.0 `pwd`, `cd`, `ls`, `mkdir`, `rmdir`, `touch`, `stat`, `cat`, `rm`, `cp`, `mv`, `df` and `du`, with `>` and `>>` (39 commands); `help` fits one screen, `help <command>` and `<command> --help` explain any command, `help <section>` lists one group, and a mistyped name gets "Did you mean" suggestions;
- **checks itself at every switch** (since 0.12): the scheduler, address-space (CR3) and return-to-user-mode self-checks are always on, and each one's panic is proven by its own test boot;
- keeps a 32 KiB kernel log in memory (`dmesg`), reads the date and time from the real-time clock, and restarts the machine through the keyboard controller, with a triple-fault fallback;
- draws the Madire M logo, built from geometry, in VGA text mode.

The project is built one milestone at a time, and each milestone has to boot in VirtualBox before the next one starts. See [docs/roadmap.md](docs/roadmap.md) and [docs/PROGRESS.md](docs/PROGRESS.md).

**Tested at v0.13:** 1233 QEMU checks and 55 unit-test suites pass in the official regression on GitHub Actions, together with the reproducible-build check; the automated VirtualBox acceptance test (`MadireOS-Test`) passes all its groups on the same ISO, booting all 27 Diagnostics entries through Limine's menu.

## Roadmap

A summary of [docs/roadmap.md](docs/roadmap.md); each step needs its own plan and the owner's approval.

- **0.1–0.13: done.** Boot, CPU and memory foundations, interrupts, the console and shell, paging and the heap, 64-bit long mode, the higher-half kernel, kernel threads and preemption, user processes, ELF programs and process management, the shell improvements of 0.12, and in 0.13 the bootloader, GRUB → Limine ([plan](docs/plans/0.13.md), [evaluation](docs/evaluations/limine.md)).
- **1.0: the filesystem, done** ([plan](docs/plans/1.0.md)): a VFS layer with a RAM filesystem, the Live mode, the file system calls and commands, System V program start-up.
- **Then, unscheduled and in this order:** graphics (a design guide first, then a framebuffer, fonts and the mouse), storage, an installer (virtual disks only at first), networking, the Linux-compatible foundation (system calls and a C library), TLS, audio and video, the Madire Browser, Linux apps, and in the long term Windows apps through Wine on top of the Linux layer.
- **Alongside:** the modernisation track: UEFI, APIC, multi-core, USB, all RAM, Wi-Fi and Bluetooth; ARM64 later.

## v0.13 in pictures

Screenshots from the 0.13 VirtualBox acceptance run (`MadireOS-Test`, the v0.13 ISO of the final regression run).

Limine's menu with the **Diagnostics** directory open (the automation selects each entry by its position):

![Limine's menu, Diagnostics open](docs/images/v0.13-boot-menu-diagnostics.png)

The boot screen of v0.13, with the self-tests passed:

![v0.13 boot screen](docs/images/v0.13-boot.png)

`help` ends with the list of sections; `help system` and `help programs` show one group each:

![help system and help programs](docs/images/v0.13-help-sections.png)

A mistyped command or help topic gets "Did you mean" suggestions:

![Did you mean suggestions](docs/images/v0.13-didyoumean.png)

`prompt` changes the prompt and refuses text that is too long, starts with `-` or has spaces:

![prompt](docs/images/v0.13-prompt.png)

`alias` makes short names for commands, with clear refusals for bad names:

![alias and unalias](docs/images/v0.13-alias.png)

`set` keeps shell variables in memory, with its limits checked:

![set](docs/images/v0.13-set.png)

`export` marks variables, and `env` lists the exported ones:

![export, env and unset](docs/images/v0.13-env.png)

`time sleep 1` takes 100 ticks (1.00 s); `presskey` waits for a key, and Ctrl+C cancels it:

![sleep, presskey and time](docs/images/v0.13-wait.png)

`kill` asks first; `kill -y` ends a process without asking, and `time kill -y` times it:

![kill and kill -y](docs/images/v0.13-kill.png)

**Ctrl+C** ends a program that never stops by itself (`run spin`):

![Ctrl+C on spin](docs/images/v0.13-ctrlc-spin.png)

## Development environment

| Role | Tool |
|---|---|
| Editor / host | Windows 11, Visual Studio Code |
| Build environment | Ubuntu Linux: a GitHub Codespace (current; created from `.devcontainer/`, proven on a fresh Codespace in 0.8), or WSL2 / CI |
| Compiler | the host GCC in 64-bit freestanding mode (`gcc -m64 -ffreestanding -mno-red-zone -mgeneral-regs-only -mcmodel=kernel`) |
| Assembler | NASM |
| Bootloader / ISO | Limine 12.9.3 (since 0.13; the pinned binary package, SHA-256 checked) + xorriso (`limine bios-install`); GRUB 2 until 0.12 |
| Automated tests | QEMU (`qemu-system-x86_64`, and `qemu-system-i386` for the "no 64-bit CPU" test), headless |
| Official regression | GitHub Actions (since 0.12): started by hand, sharded into parallel jobs, QEMU 8.2.2 and gcc 13 (`scripts/actions-regress.ps1`); a GitHub Codespace is the fallback |
| Target machine | Oracle VirtualBox 7.2, VM `MadireOS-Test` (BIOS firmware, OS type Other 64-bit) |

Linux is used only to run the build tools. It is not part of Madire OS.

## Build

On Windows, compile remotely in the Codespace. This copies your local files up, builds, runs the tests and fetches the ISO back:

```powershell
.\scripts\build.ps1 -InstallDeps   # first time on a new Codespace
.\scripts\build.ps1                # afterwards
```

On Linux (Codespace terminal, WSL, CI):

```sh
bash scripts/install-deps.sh   # once (a Codespace from .devcontainer/ runs it for you)
make            # kernel: build/madire.elf (with symbols) and build/madire.kernel (bootable)
make iso        # bootable ISO: build/madire-os.iso (Limine; scripts/install-deps.sh
                # fetches the pinned Limine package)
make unit       # host unit tests, 68 suites (memory, 4-level page tables, heap,
                # keyboard decoder and queue, PIT maths, console text grid, line
                # editor, parser, shell, CPUID decoding, kprintf, NX and page-fault
                # decoding, boot modules, stack high-water mark, scheduler, thread
                # stack slots, ps, command history, RTC decoding, kernel log, the
                # 0.9.1 commands, scrollback; 0.10: GDT, SMEP/SMAP, the freeze,
                # user pages, user copies, process table, program images, ring-3
                # entry, fault opcodes, system calls, SMEP/SMAP crash causes,
                # turn order; 0.11: ELF checks, program modules, load plans,
                # turn owners, policy (b), the early wake, run/ps/kill, the
                # output queue; 0.12: the 0.12 shell commands, shell sessions,
                # bounded diagnostic waits, the scheduler self-checks; 1.0: paths,
                # the VFS, the RAM filesystem, file pages, the VFS lock, descriptors,
                # copy_to_user, the file and folder system calls, the start-up
                # block, the file commands, help on one screen)
make repro      # two clean builds in fresh directories: madire.elf and
                # madire.kernel must be byte-identical (make test runs it too)
make test       # unit tests + repro + 1493 QEMU checks (150 from 0.3 + 30 from 0.4 +
                # 32 from 0.5 + 49 from 0.6 + 96 from 0.7 + 32 from 0.8 + 139 from
                # 0.9 + 32 from 0.9.1 + 25 from 0.9.2 + 253 from 0.10 + 233 from
                # 0.11 + 151 from 0.12 + 11 from 0.13 + 260 new; the agreed counts
                # are in tests/expected-checks.txt):
                # boot, long mode, CPUs without long mode, logo, memory at 5 RAM
                # sizes, crash tests, timer rate, scripted keyboard input, a scripted
                # console session, paging and heap self-tests, the higher-half kernel
                # and empty lower half, page faults, NX, stack overflow and stack
                # peak, heap misuse and exhaustion, boot-module integrity, screen
                # line widths, threads, preemption, sleep and join, the heap and page
                # allocator under preemption, thread stack overflow, ps, the 0.9.1
                # commands, reboot through the keyboard controller and by triple fault,
                # key logging off by default, scrollback read back from the VGA screen,
                # user mode: segments, SMEP/SMAP, address spaces, programs in ring 3,
                # kill, contained faults with their instructions, system calls, bad
                # pointers, 1000 processes, the boot menu's Diagnostics entries; ELF programs
                # from boot modules and malformed files, run/kill/ps and Ctrl+C in
                # console sessions, screen output and a flooding program; the 0.12
                # shell commands in console sessions, the self-checks' panics; the
                # Multiboot header (scripts/check-multiboot.py), Limine's files and
                # notices in the ISO, where Limine places the boot data; the
                # filesystem and its limits, the VFS lock, descriptors, the file and
                # folder system calls, System V start-up, the file commands in
                # console sessions, Madire's licence files in the ISO.
                # The official run is on GitHub Actions, sharded, about 20 minutes:
                # scripts/actions-regress.ps1 -Dispatch (see docs/development.md)
make run        # boot in a QEMU window
make clean
```

## Test in VirtualBox

```powershell
.\scripts\run.ps1                       # attach ISO, boot MadireOS-Test in a window
.\scripts\run.ps1 -Headless -Screenshot # boot without a window, save build\boot-screen.png
.\scripts\vbox-acceptance.ps1           # the automated acceptance test (since 0.9): checks
                                        # the VM settings (never changes them), boots each
                                        # test, keeps screenshots and serial logs; since
                                        # 0.12 a scripted shell session too (-Only boot,shell);
                                        # since 0.13 it drives Limine's menu;
                                        # since 1.0 a files session (-Only files)
```

In the console, try `run hello`, `run -b spin`, `ps`, then `kill <pid>` (it asks first) or `kill -y <pid>`, and `run lines 65636` followed by **Ctrl+C**. From 0.12: `help shell`, a typo such as `hlep`, `alias ll echo hi`, `set A=1`, `time sleep 1`. From 1.0: `help files`, `mkdir docs`, `cd docs`, `echo hello > a.txt`, `cat a.txt`, `ls -l`, `cp a.txt b.txt`, `df`.

When the boot screen says `System ready. Press any key to open the console.`, press a key. That key only opens the console; it isn't typed. Type `help` for the list of commands.

Limine's menu has a **Diagnostics** directory (press **↓**, then **Enter** to open it):

- **Crash tests** that deliberately trigger CPU exceptions, including a **kernel stack overflow**.
- **Page-fault tests:** read from NULL, read from an unmapped address, write to read-only kernel code, execute from a no-execute data page (NX), read the kernel's old low address `0x100000` (0.8: it must fault, because the lower half is empty).
- **Memory tests:** allocate every page, double free, free a kernel page.
- **Timer test:** measures the tick rate against the real-time clock.
- **Keyboard test:** shows the code of every key pressed.
- **Thread tests (0.9):** three threads that never yield share the CPU by preemption; a kernel thread's stack overflow is caught by its guard and the crash screen names the thread.
- **User-mode tests (0.10):** a program in ring 3 preempted by the timer; processes killed; programs that fault, try privileged instructions or touch kernel memory, each ended alone; system calls; `write`, `nanosleep` and bad pointers; preemption of user processes; 1000 processes.
- **SMEP and SMAP crash tests (0.10):** the kernel deliberately executes or reads a program's page; on a CPU with SMEP/SMAP this must end on the crash screen naming the cause, otherwise it reports NOT APPLICABLE (the VirtualBox VM doesn't offer them).

See [docs/testing.md](docs/testing.md) for the expected screens and troubleshooting.

## Limitations (v1.0)

- 64-bit x86 (x86-64) only; the 32-bit build was dropped in 0.7. The VM must use **BIOS** firmware (VGA text mode doesn't exist under EFI; the ISO is BIOS-only, although Limine could also boot UEFI later).
- **Only RAM below 4 GiB is used.** On `MadireOS-Test` (4096 MB), 512 MB sits above 4 GiB and is shown separately as `RAM above 4 GiB`.
- The whole low 1 MiB is reserved on purpose, and ACPI-reclaimable memory isn't reclaimed yet.
- **Paging:** 4 KiB pages; one kernel half, shared by every process and frozen at boot. There is no direct map of physical memory: the physical view covers only the low 1 MiB and the boot data, and other frames are reached one page at a time through the physical-access window. A page fault in the kernel is fatal (nothing is loaded on demand); one in a program ends that program.
- **Stacks:** the high-water mark covers the kernel stack and each thread's stack (not the double-fault stack). It can read a few bytes too low, never too high, and it always counts the top 736 bytes (the boot frames and a safety margin, which are never painted) as used.
- **Kernel heap:** one heap, at most 16 MiB, never shrinks. Safe between threads, but not interrupt-safe: `kmalloc`/`kcalloc`/`kfree` refuse to run with interrupts disabled (so never from an IRQ or exception handler). `kfree` checks the whole heap every time, which gets slower as the heap fills.
- **Boot modules:** the screen has one message line, so with several modules only the last result stays visible; the serial log has them all.
- **Keyboard:** PS/2 only (USB keyboards need firmware legacy emulation), US layout only, no LEDs (Caps Lock state is tracked in software only), keypad assumes Num Lock on.
- **Console and shell:**
  - 39 built-in commands (the full plan is in [docs/commands.md](docs/commands.md)).
  - Commands are lowercase and case-sensitive; at most 8 words per line and 71 characters of input.
  - `history` keeps only the last 16 lines and the scrollback the last 1000 lines; both are lost at restart. No tab completion, pipes, quoting or scripting.
  - The prompt, aliases (at most 16, one level) and variables (at most 32, values up to 63 characters) live in memory only and are lost at restart. Variables are not expanded (`$NAME` doesn't exist yet), and no environment reaches a program. `time` times built-in commands only, not aliases.
  - Shift+PgUp/PgDn need the grey PgUp/PgDn keys: the numeric keypad's ones type `9`/`3` (Num Lock is assumed on).
  - `hostname` is fixed (`madire`). `date` shows the real-time clock as it is, with no time zone: VirtualBox keeps it on the host's local time, QEMU on UTC. It assumes years 2000–2099 and can't set the date.
  - `dmesg` shows only what the kernel logged through `kprintf`/`klogf`: the boot screen's status lines and the console's own output aren't in it, and a boot-screen line can appear joined to the next one there.
  - Key events are logged only with the boot option `keylog` or during the keyboard diagnostics (for tests); never use `keylog` where secrets are typed. There is no password prompt yet; the first one must come with a secret-input mode (see [AGENTS.md](AGENTS.md)).
  - `reboot` uses the keyboard controller (or a triple fault); there is no `shutdown` or `poweroff` yet.
  - Ctrl+key and Alt+key do nothing, except Ctrl+C while a program runs in the foreground (0.11) or during `sleep` and `presskey` (0.12).
  - Long output lines wrap at column 80, even mid-word (for example `cpu`'s "Features:" line on some CPUs).
- **Interrupts:** only IRQ0 and IRQ1 are enabled, through the legacy 8259 PIC (no APIC). The timer runs at a fixed 100 Hz (10 ms resolution).
- **User processes:** programs come only as boot modules (files can't be run yet): at most 8 are kept, and 14 or more boot modules of any kind stop the boot. Only static x86-64 ELF executables within the loader's rules (no dynamic linking, at most 4 segments, 1 MiB). One thread per process, at most 63 processes, a 32 KiB stack each; no signals, `fork` or `exec`. Programs can't use x87/MMX/SSE (no FPU state is saved). Only the foreground program's output reaches the screen; background programs (`run -b`) write to the serial log and `dmesg` only. 27 system calls (Linux numbers); the others a C library needs (`mmap`, `brk`, `readv`/`writev`, `ioctl`, `fcntl`, `pipe`, …) come with the Linux-compatibility milestone. The start-up block (arguments and environment) holds at most 4096 bytes, without `AT_RANDOM`. Programs can't read the keyboard (fd 0 reads return 0). Without NX, program data is executable. SMEP and SMAP are used only when the CPU offers them (the VirtualBox VM doesn't).
- **Threads and scheduling:** at most 64 threads with 16 KiB stacks; round robin with a fixed 50 ms slice, no priorities. Only one CPU is used.
- **Files (1.0):** Live mode only: files live in memory and are **lost at restart**; there is no disk driver. At most 1024 files and folders, 2 MiB per file and 32 MiB of file data (less with under 64 MiB of RAM). No permissions, owners or links; times have no time zone. `ls` lists in creation order; `rm` removes files only (no `rm -r`); `cp` copies files only, and `cp`/`mv` don't copy or move into a folder; `mkdir` has no `-p`. `>` and `>>` work only as separate words (`echo hi > a.txt`, not `echo hi>a.txt`) and only for built-in commands, not for `run`. Large `cat` and `cp` take one 4 KiB piece per console pass (a 2 MiB copy takes a few seconds).
- **Test programs in the kernel:** the kernel image still carries the built-in test programs (in 1.0 eight more, about 80 KiB); they move to boot modules after 1.0.
- The logo is blocky: text mode gives 80×50 pixels, each 9×8 screen pixels in size.
- Building needs a Linux environment; there is no native Windows toolchain yet. The kernel build is reproducible (byte-identical), but the ISO isn't: it still differs between builds, although since 0.13 its build gives `xorriso` a fixed `SOURCE_DATE_EPOCH` (the cause is on the `xorriso` side; a watch item).

## How Madire OS is built

Madire OS is designed and directed by Mamishi Tonny Madire. The code is written with AI coding assistants, mainly Claude Code (Anthropic; OpenAI's Codex for part of 0.11), working under a written, approved plan for each milestone, in small stages that each stop for review, and checked by automated tests: host unit tests, QEMU tests on GitHub Actions, and a scripted VirtualBox acceptance run. Every decision, review and approval is the author's. Since the licence decision of 12 October 2026, every commit is signed off under the Developer Certificate of Origin (see [CONTRIBUTING.md](CONTRIBUTING.md)). The working rules for anyone, person or coding agent, are in [AGENTS.md](AGENTS.md).

## Documentation

- [docs/architecture.md](docs/architecture.md): system design, current and planned
- [docs/development.md](docs/development.md): layout, toolchain, rules for kernel code
- [docs/testing.md](docs/testing.md): automated and VirtualBox test procedures
- [docs/roadmap.md](docs/roadmap.md): milestones
- [docs/commands.md](docs/commands.md): the Madire OS Command Standard: every command, implemented and planned
- [docs/PROGRESS.md](docs/PROGRESS.md): history, tests per milestone, decisions, future direction
- [docs/references.md](docs/references.md): external projects used as references only
- [AGENTS.md](AGENTS.md): the working rules for anyone, person or coding agent, changing the repository
- [THIRD_PARTY_NOTICES](THIRD_PARTY_NOTICES): the third-party software Madire OS ships (since 0.13: Limine); since 1.0 every ISO carries it at its root, with Madire's `LICENSE` and `NOTICE`
- [docs/plans/](docs/plans/): every approved milestone plan

## Licence

Madire OS is licensed under the [Apache License 2.0](LICENSE) (see also [NOTICE](NOTICE)). The name "Madire OS" and the M logo are not covered by that licence: see [TRADEMARKS.md](TRADEMARKS.md). Contributions are accepted under the Developer Certificate of Origin, see [CONTRIBUTING.md](CONTRIBUTING.md); report vulnerabilities as described in [SECURITY.md](SECURITY.md).
