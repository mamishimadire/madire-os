# Testing

## 1. Automated tests (`make test`)

These run in the Linux build environment, and `scripts/build.ps1` runs them on every build. The script is [`tests/qemu-tests.sh`](../tests/qemu-tests.sh). It boots the kernel headless in QEMU and checks the serial output, since the kernel copies all its screen text to COM1. Each run writes a log to `build/serial-*.log`.

| Group | How it boots | What it checks |
|---|---|---|
| Multiboot header | (not booted) | `grub-file --is-x86-multiboot` accepts the kernel |
| Normal boot | the real ISO, through GRUB | logo lit-pixel count = **293** (from `scripts/logo-preview.py`); title and version; `GDT`, `IDT`, `Interrupt controller`, `Exception handling`, `Display` all `OK`; `System ready.`; no `FAILED` or `PANIC` |
| Crash test `divzero` | `-kernel … -append "test=divzero"` | crash screen; `CPU exception 0: Divide Error`; error code `0x00000000`; register dump with `CS=0x0008`; no `System ready` |
| Crash test `ud` | `-append "test=ud"` | `CPU exception 6: Invalid Opcode`; error code `0x00000000` |
| Crash test `gpf` | `-append "test=gpf"` | `CPU exception 13: General Protection Fault`; error code **`0x00000028`** (the bad selector, which proves the error-code path works) |

Every QEMU run uses `-no-reboot`. If the CPU triple-faults (an exception that can't be handled, which resets the CPU), QEMU exits, the expected text never appears, and the test fails. A crash can't pass by accident.

**Logo reference:** `python scripts/logo-preview.py preview.png` runs the same stroke data and whole-number maths as `kernel/logo.c`, independently. It prints the expected pixel grid and count, and can write a PNG preview. If you change the logo, update both files and `LOGO_PIXELS` in the test script.

## 2. VirtualBox tests (manual acceptance)

### One-time VM configuration: `MadireOS-Test`

| Setting | Value |
|---|---|
| Type | Other / Unknown |
| Firmware | **BIOS** (not EFI) |
| RAM | 4096 MB |
| CPUs | 2 |
| Video memory | 128 MB |
| Network | NAT |
| Storage | SATA port 0: 30 GB disk (unused for now); SATA port 1: optical drive |

Commands that were run (`VBoxManage` is in `C:\PFiles\Oracle\VirtualBox\`):

```
VBoxManage modifyvm MadireOS-Test --firmware bios --ostype Other
```

### Procedure

```powershell
.\scripts\build.ps1     # build + automated tests, produces build\madire-os.iso
.\scripts\run.ps1       # powers off / discards a saved state, attaches the ISO,
                        # sets boot order DVD -> disk, starts the VM
```

To do it by hand instead: VM **Settings > Storage**, select the optical drive, choose `build\madire-os.iso`, then **Start**.

### Test A: normal boot

The GRUB menu shows **Madire OS v0.2** and **Diagnostics**. After 3 seconds it boots the first entry, and the screen should look like this:

![Madire OS v0.2 boot screen](images/boot-screen-v0.2.png)

- The M logo is drawn in the blue-to-cyan gradient. If it shows purple, brown or magenta, the palette programming didn't take effect.
- Six status lines: five `OK` and `Memory initialization: PENDING (milestone 0.3)`.
- `System ready.` in green.

### Test B: crash screen

In the GRUB menu, press **↓** to highlight **Diagnostics**, press **Enter**, then choose **Crash test: divide by zero**. The screen should look like this:

![Madire OS v0.2 crash screen](images/crash-screen-v0.2.png)

- White on red, headed `*** MADIRE OS - KERNEL PANIC ***`.
- `CPU exception 0: Divide Error (#DE)`, `EAX=0x0000000a`, `ECX=0x00000000` (the values the test loads), `CS=0x0008`, `DS=0x0010`.
- The machine halts.

The other two Diagnostics entries should show `Invalid Opcode (#UD)` and `General Protection Fault (#GP)` with error code `0x00000028`.

Close the VM with **Machine > Close > Power off**. If you choose "Save the machine state" instead, `run.ps1` discards that saved state automatically next time.

### Troubleshooting by layer

| Symptom | Layer | Check |
|---|---|---|
| "No bootable medium found" | ISO / VM config | ISO attached? Boot order has DVD first? |
| UEFI shell, or a blank screen before GRUB | VM config | Firmware must be BIOS |
| GRUB shows `error: no multiboot header found` | Kernel / linker | Multiboot test in `make test`; `.multiboot` must come first in `linker.ld` |
| GRUB `invalid arch-dependent ELF magic` | Toolchain | Kernel must be 32-bit (`file build/madire.kernel` should show ELF 32-bit) |
| Screen blank after GRUB | Kernel | Check `build/serial-boot.log` from `make test` |
| VM resets straight back to GRUB/BIOS | CPU tables | Triple fault. Run `make test`: whichever status line is missing from `build/serial-boot.log` shows the failing step. |
| A status line says `FAILED` | That subsystem | The check reads back what the CPU or hardware reports; see `docs/architecture.md` |
| Logo in wrong colours | VGA palette | `drivers/vga/vga_text.c` `vga_set_palette()` |
| `run.ps1`: "state is Saved" | VM state | Use the current `run.ps1`, which discards saved states automatically (since commit `f07b517`) |
