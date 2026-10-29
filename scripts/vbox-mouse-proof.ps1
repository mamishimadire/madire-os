# VirtualBox mouse proof for Madire OS (1.3 stage 3, docs/plans/1.3.md
# section D, decision 13; report only, never acceptance evidence).
#
# Proves whether COM IMouse.PutMouseEvent on the test VM, as configured,
# reaches Madire's PS/2 mouse driver through IRQ12. Boots a test ISO whose
# first boot-menu entry has "test=mouse mouselog" (made outside Windows,
# e.g. on the Kali VM, around the kernel under test), headless, waits for
# the driver's ready line on serial, sends the four calls of 1.2's probe
# (move 10,-5; left down; left up; move -3,7), waits for test=mouse's
# summary, keeps the serial log and powers the VM off. The driver reads aux
# bytes only in its IRQ12 handler once started, so the summary's counts are
# the IRQ12 evidence. Every result line starts with PROOF and goes to
# <OutDir>\proof-summary.txt; the judgement is made from that folder.
#
# It refuses to start (one line, "PROOF RUN REFUSED: <reason>") unless the
# host's available memory is at least -MinFreeMB in three samples (AGENTS.md
# section 6), the VM's settings are as documented (the same checks as
# scripts\vbox-acceptance.ps1), no other VM of the project runs, both ISOs
# have their expected SHA-256 and the result folder does not exist yet.
#
# The VM's settings are never changed. The only change is the medium in
# the optical drive: the test ISO for the run, then the resting medium
# (-RestoreIso, the v1.2 release ISO), also after an error. showvminfo is
# saved before and after, and every line that differs is reported. The last
# line printed is "PROOF RUN FINISHED: results in <OutDir>".
#
#   powershell -ExecutionPolicy Bypass -File scripts\vbox-mouse-proof.ps1 `
#       -Iso build\kali-13s3c\madire-os-mouse.iso -IsoSha256 <sha256>
param(
    [string]$VM = "MadireOS-Test",
    [string]$VBoxManage = "",
    [Parameter(Mandatory = $true)][string]$Iso,
    [Parameter(Mandatory = $true)][string]$IsoSha256,
    [string]$RestoreIso = "",           # default: build\v1.2-release\madire-os.iso
    [string]$RestoreSha256 = "2a2acbe599c003d4e3015a6ab7bfd1632d15c8741dbcffede07381cb8b7292c6",
    [int]$MinFreeMB = 3500,             # docs/testing.md: MadireOS-Test's threshold
    [string]$OutDir = "",               # default: build\vbox-mouse-proof
    [string]$KaliVM = "kali-linux-2026.2-virtualbox-amd64",   # must not be running
    [string]$ViewVM = "MadireOS-View"   # must not be running
)
$ErrorActionPreference = "Stop"
$root = (Get-Item (Split-Path $PSScriptRoot -Parent)).FullName
$build = Join-Path $root "build"
if (-not $RestoreIso) { $RestoreIso = Join-Path $build "v1.2-release\madire-os.iso" }
if (-not $OutDir) { $OutDir = Join-Path $build "vbox-mouse-proof" }
$serial = Join-Path $build "vbox-serial.log"    # the documented COM1 file

function Refuse([string]$why) {
    Write-Host "PROOF RUN REFUSED: $why"
    exit 1
}

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
    Refuse "VBoxManage.exe not found (set VBOX_MSI_INSTALL_PATH or pass -VBoxManage)"
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

function Get-Sha256([string]$path) { return (Get-FileHash $path -Algorithm SHA256).Hash.ToLower() }

# ---- host memory first (AGENTS.md section 6): three samples, 5 s apart ----
$samples = @()
for ($i = 0; $i -lt 3; $i++) {
    if ($i) { Start-Sleep -Seconds 5 }
    $samples += [int]((Get-CimInstance Win32_OperatingSystem).FreePhysicalMemory / 1KB)
}
$memLine = "host memory available: {0} MB (threshold {1} MB)" -f ($samples -join " / "), $MinFreeMB
Write-Host $memLine
if (@($samples | Where-Object { $_ -lt $MinFreeMB }).Count) { Refuse "$memLine; a sample is below the threshold" }

# ---- the VM must be as documented (docs/testing.md); never changed here ----
try { $info = Get-VMInfo } catch { Refuse "cannot read the VM '$VM': $_" }
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
if ($info["graphicscontroller"] -ne "vboxsvga") { $problems += "graphics controller is '$($info['graphicscontroller'])' (expected vboxsvga)" }
if ($info["vram"] -ne "128") { $problems += "video memory is $($info['vram']) MB (expected 128)" }
if ($info["hidpointing"] -ne "usbtablet") { $problems += "pointing device is '$($info['hidpointing'])' (expected usbtablet)" }
if ($info["VMState"] -notin @("poweroff", "aborted")) {
    $problems += "the VM is '$($info['VMState'])'; power it off first (nothing is discarded here)"
}
if ($problems.Count) { Refuse ("the VM does not match the documented settings (no setting was changed): " + ($problems -join "; ")) }
# Only one VM at a time (AGENTS.md section 6).
$running = @(VBox list runningvms | ForEach-Object { if ($_ -match '^"(.+)" \{') { $Matches[1] } })
foreach ($other in @($KaliVM, $ViewVM)) {
    if ($running -contains $other) { Refuse "the VM '$other' is running (power it off first)" }
}
foreach ($f in @($Iso, $RestoreIso)) { if (-not (Test-Path $f)) { Refuse "not found: $f" } }
$Iso = (Get-Item $Iso).FullName
$RestoreIso = (Get-Item $RestoreIso).FullName
$isoHash = Get-Sha256 $Iso
if ($isoHash -ne $IsoSha256.ToLower()) { Refuse "the test ISO's SHA-256 is $isoHash (expected $IsoSha256)" }
$restoreHash = Get-Sha256 $RestoreIso
if ($restoreHash -ne $RestoreSha256.ToLower()) { Refuse "the resting medium's SHA-256 is $restoreHash (expected $RestoreSha256)" }
if ((Test-Path $OutDir) -and @(Get-ChildItem $OutDir -Force).Count) { Refuse "the result folder $OutDir exists and is not empty (nothing is overwritten)" }
New-Item -ItemType Directory -Force $OutDir | Out-Null
$OutDir = (Get-Item $OutDir).FullName
$summary = Join-Path $OutDir "proof-summary.txt"
$results = New-Object System.Collections.Generic.List[string]
function Note([string]$s) { Write-Host $s; $script:results.Add($s) }

$vmBefore = @(VBox showvminfo $VM --machinereadable)
$vmBefore | Set-Content -Path (Join-Path $OutDir "vminfo-before.txt") -Encoding ascii
Note ("PROOF started {0}" -f (Get-Date -Format "yyyy-MM-dd HH:mm:ss zzz"))
Note "PROOF $memLine"
Note "VM check: $VM matches the documented settings (as scripts\vbox-acceptance.ps1 checks them)"
Note ("PROOF VM (read only): VirtualBox {0}; hidpointing={1} hidkeyboard={2} usb ohci={3} ehci={4} xhci={5}" -f
      ((VBox --version) -join ''), $info["hidpointing"], $info["hidkeyboard"], $info["usbohci"], $info["usbehci"], $info["usbxhci"])
Note "PROOF test ISO: $isoHash  $Iso (a test ISO, never acceptance evidence)"
Note "PROOF resting medium: $restoreHash  $RestoreIso"

function SerialHas([string]$text) { return (Test-Path $serial) -and (Select-String -Path $serial -Pattern $text -SimpleMatch -Quiet) }
function Wait-Text([string]$text, [int]$seconds) {
    $until = (Get-Date).AddSeconds($seconds)
    while ((Get-Date) -lt $until) { if (SerialHas $text) { return $true }; Start-Sleep -Milliseconds 250 }
    return $false
}

# COM IMouse on the running VM (a shared lock: the console only, no setting
# changes): the same four calls as 1.2's probe, 100 ms apart.
function Send-ComMouse {
    $client = New-Object -ComObject VirtualBox.VirtualBoxClient
    $session = $client.Session
    $client.VirtualBox.FindMachine($VM).LockMachine($session, 1)    # LockType_Shared
    try {
        $mouse = $session.Console.Mouse
        Note ("PROOF COM IMouse: AbsoluteSupported={0} RelativeSupported={1} NeedsHostCursor={2}" -f
              $mouse.AbsoluteSupported, $mouse.RelativeSupported, $mouse.NeedsHostCursor)
        $mouse.PutMouseEvent(10, -5, 0, 0, 0); Start-Sleep -Milliseconds 100
        $mouse.PutMouseEvent(0, 0, 0, 0, 1); Start-Sleep -Milliseconds 100      # left button down
        $mouse.PutMouseEvent(0, 0, 0, 0, 0); Start-Sleep -Milliseconds 100      # left button up
        $mouse.PutMouseEvent(-3, 7, 0, 0, 0)
        Note "PROOF COM IMouse.PutMouseEvent: 4 calls returned without an error (move 10,-5; left down; left up; move -3,7)"
    } finally { $session.UnlockMachine() }
}

# What each call must give (drivers/mouse/mouse_core.c: dy in screen
# coordinates, so the host's values; kernel/display/pointer.c's format; the
# " at (x, y)" suffix is there while the pointer is on screen).
$expected = @(
    @{ call = "move 10,-5";  line = "dx +10 dy -5 buttons ---" },
    @{ call = "left down";   line = "dx +0 dy +0 buttons L-- pressed L" },
    @{ call = "left up";     line = "dx +0 dy +0 buttons --- released L" },
    @{ call = "move -3,7";   line = "dx -3 dy +7 buttons ---" }
)
$ready = "Mouse test: ready: "
$log = Join-Path $OutDir "mouse-proof.serial.log"
Note "PROOF optical drive before: $($info['SATA-1-0'])"
try {
    VBox storageattach $VM --storagectl SATA --port 1 --device 0 --type dvddrive --medium $Iso | Out-Null
    if (-not (Test-SamePath (Get-VMInfo)["SATA-1-0"] $Iso)) { throw "the optical drive does not hold the test ISO" }
    if (Test-Path $serial) { Remove-Item $serial -Confirm:$false }
    VBox startvm $VM --type headless | Out-Null
    $t0 = Get-Date
    if (Wait-Text $ready 120) {
        Note ("PROOF ready line seen after {0:N1} s" -f ((Get-Date) - $t0).TotalSeconds)
        try { Send-ComMouse } catch { Note "PROOF COM IMouse failed: $_" }
        # test=mouse ends 5 s after the last packet (60 s without any).
        if (Wait-Text "Mouse test: motion " 90) { Note "PROOF test=mouse finished" } else { Note "PROOF 'Mouse test: motion' not seen in 90 s" }
        if (Wait-Text "System ready." 60) { Note "PROOF System ready. seen (report only)" } else { Note "PROOF 'System ready.' not seen in 60 s (report only)" }
    } else { Note "PROOF the ready line ('$ready') was not seen in 120 s" }
} catch {
    Note "PROOF ERROR during the run: $_"
} finally {
    try { if ((Get-VMInfo)["VMState"] -eq "running") { VBox controlvm $VM poweroff | Out-Null; Start-Sleep -Seconds 2 } } catch {}
    try { Note "PROOF VM state after power-off: $((Get-VMInfo)['VMState'])" } catch { Note "PROOF VM state unreadable: $_" }
    if (Test-Path $serial) { Copy-Item $serial $log -Force } else { Note "PROOF no serial log was written" }
    # The resting medium: the v1.2 release ISO, its hash read after the restore.
    try {
        VBox storageattach $VM --storagectl SATA --port 1 --device 0 --type dvddrive --medium $RestoreIso | Out-Null
        $now = (Get-VMInfo)["SATA-1-0"]
        $h = Get-Sha256 $RestoreIso
        Note "PROOF optical drive after the restore (showvminfo SATA-1-0): $now"
        Note ("PROOF resting medium after the restore: {0}  {1}; path {2}, SHA-256 {3}" -f $h, $RestoreIso,
              $(if (Test-SamePath $now $RestoreIso) { "as expected" } else { "NOT the expected path" }),
              $(if ($h -eq $RestoreSha256.ToLower()) { "as expected" } else { "NOT the expected hash" }))
    } catch { Note "PROOF resting medium NOT restored: $_" }
    try {
        $vmAfter = @(VBox showvminfo $VM --machinereadable)
        $vmAfter | Set-Content -Path (Join-Path $OutDir "vminfo-after.txt") -Encoding ascii
        $diff = @(Compare-Object $vmBefore $vmAfter)
        if ($diff.Count) {
            foreach ($d in $diff) { Note ("PROOF showvminfo {0}: {1}" -f $(if ($d.SideIndicator -eq "<=") { "before" } else { "after " }), $d.InputObject) }
        } else { Note "PROOF showvminfo: before and after identical" }
    } catch { Note "PROOF showvminfo after: unreadable: $_" }
}

# ---- the serial log: the packets, the summary, any other mouse line ----
$pass = $true
function Fail([string]$why) { $script:pass = $false; Note "PROOF CHECK failed: $why" }
if (-not (Test-Path $log)) {
    Fail "no serial log"
} else {
    $lines = @(Get-Content $log | ForEach-Object { $_.TrimEnd("`r") })
    $packets = @($lines | Where-Object { $_ -like "mouse: packet *" })
    foreach ($p in $packets) { Note "PROOF serial: $p" }
    if ($packets.Count -ne 4) { Fail "$($packets.Count) packet lines (expected 4)" }
    for ($i = 0; $i -lt [Math]::Min(4, $packets.Count); $i++) {
        $e = $expected[$i]
        $re = '^mouse: packet ' + ($i + 1) + ': ' + [regex]::Escape($e.line) + '( at \(-?[0-9]+, -?[0-9]+\))?$'
        if ($packets[$i] -match $re) { Note "PROOF packet $($i + 1) ($($e.call)): matches '$($e.line)'" }
        else { Fail "packet $($i + 1) ($($e.call)): expected '$($e.line)'" }
    }
    $done = @($lines | Where-Object { $_ -like "Mouse test: done: *" })
    foreach ($l in @($lines | Where-Object { $_ -like "mouse*" -or $_ -like "pointer*" })) {
        if ($l -notlike "mouse: packet *") { Note "PROOF serial: $l" }
    }
    $doneRe = '^Mouse test: done: ([0-9]+) packets from ([0-9]+) bytes \(([0-9]+) bad bytes, ([0-9]+) overflows, ([0-9]+) dropped: queue full\); IRQ12 delivered ([0-9]+) times: ([0-9]+) bytes read, ([0-9]+) with nothing waiting, ([0-9]+) with a port 1 byte$'
    if ($done.Count -ne 1 -or $done[0] -notmatch $doneRe) {
        Fail "no single 'Mouse test: done' line in the expected form"
    } else {
        $g = $Matches
        Note ("PROOF counts: packets {0}, bytes {1}, bad {2}, overflows {3}, dropped {4}; IRQ12 {5} times: {6} bytes read, {7} with nothing waiting, {8} with a port 1 byte" -f
              $g[1], $g[2], $g[3], $g[4], $g[5], $g[6], $g[7], $g[8], $g[9])
        if ([int]$g[1] -ne 4 -or [int]$g[2] -ne 12) { Fail "not 4 packets from 12 bytes" }
        if ([int]$g[3] -ne 0 -or [int]$g[4] -ne 0 -or [int]$g[5] -ne 0 -or [int]$g[9] -ne 0) { Fail "a bad byte, overflow, drop or port 1 byte" }
        if ([int]$g[6] -lt 4) { Fail "IRQ12 delivered $($g[6]) times (expected at least 4)" }
    }
    $bad = @($lines | Where-Object { $_ -match 'PANIC|FAILED' })
    if ($bad.Count) { Note "PROOF serial: $($bad.Count) PANIC or FAILED lines (report only), the first: $($bad[0])" }
}
Note ("PROOF CHECK (the script's own reading; the judgement is made from this folder): {0}" -f
      $(if ($pass) { "every check met (4 packets, each as its call, through IRQ12, no other byte)" } else { "NOT every check met" }))
Note ("PROOF ended {0}" -f (Get-Date -Format "yyyy-MM-dd HH:mm:ss zzz"))
$results | Set-Content -Path $summary -Encoding ascii
Write-Host ""
Write-Host "PROOF RUN FINISHED: results in $OutDir"
