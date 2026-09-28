# Madire OS

Madire OS is an operating system written from scratch: its own kernel and architecture, not a Linux distribution.

**Current version:** 0.4 (Milestone 0.4: Hardware interrupts, timer and keyboard)

![Madire OS v0.4 boot screen](docs/images/boot-screen-v0.4.png)

It boots through GRUB into a 32-bit x86 kernel written in C and NASM. So far the kernel:

- installs its own segment table (GDT) and interrupt table (IDT);
- catches CPU exceptions and shows them on a readable crash screen;
- reads and validates the firmware's memory map, and protects the kernel and boot data;
- runs a physical page allocator, which it tests on real RAM at every boot;
- handles hardware interrupts safely: only the timer (IRQ0) and keyboard (IRQ1) are enabled, each only after its handler is ready;
- keeps time with the PIT at 100 Hz (uptime is shown on screen);
- reads the PS/2 keyboard: key presses and releases, Shift/Ctrl/Alt/Caps Lock and extended keys, queued for the kernel (the last key is shown on screen);
- draws the Madire M logo, built from geometry, in VGA text mode.

The project is built one milestone at a time, and each milestone has to boot in VirtualBox before the next one starts. See [docs/roadmap.md](docs/roadmap.md).

## Development environment

| Role | Tool |
|---|---|
| Editor / host | Windows 11, Visual Studio Code |
| Build environment | Ubuntu Linux: a GitHub Codespace (current), or WSL2 / CI |
| Compiler | GCC in 32-bit freestanding mode (`gcc -m32`), or `i686-elf-gcc` if installed |
| Assembler | NASM |
| Bootloader / ISO | GRUB 2 (`grub-mkrescue`) + xorriso |
| Automated tests | QEMU (`qemu-system-i386`), headless |
| Target machine | Oracle VirtualBox 7.2, VM `MadireOS-Test` (BIOS firmware) |

Linux is used only to run the build tools. It is not part of Madire OS.

## Build

On Windows, compile remotely in the Codespace. This copies your local files up, builds, runs the tests and fetches the ISO back:

```powershell
.\scripts\build.ps1 -InstallDeps   # first time on a new Codespace
.\scripts\build.ps1                # afterwards
```

On Linux (Codespace terminal, WSL, CI):

```sh
bash scripts/install-deps.sh   # once
make            # kernel ELF: build/madire.kernel
make iso        # bootable ISO: build/madire-os.iso
make unit       # host unit tests (memory, scancode decoder, key queue, PIT maths)
make test       # unit tests + 180 QEMU checks (150 regression from 0.3 + 30 new):
                # boot, logo, CPU, memory at 5 RAM sizes, crash tests, timer rate,
                # scripted keyboard input, queue overflow, unexpected IRQs
make run        # boot in a QEMU window
make clean
```

## Test in VirtualBox

```powershell
.\scripts\run.ps1                       # attach ISO, boot MadireOS-Test in a window
.\scripts\run.ps1 -Headless -Screenshot # boot without a window, save build\boot-screen.png
```

The GRUB menu has a **Diagnostics** submenu:

- **Crash tests** that deliberately trigger CPU exceptions.
- **Memory tests:** allocate every page, double free, free a kernel page.
- **Timer test:** measures the tick rate against the real-time clock.
- **Keyboard test:** shows the code of every key pressed.

See [docs/testing.md](docs/testing.md) for the expected screens and troubleshooting.

## Limitations (v0.4)

- 32-bit x86 only; the VM must use **BIOS** firmware (VGA text mode doesn't exist under EFI).
- **Only RAM below 4 GiB is used** (no PAE). On `MadireOS-Test` (4096 MB), 512 MB sits above 4 GiB and is shown separately as `RAM above 4 GiB`.
- The whole low 1 MiB is reserved on purpose, and ACPI-reclaimable memory isn't reclaimed yet.
- Physical pages only: no paging, virtual memory or heap (`kmalloc`) yet.
- **Keyboard:** PS/2 only (USB keyboards need firmware legacy emulation), US layout only, no LEDs (Caps Lock state is tracked in software only), keypad assumes Num Lock on. Keys are shown as "Last key" only: there's no console, line editing or shell yet.
- **Interrupts:** only IRQ0 and IRQ1 are enabled, through the legacy 8259 PIC (no APIC). The timer runs at a fixed 100 Hz (10 ms resolution).
- No filesystem, processes or scheduler.
- The logo is blocky: text mode gives 80×50 pixels, each 9×8 screen pixels in size.
- The screen does not scroll; output wraps back to the top.
- Building needs a Linux environment; there is no native Windows toolchain yet.

## Documentation

- [docs/architecture.md](docs/architecture.md): system design, current and planned
- [docs/development.md](docs/development.md): layout, toolchain, rules for kernel code
- [docs/testing.md](docs/testing.md): automated and VirtualBox test procedures
- [docs/roadmap.md](docs/roadmap.md): milestones
