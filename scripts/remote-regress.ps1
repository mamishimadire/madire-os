[CmdletBinding()]
param(
    # Codespace: the GitHub Codespace (paid). Kali: the local Kali Linux VM in
    # VirtualBox, reached over SSH through its NAT port forward (key login).
    [ValidateSet('Codespace', 'Kali')]
    [string]$Target = 'Codespace',
    [string]$Codespace = 'madire-os-0-8-devcontainer-test-9vrr95qjqj63rg6',
    [string]$KaliVM = 'kali-linux-2026.2-virtualbox-amd64',
    [string]$KaliUser = 'kali',
    [int]$KaliPort = 2222,
    [string]$KaliKey = (Join-Path $env:USERPROFILE '.ssh\madire_kali_ed25519'),
    [string]$KaliKnownHosts = (Join-Path $env:USERPROFILE '.ssh\known_hosts_madire_kali'),
    [string]$VBoxManage = '',
    # 1.3 stage 8: the Kali hand-off (docs/plans/1.3.md, notes item 12 (B);
    # AGENTS.md section 6, owner-run VM steps): run by the owner from plain
    # PowerShell with VS Code closed, Kali only. Instead of the full suite:
    # make all, make unit and make repro (1.3 stage 9: no QEMU boot; Kali's
    # QEMU stalled in part static's boots). Never evidence. Measures the
    # available host memory three
    # times first and refuses unless all three are at or above -MinFreeMB;
    # refuses while MadireOS-Test or MadireOS-View runs and unless Kali's
    # memory is 3,072 MB (never changed); writes a transcript, the evidence
    # and trial-summary.txt (its last line the exit status) into the run's
    # new folder build\regression\<run id>\.
    [switch]$Trial,
    [int]$MinFreeMB = 4500,
    [string]$ViewVM = 'MadireOS-View'
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repoRoot = Split-Path $PSScriptRoot -Parent
# 1.3 stage 8b: third_party/ and THIRD_PARTY_ASSETS.md (the font and the ISO,
# since 1.2) and docs/ (part static's UTF-8 check, make-wallpaper.py's colours).
$snapshotRoots = @('Makefile', 'LICENSE', 'NOTICE', 'THIRD_PARTY_NOTICES', 'THIRD_PARTY_ASSETS.md', 'boot', 'docs', 'drivers', 'include', 'iso', 'kernel', 'linker', 'scripts', 'tests', 'third_party', 'user') |
    Where-Object { Test-Path -LiteralPath (Join-Path $repoRoot $_) }
# Python's bytecode caches are never part of the snapshot (tracked files only).
$snapshotExcludes = @('__pycache__', '*.pyc')
$codespaceTouched = $false
$codespaceAvailableAt = $null
$kaliStarted = $false
$kaliAvailableAt = $null
$guardStartedAt = $null
$guardFinishedAt = $null
$regressionStartedAt = $null
$regressionFinishedAt = $null
$runId = ([DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss')) + '-' + [Guid]::NewGuid().ToString('N').Substring(0, 8)
$remoteRoot = if ($Target -eq 'Kali') { "/home/$KaliUser/madire-regression-$runId" } else { "/workspaces/madire-regression-$runId" }
# The local working folder and the results live under the git-ignored
# build\ folder, never in the system temp folder (a temp clean-up once
# removed a run's results folder while the run was going).
$tempRoot = Join-Path $repoRoot "build\regression\$runId"
$resultsRoot = Join-Path $tempRoot 'results'
$snapshotArchive = Join-Path $tempRoot 'snapshot.tar'
$manifestPath = Join-Path $tempRoot 'snapshot.sha256'
$manifestText = $null
$makeExit = $null

function Invoke-Gh {
    param([Parameter(Mandatory = $true)][string[]]$Arguments, [switch]$AllowFailure)
    $oldPreference = $ErrorActionPreference
    $oldPathConversion = $env:MSYS_NO_PATHCONV
    $ErrorActionPreference = 'Continue'
    $env:MSYS_NO_PATHCONV = '1'
    try {
        $output = & gh @Arguments 2>&1
        $exitCode = $LASTEXITCODE
    }
    finally {
        $ErrorActionPreference = $oldPreference
        if ($null -eq $oldPathConversion) {
            Remove-Item Env:MSYS_NO_PATHCONV -ErrorAction SilentlyContinue
        }
        else {
            $env:MSYS_NO_PATHCONV = $oldPathConversion
        }
    }
    $lines = @($output | ForEach-Object { [string]$_ })
    if ($exitCode -ne 0 -and -not $AllowFailure) {
        throw "gh $($Arguments -join ' ') failed ($exitCode): $($lines -join [Environment]::NewLine)"
    }
    return [pscustomobject]@{ ExitCode = $exitCode; Output = $lines }
}

function Invoke-Native {
    param([Parameter(Mandatory = $true)][string]$Exe, [Parameter(Mandatory = $true)][string[]]$Arguments,
          [switch]$AllowFailure)
    $oldPreference = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        $output = & $Exe @Arguments 2>&1
        $exitCode = $LASTEXITCODE
    }
    finally {
        $ErrorActionPreference = $oldPreference
    }
    $lines = @($output | ForEach-Object { [string]$_ })
    if ($exitCode -ne 0 -and -not $AllowFailure) {
        throw "$Exe $($Arguments -join ' ') failed ($exitCode): $($lines -join [Environment]::NewLine)"
    }
    return [pscustomobject]@{ ExitCode = $exitCode; Output = $lines }
}

# Kali: key login only (no password ever), the host key pinned in its own
# known_hosts file.
function Get-KaliSshOptions {
    return @('-i', $KaliKey, '-o', "UserKnownHostsFile=$KaliKnownHosts", '-o', 'StrictHostKeyChecking=yes',
             '-o', 'BatchMode=yes', '-o', 'PasswordAuthentication=no', '-o', 'ConnectTimeout=10')
}

# -Poll (1.3 stage 9; Kali): the poll's ssh reads no stdin (-n) and ends a
# connection that stops answering (ServerAliveInterval/ServerAliveCountMax:
# after about 60 s) instead of waiting on it.
function Invoke-Remote {
    param([Parameter(Mandatory = $true)][string[]]$RemoteArguments, [switch]$AllowFailure, [switch]$Poll)
    if ($Target -eq 'Kali') {
        $pollOptions = @()
        if ($Poll) { $pollOptions = @('-n', '-o', 'ServerAliveInterval=15', '-o', 'ServerAliveCountMax=4') }
        $arguments = @('-p', [string]$KaliPort) + (Get-KaliSshOptions) + $pollOptions + @("$KaliUser@127.0.0.1") + $RemoteArguments
        return Invoke-Native -Exe 'ssh.exe' -Arguments $arguments -AllowFailure:$AllowFailure
    }
    $arguments = @('codespace', 'ssh', '-c', $Codespace, '--') + $RemoteArguments
    return Invoke-Gh -Arguments $arguments -AllowFailure:$AllowFailure
}

function Copy-ToRemote {
    param([string]$LocalPath, [string]$RemotePath)
    if ($Target -eq 'Kali') {
        $arguments = @('-q', '-P', [string]$KaliPort) + (Get-KaliSshOptions) + @($LocalPath, "${KaliUser}@127.0.0.1:$RemotePath")
        return Invoke-Native -Exe 'scp.exe' -Arguments $arguments
    }
    return Invoke-Gh -Arguments @('codespace', 'cp', '-e', '-c', $Codespace, $LocalPath, "remote:$RemotePath")
}

function Copy-FromRemote {
    param([string]$RemotePath, [string]$LocalPath, [switch]$Recursive)
    if ($Target -eq 'Kali') {
        $arguments = @('-q', '-P', [string]$KaliPort) + (Get-KaliSshOptions)
        if ($Recursive) { $arguments += '-r' }
        $arguments += @("${KaliUser}@127.0.0.1:$RemotePath", $LocalPath)
        return Invoke-Native -Exe 'scp.exe' -Arguments $arguments
    }
    $arguments = @('codespace', 'cp', '-e')
    if ($Recursive) { $arguments += '-r' }
    $arguments += @('-c', $Codespace, "remote:$RemotePath", $LocalPath)
    return Invoke-Gh -Arguments $arguments
}

# ---- VirtualBox (Kali target) ----
function Find-VBoxManage {
    if ($VBoxManage) { return $VBoxManage }
    $dirs = @()
    if ($env:VBOX_MSI_INSTALL_PATH) { $dirs += $env:VBOX_MSI_INSTALL_PATH }
    try { $dirs += (Get-ItemProperty 'HKLM:\SOFTWARE\Oracle\VirtualBox' -ErrorAction Stop).InstallDir } catch {}
    if ($env:ProgramFiles) { $dirs += (Join-Path $env:ProgramFiles 'Oracle\VirtualBox') }
    foreach ($d in $dirs) {
        $exe = Join-Path $d 'VBoxManage.exe'
        if (Test-Path $exe) { return $exe }
    }
    throw 'VBoxManage.exe not found (set VBOX_MSI_INSTALL_PATH or pass -VBoxManage).'
}

function Get-RunningVMs {
    $r = Invoke-Native -Exe $script:vbox -Arguments @('list', 'runningvms')
    return @($r.Output | ForEach-Object { if ($_ -match '^"(.+)" \{') { $Matches[1] } })
}

function Get-VMState([string]$name) {
    $r = Invoke-Native -Exe $script:vbox -Arguments @('showvminfo', $name, '--machinereadable')
    $line = $r.Output | Where-Object { $_ -match '^VMState=' } | Select-Object -First 1
    return ($line -replace '^VMState="(.*)"$', '$1')
}

function Wait-KaliShutdown {
    param([int]$TimeoutSeconds)
    $deadline = [DateTimeOffset]::UtcNow.AddSeconds($TimeoutSeconds)
    do {
        if ((Get-VMState $KaliVM) -eq 'poweroff') { return $true }
        Start-Sleep -Seconds 3
    } while ([DateTimeOffset]::UtcNow -lt $deadline)
    return $false
}

function Get-CodespaceInfo {
    $query = '{state: .state, machine: .machine.name, cpus: .machine.cpus}'
    $response = Invoke-Gh -Arguments @('api', "/user/codespaces/$Codespace", '--jq', $query)
    return (($response.Output -join [Environment]::NewLine) | ConvertFrom-Json)
}

function Get-HashLines {
    param([string]$Root, [string[]]$IncludeRoots)
    $rootFull = [IO.Path]::GetFullPath($Root).TrimEnd([IO.Path]::DirectorySeparatorChar, [IO.Path]::AltDirectorySeparatorChar)
    $relativePaths = New-Object 'System.Collections.Generic.List[string]'
    foreach ($relativeRoot in $IncludeRoots) {
        $full = Join-Path $rootFull $relativeRoot
        if (-not (Test-Path -LiteralPath $full)) { continue }
        if (Test-Path -LiteralPath $full -PathType Leaf) {
            $relativePaths.Add($relativeRoot.Replace('\', '/'))
            continue
        }
        Get-ChildItem -LiteralPath $full -File -Recurse -Force | ForEach-Object {
            $relative = $_.FullName.Substring($rootFull.Length + 1).Replace('\', '/')
            if ($relative -match '(^|/)__pycache__/' -or $relative -match '\.pyc$') { return }
            $relativePaths.Add($relative)
        }
    }
    if ($relativePaths.Count -eq 0) { throw "No source files found under $Root" }
    $sorted = $relativePaths.ToArray()
    [Array]::Sort($sorted, [StringComparer]::Ordinal)
    $lines = New-Object 'System.Collections.Generic.List[string]'
    foreach ($relative in $sorted) {
        $full = Join-Path $rootFull ($relative.Replace('/', [IO.Path]::DirectorySeparatorChar))
        $hash = (Get-FileHash -LiteralPath $full -Algorithm SHA256).Hash.ToLowerInvariant()
        $lines.Add("$hash  ./$relative")
    }
    return $lines.ToArray()
}

function Write-LfUtf8 {
    param([string]$Path, [string]$Text)
    $normalized = $Text.Replace(([string][char]13 + [char]10), [string][char]10).Replace([string][char]13, [string][char]10)
    [IO.File]::WriteAllText($Path, $normalized, [Text.UTF8Encoding]::new($false))
}

function Wait-CodespaceShutdown {
    param([int]$TimeoutSeconds = 180)
    $deadline = [DateTimeOffset]::UtcNow.AddSeconds($TimeoutSeconds)
    do {
        $info = Get-CodespaceInfo
        if ($info.state -eq 'Shutdown') { return $true }
        Start-Sleep -Seconds 5
    } while ([DateTimeOffset]::UtcNow -lt $deadline)
    return $false
}

if ($Trial -and $Target -ne 'Kali') { throw '-Trial runs on the Kali target only (-Target Kali).' }
if ($Target -eq 'Codespace') {
    if (-not (Get-Command gh -ErrorAction SilentlyContinue)) { throw 'GitHub CLI (gh) is required.' }
}
else {
    foreach ($tool in @('ssh.exe', 'scp.exe')) {
        if (-not (Get-Command $tool -ErrorAction SilentlyContinue)) { throw "$tool (Windows OpenSSH) is required." }
    }
    if (-not (Test-Path -LiteralPath $KaliKey)) { throw "SSH key not found: $KaliKey" }
    if (-not (Test-Path -LiteralPath $KaliKnownHosts)) { throw "Pinned host key file not found: $KaliKnownHosts" }
    $script:vbox = Find-VBoxManage
    # Rule: never a local regression while the acceptance VM runs.
    if ((Get-RunningVMs) -contains 'MadireOS-Test') { throw 'MadireOS-Test is running: a Kali regression is refused while it runs.' }
    if ($Trial) {
        # 1.3 stage 8: the owner-run trial's own checks, before anything starts.
        $trialMem = @()
        for ($i = 0; $i -lt 3; $i++) {
            if ($i) { Start-Sleep -Seconds 5 }
            $trialMem += [int]((Get-CimInstance Win32_OperatingSystem).FreePhysicalMemory / 1KB)
        }
        $trialMemLine = "host memory available: {0} MB (threshold {1} MB)" -f ($trialMem -join ' / '), $MinFreeMB
        Write-Host "TRIAL_$trialMemLine"
        if (@($trialMem | Where-Object { $_ -lt $MinFreeMB }).Count) {
            throw "TRIAL REFUSED: $trialMemLine; a sample is below the threshold (nothing started, no application closed)."
        }
        if ((Get-RunningVMs) -contains $ViewVM) { throw "TRIAL REFUSED: $ViewVM is running (power it off first)." }
        $kaliInfo = Invoke-Native -Exe $script:vbox -Arguments @('showvminfo', $KaliVM, '--machinereadable')
        $kaliMem = ($kaliInfo.Output | Where-Object { $_ -match '^memory=' }) -replace '^memory=', ''
        if ($kaliMem -ne '3072') { throw "TRIAL REFUSED: Kali's memory is $kaliMem MB (expected 3072; never changed here)." }
        Write-Host "TRIAL_KALI_MEMORY_MB=$kaliMem"
    }
}
if (-not (Get-Command tar.exe -ErrorAction SilentlyContinue)) { throw 'tar.exe is required to create the source snapshot.' }
if (-not (Test-Path -LiteralPath (Join-Path $repoRoot 'Makefile'))) { throw "Not a Madire OS repository: $repoRoot" }

New-Item -ItemType Directory -Path $tempRoot | Out-Null
New-Item -ItemType Directory -Path $resultsRoot | Out-Null
# 1.3 stage 8: the trial keeps everything it prints in its own folder.
$trialResult = 'FAIL'
$makeCommand = 'make unit test iso'
if ($Trial) {
    $makeCommand = 'make all && make unit && make repro'
    Start-Transcript -Path (Join-Path $tempRoot 'trial-transcript.txt') | Out-Null
    Write-Host "TRIAL_STARTED=$([DateTimeOffset]::Now.ToString('yyyy-MM-dd HH:mm:ss zzz')) command='$makeCommand'"
    Write-Host "TRIAL_$trialMemLine"
}

try {
    Push-Location $repoRoot
    try {
        $manifestLines = @(Get-HashLines -Root $repoRoot -IncludeRoots $snapshotRoots)
        $manifestText = [string]::Join([string][char]10, $manifestLines) + [char]10
        Write-LfUtf8 -Path $manifestPath -Text $manifestText
        $manifestBytes = [IO.File]::ReadAllBytes($manifestPath)
        if ($manifestBytes.Length -ge 3 -and $manifestBytes[0] -eq 0xEF -and $manifestBytes[1] -eq 0xBB -and $manifestBytes[2] -eq 0xBF) {
            throw 'Snapshot manifest has a UTF-8 BOM.'
        }
        if ($manifestBytes -contains 0x0D) { throw 'Snapshot manifest contains CR bytes; LF is required.' }

        $tarArguments = @('-cf', $snapshotArchive, '-C', $repoRoot) + @($snapshotExcludes | ForEach-Object { "--exclude=$_" }) + $snapshotRoots
        $tarOutput = & tar.exe @tarArguments 2>&1
        if ($LASTEXITCODE -ne 0) { throw "Could not create source snapshot: $($tarOutput -join ' ')" }

        $verifyRoot = Join-Path $tempRoot 'archive-check'
        New-Item -ItemType Directory -Path $verifyRoot | Out-Null
        $extractOutput = & tar.exe -xf $snapshotArchive -C $verifyRoot 2>&1
        if ($LASTEXITCODE -ne 0) { throw "Could not inspect source snapshot: $($extractOutput -join ' ')" }
        $archiveLines = @(Get-HashLines -Root $verifyRoot -IncludeRoots @($snapshotRoots))
        if (([string]::Join([string][char]10, $archiveLines) + [char]10) -cne $manifestText) {
            throw 'The snapshot archive does not match its LF SHA-256 manifest.'
        }
        Write-Host "SNAPSHOT_HASHES=$($manifestLines.Count)"
        Write-Host 'LOCAL_ARCHIVE_MANIFEST_MATCH=PASS'

        if ($Target -eq 'Codespace') {
            $info = Get-CodespaceInfo
            if ([int]$info.cpus -ne 2) { throw "Codespace must be 2-core; found $($info.machine) with $($info.cpus) CPUs." }
            if ($info.state -ne 'Shutdown') { throw "Codespace must be Shutdown before the regression; found $($info.state)." }
            $codespaceTouched = $true
            $null = Invoke-Gh -Arguments @('api', '-X', 'POST', "/user/codespaces/$Codespace/start")

            $startDeadline = [DateTimeOffset]::UtcNow.AddMinutes(5)
            do {
                $info = Get-CodespaceInfo
                if ($info.state -eq 'Available') { break }
                if ([DateTimeOffset]::UtcNow -ge $startDeadline) { throw 'Timed out waiting for Codespace startup.' }
                Start-Sleep -Seconds 5
            } while ($true)
            if ([int]$info.cpus -ne 2) { throw "Codespace changed machine size: $($info.machine), $($info.cpus) CPUs." }
            $codespaceAvailableAt = [DateTimeOffset]::UtcNow
            Write-Host "CODESPACE_AVAILABLE=$($codespaceAvailableAt.ToString('u')) machine=$($info.machine) cpus=$($info.cpus)"
        }
        else {
            # Start Kali headless unless it is already running (then it is
            # left running at the end, so an interactive session survives).
            if ((Get-RunningVMs) -contains $KaliVM) {
                Write-Host "KALI_VM=ALREADY_RUNNING (left running afterwards)"
            }
            else {
                $null = Invoke-Native -Exe $script:vbox -Arguments @('startvm', $KaliVM, '--type', 'headless')
                $kaliStarted = $true
                Write-Host "KALI_VM=STARTED_HEADLESS"
            }
            $sshDeadline = [DateTimeOffset]::UtcNow.AddMinutes(3)
            do {
                $probe = Invoke-Remote -RemoteArguments @('true') -AllowFailure
                if ($probe.ExitCode -eq 0) { break }
                if ([DateTimeOffset]::UtcNow -ge $sshDeadline) { throw "Kali SSH not reachable: $($probe.Output -join ' ')" }
                Start-Sleep -Seconds 5
            } while ($true)
            $kaliAvailableAt = [DateTimeOffset]::UtcNow
            $vmInfo = Invoke-Native -Exe $script:vbox -Arguments @('showvminfo', $KaliVM, '--machinereadable')
            $cpus = ($vmInfo.Output | Where-Object { $_ -match '^cpus=' }) -replace '^cpus=', ''
            $mem = ($vmInfo.Output | Where-Object { $_ -match '^memory=' }) -replace '^memory=', ''
            Write-Host "KALI_AVAILABLE=$($kaliAvailableAt.ToString('u')) vm=$KaliVM cpus=$cpus memory_mb=$mem"
        }

        $remoteCreate = Invoke-Remote -RemoteArguments @('test', '!', '-e', $remoteRoot) -AllowFailure
        if ($remoteCreate.ExitCode -ne 0) { throw "Remote run directory already exists: $remoteRoot" }
        $null = Invoke-Remote -RemoteArguments @('mkdir', '--', $remoteRoot)

        $preflightBody = @'
#!/usr/bin/env bash
set -euo pipefail
root=__ROOT__
exec > "$root/preflight.txt" 2>&1
if ps -eo pid=,comm=,args= | awk '$2 == "make" || $2 ~ /^qemu-system-x86_64/ { print; found=1 } END { exit !found }'; then
    echo ABORTED_REGRESSION_PROCESS_FOUND
    exit 40
fi
for pid in /proc/[0-9]*; do
    cwd=$(readlink -f "$pid/cwd" 2>/dev/null || true)
    case "$cwd" in
        /workspaces/madire-os-build*|/tmp/madire-os-build*) echo "ABORTED_ATTEMPT_CWD=$pid:$cwd"; exit 41 ;;
    esac
done
for path in "$root/source" "$root/altered" "$root/regression.log"; do
    if [ -e "$path" ]; then echo "FRESH_PATH_ALREADY_EXISTS=$path"; exit 42; fi
done
echo ABORTED_REGRESSION_PROCESS=NONE
echo FRESH_REMOTE_SNAPSHOT=ABSENT
for tool in gcc ld nasm limine xorriso qemu-system-x86_64 qemu-system-i386 python3 make objdump nm; do
    if ! command -v "$tool" >/dev/null 2>&1; then echo "MISSING_TOOL=$tool"; exit 38; fi
done
echo "TOOL_GCC=$(gcc --version | head -n 1)"
echo "TOOL_LD=$(ld --version | head -n 1)"
echo "TOOL_NASM=$(nasm -v)"
echo "TOOL_LIMINE=$(limine --version | head -n 1)"
echo "TOOL_QEMU=$(qemu-system-x86_64 --version | head -n 1)"
echo "TOOL_PYTHON=$(python3 --version)"
echo "TOOL_OS=$(. /etc/os-release && echo "$PRETTY_NAME")"
avail_kb=$(df -Pk "$root" | awk 'NR == 2 { print $4 }')
echo "DISK_FREE_KB=$avail_kb"
if [ "$avail_kb" -lt 2097152 ]; then echo DISK_FREE_BELOW_2_GIB; exit 39; fi
echo REMOTE_PREFLIGHT_PASS
'@
        $guardBody = @'
#!/usr/bin/env bash
set -euo pipefail
target=$1
manifest=$2
output=$3
cd "$target"
find . -type f -print0 | LC_ALL=C sort -z | xargs -0 sha256sum > "$output"
set +e
cmp -s "$manifest" "$output"
cmp_status=$?
set -e
if [ "$cmp_status" -ne 0 ]; then
    echo HASH_GUARD_MISMATCH
    diff -u "$manifest" "$output" | head || true
    exit 43
fi
echo HASH_COMPARE_PASSED
'@
        $alteredBody = @'
#!/usr/bin/env bash
set -euo pipefail
root=__ROOT__
exec > "$root/altered-proof.txt" 2>&1
mkdir "$root/altered"
tar -xf "$root/input.tar" -C "$root/altered"
printf X >> "$root/altered/Makefile"
set +e
(
    bash "$root/guard.sh" "$root/altered" "$root/snapshot.sha256" "$root/altered.sha256"
    guard_rc=$?
    if [ "$guard_rc" -ne 0 ]; then
        echo "ALTERED_GUARD_EXIT=$guard_rc"
        exit "$guard_rc"
    fi
    echo ALTERED_PROBE_AFTER_GUARD
) > "$root/altered-guard-output.txt" 2>&1
probe_rc=$?
set -e
cat "$root/altered-guard-output.txt"
if [ "$probe_rc" -eq 0 ]; then
    echo ALTERED_COPY_WAS_ACCEPTED
    exit 44
fi
if grep -q HASH_COMPARE_PASSED "$root/altered-guard-output.txt"; then exit 45; fi
if ! grep -q HASH_GUARD_MISMATCH "$root/altered-guard-output.txt"; then exit 46; fi
if grep -q ALTERED_PROBE_AFTER_GUARD "$root/altered-guard-output.txt"; then exit 47; fi
if ! grep -q '^ALTERED_GUARD_EXIT=43$' "$root/altered-guard-output.txt"; then exit 48; fi
echo "ALTERED_COPY_REJECTED_AND_HALTED exit=$probe_rc; AFTER_GUARD=NOT_RUN"
'@
        $correctBody = @'
#!/usr/bin/env bash
set -euo pipefail
root=__ROOT__
exec > "$root/correct-guard.txt" 2>&1
mkdir "$root/source"
tar -xf "$root/input.tar" -C "$root/source"
bash "$root/guard.sh" "$root/source" "$root/snapshot.sha256" "$root/correct.sha256"
touch "$root/guard-ok"
echo CORRECT_SNAPSHOT_GUARD_PASS
'@
        $runnerBody = @'
#!/usr/bin/env bash
root=__ROOT__
cd "$root/source" || exit 90
set +e
bash -c '__MAKE__' > "$root/regression.log" 2>&1
rc=$?
printf '\nMAKE EXIT %s\n' "$rc" >> "$root/regression.log"
exit "$rc"
'@
        $startBody = @'
#!/usr/bin/env bash
set -euo pipefail
root=__ROOT__
test -f "$root/guard-ok"
test ! -e "$root/regression.log"
nohup bash "$root/runner.sh" </dev/null >/dev/null 2>&1 &
pid=$!
printf '%s\n' "$pid" > "$root/runner.pid"
echo "STAGE5_REGRESSION_STARTED pid=$pid"
'@
        $pollBody = @'
#!/usr/bin/env bash
set -euo pipefail
root=__ROOT__
if [ ! -f "$root/regression.log" ]; then echo REGRESSION_LOG_PENDING; exit 0; fi
tail -n 1 "$root/regression.log"
'@
        $finalHashBody = @'
#!/usr/bin/env bash
set -euo pipefail
root=__ROOT__
cd "$root/source"
find . -type f ! -path './build/*' -print0 | LC_ALL=C sort -z | xargs -0 sha256sum > "$root/final.sha256"
if ! cmp -s "$root/snapshot.sha256" "$root/final.sha256"; then
    echo FINAL_SNAPSHOT_HASH_MISMATCH
    diff -u "$root/snapshot.sha256" "$root/final.sha256" || true
    exit 49
fi
echo FINAL_SNAPSHOT_HASH_MATCH=PASS
'@
        $checkBody = @'
#!/usr/bin/env bash
set -euo pipefail
root=__ROOT__
cd "$root/source"
log="$root/regression.log"
echo "== regression summary =="
grep -E 'ALL TESTS|FAILED|MAKE EXIT|Reproducible build:' "$log" || true
echo "== unit suite PASS lines =="
grep -- '-> PASS' "$log" || true
echo "UNIT_SUITE_COUNT=$(grep -c -- '-> PASS' "$log" || true)"
tag=$(sed -n 's/^#define MADIRE_OS_VERSION "\(.*\)"$/\1/p' kernel/kernel.h)
echo "== [$tag] result lines =="
grep -F "PASS: [$tag]" "$log" | tail -20 || true
if grep -Eiq 'iso/boot/limine/limine\.conf.*(no such file|cannot stat|not found)|no such file.*limine\.conf' "$log"; then
    echo REPRO_BOOT_CFG_NOTICES=FOUND
    exit 50
fi
echo REPRO_BOOT_CFG_NOTICES=NONE
nm -u build/madire.elf > "$root/undefined.txt"
if [ -s "$root/undefined.txt" ]; then cat "$root/undefined.txt"; exit 51; fi
echo UNDEFINED_SYMBOLS=NONE
find build -name '*.o' -not -path 'build/unit/*' -not -path 'build/repro*' -print0 |
    while IFS= read -r -d '' object; do nm -u "$object"; done |
    grep -E '__(udiv|div|mod|umod|mul|popcount|stack_chk)|^ *U (memset|memcpy|memmove|memcmp|strlen)$' |
    sort -u > "$root/helpers.txt" || true
if [ -s "$root/helpers.txt" ]; then cat "$root/helpers.txt"; exit 52; fi
echo LIBGCC_LIBC_HELPERS=NONE
sse=$(objdump -d build/madire.elf | grep -cE '%(x|y|z)mm[0-9]|%st(\(|$)|%mm[0-9]' || true)
echo "SSE_X87_REGISTER_USES=$sse"
if [ "$sse" -ne 0 ]; then exit 53; fi
size build/madire.elf
symbol=$(nm -n build/madire.elf | awk '$3 == "__kernel_end" {print $1; exit}')
physical=$(python3 - "$symbol" <<'PY'
import sys
print(int(sys.argv[1], 16) - 0xffffffff80000000)
PY
)
margin=$((0x400000 - physical))
two=$((0x200000 - physical))
printf 'KERNEL_END_VIRTUAL=0x%s\nKERNEL_END_PHYSICAL=0x%x\nKERNEL_MARGIN_BYTES=%d\nTWO_MIB_MARGIN_BYTES=%d (report only)\n' "$symbol" "$physical" "$margin" "$two"
if [ "$margin" -lt 32768 ]; then echo KERNEL_MARGIN=STOP; exit 54; fi
echo KERNEL_MARGIN=PASS
'@
        $evidenceBody = @'
#!/usr/bin/env bash
set -euo pipefail
root=__ROOT__
cd "$root/source/build"
tar -cf "$root/stage5-evidence.tar" serial-program-output.log screen-program-*.txt
echo STAGE5_QMP_SCREEN_EVIDENCE=PACKED
# Replay every pattern-helper call under mawk and gawk, old (-v) and new
# (ENVIRON) helpers (tests/helper-replay.sh; evidence only, decides no check).
command -v gawk >/dev/null || sudo apt-get install -y -qq gawk >/dev/null 2>&1 || true
command -v mawk >/dev/null || sudo apt-get install -y -qq mawk >/dev/null 2>&1 || true
(cd "$root/source" && bash tests/helper-replay.sh mawk gawk) || echo REPLAY_FAILED
# Every per-boot serial log, the key evidence, the stall captures and the
# helper trace and replay (for scripts/compare-serial.py and later diagnosis).
shopt -s nullglob
tar -czf "$root/boot-logs.tar.gz" serial-*.log *.keys.txt *.irq1.txt helper-calls.trace helper-logs replay-*.txt $( [ -d stall ] && echo stall )
echo "BOOT_LOGS_PACKED=$(tar -tzf "$root/boot-logs.tar.gz" | wc -l) files"
'@

        # 1.3 stage 9: the trial's evidence: make repro's hashes and result.
        $trialEvidenceBody = @'
#!/usr/bin/env bash
set -euo pipefail
root=__ROOT__
cd "$root/source/build"
tar -cf "$root/trial-evidence.tar" repro/sha256.txt repro/result
echo "TRIAL_EVIDENCE_PACKED=$(tar -tf "$root/trial-evidence.tar" | wc -l) files"
'@

        $remoteFiles = [ordered]@{
            'preflight.sh' = $preflightBody
            'guard.sh' = $guardBody
            'altered-proof.sh' = $alteredBody
            'correct-guard.sh' = $correctBody
            'runner.sh' = $runnerBody
            'start.sh' = $startBody
            'poll.sh' = $pollBody
            'final-hash.sh' = $finalHashBody
            'checks.sh' = $checkBody
            'evidence.sh' = $evidenceBody
            'trial-evidence.sh' = $trialEvidenceBody
        }
        foreach ($name in @($remoteFiles.Keys)) {
            $body = $remoteFiles[$name].Replace('__ROOT__', $remoteRoot).Replace('__RUN__', $runId).Replace('__MAKE__', $makeCommand)
            $localScript = Join-Path $tempRoot $name
            Write-LfUtf8 -Path $localScript -Text $body
            $null = Copy-ToRemote -LocalPath $localScript -RemotePath "$remoteRoot/$name"
        }
        $null = Copy-ToRemote -LocalPath $snapshotArchive -RemotePath "$remoteRoot/input.tar"
        $null = Copy-ToRemote -LocalPath $manifestPath -RemotePath "$remoteRoot/snapshot.sha256"

        $null = Invoke-Remote -RemoteArguments @('bash', "$remoteRoot/preflight.sh")
        $preflightResult = Invoke-Remote -RemoteArguments @('cat', "$remoteRoot/preflight.txt")
        $preflightText = $preflightResult.Output -join [Environment]::NewLine
        Write-Host $preflightText
        if ($preflightText -notmatch 'REMOTE_PREFLIGHT_PASS') { throw 'Remote preflight did not pass.' }

        $guardStartedAt = [DateTimeOffset]::UtcNow
        $null = Invoke-Remote -RemoteArguments @('bash', "$remoteRoot/altered-proof.sh")
        $alteredResult = Invoke-Remote -RemoteArguments @('cat', "$remoteRoot/altered-proof.txt")
        $alteredText = $alteredResult.Output -join [Environment]::NewLine
        Write-Host '--- altered-copy guard proof ---'
        Write-Host $alteredText
        if ($alteredText -notmatch 'HASH_GUARD_MISMATCH' -or
            $alteredText -notmatch 'ALTERED_GUARD_EXIT=43' -or
            $alteredText -notmatch 'ALTERED_COPY_REJECTED_AND_HALTED' -or
            $alteredText -match 'ALTERED_PROBE_AFTER_GUARD') {
            throw 'The altered snapshot did not fail and halt at the hash guard.'
        }

        $null = Invoke-Remote -RemoteArguments @('bash', "$remoteRoot/correct-guard.sh")
        $correctResult = Invoke-Remote -RemoteArguments @('cat', "$remoteRoot/correct-guard.txt")
        $correctText = $correctResult.Output -join [Environment]::NewLine
        Write-Host '--- correct-copy guard proof ---'
        Write-Host $correctText
        if ($correctText -notmatch 'HASH_COMPARE_PASSED' -or
            $correctText -notmatch 'CORRECT_SNAPSHOT_GUARD_PASS') {
            throw 'Correct snapshot did not pass the hash guard.'
        }
        $guardFinishedAt = [DateTimeOffset]::UtcNow
        Write-Host "HASH_GUARD_PROOF_SECONDS=$([math]::Round(($guardFinishedAt - $guardStartedAt).TotalSeconds))"

        $null = Invoke-Remote -RemoteArguments @('bash', "$remoteRoot/start.sh")
        $regressionStartedAt = [DateTimeOffset]::UtcNow
        Write-Host "REGRESSION_STARTED=$($regressionStartedAt.ToString('u')) command='$makeCommand'"

        $deadline = [DateTimeOffset]::UtcNow.AddMinutes(90)
        do {
            Start-Sleep -Seconds 60
            $poll = Invoke-Remote -RemoteArguments @('bash', "$remoteRoot/poll.sh") -Poll
            $lastLine = ([string]($poll.Output | Select-Object -Last 1)).Trim()
            Write-Host "[$([DateTimeOffset]::UtcNow.ToString('u'))] $lastLine"
            if ($lastLine -match '^MAKE EXIT (-?\d+)$') {
                $makeExit = [int]$Matches[1]
                break
            }
            if ([DateTimeOffset]::UtcNow -ge $deadline) { throw 'Regression did not finish within 90 minutes.' }
        } while ($true)
        $regressionFinishedAt = [DateTimeOffset]::UtcNow
        Write-Host "REGRESSION_SECONDS=$([math]::Round(($regressionFinishedAt - $regressionStartedAt).TotalSeconds))"

        Copy-FromRemote -RemotePath "$remoteRoot/regression.log" -LocalPath (Join-Path $resultsRoot 'regression.log') | Out-Null
        $null = Invoke-Remote -RemoteArguments @('bash', "$remoteRoot/final-hash.sh")
        Copy-FromRemote -RemotePath "$remoteRoot/final.sha256" -LocalPath (Join-Path $resultsRoot 'final.sha256') | Out-Null
        $checks = Invoke-Remote -RemoteArguments @('bash', "$remoteRoot/checks.sh")
        $checks.Output | Where-Object { $_ -match '^(REPRO_BOOT|UNDEFINED|LIBGCC|SSE_X87|KERNEL_END|KERNEL_MARGIN|TWO_MIB)' } | ForEach-Object { Write-Host $_ }
        if ($Trial) {
            # 1.3 stage 9: the trial's evidence (make repro's hashes).
            $evidence = Invoke-Remote -RemoteArguments @('bash', "$remoteRoot/trial-evidence.sh")
            $evidence.Output | ForEach-Object { Write-Host $_ }
            Copy-FromRemote -RemotePath "$remoteRoot/trial-evidence.tar" -LocalPath (Join-Path $resultsRoot 'trial-evidence.tar') | Out-Null
            $trialRoot = Join-Path $resultsRoot 'trial-evidence'
            New-Item -ItemType Directory -Path $trialRoot | Out-Null
            $null = & tar.exe -xf (Join-Path $resultsRoot 'trial-evidence.tar') -C $trialRoot
            if ($LASTEXITCODE -ne 0) { throw 'Could not unpack the downloaded trial evidence.' }
        }
        else {
        $evidence = Invoke-Remote -RemoteArguments @('bash', "$remoteRoot/evidence.sh")
        $evidence.Output | ForEach-Object { Write-Host $_ }
        Copy-FromRemote -RemotePath "$remoteRoot/stage5-evidence.tar" -LocalPath (Join-Path $resultsRoot 'stage5-evidence.tar') | Out-Null
        Copy-FromRemote -RemotePath "$remoteRoot/boot-logs.tar.gz" -LocalPath (Join-Path $resultsRoot 'boot-logs.tar.gz') | Out-Null
        }
        # Keep the per-boot log tars of the last 3 runs only (0.12, Q9).
        $runs = @(Get-ChildItem -LiteralPath (Join-Path $repoRoot 'build\regression') -Directory | Sort-Object Name)
        if ($runs.Count -gt 3) {
            foreach ($old in $runs[0..($runs.Count - 4)]) {
                $tarPath = Join-Path $old.FullName 'results\boot-logs.tar.gz'
                if (Test-Path -LiteralPath $tarPath) { Remove-Item -LiteralPath $tarPath -Force }
            }
        }

        $downloadedFinal = [IO.File]::ReadAllText((Join-Path $resultsRoot 'final.sha256'))
        if ($downloadedFinal -cne $manifestText) { throw 'Final remote source hashes differ from the snapshot manifest.' }
        Write-Host 'FINAL_SNAPSHOT_HASH_MATCH=PASS'

        if ($Trial) {
            # 1.3 stage 8: the trial's own result (never evidence).
            $logText = [IO.File]::ReadAllText((Join-Path $resultsRoot 'regression.log'))
            Write-Host "TRIAL_MAKE_EXIT=$makeExit"
            Write-Host "TRIAL_UNIT_SUITES_PASS=$(@([regex]::Matches($logText, '(?m)^.*-> PASS$')).Count)"
            Write-Host "TRIAL_REPRO=$([regex]::Match($logText, '(?m)^Reproducible build: [^\r\n]*').Value)"
            if ($makeExit -ne 0) { throw "Trial failed: MAKE EXIT $makeExit." }
            if ($logText -notmatch '(?m)^Reproducible build: identical$') { throw 'Trial: the reproducibility check did not pass.' }
            $trialResult = 'PASS'
            Write-Host 'TRIAL_RESULT=PASS'
            return
        }

        $evidenceRoot = Join-Path $resultsRoot 'stage5-evidence'
        New-Item -ItemType Directory -Path $evidenceRoot | Out-Null
        $null = & tar.exe -xf (Join-Path $resultsRoot 'stage5-evidence.tar') -C $evidenceRoot
        if ($LASTEXITCODE -ne 0) { throw 'Could not unpack the downloaded stage 5 evidence.' }

        $logText = [IO.File]::ReadAllText((Join-Path $resultsRoot 'regression.log'))
        if ($makeExit -ne 0) { throw "Regression exited with MAKE EXIT $makeExit." }
        if ($logText -notmatch '(?m)^ALL TESTS PASSED \(') { throw 'Regression log has no ALL TESTS PASSED summary.' }
        if ($logText -notmatch '(?m)^Reproducible build: identical$') { throw 'Reproducibility check did not pass.' }
        if ($logText -match '(?im)(grep:.*iso/boot/limine/limine\.conf|No such file or directory.*limine\.conf|missing.*limine\.conf)') {
            throw 'The repro log contains a missing limine.conf notice.'
        }
        Write-Host 'REPRO_BOOT_CFG_NOTICES=NONE'
        $summary = [regex]::Match($logText, '(?m)^ALL TESTS PASSED \([^\r\n]+').Value
        Write-Host "RESULT_BY_TAG=$summary"
        Write-Host "UNIT_SUITES_PASS=$(@([regex]::Matches($logText, '(?m)^.*-> PASS$')).Count)"
        Write-Host "STAGE5_EVIDENCE=$evidenceRoot"
        Write-Host "BOOT_LOGS=$(Join-Path $resultsRoot 'boot-logs.tar.gz')"
        if ($makeExit -ne 0) { throw "Regression failed: MAKE EXIT $makeExit." }
        Write-Host 'REMOTE_REGRESSION_RESULT=PASS'
    }
    finally {
        Pop-Location
    }
}
finally {
    # 1.3 stage 9: a trial that stopped after make started still brings its
    # log back, before Kali is shut down.
    $localLog = Join-Path $resultsRoot 'regression.log'
    if ($Trial -and $null -ne $regressionStartedAt -and -not (Test-Path -LiteralPath $localLog)) {
        try {
            Copy-FromRemote -RemotePath "$remoteRoot/regression.log" -LocalPath $localLog | Out-Null
            Write-Host "TRIAL_LOG_COPIED_AFTER_FAILURE=$localLog"
        }
        catch { Write-Host "TRIAL_LOG_COPY_ERROR=$($_.Exception.Message)" }
    }
    if ($kaliStarted) {
        try {
            $null = Invoke-Native -Exe $script:vbox -Arguments @('controlvm', $KaliVM, 'acpipowerbutton') -AllowFailure
            if (Wait-KaliShutdown -TimeoutSeconds 120) { Write-Host 'KALI_SHUTDOWN=CONFIRMED (ACPI)' }
            else {
                $null = Invoke-Native -Exe $script:vbox -Arguments @('controlvm', $KaliVM, 'poweroff') -AllowFailure
                if (Wait-KaliShutdown -TimeoutSeconds 60) { Write-Host 'KALI_SHUTDOWN=CONFIRMED (power off)' }
                else { Write-Host 'KALI_SHUTDOWN=NOT_CONFIRMED' }
            }
        }
        catch { Write-Host "KALI_STOP_ERROR=$($_.Exception.Message)" }
    }
    if ($null -ne $kaliAvailableAt) {
        Write-Host "KALI_SESSION_STARTED=$($kaliAvailableAt.ToString('u'))"
        Write-Host "KALI_SESSION_ELAPSED_SECONDS=$([math]::Round(([DateTimeOffset]::UtcNow - $kaliAvailableAt).TotalSeconds))"
    }
    if ($codespaceTouched) {
        try {
            $info = $null
            try { $info = Get-CodespaceInfo }
            catch { Write-Host "CODESPACE_STATE_CHECK_ERROR=$($_.Exception.Message)" }
            if ($null -eq $info -or $info.state -ne 'Shutdown') {
                $stop = Invoke-Gh -Arguments @('codespace', 'stop', '-c', $Codespace) -AllowFailure
                if ($stop.ExitCode -ne 0) { Write-Host "CODESPACE_STOP_COMMAND_EXIT=$($stop.ExitCode) $($stop.Output -join ' ')" }
                if (-not (Wait-CodespaceShutdown -TimeoutSeconds 180)) { Write-Host 'CODESPACE_SHUTDOWN=NOT_CONFIRMED' }
                else { Write-Host 'CODESPACE_SHUTDOWN=CONFIRMED' }
            }
            else { Write-Host 'CODESPACE_SHUTDOWN=ALREADY_SHUTDOWN' }
        }
        catch { Write-Host "CODESPACE_STOP_ERROR=$($_.Exception.Message)" }
    }
    if ($null -ne $codespaceAvailableAt) {
        Write-Host "CODESPACE_SESSION_STARTED=$($codespaceAvailableAt.ToString('u'))"
        Write-Host "CODESPACE_SESSION_ELAPSED_SECONDS=$([math]::Round(([DateTimeOffset]::UtcNow - $codespaceAvailableAt).TotalSeconds))"
    }
    Write-Host "RESULTS=$resultsRoot"
    if ($Trial) {
        # 1.3 stage 8: the trial's summary; its last line is the exit status.
        $lines = @("trial (never evidence): $makeCommand", "TRIAL_$trialMemLine",
                   "TRIAL_KALI_MEMORY_MB=$kaliMem", "TRIAL_MAKE_EXIT=$makeExit", "RESULTS=$resultsRoot",
                   "TRIAL_RESULT=$trialResult", ("exit status {0}" -f $(if ($trialResult -eq 'PASS') { 0 } else { 1 })))
        Write-LfUtf8 -Path (Join-Path $tempRoot 'trial-summary.txt') -Text (($lines -join [string][char]10) + [char]10)
        Write-Host "TRIAL_SUMMARY=$(Join-Path $tempRoot 'trial-summary.txt')"
        try { Stop-Transcript | Out-Null } catch {}
    }
}
