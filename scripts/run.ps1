# Boots build\madire-os.iso in the VirtualBox VM "MadireOS-Test".
#
# Attaches the ISO to the VM's optical drive, boots from DVD first, starts the
# VM, and (with -Screenshot) saves build\boot-screen.png after a delay.
#
#   .\scripts\run.ps1
#   .\scripts\run.ps1 -Screenshot -Headless
param(
    [string]$VM = "MadireOS-Test",
    [switch]$Headless,
    [switch]$Screenshot,
    [int]$WaitSeconds = 12
)
$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
$iso = Join-Path $root "build\madire-os.iso"
if (-not (Test-Path $iso)) { throw "ISO not found: $iso (run scripts\build.ps1 first)" }

$installDir = (Get-ItemProperty "HKLM:\SOFTWARE\Oracle\VirtualBox").InstallDir
$vbox = Join-Path $installDir "VBoxManage.exe"

function VBox { & $vbox @args; if ($LASTEXITCODE -ne 0) { throw "VBoxManage $args failed" } }

# Power off a previous run so the drive can be changed.
$state = (& $vbox showvminfo $VM --machinereadable | Select-String '^VMState=').Line
if ($state -match '"running"|"paused"') {
    VBox controlvm $VM poweroff
    Start-Sleep -Seconds 2
}

VBox storageattach $VM --storagectl SATA --port 1 --device 0 --type dvddrive --medium $iso
VBox modifyvm $VM --boot1 dvd --boot2 disk --boot3 none --boot4 none
VBox startvm $VM --type $(if ($Headless) { "headless" } else { "gui" })

if ($Screenshot) {
    Start-Sleep -Seconds $WaitSeconds
    $png = Join-Path $root "build\boot-screen.png"
    VBox controlvm $VM screenshotpng $png
    Write-Host "Screenshot saved: $png"
}
