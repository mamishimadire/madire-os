# The official regression on GitHub Actions (0.12, stage 0c): dispatches
# .github/workflows/regression.yml on the commit under test, and collects a
# finished run's artifact into build\regression\<run id>\results\.
#
#   .\scripts\actions-regress.ps1 -Dispatch [-Mode sharded|full] [-Parts all|stress]
#   .\scripts\actions-regress.ps1 -Collect <run id>
#
# -Dispatch refuses a working tree with changes and a commit that is not on
# GitHub, and passes the commit as a full SHA, so every job tests exactly it.
# It prints the run id and returns; it does not wait. -Collect reports a run
# that is still going and returns; once it has finished, it downloads the
# artifact, checks it is the dispatched commit and prints the summary.
# Every collected run's per-boot logs are kept (1.0 stage 9: nothing is
# deleted here until the milestone is tagged).
[CmdletBinding(DefaultParameterSetName = 'Dispatch')]
param(
    [Parameter(ParameterSetName = 'Dispatch', Mandatory = $true)]
    [switch]$Dispatch,
    [Parameter(ParameterSetName = 'Dispatch')]
    [ValidateSet('sharded', 'full')]
    [string]$Mode = 'sharded',
    # 0.12 stage 2b: "stress" runs only the kill-path stress part (one job);
    # "all" (the default) is the regression, exactly as before.
    [Parameter(ParameterSetName = 'Dispatch')]
    [ValidateSet('all', 'stress')]
    [string]$Parts = 'all',
    [Parameter(ParameterSetName = 'Collect', Mandatory = $true)]
    [ValidatePattern('^[0-9]+$')]
    [string]$Collect
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$repoRoot = Split-Path $PSScriptRoot -Parent
$workflow = 'regression.yml'

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
        throw "$Exe $($Arguments -join ' ') failed with exit code ${exitCode}: $($lines -join [Environment]::NewLine)"
    }
    [pscustomobject]@{ ExitCode = $exitCode; Output = $lines }
}

Push-Location $repoRoot
try {
    if ($Dispatch) {
        $status = (Invoke-Native git @('status', '--porcelain')).Output | Where-Object { $_ -ne '' }
        if (@($status).Count -ne 0) { throw "The working tree has changes; commit them first:`n$($status -join "`n")" }
        $sha = ((Invoke-Native git @('rev-parse', 'HEAD')).Output | Select-Object -First 1).Trim()
        if ($sha -notmatch '^[0-9a-f]{40}$') { throw "Unexpected commit id '$sha'." }
        $null = Invoke-Native git @('fetch', '--quiet', 'origin')
        $remote = (Invoke-Native git @('branch', '--remotes', '--contains', $sha) -AllowFailure).Output | Where-Object { $_ -match '\S' }
        if (@($remote).Count -eq 0) { throw "Commit $sha is not on GitHub; push it first." }
        $before = [DateTimeOffset]::UtcNow.AddSeconds(-5)
        $runArgs = @('workflow', 'run', $workflow, '--ref', 'main', '-f', "ref=$sha", '-f', "mode=$Mode")
        if ($Parts -ne 'all') { $runArgs += @('-f', "parts=$Parts") }
        $null = Invoke-Native gh $runArgs
        Write-Host "DISPATCHED commit=$sha mode=$Mode parts=$Parts"
        $run = $null
        for ($i = 0; $i -lt 12 -and -not $run; $i++) {
            Start-Sleep -Seconds 5
            $json = (Invoke-Native gh @('run', 'list', '--workflow', $workflow, '--event', 'workflow_dispatch',
                '--limit', '5', '--json', 'databaseId,createdAt,url')).Output -join "`n"
            # Windows PowerShell 5.1 emits a JSON array as one object: unroll it first.
            $list = $json | ConvertFrom-Json
            $run = @($list | ForEach-Object { $_ }) |
                Where-Object { [DateTimeOffset]::Parse([string]$_.createdAt) -ge $before } |
                Sort-Object { [DateTimeOffset]::Parse([string]$_.createdAt) } | Select-Object -First 1
        }
        if (-not $run) { throw 'The dispatched run did not appear within a minute; see: gh run list --workflow regression.yml' }
        Write-Host "RUN_ID=$($run.databaseId)"
        Write-Host "RUN_URL=$($run.url)"
        Write-Host "Collect it when it has finished: .\scripts\actions-regress.ps1 -Collect $($run.databaseId)"
        return
    }

    $info = (Invoke-Native gh @('run', 'view', $Collect, '--json', 'status,conclusion,createdAt,updatedAt,url')).Output -join "`n" |
        ConvertFrom-Json
    Write-Host "RUN $Collect status=$($info.status) conclusion=$($info.conclusion) created=$($info.createdAt) updated=$($info.updatedAt)"
    if ($info.status -ne 'completed') { Write-Host 'Not finished yet; collect it later.'; return }
    $minutes = [math]::Round(([DateTimeOffset]::Parse([string]$info.updatedAt) - [DateTimeOffset]::Parse([string]$info.createdAt)).TotalMinutes, 1)
    Write-Host "WALL_MINUTES=$minutes"

    $runRoot = Join-Path $repoRoot "build\regression\$Collect"
    $results = Join-Path $runRoot 'results'
    if (Test-Path -LiteralPath $results) { Remove-Item -LiteralPath $results -Recurse -Force }
    New-Item -ItemType Directory -Path $results -Force | Out-Null
    $null = Invoke-Native gh @('run', 'download', $Collect, '--name', "regression-$Collect", '--dir', $results)

    $summaryFile = Join-Path $results 'combine-summary.txt'
    if (Test-Path -LiteralPath $summaryFile) {
        Write-Host '--- combined summary (sharded run) ---'
        Get-Content -LiteralPath $summaryFile | Where-Object {
            $_ -match '^(SAME_|ISO_|part |COMBINE_FAIL|UNIT_SUITES|Reproducible|UNDEFINED|LIBGCC|SSE_X87|KERNEL_END|KERNEL_MARGIN|TWO_MIB|COUNTED|EXPECTED_CHECKS|REPLAY|ALL TESTS|COMBINED_RESULT|[0-9]+ TEST)' -or $_ -match '^  '
        } | ForEach-Object { Write-Host $_ }
        $commit = (Get-Content -LiteralPath (Join-Path $results 'shard-static\commit.txt') | Select-Object -First 1)
    }
    else {
        Write-Host '--- summary (full run) ---'
        Get-Content -LiteralPath (Join-Path $results 'checks.txt') | ForEach-Object { Write-Host $_ }
        Get-Content -LiteralPath (Join-Path $results 'replay.txt') -ErrorAction SilentlyContinue | ForEach-Object { Write-Host $_ }
        $commit = (Get-Content -LiteralPath (Join-Path $results 'commit.txt') | Select-Object -First 1)
    }
    Write-Host "COMMIT=$commit"
    Write-Host "RESULTS=$results"

    # 1.0 stage 9: no pruning; every run's per-boot logs stay until the tag.
    if ($info.conclusion -ne 'success') { throw "Run $Collect concluded '$($info.conclusion)'." }
    Write-Host 'ACTIONS_REGRESSION_RESULT=PASS'
}
finally {
    Pop-Location
}
