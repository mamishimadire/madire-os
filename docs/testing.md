# Testing

## 1. Automated test (`make test`)

This runs in the Linux build environment and is included in `scripts/build.ps1`.

1. `grub-file --is-x86-multiboot build/madire.kernel` confirms GRUB will accept the kernel.
2. The ISO boots in QEMU with no display for up to 15 seconds. The kernel copies all of its screen text to serial port COM1, which QEMU writes to `build/serial.log`.
3. The test passes if `build/serial.log` contains `System ready.`

## 2. VirtualBox test (manual acceptance)

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
.\scripts\build.ps1     # produces build\madire-os.iso
.\scripts\run.ps1       # attaches the ISO, sets boot order DVD -> disk, starts the VM
```

To do it by hand instead: VM **Settings > Storage**, select the optical drive, choose `build\madire-os.iso`, then **Start**.

### Expected result

1. The GRUB menu shows **Madire OS v0.1** and boots automatically after 3 seconds.
2. The screen shows:

```
-------------------------------------------------------------------------------
                               M A D I R E   O S
-------------------------------------------------------------------------------

Madire OS v0.1

Kernel started successfully.

CPU initialization:     OK
Memory initialization:  OK
Display initialization: OK

System ready.
```

The VM then sits idle with the CPU halted. Close it with **Machine > Close > Power off**.

### Troubleshooting by layer

| Symptom | Layer | Check |
|---|---|---|
| "No bootable medium found" | ISO / VM config | ISO attached? Boot order has DVD first? |
| UEFI shell or blank screen before GRUB | VM config | Firmware must be BIOS |
| GRUB shows `error: no multiboot header found` | Kernel / linker | `make test` step 1; `.multiboot` must come first in `linker.ld` |
| GRUB `invalid arch-dependent ELF magic` | Toolchain | Kernel must be 32-bit (`file build/madire.kernel` should show ELF 32-bit) |
| Screen blank after GRUB | Kernel | Check `build/serial.log` from `make test` |
| "Error: not loaded by a Multiboot bootloader" | Bootloader | `grub.cfg` must use `multiboot`, not `multiboot2` |
