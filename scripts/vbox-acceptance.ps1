# VirtualBox acceptance test for Madire OS (0.9; see docs/testing.md).
#
# Boots the given ISO in the test VM once per test, sends keys through
# VirtualBox's emulated PS/2 keyboard, saves a screenshot and a copy of the
# serial log (COM1) of every run, and checks each log for the line that
# proves the test passed. The results are compared with QEMU afterwards.
#
# The VM's settings are CHECKED, never changed: if the OS type, firmware,
# memory, PAE, boot order or COM1 port/file differ from the documented
# values, the script refuses to run. Attaching the given ISO to the
# optical drive for the run is the only change it makes (as scripts\run.ps1
# does), and it verifies the attachment.
#
#   .\scripts\vbox-acceptance.ps1                      # every test
#   .\scripts\vbox-acceptance.ps1 -Only boot,preempt   # some tests
#   .\scripts\vbox-acceptance.ps1 -Only repeat -Repeat 20   # 20 normal boots
#   .\scripts\vbox-acceptance.ps1 -VBoxManage "D:\VirtualBox\VBoxManage.exe"
#
# 1.3 stage 8: an owner-run step (AGENTS.md section 6): run from plain
# PowerShell with VS Code closed. Before anything else it measures the
# available host memory three times (5 s apart) and refuses unless all
# three are at or above -MinFreeMB (5,500 MB); it refuses while Kali or
# MadireOS-View runs; it writes everything into one new result folder
# (-OutDir, by default build\vbox-acceptance-<date>-<time>; an existing,
# non-empty folder is refused, never overwritten); it saves showvminfo
# before and after and reports every line that differs (only the medium
# and VMStateChangeTime may); and at the end, also after an error, it
# restores the resting medium (-RestoreIso, checked against
# -RestoreSha256) and records its SHA-256 and the medium as showvminfo
# reads it. It closes no application and asks for no credential. The
# summary's last line is its exit status.
#
# 1.3 stage 9: -IsoSha256 and -ModuleIsoSha256, when given, are checked
# before anything starts (a difference refuses the run); the module ISO is
# needed only when the module group runs. -Risk13 N adds the risk 13
# sample (N normal boots, each to the desktop's first frame; the risk13
# group, below); with -Risk13 0, the default, the run is unchanged.
#
#   powershell -ExecutionPolicy Bypass -File scripts\vbox-acceptance.ps1 -Iso <iso> -IsoSha256 <sha256> `
#       -ModuleIso <module iso> -ModuleIsoSha256 <sha256>          # every group
#   powershell -ExecutionPolicy Bypass -File scripts\vbox-acceptance.ps1 -Iso <iso> -IsoSha256 <sha256> `
#       -Only risk13 -Risk13 30                                     # the risk 13 sample
param(
    [string]$VM = "MadireOS-Test",
    [string]$VBoxManage = "",
    [string]$Iso = "",                  # default: build\madire-os.iso
    [string]$IsoSha256 = "",            # 1.3 stage 9: checked before the run when given
    [string]$ModuleIso = "",            # default: build\madire-os-module.iso
    [string]$ModuleIsoSha256 = "",      # 1.3 stage 9: checked before the run when given (module group only)
    [string]$OutDir = "",               # default: build\vbox-acceptance-<date>-<time> (1.3 stage 8: always a new folder)
    [string]$Prefix = "vbox",
    [string[]]$Only = @(),              # menu, boot, graphics, desktop, procs, commands, reboot, scrollback, preempt, threadoverflow, pf, diag, diagmore, user, smx, memexhaust, module, shell, files, repeat, keywindow, risk13
    [string]$Python = "python",         # 1.2 stage 10b: makes the reference screens (standard library only)
    [int]$KeyDelayMs = 3500,            # keys no sooner than 3 s after power-on
    [int]$MenuHoldMs = 15000,           # 0.13: expected time to the boot menu (the wait for it allows 30 s more)
    [int]$Repeat = 0,                   # extra normal boots (self-test result of each)
    [int]$Risk13 = 0,                   # 1.3 stage 9: the risk 13 sample's boots (the risk13 group)
    [string]$KaliVM = "kali-linux-2026.2-virtualbox-amd64",  # must not be running (0.11)
    [string]$ViewVM = "MadireOS-View",  # 1.3 stage 8: must not be running (AGENTS.md section 6)
    [int]$MinFreeMB = 5500,             # 1.3 stage 8: the owner's threshold for this step (three samples)
    [string]$RestoreIso = "",           # the resting medium; default: build\v1.2-release\madire-os.iso
    [string]$RestoreSha256 = "2a2acbe599c003d4e3015a6ab7bfd1632d15c8741dbcffede07381cb8b7292c6"
)
$ErrorActionPreference = "Stop"
$root = (Get-Item (Split-Path $PSScriptRoot -Parent)).FullName
$build = Join-Path $root "build"
if (-not $Iso) { $Iso = Join-Path $build "madire-os.iso" }
if (-not $ModuleIso) { $ModuleIso = Join-Path $build "madire-os-module.iso" }
if (-not $OutDir) { $OutDir = Join-Path $build ("vbox-acceptance-" + (Get-Date -Format "yyyyMMdd-HHmmss")) }
if (-not $RestoreIso) { $RestoreIso = Join-Path $build "v1.2-release\madire-os.iso" }

# ---- 1.3 stage 8: host memory first (AGENTS.md section 6): three samples, 5 s apart ----
$memSamples = @()
for ($i = 0; $i -lt 3; $i++) {
    if ($i) { Start-Sleep -Seconds 5 }
    $memSamples += [int]((Get-CimInstance Win32_OperatingSystem).FreePhysicalMemory / 1KB)
}
$memLine = "host memory available: {0} MB (threshold {1} MB)" -f ($memSamples -join " / "), $MinFreeMB
Write-Host $memLine
if (@($memSamples | Where-Object { $_ -lt $MinFreeMB }).Count) {
    Write-Host "ACCEPTANCE RUN REFUSED: $memLine; a sample is below the threshold (nothing started, no application closed)"
    exit 1
}
$serial = Join-Path $build "vbox-serial.log"    # the documented COM1 file

# 0.10: the version is read once, from kernel/kernel.h; the checks that
# contain it build their regex from $VRe, so a new version changes no check.
$verMatch = Select-String -Path (Join-Path $root "kernel\kernel.h") -Pattern '^#define MADIRE_OS_VERSION "([0-9]+(\.[0-9]+)*)"$'
if (-not $verMatch) { throw "cannot read MADIRE_OS_VERSION from kernel\kernel.h" }
$VRe = [regex]::Escape($verMatch.Matches[0].Groups[1].Value)   # 0.9.2 -> 0\.9\.2

# ---- VBoxManage ----
function Find-VBoxManage {
    if ($VBoxManage) { return $VBoxManage }
    $dirs = @()
    if ($env:VBOX_MSI_INSTALL_PATH) { $dirs += $env:VBOX_MSI_INSTALL_PATH }
    try { $dirs += (Get-ItemProperty "HKLM:\SOFTWARE\Oracle\VirtualBox" -ErrorAction Stop).InstallDir } catch {}
    if ($env:ProgramFiles) { $dirs += (Join-Path $env:ProgramFiles "Oracle\VirtualBox") }
    if (${env:ProgramFiles(x86)}) { $dirs += (Join-Path ${env:ProgramFiles(x86)} "Oracle\VirtualBox") }
    foreach ($d in $dirs) {
        $exe = Join-Path $d "VBoxManage.exe"
        if (Test-Path $exe) { return $exe }
    }
    throw "VBoxManage.exe not found (set VBOX_MSI_INSTALL_PATH or pass -VBoxManage)."
}
$vbox = Find-VBoxManage

# VBoxManage reports progress ("0%...100%") on stderr. Windows PowerShell
# 5.1 turns native stderr into error records, which would stop the script
# under "Stop", so errors are non-terminating here and the exit code decides.
function VBox {
    $old = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    try { $out = & $script:vbox @args 2>&1 } finally { $ErrorActionPreference = $old }
    $lines = @($out | ForEach-Object { "$_" })
    if ($LASTEXITCODE -ne 0) { throw "VBoxManage $($args -join ' ') failed: $($lines -join ' ')" }
    return $lines
}

function Get-VMInfo {
    $info = @{}
    foreach ($line in (VBox showvminfo $VM --machinereadable)) {
        if ($line -match '^"?([^"=]+)"?="?(.*?)"?$') { $info[$Matches[1]] = $Matches[2] }
    }
    return $info
}

function Test-SamePath([string]$a, [string]$b) {
    $na = [IO.Path]::GetFullPath($a.Replace('\\', '\')).TrimEnd('\')
    $nb = [IO.Path]::GetFullPath($b.Replace('\\', '\')).TrimEnd('\')
    return [string]::Equals($na, $nb, [StringComparison]::OrdinalIgnoreCase)
}

# ---- the VM must be as documented (docs/testing.md); never changed here ----
$info = Get-VMInfo
$problems = @()
if ($info["ostype"] -notin @("Other/Unknown (64-bit)", "Other_64")) { $problems += "OS type is '$($info['ostype'])' (expected Other_64)" }
if ($info["firmware"] -ne "BIOS") { $problems += "firmware is '$($info['firmware'])' (expected BIOS)" }
if ($info["memory"] -ne "4096") { $problems += "memory is $($info['memory']) MB (expected 4096)" }
if ($info["pae"] -ne "off") { $problems += "PAE is '$($info['pae'])' (expected off, the default)" }
if ($info["boot1"] -ne "dvd") { $problems += "first boot device is '$($info['boot1'])' (expected dvd)" }
if ($info["uart1"] -ne "0x03f8,4") { $problems += "COM1 is '$($info['uart1'])' (expected 0x03f8,4)" }
$mode = $info["uartmode1"]
if (-not ($mode -like "file,*") -or -not (Test-SamePath $mode.Substring(5) $serial)) {
    $problems += "COM1 output is '$mode' (expected file,$serial)"
}
# 1.0 (stage 11): the storage, every controller and port: one SATA (AHCI)
# controller; port 0 the documented unused disk MadireOS-Test.vdi, port 1
# the optical drive (the script attaches the ISO under test there), nothing
# else attached anywhere.
$ctls = @($info.Keys | Where-Object { $_ -match '^storagecontrollername[0-9]+$' } | ForEach-Object { $info[$_] })
if ($ctls.Count -ne 1 -or $ctls[0] -ne "SATA" -or $info["storagecontrollertype0"] -ne "IntelAhci") {
    $problems += "storage controllers are '$($ctls -join ', ')' (expected one: SATA, IntelAhci)"
}
$disk = "$($info['SATA-0-0'])".Replace('\\', '\')
if (-not $disk -or $disk -eq "none" -or (Split-Path $disk -Leaf) -ne "MadireOS-Test.vdi" -or $info.ContainsKey("SATA-tempeject-0-0")) {
    $problems += "SATA port 0 is '$disk' (expected the hard disk MadireOS-Test.vdi)"
}
if (-not $info.ContainsKey("SATA-tempeject-1-0")) { $problems += "SATA port 1 is not an optical drive ('$($info['SATA-1-0'])')" }
foreach ($c in $ctls) {
    foreach ($k in @($info.Keys | Where-Object { $_ -match ('^' + [regex]::Escape($c) + '-[0-9]+-[0-9]+$') })) {
        if ($k -notin @("SATA-0-0", "SATA-1-0") -and $info[$k] -ne "none") { $problems += "$k holds '$($info[$k])' (expected nothing)" }
    }
}
# 1.2 stage 10b: the display and the pointing device, as the stage 3 probe
# found them (docs/plans/1.2.md: VBoxSVGA, 128 MB VRAM; decision 11: the
# USB tablet kept). Read only, like every check here.
if ($info["graphicscontroller"] -ne "vboxsvga") { $problems += "graphics controller is '$($info['graphicscontroller'])' (expected vboxsvga)" }
if ($info["vram"] -ne "128") { $problems += "video memory is $($info['vram']) MB (expected 128)" }
if ($info["hidpointing"] -ne "usbtablet") { $problems += "pointing device is '$($info['hidpointing'])' (expected usbtablet)" }
if ($info["VMState"] -notin @("poweroff", "aborted")) {
    $problems += "the VM is '$($info['VMState'])'; power it off first (nothing is discarded here)"
}
if ($problems.Count) {
    $problems | ForEach-Object { Write-Host "VM check: $_" }
    throw "The VM does not match the documented settings; refusing to run (no setting was changed)."
}
# Rule (since 0.11): no acceptance run while the Kali build VM runs (a
# regression there would distort this run's timing evidence).
$running = @(VBox list runningvms | ForEach-Object { if ($_ -match '^"(.+)" \{') { $Matches[1] } })
if ($running -contains $KaliVM) { throw "The Kali build VM '$KaliVM' is running; refusing to run (power it off first)." }
# 1.3 stage 8: only one VM at a time (AGENTS.md section 6).
if ($running -contains $ViewVM) { throw "The VM '$ViewVM' is running; refusing to run (power it off first)." }
# 1.3 stage 9: the module ISO only when the module group runs.
$wantModule = ($Only.Count -eq 0) -or ($Only -contains "module")
$isos = @($Iso)
if ($wantModule) { $isos += $ModuleIso }
foreach ($f in @($isos + $RestoreIso)) { if (-not (Test-Path $f)) { throw "Not found: $f" } }
$Iso = (Get-Item $Iso).FullName
if ($wantModule) { $ModuleIso = (Get-Item $ModuleIso).FullName; $isos = @($Iso, $ModuleIso) } else { $isos = @($Iso) }
$RestoreIso = (Get-Item $RestoreIso).FullName
$restoreHash = (Get-FileHash $RestoreIso -Algorithm SHA256).Hash.ToLower()
if ($restoreHash -ne $RestoreSha256.ToLower()) { throw "The resting medium's SHA-256 is $restoreHash (expected $RestoreSha256); refusing to run." }
# 1.3 stage 9: the ISOs under test against their expected SHA-256, before anything starts.
$isoHashes = @{}
foreach ($f in $isos) { $isoHashes[$f] = (Get-FileHash $f -Algorithm SHA256).Hash.ToLower() }
$isoWanted = @{ $Iso = $IsoSha256.ToLower() }
if ($wantModule) { $isoWanted[$ModuleIso] = $ModuleIsoSha256.ToLower() }
foreach ($f in $isos) {
    if ($isoWanted[$f] -and $isoHashes[$f] -ne $isoWanted[$f]) { throw "The SHA-256 of $f is $($isoHashes[$f]) (expected $($isoWanted[$f])); refusing to run." }
}
# 1.3 stage 8: one new result folder; an earlier one is never overwritten.
if ((Test-Path $OutDir) -and @(Get-ChildItem $OutDir -Force).Count) { throw "The result folder $OutDir exists and is not empty; refusing to run (nothing is overwritten)." }
New-Item -ItemType Directory -Force $OutDir | Out-Null
$OutDir = (Get-Item $OutDir).FullName
$summary = Join-Path $OutDir "$Prefix-summary.txt"
$results = New-Object System.Collections.Generic.List[string]
function Note([string]$s) { Write-Host $s; $script:results.Add($s) }
$memSamples -join "`r`n" | Set-Content -Path (Join-Path $OutDir "$Prefix-host-memory.txt") -Encoding ascii
Note ("started {0}; {1}" -f (Get-Date -Format "yyyy-MM-dd HH:mm:ss zzz"), $memLine)
$vmBefore = @(VBox showvminfo $VM --machinereadable)
$vmBefore | Set-Content -Path (Join-Path $OutDir "$Prefix-vminfo-before.txt") -Encoding ascii
Note "resting medium (checked before the run): $restoreHash  $RestoreIso"

Note "VM check: $VM matches the documented settings (Other_64, BIOS, 4096 MB, PAE off, boot DVD, COM1 0x3F8 IRQ 4 to build\vbox-serial.log; storage: SATA port 0 MadireOS-Test.vdi, port 1 optical, nothing else; display VBoxSVGA, 128 MB VRAM; pointing device USB tablet)"
foreach ($f in $isos) {
    Note ("ISO: {0}  {1} ({2})" -f $isoHashes[$f], $f, $(if ($isoWanted[$f]) { "the expected SHA-256: checked" } else { "no expected SHA-256 given" }))
}

# ---- 1.2 stage 10b: the reference screens (made here, from this tree) ----
# The menu's wallpaper and its layout (scripts/make-wallpaper.py, the one
# source; the ISO's /boot/limine/wallpaper.png is byte-identical to it, the
# [1.1] S2a check): Limine's menu is told by its header box, since the
# kernel's own screen is 1024x768 too (docs/plans/1.2.md, D2). For the
# graphics group also the splash's five screens and the console after
# "clear" (tests/screen_ref.py; the QEMU [1.2] check proves the console's
# reference equal to the kernel's rendering).
$refDir = Join-Path $OutDir "ref"
New-Item -ItemType Directory -Force $refDir | Out-Null
$Wallpaper = Join-Path $refDir "wallpaper.png"
$Layout = Join-Path $refDir "layout.json"
function Py {
    $old = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    try { $out = & $script:Python @args 2>&1 } finally { $ErrorActionPreference = $old }
    $lines = @($out | ForEach-Object { "$_" })
    if ($LASTEXITCODE -ne 0) { throw "$Python $($args -join ' ') failed: $($lines -join ' ')" }
    return $lines
}
$verText = $verMatch.Matches[0].Groups[1].Value
Py -I (Join-Path $root "scripts\make-wallpaper.py") $Wallpaper --version $verText --layout-json $Layout | Out-Null
$hb = @((Get-Content $Layout -Raw | ConvertFrom-Json).header)
if ($hb.Count -ne 4) { throw "${Layout}: no header box" }
Note ("reference (report only): the menu's wallpaper {0} (make-wallpaper.py, v{1}); header box {2}" -f
      (Get-FileHash $Wallpaper -Algorithm SHA256).Hash.ToLower(), $verText, ($hb -join ','))
if (($Only.Count -eq 0) -or ($Only -contains "graphics")) {
    foreach ($l in (Py -I (Join-Path $root "tests\screen_ref.py") splash 1024 768 $refDir)) { Note "reference (report only): $l" }
    foreach ($l in (Py -I (Join-Path $root "tests\screen_ref.py") console 1024 768 (Join-Path $refDir "console-cleared.rgb"))) {
        Note "reference (report only): $l"
    }
}
# 1.3 stage 8: the real ISO shows the desktop at System ready (the default
# path), so the graphics and desktop groups compare with the desktop's
# references (tests/screen_ref.py, from tests/desktop_ref.py), the clock's
# cells masked (decision 12: its time is not known here).
$Mask = $null
if (($Only.Count -eq 0) -or ($Only -contains "graphics") -or ($Only -contains "desktop") -or ($Only -contains "risk13")) {
    foreach ($kind in @("desktop", "desktop-console")) {
        foreach ($l in (Py -I (Join-Path $root "tests\screen_ref.py") $kind 1024 768 (Join-Path $refDir "$kind.rgb"))) {
            Note "reference (report only): $l"
            if ($l -match '^MASK ([0-9]+) ([0-9]+) ([0-9]+) ([0-9]+) ') { $Mask = @([int]$Matches[1], [int]$Matches[2], [int]$Matches[3], [int]$Matches[4]) }
        }
    }
    if (-not $Mask) { throw "tests\screen_ref.py gave no clock mask" }
}

# A screenshot's size, whether it is one colour, whether it shows the
# menu's header box (the wallpaper centred, as Limine's wallpaper_style:
# centered draws it), and how many pixels differ from a raw RGB reference
# (-1: another size). VirtualBox's screenshots are PNG files.
Add-Type -ReferencedAssemblies System.Drawing -TypeDefinition @'
using System;
using System.Drawing;
using System.Drawing.Imaging;
using System.IO;
using System.Runtime.InteropServices;
public static class MadireShot {
    static string refPath;
    static int[] refPx;
    static int refW, refH;
    static int[] Load(string path, out int w, out int h) {
        using (Bitmap b = new Bitmap(path)) {
            w = b.Width; h = b.Height;
            BitmapData d = b.LockBits(new Rectangle(0, 0, w, h), ImageLockMode.ReadOnly, PixelFormat.Format32bppRgb);
            int[] px = new int[w * h];
            for (int y = 0; y < h; y++)
                Marshal.Copy(new IntPtr(d.Scan0.ToInt64() + (long)y * d.Stride), px, y * w, w);
            b.UnlockBits(d);
            for (int i = 0; i < px.Length; i++) px[i] &= 0xFFFFFF;
            return px;
        }
    }
    // "<w> <h> <blank: yes|no> <header: match|differ>"
    public static string Classify(string shot, string wallpaper, int x0, int y0, int x1, int y1) {
        int w, h;
        int[] p = Load(shot, out w, out h);
        bool blank = p.Length > 0;
        for (int i = 1; i < p.Length && blank; i++) if (p[i] != p[0]) blank = false;
        if (refPath != wallpaper) { refPx = Load(wallpaper, out refW, out refH); refPath = wallpaper; }
        bool header = w >= refW && h >= refH;
        if (header) {
            int ox = (w - refW) / 2, oy = (h - refH) / 2;
            for (int y = y0; y <= y1 && header; y++)
                for (int x = x0; x <= x1; x++)
                    if (p[(oy + y) * w + ox + x] != refPx[y * refW + x]) { header = false; break; }
        }
        return string.Format("{0} {1} {2} {3}", w, h, blank ? "yes" : "no", header ? "match" : "differ");
    }
    // The pixels of the screenshot that differ from the reference (raw RGB,
    // width x height), or -1 for another size.
    public static long Differ(string shot, string rgb, int width, int height) {
        int w, h;
        int[] p = Load(shot, out w, out h);
        if (w != width || h != height) return -1;
        byte[] r = File.ReadAllBytes(rgb);
        if (r.Length != (long)w * h * 3) return -1;
        long n = 0;
        for (int i = 0; i < p.Length; i++)
            if (p[i] != ((r[3 * i] << 16) | (r[3 * i + 1] << 8) | r[3 * i + 2])) n++;
        return n;
    }
    // 1.3 stage 8: "<pixels differing outside the mask> <inside it>" (the
    // clock's cells, x y w h), or "-1 -1" for another size.
    public static string DifferMasked(string shot, string rgb, int width, int height, int mx, int my, int mw, int mh) {
        int w, h;
        int[] p = Load(shot, out w, out h);
        if (w != width || h != height) return "-1 -1";
        byte[] r = File.ReadAllBytes(rgb);
        if (r.Length != (long)w * h * 3) return "-1 -1";
        long outside = 0, inside = 0;
        for (int i = 0; i < p.Length; i++) {
            if (p[i] == ((r[3 * i] << 16) | (r[3 * i + 1] << 8) | r[3 * i + 2])) continue;
            int x = i % w, y = i / w;
            if (x >= mx && x < mx + mw && y >= my && y < my + mh) inside++; else outside++;
        }
        return outside + " " + inside;
    }
}
'@

# ---- keys and screenshots ----
function Key { VBox controlvm $VM keyboardputscancode @args | Out-Null; Start-Sleep -Milliseconds 150 }
function TypeText([string]$s) { VBox controlvm $VM keyboardputstring $s | Out-Null; Start-Sleep -Milliseconds 400 }
function Enter { Key 1c 9c; Start-Sleep -Milliseconds 400 }
function Down { Key e0 50 e0 d0 }
function HomeKey { Key e0 47 e0 c7 }
function Shot([string]$name) {
    VBox controlvm $VM screenshotpng (Join-Path $OutDir "$Prefix-$name.png") | Out-Null
}

function Attach([string]$file) {
    VBox storageattach $VM --storagectl SATA --port 1 --device 0 --type dvddrive --medium $file | Out-Null
    $now = (Get-VMInfo)["SATA-1-0"]
    if (-not (Test-SamePath $now $file)) { throw "Optical drive holds '$now', expected '$file'." }
}

function Start-Run {
    if (Test-Path $serial) { Remove-Item $serial -Confirm:$false }
    VBox startvm $VM --type headless | Out-Null
    Start-Sleep -Milliseconds $KeyDelayMs
}

# Waits until the serial log matches `pattern` (true) or the timeout passes.
function Wait-Serial([string]$pattern, [int]$seconds) {
    for ($i = 0; $i -lt $seconds * 2; $i++) {
        if ((Test-Path $serial) -and (Select-String -Path $serial -Pattern $pattern -Quiet)) { return $true }
        Start-Sleep -Milliseconds 500
    }
    return $false
}

# Powers off, keeps the serial log as <Prefix>-<name>.serial.log, and checks
# it for each expected pattern.
function Stop-Run([string]$name, [string[]]$expect) {
    VBox controlvm $VM poweroff | Out-Null
    Start-Sleep -Seconds 2
    $copy = Join-Path $OutDir "$Prefix-$name.serial.log"
    if (Test-Path $serial) { Copy-Item $serial $copy -Force } else { Note "${name}: NO SERIAL LOG"; return }
    foreach ($p in $expect) {
        $ok = Select-String -Path $copy -Pattern $p -Quiet
        Note ("{0,-16} {1,-8} /{2}/" -f $name, $(if ($ok) { "OK" } else { "MISSING" }), $p)
    }
}

# Diagnostics positions (0.9; 16-26 appended in 0.10; since 0.13 the
# sub-entries of the boot menu's "Diagnostics" directory, in limine.conf's
# order): position N is reached with N + 1 presses of the down arrow from the
# expanded "Diagnostics" directory.
$Diag = [ordered]@{
    divzero = 0; ud = 1; gpf = 2; pfnull = 3; pfunmapped = 4; pfreadonly = 5; pfnx = 6;
    pflow = 7; stackoverflow = 8; memexhaust = 9; doublefree = 10; freereserved = 11;
    timer = 12; keyboard = 13; preempt = 14; threadoverflow = 15;
    userspin = 16; userkill = 17; userfault = 18; userpriv = 19; userkmem = 20;
    usersys = 21; userio = 22; userpreempt = 23; userchurn = 24; smep = 25; smap = 26
}

# Notes a check that a pattern is absent from a kept serial log (case-
# sensitive: normal boot lines say "panic_exception" and "panics").
function Absent([string]$name, [string]$pattern) {
    $log = Join-Path $OutDir "$Prefix-$name.serial.log"
    if (-not (Test-Path $log)) { return }
    $n = @(Select-String -Path $log -Pattern $pattern -CaseSensitive).Count
    Note ("{0,-16} {1,-8} /no {2}: {3} lines/" -f $name, $(if ($n -eq 0) { "OK" } else { "MISSING" }), $pattern, $n)
}

# 0.13: the width of the VM's screen now (from a screenshot's PNG header).
# Limine draws its menu on a 1024x768 graphics screen; the BIOS splash and
# Madire's text screen are 720x400 (or 640x480). 1.2 stage 10b: Madire's
# normal boot is 1024x768 too since the switch, so only the menu's header
# box tells the menu apart (Test-MenuShown, below); the width here only
# tells whether there is a screen yet.
# 1.0 (stage 11): 0 also when the VM has no screen yet (VirtualBox refuses
# the screenshot, "Unsupported resolution 0x0"), instead of ending the run.
function Get-ScreenWidth {
    $f = Join-Path $OutDir "$Prefix-screen-probe.png"
    if (Test-Path $f) { Remove-Item $f -Confirm:$false }
    try { VBox controlvm $VM screenshotpng $f 2>$null | Out-Null } catch { return 0 }
    if (-not (Test-Path $f)) { return 0 }
    $b = [IO.File]::ReadAllBytes($f)
    Remove-Item $f -Confirm:$false
    if ($b.Length -lt 24) { return 0 }
    return ([int]$b[16] -shl 24) -bor ([int]$b[17] -shl 16) -bor ([int]$b[18] -shl 8) -bor [int]$b[19]
}

# 1.2 stage 10b: a screenshot now, classified (MadireShot): W, H, Blank (one
# colour), Menu (the menu's header box shown) and Path (the PNG, kept until
# the next call); $null when VirtualBox gives none (no screen yet).
function Get-Shot {
    $f = Join-Path $OutDir "$Prefix-screen-shot.png"
    if (Test-Path $f) { Remove-Item $f -Confirm:$false }
    try { VBox controlvm $VM screenshotpng $f 2>$null | Out-Null } catch { return $null }
    if (-not (Test-Path $f)) { return $null }
    $c = ([MadireShot]::Classify($f, $Wallpaper, [int]$hb[0], [int]$hb[1], [int]$hb[2], [int]$hb[3])) -split ' '
    return [pscustomobject]@{ W = [int]$c[0]; H = [int]$c[1]; Blank = $c[2] -eq "yes"; Menu = $c[3] -eq "match"; Path = $f }
}
# Limine's menu is on the screen: a screenshot with the menu's header box.
# (Until 1.2 stage 10b: a screen 1024 pixels wide; the kernel's own screen
# is 1024x768 since the switch, docs/plans/1.2.md, D2.)
function Test-MenuShown { $s = Get-Shot; return ($null -ne $s) -and $s.Menu }
# The kernel's mode from its Display: line ("1024x768"; "720x400" for VGA
# text; "" before the line).
function Get-KernelMode {
    if (-not (Test-Path $serial)) { return "" }
    $m = Select-String -Path $serial -Pattern '^Display: framebuffer ([0-9]+)x([0-9]+)x[0-9]+ at ' | Select-Object -First 1
    if ($m) { return "{0}x{1}" -f $m.Matches[0].Groups[1].Value, $m.Matches[0].Groups[2].Value }
    if (Select-String -Path $serial -Pattern '^Display: VGA text ' -Quiet) { return "720x400" }
    return ""
}

# 0.13: powers on and stops at the boot menu. 1.1: the menu is hidden (quiet):
# Limine shows nothing and boots the first entry after 2 s unless a key
# arrives; the first key shows the menu and is discarded (common/menu.c), and
# the menu then waits with no countdown. So Home is pressed every 300 ms, for
# at most $MenuHoldMs + 30 s, until the screen shows Limine's menu (1.2 stage
# 10b: its header box, Test-MenuShown; until then a screen 1024 pixels
# wide); Home only selects the first entry, so a press still queued changes
# nothing. (In 0.13 keys during the BIOS's power-on self-test stopped it with
# "FATAL: Keyboard error" on this VM; the 1.1 probe on a scratch VM saw no
# error, and the keywindow group repeats it here.) After 4 s, longer than the
# 2 s timeout, the menu must still be on the screen and the serial log still
# empty (no kernel started). Returns $true if both hold, otherwise notes the
# failure and returns $false.
# 1.0 (stage 11): first the screen must exist: a screenshot is retried for
# at most 10 s after startvm (the time is reported); if there is still none,
# the menu counts as not shown (MISSING) and the run carries on.
$script:ScreenWaits = New-Object System.Collections.Generic.List[int]
function Start-Menu([string]$name) {
    if (Test-Path $serial) { Remove-Item $serial -Confirm:$false }
    VBox startvm $VM --type headless | Out-Null
    $t0 = Get-Date
    while ((Get-ScreenWidth) -eq 0 -and ((Get-Date) - $t0).TotalSeconds -lt 10) { Start-Sleep -Milliseconds 250 }
    $waited = [int]((Get-Date) - $t0).TotalMilliseconds
    if ((Get-ScreenWidth) -eq 0) {
        Note ("{0,-16} {1,-8} /a screen within 10 s of startvm (no screenshot after {2} ms)/" -f $name, "MISSING", $waited)
        return $false
    }
    $script:ScreenWaits.Add($waited)
    $until = (Get-Date).AddMilliseconds($MenuHoldMs + 30000)
    $shown = $false
    while (-not $shown -and (Get-Date) -lt $until) {
        $next = (Get-Date).AddMilliseconds(300)
        HomeKey
        if (Test-MenuShown) { $shown = $true; break }
        if ((Test-Path $serial) -and (Get-Item $serial).Length -gt 0) { break }   # booted without the menu
        while ((Get-Date) -lt $next) { Start-Sleep -Milliseconds 20 }
    }
    if ($shown) { Start-Sleep -Seconds 4 }
    $quiet = -not ((Test-Path $serial) -and (Get-Item $serial).Length -gt 0)
    $held = $shown -and $quiet -and (Test-MenuShown)
    if (-not $held) {
        Note ("{0,-16} {1,-8} /boot menu shown and held (menu seen: {2}; no kernel output: {3})/" -f $name, "MISSING", $shown, $quiet)
    }
    return $held
}

# Boots a Diagnostics entry through the boot menu (0.13: Limine): the menu
# held at the first entry, Down to "Diagnostics", Enter expands it (the
# selection stays on it), Down N + 1 times to position N, Enter boots it.
$script:DiagBooted = [ordered]@{}
function Start-Diag([string]$test) {
    if (-not (Start-Menu $test)) { return }
    HomeKey; Start-Sleep -Milliseconds 300
    Down; Start-Sleep -Milliseconds 300
    Enter; Start-Sleep -Milliseconds 500
    for ($i = 0; $i -le $Diag[$test]; $i++) { Down }
    Start-Sleep -Milliseconds 300
    Enter
    $script:DiagBooted[$test] = $Diag[$test]
}

# 1.1 (stage 9): VBoxManage debugvm <vm> info ps2k (read only), and the light
# byte of its "LED state XX" line ("none" if there is no such line).
function Get-Ps2k {
    try { return @(VBox debugvm $VM info ps2k) } catch { return @("debugvm failed: $_") }
}
function Get-Ps2kLeds([string[]]$lines) {
    $m = [regex]::Match(($lines -join "`n"), 'LED state ([0-9A-Fa-f]{2})')
    if ($m.Success) { return $m.Groups[1].Value.ToUpper() } else { return "none" }
}

function Want([string]$group) { return ($Only.Count -eq 0) -or ($Only -contains $group) }

# 1.3 stage 8: a kept screenshot <Prefix>-<shot>.png against a reference
# in the ref folder, the clock's cells ($Mask) masked: @(outside, inside),
# -1 for another size or a missing file.
function Get-MaskedDiffer([string]$shot, [string]$ref) {
    $png = Join-Path $OutDir "$Prefix-$shot.png"
    $rgb = Join-Path $refDir $ref
    if (-not (Test-Path $png) -or -not (Test-Path $rgb)) { return @(-1, -1) }
    $r = [MadireShot]::DifferMasked($png, $rgb, 1024, 768, $Mask[0], $Mask[1], $Mask[2], $Mask[3]) -split ' '
    return @([long]$r[0], [long]$r[1])
}

$halted = "System halted\. Power off the machine\."

# 1.3 stage 8 (AGENTS.md section 6): at the end, also after an error: the
# VM powered off, the resting medium back in the optical drive (its SHA-256
# and the medium as showvminfo reads it recorded), and showvminfo after the
# run against before: only the medium and VMStateChangeTime may differ.
function Finish-Run {
    try { if ((Get-VMInfo)["VMState"] -eq "running") { VBox controlvm $VM poweroff | Out-Null; Start-Sleep -Seconds 2 } } catch {}
    try {
        VBox storageattach $VM --storagectl SATA --port 1 --device 0 --type dvddrive --medium $RestoreIso | Out-Null
        $now = (Get-VMInfo)["SATA-1-0"]
        $h = (Get-FileHash $RestoreIso -Algorithm SHA256).Hash.ToLower()
        $ok = (Test-SamePath $now $RestoreIso) -and $h -eq $RestoreSha256.ToLower()
        Note ("{0,-16} {1,-8} /the resting medium restored: SHA-256 {2}; showvminfo SATA-1-0: {3}/" -f "vm",
              $(if ($ok) { "OK" } else { "MISSING" }), $h, $now)
    } catch { Note ("{0,-16} {1,-8} /the resting medium restored: {2}/" -f "vm", "MISSING", $_) }
    try {
        $after = @(VBox showvminfo $VM --machinereadable)
        $after | Set-Content -Path (Join-Path $OutDir "$Prefix-vminfo-after.txt") -Encoding ascii
        $diff = @(Compare-Object $script:vmBefore $after)
        $other = @($diff | Where-Object { $_.InputObject -notmatch '^"?(SATA-1-0|SATA-ImageUUID-1-0|VMStateChangeTime)"?=' })
        foreach ($d in $diff) { Note ("vm (report only): showvminfo {0}: {1}" -f $(if ($d.SideIndicator -eq "<=") { "before" } else { "after " }), $d.InputObject) }
        Note ("{0,-16} {1,-8} /showvminfo before and after: {2} lines differ, {3} other than the medium and VMStateChangeTime/" -f "vm",
              $(if ($other.Count -eq 0) { "OK" } else { "MISSING" }), $diff.Count, $other.Count)
    } catch { Note ("{0,-16} {1,-8} /showvminfo after the run: {2}/" -f "vm", "MISSING", $_) }
}

try {
Attach $Iso

if (Want "menu") {
    if (Start-Menu "menu") {
        Note ("{0,-16} {1,-8} /boot menu held (no kernel output before the selection)/" -f "menu", "OK")
        HomeKey; Start-Sleep -Milliseconds 300
        Shot "boot-menu"                            # the default entry selected, no countdown
        Down; Start-Sleep -Milliseconds 300
        Enter; Start-Sleep -Milliseconds 500
        Shot "boot-menu-diagnostics-open"           # "Diagnostics" expanded, still selected
        Down; Start-Sleep -Milliseconds 300
        Shot "boot-menu-diagnostics-top"            # position 0 selected
        for ($i = 0; $i -lt 15; $i++) { Down }
        Start-Sleep -Milliseconds 300
        Shot "boot-menu-diagnostics-end"            # position 15 selected
        for ($i = 0; $i -lt 11; $i++) { Down }
        Start-Sleep -Milliseconds 300
        Shot "boot-menu-diagnostics-user"           # 0.10: position 26 (smap) selected
        Shot "boot-menu-diagnostics-scrolled"       # 1.1: the same state: the list scrolled to its end, the up markers
        $still = -not ((Test-Path $serial) -and (Get-Item $serial).Length -gt 0)
        Note ("{0,-16} {1,-8} /no boot while navigating the menu/" -f "menu", $(if ($still) { "OK" } else { "MISSING" }))
        Enter                                       # boots the selected entry: it must be smap
        Wait-Serial "^SMAP test: |$halted" 60 | Out-Null
        Start-Sleep -Seconds 1
    }
    Stop-Run "menu" @("^SMAP test: ")
}
if (Want "boot") {
    # 1.1: no key before System ready, so the hidden boot menu must never show.
    # 1.2 stage 10b (docs/plans/1.2.md, D2): the real ISO boots in graphics
    # at 1024x768, so the width no longer tells the menu apart. From startvm
    # on, a screenshot every 250 ms (or as fast as VirtualBox gives them),
    # each placed by the serial log's size read just before it: while it is
    # empty (Limine's time; the kernel writes serial before it draws) only
    # 720x400, 640x480 or a blank screen in the kernel's mode; after it only
    # the kernel's mode (its Display: line); no screenshot with the menu's
    # header box. (Until 10a: no screenshot 1024 wide.)
    if (Test-Path $serial) { Remove-Item $serial -Confirm:$false }
    VBox startvm $VM --type headless | Out-Null
    $until = (Get-Date).AddMilliseconds($KeyDelayMs + 40000)
    $ready = $false
    $shots = New-Object System.Collections.Generic.List[object]
    while (-not $ready -and (Get-Date) -lt $until) {
        $before = -not ((Test-Path $serial) -and (Get-Item $serial).Length -gt 0)
        $s = Get-Shot
        if ($s) { $shots.Add([pscustomobject]@{ Before = $before; Size = ("{0}x{1}" -f $s.W, $s.H); Blank = $s.Blank; Menu = $s.Menu }) }
        $ready = (Test-Path $serial) -and (Select-String -Path $serial -Pattern "System ready\." -Quiet)
        if (-not $ready) { Start-Sleep -Milliseconds 250 }
    }
    $kmode = Get-KernelMode
    $badBefore = @($shots | Where-Object { $_.Before -and $_.Size -notin @("720x400", "640x480") -and -not ($_.Blank -and $_.Size -eq $kmode) }).Count
    $badAfter = @($shots | Where-Object { -not $_.Before -and $_.Size -ne $kmode }).Count
    $menus = @($shots | Where-Object { $_.Menu }).Count
    $tally = { param($list) ($list | Group-Object { $_.Size + $(if ($_.Blank) { "/blank" } else { "" }) } |
                             ForEach-Object { "{0}:{1}" -f $_.Name, $_.Count }) -join " " }
    Note ("boot (report only): kernel mode {0}; before the first serial byte {1}; after {2}; menu header matches {3}" -f
          $(if ($kmode) { $kmode } else { "none" }), (& $tally @($shots | Where-Object { $_.Before })),
          (& $tally @($shots | Where-Object { -not $_.Before })), $menus)
    Note ("{0,-16} {1,-8} /normal boot without the boot menu (before the first serial byte only 720x400, 640x480 or blank; no screenshot matches the menu header)/" -f "boot",
          $(if ($ready -and $kmode -and $badBefore -eq 0 -and $badAfter -eq 0 -and $menus -eq 0) { "OK" } else { "MISSING" }))
    Start-Sleep -Seconds 1
    Shot "boot"
    # 1.1 (stage 9): VirtualBox's own view of its PS/2 keyboard (debugvm info
    # only reads the device's state), printed as a report-only line. The
    # light rows read its "LED state XX" line (the PS/2 light byte: bit 0
    # Scroll Lock, bit 1 Num Lock, bit 2 Caps Lock), so they depend on that
    # output format of VirtualBox.
    $ps2k = Get-Ps2k
    Note ("ps2k (report only): {0}" -f (($ps2k | Where-Object { "$_".Trim() }) -join " | "))
    Note ("{0,-16} {1,-8} /VirtualBox keyboard lights after boot: Num Lock on, Caps Lock and Scroll Lock off/" -f "lights",
          $(if ((Get-Ps2kLeds $ps2k) -eq "02") { "OK" } else { "MISSING" }))
    Key 39 b9; Start-Sleep -Seconds 1      # opens the console (not typed)
    # Caps Lock: the kernel sends the lights from its idle loop after the
    # key, so the state is read every 200 ms for at most 2 s until it is 06.
    Key 3a ba
    $until = (Get-Date).AddSeconds(2)
    do {
        Start-Sleep -Milliseconds 200
        $ps2k = Get-Ps2k
        $leds = Get-Ps2kLeds $ps2k
    } while ($leds -ne "06" -and (Get-Date) -lt $until)
    Note ("ps2k after Caps Lock (report only): {0}" -f (($ps2k | Where-Object { "$_".Trim() }) -join " | "))
    Note ("{0,-16} {1,-8} /VirtualBox keyboard lights after Caps Lock: Num Lock on, Caps Lock on, Scroll Lock off/" -f "lights",
          $(if ($leds -eq "06") { "OK" } else { "MISSING" }))
    Key 3a ba; Start-Sleep -Milliseconds 500      # Caps Lock off again before anything is typed
    TypeText "ps"; Enter
    TypeText "version"; Enter
    Start-Sleep -Seconds 1
    Shot "console-ps-version"
    TypeText "clear"; Enter
    TypeText "mem"; Enter
    Start-Sleep -Seconds 1
    Shot "console-mem"
    Stop-Run "boot-console" @("^Scheduler self-test: PASS", "^System ready\.", "^madire> ps$",
        "^  0  running .* kernel$", "^Madire OS v$VRe \(64-bit x86-64, Multiboot 1\)$",
        "^Kernel stack: +[0-9]+ bytes at peak, of 16384$",
        "^Key events: not logged \(boot option 'keylog' logs them; for tests only\)$",
        "^Keyboard lights: Num Lock on, Caps Lock off, Scroll Lock off \(acknowledged\)$")
    # 0.9.2: keys were typed (the console was opened and used), but key
    # events are not logged on a normal boot.
    $log = Join-Path $OutDir "$Prefix-boot-console.serial.log"
    if (Test-Path $log) {
        $keys = @(Select-String -Path $log -Pattern '^key: ').Count
        Note ("{0,-16} {1,-8} /no key event logged although keys were typed: {2} key lines/" -f "boot-console",
              $(if ($keys -eq 0) { "OK" } else { "MISSING" }), $keys)
        # 0.10: the user-mode boot self-test (hello, exitcode, nullptr as
        # processes 1-3), from the same normal boot.
        foreach ($p in @("^User-mode self-test: hello \(one line through write\): process 1 exited, status 0 \(expected 0\): OK$",
                         "^User-mode self-test: exitcode \(exit\(42\)\): process 2 exited, status 42 \(expected 42\): OK$",
                         "^User-mode self-test: nullptr \(read of address 0\): process 3 killed by a CPU exception: #PF, address 0x0000000000000000, instruction 48 8b 04 25: OK$",
                         "^User-mode self-test: PASS$")) {
            $ok = Select-String -Path $log -Pattern $p -Quiet
            Note ("{0,-16} {1,-8} /{2}/" -f "boot-selftest", $(if ($ok) { "OK" } else { "MISSING" }), $p)
        }
        # 0.10: the CPU features as the VM presents them (report only).
        $feat = @(Select-String -Path $log -Pattern '^(SMEP|SMAP|NX|CR0\.EM): ' | ForEach-Object { $_.Line })
        foreach ($f in $feat) { Write-Host "cpu (report only): $f"; $script:results.Add("cpu report: $f") }
    }
}
# 1.2 stage 10b: the graphics group. One normal boot of the real ISO (the
# default entry, in graphics since the switch): the splash while it boots,
# its ready state at System ready, then a key (not typed) closes it and
# opens the console; "display", then "clear". Each screen against its
# reference rendering, made above from this tree: the splash's
# (scripts/make-wallpaper.py, as the QEMU [1.2] splash checks use) and the
# console's after "clear" (tests/screen_ref.py, which the QEMU [1.2] check
# proves equal to the kernel's rendering). No clock, no pointer (1.2 draws
# neither on these screens). While it boots, every 1024x768 screenshot
# after the first serial byte is counted as blank (the hand-off), one of
# the splash's four dot phases, or other (taken while a frame was being
# copied; report only).
# 1.3 stage 8 (the switch): at System ready the desktop instead of the
# splash's ready state, the key opens the Console window (not typed), and
# both screens are compared with the desktop's references, the clock's
# cells masked (tests/screen_ref.py desktop and desktop-console, which the
# QEMU [1.2] check of the real ISO proves equal to the kernel's rendering).
if (Want "graphics") {
    if (Test-Path $serial) { Remove-Item $serial -Confirm:$false }
    VBox startvm $VM --type headless | Out-Null
    $until = (Get-Date).AddMilliseconds($KeyDelayMs + 40000)
    $ready = $false
    $phases = [ordered]@{ blank = 0; dots0 = 0; dots1 = 0; dots2 = 0; dots3 = 0; other = 0 }
    while (-not $ready -and (Get-Date) -lt $until) {
        $before = -not ((Test-Path $serial) -and (Get-Item $serial).Length -gt 0)
        $s = Get-Shot
        $ready = (Test-Path $serial) -and (Select-String -Path $serial -Pattern "System ready\." -Quiet)
        if ($s -and -not $before -and -not $ready -and $s.W -eq 1024 -and $s.H -eq 768) {
            $kind = "other"
            if ($s.Blank) { $kind = "blank" }
            else {
                foreach ($p in 0..3) {
                    if ([MadireShot]::Differ($s.Path, (Join-Path $refDir "splash-dots$p.rgb"), 1024, 768) -eq 0) { $kind = "dots$p"; break }
                }
            }
            $phases[$kind]++
        }
        if (-not $ready) { Start-Sleep -Milliseconds 250 }
    }
    $seen = $phases["dots0"] + $phases["dots1"] + $phases["dots2"] + $phases["dots3"]
    Note ("graphics (report only): screenshots while booting: " + (($phases.Keys | ForEach-Object { "{0} {1}" -f $_, $phases[$_] }) -join ", "))
    Note ("{0,-16} {1,-8} /the splash while booting: a screenshot equal to its reference rendering with 0-3 dots lit (make-wallpaper.py): {2}/" -f "graphics",
          $(if ($ready -and $seen -gt 0) { "OK" } else { "MISSING" }), $seen)
    Start-Sleep -Seconds 1
    # 1.3 stage 8: at System ready the desktop (the default path), once its
    # line is logged; then the screenshot against its reference, the clock's
    # cells masked.
    Wait-Serial "^Desktop: shown on " 20 | Out-Null
    Start-Sleep -Seconds 1
    Shot "graphics-ready"
    $d = Get-MaskedDiffer "graphics-ready" "desktop.rgb"
    Note ("{0,-16} {1,-8} /the desktop at System ready equals its reference rendering (tests/screen_ref.py, the clock's box masked): {2} pixels differ outside the clock's box/" -f "graphics",
          $(if ($ready -and $d[0] -eq 0) { "OK" } else { "MISSING" }), $d[0])
    Key 39 b9; Start-Sleep -Seconds 1      # 1.3 stage 8: opens the Console window (not typed)
    TypeText "display"; Enter
    Start-Sleep -Seconds 1
    Shot "graphics-display"
    TypeText "clear"; Enter
    Start-Sleep -Seconds 1
    Shot "graphics-console"
    $d = Get-MaskedDiffer "graphics-console" "desktop-console.rgb"
    Note ("{0,-16} {1,-8} /the Console window after clear equals its reference rendering (tests/screen_ref.py, the clock's box masked): {2} pixels differ outside the clock's box/" -f "graphics",
          $(if ($d[0] -eq 0) { "OK" } else { "MISSING" }), $d[0])
    Stop-Run "graphics" @("^Display: framebuffer 1024x768x32 at 0x[0-9a-f]+, pitch 4096, RGB at bits 16/8/0$",
        "^Display: console on the framebuffer: 80x25 cells of 8x16 pixels at \(192, 184\), scale 100 %$",
        "^System ready\. Press any key to open the console\.$", "^console: started$",
        "^Desktop: shown on 1024x768x32 \(scale 100 %\): .* \(the default path, 1\.3 stage 8\)$",
        "^Desktop: a key opened the Console window \(the key is not typed\)$",
        "^madire> display$", "^Screen:  framebuffer 1024x768, 32 bits per pixel$",
        "^Scale:   100 % \(console: 80x25 cells of 8x16 pixels\)$", "^console: cleared$")
    Absent "graphics" "PANIC|FAILED|events dropped|Unexpected IRQ"
}
# 1.3 stage 8 (run in stage 9): the desktop group. One normal boot of the
# real ISO (the default path: the desktop at System ready). The steps come
# from tests/screen_ref.py desktop-plan (its VBOX_DESKTOP list, replayed
# through tests/desktop_ref.py's model of the window manager and the menus,
# which the QEMU [1.3] sessions prove against the kernel): relative mouse
# events through COM IMouse.PutMouseEvent (the pointer parked at 0, 0 by
# moves the clamp stops, then moved 1:1; at most 127 per event), the
# Windows key as the scancodes e0 5b / e0 db, other keys and text through
# keyboardputscancode / keyboardputstring. After the run every shot's
# reference is rendered (tests/screen_ref.py desktop-render, the About
# window's memory from this boot's serial log) and each screenshot is
# compared with it, the clock's cells masked; the serial log is checked
# for the default path's lines, the Console window's commands, the
# Restart dialog (Cancel keeps running, Restart restarts) and, by
# tests/pagetables.py, slot 506 with the cache and the window surfaces.
if (Want "desktop") {
    $planLines = @(Py -I (Join-Path $root "tests\screen_ref.py") desktop-plan 1024 768)
    $shots = @($planLines | Where-Object { $_ -match '^SHOT ' } | ForEach-Object { $_.Substring(5) })
    Note ("desktop (report only): the plan: {0} mouse events, {1} key events, {2} texts, {3} shots" -f
          @($planLines | Where-Object { $_ -match '^MOUSE ' }).Count, @($planLines | Where-Object { $_ -match '^KEYS ' }).Count,
          @($planLines | Where-Object { $_ -match '^TYPE ' }).Count, $shots.Count)
    Start-Run
    $ready = Wait-Serial "^Desktop: shown on " 60
    Start-Sleep -Seconds 2
    $client = New-Object -ComObject VirtualBox.VirtualBoxClient
    $session = $client.Session
    $client.VirtualBox.FindMachine($VM).LockMachine($session, 1)    # LockType_Shared: the console only, no setting
    $restarted = $false
    try {
        $mouse = $session.Console.Mouse
        foreach ($p in $planLines) {
            $f = $p -split ' ', 2
            switch ($f[0]) {
                "MOUSE" { $v = @($f[1] -split ' ' | ForEach-Object { [int]$_ }); $mouse.PutMouseEvent($v[0], $v[1], 0, 0, $v[2]); Start-Sleep -Milliseconds 60 }
                "KEYS" { Key @($f[1] -split ' '); Start-Sleep -Milliseconds 400 }
                "TYPE" { TypeText $f[1] }
                "SHOT" { Start-Sleep -Seconds 1; Shot ("desktop-" + $f[1]) }
                "RESTART" {
                    # Restart in the dialog: the reset path; the second boot's desktop.
                    $until = (Get-Date).AddSeconds(90)
                    do {
                        Start-Sleep -Milliseconds 500
                        $n = @(Select-String -Path $serial -Pattern '^Desktop: shown on ' -ErrorAction SilentlyContinue).Count
                    } while ($n -lt 2 -and (Get-Date) -lt $until)
                    $restarted = $n -ge 2
                    Start-Sleep -Seconds 2
                    Shot "desktop-after-restart"
                }
                default { }
            }
        }
    } finally { $session.UnlockMachine() }
    Stop-Run "desktop" @("^Desktop: shown on 1024x768x32 \(scale 100 %\): .* \(the default path, 1\.3 stage 8\)$",
        "^Desktop: the pointer shown from the first mouse movement: 12x18 pixels \(scale 100 %\), first at \(512, 384\)$",
        "^Desktop: a key opened the Console window \(the key is not typed\)$", "^console: started$",
        "^madire> version$", "^Madire OS v$VRe \(64-bit x86-64, Multiboot 1\)$", "^madire> echo ok$",
        "^Desktop: Restart chosen in the Restart dialog: restarting$")
    $log = Join-Path $OutDir "$Prefix-desktop.serial.log"
    if (Test-Path $log) {
        $lines = @(Get-Content $log)
        $chosen = @($lines | Where-Object { $_ -ceq "Desktop: Restart chosen in the Restart dialog: restarting" }).Count
        $boots = @($lines | Where-Object { $_ -ceq "boot32: start" }).Count
        Note ("{0,-16} {1,-8} /Restart then Cancel keeps running, Restart then Restart restarts: 1 restart chosen ({2}), 2 boots ({3}), the second boot's desktop seen/" -f "desktop",
              $(if ($ready -and $restarted -and $chosen -eq 1 -and $boots -eq 2) { "OK" } else { "MISSING" }), $chosen, $boots)
        $typedOk = @($lines | Where-Object { $_ -cmatch '^madire> (echo ok|version|clear)$' }).Count -eq 5 -and
                   @($lines | Where-Object { $_ -cmatch '^madire> ' }).Count -eq 5
        Note ("{0,-16} {1,-8} /every line typed in the Console window as sent: madire> clear (x3), version, echo ok; the opening key and the Windows key not typed/" -f "desktop",
              $(if ($typedOk) { "OK" } else { "MISSING" }))
        # Slot 506 with the cache and the window surfaces (the first boot's lines).
        $first = @(); foreach ($l in $lines) { if ($l -ceq "boot32: start" -and $first.Count) { break }; $first += $l }
        $one = Join-Path $OutDir "$Prefix-desktop-first-boot.serial.log"
        $first | Set-Content -Path $one -Encoding ascii
        $pt = @(Py -I (Join-Path $root "tests\pagetables.py") $one --label vbox-desktop)
        foreach ($l in $pt) { if ($l -match '^PAGETABLES_') { Note "desktop (report only): $l" } }
        $ptOk = @($pt | Where-Object { $_ -cmatch '^PAGETABLES_DESKTOP label=vbox-desktop cache=present computed=([0-9]+/[0-9]+) logged=\1 MATCH$' }).Count -eq 1 -and
                @($pt | Where-Object { $_ -cmatch '^PAGETABLES_SURFACES label=vbox-desktop cache=present surfaces=Console,About computed=([0-9]+/[0-9]+) logged=\1 MATCH$' }).Count -eq 1
        Note ("{0,-16} {1,-8} /slot 506 with the background cache and the window surfaces as tests/pagetables.py models them/" -f "desktop",
              $(if ($ptOk) { "OK" } else { "MISSING" }))
        # The references, now that this boot's usable memory is known.
        $mb = $null
        foreach ($l in $first) { if ($l -cmatch '^Memory: [0-9]+ MB detected, ([0-9]+) MB usable below 4 GiB, ') { $mb = $Matches[1]; break } }
        if ($mb) {
            foreach ($l in (Py -I (Join-Path $root "tests\screen_ref.py") desktop-render 1024 768 $refDir $mb)) { Note "reference (report only): $l" }
            foreach ($s in @($shots + "after-restart")) {
                $ref = if ($s -eq "after-restart") { "desktop-ready.rgb" } else { "desktop-$s.rgb" }
                $d = Get-MaskedDiffer "desktop-$s" $ref
                Note ("{0,-16} {1,-8} /the screen '{2}' equals its reference rendering (tests/screen_ref.py, the clock's box masked): {3} pixels differ outside the clock's box/" -f "desktop",
                      $(if ($d[0] -eq 0) { "OK" } else { "MISSING" }), $s, $d[0])
            }
        } else {
            Note ("{0,-16} {1,-8} /the usable memory line (for the About window's reference)/" -f "desktop", "MISSING")
        }
    }
    Absent "desktop" "PANIC|FAILED|events dropped|bytes dropped|Unexpected IRQ"
}

# 0.9.2: console scrollback. dmesg 60 gives enough lines to page back.
if (Want "scrollback") {
    Start-Run
    Wait-Serial "System ready\." 40 | Out-Null
    Start-Sleep -Seconds 1
    Key 39 b9; Start-Sleep -Seconds 1      # opens the console (not typed)
    TypeText "dmesg 60"; Enter
    Start-Sleep -Seconds 2
    Shot "scroll-bottom"
    Key 2a e0 49 e0 c9 aa; Start-Sleep -Seconds 1      # Shift+PgUp
    Shot "scroll-page1"
    Key 2a e0 49 e0 c9 aa; Start-Sleep -Seconds 1      # Shift+PgUp again
    Shot "scroll-page2"
    Key 1e 9e; Start-Sleep -Seconds 1                  # "a": back to the bottom
    Shot "scroll-back"
    Stop-Run "scrollback" @("^madire> dmesg 60$",
        "^console: scrolled back: line [0-9]+ of [0-9]+, top '",
        "^console: back at the bottom$")
    $log = Join-Path $OutDir "$Prefix-scrollback.serial.log"
    if (Test-Path $log) {
        # The first page shows the 24 lines just above the screen (X = Y - 48);
        # the second goes 24 lines further back, but never past line 1.
        $pages = @(Select-String -Path $log -Pattern '^console: scrolled back: line ([0-9]+) of ([0-9]+)')
        $ok = $false
        if ($pages.Count -eq 2) {
            $x1 = [int]$pages[0].Matches[0].Groups[1].Value
            $y1 = [int]$pages[0].Matches[0].Groups[2].Value
            $x2 = [int]$pages[1].Matches[0].Groups[1].Value
            $ok = $x1 -eq $y1 - 48 -and $x2 -eq [Math]::Max(1, $x1 - 24)
        }
        $desc = ($pages | ForEach-Object { $_.Matches[0].Groups[1].Value + " of " + $_.Matches[0].Groups[2].Value }) -join ", "
        Note ("{0,-16} {1,-8} /two pages back: the first = the 24 lines above the screen, the second 24 further (not past line 1): {2}/" -f "scrollback",
              $(if ($ok) { "OK" } else { "MISSING" }), $desc)
    }
}
# 0.9.1: the shell commands. The guest's "date" is compared with the host's
# clock (local time and UTC) and the difference is only reported: the VM's
# RTC setting is never changed here.
# 0.11: programs from the console (the default boot entry loads them):
# run (output on the screen), run -b, ps, kill asking first (no, then yes),
# Ctrl+C on a spinning and on a printing program.
if (Want "procs") {
    $ctrlC = { Key 1d 2e ae 9d; Start-Sleep -Seconds 2 }
    Start-Run
    Wait-Serial "System ready\." 40 | Out-Null
    Start-Sleep -Seconds 1
    Key 39 b9; Start-Sleep -Seconds 1      # opens the console (not typed)
    TypeText "run hello"; Enter; Start-Sleep -Seconds 2
    Shot "procs-run-hello"
    TypeText "run exitcode"; Enter; Start-Sleep -Seconds 2
    TypeText "run -b spin"; Enter
    Wait-Serial "^\[process [0-9]+ 'spin' started in the background\]$" 10 | Out-Null
    $m = Select-String -Path $serial -Pattern "^\[process ([0-9]+) 'spin' started in the background\]$" |
         Select-Object -First 1
    $bgPid = if ($m) { $m.Matches[0].Groups[1].Value } else { "0" }
    TypeText "ps"; Enter; Start-Sleep -Seconds 1
    Shot "procs-ps"
    TypeText "kill $bgPid"; Enter
    TypeText "no"; Enter; Start-Sleep -Seconds 1
    Shot "procs-kill-no"
    TypeText "kill $bgPid"; Enter
    TypeText "yes"; Enter; Start-Sleep -Seconds 2
    Shot "procs-kill-yes"
    TypeText "clear"; Enter
    TypeText "run spin"; Enter; Start-Sleep -Seconds 2
    & $ctrlC
    Shot "procs-ctrlc-spin"
    TypeText "run lines 65636"; Enter; Start-Sleep -Seconds 2
    & $ctrlC
    Shot "procs-ctrlc-lines"
    TypeText "ps"; Enter; Start-Sleep -Seconds 1
    Shot "procs-ps-after"
    Stop-Run "procs" @("^\[pid [0-9]+\] Hello from user mode$", "^\[process [0-9]+ 'hello' exited, status 0\]$",
        "^\[process [0-9]+ 'exitcode' exited, status 42\]$",
        "^\[process $bgPid 'spin' started in the background\]$", "^ +$bgPid  running +[0-9]+  spin$",
        "^End process $bgPid 'spin'\? Type 'yes' to confirm:$", "^yes/no> no$", "^Kill cancelled\.$",
        "^yes/no> yes$", "^console: kill confirmed for process $bgPid$", "^\[process $bgPid 'spin' killed\]$",
        "^console: Ctrl\+C: kill requested for process [0-9]+$", "^\[process [0-9]+ 'lines' killed\]$",
        "^console: process [0-9]+ collected [0-9]+ ticks after Ctrl\+C$", "^0 processes$")
    Absent "procs" "^key: "
    Absent "procs" "PANIC|Screen output from thread"
}

if (Want "commands") {
    Start-Run
    Wait-Serial "System ready\." 40 | Out-Null
    Start-Sleep -Seconds 1
    Key 39 b9; Start-Sleep -Seconds 1      # opens the console (not typed)
    TypeText "help mem"; Enter
    TypeText "uname -a"; Enter
    TypeText "hostname"; Enter
    $hostLocal = Get-Date
    TypeText "date"; Enter
    TypeText "dmesg 5"; Enter
    TypeText "echo one"; Enter
    Key e0 48 e0 c8; Enter                 # Up recalls "echo one"
    TypeText "history"; Enter
    Start-Sleep -Seconds 1
    Shot "commands"
    TypeText "clear"; Enter
    TypeText "help"; Enter
    Start-Sleep -Seconds 1
    Shot "commands-help"
    TypeText "clear"; Enter
    TypeText "reboot"; Enter
    TypeText "no"; Enter
    Start-Sleep -Seconds 1
    Shot "reboot-cancelled"
    Stop-Run "commands" @("^mem: Show memory information$", "^Usage: mem$",
        "^Madire OS madire $VRe x86_64$", "^madire$",
        "^[0-9]{4}-[0-9]{2}-[0-9]{2} [0-9]{2}:[0-9]{2}:[0-9]{2} \(RTC; time zone not set\)$",
        "^madire> dmesg 5$", "^ +[0-9]+  history$", "^  version  uptime  mem  cpu  uname  hostname  date  dmesg  reboot  display$",
        "^Restart Madire OS now\? Type 'yes' to confirm:$", "^yes/no> no$", "^Restart cancelled\.$")
    $log = Join-Path $OutDir "$Prefix-commands.serial.log"
    if (Test-Path $log) {
        $recalled = @(Select-String -Path $log -Pattern '^madire> echo one$').Count
        Note ("{0,-16} {1,-8} /Up recalled 'echo one': {2} runs (2 expected)/" -f "commands",
              $(if ($recalled -eq 2) { "OK" } else { "MISSING" }), $recalled)
        $d = Select-String -Path $log -Pattern '^([0-9]{4}-[0-9]{2}-[0-9]{2} [0-9]{2}:[0-9]{2}:[0-9]{2}) \(RTC' |
             Select-Object -First 1
        if ($d) {
            $rtc = [datetime]::ParseExact($d.Matches[0].Groups[1].Value, "yyyy-MM-dd HH:mm:ss",
                                          [Globalization.CultureInfo]::InvariantCulture)
            $vsLocal = [int]($rtc - $hostLocal).TotalSeconds
            $vsUtc = [int]($rtc - $hostLocal.ToUniversalTime()).TotalSeconds
            Write-Host ("date (report only): guest RTC {0}; host local {1:yyyy-MM-dd HH:mm:ss}, UTC {2:yyyy-MM-dd HH:mm:ss}; RTC - local = {3} s, RTC - UTC = {4} s" -f
                        $rtc.ToString("yyyy-MM-dd HH:mm:ss"), $hostLocal, $hostLocal.ToUniversalTime(), $vsLocal, $vsUtc)
            $script:results.Add(("date report: RTC - local = {0} s, RTC - UTC = {1} s" -f $vsLocal, $vsUtc))
        }
    }
}
# 0.9.1: "reboot", then "yes": the guest resets the VM through the keyboard
# controller and boots again from the ISO. Reported only: whether VirtualBox
# keeps the COM1 file across the guest's reset (two "boot32: start" lines).
if (Want "reboot") {
    Start-Run
    Wait-Serial "System ready\." 40 | Out-Null
    Start-Sleep -Seconds 1
    Key 39 b9; Start-Sleep -Seconds 1
    TypeText "reboot"; Enter
    TypeText "yes"; Enter
    Start-Sleep -Seconds 3
    Shot "reboot-after-reset"
    Start-Sleep -Seconds 25                 # the BIOS, the hidden boot menu's 2 s (3 s before 1.1), then the second boot (25 s since 0.13: Limine is slower to show)
    Shot "reboot-second-boot"
    $state = (Get-VMInfo)["VMState"]
    Stop-Run "reboot" @("^yes/no> yes$", "^Restarting\.\.\.$", "^console: restart confirmed$",
        "^Restart: keyboard controller reset \(0xfe to port 0x64\)$")
    $log = Join-Path $OutDir "$Prefix-reboot.serial.log"
    if (Test-Path $log) {
        $fallback = @(Select-String -Path $log -Pattern 'did not reset the machine|forcing a triple fault').Count
        Note ("{0,-16} {1,-8} /no fallback to the triple fault: {2} fallback lines/" -f "reboot",
              $(if ($fallback -eq 0) { "OK" } else { "MISSING" }), $fallback)
        $boots = @(Select-String -Path $log -Pattern '^boot32: start$').Count
        $ready = @(Select-String -Path $log -Pattern '^System ready\.').Count
        Write-Host ("reboot (report only): VM state before power-off '{0}'; COM1 log has {1} 'boot32: start' and {2} 'System ready.' lines ({3})" -f
                    $state, $boots, $ready,
                    $(if ($boots -ge 2) { "the log survived the guest reset" } else { "the log did not keep the first boot, or the second boot did not start" }))
        $script:results.Add(("reboot report: VM state '{0}', boot32 lines {1}, System ready lines {2}" -f $state, $boots, $ready))
    }
}
if (Want "preempt") {
    Start-Diag "preempt"
    Wait-Serial "^Preempt test: (PASS|FAIL)$" 40 | Out-Null
    Start-Sleep -Seconds 1
    Shot "preempt"
    Stop-Run "preempt" @("^Preempt test: fair shares: .*: OK$", "^Preempt test: PASS \(")
}
if (Want "threadoverflow") {
    Start-Diag "threadoverflow"
    Wait-Serial $halted 40 | Out-Null
    Start-Sleep -Seconds 1
    Shot "threadoverflow"
    Stop-Run "threadoverflow" @("Double fault: kernel thread stack overflow \(thread [0-9]+ 'overflow'\)", $halted)
}
if (Want "pf") {
    $pf = [ordered]@{
        pfnull = "Address: 0x0000000000000000"; pfunmapped = "Address: 0x0000000040000000";
        pfreadonly = "Address: 0xffffffff80100000"; pfnx = "Details: instruction fetch \(NX violation\)";
        pflow = "Address: 0x0000000000100000"; stackoverflow = "Double fault: kernel stack overflow"
    }
    foreach ($t in $pf.Keys) {
        Start-Diag $t
        Wait-Serial $halted 40 | Out-Null
        Start-Sleep -Seconds 1
        Shot $t
        Stop-Run $t @($pf[$t], $halted)
    }
}
if (Want "diag") {
    Start-Diag "divzero"
    Wait-Serial $halted 40 | Out-Null
    Start-Sleep -Seconds 1
    Shot "divzero"
    Stop-Run "divzero" @("CPU exception 0: Divide Error \(#DE\)", $halted)

    Start-Diag "timer"
    Wait-Serial "^Timer diagnostic: (PASS|FAIL)" 40 | Out-Null    # the result, not "measuring..."
    Start-Sleep -Seconds 1
    Shot "timer"
    Stop-Run "timer" @("^Timer diagnostic: PASS")

    Start-Diag "keyboard"
    Wait-Serial "^Keyboard test: ready" 40 | Out-Null
    TypeText "ab"; Key 2a 1e 9e aa; Key e0 48 e0 c8      # a, b, Shift+A, Up
    Key 56 d6; Key 45 c5; Key 48 c8                      # 1.1: the ISO key, Num Lock off, keypad 8
    Start-Sleep -Seconds 1
    Shot "keyboard"
    Stop-Run "keyboard" @("^key: press .*ascii='a'", "^key: press .*ascii='A' flags=0x03", "^key: press .*key=UP",
        "^key: press sc=0x56 key=CHAR ascii='\\'", "^key: press sc=0x48 key=UP ascii=none flags=0x01")
}
# 0.13: the four Diagnostics entries no other group boots (positions 1, 2, 10
# and 11), so that every one of the 27 positions is booted through the boot
# menu at least once; the crash lines are the QEMU crash tests' own.
if (Want "diagmore") {
    $more = [ordered]@{
        ud = @("CPU exception 6: Invalid Opcode", "Error code: 0x00000000");
        gpf = @("CPU exception 13: General Protection Fault", "Error code: 0x00000028");
        doublefree = @("KERNEL PANIC", "Double free of physical page 0x[0-9a-f]{8}");
        freereserved = @("KERNEL PANIC", "Attempt to free reserved page 0x00100000")
    }
    foreach ($t in $more.Keys) {
        Start-Diag $t
        Wait-Serial $halted 40 | Out-Null
        Start-Sleep -Seconds 1
        Shot $t
        Stop-Run $t @($more[$t][0], $more[$t][1], $halted)
    }
}
# 0.10: the user-mode Diagnostics entries 16-24, each through the boot menu:
# its PASS line, then System ready, and no panic (the same result lines as
# the QEMU boot-menu checks).
if (Want "user") {
    $user = [ordered]@{
        userspin = @("User-mode test: PASS", 60); userkill = @("User kill test: PASS", 90);
        userfault = @("User fault test: PASS", 90); userpriv = @("User privilege test: PASS", 90);
        userkmem = @("User kernel-memory test: PASS", 90); usersys = @("User syscall test: PASS", 90);
        userio = @("User I/O test: PASS", 90); userpreempt = @("User preemption test: PASS", 90);
        userchurn = @("User churn test: PASS", 300)
    }
    foreach ($t in $user.Keys) {
        $pass = [regex]::Escape($user[$t][0])
        Start-Diag $t
        Wait-Serial "^$pass$|FAIL|PANIC|$halted" $user[$t][1] | Out-Null
        Wait-Serial "^System ready\.|$halted" 20 | Out-Null
        Start-Sleep -Seconds 1
        Shot $t
        Stop-Run $t @("^$pass$", "^System ready\.")
        Absent $t "PANIC"
    }
}
# 0.10: smep and smap (entries 25 and 26). The expected result follows the
# CPU as the VM presents it (the boot's "SMEP:"/"SMAP:" line): enabled ->
# the crash screen with #PF and the Cause line; not supported -> NOT
# APPLICABLE, then System ready.
if (Want "smx") {
    $smx = [ordered]@{
        smep = @("SMEP", "0x00000011", "SMEP violation \(the kernel executed a user page\)", "0x0000000000400000");
        smap = @("SMAP", "0x00000001", "SMAP violation \(the kernel read a user page, AC=0\)", "0x0000000000500000")
    }
    foreach ($t in $smx.Keys) {
        $Feat, $err, $cause, $addr = $smx[$t]      # not $T: PowerShell names ignore case
        Start-Diag $t
        Wait-Serial "^$Feat test: NOT APPLICABLE|$halted" 60 | Out-Null
        Wait-Serial "^System ready\.|$halted" 20 | Out-Null
        Start-Sleep -Seconds 1
        Shot $t
        $on = (Test-Path $serial) -and (Select-String -Path $serial -Pattern "^${Feat}: enabled" -Quiet)
        Write-Host "${t} (report only): the VM's CPU $(if ($on) { "has $Feat (enabled)" } else { "does not report $Feat" })"
        $script:results.Add("$t report: $Feat $(if ($on) { 'enabled' } else { 'not supported' })")
        if ($on) {
            Stop-Run $t @("^$Feat test: user page $addr ", "CPU exception 14: Page Fault \(#PF\)", "Error code: $err",
                          "^ Cause: $cause$", "^ Address: $addr$", $halted)
            Absent $t "System ready|without a fault|NX violation"
        } else {
            Stop-Run $t @("^$Feat test: NOT APPLICABLE \(not supported by this CPU\)$", "^System ready\.")
            Absent $t "PANIC"
        }
    }
}
if (Want "memexhaust") {
    Start-Diag "memexhaust"
    Wait-Serial "^System ready\." 300 | Out-Null
    Start-Sleep -Seconds 2
    Shot "memexhaust"
    Key 39 b9; Start-Sleep -Seconds 1
    TypeText "mem"; Enter
    Start-Sleep -Seconds 2
    Shot "memexhaust-mem"
    Stop-Run "memexhaust" @("^Memory exhaustion test: PASS", "^madire> mem$")
}
if (Want "module") {
    Attach $ModuleIso
    Start-Run
    Wait-Serial "^System ready\." 40 | Out-Null
    Start-Sleep -Seconds 1
    Shot "module"
    Stop-Run "module" @("^Boot module 1: .*test pattern intact", "^Boot module 1: test pattern intact \(14016 bytes\)$")
    Attach $Iso
}

# 0.12 (stage 4): every 0.12 shell command and its error paths, typed
# through keyboardputscancode (set 1 scancodes, US layout) in short bursts
# with pauses, as in the QEMU [0.12] sessions. Each command's line and its
# expected output lines are checked IN ORDER in the serial log (a pattern
# must match after the previous one). The boot calibration is recorded
# (report only), and the uptime heartbeat must stay out of dmesg.
$Scan = New-Object 'System.Collections.Generic.Dictionary[char,int[]]'
foreach ($row in @(@('1234567890-=', '!@#$%^&*()_+', 0x02), @('qwertyuiop[]', 'QWERTYUIOP{}', 0x10),
                   @("asdfghjkl;'``", 'ASDFGHJKL:"~', 0x1e), @('zxcvbnm,./', 'ZXCVBNM<>?', 0x2c))) {
    for ($i = 0; $i -lt $row[0].Length; $i++) {
        $Scan[$row[0][$i]] = @(($row[2] + $i), 0)
        $Scan[$row[1][$i]] = @(($row[2] + $i), 1)
    }
}
$Scan[[char]'\'] = @(0x2b, 0); $Scan[[char]'|'] = @(0x2b, 1); $Scan[[char]' '] = @(0x39, 0)
function ScanCodes([string]$s) {
    $codes = New-Object System.Collections.Generic.List[string]
    foreach ($c in $s.ToCharArray()) {
        if (-not $Scan.ContainsKey($c)) { throw "no scancode for '$c'" }
        $k = $Scan[$c]
        if ($k[1]) { $codes.Add("2a") }
        $codes.Add("{0:x2}" -f $k[0]); $codes.Add("{0:x2}" -f ($k[0] -bor 0x80))
        if ($k[1]) { $codes.Add("aa") }
    }
    return $codes.ToArray()
}
# Types `s` in bursts of `chunk` characters, `pauseMs` apart.
function TypeScan([string]$s, [int]$chunk = 8, [int]$pauseMs = 80) {
    for ($p = 0; $p -lt $s.Length; $p += $chunk) {
        $a = @('controlvm', $VM, 'keyboardputscancode') + (ScanCodes $s.Substring($p, [Math]::Min($chunk, $s.Length - $p)))
        VBox @a | Out-Null
        Start-Sleep -Milliseconds $pauseMs
    }
}
$shellSeq = New-Object System.Collections.Generic.List[string]
$shellPrompt = "madire> "
function Expect([string[]]$p) { foreach ($x in $p) { $script:shellSeq.Add($x) } }
# Types a command and Enter; expects its logged line, then `out` in order.
function Cmd([string]$c, [string[]]$out = @()) {
    TypeScan $c; Enter
    Expect (@("^" + [regex]::Escape($script:shellPrompt + $c) + "$") + $out)
}
# Checks the patterns in order in a kept serial log, one row each.
function InOrder([string]$name, $patterns) {
    $log = Join-Path $OutDir "$Prefix-$name.serial.log"
    if (-not (Test-Path $log)) { return }
    $lines = @(Get-Content -LiteralPath $log)
    $pos = 0
    foreach ($p in $patterns) {
        $hit = -1
        for ($i = $pos; $i -lt $lines.Count; $i++) { if ([regex]::IsMatch($lines[$i], $p)) { $hit = $i; break } }
        if ($hit -ge 0) { $pos = $hit + 1 }
        Note ("{0,-16} {1,-8} /{2}/" -f $name, $(if ($hit -ge 0) { "OK" } else { "MISSING" }), $p)
    }
}
# Waits until `pattern` has matched more than `before` lines; returns the
# last match's first group (a process ID), or "0".
function WaitPid([string]$pattern, [int]$before, [int]$seconds) {
    for ($i = 0; $i -lt $seconds * 2; $i++) {
        $m = @(Select-String -Path $serial -Pattern $pattern -CaseSensitive)
        if ($m.Count -gt $before) { return $m[-1].Matches[0].Groups[1].Value }
        Start-Sleep -Milliseconds 500
    }
    return "0"
}
function CountLines([string]$pattern) { return @(Select-String -Path $serial -Pattern $pattern -CaseSensitive).Count }

if (Want "shell") {
    $ctrlC = { Key 1d 2e ae 9d; Start-Sleep -Seconds 1 }
    $TL = "^time: [0-9]+ ticks? \([0-9]+\.[0-9]{2} s\)$"
    $NOSUCH = "^Unknown command: {0}\. Type 'help' for a list of commands\.$"
    Start-Run
    Wait-Serial "System ready\." 40 | Out-Null
    Start-Sleep -Seconds 1
    Shot "shell-boot"
    Key 39 b9; Start-Sleep -Seconds 1      # opens the console (not typed)
    Cmd "dmesg 40"; Start-Sleep -Seconds 1
    Key 2a e0 49 e0 c9 aa; Start-Sleep -Seconds 1      # Shift+PgUp: the calibration line is above
    Shot "shell-calibration"                           # the next key returns to the bottom

    # help, its sections, "Did you mean"
    Cmd "clear"
    Cmd "help" @("^  sleep  presskey  time$", "^Purposes and usage: help <command>; one section: help <section>\.$",
                 "^Scroll back: Shift\+PgUp and Shift\+PgDn\.$")
    Start-Sleep -Seconds 1; Shot "shell-help"
    Cmd "clear"
    Cmd "help shell" @("^shell: The shell's own commands$", "^  help     Show this list$", "^  time     Time a command: time <command>$")
    Cmd "help system" @("^system: Information about the system$", "^  version  Show the Madire OS version$",
                        "^  reboot   Restart the computer \(asks first\)$", "^  display  Show the screen's mode and scale$")
    Cmd "help programs" @("^programs: Programs and processes$", "^  ps       Show threads and processes$",
                          "^  run      Run a program: run \[-b\] <name> \[args\]$", "^  kill     End a process: kill <pid> \(asks first\)$")
    Start-Sleep -Seconds 1; Shot "shell-help-sections"
    Cmd "clear"
    Cmd "help shel" @("^No help for 'shel': unknown command\.$", "^Did you mean: shell, help, set\?$")
    Cmd "help nosuch" @("^No help for 'nosuch': unknown command\.$", "^Did you mean: touch\?$")
    Cmd "hlep" @(($NOSUCH -f "hlep"), "^Did you mean: help, sleep\?$")
    Cmd "ru" @(($NOSUCH -f "ru"), "^Did you mean: du, rm, run\?$")
    Cmd "Echo x" @(($NOSUCH -f "Echo"), "^Did you mean: echo\?$")
    Cmd "kil 5" @(($NOSUCH -f "kil"), "^Did you mean: kill\?$")
    Start-Sleep -Seconds 1; Shot "shell-didyoumean"

    # prompt
    Cmd "clear"
    Cmd "prompt" @("^Prompt: 'madire> ' \(the default\)$")
    Cmd "prompt abc>" @("^Prompt set to 'abc>    '\.$")
    $shellPrompt = "abc>    "
    Cmd "echo under-new-prompt" @("^under-new-prompt$")
    Cmd "prompt" @("^Prompt: 'abc>    '$")
    Cmd "prompt 123456789" @("^prompt: at most 8 characters \(this text has 9\)\.$")
    Cmd "prompt -x" @("^prompt: the text may not start with '-' \(to reset: prompt --reset\)\.$")
    Cmd "prompt a b" @("^Usage: prompt \[text \| --reset\]$")
    Start-Sleep -Seconds 1; Shot "shell-prompt"
    Cmd "prompt --reset" @("^Prompt reset to 'madire> '\.$")
    $shellPrompt = "madire> "
    Cmd "prompt" @("^Prompt: 'madire> ' \(the default\)$")

    # alias, unalias
    Cmd "clear"
    Cmd "alias" @("^0 aliases \(at most 16\)$")
    Cmd "alias ll echo hi there" @("^Alias 'll' added\.$")
    Cmd "ll" @("^hi there$")
    Cmd "ll more" @("^hi there more$")
    Cmd "alias ll echo replaced" @("^Alias 'll' replaced\.$")
    Cmd "ll" @("^replaced$")
    Cmd "alias echo echo x" @("^alias: 'echo' is a built-in command\.$")
    Cmd "alias zz ll" @("^alias: 'll' is not a built-in command\.$")
    Cmd "alias abcdefghijklmnop echo x" @("^alias: a name has at most 15 characters \(this one has 16\)\.$")
    Cmd "alias Bad echo x" @("^alias: a name uses only a-z, 0-9 and _\.$")
    Cmd "unalias nosuch" @("^unalias: no alias named 'nosuch'\.$")
    Cmd "unalias" @("^Usage: unalias <name>$")
    Cmd "alias e5 echo 1 2 3 4 5" @("^Alias 'e5' added\.$")
    Cmd "e5 6 7" @("^1 2 3 4 5 6 7$")
    Cmd "e5 6 7 8" @("^Too many arguments \(max 8\)\.$")
    Cmd "lll" @(($NOSUCH -f "lll"), "^Did you mean: ll, kill, ls\?$")
    Cmd "alias" @("^e5 +echo 1 2 3 4 5$", "^ll +echo replaced$", "^2 aliases \(at most 16\)$")
    Start-Sleep -Seconds 1; Shot "shell-alias"
    Cmd "unalias ll" @("^Alias 'll' removed\.$")
    Cmd "alias" @("^e5 +echo 1 2 3 4 5$", "^1 alias \(at most 16\)$")
    Cmd "clear"
    for ($n = 2; $n -le 16; $n++) { Cmd ("alias t{0:D2} echo {0:D2}" -f $n) @("^Alias 't{0:D2}' added\.$" -f $n) }
    Cmd "alias t17 echo 17" @("^alias: the table is full \(16 aliases\); remove one with unalias\.$")
    Cmd "unalias t16" @("^Alias 't16' removed\.$")
    Cmd "t15" @("^15$")

    # set, unset, export, env
    Cmd "clear"
    Cmd "set" @("^No variables\.$")
    Cmd "env" @("^No exported variables\.$")
    Cmd "set A=1"
    Cmd "set A" @("^A=1$")
    Cmd "set B=b=c"
    Cmd "set E="
    Cmd "set W=x   y  z"
    Cmd "set W" @("^W=x y z$")
    Cmd "set a=small"
    Cmd "set _u=under"
    Cmd "set" @("^A=1$", "^B=b=c$", "^E=$", "^W=x y z$", "^_u=under$", "^a=small$")
    Cmd "set nosuch" @("^set: nosuch is not set$")
    Cmd "set A b" @("^Usage: set \[NAME\[=value\]\] \(names are case-sensitive\)$")
    Cmd "set 1abc=v" @("^set: invalid name: 1abc$")
    Cmd "set =v" @("^set: invalid name: \(empty\)$")
    Cmd "set abcdefghijklmnopq=v" @("^set: invalid name: abcdefghijklmnopq$")
    Cmd ("set V=" + ("v" * 64)) @("^set: value longer than 63 characters$")
    Start-Sleep -Seconds 1; Shot "shell-set"
    Cmd "clear"
    Cmd "export A"
    Cmd "export a"
    Cmd "env" @("^A=1$", "^a=small$")
    Cmd "set A=2"
    Cmd "env" @("^A=2$", "^a=small$")
    Cmd "unset a"
    Cmd "env" @("^A=2$")
    Cmd "unset nosuch" @("^unset: nosuch is not set$")
    Cmd "unset" @("^Usage: unset <NAME>$")
    Cmd "export nosuch" @("^export: nosuch is not set$")
    Cmd "export B=2" @("^export: NAME=value is not supported; use set NAME=value, then export NAME\.$")
    Cmd "export" @("^Usage: export <NAME>$")
    Cmd "env x" @("^Usage: env$")
    Start-Sleep -Seconds 1; Shot "shell-env"
    Cmd "clear"
    for ($n = 0; $n -le 26; $n++) { Cmd ("set V{0:D2}={0}" -f $n) }     # 5 + 27 = 32 variables
    Cmd "set V27=27" @("^set: no room \(32 variables\)$")

    # sleep, presskey, time (keys typed during a wait are ignored: pauses)
    Cmd "clear"
    Cmd "sleep 1" @("^console: sleep 1 s$", "^console: sleep done after [0-9]+ ticks$"); Start-Sleep -Seconds 2
    Cmd "sleep 0" @("^sleep: give whole seconds from 1 to 3600$")
    Cmd "sleep 3601" @("^sleep: give whole seconds from 1 to 3600$")
    Cmd "sleep x" @("^sleep: give whole seconds from 1 to 3600$")
    Cmd "sleep" @("^Usage: sleep <seconds>$")
    Cmd "sleep 3" @("^console: sleep 3 s$"); Start-Sleep -Seconds 1
    & $ctrlC
    Expect @("^console: Ctrl\+C: sleep cancelled after [0-9]+ ticks$", "^sleep: cancelled\.$")
    Cmd "sleep 3" @("^console: sleep 3 s$", "^console: sleep done after [0-9]+ ticks$"); Start-Sleep -Seconds 4
    Cmd "time sleep 1" @("^console: sleep 1 s$", "^console: sleep done after [0-9]+ ticks$", $TL); Start-Sleep -Seconds 2
    Cmd "presskey" @("^Press any key to continue\.\.\.$", "^console: presskey: waiting for a key$"); Start-Sleep -Seconds 1
    Key 10 90; Start-Sleep -Seconds 1        # "q": ends the wait, not typed
    Expect @("^console: presskey: key pressed$")
    Cmd "echo after-presskey" @("^after-presskey$")
    Cmd "presskey" @("^Press any key to continue\.\.\.$"); Start-Sleep -Seconds 1
    & $ctrlC
    Expect @("^console: Ctrl\+C: presskey cancelled$", "^presskey: cancelled\.$")
    Cmd "presskey x" @("^Usage: presskey$")
    Cmd "time echo hi" @("^hi$", $TL)
    Cmd "time time echo x" @("^time: cannot time time$")
    Cmd "time nosuch" @("^time: 'nosuch' is not a built-in command\.$")
    Cmd "alias ll echo hi" @("^Alias 'll' added\.$")
    Cmd "time ll" @("^time: 'll' is an alias; time runs built-in commands only\.$")
    Cmd "time" @("^Usage: time <command\.\.\.>$")
    Start-Sleep -Seconds 1; Shot "shell-wait"

    # run, ps, kill, kill -y, Ctrl+C
    Cmd "clear"
    Cmd "run hello" @("^\[process [0-9]+ 'hello' exited, status 0\]$"); Start-Sleep -Seconds 2
    Shot "shell-run-hello"
    $bgPat = "^\[process ([0-9]+) 'spin' started in the background\]$"
    $before = CountLines $bgPat
    Cmd "run -b spin"
    $p1 = WaitPid $bgPat $before 10
    Expect @("^\[process $p1 'spin' started in the background\]$")
    Cmd "ps" @("^ +$p1  running +[0-9]+  spin$")
    Cmd "kill $p1" @("^End process $p1 'spin'\? Type 'yes' to confirm:$")
    TypeScan "no"; Enter
    Expect @("^yes/no> no$", "^Kill cancelled\.$")
    Cmd "kill $p1" @("^End process $p1 'spin'\? Type 'yes' to confirm:$")
    TypeScan "yes"; Enter; Start-Sleep -Seconds 1
    Expect @("^yes/no> yes$", "^console: kill confirmed for process $p1$", "^\[process $p1 'spin' killed\]$")
    $before = CountLines $bgPat
    Cmd "run -b spin"
    $p2 = WaitPid $bgPat $before 10
    Cmd "kill -y $p2" @("^console: kill -y for process $p2$", "^\[process $p2 'spin' killed\]$"); Start-Sleep -Seconds 1
    Cmd "kill -y 0" @("^kill: PID 0 is the kernel, not a process\.$")
    Cmd "kill -y 99" @("^kill: no process with PID 99\.$")
    Cmd "kill -y $p2" @("^kill: no process with PID $p2\.$")
    Cmd "kill -y x" @("^kill: 'x' is not a process ID\.$")
    Cmd "kill -y" @("^Usage: kill \[-y\] <pid>$")
    $before = CountLines $bgPat
    Cmd "run -b spin"
    $p3 = WaitPid $bgPat $before 10
    Cmd "time kill -y $p3" @("^console: kill -y for process $p3$", "^\[process $p3 'spin' killed\]$", $TL); Start-Sleep -Seconds 1
    Start-Sleep -Seconds 1; Shot "shell-kill"
    Cmd "clear"
    Cmd "run spin"; Start-Sleep -Seconds 2
    & $ctrlC
    Expect @("^console: Ctrl\+C: kill requested for process [0-9]+$", "^\[process [0-9]+ 'spin' killed\]$")
    Start-Sleep -Seconds 1; Shot "shell-ctrlc-spin"
    Cmd "ps" @("^0 processes$")

    # Typing test: 451 characters in bursts; Shift must not stick (the
    # lowercase lines after the Shift-heavy ones come back exactly).
    Cmd "clear"
    foreach ($t in @("The-quick-brown-fox-jumps-over-the-lazy-dog. 0123456789 Done1",
                     "PACK-MY-BOX-WITH-FIVE-DOZEN-LIQUOR-JUGS! ABCDEFGHIJKLM Done2",
                     "MiXeD:CaSe_TeSt+aBcDeF=GhIjKl(MnOpQr)?StUvWx@YZ%123 Done3",
                     "sphinx-of-black-quartz,judge-my-vow;how-vexingly-quick Done4",
                     "!@#%^&*()_+-=:;,.?~ ZYXWVUTSRQPONMLKJIHGFEDCBA 9876543210 Done5",
                     "all-lowercase-now:abcdefghijklmnopqrstuvwxyz-0123456789 Done6",
                     "shift-is-not-stuck-if-this-line-is-lowercase.end Done7")) {
        Cmd "echo $t" @("^" + [regex]::Escape($t) + "$")
    }
    Start-Sleep -Seconds 1; Shot "shell-typing"

    # The uptime heartbeat: serial only, never in dmesg (option B).
    Cmd "clear"
    Cmd "echo before-idle" @("^before-idle$")
    Start-Sleep -Seconds 15
    Cmd "dmesg 5"
    Start-Sleep -Seconds 1; Shot "shell-dmesg-idle"

    # 1.1 (stage 9): the console keys. Ctrl+C and Esc clear the line (the
    # cleared text is never logged), Tab completes a command name, Caps Lock
    # types exactly, and Ctrl+C at a yes/no question answers no.
    TypeScan "echo secretvbox"; & $ctrlC
    Expect @("^console: Ctrl\+C: line cleared$")
    TypeScan "echo escvbox"; Key 01 81                   # Esc
    Cmd "echo after-clear" @("^after-clear$")
    TypeScan "his"; Key 0f 8f; Enter                     # Tab
    Expect @("^madire> history $")
    TypeScan "echo "; Key 3a ba; TypeScan "q"; Key 3a ba; TypeScan "r"; Enter   # Caps Lock around q
    Expect @("^madire> echo Qr$", "^Qr$")
    Cmd "reboot"; TypeScan "ye"; & $ctrlC
    Expect @("^console: Ctrl\+C: answered no$", "^Restart cancelled\.$")
    Cmd "echo end-of-shell-test" @("^end-of-shell-test$")
    Wait-Serial "^end-of-shell-test$" 30 | Out-Null
    Stop-Run "shell" @("^System ready\.")
    InOrder "shell" $shellSeq
    $log = Join-Path $OutDir "$Prefix-shell.serial.log"
    if (Test-Path $log) {
        $lines = @(Get-Content -LiteralPath $log)
        $cal = Select-String -Path $log -Pattern '^Diagnostic waits: calibrated: at most ([0-9]+) polls per tick' -CaseSensitive |
               Select-Object -First 1
        Note ("{0,-16} {1,-8} /boot calibration logged (report only): {2} polls per tick/" -f "shell",
              $(if ($cal) { "OK" } else { "MISSING" }), $(if ($cal) { $cal.Matches[0].Groups[1].Value } else { "none" }))
        $script:results.Add(("calibration report: {0}" -f $(if ($cal) { $cal.Line } else { "none" })))
        # dmesg 5's 5 lines follow the first "madire> dmesg 5" line.
        $at = -1
        for ($i = 0; $i -lt $lines.Count; $i++) { if ($lines[$i] -ceq "madire> dmesg 5") { $at = $i; break } }
        $dm = if ($at -ge 0) { @($lines[($at + 1)..([Math]::Min($at + 5, $lines.Count - 1))]) } else { @() }
        $ok = $dm.Count -eq 5 -and -not ($dm | Where-Object { $_ -cmatch '^uptime: ' }) -and
              $dm[3] -ceq "madire> echo before-idle" -and $dm[4] -ceq "madire> dmesg 5"
        Note ("{0,-16} {1,-8} /dmesg 5 after 15 s idle: no uptime line, ends with the command before the pause and dmesg 5/" -f
              "shell", $(if ($ok) { "OK" } else { "MISSING" }))
        $hb = 0; $in = $false
        foreach ($l in $lines) {
            if ($l -ceq "madire> echo before-idle") { $in = $true; continue }
            if ($in -and $l -ceq "madire> dmesg 5") { break }
            if ($in -and $l -cmatch '^uptime: [0-9]+ s$') { $hb++ }
        }
        Note ("{0,-16} {1,-8} /the heartbeat is still in the serial log during the idle period: {2} lines/" -f "shell",
              $(if ($hb -ge 2) { "OK" } else { "MISSING" }), $hb)
        $t = Select-String -Path $log -Pattern '^time: ([0-9]+) ticks? \(' -CaseSensitive | ForEach-Object { $_.Matches[0].Groups[1].Value }
        $script:results.Add(("time report (ticks): {0}" -f ($t -join ", ")))
    }
    Absent "shell" "^key: "
    Absent "shell" "PANIC|Screen output from thread|events dropped|Unexpected IRQ"
    Absent "shell" "secretvbox|escvbox"
}
# 1.0 (stage 11): the file commands, typed as in the shell group, with the
# same expected lines as the QEMU [1.0] file sessions: the Live label, pwd,
# ls, mkdir, cd, redirection (> and >>, '>' inside a word is text), stat,
# cp, mv and rm asking first (no, then yes) and with -y, their errors, df
# and du. The files are made by redirection and cp (the ISO has no fsmake).
# Keys typed while cat, cp or a redirected command runs are not typed, so
# the session waits for each one's end line in the serial log.
# WaitMore <pattern> <before> <seconds>: until `pattern` has matched more
# than `before` lines (or the time passes).
function WaitMore([string]$pattern, [int]$before, [int]$seconds) {
    for ($i = 0; $i -lt $seconds * 4; $i++) {
        if ((CountLines $pattern) -gt $before) { break }
        Start-Sleep -Milliseconds 250
    }
    Start-Sleep -Milliseconds 300
}
# CmdWait: Cmd, then waits for one more line matching `done`.
function CmdWait([string]$c, [string[]]$out, [string]$done) {
    $before = CountLines $done
    Cmd $c $out
    WaitMore $done $before 15
}
# Answer: waits for the question, then types the answer and Enter.
function Answer([string]$question, [int]$before, [string]$answer) {
    WaitMore $question $before 10
    TypeScan $answer; Enter
}

if (Want "files") {
    $shellSeq = New-Object System.Collections.Generic.List[string]
    $shellPrompt = "madire> "
    $D = "[0-9]{4}-[0-9]{2}-[0-9]{2} [0-9]{2}:[0-9]{2}"
    $CAT = "^console: cat: [0-9]+ bytes in "
    $CP = "^console: cp: [0-9]+ bytes in [0-9]+ chunks, "
    $OUTL = "^console: output to '"
    $QUEST = "^(Replace|Remove) file '.*'\? Type 'yes' to confirm:$"
    Start-Run
    Wait-Serial "System ready\." 40 | Out-Null
    Start-Sleep -Seconds 1
    Key 39 b9; Start-Sleep -Seconds 1      # opens the console (not typed)
    Expect @("^Filesystem: live RAM filesystem, 1024 nodes, 2 MiB per file, budget 8192 pages$",
             "^Filesystem self-test: PASS$", "^System ready\.", "^Live mode: files are kept in memory only\.$",
             "^Nothing is saved: all files are lost when the computer restarts\.$")
    Cmd "version" @("^Madire OS v$VRe \(64-bit x86-64, Multiboot 1\)$", "^Live mode \(files in memory only\)$")
    Cmd "pwd" @("^/home$")
    Cmd "help files" @("^files: Files and folders$", "^  pwd      Show the current folder$",
        "^  cd       Change the current folder: cd \[path\]$", "^  ls       List a folder: ls \[-l\] \[path\]$",
        "^  mkdir    Create a folder: mkdir <path>$", "^  rmdir    Remove an empty folder: rmdir <path>$",
        "^  touch    Create an empty file, or update its time$",
        "^  stat     Show a file's or folder's details: stat <path>$",
        "^  cat      Show a file's contents: cat <path>$",
        "^  rm       Remove a file: rm \[-y\] <path> \(asks first\)$", "^  cp       Copy a file: cp \[-y\] <from> <to>$",
        "^  mv       Move or rename: mv \[-y\] <from> <to>$", "^  df       Show the filesystem's nodes and pages in use$",
        "^  du       Show the pages under a folder: du \[path\]$")
    Start-Sleep -Seconds 1; Shot "files-help"

    # folders, redirection, stat
    Cmd "clear"
    Cmd "ls /" @("^home/$", "^tmp/$")
    Cmd "ls -l /" @("^d       0 $D home/$", "^d       0 $D tmp/$")
    Cmd "mkdir docs"
    Cmd "mkdir docs" @("^mkdir: 'docs': already exists\.$")
    Cmd "cd docs"
    Cmd "pwd" @("^/home/docs$")
    CmdWait "echo hello > a.txt" @("^console: output to 'a\.txt': 6 bytes in 1 writes, largest 6$") $OUTL
    CmdWait "cat a.txt" @("^hello$") $CAT
    CmdWait "echo world >> a.txt" @("^console: output to 'a\.txt': 6 bytes in 1 writes, largest 6$") $OUTL
    CmdWait "cat a.txt" @("^hello$", "^world$") $CAT
    Cmd "echo hi>b.txt" @("^hi>b\.txt$")
    Cmd "stat b.txt" @("^stat: 'b\.txt': no such file or folder\.$")
    Cmd "touch t.txt"
    Cmd "ls" @("^a\.txt$", "^t\.txt$")
    Cmd "stat a.txt" @("^File:   a\.txt$", "^Type:   file$", "^Size:   12 bytes$",
        "^Pages:  2 \(4 KiB each, the index page included\)$", "^Node:   [1-9][0-9]*$", "^Links:  1$",
        "^Mode:   0644$", "^Access: ${D}:[0-9]{2}$", "^Modify: ${D}:[0-9]{2}$", "^Change: ${D}:[0-9]{2}$")
    Start-Sleep -Seconds 1; Shot "files-stat"

    # cp: a file of several chunks, asking first (no, then yes), -y, errors
    Cmd "clear"
    CmdWait "dmesg 9999 > log.txt" @("^console: output to 'log\.txt': [0-9]+ bytes in [0-9]+ writes, largest [0-9]+$") $OUTL
    Cmd "stat log.txt" @("^Size:   [0-9]+ bytes$")
    CmdWait "cp log.txt copy.txt" @("^console: cp: [0-9]+ bytes in [0-9]+ chunks, [0-9]+ ticks$") $CP
    $q = CountLines $QUEST
    Cmd "cp log.txt copy.txt" @("^Replace file 'copy\.txt'\? Type 'yes' to confirm:$")
    Answer $QUEST $q "no"
    Expect @("^yes/no> no$", "^Copy cancelled\.$")
    $q = CountLines $QUEST
    $c = CountLines $CP
    Cmd "cp a.txt copy.txt" @("^Replace file 'copy\.txt'\? Type 'yes' to confirm:$")
    Answer $QUEST $q "yes"
    WaitMore $CP $c 15
    Expect @("^yes/no> yes$", "^console: cp: 12 bytes in 1 chunks, [0-9]+ ticks$")
    Cmd "stat copy.txt" @("^Size:   12 bytes$")
    CmdWait "cp -y log.txt copy.txt" @("^console: cp: [0-9]+ bytes in [0-9]+ chunks, [0-9]+ ticks$") $CP
    Cmd "cp nosuch x" @("^cp: 'nosuch': no such file or folder\.$")
    Start-Sleep -Seconds 1; Shot "files-cp"

    # mv and rm, asking first (no, then yes), and -y
    Cmd "clear"
    Cmd "mv copy.txt moved.txt"
    Cmd "stat copy.txt" @("^stat: 'copy\.txt': no such file or folder\.$")
    CmdWait "echo other > o.txt" @("^console: output to 'o\.txt': 6 bytes in 1 writes, largest 6$") $OUTL
    $q = CountLines $QUEST
    Cmd "mv o.txt moved.txt" @("^Replace file 'moved\.txt'\? Type 'yes' to confirm:$")
    Answer $QUEST $q "no"
    Expect @("^yes/no> no$", "^Move cancelled\.$")
    Cmd "mv -y o.txt moved.txt"
    CmdWait "cat moved.txt" @("^other$") $CAT
    $q = CountLines $QUEST
    Cmd "rm a.txt" @("^Remove file 'a\.txt'\? Type 'yes' to confirm:$")
    Answer $QUEST $q "no"
    Expect @("^yes/no> no$", "^Remove cancelled\.$")
    $q = CountLines $QUEST
    Cmd "rm a.txt" @("^Remove file 'a\.txt'\? Type 'yes' to confirm:$")
    Answer $QUEST $q "yes"
    Expect @("^yes/no> yes$")
    Cmd "stat a.txt" @("^stat: 'a\.txt': no such file or folder\.$")
    Cmd "rm -y t.txt"
    Start-Sleep -Seconds 1; Shot "files-rm"

    # errors, df and du
    Cmd "clear"
    Cmd "cd .."
    Cmd "rm docs" @("^rm: 'docs': is a folder \(rmdir removes folders\)\.$")
    Cmd "rmdir docs" @("^rmdir: 'docs': the folder is not empty\.$")
    Cmd "echo a >" @("^Redirection: '>' needs a file name after it\.$")
    Cmd "nosuch > n.txt" @(("^Unknown command: nosuch\. Type 'help' for a list of commands\.$"), "^Did you mean: touch\?$")
    Cmd "stat n.txt" @("^stat: 'n\.txt': no such file or folder\.$")
    Cmd "df" @("^Filesystem: live \(RAM\), mounted on / \(files in memory only\)$",
        "^Nodes:      [0-9]+ used of 1024 \([0-9]+ free\)$", "^Pages:      [0-9]+ used of 8192 \([0-9]+ free\), 4 KiB each$",
        "^Space:      [0-9]+ KiB used of 32768 KiB \([0-9]+ KiB free\)$")
    Cmd "du /" @("^[0-9]+ pages? \([0-9]+ KiB\) in [0-9]+ files? and [0-9]+ folders?$")
    Start-Sleep -Seconds 1; Shot "files-df"
    Cmd "echo end-of-files-test" @("^end-of-files-test$")
    Wait-Serial "^end-of-files-test$" 30 | Out-Null
    Stop-Run "files" @("^System ready\.")
    InOrder "files" $shellSeq
    $log = Join-Path $OutDir "$Prefix-files.serial.log"
    if (Test-Path $log) {
        $lines = @(Get-Content -LiteralPath $log)
        # The lines after a typed line's first occurrence, up to the next prompt.
        $after = {
            param([string]$typed)
            $at = -1
            for ($i = 0; $i -lt $lines.Count; $i++) { if ($lines[$i] -ceq "madire> $typed") { $at = $i; break } }
            if ($at -lt 0) { return @() }
            $r = @()
            for ($i = $at + 1; $i -lt $lines.Count -and -not ($lines[$i] -cmatch '^(madire|yes/no)> '); $i++) { $r += $lines[$i] }
            return $r
        }
        $o = @(& $after "dmesg 9999 > log.txt" | Where-Object { $_ -cmatch "^console: output to 'log\.txt': ([0-9]+) bytes in ([0-9]+) writes, largest ([0-9]+)$" })
        $sz = @(& $after "stat log.txt" | Where-Object { $_ -cmatch '^Size:   ([0-9]+) bytes$' })
        $ok = $false; $desc = "no output or stat line"
        if ($o.Count -eq 1 -and $sz.Count -eq 1) {
            $m = [regex]::Match($o[0], "([0-9]+) bytes in ([0-9]+) writes, largest ([0-9]+)$")
            $n = [long]$m.Groups[1].Value; $k = [long]$m.Groups[2].Value; $big = [long]$m.Groups[3].Value
            $s = [long]([regex]::Match($sz[0], '([0-9]+) bytes$').Groups[1].Value)
            $ok = $n -gt 4096 -and $big -le 4096 -and $k -ge [Math]::Ceiling($n / 4096) -and $s -eq $n
            $desc = "$n bytes in $k writes, largest $big; stat $s bytes"
        }
        Note ("{0,-16} {1,-8} /dmesg 9999 > log.txt: more than 4096 bytes, pieces of at most 4096, stat shows the logged size: {2}/" -f
              "files", $(if ($ok) { "OK" } else { "MISSING" }), $desc)
        $pu = @(& $after "df" | ForEach-Object { if ($_ -cmatch '^Pages:      ([0-9]+) used of ') { $Matches[1] } })
        $du = @(& $after "du /" | ForEach-Object { if ($_ -cmatch '^([0-9]+) pages? \(') { $Matches[1] } })
        $ok = $pu.Count -eq 1 -and $du.Count -eq 1 -and $pu[0] -eq $du[0]
        Note ("{0,-16} {1,-8} /df's pages used equal du /'s pages: df {2}, du {3}/" -f "files",
              $(if ($ok) { "OK" } else { "MISSING" }), ($pu -join ","), ($du -join ","))
    }
    Absent "files" "^key: "
    Absent "files" "PANIC|Screen output from thread|events dropped|Unexpected IRQ"
}
# Repeated normal boots (-Repeat N): the scheduler self-test runs at every
# boot; each boot's result, ping-pong verdict and the preemptions during
# the ping-pong are recorded (VirtualBox can deliver timer ticks in bursts).
if ($Repeat -gt 0 -and ((Want "repeat") -or $Only.Count -eq 0)) {
    for ($n = 1; $n -le $Repeat; $n++) {
        $name = "repeat-{0:D2}" -f $n
        Start-Run
        Wait-Serial "^Scheduler self-test: (PASS|FAIL)" 60 | Out-Null
        Wait-Serial "^System ready\.|System halted" 20 | Out-Null
        VBox controlvm $VM poweroff | Out-Null
        Start-Sleep -Seconds 2
        $copy = Join-Path $OutDir "$Prefix-$name.serial.log"
        if (-not (Test-Path $serial)) { Note "${name}: NO SERIAL LOG"; continue }
        Copy-Item $serial $copy -Force
        $res = (Select-String -Path $copy -Pattern '^Scheduler self-test: (PASS|FAIL)' | Select-Object -First 1)
        $pre = (Select-String -Path $copy -Pattern '^Scheduler self-test: preemptions during the ping-pong: ([0-9]+)' | Select-Object -First 1)
        $pp = (Select-String -Path $copy -Pattern '^Scheduler self-test: cooperative ping-pong: (.*)$' | Select-Object -First 1)
        $ok = $res -and $res.Matches[0].Groups[1].Value -eq "PASS"
        Note ("{0,-16} {1,-8} /self-test {2}; ping-pong preemptions {3}; {4}/" -f $name,
              $(if ($ok) { "OK" } else { "MISSING" }),
              $(if ($res) { $res.Matches[0].Groups[1].Value } else { "no result" }),
              $(if ($pre) { $pre.Matches[0].Groups[1].Value } else { "?" }),
              $(if ($pp) { $pp.Matches[0].Groups[1].Value } else { "?" }))
    }
}

# 1.3 stage 9: the risk 13 sample (docs/plans/1.3.md, section J; 1.2's risk
# 13, an intermittent hang before the kernel starts at 1024x768x32).
# -Risk13 N normal boots of the ISO with no key, each to System ready and
# the desktop's first frame: from startvm on a screenshot every 250 ms (the
# first one's size and time reported); a boot passes when its serial log
# has "System ready." and the default path's "Desktop: shown on" line, a
# screenshot equal to the desktop's reference (tests/screen_ref.py desktop,
# the clock's cells masked) follows within 15 s, and no PANIC or FAILED
# (case-sensitive). A boot whose serial log stays empty for 60 s is a hang
# before the kernel started: counted, with its (empty) log, its screenshot
# and the CPU's registers (VBoxManage debugvm getregisters, read only).
# Every boot's serial log and last screenshot are kept; one row per boot,
# then a report-only summary.
if ($Risk13 -gt 0 -and (Want "risk13")) {
    $inv = [Globalization.CultureInfo]::InvariantCulture
    $sec = { param($x) if ($null -eq $x) { "none" } else { ([double]$x).ToString("0.0", $inv) } }
    $deskRgb = Join-Path $refDir "desktop.rgb"
    $r13 = New-Object System.Collections.Generic.List[object]
    for ($n = 1; $n -le $Risk13; $n++) {
        $name = "risk13-{0:D2}" -f $n
        if (Test-Path $serial) { Remove-Item $serial -Confirm:$false }
        VBox startvm $VM --type headless | Out-Null
        $t0 = Get-Date
        $first = $null; $readyAt = $null; $shownAt = $null; $frameAt = $null
        $until = $t0.AddSeconds(60)
        while ((Get-Date) -lt $until) {
            $s = Get-Shot
            $t = ((Get-Date) - $t0).TotalSeconds
            if ($s -and -not $first) { $first = "{0}x{1} at {2} ms" -f $s.W, $s.H, [int]($t * 1000) }
            if (Test-Path $serial) {
                if (-not $readyAt -and (Select-String -Path $serial -Pattern '^System ready\.' -CaseSensitive -Quiet)) { $readyAt = $t }
                if (-not $shownAt -and (Select-String -Path $serial -Pattern '^Desktop: shown on ' -CaseSensitive -Quiet)) {
                    $shownAt = $t
                    $until = (Get-Date).AddSeconds(15)
                }
            }
            if ($shownAt -and $s -and $s.W -eq 1024 -and $s.H -eq 768) {
                $d = ([MadireShot]::DifferMasked($s.Path, $deskRgb, 1024, 768, $Mask[0], $Mask[1], $Mask[2], $Mask[3])) -split ' '
                if ([long]$d[0] -eq 0) { $frameAt = ((Get-Date) - $t0).TotalSeconds; break }
            }
            Start-Sleep -Milliseconds 250
        }
        try { Shot $name } catch { Write-Host "${name}: no screenshot ($_)" }   # a hang may leave no screen
        if (-not ((Test-Path $serial) -and (Get-Item $serial).Length -gt 0)) {
            # As 1.2's sample: the CPU's registers (debugvm only reads them) before the power-off.
            $regs = try { @(VBox debugvm $VM getregisters all) } catch { @("debugvm getregisters failed: $_") }
            $regs | Set-Content -Path (Join-Path $OutDir "$Prefix-$name.registers.txt") -Encoding ascii
        }
        VBox controlvm $VM poweroff | Out-Null
        Start-Sleep -Seconds 2
        $copy = Join-Path $OutDir "$Prefix-$name.serial.log"
        if (Test-Path $serial) { Copy-Item $serial $copy -Force } else { New-Item -ItemType File -Path $copy | Out-Null }
        $bytes = (Get-Item $copy).Length
        $bad = @(Select-String -Path $copy -Pattern 'PANIC|FAILED' -CaseSensitive).Count
        $mode = ""
        $m = Select-String -Path $copy -Pattern '^Display: framebuffer ([0-9]+x[0-9]+x[0-9]+) at ' | Select-Object -First 1
        if ($m) { $mode = $m.Matches[0].Groups[1].Value }
        $ok = $bytes -gt 0 -and $readyAt -and $shownAt -and $frameAt -and $bad -eq 0
        $r13.Add([pscustomobject]@{ Bytes = $bytes; Ready = $readyAt; Frame = $frameAt; First = $first; Ok = $ok })
        Note ("{0,-16} {1,-8} /System ready and the desktop's first frame: {2} serial bytes, mode {3}, ready {4} s, desktop shown {5} s, first frame {6} s; first screenshot {7}; PANIC or FAILED lines {8}{9}/" -f
              $name, $(if ($ok) { "OK" } else { "MISSING" }), $bytes, $(if ($mode) { $mode } else { "none" }),
              (& $sec $readyAt), (& $sec $shownAt), (& $sec $frameAt), $(if ($first) { $first } else { "none" }), $bad,
              $(if ($bytes -eq 0) { "; a hang before the kernel started" } else { "" }))
    }
    $hangs = @($r13 | Where-Object { $_.Bytes -eq 0 }).Count
    $noReady = @($r13 | Where-Object { -not $_.Ready }).Count
    $noFrame = @($r13 | Where-Object { -not $_.Frame }).Count
    $range = { param($list) if (@($list).Count) { $mm = @($list) | Measure-Object -Minimum -Maximum; "{0}-{1}" -f $mm.Minimum, $mm.Maximum } else { "none" } }
    $rangeS = { param($list) if (@($list).Count) { $mm = @($list) | Measure-Object -Minimum -Maximum; "{0}-{1}" -f (& $sec $mm.Minimum), (& $sec $mm.Maximum) } else { "none" } }
    $firsts = ($r13 | Where-Object { $_.First } | Group-Object { ($_.First -split ' ')[0] } | ForEach-Object { "{0} x{1}" -f $_.Name, $_.Count }) -join ", "
    Note ("risk13 (report only): {0} boots of {1}; hangs before the kernel started (0 serial bytes) {2}; without System ready {3}; without the desktop's first frame {4}; serial bytes {5}; System ready {6} s; first frame {7} s; first screenshots {8}" -f
          $r13.Count, $isoHashes[$Iso], $hangs, $noReady, $noFrame,
          (& $range @($r13 | Where-Object { $_.Bytes -gt 0 } | ForEach-Object { $_.Bytes })),
          (& $rangeS @($r13 | Where-Object { $_.Ready } | ForEach-Object { $_.Ready })),
          (& $rangeS @($r13 | Where-Object { $_.Frame } | ForEach-Object { $_.Frame })),
          $(if ($firsts) { $firsts } else { "none" }))
}

# 1.1: the key window: the 0.13 keyboard error repeated on this VM. Twelve
# boots; in each, Home is pressed every 300 ms from 0, 0.5, 1, ... 5 or 6 s
# after startvm until the hidden menu shows (1.2 stage 10b: its header box,
# Test-MenuShown; until then a screen 1024 wide). A
# BIOS stopped by keys ("FATAL: Keyboard error") shows no menu and starts no
# kernel: the row is MISSING, with a screenshot, if the screen stays
# without the menu and the serial log empty for 30 s (or the kernel starts
# first).
# A shown menu is held 4 s, then Enter boots the first entry to System ready
# (report only, with the earliest reveal).
if (Want "keywindow") {
    $reveals = New-Object System.Collections.Generic.List[int]
    $kwHeld = 0
    $kwReady = 0
    foreach ($start in @(0, 500, 1000, 1500, 2000, 2500, 3000, 3500, 4000, 4500, 5000, 6000)) {
        $name = "keywindow-{0:D4}" -f $start
        if (Test-Path $serial) { Remove-Item $serial -Confirm:$false }
        VBox startvm $VM --type headless | Out-Null
        $t0 = Get-Date
        while (((Get-Date) - $t0).TotalMilliseconds -lt $start) { Start-Sleep -Milliseconds 20 }
        $until = (Get-Date).AddSeconds(30)
        $shown = $false
        $booted = $false
        while (-not $shown -and -not $booted -and (Get-Date) -lt $until) {
            $next = (Get-Date).AddMilliseconds(300)
            try { HomeKey } catch {}            # the VM may not take keys yet right after startvm
            if (Test-MenuShown) { $shown = $true; break }
            $booted = (Test-Path $serial) -and (Get-Item $serial).Length -gt 0
            while ((Get-Date) -lt $next) { Start-Sleep -Milliseconds 20 }
        }
        $at = [int]((Get-Date) - $t0).TotalMilliseconds
        if ($shown) {
            $reveals.Add($at)
            Start-Sleep -Seconds 4
            if (-not ((Test-Path $serial) -and (Get-Item $serial).Length -gt 0) -and (Test-MenuShown)) { $kwHeld++ }
            Enter
            if (Wait-Serial "System ready\." 40) { $kwReady++ }
        } else {
            Shot $name
        }
        # A shown menu also proves "no BIOS keyboard error": that error halts
        # the BIOS during its power-on self-test, before it loads Limine, so
        # Limine's menu (its header box) can't appear after it.
        Note ("{0,-16} {1,-8} /menu shown with keys from {2} ms; no BIOS keyboard error/" -f "keywindow",
              $(if ($shown) { "OK" } else { "MISSING" }), $start)
        Write-Host ("{0}: keys from {1} ms, {2} after {3} ms" -f $name, $start, $(if ($shown) { "menu shown" } elseif ($booted) { "kernel started without the menu" } else { "no menu, no kernel output" }), $at)
        VBox controlvm $VM poweroff | Out-Null
        Start-Sleep -Seconds 2
        if (Test-Path $serial) { Copy-Item $serial (Join-Path $OutDir "$Prefix-$name.serial.log") -Force }
    }
    $rv = $reveals | Measure-Object -Minimum -Maximum
    Note ("keywindow (report only): menu shown in {0} of 12 boots; earliest reveal {1} ms after startvm, latest {2} ms; held 4 s with no kernel output in {3}; System ready after Enter in {4}" -f
          $reveals.Count, $(if ($reveals.Count) { $rv.Minimum } else { "none" }), $(if ($reveals.Count) { $rv.Maximum } else { "none" }), $kwHeld, $kwReady)
}

# 0.13: which Diagnostics positions this run booted through the boot menu
# (their result rows above prove the right entry was selected).
if ($script:DiagBooted.Count) {
    $pos = ($script:DiagBooted.Values | Sort-Object) -join ","
    Note ("boot menu (report only): {0} of 27 Diagnostics positions booted through the menu: {1}" -f $script:DiagBooted.Count, $pos)
}

# 1.0 (stage 11): how long the screen took to exist after startvm (report only).
if ($script:ScreenWaits.Count) {
    $sw = $script:ScreenWaits | Measure-Object -Maximum -Average
    Note ("screen (report only): first screenshot after startvm in {0} menu boots: longest {1} ms, average {2:N0} ms (limit 10000)" -f
          $sw.Count, $sw.Maximum, $sw.Average)
}

} catch {
    Note ("{0,-16} {1,-8} /the run stopped on an error: {2}/" -f "run", "MISSING", $_)
} finally {
    Finish-Run
}

$missing =@($results | Where-Object { $_ -match ' MISSING ' -or $_ -match 'NO SERIAL LOG' }).Count
$results.Add(("exit status {0} ({1} missing)" -f $(if ($missing) { 1 } else { 0 }), $missing))
$results | Set-Content -Path $summary -Encoding ascii
Write-Host ""
Write-Host "Summary: $summary ($missing missing)"
if ($missing) { exit 1 }
