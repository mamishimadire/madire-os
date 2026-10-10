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
    [string[]]$Only = @(),              # menu, boot, procs, commands, reboot, scrollback, preempt, threadoverflow, pf, diag, user, smx, memexhaust, module, repeat
    [int]$KeyDelayMs = 3500,            # keys no sooner than 3 s after power-on
    [int]$Repeat = 0                    # extra normal boots (self-test result of each)
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

# GRUB Diagnostics positions (0.9; 16-26 appended in 0.10): presses of the
# down arrow from the first entry of the submenu.
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

# Boots a Diagnostics entry: down to "Diagnostics", Enter, down to the entry, Enter.
function Start-Diag([string]$test) {
    Start-Run
    Down; Start-Sleep -Milliseconds 500
    Enter; Start-Sleep -Milliseconds 500
    for ($i = 0; $i -lt $Diag[$test]; $i++) { Down }
    Start-Sleep -Milliseconds 300
    Enter
}

function Want([string]$group) { return ($Only.Count -eq 0) -or ($Only -contains $group) }

$halted = "System halted\. Power off the machine\."
Attach $Iso

if (Want "menu") {
    Start-Run
    Down; Start-Sleep -Milliseconds 500
    Shot "grub-menu"
    Enter; Start-Sleep -Milliseconds 500
    Shot "grub-diagnostics-top"
    for ($i = 0; $i -lt 15; $i++) { Down }
    Start-Sleep -Milliseconds 300
    Shot "grub-diagnostics-end"             # entries 14 and 15 at the bottom
    for ($i = 0; $i -lt 11; $i++) { Down }
    Start-Sleep -Milliseconds 300
    Shot "grub-diagnostics-user"            # 0.10: entries 16-26, 26 (smap) selected
    Stop-Run "menu" @()
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
# 0.11: programs from the console (the default GRUB entry loads them):
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
    Start-Sleep -Seconds 15                 # GRUB's 3 s, then the second boot
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
# 0.10: the user-mode Diagnostics entries 16-24, each through GRUB: its
# PASS line, then System ready, and no panic (the same result lines as the
# QEMU GRUB checks).
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

$results | Set-Content -Path $summary -Encoding ascii
$missing = @($results | Where-Object { $_ -match ' MISSING ' -or $_ -match 'NO SERIAL LOG' }).Count
Write-Host ""
Write-Host "Summary: $summary ($missing missing)"
if ($missing) { exit 1 }
