[CmdletBinding()]
param(
    # Kali remains unavailable until separately approved.
    [ValidateSet('Codespace')]
    [string]$Target = 'Codespace',
    [string]$Codespace = 'madire-os-0-8-devcontainer-test-9vrr95qjqj63rg6'
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repoRoot = Split-Path $PSScriptRoot -Parent
$snapshotRoots = @('Makefile', 'boot', 'drivers', 'include', 'iso', 'kernel', 'linker', 'scripts', 'tests', 'user') |
    Where-Object { Test-Path -LiteralPath (Join-Path $repoRoot $_) }
$codespaceTouched = $false
$codespaceAvailableAt = $null
$guardStartedAt = $null
$guardFinishedAt = $null
$regressionStartedAt = $null
$regressionFinishedAt = $null
$runId = ([DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss')) + '-' + [Guid]::NewGuid().ToString('N').Substring(0, 8)
$remoteRoot = "/workspaces/madire-regression-$runId"
$tempRoot = Join-Path ([IO.Path]::GetTempPath()) "madire-regression-$runId"
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

function Invoke-Remote {
    param([Parameter(Mandatory = $true)][string[]]$RemoteArguments, [switch]$AllowFailure)
    $arguments = @('codespace', 'ssh', '-c', $Codespace, '--') + $RemoteArguments
    return Invoke-Gh -Arguments $arguments -AllowFailure:$AllowFailure
}

function Copy-ToRemote {
    param([string]$LocalPath, [string]$RemotePath)
    return Invoke-Gh -Arguments @('codespace', 'cp', '-e', '-c', $Codespace, $LocalPath, "remote:$RemotePath")
}

function Copy-FromRemote {
    param([string]$RemotePath, [string]$LocalPath, [switch]$Recursive)
    $arguments = @('codespace', 'cp', '-e')
    if ($Recursive) { $arguments += '-r' }
    $arguments += @('-c', $Codespace, "remote:$RemotePath", $LocalPath)
    return Invoke-Gh -Arguments $arguments
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
            $relativePaths.Add($_.FullName.Substring($rootFull.Length + 1).Replace('\', '/'))
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

if ($Target -ne 'Codespace') { throw "Unsupported target: $Target" }
if (-not (Get-Command gh -ErrorAction SilentlyContinue)) { throw 'GitHub CLI (gh) is required.' }
if (-not (Get-Command tar.exe -ErrorAction SilentlyContinue)) { throw 'tar.exe is required to create the source snapshot.' }
if (-not (Test-Path -LiteralPath (Join-Path $repoRoot 'Makefile'))) { throw "Not a Madire OS repository: $repoRoot" }

New-Item -ItemType Directory -Path $tempRoot | Out-Null
New-Item -ItemType Directory -Path $resultsRoot | Out-Null

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

        $tarArguments = @('-cf', $snapshotArchive, '-C', $repoRoot) + $snapshotRoots
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
make unit test iso > "$root/regression.log" 2>&1
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
echo "== [0.11] result lines =="
grep -F 'PASS: [0.11]' "$log" | tail -20 || true
if grep -Eiq 'iso/boot/grub/grub\.cfg.*(no such file|cannot stat|not found)|no such file.*grub\.cfg' "$log"; then
    echo REPRO_GRUB_CFG_NOTICES=FOUND
    exit 50
fi
echo REPRO_GRUB_CFG_NOTICES=NONE
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
margin=$((0x200000 - physical))
printf 'KERNEL_END_VIRTUAL=0x%s\nKERNEL_END_PHYSICAL=0x%x\nTWO_MIB_MARGIN_BYTES=%d\n' "$symbol" "$physical" "$margin"
if [ "$margin" -lt 32768 ]; then echo TWO_MIB_MARGIN=STOP; exit 54; fi
echo TWO_MIB_MARGIN=PASS
'@
        $evidenceBody = @'
#!/usr/bin/env bash
set -euo pipefail
root=__ROOT__
cd "$root/source/build"
tar -cf "$root/stage5-evidence.tar" serial-program-output.log screen-program-*.txt
echo STAGE5_QMP_SCREEN_EVIDENCE=PACKED
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
        }
        foreach ($name in @($remoteFiles.Keys)) {
            $body = $remoteFiles[$name].Replace('__ROOT__', $remoteRoot).Replace('__RUN__', $runId)
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
        Write-Host "REGRESSION_STARTED=$($regressionStartedAt.ToString('u')) command='make unit test iso'"

        $deadline = [DateTimeOffset]::UtcNow.AddMinutes(90)
        do {
            Start-Sleep -Seconds 60
            $poll = Invoke-Remote -RemoteArguments @('bash', "$remoteRoot/poll.sh")
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
        $null = Invoke-Remote -RemoteArguments @('bash', "$remoteRoot/checks.sh")
        $null = Invoke-Remote -RemoteArguments @('bash', "$remoteRoot/evidence.sh")
        Copy-FromRemote -RemotePath "$remoteRoot/stage5-evidence.tar" -LocalPath (Join-Path $resultsRoot 'stage5-evidence.tar') | Out-Null

        $downloadedFinal = [IO.File]::ReadAllText((Join-Path $resultsRoot 'final.sha256'))
        if ($downloadedFinal -cne $manifestText) { throw 'Final remote source hashes differ from the snapshot manifest.' }
        Write-Host 'FINAL_SNAPSHOT_HASH_MATCH=PASS'

        $evidenceRoot = Join-Path $resultsRoot 'stage5-evidence'
        New-Item -ItemType Directory -Path $evidenceRoot | Out-Null
        $null = & tar.exe -xf (Join-Path $resultsRoot 'stage5-evidence.tar') -C $evidenceRoot
        if ($LASTEXITCODE -ne 0) { throw 'Could not unpack the downloaded stage 5 evidence.' }

        $logText = [IO.File]::ReadAllText((Join-Path $resultsRoot 'regression.log'))
        if ($makeExit -ne 0) { throw "Regression exited with MAKE EXIT $makeExit." }
        if ($logText -notmatch '(?m)^ALL TESTS PASSED \(') { throw 'Regression log has no ALL TESTS PASSED summary.' }
        if ($logText -notmatch '(?m)^Reproducible build: identical$') { throw 'Reproducibility check did not pass.' }
        if ($logText -match '(?im)(grep:.*iso/boot/grub/grub\.cfg|No such file or directory.*grub\.cfg|missing.*grub\.cfg)') {
            throw 'The repro log contains a missing grub.cfg notice.'
        }
        Write-Host 'REPRO_GRUB_CFG_NOTICES=NONE'
        $summary = [regex]::Match($logText, '(?m)^ALL TESTS PASSED \([^\r\n]+').Value
        Write-Host "RESULT_BY_TAG=$summary"
        Write-Host "UNIT_SUITES_PASS=$(@([regex]::Matches($logText, '(?m)^.*-> PASS$')).Count)"
        Write-Host "STAGE5_EVIDENCE=$evidenceRoot"
        if ($makeExit -ne 0) { throw "Regression failed: MAKE EXIT $makeExit." }
        Write-Host 'REMOTE_REGRESSION_RESULT=PASS'
    }
    finally {
        Pop-Location
    }
}
finally {
    if ($codespaceTouched) {
        try {
            $info = Get-CodespaceInfo
            if ($info.state -ne 'Shutdown') {
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
}
