# The Kali steps of the VirtualBox storage work (1.4 stage 13;
# docs/plans/1.4.md, C13, S13/S14; docs/testing.md, "In 1.4 (stage 13)"):
# scripts/kali-disk.sh runs on the Kali VM.
#
#   -Step image   the marked test image for MadireOS-Test's disk (64 MiB:
#                 an MBR, one Linux partition at LBA 2048 with the Madire
#                 test marker, an ext2 volume labelled madire-test made by
#                 mke2fs with the pinned tests/mke2fs.conf from the seed
#                 tree of scripts/vbox-storage.py); e2fsck -fn reports
#                 nothing; copied back as madire-test-disk.img with its
#                 SHA-256 and Kali's e2fsprogs version.
#   -Step check   a VirtualBox disk file (-Vdi, checked against -VdiSha256):
#                 converted to raw on Kali (qemu-img), its partition
#                 extracted, e2fsck -fn must report nothing, the volume must
#                 be clean, and debugfs's tree must equal the expected tree
#                 (-Expect seed: the pristine "marked" copy, which with
#                 -Image must also hold exactly that image and zeros after
#                 it; -Expect session: a disk the storage group used).
#
# An owner-run step (AGENTS.md section 6): run from plain PowerShell with VS
# Code closed. It refuses to start (one line, "KALI DISK STEP REFUSED:
# <reason>") unless the available host memory is at or above -MinFreeMB in
# three samples (5 s apart), Kali, MadireOS-Test and MadireOS-View are all
# powered off, Kali's memory is 3,072 MB (never changed here), every input
# has its expected SHA-256 and the result folder does not exist yet. It
# starts Kali headless (key login only), and at the end, also after an
# error, powers it off (ACPI, then power off) and confirms it; showvminfo of
# Kali before and after: only VMStateChangeTime may differ. It never touches
# MadireOS-Test or its disk (the disk file it checks is a copy). It closes no
# application and asks for no credential. Everything goes into one new
# folder (-OutDir, by default build\kali-disk-<step>-<date>-<time>): Kali's
# log, the memory samples, showvminfo before and after, a transcript, the
# image (image step) and kali-disk-summary.txt, whose last line is the exit
# status.
#
#   powershell -ExecutionPolicy Bypass -File scripts\kali-disk.ps1 -Step image
#   powershell -ExecutionPolicy Bypass -File scripts\kali-disk.ps1 -Step check `
#       -Vdi <disk file> -VdiSha256 <sha256> -Expect seed -Image <image> -ImageSha256 <sha256>
#   powershell -ExecutionPolicy Bypass -File scripts\kali-disk.ps1 -Step check `
#       -Vdi <used disk file> -VdiSha256 <sha256> -Expect session
param(
    [Parameter(Mandatory = $true)][ValidateSet("image", "check")][string]$Step,
    [string]$Vdi = "",                  # check: the disk file (a copy; never the attached disk)
    [string]$VdiSha256 = "",
    [ValidateSet("", "seed", "session")][string]$Expect = "",
    [string]$Image = "",                # check, optional: the marked image the disk must hold exactly
    [string]$ImageSha256 = "",
    [string]$OutDir = "",               # default: build\kali-disk-<step>-<date>-<time>
    [int]$MinFreeMB = 4500,             # the Kali steps' threshold
    [string]$KaliVM = "kali-linux-2026.2-virtualbox-amd64",
    [string]$KaliUser = "kali",
    [int]$KaliPort = 2222,
    [string]$KaliKey = (Join-Path $env:USERPROFILE ".ssh\madire_kali_ed25519"),
    [string]$KaliKnownHosts = (Join-Path $env:USERPROFILE ".ssh\known_hosts_madire_kali"),
    [string]$VBoxManage = "",
    [string]$TestVM = "MadireOS-Test",  # must not be running
    [string]$ViewVM = "MadireOS-View"   # must not be running
)
$ErrorActionPreference = "Stop"
$root = (Get-Item (Split-Path $PSScriptRoot -Parent)).FullName
$build = Join-Path $root "build"
$stamp = Get-Date -Format "yyyyMMdd-HHmmss"
if (-not $OutDir) { $OutDir = Join-Path $build "kali-disk-$Step-$stamp" }

function Refuse([string]$why) {
    Write-Host "KALI DISK STEP REFUSED: $why"
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

# ---- tools, VMs, inputs (read only) ----
foreach ($tool in @("ssh.exe", "scp.exe")) {
    if (-not (Get-Command $tool -ErrorAction SilentlyContinue)) { Refuse "$tool (Windows OpenSSH) is required" }
}
if (-not (Test-Path -LiteralPath $KaliKey)) { Refuse "SSH key not found: $KaliKey" }
if (-not (Test-Path -LiteralPath $KaliKnownHosts)) { Refuse "pinned host key file not found: $KaliKnownHosts" }

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

# Native commands: stderr is not an error here, the exit code decides.
function Invoke-Native([string]$Exe, [string[]]$Arguments, [switch]$AllowFailure) {
    $old = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    try { $out = & $Exe @Arguments 2>&1; $code = $LASTEXITCODE } finally { $ErrorActionPreference = $old }
    $lines = @($out | ForEach-Object { "$_" })
    if ($code -ne 0 -and -not $AllowFailure) { throw "$Exe $($Arguments -join ' ') failed ($code): $($lines -join ' ')" }
    return [pscustomobject]@{ ExitCode = $code; Output = $lines }
}
function VBox([string[]]$a) { return (Invoke-Native $script:vbox $a).Output }
function Get-RunningVMs { return @(VBox @("list", "runningvms") | ForEach-Object { if ($_ -match '^"(.+)" \{') { $Matches[1] } }) }
function Get-KaliInfo { return @(VBox @("showvminfo", $KaliVM, "--machinereadable")) }
function Get-KaliState { return ((Get-KaliInfo | Where-Object { $_ -match '^VMState=' } | Select-Object -First 1) -replace '^VMState="(.*)"$', '$1') }

$running = Get-RunningVMs
foreach ($vm in @($KaliVM, $TestVM, $ViewVM)) {
    if ($running -contains $vm) { Refuse "the VM '$vm' is running (power it off first; this step starts Kali and powers it off)" }
}
$kaliBefore = Get-KaliInfo
$kaliMem = ($kaliBefore | Where-Object { $_ -match '^memory=' }) -replace '^memory=', ''
if ($kaliMem -ne "3072") { Refuse "Kali's memory is $kaliMem MB (expected 3072; never changed here)" }
# check: the disk file is a copy, never the disk attached to a VM.
$inputs = @()
if ($Step -eq "check") {
    if (-not $Vdi -or -not $VdiSha256 -or -not $Expect) { Refuse "-Step check needs -Vdi, -VdiSha256 and -Expect (seed or session)" }
    if (-not (Test-Path -LiteralPath $Vdi)) { Refuse "not found: $Vdi" }
    $Vdi = (Get-Item -LiteralPath $Vdi).FullName
    $attached = @(VBox @("list", "hdds") | Where-Object { $_ -match '^Location:\s+(.+)$' } | ForEach-Object { $Matches[1].Trim() })
    foreach ($a in $attached) {
        if ([string]::Equals([IO.Path]::GetFullPath($a), $Vdi, [StringComparison]::OrdinalIgnoreCase)) {
            Refuse "$Vdi is a disk registered in VirtualBox (give a copy: the storage group's saved disk, or the pristine copy)"
        }
    }
    $h = (Get-FileHash -LiteralPath $Vdi -Algorithm SHA256).Hash.ToLower()
    if ($h -ne $VdiSha256.ToLower()) { Refuse "the disk file's SHA-256 is $h (expected $VdiSha256)" }
    $inputs += [pscustomobject]@{ Path = $Vdi; Remote = "disk.vdi"; Sha = $h }
    if ($Image) {
        if ($Expect -ne "seed") { Refuse "-Image is for -Expect seed (the pristine marked copy) only" }
        if (-not (Test-Path -LiteralPath $Image)) { Refuse "not found: $Image" }
        $Image = (Get-Item -LiteralPath $Image).FullName
        $h = (Get-FileHash -LiteralPath $Image -Algorithm SHA256).Hash.ToLower()
        if ($h -ne $ImageSha256.ToLower()) { Refuse "the image's SHA-256 is $h (expected $ImageSha256)" }
        $inputs += [pscustomobject]@{ Path = $Image; Remote = "madire-test-disk.img"; Sha = $h }
    }
} elseif ($Vdi -or $Expect -or $Image) {
    Refuse "-Step image takes no -Vdi, -Expect or -Image"
}
if (Test-Path -LiteralPath $OutDir) { Refuse "the result folder $OutDir exists (nothing is overwritten)" }

# The repository files sent to Kali (provenance, report only).
$sent = @("scripts\kali-disk.sh", "scripts\vbox-storage.py", "tests\disk-lib.sh", "tests\disk_image.py",
          "tests\mke2fs.conf", "tests\ext2_tree.py", "tests\ext2_view.py") | ForEach-Object { Join-Path $root $_ }
$head = "unknown"
try { $head = (Invoke-Native "git" @("-C", $root, "rev-parse", "HEAD")).Output[0] } catch {}
$dirty = "unknown"
try { $dirty = @((Invoke-Native "git" (@("-C", $root, "status", "--porcelain", "--") + $sent)).Output | Where-Object { $_ }).Count } catch {}

New-Item -ItemType Directory -Path $OutDir | Out-Null
$OutDir = (Get-Item -LiteralPath $OutDir).FullName
Start-Transcript -Path (Join-Path $OutDir "transcript.txt") | Out-Null
$results = New-Object System.Collections.Generic.List[string]
function Note([string]$s) { Write-Host $s; $script:results.Add($s) }
function Write-Lf([string]$path, [string[]]$lines) {
    [IO.File]::WriteAllText($path, (($lines -join "`n") + "`n"), [Text.UTF8Encoding]::new($false))
}
Write-Lf (Join-Path $OutDir "host-memory.txt") ($memSamples | ForEach-Object { "$_" })
Write-Lf (Join-Path $OutDir "kali-vminfo-before.txt") $kaliBefore
Note "Kali disk step '$Step' (1.4 stage 13; an owner-run step, AGENTS.md section 6)"
Note ("started {0}; {1}" -f (Get-Date -Format "yyyy-MM-dd HH:mm:ss zzz"), $memLine)
Note "Kali: $KaliVM, memory $kaliMem MB; $TestVM and $ViewVM powered off"
foreach ($i in $inputs) { Note "input: $($i.Sha)  $($i.Path) (expected: OK)" }
if ($Step -eq "check") { Note "expected tree: $Expect" }
Note ("repository: HEAD {0}; files sent with local changes: {1}" -f $head, $dirty)
foreach ($f in $sent) { Note ("sent: {0}  {1}" -f (Get-FileHash -LiteralPath $f -Algorithm SHA256).Hash.ToLower(), $f.Substring($root.Length + 1)) }

$sshOptions = @("-i", $KaliKey, "-o", "UserKnownHostsFile=$KaliKnownHosts", "-o", "StrictHostKeyChecking=yes",
                "-o", "BatchMode=yes", "-o", "PasswordAuthentication=no", "-o", "ConnectTimeout=10")
function Ssh([string[]]$a, [switch]$AllowFailure) {
    $args2 = @("-n", "-p", [string]$KaliPort) + $sshOptions + @("-o", "ServerAliveInterval=15", "-o", "ServerAliveCountMax=4", "$KaliUser@127.0.0.1") + $a
    return Invoke-Native "ssh.exe" $args2 -AllowFailure:$AllowFailure
}
function ScpTo([string]$local, [string]$remote) {
    $null = Invoke-Native "scp.exe" (@("-q", "-P", [string]$KaliPort) + $sshOptions + @($local, "${KaliUser}@127.0.0.1:$remote"))
}
function ScpFrom([string]$remote, [string]$local) {
    $null = Invoke-Native "scp.exe" (@("-q", "-P", [string]$KaliPort) + $sshOptions + @("${KaliUser}@127.0.0.1:$remote", $local))
}

$result = "FAIL"
$kaliStarted = $false
try {
    $null = VBox @("startvm", $KaliVM, "--type", "headless")
    $kaliStarted = $true
    Note "Kali started headless"
    $deadline = (Get-Date).AddMinutes(3)
    do {
        $probe = Ssh @("true") -AllowFailure
        if ($probe.ExitCode -eq 0) { break }
        if ((Get-Date) -ge $deadline) { throw "Kali SSH not reachable within 3 minutes: $($probe.Output -join ' ')" }
        Start-Sleep -Seconds 5
    } while ($true)
    Note "Kali reachable over SSH (key login)"
    $remote = "/home/$KaliUser/madire-disk-$Step-$stamp"
    if ((Ssh @("test", "!", "-e", $remote) -AllowFailure).ExitCode -ne 0) { throw "the remote folder $remote exists" }
    $null = Ssh @("mkdir", "--", $remote)
    foreach ($f in $sent) { ScpTo $f "$remote/$(Split-Path $f -Leaf)" }
    foreach ($i in $inputs) { ScpTo $i.Path "$remote/$($i.Remote)" }
    $shArgs = @("bash", "$remote/kali-disk.sh", $Step, $remote)
    if ($Step -eq "check") {
        $shArgs += $Expect
        if ($Image) { $shArgs += "madire-test-disk.img" }
    }
    $run = Ssh $shArgs -AllowFailure
    Write-Lf (Join-Path $OutDir "kali-disk.log") $run.Output
    foreach ($l in $run.Output) {
        if ($l -match '^KDISK_(E2FSPROGS|QEMU_IMG|IMAGE_SHA256|FS_SHA256|IMAGE_SECTORS|VDI_SHA256|COMPARE|REST_ZERO|E2FSCK |STATE|TREE_LINE: TREE|DONE|CHECK_FAILED|.*_FAILED|.*_MISSING|.*_DIFFERS|.*_NOT_CLEAN)') { Note $l }
    }
    if ($run.ExitCode -ne 0 -or -not ($run.Output -contains "KDISK_DONE")) { throw "kali-disk.sh $Step failed (exit $($run.ExitCode)); see kali-disk.log" }
    if ($Step -eq "image") {
        $img = Join-Path $OutDir "madire-test-disk.img"
        ScpFrom "$remote/madire-test-disk.img" $img
        $imgHash = (Get-FileHash -LiteralPath $img -Algorithm SHA256).Hash.ToLower()
        $kaliHash = (@($run.Output | Where-Object { $_ -match '^KDISK_IMAGE_SHA256=' }) -replace '^KDISK_IMAGE_SHA256=', '') | Select-Object -First 1
        if ($imgHash -ne $kaliHash) { throw "the image copied back has SHA-256 $imgHash (Kali: $kaliHash)" }
        Note "marked image: $imgHash  $img ($((Get-Item -LiteralPath $img).Length) bytes; copied back, equal to Kali's)"
    } else {
        $kaliHash = (@($run.Output | Where-Object { $_ -match '^KDISK_VDI_SHA256=' }) -replace '^KDISK_VDI_SHA256=', '') | Select-Object -First 1
        if ($kaliHash -ne $VdiSha256.ToLower()) { throw "the disk file on Kali has SHA-256 $kaliHash (expected $VdiSha256)" }
    }
    $null = Ssh @("rm", "-rf", "--", $remote) -AllowFailure
    $result = "PASS"
} catch {
    Note "error: $($_.Exception.Message)"
} finally {
    if ($kaliStarted) {
        try {
            $null = Invoke-Native $script:vbox @("controlvm", $KaliVM, "acpipowerbutton") -AllowFailure
            $how = "ACPI"
            $until = (Get-Date).AddSeconds(120)
            while ((Get-KaliState) -ne "poweroff" -and (Get-Date) -lt $until) { Start-Sleep -Seconds 3 }
            if ((Get-KaliState) -ne "poweroff") {
                $how = "power off"
                $null = Invoke-Native $script:vbox @("controlvm", $KaliVM, "poweroff") -AllowFailure
                $until = (Get-Date).AddSeconds(60)
                while ((Get-KaliState) -ne "poweroff" -and (Get-Date) -lt $until) { Start-Sleep -Seconds 3 }
            }
            if ((Get-KaliState) -eq "poweroff") { Note "Kali powered off: CONFIRMED ($how)" }
            else { Note "Kali powered off: NOT CONFIRMED"; $result = "FAIL" }
        } catch { Note "Kali power off: error: $($_.Exception.Message)"; $result = "FAIL" }
    }
    try {
        $after = Get-KaliInfo
        Write-Lf (Join-Path $OutDir "kali-vminfo-after.txt") $after
        $diff = @(Compare-Object $kaliBefore $after)
        $other = @($diff | Where-Object { $_.InputObject -notmatch '^VMStateChangeTime=' })
        foreach ($d in $diff) { Note ("showvminfo {0}: {1}" -f $(if ($d.SideIndicator -eq "<=") { "before" } else { "after " }), $d.InputObject) }
        Note ("showvminfo of Kali before and after: {0} lines differ, {1} other than VMStateChangeTime" -f $diff.Count, $other.Count)
        if ($other.Count) { $result = "FAIL" }
    } catch { Note "showvminfo of Kali after: error: $($_.Exception.Message)"; $result = "FAIL" }
    Note "RESULT=$result"
    $results.Add(("exit status {0}" -f $(if ($result -eq "PASS") { 0 } else { 1 })))
    Write-Lf (Join-Path $OutDir "kali-disk-summary.txt") $results.ToArray()
    Write-Host "Summary: $(Join-Path $OutDir 'kali-disk-summary.txt')"
    try { Stop-Transcript | Out-Null } catch {}
}
if ($result -ne "PASS") { exit 1 }
