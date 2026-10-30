# The module ISO for the VirtualBox acceptance (1.3 stage 9; docs/testing.md,
# Test I and "Stage 9's owner-run steps"): packaged on the Kali VM around
# the kernel of the ISO under test by scripts/kali-module-iso.sh, as part 1
# makes its own (the module entry with "textmode: yes"). Nothing is built:
# the kernel is the ISO's own /boot/madire.kernel, checked against
# -KernelSha256, and the module ISO is read back and its kernel checked
# again.
#
# An owner-run step (AGENTS.md section 6): run from plain PowerShell with VS
# Code closed. It refuses to start (one line, "MODULE ISO STEP REFUSED:
# <reason>") unless the available host memory is at or above -MinFreeMB in
# three samples (5 s apart), Kali, MadireOS-Test and MadireOS-View are all
# powered off, Kali's memory is 3,072 MB (never changed here), the ISO has
# its expected SHA-256 and the result folder does not exist yet. It starts
# Kali headless (key login only), and at the end, also after an error,
# powers it off (ACPI, then power off) and confirms it; showvminfo of Kali
# before and after: only VMStateChangeTime may differ. It closes no
# application and asks for no credential. Everything goes into one new
# folder (-OutDir, by default build\kali-module-iso-<date>-<time>):
# madire-os-module.iso, its limine.conf, Kali's log, the memory samples,
# showvminfo before and after, a transcript and module-iso-summary.txt,
# whose last line is the exit status.
#
#   powershell -ExecutionPolicy Bypass -File scripts\kali-module-iso.ps1 `
#       -Iso <ISO under test> -IsoSha256 <sha256> -KernelSha256 <sha256>
param(
    [Parameter(Mandatory = $true)][string]$Iso,
    [Parameter(Mandatory = $true)][string]$IsoSha256,
    [Parameter(Mandatory = $true)][string]$KernelSha256,
    [string]$OutDir = "",               # default: build\kali-module-iso-<date>-<time>
    [int]$MinFreeMB = 4500,             # the Kali steps' threshold (docs/plans/1.3.md, notes item 6)
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
if (-not $OutDir) { $OutDir = Join-Path $build "kali-module-iso-$stamp" }

function Refuse([string]$why) {
    Write-Host "MODULE ISO STEP REFUSED: $why"
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
if (-not (Test-Path -LiteralPath $Iso)) { Refuse "not found: $Iso" }
$Iso = (Get-Item -LiteralPath $Iso).FullName
$isoHash = (Get-FileHash -LiteralPath $Iso -Algorithm SHA256).Hash.ToLower()
if ($isoHash -ne $IsoSha256.ToLower()) { Refuse "the ISO's SHA-256 is $isoHash (expected $IsoSha256)" }
if (Test-Path -LiteralPath $OutDir) { Refuse "the result folder $OutDir exists (nothing is overwritten)" }

# The repository files sent to Kali (provenance, report only).
$sent = @("scripts\kali-module-iso.sh", "tests\qemu-part-1.sh", "tests\qemu-lib.sh") | ForEach-Object { Join-Path $root $_ }
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
Note "module ISO step (1.3 stage 9; an owner-run step, AGENTS.md section 6)"
Note ("started {0}; {1}" -f (Get-Date -Format "yyyy-MM-dd HH:mm:ss zzz"), $memLine)
Note "Kali: $KaliVM, memory $kaliMem MB; $TestVM and $ViewVM powered off"
Note "ISO under test: $isoHash  $Iso (expected: OK)"
Note "kernel expected: $KernelSha256"
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
    $remote = "/home/$KaliUser/madire-module-iso-$stamp"
    if ((Ssh @("test", "!", "-e", $remote) -AllowFailure).ExitCode -ne 0) { throw "the remote folder $remote exists" }
    $null = Ssh @("mkdir", "--", $remote)
    ScpTo $Iso "$remote/madire-os.iso"
    foreach ($f in $sent) { ScpTo $f "$remote/$(Split-Path $f -Leaf)" }
    $run = Ssh @("bash", "$remote/kali-module-iso.sh", $remote, $IsoSha256.ToLower(), $KernelSha256.ToLower()) -AllowFailure
    Write-Lf (Join-Path $OutDir "kali-module-iso.log") $run.Output
    foreach ($l in $run.Output) { if ($l -match '^MODISO_') { Note $l } }
    if ($run.ExitCode -ne 0) { throw "kali-module-iso.sh failed (exit $($run.ExitCode)); see kali-module-iso.log" }
    $moduleIso = Join-Path $OutDir "madire-os-module.iso"
    ScpFrom "$remote/madire-os-module.iso" $moduleIso
    ScpFrom "$remote/out/isomod/boot/limine/limine.conf" (Join-Path $OutDir "limine.conf")
    $modHash = (Get-FileHash -LiteralPath $moduleIso -Algorithm SHA256).Hash.ToLower()
    $kaliHash = (@($run.Output | Where-Object { $_ -match '^MODISO_MODULE_ISO_SHA256=' }) -replace '^MODISO_MODULE_ISO_SHA256=', '') | Select-Object -First 1
    if ($modHash -ne $kaliHash) { throw "the module ISO copied back has SHA-256 $modHash (Kali: $kaliHash)" }
    Note "module ISO: $modHash  $moduleIso (copied back, equal to Kali's)"
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
    Write-Lf (Join-Path $OutDir "module-iso-summary.txt") $results.ToArray()
    Write-Host "Summary: $(Join-Path $OutDir 'module-iso-summary.txt')"
    try { Stop-Transcript | Out-Null } catch {}
}
if ($result -ne "PASS") { exit 1 }
