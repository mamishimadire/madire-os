# Builds Madire OS from Windows by compiling inside a GitHub Codespace.
#
# Copies the local source tree to the Codespace, runs `make` there, and copies
# the ISO back to build\madire-os.iso. Local edits do not need to be committed
# or pushed first.
#
#   .\scripts\build.ps1                  # make iso test, fetch the ISO
#   .\scripts\build.ps1 -InstallDeps     # first run on a fresh Codespace
#   .\scripts\build.ps1 -Codespace <name>
param(
    [string]$Codespace,
    [switch]$InstallDeps
)
$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
$remote = "/tmp/madire-os-build"

if (-not $Codespace) {
    $Codespace = gh codespace list --repo mamishimadire/madire-os --json name --jq ".[0].name"
    if (-not $Codespace) { throw "No Codespace found for mamishimadire/madire-os." }
}
Write-Host "Using Codespace: $Codespace"

function Invoke-Remote([string]$cmd) {
    gh codespace ssh -c $Codespace -- $cmd
    if ($LASTEXITCODE -ne 0) { throw "Remote command failed: $cmd" }
}

Push-Location $root
try {
    Invoke-Remote "rm -rf $remote && mkdir -p $remote"
    $sources = @("Makefile", "boot", "kernel", "linker", "iso", "include", "scripts") | Where-Object { Test-Path $_ }
    foreach ($s in $sources) {
        gh codespace cp -e -r -c $Codespace $s "remote:$remote/"
        if ($LASTEXITCODE -ne 0) { throw "Copying $s to Codespace failed." }
    }

    if ($InstallDeps) { Invoke-Remote "bash $remote/scripts/install-deps.sh" }

    Invoke-Remote "cd $remote && make iso test"

    New-Item -ItemType Directory -Force build | Out-Null
    gh codespace cp -e -c $Codespace "remote:$remote/build/madire-os.iso" "build/madire-os.iso"
    if ($LASTEXITCODE -ne 0) { throw "Copying the ISO back from the Codespace failed." }
    Write-Host "ISO ready: $root\build\madire-os.iso"
}
finally {
    Pop-Location
}
