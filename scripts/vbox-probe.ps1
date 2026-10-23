# VirtualBox probe for Madire OS (1.2 stage 3, docs/plans/1.2.md; test-only,
# report only: never acceptance evidence).
#
# Boots the PROBE ISO (make probe-iso: build/probe/madire-probe.iso, made
# outside Windows, e.g. on the Kali VM) in the test VM and records what
# VirtualBox gives the probe kernel: the framebuffer Limine sets up for each
# probe case (probe c), whether "textmode: yes" keeps text mode (b), whether
# the screen between Limine's mode set and the kernel's first serial byte is
# blank, and whether a screenshot of the shown menu equals wallpaper.png in
# the layout's header box pixel for pixel (g), and whether COM
# IMouse.PutMouseEvent works and its events reach the PS/2 aux port with the
# VM's current pointing device (f). It decides nothing: every result line
# starts with PROBE and goes to <OutDir>\probe-summary.txt.
#
# The VM's settings are CHECKED, never changed (the same check as
# scripts\vbox-acceptance.ps1; the graphics controller, VRAM and pointing
# device are read and reported). The only change is attaching the probe ISO
# to the optical drive; the medium it held before is attached again at the
# end, also after an error.
#
#   .\scripts\vbox-probe.ps1 -Iso build\probe\madire-probe.iso `
#       -Wallpaper build\probe\wallpaper.png -Layout build\probe\layout.json
param(
    [string]$VM = "MadireOS-Test",
    [string]$VBoxManage = "",
    [Parameter(Mandatory = $true)][string]$Iso,
    [Parameter(Mandatory = $true)][string]$Wallpaper,     # the ISO's /boot/limine/wallpaper.png
    [Parameter(Mandatory = $true)][string]$Layout,        # make-wallpaper.py --layout-json
    [string]$OutDir = "",               # default: build\vbox-probe
    [string]$KaliVM = "kali-linux-2026.2-virtualbox-amd64"   # must not be running
)
$ErrorActionPreference = "Stop"
$root = (Get-Item (Split-Path $PSScriptRoot -Parent)).FullName
$build = Join-Path $root "build"
if (-not $OutDir) { $OutDir = Join-Path $build "vbox-probe" }
$serial = Join-Path $build "vbox-serial.log"    # the documented COM1 file

# ---- VBoxManage (as scripts\vbox-acceptance.ps1) ----
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
if ($info["VMState"] -notin @("poweroff", "aborted")) {
    $problems += "the VM is '$($info['VMState'])'; power it off first (nothing is discarded here)"
}
if ($problems.Count) {
    $problems | ForEach-Object { Write-Host "VM check: $_" }
    throw "The VM does not match the documented settings; refusing to run (no setting was changed)."
}
$running = @(VBox list runningvms | ForEach-Object { if ($_ -match '^"(.+)" \{') { $Matches[1] } })
if ($running -contains $KaliVM) { throw "The Kali build VM '$KaliVM' is running; refusing to run (power it off first)." }
foreach ($f in @($Iso, $Wallpaper, $Layout)) { if (-not (Test-Path $f)) { throw "Not found: $f" } }
$Iso = (Get-Item $Iso).FullName
$Wallpaper = (Get-Item $Wallpaper).FullName
$hb = @((Get-Content $Layout -Raw | ConvertFrom-Json).header)
if ($hb.Count -ne 4) { throw "${Layout}: no header box" }
New-Item -ItemType Directory -Force $OutDir | Out-Null
$summary = Join-Path $OutDir "probe-summary.txt"
$results = New-Object System.Collections.Generic.List[string]
function Note([string]$s) { Write-Host $s; $script:results.Add($s) }

Note "VM check: $VM matches the documented settings (as scripts\vbox-acceptance.ps1 checks them)"
Note ("PROBE VM (read only): VirtualBox {0}; graphicscontroller={1} vram={2} MB hidpointing={3} hidkeyboard={4} usb ohci={5} ehci={6} xhci={7}" -f
      ((VBox --version) -join ''), $info["graphicscontroller"], $info["vram"], $info["hidpointing"], $info["hidkeyboard"],
      $info["usbohci"], $info["usbehci"], $info["usbxhci"])
Note ("PROBE ISO: {0}  {1} (a probe ISO, never acceptance evidence)" -f (Get-FileHash $Iso -Algorithm SHA256).Hash.ToLower(), $Iso)
Note ("PROBE wallpaper: {0}  {1}; header box {2}" -f (Get-FileHash $Wallpaper -Algorithm SHA256).Hash.ToLower(), $Wallpaper, ($hb -join ','))
$os = Get-CimInstance Win32_OperatingSystem
Note ("PROBE host memory (report only): {0:N0} MB available of {1:N0} MB" -f ($os.FreePhysicalMemory / 1KB), ($os.TotalVisibleMemorySize / 1KB))

# ---- screenshots: size, blank (one colour) and the menu header box ----
Add-Type -ReferencedAssemblies System.Drawing -TypeDefinition @'
using System;
using System.Drawing;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;
public static class MadireProbeImage {
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
    // "size=WxH blank=RRGGBB|no header=match|differ|n/a": the wallpaper is
    // centred on a larger screen, as Limine's wallpaper_style: centered.
    public static string Classify(string shot, string wallpaper, int x0, int y0, int x1, int y1) {
        int w, h;
        int[] p = Load(shot, out w, out h);
        string blank = p.Length == 0 ? "none" : p[0].ToString("x6");
        for (int i = 1; i < p.Length; i++) if (p[i] != p[0]) { blank = "no"; break; }
        if (refPath != wallpaper) { refPx = Load(wallpaper, out refW, out refH); refPath = wallpaper; }
        string header = "n/a";
        if (w >= refW && h >= refH) {
            int ox = (w - refW) / 2, oy = (h - refH) / 2;
            header = "match";
            for (int y = y0; y <= y1 && header == "match"; y++)
                for (int x = x0; x <= x1; x++)
                    if (p[(oy + y) * w + ox + x] != refPx[y * refW + x]) { header = "differ"; break; }
        }
        return string.Format("size={0}x{1} blank={2} header={3}", w, h, blank, header);
    }
}
'@

function Key { VBox controlvm $VM keyboardputscancode @args | Out-Null; Start-Sleep -Milliseconds 150 }
function Enter { Key 1c 9c; Start-Sleep -Milliseconds 400 }
function Down { Key e0 50 e0 d0 }
function HomeKey { Key e0 47 e0 c7 }
function SerialSize { if (Test-Path $serial) { return (Get-Item $serial).Length } else { return 0 } }
function SerialHas([string]$text) { return (Test-Path $serial) -and (Select-String -Path $serial -Pattern $text -SimpleMatch -Quiet) }
# A screenshot as <OutDir>\<name>.png (its path), or $null if VirtualBox
# gives none (no screen yet).
function Take([string]$name) {
    $f = Join-Path $OutDir "$name.png"
    if (Test-Path $f) { Remove-Item $f -Confirm:$false }
    try { VBox controlvm $VM screenshotpng $f 2>$null | Out-Null } catch { return $null }
    if (Test-Path $f) { return $f } else { return $null }
}
function Classify([string]$f) {
    if (-not $f) { return "size=none blank=none header=n/a" }
    return [MadireProbeImage]::Classify($f, $Wallpaper, [int]$hb[0], [int]$hb[1], [int]$hb[2], [int]$hb[3])
}
function Wait-Text([string]$text, [int]$seconds) {
    $until = (Get-Date).AddSeconds($seconds)
    while ((Get-Date) -lt $until) { if (SerialHas $text) { return $true }; Start-Sleep -Milliseconds 250 }
    return $false
}
function Save-Ps2m([string]$name) {
    try { $lines = @(VBox debugvm $VM info ps2m) } catch { $lines = @("debugvm failed: $_") }
    $lines | Set-Content -Path (Join-Path $OutDir "$name-ps2m.txt") -Encoding ascii
    foreach ($l in $lines) { if ($l.Trim()) { Note "PROBE ${name}: info ps2m: $($l.Trim())" } }
}
# Powers off and keeps the serial log as <name>.serial.log; notes its
# probe lines.
function Stop-Boot([string]$name) {
    try { VBox controlvm $VM poweroff | Out-Null } catch {}
    Start-Sleep -Seconds 2
    $copy = Join-Path $OutDir "$name.serial.log"
    if (Test-Path $serial) { Copy-Item $serial $copy -Force } else { Note "PROBE ${name}: NO SERIAL LOG"; return }
    foreach ($m in @(Select-String -Path $copy -Pattern '^probe: ' -CaseSensitive)) { Note "PROBE $name serial: $($m.Line)" }
}

# f: COM IMouse on the running VM (a shared lock: the console only, no
# setting changes), the same events as tests/probe_fb.py sends in QEMU.
function Send-ComMouse([string]$name) {
    try {
        $client = New-Object -ComObject VirtualBox.VirtualBoxClient
        $session = $client.Session
        $client.VirtualBox.FindMachine($VM).LockMachine($session, 1)    # LockType_Shared
        try {
            $mouse = $session.Console.Mouse
            Note ("PROBE ${name}: COM IMouse: AbsoluteSupported={0} RelativeSupported={1} NeedsHostCursor={2}" -f
                  $mouse.AbsoluteSupported, $mouse.RelativeSupported, $mouse.NeedsHostCursor)
            $mouse.PutMouseEvent(10, -5, 0, 0, 0); Start-Sleep -Milliseconds 100
            $mouse.PutMouseEvent(0, 0, 0, 0, 1); Start-Sleep -Milliseconds 100      # left button down
            $mouse.PutMouseEvent(0, 0, 0, 0, 0); Start-Sleep -Milliseconds 100      # up
            $mouse.PutMouseEvent(-3, 7, 0, 0, 0)
            Note "PROBE ${name}: COM IMouse.PutMouseEvent: 4 calls returned without an error (move 10,-5; left down; up; move -3,7)"
        } finally { $session.UnlockMachine() }
    } catch { Note "PROBE ${name}: COM IMouse failed: $_" }
}

# Boot A, case 1 with no key: a screenshot every 250 ms (each classed by
# the serial log's size just before it), the mouse events once the probe
# polls the aux port, until "probe: done", then one more 1 s later.
function Watch-NoKey([string]$name) {
    if (Test-Path $serial) { Remove-Item $serial -Confirm:$false }
    VBox startvm $VM --type headless | Out-Null
    $t0 = Get-Date
    $counts = [ordered]@{}; $last = ""; $i = 0; $mouseSent = $false; $seen = $false; $firstByte = $null
    while (((Get-Date) - $t0).TotalSeconds -lt 120) {
        $size = SerialSize
        $t = ((Get-Date) - $t0).TotalSeconds
        if ($size -gt 0 -and $null -eq $firstByte) { $firstByte = $t }
        $phase = if ($size -eq 0) { "before" } else { "after" }
        $i++
        $f = Take ("{0}-{1:D3}" -f $name, $i)
        $key = "phase=$phase $(Classify $f)"
        if ($counts.Contains($key)) { $counts[$key]++ } else { $counts[$key] = 1 }
        if ($key -ne $last) {
            Note ("PROBE {0}: SHOT {1:N2} s {2} ({3})" -f $name, $t, $key, $(if ($f) { Split-Path $f -Leaf } else { "no screenshot" }))
            $last = $key
        }
        if (-not $mouseSent -and (SerialHas "probe: aux ready")) {
            $mouseSent = $true
            Save-Ps2m "$name-before-mouse"
            Send-ComMouse $name
        }
        if (SerialHas "probe: done") { $seen = $true; break }
        Start-Sleep -Milliseconds 250
    }
    if ($seen) {
        Start-Sleep -Seconds 1
        Note "PROBE ${name}: FINAL 1 s after probe: done: $(Classify (Take "$name-final"))"
        Save-Ps2m "$name-after"
    } else { Note "PROBE ${name}: 'probe: done' not seen in 120 s" }
    foreach ($k in $counts.Keys) { Note "PROBE ${name}: SUMMARY $k : $($counts[$k]) screenshots" }
    $hdr = 0; foreach ($k in $counts.Keys) { if ($k -like '*header=match*') { $hdr += $counts[$k] } }
    Note ("PROBE {0}: FIRST_BYTE {1}; HEADER_MATCHES {2} of {3} screenshots; mouse events {4}" -f $name,
          $(if ($null -ne $firstByte) { "seen by {0:N2} s" -f $firstByte } else { "none seen" }), $hdr, $i,
          $(if ($mouseSent) { "sent" } else { "not sent ('probe: aux ready' never logged)" }))
    Stop-Boot $name
}

# Boots B-G: Home every 300 ms until Limine's menu shows, a screenshot of
# the shown menu (classed: the header box against the wallpaper), then
# Down to the case's entry, Enter, until <until>, and a screenshot 1 s
# later. The menu counts as shown only when a screenshot's header box
# equals the wallpaper's (header=match; docs/plans/1.2.md D2, amended in
# stage 3): a blank or other 1024x768 screen (the hand-off to the kernel)
# never does. The screenshots taken while waiting for the menu are deleted
# once it shows; if it never shows they are kept (with the serial log, as
# for every boot) and the reason is noted.
function Start-MenuCase([string]$name, [int]$case, [string]$until) {
    if (Test-Path $serial) { Remove-Item $serial -Confirm:$false }
    VBox startvm $VM --type headless | Out-Null
    $t0 = Get-Date
    $shown = $false
    $shots = New-Object System.Collections.Generic.List[string]
    $why = "45 s passed"
    while (((Get-Date) - $t0).TotalSeconds -lt 45) {
        $next = (Get-Date).AddMilliseconds(300)
        HomeKey
        $f = Take ("{0}-reveal-{1:D3}" -f $name, ($shots.Count + 1))
        if ($f) {
            $shots.Add($f)
            if ((Classify $f) -like '* header=match') { $shown = $true; break }
        }
        if ((SerialSize) -gt 0) { $why = "the serial log grew (booted without the menu)"; break }
        while ((Get-Date) -lt $next) { Start-Sleep -Milliseconds 20 }
    }
    if (-not $shown) {
        Note ("PROBE {0}: the menu did not show ({1} after {2:N1} s); {3} screenshots kept ({0}-reveal-*.png), the last: {4}" -f
              $name, $why, ((Get-Date) - $t0).TotalSeconds, $shots.Count, $(if ($shots.Count) { Classify $shots[$shots.Count - 1] } else { "none" }))
        Stop-Boot $name
        return
    }
    foreach ($f in $shots) { Remove-Item $f -Confirm:$false }
    Start-Sleep -Seconds 1
    Note ("PROBE {0}: menu shown after {1:N1} s: {2}" -f $name, ((Get-Date) - $t0).TotalSeconds, (Classify (Take "$name-menu")))
    HomeKey; Start-Sleep -Milliseconds 300
    for ($i = 1; $i -lt $case; $i++) { Down }
    Start-Sleep -Milliseconds 300
    Enter
    if (Wait-Text $until 90) {
        Start-Sleep -Seconds 1
        Note "PROBE ${name}: case $case, 1 s after '$until': $(Classify (Take "$name-final"))"
    } else { Note "PROBE ${name}: case ${case}: '$until' not seen in 90 s" }
    Stop-Boot $name
}

$prevMedium = "$($info['SATA-1-0'])"
Note "PROBE optical drive before: $prevMedium"
try {
    VBox storageattach $VM --storagectl SATA --port 1 --device 0 --type dvddrive --medium $Iso | Out-Null
    if (-not (Test-SamePath (Get-VMInfo)["SATA-1-0"] $Iso)) { throw "the optical drive does not hold the probe ISO" }
    Watch-NoKey "A-case1"
    Start-MenuCase "B-case2" 2 "System ready."
    Start-MenuCase "C-case3" 3 "probe: done"
    Start-MenuCase "D-case4" 4 "probe: done"
    Start-MenuCase "E-case5" 5 "probe: done"
    Start-MenuCase "F-case6" 6 "probe: done"
    Start-MenuCase "G-case7" 7 "probe: done"
    # One line per boot: the mode asked for and the mode given.
    foreach ($n in @("A-case1", "B-case2", "C-case3", "D-case4", "E-case5", "F-case6", "G-case7")) {
        $log = Join-Path $OutDir "$n.serial.log"
        $req = "none logged"; $got = "no framebuffer fields"
        if (Test-Path $log) {
            $r = Select-String -Path $log -Pattern '^probe: header requests mode_type [0-9]+, width ([0-9]+), height ([0-9]+), depth ([0-9]+)$' | Select-Object -Last 1
            if ($r) { $req = "{0}x{1}x{2}" -f $r.Matches[0].Groups[1].Value, $r.Matches[0].Groups[2].Value, $r.Matches[0].Groups[3].Value }
            $g = Select-String -Path $log -Pattern '^probe: framebuffer \(flags bit 12\): address (0x[0-9a-f]+), pitch ([0-9]+), width ([0-9]+), height ([0-9]+), bpp ([0-9]+), type ([0-9]+)$' | Select-Object -Last 1
            if ($g) { $got = "{0}x{1}x{2} type {3}, pitch {4}, at {5}" -f $g.Matches[0].Groups[3].Value, $g.Matches[0].Groups[4].Value, $g.Matches[0].Groups[5].Value, $g.Matches[0].Groups[6].Value, $g.Matches[0].Groups[2].Value, $g.Matches[0].Groups[1].Value }
        }
        Note "PROBE_MODE ${n}: requested $req; given $got"
    }
} finally {
    try { if ((Get-VMInfo)["VMState"] -eq "running") { VBox controlvm $VM poweroff | Out-Null; Start-Sleep -Seconds 2 } } catch {}
    $restore = $prevMedium.Replace('\\', '\')
    if (-not $restore -or $restore -eq "none" -or $restore -eq "emptydrive" -or -not (Test-Path $restore)) { $restore = "emptydrive" }
    try {
        VBox storageattach $VM --storagectl SATA --port 1 --device 0 --type dvddrive --medium $restore | Out-Null
        Note "PROBE optical drive restored: $((Get-VMInfo)['SATA-1-0']) (before: $prevMedium)"
    } catch { Note "PROBE optical drive NOT restored (before: $prevMedium): $_" }
    $results | Set-Content -Path $summary -Encoding ascii
    Write-Host ""
    Write-Host "Summary: $summary"
}
