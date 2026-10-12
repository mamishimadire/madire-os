# VirtualBox acceptance test for Madire OS (0.9; see docs/testing.md).
#
# Boots the given ISO in the test VM once per test, sends keys through
# VirtualBox's emulated PS/2 keyboard, saves a screenshot and a copy of the
# serial log (COM1) of every run, and checks each log for the line that
# proves the test passed. The results are compared with QEMU afterwards.
#
# The VM's settings are CHECKED, never changed: if the OS type, firmware,
# memory, PAE, boot order or COM1 port/file differ from the documented
# values, the script refuses to run. Attaching the given ISO to the
# optical drive for the run is the only change it makes (as scripts\run.ps1
# does), and it verifies the attachment.
#
#   .\scripts\vbox-acceptance.ps1                      # every test
#   .\scripts\vbox-acceptance.ps1 -Only boot,preempt   # some tests
#   .\scripts\vbox-acceptance.ps1 -Only repeat -Repeat 20   # 20 normal boots
#   .\scripts\vbox-acceptance.ps1 -VBoxManage "D:\VirtualBox\VBoxManage.exe"
param(
    [string]$VM = "MadireOS-Test",
    [string]$VBoxManage = "",
    [string]$Iso = "",                  # default: build\madire-os.iso
    [string]$ModuleIso = "",            # default: build\madire-os-module.iso
    [string]$OutDir = "",               # default: build\vbox-acceptance
    [string]$Prefix = "vbox",
    [string[]]$Only = @(),              # menu, boot, procs, commands, reboot, scrollback, preempt, threadoverflow, pf, diag, diagmore, user, smx, memexhaust, module, shell, repeat
    [int]$KeyDelayMs = 3500,            # keys no sooner than 3 s after power-on
    [int]$MenuHoldMs = 15000,           # 0.13: expected time to the boot menu (the wait for it allows 30 s more)
    [int]$Repeat = 0,                   # extra normal boots (self-test result of each)
    [string]$KaliVM = "kali-linux-2026.2-virtualbox-amd64"   # must not be running (0.11)
)
$ErrorActionPreference = "Stop"
$root = (Get-Item (Split-Path $PSScriptRoot -Parent)).FullName
$build = Join-Path $root "build"
if (-not $Iso) { $Iso = Join-Path $build "madire-os.iso" }
if (-not $ModuleIso) { $ModuleIso = Join-Path $build "madire-os-module.iso" }
if (-not $OutDir) { $OutDir = Join-Path $build "vbox-acceptance" }
$serial = Join-Path $build "vbox-serial.log"    # the documented COM1 file

# 0.10: the version is read once, from kernel/kernel.h; the checks that
# contain it build their regex from $VRe, so a new version changes no check.
$verMatch = Select-String -Path (Join-Path $root "kernel\kernel.h") -Pattern '^#define MADIRE_OS_VERSION "([0-9]+(\.[0-9]+)*)"$'
if (-not $verMatch) { throw "cannot read MADIRE_OS_VERSION from kernel\kernel.h" }
$VRe = [regex]::Escape($verMatch.Matches[0].Groups[1].Value)   # 0.9.2 -> 0\.9\.2

# ---- VBoxManage ----
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

# VBoxManage reports progress ("0%...100%") on stderr. Windows PowerShell
# 5.1 turns native stderr into error records, which would stop the script
# under "Stop", so errors are non-terminating here and the exit code decides.
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
if ($info["VMState"] -notin @("poweroff", "aborted")) {
    $problems += "the VM is '$($info['VMState'])'; power it off first (nothing is discarded here)"
}
if ($problems.Count) {
    $problems | ForEach-Object { Write-Host "VM check: $_" }
    throw "The VM does not match the documented settings; refusing to run (no setting was changed)."
}
# Rule (since 0.11): no acceptance run while the Kali build VM runs (a
# regression there would distort this run's timing evidence).
$running = @(VBox list runningvms | ForEach-Object { if ($_ -match '^"(.+)" \{') { $Matches[1] } })
if ($running -contains $KaliVM) { throw "The Kali build VM '$KaliVM' is running; refusing to run (power it off first)." }
foreach ($f in @($Iso, $ModuleIso)) { if (-not (Test-Path $f)) { throw "Not found: $f" } }
$Iso = (Get-Item $Iso).FullName
$ModuleIso = (Get-Item $ModuleIso).FullName
New-Item -ItemType Directory -Force $OutDir | Out-Null
$summary = Join-Path $OutDir "$Prefix-summary.txt"
$results = New-Object System.Collections.Generic.List[string]
function Note([string]$s) { Write-Host $s; $script:results.Add($s) }

Note "VM check: $VM matches the documented settings (Other_64, BIOS, 4096 MB, PAE off, boot DVD, COM1 0x3F8 IRQ 4 to build\vbox-serial.log)"
foreach ($f in @($Iso, $ModuleIso)) {
    Note ("ISO: {0}  {1}" -f (Get-FileHash $f -Algorithm SHA256).Hash.ToLower(), (Split-Path $f -Leaf))
}

# ---- keys and screenshots ----
function Key { VBox controlvm $VM keyboardputscancode @args | Out-Null; Start-Sleep -Milliseconds 150 }
function TypeText([string]$s) { VBox controlvm $VM keyboardputstring $s | Out-Null; Start-Sleep -Milliseconds 400 }
function Enter { Key 1c 9c; Start-Sleep -Milliseconds 400 }
function Down { Key e0 50 e0 d0 }
function HomeKey { Key e0 47 e0 c7 }
function Shot([string]$name) {
    VBox controlvm $VM screenshotpng (Join-Path $OutDir "$Prefix-$name.png") | Out-Null
}

function Attach([string]$file) {
    VBox storageattach $VM --storagectl SATA --port 1 --device 0 --type dvddrive --medium $file | Out-Null
    $now = (Get-VMInfo)["SATA-1-0"]
    if (-not (Test-SamePath $now $file)) { throw "Optical drive holds '$now', expected '$file'." }
}

function Start-Run {
    if (Test-Path $serial) { Remove-Item $serial -Confirm:$false }
    VBox startvm $VM --type headless | Out-Null
    Start-Sleep -Milliseconds $KeyDelayMs
}

# Waits until the serial log matches `pattern` (true) or the timeout passes.
function Wait-Serial([string]$pattern, [int]$seconds) {
    for ($i = 0; $i -lt $seconds * 2; $i++) {
        if ((Test-Path $serial) -and (Select-String -Path $serial -Pattern $pattern -Quiet)) { return $true }
        Start-Sleep -Milliseconds 500
    }
    return $false
}

# Powers off, keeps the serial log as <Prefix>-<name>.serial.log, and checks
# it for each expected pattern.
function Stop-Run([string]$name, [string[]]$expect) {
    VBox controlvm $VM poweroff | Out-Null
    Start-Sleep -Seconds 2
    $copy = Join-Path $OutDir "$Prefix-$name.serial.log"
    if (Test-Path $serial) { Copy-Item $serial $copy -Force } else { Note "${name}: NO SERIAL LOG"; return }
    foreach ($p in $expect) {
        $ok = Select-String -Path $copy -Pattern $p -Quiet
        Note ("{0,-16} {1,-8} /{2}/" -f $name, $(if ($ok) { "OK" } else { "MISSING" }), $p)
    }
}

# Diagnostics positions (0.9; 16-26 appended in 0.10; since 0.13 the
# sub-entries of the boot menu's "Diagnostics" directory, in limine.conf's
# order): position N is reached with N + 1 presses of the down arrow from the
# expanded "Diagnostics" directory.
$Diag = [ordered]@{
    divzero = 0; ud = 1; gpf = 2; pfnull = 3; pfunmapped = 4; pfreadonly = 5; pfnx = 6;
    pflow = 7; stackoverflow = 8; memexhaust = 9; doublefree = 10; freereserved = 11;
    timer = 12; keyboard = 13; preempt = 14; threadoverflow = 15;
    userspin = 16; userkill = 17; userfault = 18; userpriv = 19; userkmem = 20;
    usersys = 21; userio = 22; userpreempt = 23; userchurn = 24; smep = 25; smap = 26
}

# Notes a check that a pattern is absent from a kept serial log (case-
# sensitive: normal boot lines say "panic_exception" and "panics").
function Absent([string]$name, [string]$pattern) {
    $log = Join-Path $OutDir "$Prefix-$name.serial.log"
    if (-not (Test-Path $log)) { return }
    $n = @(Select-String -Path $log -Pattern $pattern -CaseSensitive).Count
    Note ("{0,-16} {1,-8} /no {2}: {3} lines/" -f $name, $(if ($n -eq 0) { "OK" } else { "MISSING" }), $pattern, $n)
}

# 0.13: the width of the VM's screen now (from a screenshot's PNG header).
# Limine draws its menu on a 1024x768 graphics screen; the BIOS splash and
# Madire's text screen are 720x400 (or 640x480).
function Get-ScreenWidth {
    $f = Join-Path $OutDir "$Prefix-screen-probe.png"
    VBox controlvm $VM screenshotpng $f 2>$null | Out-Null
    if (-not (Test-Path $f)) { return 0 }
    $b = [IO.File]::ReadAllBytes($f)
    Remove-Item $f -Confirm:$false
    if ($b.Length -lt 24) { return 0 }
    return ([int]$b[16] -shl 24) -bor ([int]$b[17] -shl 16) -bor ([int]$b[18] -shl 8) -bor [int]$b[19]
}

# 0.13: powers on and stops the boot menu's 3 s countdown. No key is sent
# before the menu is on the screen (keys during the BIOS's power-on self-test
# stop it with "FATAL: Keyboard error"): the screen is polled every 300 ms,
# for at most $MenuHoldMs + 30 s, until it is 1024 pixels wide (Limine's menu).
# Then Home is pressed: Limine (common/menu.c) ends the countdown at the first
# key and then handles it, and Home only selects the first entry. After 4 s,
# longer than the countdown, the menu must still be on the screen and the
# serial log still empty (no kernel started). Returns $true if both hold,
# otherwise notes the failure and returns $false.
function Start-Menu([string]$name) {
    if (Test-Path $serial) { Remove-Item $serial -Confirm:$false }
    VBox startvm $VM --type headless | Out-Null
    $until = (Get-Date).AddMilliseconds($MenuHoldMs + 30000)
    $shown = $false
    while (-not $shown -and (Get-Date) -lt $until) {
        if ((Get-ScreenWidth) -eq 1024) { $shown = $true } else { Start-Sleep -Milliseconds 300 }
    }
    if ($shown) { HomeKey; Start-Sleep -Seconds 4 }
    $quiet = -not ((Test-Path $serial) -and (Get-Item $serial).Length -gt 0)
    $held = $shown -and $quiet -and ((Get-ScreenWidth) -eq 1024)
    if (-not $held) {
        Note ("{0,-16} {1,-8} /boot menu shown and held (menu seen: {2}; no kernel output: {3})/" -f $name, "MISSING", $shown, $quiet)
    }
    return $held
}

# Boots a Diagnostics entry through the boot menu (0.13: Limine): the menu
# held at the first entry, Down to "Diagnostics", Enter expands it (the
# selection stays on it), Down N + 1 times to position N, Enter boots it.
$script:DiagBooted = [ordered]@{}
function Start-Diag([string]$test) {
    if (-not (Start-Menu $test)) { return }
    HomeKey; Start-Sleep -Milliseconds 300
    Down; Start-Sleep -Milliseconds 300
    Enter; Start-Sleep -Milliseconds 500
    for ($i = 0; $i -le $Diag[$test]; $i++) { Down }
    Start-Sleep -Milliseconds 300
    Enter
    $script:DiagBooted[$test] = $Diag[$test]
}

function Want([string]$group) { return ($Only.Count -eq 0) -or ($Only -contains $group) }

$halted = "System halted\. Power off the machine\."
Attach $Iso

if (Want "menu") {
    if (Start-Menu "menu") {
        Note ("{0,-16} {1,-8} /boot menu held (no kernel output before the selection)/" -f "menu", "OK")
        HomeKey; Start-Sleep -Milliseconds 300
        Shot "boot-menu"                            # the default entry selected, no countdown
        Down; Start-Sleep -Milliseconds 300
        Enter; Start-Sleep -Milliseconds 500
        Shot "boot-menu-diagnostics-open"           # "Diagnostics" expanded, still selected
        Down; Start-Sleep -Milliseconds 300
        Shot "boot-menu-diagnostics-top"            # position 0 selected
        for ($i = 0; $i -lt 15; $i++) { Down }
        Start-Sleep -Milliseconds 300
        Shot "boot-menu-diagnostics-end"            # position 15 selected
        for ($i = 0; $i -lt 11; $i++) { Down }
        Start-Sleep -Milliseconds 300
        Shot "boot-menu-diagnostics-user"           # 0.10: position 26 (smap) selected
        $still = -not ((Test-Path $serial) -and (Get-Item $serial).Length -gt 0)
        Note ("{0,-16} {1,-8} /no boot while navigating the menu/" -f "menu", $(if ($still) { "OK" } else { "MISSING" }))
        Enter                                       # boots the selected entry: it must be smap
        Wait-Serial "^SMAP test: |$halted" 60 | Out-Null
        Start-Sleep -Seconds 1
    }
    Stop-Run "menu" @("^SMAP test: ")
}
if (Want "boot") {
    Start-Run
    Wait-Serial "System ready\." 40 | Out-Null
    Start-Sleep -Seconds 1
    Shot "boot"
    Key 39 b9; Start-Sleep -Seconds 1      # opens the console (not typed)
    TypeText "ps"; Enter
    TypeText "version"; Enter
    Start-Sleep -Seconds 1
    Shot "console-ps-version"
    TypeText "clear"; Enter
    TypeText "mem"; Enter
    Start-Sleep -Seconds 1
    Shot "console-mem"
    Stop-Run "boot-console" @("^Scheduler self-test: PASS", "^System ready\.", "^madire> ps$",
        "^  0  running .* kernel$", "^Madire OS v$VRe \(64-bit x86-64, Multiboot 1\)$",
        "^Kernel stack: +[0-9]+ bytes at peak, of 16384$",
        "^Key events: not logged \(boot option 'keylog' logs them; for tests only\)$")
    # 0.9.2: keys were typed (the console was opened and used), but key
    # events are not logged on a normal boot.
    $log = Join-Path $OutDir "$Prefix-boot-console.serial.log"
    if (Test-Path $log) {
        $keys = @(Select-String -Path $log -Pattern '^key: ').Count
        Note ("{0,-16} {1,-8} /no key event logged although keys were typed: {2} key lines/" -f "boot-console",
              $(if ($keys -eq 0) { "OK" } else { "MISSING" }), $keys)
        # 0.10: the user-mode boot self-test (hello, exitcode, nullptr as
        # processes 1-3), from the same normal boot.
        foreach ($p in @("^User-mode self-test: hello \(one line through write\): process 1 exited, status 0 \(expected 0\): OK$",
                         "^User-mode self-test: exitcode \(exit\(42\)\): process 2 exited, status 42 \(expected 42\): OK$",
                         "^User-mode self-test: nullptr \(read of address 0\): process 3 killed by a CPU exception: #PF, address 0x0000000000000000, instruction 48 8b 04 25: OK$",
                         "^User-mode self-test: PASS$")) {
            $ok = Select-String -Path $log -Pattern $p -Quiet
            Note ("{0,-16} {1,-8} /{2}/" -f "boot-selftest", $(if ($ok) { "OK" } else { "MISSING" }), $p)
        }
        # 0.10: the CPU features as the VM presents them (report only).
        $feat = @(Select-String -Path $log -Pattern '^(SMEP|SMAP|NX|CR0\.EM): ' | ForEach-Object { $_.Line })
        foreach ($f in $feat) { Write-Host "cpu (report only): $f"; $script:results.Add("cpu report: $f") }
    }
}
# 0.9.2: console scrollback. dmesg 60 gives enough lines to page back.
if (Want "scrollback") {
    Start-Run
    Wait-Serial "System ready\." 40 | Out-Null
    Start-Sleep -Seconds 1
    Key 39 b9; Start-Sleep -Seconds 1      # opens the console (not typed)
    TypeText "dmesg 60"; Enter
    Start-Sleep -Seconds 2
    Shot "scroll-bottom"
    Key 2a e0 49 e0 c9 aa; Start-Sleep -Seconds 1      # Shift+PgUp
    Shot "scroll-page1"
    Key 2a e0 49 e0 c9 aa; Start-Sleep -Seconds 1      # Shift+PgUp again
    Shot "scroll-page2"
    Key 1e 9e; Start-Sleep -Seconds 1                  # "a": back to the bottom
    Shot "scroll-back"
    Stop-Run "scrollback" @("^madire> dmesg 60$",
        "^console: scrolled back: line [0-9]+ of [0-9]+, top '",
        "^console: back at the bottom$")
    $log = Join-Path $OutDir "$Prefix-scrollback.serial.log"
    if (Test-Path $log) {
        # The first page shows the 24 lines just above the screen (X = Y - 48);
        # the second goes 24 lines further back, but never past line 1.
        $pages = @(Select-String -Path $log -Pattern '^console: scrolled back: line ([0-9]+) of ([0-9]+)')
        $ok = $false
        if ($pages.Count -eq 2) {
            $x1 = [int]$pages[0].Matches[0].Groups[1].Value
            $y1 = [int]$pages[0].Matches[0].Groups[2].Value
            $x2 = [int]$pages[1].Matches[0].Groups[1].Value
            $ok = $x1 -eq $y1 - 48 -and $x2 -eq [Math]::Max(1, $x1 - 24)
        }
        $desc = ($pages | ForEach-Object { $_.Matches[0].Groups[1].Value + " of " + $_.Matches[0].Groups[2].Value }) -join ", "
        Note ("{0,-16} {1,-8} /two pages back: the first = the 24 lines above the screen, the second 24 further (not past line 1): {2}/" -f "scrollback",
              $(if ($ok) { "OK" } else { "MISSING" }), $desc)
    }
}
# 0.9.1: the shell commands. The guest's "date" is compared with the host's
# clock (local time and UTC) and the difference is only reported: the VM's
# RTC setting is never changed here.
# 0.11: programs from the console (the default boot entry loads them):
# run (output on the screen), run -b, ps, kill asking first (no, then yes),
# Ctrl+C on a spinning and on a printing program.
if (Want "procs") {
    $ctrlC = { Key 1d 2e ae 9d; Start-Sleep -Seconds 2 }
    Start-Run
    Wait-Serial "System ready\." 40 | Out-Null
    Start-Sleep -Seconds 1
    Key 39 b9; Start-Sleep -Seconds 1      # opens the console (not typed)
    TypeText "run hello"; Enter; Start-Sleep -Seconds 2
    Shot "procs-run-hello"
    TypeText "run exitcode"; Enter; Start-Sleep -Seconds 2
    TypeText "run -b spin"; Enter
    Wait-Serial "^\[process [0-9]+ 'spin' started in the background\]$" 10 | Out-Null
    $m = Select-String -Path $serial -Pattern "^\[process ([0-9]+) 'spin' started in the background\]$" |
         Select-Object -First 1
    $bgPid = if ($m) { $m.Matches[0].Groups[1].Value } else { "0" }
    TypeText "ps"; Enter; Start-Sleep -Seconds 1
    Shot "procs-ps"
    TypeText "kill $bgPid"; Enter
    TypeText "no"; Enter; Start-Sleep -Seconds 1
    Shot "procs-kill-no"
    TypeText "kill $bgPid"; Enter
    TypeText "yes"; Enter; Start-Sleep -Seconds 2
    Shot "procs-kill-yes"
    TypeText "clear"; Enter
    TypeText "run spin"; Enter; Start-Sleep -Seconds 2
    & $ctrlC
    Shot "procs-ctrlc-spin"
    TypeText "run lines 65636"; Enter; Start-Sleep -Seconds 2
    & $ctrlC
    Shot "procs-ctrlc-lines"
    TypeText "ps"; Enter; Start-Sleep -Seconds 1
    Shot "procs-ps-after"
    Stop-Run "procs" @("^\[pid [0-9]+\] Hello from user mode$", "^\[process [0-9]+ 'hello' exited, status 0\]$",
        "^\[process [0-9]+ 'exitcode' exited, status 42\]$",
        "^\[process $bgPid 'spin' started in the background\]$", "^ +$bgPid  running +[0-9]+  spin$",
        "^End process $bgPid 'spin'\? Type 'yes' to confirm:$", "^yes/no> no$", "^Kill cancelled\.$",
        "^yes/no> yes$", "^console: kill confirmed for process $bgPid$", "^\[process $bgPid 'spin' killed\]$",
        "^console: Ctrl\+C: kill requested for process [0-9]+$", "^\[process [0-9]+ 'lines' killed\]$",
        "^console: process [0-9]+ collected [0-9]+ ticks after Ctrl\+C$", "^0 processes$")
    Absent "procs" "^key: "
    Absent "procs" "PANIC|Screen output from thread"
}

if (Want "commands") {
    Start-Run
    Wait-Serial "System ready\." 40 | Out-Null
    Start-Sleep -Seconds 1
    Key 39 b9; Start-Sleep -Seconds 1      # opens the console (not typed)
    TypeText "help mem"; Enter
    TypeText "uname -a"; Enter
    TypeText "hostname"; Enter
    $hostLocal = Get-Date
    TypeText "date"; Enter
    TypeText "dmesg 5"; Enter
    TypeText "echo one"; Enter
    Key e0 48 e0 c8; Enter                 # Up recalls "echo one"
    TypeText "history"; Enter
    Start-Sleep -Seconds 1
    Shot "commands"
    TypeText "clear"; Enter
    TypeText "help"; Enter
    Start-Sleep -Seconds 1
    Shot "commands-help"
    TypeText "clear"; Enter
    TypeText "reboot"; Enter
    TypeText "no"; Enter
    Start-Sleep -Seconds 1
    Shot "reboot-cancelled"
    Stop-Run "commands" @("^mem: Show memory information$", "^Usage: mem$",
        "^Madire OS madire $VRe x86_64$", "^madire$",
        "^[0-9]{4}-[0-9]{2}-[0-9]{2} [0-9]{2}:[0-9]{2}:[0-9]{2} \(RTC; time zone not set\)$",
        "^madire> dmesg 5$", "^ +[0-9]+  history$", "^  reboot   Restart the computer \(asks first\)$",
        "^Restart Madire OS now\? Type 'yes' to confirm:$", "^yes/no> no$", "^Restart cancelled\.$")
    $log = Join-Path $OutDir "$Prefix-commands.serial.log"
    if (Test-Path $log) {
        $recalled = @(Select-String -Path $log -Pattern '^madire> echo one$').Count
        Note ("{0,-16} {1,-8} /Up recalled 'echo one': {2} runs (2 expected)/" -f "commands",
              $(if ($recalled -eq 2) { "OK" } else { "MISSING" }), $recalled)
        $d = Select-String -Path $log -Pattern '^([0-9]{4}-[0-9]{2}-[0-9]{2} [0-9]{2}:[0-9]{2}:[0-9]{2}) \(RTC' |
             Select-Object -First 1
        if ($d) {
            $rtc = [datetime]::ParseExact($d.Matches[0].Groups[1].Value, "yyyy-MM-dd HH:mm:ss",
                                          [Globalization.CultureInfo]::InvariantCulture)
            $vsLocal = [int]($rtc - $hostLocal).TotalSeconds
            $vsUtc = [int]($rtc - $hostLocal.ToUniversalTime()).TotalSeconds
            Write-Host ("date (report only): guest RTC {0}; host local {1:yyyy-MM-dd HH:mm:ss}, UTC {2:yyyy-MM-dd HH:mm:ss}; RTC - local = {3} s, RTC - UTC = {4} s" -f
                        $rtc.ToString("yyyy-MM-dd HH:mm:ss"), $hostLocal, $hostLocal.ToUniversalTime(), $vsLocal, $vsUtc)
            $script:results.Add(("date report: RTC - local = {0} s, RTC - UTC = {1} s" -f $vsLocal, $vsUtc))
        }
    }
}
# 0.9.1: "reboot", then "yes": the guest resets the VM through the keyboard
# controller and boots again from the ISO. Reported only: whether VirtualBox
# keeps the COM1 file across the guest's reset (two "boot32: start" lines).
if (Want "reboot") {
    Start-Run
    Wait-Serial "System ready\." 40 | Out-Null
    Start-Sleep -Seconds 1
    Key 39 b9; Start-Sleep -Seconds 1
    TypeText "reboot"; Enter
    TypeText "yes"; Enter
    Start-Sleep -Seconds 3
    Shot "reboot-after-reset"
    Start-Sleep -Seconds 25                 # the BIOS, the boot menu's 3 s, then the second boot (25 s since 0.13: Limine is slower to show)
    Shot "reboot-second-boot"
    $state = (Get-VMInfo)["VMState"]
    Stop-Run "reboot" @("^yes/no> yes$", "^Restarting\.\.\.$", "^console: restart confirmed$",
        "^Restart: keyboard controller reset \(0xfe to port 0x64\)$")
    $log = Join-Path $OutDir "$Prefix-reboot.serial.log"
    if (Test-Path $log) {
        $fallback = @(Select-String -Path $log -Pattern 'did not reset the machine|forcing a triple fault').Count
        Note ("{0,-16} {1,-8} /no fallback to the triple fault: {2} fallback lines/" -f "reboot",
              $(if ($fallback -eq 0) { "OK" } else { "MISSING" }), $fallback)
        $boots = @(Select-String -Path $log -Pattern '^boot32: start$').Count
        $ready = @(Select-String -Path $log -Pattern '^System ready\.').Count
        Write-Host ("reboot (report only): VM state before power-off '{0}'; COM1 log has {1} 'boot32: start' and {2} 'System ready.' lines ({3})" -f
                    $state, $boots, $ready,
                    $(if ($boots -ge 2) { "the log survived the guest reset" } else { "the log did not keep the first boot, or the second boot did not start" }))
        $script:results.Add(("reboot report: VM state '{0}', boot32 lines {1}, System ready lines {2}" -f $state, $boots, $ready))
    }
}
if (Want "preempt") {
    Start-Diag "preempt"
    Wait-Serial "^Preempt test: (PASS|FAIL)$" 40 | Out-Null
    Start-Sleep -Seconds 1
    Shot "preempt"
    Stop-Run "preempt" @("^Preempt test: fair shares: .*: OK$", "^Preempt test: PASS \(")
}
if (Want "threadoverflow") {
    Start-Diag "threadoverflow"
    Wait-Serial $halted 40 | Out-Null
    Start-Sleep -Seconds 1
    Shot "threadoverflow"
    Stop-Run "threadoverflow" @("Double fault: kernel thread stack overflow \(thread [0-9]+ 'overflow'\)", $halted)
}
if (Want "pf") {
    $pf = [ordered]@{
        pfnull = "Address: 0x0000000000000000"; pfunmapped = "Address: 0x0000000040000000";
        pfreadonly = "Address: 0xffffffff80100000"; pfnx = "Details: instruction fetch \(NX violation\)";
        pflow = "Address: 0x0000000000100000"; stackoverflow = "Double fault: kernel stack overflow"
    }
    foreach ($t in $pf.Keys) {
        Start-Diag $t
        Wait-Serial $halted 40 | Out-Null
        Start-Sleep -Seconds 1
        Shot $t
        Stop-Run $t @($pf[$t], $halted)
    }
}
if (Want "diag") {
    Start-Diag "divzero"
    Wait-Serial $halted 40 | Out-Null
    Start-Sleep -Seconds 1
    Shot "divzero"
    Stop-Run "divzero" @("CPU exception 0: Divide Error \(#DE\)", $halted)

    Start-Diag "timer"
    Wait-Serial "^Timer diagnostic: (PASS|FAIL)" 40 | Out-Null    # the result, not "measuring..."
    Start-Sleep -Seconds 1
    Shot "timer"
    Stop-Run "timer" @("^Timer diagnostic: PASS")

    Start-Diag "keyboard"
    Wait-Serial "^Keyboard test: ready" 40 | Out-Null
    TypeText "ab"; Key 2a 1e 9e aa; Key e0 48 e0 c8      # a, b, Shift+A, Up
    Start-Sleep -Seconds 1
    Shot "keyboard"
    Stop-Run "keyboard" @("^key: press .*ascii='a'", "^key: press .*ascii='A' flags=0x03", "^key: press .*key=UP")
}
# 0.13: the four Diagnostics entries no other group boots (positions 1, 2, 10
# and 11), so that every one of the 27 positions is booted through the boot
# menu at least once; the crash lines are the QEMU crash tests' own.
if (Want "diagmore") {
    $more = [ordered]@{
        ud = @("CPU exception 6: Invalid Opcode", "Error code: 0x00000000");
        gpf = @("CPU exception 13: General Protection Fault", "Error code: 0x00000028");
        doublefree = @("KERNEL PANIC", "Double free of physical page 0x[0-9a-f]{8}");
        freereserved = @("KERNEL PANIC", "Attempt to free reserved page 0x00100000")
    }
    foreach ($t in $more.Keys) {
        Start-Diag $t
        Wait-Serial $halted 40 | Out-Null
        Start-Sleep -Seconds 1
        Shot $t
        Stop-Run $t @($more[$t][0], $more[$t][1], $halted)
    }
}
# 0.10: the user-mode Diagnostics entries 16-24, each through the boot menu:
# its PASS line, then System ready, and no panic (the same result lines as
# the QEMU boot-menu checks).
if (Want "user") {
    $user = [ordered]@{
        userspin = @("User-mode test: PASS", 60); userkill = @("User kill test: PASS", 90);
        userfault = @("User fault test: PASS", 90); userpriv = @("User privilege test: PASS", 90);
        userkmem = @("User kernel-memory test: PASS", 90); usersys = @("User syscall test: PASS", 90);
        userio = @("User I/O test: PASS", 90); userpreempt = @("User preemption test: PASS", 90);
        userchurn = @("User churn test: PASS", 300)
    }
    foreach ($t in $user.Keys) {
        $pass = [regex]::Escape($user[$t][0])
        Start-Diag $t
        Wait-Serial "^$pass$|FAIL|PANIC|$halted" $user[$t][1] | Out-Null
        Wait-Serial "^System ready\.|$halted" 20 | Out-Null
        Start-Sleep -Seconds 1
        Shot $t
        Stop-Run $t @("^$pass$", "^System ready\.")
        Absent $t "PANIC"
    }
}
# 0.10: smep and smap (entries 25 and 26). The expected result follows the
# CPU as the VM presents it (the boot's "SMEP:"/"SMAP:" line): enabled ->
# the crash screen with #PF and the Cause line; not supported -> NOT
# APPLICABLE, then System ready.
if (Want "smx") {
    $smx = [ordered]@{
        smep = @("SMEP", "0x00000011", "SMEP violation \(the kernel executed a user page\)", "0x0000000000400000");
        smap = @("SMAP", "0x00000001", "SMAP violation \(the kernel read a user page, AC=0\)", "0x0000000000500000")
    }
    foreach ($t in $smx.Keys) {
        $Feat, $err, $cause, $addr = $smx[$t]      # not $T: PowerShell names ignore case
        Start-Diag $t
        Wait-Serial "^$Feat test: NOT APPLICABLE|$halted" 60 | Out-Null
        Wait-Serial "^System ready\.|$halted" 20 | Out-Null
        Start-Sleep -Seconds 1
        Shot $t
        $on = (Test-Path $serial) -and (Select-String -Path $serial -Pattern "^${Feat}: enabled" -Quiet)
        Write-Host "${t} (report only): the VM's CPU $(if ($on) { "has $Feat (enabled)" } else { "does not report $Feat" })"
        $script:results.Add("$t report: $Feat $(if ($on) { 'enabled' } else { 'not supported' })")
        if ($on) {
            Stop-Run $t @("^$Feat test: user page $addr ", "CPU exception 14: Page Fault \(#PF\)", "Error code: $err",
                          "^ Cause: $cause$", "^ Address: $addr$", $halted)
            Absent $t "System ready|without a fault|NX violation"
        } else {
            Stop-Run $t @("^$Feat test: NOT APPLICABLE \(not supported by this CPU\)$", "^System ready\.")
            Absent $t "PANIC"
        }
    }
}
if (Want "memexhaust") {
    Start-Diag "memexhaust"
    Wait-Serial "^System ready\." 300 | Out-Null
    Start-Sleep -Seconds 2
    Shot "memexhaust"
    Key 39 b9; Start-Sleep -Seconds 1
    TypeText "mem"; Enter
    Start-Sleep -Seconds 2
    Shot "memexhaust-mem"
    Stop-Run "memexhaust" @("^Memory exhaustion test: PASS", "^madire> mem$")
}
if (Want "module") {
    Attach $ModuleIso
    Start-Run
    Wait-Serial "^System ready\." 40 | Out-Null
    Start-Sleep -Seconds 1
    Shot "module"
    Stop-Run "module" @("^Boot module 1: .*test pattern intact", "^Boot module 1: test pattern intact \(14016 bytes\)$")
    Attach $Iso
}

# 0.12 (stage 4): every 0.12 shell command and its error paths, typed
# through keyboardputscancode (set 1 scancodes, US layout) in short bursts
# with pauses, as in the QEMU [0.12] sessions. Each command's line and its
# expected output lines are checked IN ORDER in the serial log (a pattern
# must match after the previous one). The boot calibration is recorded
# (report only), and the uptime heartbeat must stay out of dmesg.
$Scan = New-Object 'System.Collections.Generic.Dictionary[char,int[]]'
foreach ($row in @(@('1234567890-=', '!@#$%^&*()_+', 0x02), @('qwertyuiop[]', 'QWERTYUIOP{}', 0x10),
                   @("asdfghjkl;'``", 'ASDFGHJKL:"~', 0x1e), @('zxcvbnm,./', 'ZXCVBNM<>?', 0x2c))) {
    for ($i = 0; $i -lt $row[0].Length; $i++) {
        $Scan[$row[0][$i]] = @(($row[2] + $i), 0)
        $Scan[$row[1][$i]] = @(($row[2] + $i), 1)
    }
}
$Scan[[char]'\'] = @(0x2b, 0); $Scan[[char]'|'] = @(0x2b, 1); $Scan[[char]' '] = @(0x39, 0)
function ScanCodes([string]$s) {
    $codes = New-Object System.Collections.Generic.List[string]
    foreach ($c in $s.ToCharArray()) {
        if (-not $Scan.ContainsKey($c)) { throw "no scancode for '$c'" }
        $k = $Scan[$c]
        if ($k[1]) { $codes.Add("2a") }
        $codes.Add("{0:x2}" -f $k[0]); $codes.Add("{0:x2}" -f ($k[0] -bor 0x80))
        if ($k[1]) { $codes.Add("aa") }
    }
    return $codes.ToArray()
}
# Types `s` in bursts of `chunk` characters, `pauseMs` apart.
function TypeScan([string]$s, [int]$chunk = 8, [int]$pauseMs = 80) {
    for ($p = 0; $p -lt $s.Length; $p += $chunk) {
        $a = @('controlvm', $VM, 'keyboardputscancode') + (ScanCodes $s.Substring($p, [Math]::Min($chunk, $s.Length - $p)))
        VBox @a | Out-Null
        Start-Sleep -Milliseconds $pauseMs
    }
}
$shellSeq = New-Object System.Collections.Generic.List[string]
$shellPrompt = "madire> "
function Expect([string[]]$p) { foreach ($x in $p) { $script:shellSeq.Add($x) } }
# Types a command and Enter; expects its logged line, then `out` in order.
function Cmd([string]$c, [string[]]$out = @()) {
    TypeScan $c; Enter
    Expect (@("^" + [regex]::Escape($script:shellPrompt + $c) + "$") + $out)
}
# Checks the patterns in order in a kept serial log, one row each.
function InOrder([string]$name, $patterns) {
    $log = Join-Path $OutDir "$Prefix-$name.serial.log"
    if (-not (Test-Path $log)) { return }
    $lines = @(Get-Content -LiteralPath $log)
    $pos = 0
    foreach ($p in $patterns) {
        $hit = -1
        for ($i = $pos; $i -lt $lines.Count; $i++) { if ([regex]::IsMatch($lines[$i], $p)) { $hit = $i; break } }
        if ($hit -ge 0) { $pos = $hit + 1 }
        Note ("{0,-16} {1,-8} /{2}/" -f $name, $(if ($hit -ge 0) { "OK" } else { "MISSING" }), $p)
    }
}
# Waits until `pattern` has matched more than `before` lines; returns the
# last match's first group (a process ID), or "0".
function WaitPid([string]$pattern, [int]$before, [int]$seconds) {
    for ($i = 0; $i -lt $seconds * 2; $i++) {
        $m = @(Select-String -Path $serial -Pattern $pattern -CaseSensitive)
        if ($m.Count -gt $before) { return $m[-1].Matches[0].Groups[1].Value }
        Start-Sleep -Milliseconds 500
    }
    return "0"
}
function CountLines([string]$pattern) { return @(Select-String -Path $serial -Pattern $pattern -CaseSensitive).Count }

if (Want "shell") {
    $ctrlC = { Key 1d 2e ae 9d; Start-Sleep -Seconds 1 }
    $TL = "^time: [0-9]+ ticks? \([0-9]+\.[0-9]{2} s\)$"
    $NOSUCH = "^Unknown command: {0}\. Type 'help' for a list of commands\.$"
    Start-Run
    Wait-Serial "System ready\." 40 | Out-Null
    Start-Sleep -Seconds 1
    Shot "shell-boot"
    Key 39 b9; Start-Sleep -Seconds 1      # opens the console (not typed)
    Cmd "dmesg 40"; Start-Sleep -Seconds 1
    Key 2a e0 49 e0 c9 aa; Start-Sleep -Seconds 1      # Shift+PgUp: the calibration line is above
    Shot "shell-calibration"                           # the next key returns to the bottom

    # help, its sections, "Did you mean"
    Cmd "clear"
    Cmd "help" @("^  time     Time a command: time <command>$", "^Sections: shell system programs \(help <section>\)$")
    Start-Sleep -Seconds 1; Shot "shell-help"
    Cmd "clear"
    Cmd "help shell" @("^shell: The shell's own commands$", "^  help     Show this list$", "^  time     Time a command: time <command>$")
    Cmd "help system" @("^system: Information about the system$", "^  version  Show the Madire OS version$",
                        "^  reboot   Restart the computer \(asks first\)$")
    Cmd "help programs" @("^programs: Programs and processes$", "^  ps       Show threads and processes$",
                          "^  run      Run a program: run \[-b\] <name> \[n\]$", "^  kill     End a process: kill <pid> \(asks first\)$")
    Start-Sleep -Seconds 1; Shot "shell-help-sections"
    Cmd "clear"
    Cmd "help shel" @("^No help for 'shel': unknown command\.$", "^Did you mean: shell, help, set\?$")
    Cmd "help nosuch" @("^No help for 'nosuch': unknown command\.$")
    Cmd "hlep" @(($NOSUCH -f "hlep"), "^Did you mean: help, sleep\?$")
    Cmd "ru" @(($NOSUCH -f "ru"), "^Did you mean: run, cpu, ps\?$")
    Cmd "Echo x" @(($NOSUCH -f "Echo"), "^Did you mean: echo\?$")
    Cmd "kil 5" @(($NOSUCH -f "kil"), "^Did you mean: kill\?$")
    Start-Sleep -Seconds 1; Shot "shell-didyoumean"

    # prompt
    Cmd "clear"
    Cmd "prompt" @("^Prompt: 'madire> ' \(the default\)$")
    Cmd "prompt abc>" @("^Prompt set to 'abc>    '\.$")
    $shellPrompt = "abc>    "
    Cmd "echo under-new-prompt" @("^under-new-prompt$")
    Cmd "prompt" @("^Prompt: 'abc>    '$")
    Cmd "prompt 123456789" @("^prompt: at most 8 characters \(this text has 9\)\.$")
    Cmd "prompt -x" @("^prompt: the text may not start with '-' \(to reset: prompt --reset\)\.$")
    Cmd "prompt a b" @("^Usage: prompt \[text \| --reset\]$")
    Start-Sleep -Seconds 1; Shot "shell-prompt"
    Cmd "prompt --reset" @("^Prompt reset to 'madire> '\.$")
    $shellPrompt = "madire> "
    Cmd "prompt" @("^Prompt: 'madire> ' \(the default\)$")

    # alias, unalias
    Cmd "clear"
    Cmd "alias" @("^0 aliases \(at most 16\)$")
    Cmd "alias ll echo hi there" @("^Alias 'll' added\.$")
    Cmd "ll" @("^hi there$")
    Cmd "ll more" @("^hi there more$")
    Cmd "alias ll echo replaced" @("^Alias 'll' replaced\.$")
    Cmd "ll" @("^replaced$")
    Cmd "alias echo echo x" @("^alias: 'echo' is a built-in command\.$")
    Cmd "alias zz ll" @("^alias: 'll' is not a built-in command\.$")
    Cmd "alias abcdefghijklmnop echo x" @("^alias: a name has at most 15 characters \(this one has 16\)\.$")
    Cmd "alias Bad echo x" @("^alias: a name uses only a-z, 0-9 and _\.$")
    Cmd "unalias nosuch" @("^unalias: no alias named 'nosuch'\.$")
    Cmd "unalias" @("^Usage: unalias <name>$")
    Cmd "alias e5 echo 1 2 3 4 5" @("^Alias 'e5' added\.$")
    Cmd "e5 6 7" @("^1 2 3 4 5 6 7$")
    Cmd "e5 6 7 8" @("^Too many arguments \(max 8\)\.$")
    Cmd "lll" @(($NOSUCH -f "lll"), "^Did you mean: ll, kill\?$")
    Cmd "alias" @("^e5 +echo 1 2 3 4 5$", "^ll +echo replaced$", "^2 aliases \(at most 16\)$")
    Start-Sleep -Seconds 1; Shot "shell-alias"
    Cmd "unalias ll" @("^Alias 'll' removed\.$")
    Cmd "alias" @("^e5 +echo 1 2 3 4 5$", "^1 alias \(at most 16\)$")
    Cmd "clear"
    for ($n = 2; $n -le 16; $n++) { Cmd ("alias t{0:D2} echo {0:D2}" -f $n) @("^Alias 't{0:D2}' added\.$" -f $n) }
    Cmd "alias t17 echo 17" @("^alias: the table is full \(16 aliases\); remove one with unalias\.$")
    Cmd "unalias t16" @("^Alias 't16' removed\.$")
    Cmd "t15" @("^15$")

    # set, unset, export, env
    Cmd "clear"
    Cmd "set" @("^No variables\.$")
    Cmd "env" @("^No exported variables\.$")
    Cmd "set A=1"
    Cmd "set A" @("^A=1$")
    Cmd "set B=b=c"
    Cmd "set E="
    Cmd "set W=x   y  z"
    Cmd "set W" @("^W=x y z$")
    Cmd "set a=small"
    Cmd "set _u=under"
    Cmd "set" @("^A=1$", "^B=b=c$", "^E=$", "^W=x y z$", "^_u=under$", "^a=small$")
    Cmd "set nosuch" @("^set: nosuch is not set$")
    Cmd "set A b" @("^Usage: set \[NAME\[=value\]\] \(names are case-sensitive\)$")
    Cmd "set 1abc=v" @("^set: invalid name: 1abc$")
    Cmd "set =v" @("^set: invalid name: \(empty\)$")
    Cmd "set abcdefghijklmnopq=v" @("^set: invalid name: abcdefghijklmnopq$")
    Cmd ("set V=" + ("v" * 64)) @("^set: value longer than 63 characters$")
    Start-Sleep -Seconds 1; Shot "shell-set"
    Cmd "clear"
    Cmd "export A"
    Cmd "export a"
    Cmd "env" @("^A=1$", "^a=small$")
    Cmd "set A=2"
    Cmd "env" @("^A=2$", "^a=small$")
    Cmd "unset a"
    Cmd "env" @("^A=2$")
    Cmd "unset nosuch" @("^unset: nosuch is not set$")
    Cmd "unset" @("^Usage: unset <NAME>$")
    Cmd "export nosuch" @("^export: nosuch is not set$")
    Cmd "export B=2" @("^export: NAME=value is not supported; use set NAME=value, then export NAME\.$")
    Cmd "export" @("^Usage: export <NAME>$")
    Cmd "env x" @("^Usage: env$")
    Start-Sleep -Seconds 1; Shot "shell-env"
    Cmd "clear"
    for ($n = 0; $n -le 26; $n++) { Cmd ("set V{0:D2}={0}" -f $n) }     # 5 + 27 = 32 variables
    Cmd "set V27=27" @("^set: no room \(32 variables\)$")

    # sleep, presskey, time (keys typed during a wait are ignored: pauses)
    Cmd "clear"
    Cmd "sleep 1" @("^console: sleep 1 s$", "^console: sleep done after [0-9]+ ticks$"); Start-Sleep -Seconds 2
    Cmd "sleep 0" @("^sleep: give whole seconds from 1 to 3600$")
    Cmd "sleep 3601" @("^sleep: give whole seconds from 1 to 3600$")
    Cmd "sleep x" @("^sleep: give whole seconds from 1 to 3600$")
    Cmd "sleep" @("^Usage: sleep <seconds>$")
    Cmd "sleep 3" @("^console: sleep 3 s$"); Start-Sleep -Seconds 1
    & $ctrlC
    Expect @("^console: Ctrl\+C: sleep cancelled after [0-9]+ ticks$", "^sleep: cancelled\.$")
    Cmd "sleep 3" @("^console: sleep 3 s$", "^console: sleep done after [0-9]+ ticks$"); Start-Sleep -Seconds 4
    Cmd "time sleep 1" @("^console: sleep 1 s$", "^console: sleep done after [0-9]+ ticks$", $TL); Start-Sleep -Seconds 2
    Cmd "presskey" @("^Press any key to continue\.\.\.$", "^console: presskey: waiting for a key$"); Start-Sleep -Seconds 1
    Key 10 90; Start-Sleep -Seconds 1        # "q": ends the wait, not typed
    Expect @("^console: presskey: key pressed$")
    Cmd "echo after-presskey" @("^after-presskey$")
    Cmd "presskey" @("^Press any key to continue\.\.\.$"); Start-Sleep -Seconds 1
    & $ctrlC
    Expect @("^console: Ctrl\+C: presskey cancelled$", "^presskey: cancelled\.$")
    Cmd "presskey x" @("^Usage: presskey$")
    Cmd "time echo hi" @("^hi$", $TL)
    Cmd "time time echo x" @("^time: cannot time time$")
    Cmd "time nosuch" @("^time: 'nosuch' is not a built-in command\.$")
    Cmd "alias ll echo hi" @("^Alias 'll' added\.$")
    Cmd "time ll" @("^time: 'll' is an alias; time runs built-in commands only\.$")
    Cmd "time" @("^Usage: time <command\.\.\.>$")
    Start-Sleep -Seconds 1; Shot "shell-wait"

    # run, ps, kill, kill -y, Ctrl+C
    Cmd "clear"
    Cmd "run hello" @("^\[process [0-9]+ 'hello' exited, status 0\]$"); Start-Sleep -Seconds 2
    Shot "shell-run-hello"
    $bgPat = "^\[process ([0-9]+) 'spin' started in the background\]$"
    $before = CountLines $bgPat
    Cmd "run -b spin"
    $p1 = WaitPid $bgPat $before 10
    Expect @("^\[process $p1 'spin' started in the background\]$")
    Cmd "ps" @("^ +$p1  running +[0-9]+  spin$")
    Cmd "kill $p1" @("^End process $p1 'spin'\? Type 'yes' to confirm:$")
    TypeScan "no"; Enter
    Expect @("^yes/no> no$", "^Kill cancelled\.$")
    Cmd "kill $p1" @("^End process $p1 'spin'\? Type 'yes' to confirm:$")
    TypeScan "yes"; Enter; Start-Sleep -Seconds 1
    Expect @("^yes/no> yes$", "^console: kill confirmed for process $p1$", "^\[process $p1 'spin' killed\]$")
    $before = CountLines $bgPat
    Cmd "run -b spin"
    $p2 = WaitPid $bgPat $before 10
    Cmd "kill -y $p2" @("^console: kill -y for process $p2$", "^\[process $p2 'spin' killed\]$"); Start-Sleep -Seconds 1
    Cmd "kill -y 0" @("^kill: PID 0 is the kernel, not a process\.$")
    Cmd "kill -y 99" @("^kill: no process with PID 99\.$")
    Cmd "kill -y $p2" @("^kill: no process with PID $p2\.$")
    Cmd "kill -y x" @("^kill: 'x' is not a process ID\.$")
    Cmd "kill -y" @("^Usage: kill \[-y\] <pid>$")
    $before = CountLines $bgPat
    Cmd "run -b spin"
    $p3 = WaitPid $bgPat $before 10
    Cmd "time kill -y $p3" @("^console: kill -y for process $p3$", "^\[process $p3 'spin' killed\]$", $TL); Start-Sleep -Seconds 1
    Start-Sleep -Seconds 1; Shot "shell-kill"
    Cmd "clear"
    Cmd "run spin"; Start-Sleep -Seconds 2
    & $ctrlC
    Expect @("^console: Ctrl\+C: kill requested for process [0-9]+$", "^\[process [0-9]+ 'spin' killed\]$")
    Start-Sleep -Seconds 1; Shot "shell-ctrlc-spin"
    Cmd "ps" @("^0 processes$")

    # Typing test: 451 characters in bursts; Shift must not stick (the
    # lowercase lines after the Shift-heavy ones come back exactly).
    Cmd "clear"
    foreach ($t in @("The-quick-brown-fox-jumps-over-the-lazy-dog. 0123456789 Done1",
                     "PACK-MY-BOX-WITH-FIVE-DOZEN-LIQUOR-JUGS! ABCDEFGHIJKLM Done2",
                     "MiXeD:CaSe_TeSt+aBcDeF=GhIjKl(MnOpQr)?StUvWx@YZ%123 Done3",
                     "sphinx-of-black-quartz,judge-my-vow;how-vexingly-quick Done4",
                     "!@#%^&*()_+-=:;,.?~ ZYXWVUTSRQPONMLKJIHGFEDCBA 9876543210 Done5",
                     "all-lowercase-now:abcdefghijklmnopqrstuvwxyz-0123456789 Done6",
                     "shift-is-not-stuck-if-this-line-is-lowercase.end Done7")) {
        Cmd "echo $t" @("^" + [regex]::Escape($t) + "$")
    }
    Start-Sleep -Seconds 1; Shot "shell-typing"

    # The uptime heartbeat: serial only, never in dmesg (option B).
    Cmd "clear"
    Cmd "echo before-idle" @("^before-idle$")
    Start-Sleep -Seconds 15
    Cmd "dmesg 5"
    Start-Sleep -Seconds 1; Shot "shell-dmesg-idle"
    Cmd "echo end-of-shell-test" @("^end-of-shell-test$")
    Wait-Serial "^end-of-shell-test$" 30 | Out-Null
    Stop-Run "shell" @("^System ready\.")
    InOrder "shell" $shellSeq
    $log = Join-Path $OutDir "$Prefix-shell.serial.log"
    if (Test-Path $log) {
        $lines = @(Get-Content -LiteralPath $log)
        $cal = Select-String -Path $log -Pattern '^Diagnostic waits: calibrated: at most ([0-9]+) polls per tick' -CaseSensitive |
               Select-Object -First 1
        Note ("{0,-16} {1,-8} /boot calibration logged (report only): {2} polls per tick/" -f "shell",
              $(if ($cal) { "OK" } else { "MISSING" }), $(if ($cal) { $cal.Matches[0].Groups[1].Value } else { "none" }))
        $script:results.Add(("calibration report: {0}" -f $(if ($cal) { $cal.Line } else { "none" })))
        # dmesg 5's 5 lines follow the first "madire> dmesg 5" line.
        $at = -1
        for ($i = 0; $i -lt $lines.Count; $i++) { if ($lines[$i] -ceq "madire> dmesg 5") { $at = $i; break } }
        $dm = if ($at -ge 0) { @($lines[($at + 1)..([Math]::Min($at + 5, $lines.Count - 1))]) } else { @() }
        $ok = $dm.Count -eq 5 -and -not ($dm | Where-Object { $_ -cmatch '^uptime: ' }) -and
              $dm[3] -ceq "madire> echo before-idle" -and $dm[4] -ceq "madire> dmesg 5"
        Note ("{0,-16} {1,-8} /dmesg 5 after 15 s idle: no uptime line, ends with the command before the pause and dmesg 5/" -f
              "shell", $(if ($ok) { "OK" } else { "MISSING" }))
        $hb = 0; $in = $false
        foreach ($l in $lines) {
            if ($l -ceq "madire> echo before-idle") { $in = $true; continue }
            if ($in -and $l -ceq "madire> dmesg 5") { break }
            if ($in -and $l -cmatch '^uptime: [0-9]+ s$') { $hb++ }
        }
        Note ("{0,-16} {1,-8} /the heartbeat is still in the serial log during the idle period: {2} lines/" -f "shell",
              $(if ($hb -ge 2) { "OK" } else { "MISSING" }), $hb)
        $t = Select-String -Path $log -Pattern '^time: ([0-9]+) ticks? \(' -CaseSensitive | ForEach-Object { $_.Matches[0].Groups[1].Value }
        $script:results.Add(("time report (ticks): {0}" -f ($t -join ", ")))
    }
    Absent "shell" "^key: "
    Absent "shell" "PANIC|Screen output from thread|events dropped|Unexpected IRQ"
}
# Repeated normal boots (-Repeat N): the scheduler self-test runs at every
# boot; each boot's result, ping-pong verdict and the preemptions during
# the ping-pong are recorded (VirtualBox can deliver timer ticks in bursts).
if ($Repeat -gt 0 -and ((Want "repeat") -or $Only.Count -eq 0)) {
    for ($n = 1; $n -le $Repeat; $n++) {
        $name = "repeat-{0:D2}" -f $n
        Start-Run
        Wait-Serial "^Scheduler self-test: (PASS|FAIL)" 60 | Out-Null
        Wait-Serial "^System ready\.|System halted" 20 | Out-Null
        VBox controlvm $VM poweroff | Out-Null
        Start-Sleep -Seconds 2
        $copy = Join-Path $OutDir "$Prefix-$name.serial.log"
        if (-not (Test-Path $serial)) { Note "${name}: NO SERIAL LOG"; continue }
        Copy-Item $serial $copy -Force
        $res = (Select-String -Path $copy -Pattern '^Scheduler self-test: (PASS|FAIL)' | Select-Object -First 1)
        $pre = (Select-String -Path $copy -Pattern '^Scheduler self-test: preemptions during the ping-pong: ([0-9]+)' | Select-Object -First 1)
        $pp = (Select-String -Path $copy -Pattern '^Scheduler self-test: cooperative ping-pong: (.*)$' | Select-Object -First 1)
        $ok = $res -and $res.Matches[0].Groups[1].Value -eq "PASS"
        Note ("{0,-16} {1,-8} /self-test {2}; ping-pong preemptions {3}; {4}/" -f $name,
              $(if ($ok) { "OK" } else { "MISSING" }),
              $(if ($res) { $res.Matches[0].Groups[1].Value } else { "no result" }),
              $(if ($pre) { $pre.Matches[0].Groups[1].Value } else { "?" }),
              $(if ($pp) { $pp.Matches[0].Groups[1].Value } else { "?" }))
    }
}

# 0.13: which Diagnostics positions this run booted through the boot menu
# (their result rows above prove the right entry was selected).
if ($script:DiagBooted.Count) {
    $pos = ($script:DiagBooted.Values | Sort-Object) -join ","
    Note ("boot menu (report only): {0} of 27 Diagnostics positions booted through the menu: {1}" -f $script:DiagBooted.Count, $pos)
}

$results | Set-Content -Path $summary -Encoding ascii
$missing =@($results | Where-Object { $_ -match ' MISSING ' -or $_ -match 'NO SERIAL LOG' }).Count
Write-Host ""
Write-Host "Summary: $summary ($missing missing)"
if ($missing) { exit 1 }
