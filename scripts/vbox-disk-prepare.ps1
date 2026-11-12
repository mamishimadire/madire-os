# The one-time disk set-up of MadireOS-Test (1.4 stage 13; docs/plans/1.4.md,
# D6, C13, risk 30; AGENTS.md section 6, "MadireOS-Test's disk contents";
# docs/testing.md, "In 1.4 (stage 13)"). It makes the two pristine copies of
# the attached disk MadireOS-Test.vdi, "empty" and "marked", and leaves the
# disk in its resting state, "empty". Strictly in this order, stopping on
# any mismatch:
#
#   a) the attached (empty) disk file copied to the pristine "empty" copy;
#      the two SHA-256s equal; the UUID read by showmediuminfo (read only)
#      is the documented one. Nothing touches the attached disk before this.
#   b) the marked image (scripts\kali-disk.ps1 -Step image, checked against
#      -ImageSha256) written into the attached disk, keeping its UUID and
#      its 30 GB: VBoxManage convertfromraw makes a VDI of the image in the
#      result folder (a new file with its own UUID), then VBoxManage
#      clonemedium <that VDI> <the attached disk's UUID> --existing copies
#      its contents into the attached disk (the rest of the 30 GB stays as
#      it was: zero); the result folder's VDI is then closed in VirtualBox
#      if it was left registered (closemedium, never --delete), so the list
#      of registered disks is as before. Checked: the UUID and the capacity
#      unchanged (showmediuminfo), the file's SHA-256 changed.
#   c) the attached (marked) disk file copied to the pristine "marked" copy;
#      the two SHA-256s equal; its UUID read by showmediuminfo.
#   d) the "empty" copy copied back over the attached file (a file copy,
#      new in 1.4: risk 30); its SHA-256 equal to the "empty" copy's, and
#      VirtualBox's view of the disk (showmediuminfo, which reads the file
#      again) before and after kept: the UUID, the state "created" and the
#      capacity must be as before. If VirtualBox does not accept it, the
#      step stops and says so (the fallback, clonemedium --existing, needs
#      the owner's decision: the pristine copies carry the attached disk's
#      own UUID, which VirtualBox will not open twice).
#   e) showvminfo --machinereadable before and after: only the medium lines
#      and VMStateChangeTime may differ (none are expected: nothing is
#      attached or detached), and the registered disks (list hdds) before
#      and after are the same. MadireOS-View is never touched.
#
# Before (a) the attached disk must be exactly the empty state the S4 probe
# recorded (SHA-256 -EmptyDiskSha256, -EmptyDiskBytes bytes) with the
# documented UUID; otherwise the step refuses and changes nothing. It is
# one-time only: if a pristine copy (or its .sha256 file) exists, it
# refuses (a pristine copy is never overwritten). The copies are kept
# outside build\, in -PristineDir (by default %USERPROFILE%\madire-vm-disks),
# each with a .sha256 file next to it; their SHA-256s and UUID also go to
# <PristineDir>\pristine.txt and the result folder. The copies are plain files, never registered in VirtualBox
# (never opened by VBoxManage: they share the attached disk's UUID).
#
# An owner-run step (AGENTS.md section 6): run from plain PowerShell with VS
# Code closed. It refuses to start (one line, "DISK PREPARATION REFUSED:
# <reason>") unless the available host memory is at or above -MinFreeMB in
# three samples (5 s apart; no VM is started), no VM is running, the VM's
# disk is MadireOS-Test.vdi with the documented UUID, the image has its
# expected SHA-256 and fits the disk, and the result folder does not exist
# yet. It starts no VM, changes no VM setting, closes no application and
# asks for no credential. If it stops after (a), it puts the "empty" copy
# back over the attached file before it ends (the resting state) and says
# so. Everything goes into one new folder (-OutDir, by default
# build\vbox-disk-prepare-<date>-<time>); the summary's last line is its
# exit status.
#
#   powershell -ExecutionPolicy Bypass -File scripts\vbox-disk-prepare.ps1 `
#       -Image <kali-disk-image folder>\madire-test-disk.img -ImageSha256 <sha256>
param(
    [Parameter(Mandatory = $true)][string]$Image,
    [Parameter(Mandatory = $true)][string]$ImageSha256,
    [string]$PristineDir = "",          # default: %USERPROFILE%\madire-vm-disks (outside build\)
    [string]$DiskUuid = "e708d954-4c99-4b72-bd96-c1362a59c6cf",   # MadireOS-Test.vdi (docs/testing.md, 1.0 stage 11)
    [string]$EmptyDiskSha256 = "cb2679e9f545fdbc43605768420ca532c13de53f5b713814805fbfd4ffcc6ceb",   # the empty state (the S4 probe)
    [long]$EmptyDiskBytes = 2097152,    # the empty state's size (the S4 probe)
    [string]$OutDir = "",               # default: build\vbox-disk-prepare-<date>-<time>
    [int]$MinFreeMB = 2048,             # no VM is started by this step
    [string]$VM = "MadireOS-Test",
    [string]$VBoxManage = ""
)
$ErrorActionPreference = "Stop"
$root = (Get-Item (Split-Path $PSScriptRoot -Parent)).FullName
$build = Join-Path $root "build"
$stamp = Get-Date -Format "yyyyMMdd-HHmmss"
if (-not $OutDir) { $OutDir = Join-Path $build "vbox-disk-prepare-$stamp" }
if (-not $PristineDir) { $PristineDir = Join-Path $env:USERPROFILE "madire-vm-disks" }

function Refuse([string]$why) {
    Write-Host "DISK PREPARATION REFUSED: $why"
    exit 1
}

# ---- host memory first: three samples, 5 s apart ----
$memSamples = @()
for ($i = 0; $i -lt 3; $i++) {
    if ($i) { Start-Sleep -Seconds 5 }
    $memSamples += [int]((Get-CimInstance Win32_OperatingSystem).FreePhysicalMemory / 1KB)
}
$memLine = "host memory available: {0} MB (threshold {1} MB)" -f ($memSamples -join " / "), $MinFreeMB
Write-Host $memLine
if (@($memSamples | Where-Object { $_ -lt $MinFreeMB }).Count) {
    Refuse "$memLine; a sample is below the threshold (nothing started, no application closed)"
}

function Find-VBoxManage {
    if ($VBoxManage) { return $VBoxManage }
    $dirs = @()
    if ($env:VBOX_MSI_INSTALL_PATH) { $dirs += $env:VBOX_MSI_INSTALL_PATH }
    try { $dirs += (Get-ItemProperty "HKLM:\SOFTWARE\Oracle\VirtualBox" -ErrorAction Stop).InstallDir } catch {}
    if ($env:ProgramFiles) { $dirs += (Join-Path $env:ProgramFiles "Oracle\VirtualBox") }
    foreach ($d in $dirs) {
        $exe = Join-Path $d "VBoxManage.exe"
        if (Test-Path $exe) { return $exe }
    }
    Refuse "VBoxManage.exe not found (set VBOX_MSI_INSTALL_PATH or pass -VBoxManage)"
}
$vbox = Find-VBoxManage
function Invoke-Native([string]$Exe, [string[]]$Arguments, [switch]$AllowFailure) {
    $old = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    try { $out = & $Exe @Arguments 2>&1; $code = $LASTEXITCODE } finally { $ErrorActionPreference = $old }
    $lines = @($out | ForEach-Object { "$_" })
    if ($code -ne 0 -and -not $AllowFailure) { throw "$Exe $($Arguments -join ' ') failed ($code): $($lines -join ' ')" }
    return [pscustomobject]@{ ExitCode = $code; Output = $lines }
}
function VBox([string[]]$a) { return (Invoke-Native $script:vbox $a).Output }
function Get-VMInfo {
    $info = @{}
    foreach ($line in (VBox @("showvminfo", $VM, "--machinereadable"))) {
        if ($line -match '^"?([^"=]+)"?="?(.*?)"?$') { $info[$Matches[1]] = $Matches[2] }
    }
    return $info
}
# showmediuminfo of the attached disk (by its UUID; read only): its lines,
# and the UUID, State, Capacity and Location values.
function Get-Medium {
    $lines = @(VBox @("showmediuminfo", "disk", $DiskUuid))
    $v = @{ Lines = $lines }
    foreach ($l in $lines) {
        if ($l -match '^(UUID|State|Capacity|Location|Storage format|Format variant|Size on disk):\s+(.*)$') { $v[$Matches[1]] = $Matches[2].Trim() }
    }
    return $v
}
function Get-Hdds { return @(VBox @("list", "hdds")) }
function Sha([string]$p) { return (Get-FileHash -LiteralPath $p -Algorithm SHA256).Hash.ToLower() }

# ---- the VMs and the disk (read only) ----
$running = @(VBox @("list", "runningvms") | ForEach-Object { if ($_ -match '^"(.+)" \{') { $Matches[1] } })
if ($running.Count) { Refuse "VMs are running: $($running -join ', ') (power every VM off first; this step starts none)" }
$info = Get-VMInfo
if ($info["VMState"] -notin @("poweroff", "aborted")) { Refuse "$VM is '$($info['VMState'])' (expected powered off)" }
$disk = "$($info['SATA-0-0'])".Replace('\\', '\')
if (-not $disk -or (Split-Path $disk -Leaf) -ne "MadireOS-Test.vdi" -or -not (Test-Path -LiteralPath $disk)) { Refuse "SATA port 0 holds '$disk' (expected the file MadireOS-Test.vdi)" }
if ($info["SATA-ImageUUID-0-0"] -ne $DiskUuid) { Refuse "SATA port 0's disk has UUID '$($info['SATA-ImageUUID-0-0'])' (expected $DiskUuid)" }
$disk = (Get-Item -LiteralPath $disk).FullName
# The attached disk must be exactly the empty state the S4 probe recorded
# (SHA-256 and size); otherwise nothing is changed.
$hStart = Sha $disk
$lenStart = (Get-Item -LiteralPath $disk).Length
if ($hStart -ne $EmptyDiskSha256.ToLower() -or $lenStart -ne $EmptyDiskBytes) {
    Refuse "MadireOS-Test.vdi is not the recorded empty state: SHA-256 $hStart, $lenStart bytes (expected $EmptyDiskSha256, $EmptyDiskBytes bytes); nothing changed"
}
$m0 = Get-Medium
if ($m0["UUID"] -ne $DiskUuid -or $m0["State"] -ne "created" -or -not $m0["Capacity"]) { Refuse "showmediuminfo: UUID '$($m0['UUID'])', state '$($m0['State'])', capacity '$($m0['Capacity'])'" }
if (-not ($m0["Capacity"] -match '^([0-9]+) MBytes$')) { Refuse "showmediuminfo: capacity '$($m0['Capacity'])' not in MBytes" }
$capBytes = [long]$Matches[1] * 1MB
if (-not (Test-Path -LiteralPath $Image)) { Refuse "not found: $Image" }
$Image = (Get-Item -LiteralPath $Image).FullName
$imgHash = Sha $Image
if ($imgHash -ne $ImageSha256.ToLower()) { Refuse "the image's SHA-256 is $imgHash (expected $ImageSha256)" }
$imgLen = (Get-Item -LiteralPath $Image).Length
if ($imgLen % 512 -or $imgLen -ge $capBytes) { Refuse "the image has $imgLen bytes (a multiple of 512 below the disk's $capBytes expected)" }
$emptyCopy = Join-Path $PristineDir "MadireOS-Test-empty.vdi"
$markedCopy = Join-Path $PristineDir "MadireOS-Test-marked.vdi"
# One-time only: a pristine copy (or its .sha256 file) that exists is never overwritten.
foreach ($f in @($emptyCopy, $markedCopy, "$emptyCopy.sha256", "$markedCopy.sha256")) {
    if (Test-Path -LiteralPath $f) { Refuse "$f exists (the set-up is one-time only; a pristine copy is never overwritten)" }
}
if (Test-Path -LiteralPath $OutDir) { Refuse "the result folder $OutDir exists (nothing is overwritten)" }

New-Item -ItemType Directory -Path $OutDir | Out-Null
$OutDir = (Get-Item -LiteralPath $OutDir).FullName
if (-not (Test-Path -LiteralPath $PristineDir)) { New-Item -ItemType Directory -Path $PristineDir | Out-Null }
$PristineDir = (Get-Item -LiteralPath $PristineDir).FullName
$emptyCopy = Join-Path $PristineDir "MadireOS-Test-empty.vdi"
$markedCopy = Join-Path $PristineDir "MadireOS-Test-marked.vdi"
Start-Transcript -Path (Join-Path $OutDir "transcript.txt") | Out-Null
$results = New-Object System.Collections.Generic.List[string]
function Note([string]$s) { Write-Host $s; $script:results.Add($s) }
function Write-Lf([string]$path, [string[]]$lines) {
    [IO.File]::WriteAllText($path, (($lines -join "`n") + "`n"), [Text.UTF8Encoding]::new($false))
}
Write-Lf (Join-Path $OutDir "host-memory.txt") ($memSamples | ForEach-Object { "$_" })
$vmBefore = @(VBox @("showvminfo", $VM, "--machinereadable"))
Write-Lf (Join-Path $OutDir "vminfo-before.txt") $vmBefore
$hddsBefore = Get-Hdds
Write-Lf (Join-Path $OutDir "hdds-before.txt") $hddsBefore
Write-Lf (Join-Path $OutDir "mediuminfo-0-start.txt") $m0.Lines
Note "disk preparation (1.4 stage 13, D6; an owner-run step, AGENTS.md section 6)"
Note ("started {0}; {1}" -f (Get-Date -Format "yyyy-MM-dd HH:mm:ss zzz"), $memLine)
Note "no VM running; $VM powered off; SATA port 0: $disk (UUID $DiskUuid, $($m0['Capacity']), $($m0['Storage format']) $($m0['Format variant']))"
Note "marked image: $imgHash  $Image ($imgLen bytes, $($imgLen / 512) sectors; expected SHA-256: OK)"
Note "pristine copies: $emptyCopy, $markedCopy"

$result = "FAIL"
$emptyHash = $null
$emptyReady = $false
$markedHash = $null
$sourceVdi = Join-Path $OutDir "marked-source.vdi"
try {
    # a) the empty disk first: copied and verified before anything touches the attached disk.
    $h0 = Sha $disk
    Copy-Item -LiteralPath $disk -Destination $emptyCopy
    $emptyHash = Sha $emptyCopy
    $h0b = Sha $disk
    if ($emptyHash -ne $h0 -or $h0b -ne $h0) { throw "(a) the empty copy's SHA-256 $emptyHash differs from the attached file's ($h0, then $h0b)" }
    $emptyReady = $true
    Write-Lf "$emptyCopy.sha256" @("$emptyHash  MadireOS-Test-empty.vdi")
    Note "(a) OK: the empty disk copied: SHA-256 $emptyHash ($((Get-Item -LiteralPath $emptyCopy).Length) bytes), equal to the attached file's; UUID $($m0['UUID']) (showmediuminfo of the attached file, whose bytes the copy has)"

    # b) the marked image into the attached disk, its UUID and capacity kept.
    $null = VBox @("convertfromraw", "--format", "VDI", $Image, $sourceVdi)
    Note "(b) convertfromraw: $sourceVdi ($((Get-Item -LiteralPath $sourceVdi).Length) bytes, SHA-256 $(Sha $sourceVdi))"
    $clone = VBox @("clonemedium", "disk", $sourceVdi, $DiskUuid, "--existing")
    Note "(b) clonemedium $sourceVdi -> $DiskUuid --existing: $(($clone | Where-Object { "$_".Trim() }) -join ' | ')"
    # The result folder's VDI closed if VBoxManage left it registered (never deleted here).
    $hddsMid = Get-Hdds
    Write-Lf (Join-Path $OutDir "hdds-after-clone.txt") $hddsMid
    $uuidNow = ""
    $closed = @()
    foreach ($l in $hddsMid) {
        if ($l -match '^UUID:\s+(\S+)') { $uuidNow = $Matches[1] }
        if ($l -match '^Location:\s+(.+)$' -and $uuidNow -ne $DiskUuid -and
            [string]::Equals([IO.Path]::GetFullPath($Matches[1].Trim()), $sourceVdi, [StringComparison]::OrdinalIgnoreCase)) {
            $null = VBox @("closemedium", "disk", $uuidNow)
            $closed += $uuidNow
        }
    }
    Note ("(b) registered disks: the result folder's VDI {0}" -f $(if ($closed.Count) { "was left registered by clonemedium: closed (closemedium, not deleted): $($closed -join ', ')" } else { "was not left registered" }))
    $hddsNow = Get-Hdds
    $hd = @(Compare-Object $hddsBefore $hddsNow)
    if ($hd.Count) { throw "(b) the registered disks differ from before: $(($hd | ForEach-Object { "$($_.SideIndicator) $($_.InputObject)" }) -join ' | ')" }
    $m1 = Get-Medium
    Write-Lf (Join-Path $OutDir "mediuminfo-1-marked.txt") $m1.Lines
    $markedHash = Sha $disk
    if ($m1["UUID"] -ne $DiskUuid -or $m1["Capacity"] -ne $m0["Capacity"] -or $m1["State"] -ne "created") {
        throw "(b) showmediuminfo after the clone: UUID '$($m1['UUID'])', capacity '$($m1['Capacity'])', state '$($m1['State'])' (expected $DiskUuid, $($m0['Capacity']), created)"
    }
    if ($markedHash -eq $emptyHash) { throw "(b) the attached file did not change (SHA-256 $markedHash)" }
    Note "(b) OK: the attached disk holds the marked image: SHA-256 $markedHash ($((Get-Item -LiteralPath $disk).Length) bytes); UUID $($m1['UUID']) and capacity $($m1['Capacity']) unchanged; size on disk $($m1['Size on disk']); registered disks as before"

    # c) the marked disk copied.
    Copy-Item -LiteralPath $disk -Destination $markedCopy
    $mc = Sha $markedCopy
    if ($mc -ne $markedHash -or (Sha $disk) -ne $markedHash) { throw "(c) the marked copy's SHA-256 $mc differs from the attached file's ($markedHash)" }
    Write-Lf "$markedCopy.sha256" @("$markedHash  MadireOS-Test-marked.vdi")
    Note "(c) OK: the marked disk copied: SHA-256 $mc ($((Get-Item -LiteralPath $markedCopy).Length) bytes), equal to the attached file's; UUID $($m1['UUID']) (showmediuminfo of the attached file, whose bytes the copy has)"

    # d) the resting state: the empty copy back over the attached file (risk 30).
    Copy-Item -LiteralPath $emptyCopy -Destination $disk -Force
    $hr = Sha $disk
    $m2 = Get-Medium
    Write-Lf (Join-Path $OutDir "mediuminfo-2-restored.txt") $m2.Lines
    $md = @(Compare-Object $m1.Lines $m2.Lines)
    foreach ($d in $md) { Note ("(d) showmediuminfo {0}: {1}" -f $(if ($d.SideIndicator -eq "<=") { "marked  " } else { "restored" }), $d.InputObject) }
    if ($hr -ne $emptyHash) { throw "(d) the restored file's SHA-256 is $hr (expected the empty copy's $emptyHash)" }
    if ($m2["UUID"] -ne $DiskUuid -or $m2["State"] -ne "created" -or $m2["Capacity"] -ne $m0["Capacity"]) {
        throw "(d) VirtualBox does not accept the restored file as the same disk: UUID '$($m2['UUID'])', state '$($m2['State'])', capacity '$($m2['Capacity'])' (expected $DiskUuid, created, $($m0['Capacity'])); the fallback (clonemedium --existing) needs the owner's decision"
    }
    Note "(d) OK: the empty copy restored over the attached file by a file copy: SHA-256 $hr (the empty copy's); VirtualBox reads it as UUID $($m2['UUID']), state $($m2['State']), capacity $($m2['Capacity']), size on disk $($m2['Size on disk'])"
    Write-Lf (Join-Path $PristineDir "pristine.txt") @(
        "MadireOS-Test.vdi pristine copies (1.4 stage 13; made $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss zzz'); result folder $OutDir)",
        "UUID $DiskUuid (both: byte copies of the attached disk)",
        "empty  $emptyHash  MadireOS-Test-empty.vdi",
        "marked $markedHash  MadireOS-Test-marked.vdi",
        "marked image $imgHash  $Image")
    $result = "PASS"
} catch {
    Note "error: $($_.Exception.Message)"
} finally {
    # The resting state: if the step stopped after (a), the empty copy goes back.
    try {
        if ($emptyReady -and (Sha $disk) -ne $emptyHash) {
            Copy-Item -LiteralPath $emptyCopy -Destination $disk -Force
            Note ("resting state: the empty copy put back over the attached file after the stop: SHA-256 {0}" -f (Sha $disk))
        }
    } catch { Note "resting state: the empty copy could not be put back: $($_.Exception.Message)"; $result = "FAIL" }
    try {
        $mEnd = Get-Medium
        Write-Lf (Join-Path $OutDir "mediuminfo-3-end.txt") $mEnd.Lines
        $hEnd = Sha $disk
        Note ("resting disk: SHA-256 {0}; showmediuminfo UUID {1}, state {2}, capacity {3}{4}" -f $hEnd, $mEnd["UUID"], $mEnd["State"], $mEnd["Capacity"],
              $(if ($emptyHash) { "; " + $(if ($hEnd -eq $emptyHash) { "the empty copy's" } else { "NOT the empty copy's" }) } else { "" }))
        if ($emptyHash -and $hEnd -ne $emptyHash) { $result = "FAIL" }
    } catch { Note "resting disk: showmediuminfo failed: $($_.Exception.Message)"; $result = "FAIL" }
    try {
        $vmAfter = @(VBox @("showvminfo", $VM, "--machinereadable"))
        Write-Lf (Join-Path $OutDir "vminfo-after.txt") $vmAfter
        $diff = @(Compare-Object $vmBefore $vmAfter)
        $other = @($diff | Where-Object { $_.InputObject -notmatch '^"?(SATA-[0-9]+-0|SATA-ImageUUID-[0-9]+-0|VMStateChangeTime)"?=' })
        foreach ($d in $diff) { Note ("showvminfo {0}: {1}" -f $(if ($d.SideIndicator -eq "<=") { "before" } else { "after " }), $d.InputObject) }
        Note ("(e) showvminfo before and after: {0} lines differ, {1} other than the medium lines and VMStateChangeTime" -f $diff.Count, $other.Count)
        if ($other.Count) { $result = "FAIL" }
        $hddsAfter = Get-Hdds
        Write-Lf (Join-Path $OutDir "hdds-after.txt") $hddsAfter
        $hd = @(Compare-Object $hddsBefore $hddsAfter)
        Note ("(e) registered disks (list hdds) before and after: {0} lines differ" -f $hd.Count)
        if ($hd.Count) { $result = "FAIL" }
        $running = @(VBox @("list", "runningvms") | ForEach-Object { if ($_ -match '^"(.+)" \{') { $Matches[1] } })
        Note ("(e) VMs running at the end: {0}" -f $(if ($running.Count) { $running -join ", " } else { "none" }))
    } catch { Note "(e) showvminfo or list hdds after: error: $($_.Exception.Message)"; $result = "FAIL" }
    if ($result -eq "PASS") {
        Note "for the storage group: -EmptySha256 $emptyHash -MarkedSha256 $markedHash"
    }
    Note "RESULT=$result"
    $results.Add(("exit status {0}" -f $(if ($result -eq "PASS") { 0 } else { 1 })))
    Write-Lf (Join-Path $OutDir "disk-prepare-summary.txt") $results.ToArray()
    Write-Host "Summary: $(Join-Path $OutDir 'disk-prepare-summary.txt')"
    try { Stop-Transcript | Out-Null } catch {}
}
if ($result -ne "PASS") { exit 1 }
