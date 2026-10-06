#!/usr/bin/env bash
# Madire OS automated tests. Boots the kernel headless in QEMU and checks the
# serial output (the kernel mirrors all screen text to COM1).
#
#   bash tests/qemu-tests.sh build/madire.kernel build/madire-os.iso
set -u

KERNEL=$1
ISO=$2
OUT=$(dirname "$KERNEL")
LOGO_PIXELS=293      # from: python3 scripts/logo-preview.py
MEMCHECK=$(dirname "$0")/memory_check.py
QMPKEYS=$(dirname "$0")/qmp_keys.py
failures=0
passes=0
passes_new=0         # checks added in 0.4, tagged "[0.4]"
passes_05=0          # checks added in 0.5, tagged "[0.5]"
passes_06=0          # checks added in 0.6, tagged "[0.6]"
passes_07=0          # checks added in 0.7, tagged "[0.7]"
passes_08=0          # checks added in 0.8, tagged "[0.8]"
passes_09=0          # checks added in 0.9, tagged "[0.9]"; the rest are 0.3 regression

pass() {
    echo "PASS: $1"
    passes=$((passes + 1))
    case "$1" in
    "[0.4]"*) passes_new=$((passes_new + 1)) ;;
    "[0.5]"*) passes_05=$((passes_05 + 1)) ;;
    "[0.6]"*) passes_06=$((passes_06 + 1)) ;;
    "[0.7]"*) passes_07=$((passes_07 + 1)) ;;
    "[0.8]"*) passes_08=$((passes_08 + 1)) ;;
    "[0.9]"*) passes_09=$((passes_09 + 1)) ;;
    esac
}
fail() { echo "FAIL: $1"; failures=$((failures + 1)); }

# memory_check <log> <label> [memory_check.py options...]
# Recomputes the kernel's memory figures independently from its serial log.
memory_check() {
    local log=$1 label=$2 out rc n
    shift 2
    out=$(python3 "$MEMCHECK" "$log" --label "$label" "$@" 2>&1)
    rc=$?
    echo "$out"
    n=$(grep -c '^FAIL' <<< "$out")
    failures=$((failures + n))
    passes=$((passes + $(grep -c '^PASS' <<< "$out")))
    if [ "$rc" -ne 0 ] && [ "$n" -eq 0 ]; then fail "$label: memory_check.py error"; fi
}

# expect <log> <extended regex> <description>
expect() {
    if grep -qE -- "$2" "$1"; then pass "$3"; else fail "$3 (no match for /$2/ in $1)"; fi
}

# reject <log> <extended regex> <description>
reject() {
    if grep -qE -- "$2" "$1"; then fail "$3 (unexpected /$2/ in $1)"; else pass "$3"; fi
}

# boot <log> <seconds> <qemu args...>   (-no-reboot: a triple fault ends the
# run instead of looping, and the expected text will then be missing)
boot() {
    local log=$1 secs=$2
    shift 2
    rm -f "$log"
    timeout "$secs" qemu-system-x86_64 -display none -no-reboot \
        -serial file:"$log" "$@" >/dev/null 2>&1
    [ -f "$log" ] || : > "$log"
}

# boot_keys <log> <seconds> <ready text> <keys> <gap> <qemu args...>
# Like boot, but types <keys> over QMP once <ready text> is in the log
# (see tests/qmp_keys.py for the key syntax).
boot_keys() {
    local log=$1 secs=$2 ready=$3 keys=$4 gap=$5 sock=$OUT/qmp.sock
    shift 5
    rm -f "$log" "$sock"
    timeout "$secs" qemu-system-x86_64 -display none -no-reboot -serial file:"$log" \
        -qmp unix:"$sock",server=on,wait=off "$@" >/dev/null 2>&1 &
    local qpid=$!
    local out
    out=$(python3 "$QMPKEYS" "$sock" "$log" "$ready" "$keys" --gap "$gap" 2>&1)
    echo "$out"
    case "$out" in *ERROR*) fail "[0.4] key injection into $log" ;; esac
    wait "$qpid"
    [ -f "$log" ] || : > "$log"
}

# boot_text <log> <seconds> <ready text> <text> <gap> <done text> <qemu args...>
# Types <text> with qmp_keys.py --text once <ready text> is in the log, waits
# until <done text> is in the log (the guest has processed every key), then
# stops QEMU (instead of waiting for <seconds>).
boot_text() {
    local log=$1 secs=$2 ready=$3 text=$4 gap=$5 done=$6 sock=$OUT/qmp.sock
    shift 6
    rm -f "$log" "$sock"
    timeout "$secs" qemu-system-x86_64 -display none -no-reboot -serial file:"$log" \
        -qmp unix:"$sock",server=on,wait=off "$@" >/dev/null 2>&1 &
    local qpid=$!
    local out
    out=$(python3 "$QMPKEYS" "$sock" "$log" "$ready" --text "$text" --gap "$gap" \
          --done "$done" 2>&1)
    echo "$out"
    case "$out" in *ERROR*) fail "[0.5] key injection into $log" ;; esac
    sleep 2
    kill "$qpid" 2>/dev/null
    wait "$qpid" 2>/dev/null
    [ -f "$log" ] || : > "$log"
}

# expect_next <log> <description> <regex> <next regex>: the first line after
# the one matching <regex>, ignoring "key:" event lines, must match <next regex>.
expect_next() {
    local log=$1 desc=$2
    if awk -v pat="$3" -v nxt="$4" '
            found && !/^key: / { ok = ($0 ~ nxt); exit }
            !found && $0 ~ pat { found = 1 }
            END { exit !ok }' "$log"; then
        pass "$desc"
    else
        fail "$desc (line after /$3/ does not match /$4/ in $log)"
    fi
}

# expect_in_order <log> <description> <regex>...: each regex must match a
# line after the line matched by the previous one.
expect_in_order() {
    local log=$1 desc=$2 line=0 n
    shift 2
    for pattern in "$@"; do
        n=$(awk -v start="$line" -v pat="$pattern" 'NR > start && $0 ~ pat { print NR; exit }' "$log")
        if [ -z "$n" ]; then
            fail "$desc (no /$pattern/ after line $line of $log)"
            return
        fi
        line=$n
    done
    pass "$desc"
}

echo "== Multiboot header"
if grub-file --is-x86-multiboot "$KERNEL"; then pass "kernel has a valid Multiboot header"
else fail "kernel has no valid Multiboot header"; fi

echo "== Normal boot (ISO through GRUB)"
LOG=$OUT/serial-boot.log
# 0.4: also type one key after boot, to prove input works in normal mode.
boot_keys "$LOG" 15 "System ready." "x" 0.15 -cdrom "$ISO"
expect "$LOG" "Logo: 36x24, $LOGO_PIXELS pixels lit" "logo rendered ($LOGO_PIXELS pixels, matches reference)"
expect "$LOG" "M A D I R E   O S"                     "title shown"
expect "$LOG" "Madire OS v0\.9"                       "version shown"
expect "$LOG" "GDT loaded: +OK"                       "GDT loaded"
expect "$LOG" "IDT loaded \(256 entries\): +OK"       "IDT loaded"
expect "$LOG" "Interrupt controller: +OK"             "PIC remapped and masked"
expect "$LOG" "Exception handling: +OK"               "int3 self-test returned"
expect "$LOG" "Display initialization: +OK"           "display initialised"
expect "$LOG" "System ready\."                        "reached System ready"
reject "$LOG" "FAILED|PANIC"                          "no failures or panic"
expect "$LOG" "Memory self-test: PASS"                "boot memory self-test passed"
expect "$LOG" "Memory initialization: +OK"            "memory status OK (after self-test)"
reject "$LOG" "PENDING"                               "no subsystem left pending"
expect "$LOG" "Physical RAM detected: +127 MB"        "panel: physical RAM detected"
expect "$LOG" "Usable RAM \(<4 GiB\): +127 MB"        "panel: usable RAM below 4 GiB"
expect "$LOG" "RAM above 4 GiB: +0 MB"                "panel: RAM above 4 GiB"
expect "$LOG" "Page size: +4096 bytes"                "panel: page size"
expect "$LOG" "Usable pages: +32639$"                 "panel: usable pages"
expect "$LOG" "Free pages: +[0-9]+$"                  "panel: free pages"
memory_check "$LOG" "ISO 128M" --usable-pages 32639 --ram-mb 127 --above4g-mb 0

# 0.4 additions to the normal boot
expect "$LOG" "Timer self-test: PASS \(PIT counting, IRQ0 delivered [0-9]+ ticks, divisor 11932\)" \
                                                      "[0.4] timer self-test: PIT counting, IRQ0 delivered"
expect "$LOG" "Timer \(PIT, 100 Hz\): +OK"            "[0.4] timer status OK"
expect "$LOG" "Keyboard self-test: PASS \(controller set up, echo via IRQ1, decoder and queue checked\)" \
                                                      "[0.4] keyboard self-test: controller, IRQ1, decoder, queue"
expect "$LOG" "Keyboard \(PS/2\): +OK"                "[0.4] keyboard status OK"
expect "$LOG" "IRQ mask: 0xfffc \(expected 0xfffc\)"  "[0.4] only IRQ0 and IRQ1 unmasked"
expect "$LOG" "^uptime: 5 s$"                         "[0.4] uptime advances (5 s logged)"
expect "$LOG" "key: press sc=0x2d key=CHAR ascii='x' flags=0x01" \
                                                      "[0.4] key typed after boot reaches the kernel"
expect "$LOG" "key: release sc=0xad key=CHAR ascii='x' flags=0x00" \
                                                      "[0.4] key release reaches the kernel"
reject "$LOG" "Unexpected IRQ|events dropped"         "[0.4] no unexpected IRQs, no dropped keys"

# 0.5 additions to the normal boot
expect "$LOG" "Console self-test: PASS \(line editor ok, parser ok, shell ok, text grid ok\)" \
                                                      "[0.5] console/shell pure self-test passed"
expect "$LOG" "Console & shell: +OK"                  "[0.5] console status OK"
expect "$LOG" "System ready\. Press any key to open the console\." \
                                                      "[0.5] console offered after System ready"
expect_in_order "$LOG" "[0.5] the typed key opened the console" \
    "System ready\. Press any key" "key: press sc=0x2d" "console: started"

# Memory at different RAM sizes (QEMU's own Multiboot loader). The pinned
# page counts come from the memory maps QEMU 8.2.2 reported when inspected
# with GRUB 2.12 lsmmap; the kernel itself always calculates them from the
# map. A different QEMU version may legitimately change them.
#   usable pages = 159 (below 640 KiB) + whole pages from 1 MiB to the
#   top-of-RAM reserved area; -m 4G also places 1 GiB above 4 GiB.
# ram_test <label> <-m size> <usable pages> <physical MB> <above 4 GiB MB>
ram_test() {
    local label=$1 mem=$2 pages=$3 ram=$4 above=$5
    echo "== Memory: $label"
    local log=$OUT/serial-mem-$label.log
    boot "$log" 10 -m "$mem" -kernel "$KERNEL"
    expect "$log" "System ready\." "$label: boots to System ready"
    reject "$log" "PANIC"          "$label: no panic"
    expect "$log" "Memory self-test: PASS" "$label: boot memory self-test passed"
    expect "$log" "Memory initialization: +OK" "$label: memory status OK"
    expect "$log" "RAM above 4 GiB: +$above MB" "$label: panel shows $above MB above 4 GiB"
    memory_check "$log" "$label" --usable-pages "$pages" --ram-mb "$ram" --above4g-mb "$above"
}

ram_test 16M  16M 3967   15   0
ram_test 32M  32M 8063   31   0
ram_test 1G   1G  262015 1023 0
ram_test 4G   4G  786303 4095 1024

echo "== Memory: boot module reserved (-initrd)"
LOG=$OUT/serial-mem-module.log
# 0.7: a test module (kernel/bootmod.h): "MADIRETM", payload length, FNV-1a
# of the payload, then 14000 random payload bytes. The kernel checks it after
# start-up; "corrupt" flips one payload byte after the checksum is taken.
make_test_module() {
    python3 - "$1" "$2" "${3:-}" <<'PY'
import os, struct, sys
path, length, corrupt = sys.argv[1], int(sys.argv[2]), sys.argv[3] == "corrupt"
payload = bytearray(os.urandom(length))
h = 0x811C9DC5
for b in payload:
    h = ((h ^ b) * 0x01000193) & 0xFFFFFFFF
if corrupt:
    payload[length // 2] ^= 0xFF
open(path, "wb").write(b"MADIRETM" + struct.pack("<II", length, h) + payload)
PY
}
make_test_module "$OUT/test-module.bin" 14000
boot "$LOG" 10 -m 32M -kernel "$KERNEL" -initrd "$OUT/test-module.bin"
expect "$LOG" "System ready\." "module: boots to System ready"
memory_check "$LOG" "module" --expect-module
expect "$LOG" "^Boot module 1: 0x[0-9a-f]{8}-0x[0-9a-f]{8} \(14016 bytes\): test pattern intact" \
                                                      "[0.7] module (QEMU -kernel/-initrd): contents intact after boot"

echo "== [0.7] Boot module: corruption is detected"
LOG=$OUT/serial-mem-module-corrupt.log
make_test_module "$OUT/test-module-corrupt.bin" 14000 corrupt
boot "$LOG" 10 -m 32M -kernel "$KERNEL" -initrd "$OUT/test-module-corrupt.bin"
expect "$LOG" "^Boot module 1: .*: test pattern CORRUPTED \(FNV-1a 0x[0-9a-f]{8}, expected 0x[0-9a-f]{8}\)" \
                                                      "[0.7] module: a changed byte is reported as CORRUPTED"

echo "== [0.7] Boot module through GRUB (ISO with a module line)"
LOG=$OUT/serial-grub-module.log
rm -rf "$OUT/isomod" && mkdir -p "$OUT/isomod/boot/grub"
cp "$KERNEL" "$OUT/isomod/boot/madire.kernel"
cp "$OUT/test-module.bin" "$OUT/isomod/boot/test-module.bin"
cat > "$OUT/isomod/boot/grub/grub.cfg" <<'CFG'
set timeout=0
set default=0
menuentry "Madire OS (module test)" {
    multiboot /boot/madire.kernel
    module /boot/test-module.bin test-module
    boot
}
CFG
if grub-mkrescue -o "$OUT/madire-os-module.iso" "$OUT/isomod" >/dev/null 2>&1; then
    boot "$LOG" 12 -m 128M -cdrom "$OUT/madire-os-module.iso"
    expect "$LOG" "^Boot module 1: 0x[0-9a-f]{8}-0x[0-9a-f]{8} \(14016 bytes\): test pattern intact" \
                                                      "[0.7] module (GRUB): contents intact after boot"
    expect "$LOG" "System ready\."                    "[0.7] module (GRUB): boots to System ready"
else
    fail "[0.7] module (GRUB): grub-mkrescue failed"
fi

# exhaust_test <-m size>: allocate every free page, verify, free them all.
exhaust_test() {
    local mem=$1
    echo "== Memory exhaustion: $mem"
    local log=$OUT/serial-exhaust-$mem.log
    boot "$log" 20 -m "$mem" -kernel "$KERNEL" -append "test=memexhaust"
    local free inuse held expected
    free=$(grep -oE 'free_pages=[0-9]+' "$log" | head -1 | cut -d= -f2)
    # 0.6: paging (page tables + directory) and the kernel heap hold frames
    # before the test starts. The kernel reports them; the test must leave
    # exactly that many alone and allocate everything else.
    inuse=$(grep -oE 'frames in use before the test: [0-9]+ page tables \+ [0-9]+ directory \+ [0-9]+ heap pages = [0-9]+' "$log" | head -1)
    if [ -n "$free" ] && [ -n "$inuse" ]; then
        read -r t d hp held <<< "$(grep -oE '[0-9]+' <<< "$inuse" | tr '\n' ' ')"
        if [ $((t + d + hp)) -eq "$held" ]; then expected=$((free - held)); else held=""; fi
    fi
    expect "$log" "Memory exhaustion test: PASS \(${expected:-X} pages allocated, verified and freed; [0-9]+ checks; ${held:-X} pages already in use left alone\)" \
        "exhaust $mem: all ${expected:-?} free pages allocated, verified, freed (${free:-?} minus ${held:-?} paging + heap frames, exactly ${held:-?} left alone)"
    expect "$log" "System ready\." "exhaust $mem: system still ready afterwards"
    reject "$log" "PANIC"          "exhaust $mem: no panic"
    expect "$log" "IRQ mask: 0xfffc" "[0.4] exhaust $mem: ran with IRQ0/IRQ1 live"
}

exhaust_test 16M
exhaust_test 128M

echo "== Memory: double free must be caught"
LOG=$OUT/serial-doublefree.log
boot "$LOG" 8 -kernel "$KERNEL" -append "test=doublefree"
expect "$LOG" "KERNEL PANIC"                              "doublefree: crash screen shown"
expect "$LOG" "Double free of physical page 0x[0-9a-f]{8}" "doublefree: reported with page address"
reject "$LOG" "System ready|not detected"                  "doublefree: halted, detection worked"

echo "== Memory: freeing a reserved page must be caught"
LOG=$OUT/serial-freereserved.log
boot "$LOG" 8 -kernel "$KERNEL" -append "test=freereserved"
expect "$LOG" "KERNEL PANIC"                                "freereserved: crash screen shown"
expect "$LOG" "Attempt to free reserved page 0x00100000"    "freereserved: kernel page rejected"
reject "$LOG" "System ready|not detected"                    "freereserved: halted, detection worked"

# crash_test <name> <vector> <exception name> <error code>
crash_test() {
    local name=$1 vector=$2 exname=$3 errcode=$4
    echo "== Crash test: $name"
    local log=$OUT/serial-$name.log
    boot "$log" 8 -kernel "$KERNEL" -append "test=$name"
    expect "$log" "KERNEL PANIC"                        "$name: crash screen shown"
    expect "$log" "CPU exception $vector: $exname"      "$name: reports vector $vector ($exname)"
    expect "$log" "Error code: 0x$errcode"              "$name: error code 0x$errcode"
    expect "$log" "RIP=0x[0-9a-f]{16}  CS=0x0008"       "$name: registers dumped, CS is our GDT code segment"
    reject "$log" "System ready"                        "$name: halted instead of continuing"
    expect "$log" "IRQ mask: 0xfffc"                    "[0.4] $name: exception raised with IRQ0/IRQ1 live"
}

crash_test divzero 0  "Divide Error"             00000000
crash_test ud      6  "Invalid Opcode"           00000000
crash_test gpf     13 "General Protection Fault" 00000028

# ---- 0.4: timer, keyboard and IRQ layer ----

echo "== [0.4] Timer rate against the RTC"
# Deterministic time (-icount shift=0,sleep=off -rtc clock=vm) was tried and
# does not reach a result: the diagnostic busy-polls the RTC and each port
# access costs far more real time than the virtual time it advances. The
# documented fallback is QEMU's normal clock with the kernel's +-10% band.
LOG=$OUT/serial-timer.log
boot "$LOG" 15 -m 32M -kernel "$KERNEL" -append "test=timer"
expect "$LOG" "Timer diagnostic: PASS \([0-9]+ ticks in 3 RTC seconds, expected 270-330\)" \
                                                      "[0.4] timer: 300 +-10% ticks in 3 RTC seconds"
grep -o "Timer diagnostic: [A-Z]* ([0-9]* ticks" "$LOG" | sed 's/^/      measured: /'
expect "$LOG" "System ready\."                        "[0.4] timer: system ready afterwards"
reject "$LOG" "PANIC"                                 "[0.4] timer: no panic"

echo "== [0.4] Keyboard: scripted keys via QMP"
LOG=$OUT/serial-keyboard.log
boot_keys "$LOG" 20 "Keyboard test: ready" \
    "a,shift+b,caps_lock,a,caps_lock,shift+1,up,ctrl_r,ret,kp_enter" 0.15 \
    -m 32M -kernel "$KERNEL" -append "test=keyboard"
expect_in_order "$LOG" "[0.4] keyboard: every key decoded, in order" \
    "key: press sc=0x1e key=CHAR ascii='a' flags=0x01" \
    "key: release sc=0x9e key=CHAR ascii='a' flags=0x00" \
    "key: press sc=0x2a key=LSHIFT" \
    "key: press sc=0x30 key=CHAR ascii='B' flags=0x03" \
    "key: press sc=0x3a key=CAPSLOCK ascii=none flags=0x11" \
    "key: press sc=0x1e key=CHAR ascii='A' flags=0x11" \
    "key: press sc=0x3a key=CAPSLOCK ascii=none flags=0x01" \
    "key: press sc=0x02 key=CHAR ascii='!' flags=0x03" \
    "key: press sc=0x48 key=UP ascii=none flags=0x21" \
    "key: press sc=0x1d key=RCTRL ascii=none flags=0x25" \
    "key: press sc=0x1c key=ENTER ascii=none flags=0x01" \
    "key: press sc=0x1c key=KP_ENTER ascii=none flags=0x21"
expect "$LOG" "key: release sc=0x9d key=RCTRL"        "[0.4] keyboard: extended key release decoded"
reject "$LOG" "events dropped|UNKNOWN|PANIC"          "[0.4] keyboard: nothing dropped or unknown"

echo "== [0.4] Keyboard queue overflow"
LOG=$OUT/serial-kbdflood.log
# 50 keys = 100 events (make + break) sent while the kernel is not reading
# for 3 s. The queue must end up full (64 kept, oldest first) and every
# other event must be accounted for: dropped (counted) or, if it arrived
# after the 3 s window, delivered normally afterwards. How many arrive
# inside the window depends on host timing, so the check is conservation
# (kept + dropped + late = 100), not a fixed dropped count.
FLOOD=$(printf 'a,b,c,d,e,f,g,h,i,j,k,l,m,n,o,p,q,r,s,t,u,v,w,x,y,%.0s' 1 2)
boot_keys "$LOG" 20 "Keyboard flood test: ready" "$FLOOD" 0.02 \
    -m 32M -kernel "$KERNEL" -append "test=kbdflood"
expect "$LOG" "Keyboard flood test: kept=64 dropped=[1-9][0-9]* \(queue holds 64\)" \
                                                      "[0.4] flood: queue full (64 kept), overflow dropped and counted"
KEPT=$(grep -oE 'kept=[0-9]+' "$LOG" | head -1 | cut -d= -f2)
DROPPED=$(grep -oE 'kept=[0-9]+ dropped=[0-9]+' "$LOG" | head -1 | sed 's/.*dropped=//')
LATE=$(awk '/Keyboard flood test: kept=/ { after = 1; next } after && /^key: / { n++ } END { print n + 0 }' "$LOG")
echo "      kept=${KEPT:-?} dropped=${DROPPED:-?} late=$LATE"
if [ "$(( ${KEPT:-0} + ${DROPPED:-0} + LATE ))" -eq 100 ]; then
    pass "[0.4] flood: all 100 events accounted for (kept + dropped + late)"
else
    fail "[0.4] flood: kept ${KEPT:-?} + dropped ${DROPPED:-?} + late $LATE != 100 events sent"
fi
expect "$LOG" "Keyboard: ${DROPPED:-X} events dropped \(queue full\)" \
                                                      "[0.4] flood: drop count reported from normal context"
expect_in_order "$LOG" "[0.4] flood: kept events are the oldest, in order" \
    "Keyboard flood test: ready" \
    "key: press sc=0x1e key=CHAR ascii='a'" \
    "key: release sc=0x9e key=CHAR ascii='a'" \
    "key: press sc=0x30 key=CHAR ascii='b'" \
    "key: release sc=0xb0 key=CHAR ascii='b'"
expect "$LOG" "System ready\."                        "[0.4] flood: system ready afterwards"
reject "$LOG" "PANIC"                                 "[0.4] flood: no panic"

echo "== [0.4] Unexpected IRQ"
LOG=$OUT/serial-irqunexpected.log
boot "$LOG" 8 -m 32M -kernel "$KERNEL" -append "test=irqunexpected"
expect "$LOG" "Unexpected IRQ test: PASS \(IRQ 2 counted and acknowledged, total 1\)" \
                                                      "[0.4] unexpected IRQ counted and acknowledged"
expect "$LOG" "Unexpected IRQ 2 \(total 1\)"          "[0.4] unexpected IRQ reported from normal context"
expect "$LOG" "System ready\."                        "[0.4] unexpected IRQ: system ready afterwards"
reject "$LOG" "PANIC"                                 "[0.4] unexpected IRQ: no panic"

# ---- 0.5: console and shell, typed through the real keyboard path ----
# Every key goes QMP -> i8042 -> IRQ1 -> 0.4 driver -> decoder -> key queue
# -> idle loop -> console. The console copies each submitted line to serial
# as "madire> <line>" followed by the command's output lines.

echo "== [0.5] Console session (scripted keys via QMP)"
LOG=$OUT/serial-console.log
A100=$(printf 'a%.0s' $(seq 1 100))
S="x{wait:1}"                                        # first key: only opens the console
S+="{ret}"                                           # 1  empty line
S+="   {ret}"                                        # 2  blank line
S+="help{ret}"                                       # 3
S+="version{ret}"                                    # 4
S+="uptime{ret}{wait:2}uptime{ret}"                  # 5, 6
S+="mem{ret}"                                        # 7
S+="cpu{ret}"                                        # 8
S+="echo   Hello   World!{ret}"                      # 9  Shift + multiple spaces
S+="echo{tab}a{tab}{tab}b{ret}"                      # 10 tabs
S+="{caps}echo abc{caps}{ret}"                       # 11 Caps Lock
S+="echo hellx{bs}o{ret}"                            # 12 Backspace
S+="echo abXc{left}{left}{del}{ret}"                 # 13 Left + Delete
S+="cho world{home}e{end}!{ret}"                     # 14 Home / End / insert
S+="echo hllo{home}{right}{right}{right}{right}{right}{right}e{end}{ret}"   # 15 Right
S+="{ctrl+c}{alt+x}echo ok{ret}"                     # 16 Ctrl / Alt ignored
S+="foo{ret}"                                        # 17 unknown command
S+="version x{ret}"                                  # 18 argument error
S+="echo 1 2 3 4 5 6 7 8{ret}"                       # 19 too many words
S+="echo 1 2 3 4 5 6 7{ret}"                         # 20 exactly 8 words
S+="echo ${A100}{ret}"                               # 21 long input
S+="version{ret}version{ret}version{ret}version{ret}version{ret}"   # 22-26
S+="help{ret}help{ret}help{ret}"                     # 27-29 enough output to scroll
S+="clear{ret}"                                      # 30
S+="version{ret}"                                    # 31
S+="echo end-of-console-test{ret}"                   # 32 marks the end of the session
boot_text "$LOG" 180 "System ready." "$S" 0.03 $'\nend-of-console-test\n' -kernel "$KERNEL"
TAB=$'\t'
A66=$(printf 'a%.0s' $(seq 1 66))

expect_in_order "$LOG" "[0.5] console opens on the first key, which is not typed" \
    "System ready\. Press any key" "console: started" "^madire> $"
PROMPTS=$(grep -c '^madire> ' "$LOG")
if [ "$PROMPTS" -eq 32 ]; then pass "[0.5] Enter: all 32 lines submitted"
else fail "[0.5] Enter: expected 32 submitted lines, got $PROMPTS"; fi
expect_next "$LOG" "[0.5] empty line: new prompt, no output"   "^madire> $"      "^madire>    $"
expect_next "$LOG" "[0.5] blank line: new prompt, no output"   "^madire>    $"   "^madire> help$"
expect_in_order "$LOG" "[0.5] help lists all seven commands" \
    "^madire> help$" "^Available commands:$" "^  help     Show this list$" \
    "^  clear    Clear the screen$" "^  version  Show the Madire OS version$" \
    "^  uptime   Show time since boot$" "^  mem      Show memory information$" \
    "^  cpu      Show processor information$" "^  echo     Print text: echo <text>$"
expect_next "$LOG" "[0.5] version"                 "^madire> version$" "^Madire OS v0\.9 \(64-bit x86-64, Multiboot 1\)$"
UP=$(grep -A1 '^madire> uptime$' "$LOG" | grep -oE '^Uptime: [0-9]{2}:[0-9]{2}:[0-9]{2} \([0-9]+ ticks at 100 Hz\)$')
hms_to_s() { grep -oE '[0-9]{2}:[0-9]{2}:[0-9]{2}' | awk -F: '{ print $1 * 3600 + $2 * 60 + $3 }'; }
U1=$(sed -n 1p <<< "$UP" | hms_to_s)
U2=$(sed -n 2p <<< "$UP" | hms_to_s)
echo "      uptime: ${U1:-?} s, then ${U2:-?} s"
if [ -n "$U1" ] && [ -n "$U2" ] && [ "$U2" -ge $((U1 + 1)) ]; then
    pass "[0.5] uptime: HH:MM:SS with ticks, and it advances"
else
    fail "[0.5] uptime output missing or not advancing ($UP)"
fi
expect_in_order "$LOG" "[0.5] mem shows the 0.3 memory figures" \
    "^madire> mem$" "^Physical RAM detected: +127 MB$" "^Usable RAM \(<4 GiB\): +127 MB$" \
    "^RAM above 4 GiB: +0 MB$" "^Page size: +4096 bytes$" "^Usable pages: +32639$" \
    "^Free pages \(now\): +[0-9]+$" "^Reserved ranges: +[0-9]+$"
expect_in_order "$LOG" "[0.5] cpu reports CPUID information" \
    "^madire> cpu$" "^CPUID: +supported$" "^Vendor: +[A-Za-z]+" \
    "^Family/model: [0-9]+ / [0-9]+, stepping [0-9]+$" "^Features: +[a-z]" \
    "^Mode: +64-bit long mode, paging on, 1 CPU in use$"
expect_next "$LOG" "[0.5] Shift and multiple spaces"   "^madire> echo   Hello   World!$" "^Hello World!$"
expect_next "$LOG" "[0.5] tabs separate arguments"     "^madire> echo${TAB}a${TAB}${TAB}b$" "^a b$"
expect_next "$LOG" "[0.5] Caps Lock (and names are case-sensitive)" \
    "^madire> ECHO ABC$" "^Unknown command: ECHO\. Type 'help' for a list of commands\.$"
# Only the final, edited line is logged: "hellx" + Backspace + "o" -> "hello".
expect_in_order "$LOG" "[0.5] Backspace" "^madire> ECHO ABC$" "^madire> echo hello$" "^hello$"
reject "$LOG" "hellx"                                 "[0.5] Backspace: the deleted character is gone"
expect_next "$LOG" "[0.5] Left and Delete"             "^madire> echo abc$"    "^abc$"
expect_next "$LOG" "[0.5] Home, End and insert at cursor" "^madire> echo world!$" "^world!$"
expect_in_order "$LOG" "[0.5] Right and insert at cursor" \
    "^madire> echo world!$" "^madire> echo hello$" "^hello$"
expect_next "$LOG" "[0.5] Ctrl+key and Alt+key ignored" "^madire> echo ok$"   "^ok$"
expect_next "$LOG" "[0.5] unknown command"             "^madire> foo$" \
    "^Unknown command: foo\. Type 'help' for a list of commands\.$"
expect_next "$LOG" "[0.5] argument error shows usage"  "^madire> version x$" "^Usage: version$"
expect_next "$LOG" "[0.5] more than 8 words refused"   "^madire> echo 1 2 3 4 5 6 7 8$" \
    "^Too many arguments \(max 8\)\.$"
expect_next "$LOG" "[0.5] exactly 8 words accepted"    "^madire> echo 1 2 3 4 5 6 7$" "^1 2 3 4 5 6 7$"
expect "$LOG" "^console: input line full \(71 characters\)$" "[0.5] long input: refusal logged"
expect_next "$LOG" "[0.5] long input: exactly 71 characters kept" "^madire> echo ${A66}$" "^${A66}$"
VERSIONS=$(grep -c '^Madire OS v0\.9 (64-bit x86-64, Multiboot 1)$' "$LOG")
if [ "$VERSIONS" -eq 7 ]; then pass "[0.5] repeated commands: 7 identical version outputs"
else fail "[0.5] repeated commands: expected 7 version outputs, got $VERSIONS"; fi
expect "$LOG" "^console: scrolled [0-9]+ lines$"      "[0.5] output scrolls"
expect_in_order "$LOG" "[0.5] clear, then the console keeps working" \
    "^madire> clear$" "^console: cleared$" "^madire> version$" "^Madire OS v0\.9"
reject "$LOG" "events dropped|PANIC|Unexpected IRQ"   "[0.5] no dropped keys, no panic, no unexpected IRQs"

# ---- 0.6: paging and kernel heap ----

echo "== [0.6] Paging and heap at boot (ISO)"
LOG=$OUT/serial-boot.log
expect "$LOG" "^Paging walk-check: PASS \([0-9]+ items, 0 mismatches\)$" "[0.6] walk-check before enabling: PASS"
expect "$LOG" "^Paging check: page 0 .*expected unmapped, actual unmapped .*: OK$" \
                                                      "[0.6] page 0 (NULL, BIOS data area) left unmapped"
expect "$LOG" "^Paging self-test: TLB flush on remap: .*: OK$"      "[0.6] self-test: TLB flush on remap"
expect "$LOG" "^Paging self-test: physical-access window: .*: OK$" "[0.6] self-test: window map, access, unmap"
expect "$LOG" "^Paging self-test: no page leak: free pages ([0-9]+) -> \1: OK$" \
                                                      "[0.6] paging self-test: no page leak"
expect "$LOG" "^Paging self-test: PASS \([0-9]+ checks, 0 mismatches\)$" "[0.6] paging self-test: PASS"
# 0.7: 4-level paging in long mode also needs CR4.PAE and EFER.LMA.
REGS=$(grep -oE '^Paging: enabled; CR0=0x[0-9a-f]{8} CR3=0x[0-9a-f]{16} CR4=0x[0-9a-f]{16} EFER=0x[0-9a-f]{16}' "$LOG" | head -1)
CR0=$(grep -oE 'CR0=0x[0-9a-f]{8}' <<< "$REGS" | cut -d= -f2)
CR3=$(grep -oE 'CR3=0x[0-9a-f]{16}' <<< "$REGS" | cut -d= -f2)
CR4=$(grep -oE 'CR4=0x[0-9a-f]{16}' <<< "$REGS" | cut -d= -f2)
EFER=$(grep -oE 'EFER=0x[0-9a-f]{16}' <<< "$REGS" | cut -d= -f2)
echo "      ${REGS:-no CR0/CR3 line}"
if [ -n "$CR0" ] && [ $(( CR0 & 0x80010000 )) -eq $(( 0x80010000 )) ] &&
   [ -n "$CR3" ] && [ $(( CR3 )) -ne 0 ] && [ $(( CR3 & 0xfff )) -eq 0 ] &&
   [ -n "$CR4" ] && [ $(( CR4 & 0x20 )) -ne 0 ] && [ -n "$EFER" ] && [ $(( EFER & 0x400 )) -ne 0 ]; then
    pass "[0.6] CR0.PG, CR0.WP, CR4.PAE and EFER.LMA set, CR3 holds a page-aligned PML4"
else
    fail "[0.6] CR0/CR3 after enabling paging: ${REGS:-missing}"
fi
expect "$LOG" "^Paging enabled: +OK"                  "[0.6] status: paging enabled OK"
expect "$LOG" "^Kernel heap self-test: no page leak: .*: OK$" "[0.6] heap self-test: no page leak"
expect "$LOG" "^Kernel heap self-test: PASS \([0-9]+ checks, 0 mismatches\)$" "[0.6] heap self-test: PASS"
expect "$LOG" "^Kernel heap: +OK"                     "[0.6] status: kernel heap OK"
reject "$LOG" "MISMATCH"                              "[0.6] no walk-check or self-test mismatches"

echo "== [0.6] Paging and heap at every RAM size"
for label in 16M 32M 1G 4G module; do
    log=$OUT/serial-mem-$label.log
    expect "$log" "^Paging enabled: +OK" "[0.6] $label: paging enabled"
    expect "$log" "^Kernel heap: +OK"    "[0.6] $label: kernel heap OK"
done

# pf_test <name> <CR2> <decoded line>: a deliberate page fault.
pf_test() {
    local name=$1 cr2=$2 decoded=$3
    echo "== [0.6] Page fault: $name"
    local log=$OUT/serial-$name.log
    boot "$log" 8 -m 32M -kernel "$KERNEL" -append "test=$name"
    expect "$log" "CPU exception 14: Page Fault \(#PF\)" "[0.6] $name: exception 14"
    expect "$log" "CR2=0x$cr2"                           "[0.6] $name: CR2=0x$cr2"
    # 0.7: the address has its own line (no crash-screen line reaches column 80).
    expect_in_order "$log" "[0.6] $name: decoded: $decoded" \
        "^ Page fault: $decoded$" "^ Address: 0x$cr2$"
    reject "$log" "System ready|not detected"            "[0.6] $name: halted, fault raised"
}
pf_test pfnull     0000000000000000 "not present, read, kernel mode"
pf_test pfunmapped 0000000040000000 "not present, read, kernel mode"
pf_test pfreadonly ffffffff80100000 "protection violation, write, kernel mode"   # 0.8: kernel code is high

# heap_panic_test <name> <message regex>: a kfree misuse must be caught.
heap_panic_test() {
    local name=$1 message=$2
    echo "== [0.6] Heap: $name"
    local log=$OUT/serial-$name.log
    boot "$log" 8 -m 32M -kernel "$KERNEL" -append "test=$name"
    expect "$log" "KERNEL PANIC"                         "[0.6] $name: crash screen shown"
    expect "$log" "$message"                             "[0.6] $name: reported"
    reject "$log" "System ready|not detected"            "[0.6] $name: halted, detection worked"
}
heap_panic_test heapdouble  "Heap: double free of 0xfffffe80000[0-9a-f]{5}\."
heap_panic_test heapinvalid "Heap: invalid free of 0x[0-9a-f]{16} \(pointer not from the heap\)\."
heap_panic_test heapcorrupt "Heap: corrupted block at 0xfffffe80000[0-9a-f]{5} \(freeing 0xfffffe80000[0-9a-f]{5}\)\."

echo "== [0.6] Heap exhaustion (32M: the 16 MiB cap is reachable)"
LOG=$OUT/serial-heapexhaust.log
boot "$LOG" 30 -m 32M -kernel "$KERNEL" -append "test=heapexhaust"
expect "$LOG" "Heap exhaustion test: [0-9]+ blocks, arena 16384 KiB \(16 MiB cap reached\), [0-9]+ free pages left, further kmalloc still NULL" \
                                                      "[0.6] heapexhaust: NULL at the 16 MiB cap, and still NULL after"
expect "$LOG" "Heap exhaustion test: PASS \([0-9]+ blocks freed; heap check OK;" \
                                                      "[0.6] heapexhaust: everything freed, heap check OK"
ACCT=$(grep -oE 'free pages [0-9]+ -> [0-9]+ = [0-9]+ heap pages \+ [0-9]+ page tables' "$LOG" | head -1)
read -r HB HA HG HT <<< "$(grep -oE '[0-9]+' <<< "$ACCT" | tr '\n' ' ')"
echo "      ${ACCT:-no page accounting line}"
if [ -n "$HT" ] && [ $((HB - HA)) -eq $((HG + HT)) ]; then
    pass "[0.6] heapexhaust: pages used are exactly the heap's growth plus its page tables"
else
    fail "[0.6] heapexhaust: page accounting does not add up (${ACCT:-missing})"
fi
expect "$LOG" "System ready\."                        "[0.6] heapexhaust: system ready afterwards"
reject "$LOG" "PANIC"                                 "[0.6] heapexhaust: no panic"

echo "== [0.6] Console: mem shows paging and heap"
LOG=$OUT/serial-console.log
expect_in_order "$LOG" "[0.6] mem: paging and heap sections, in order" \
    "^madire> mem$" "^Reserved ranges: +[0-9]+$" \
    "^Paging: +enabled \(4-level, 4 KiB, higher-half, NX (on|off)\)$" "^Mapped pages: +[0-9]+$" \
    "^Page tables: +[0-9]+$" "^Kernel heap: +[0-9]+ KiB mapped, max 16384 KiB$" \
    "^Heap in use: +[0-9]+ KiB in [0-9]+ blocks?$" "^Heap free: +[0-9]+ KiB, largest block [0-9]+ KiB$"

# ---- 0.7: long mode, 4-level layout ----

echo "== [0.7] Long mode active (normal boot, ISO)"
LOG=$OUT/serial-boot.log
expect_in_order "$LOG" "[0.7] stub markers: boot32 start, CPU checks passed, entered 64-bit mode" \
    "^boot32: start$" "^boot32: CPUID ok, long mode and PAE present$" "^boot64: entered 64-bit long mode$"
MODE=$(grep -m1 -oE '^CPU mode: EFER=0x[0-9a-f]{16} .* CR0=0x[0-9a-f]{8} .* CR4=0x[0-9a-f]{16}' "$LOG")
read -r ME MC MR <<< "$(grep -oE '(EFER|CR0|CR4)=0x[0-9a-f]+' <<< "$MODE" | cut -d= -f2 | tr '\n' ' ')"
echo "      ${MODE:-no CPU mode line}"
if [ -n "${MR:-}" ] && [ $(( ME & 0x100 )) -ne 0 ] && [ $(( ME & 0x400 )) -ne 0 ] &&
   [ $(( MC & 0x1 )) -ne 0 ] && [ $(( MC & 0x80000000 )) -ne 0 ] && [ $(( MR & 0x20 )) -ne 0 ]; then
    pass "[0.7] EFER.LME and LMA, CR0.PE and PG, CR4.PAE set (bits checked by the script)"
else
    fail "[0.7] long-mode control bits: ${MODE:-missing}"
fi
DESC=$(grep -m1 -oE '^CPU mode: CS=0x0008 descriptor=0x[0-9a-f]{16} \(L=1 D=0\) TR=0x0018' "$LOG" |
       grep -oE 'descriptor=0x[0-9a-f]{16}' | cut -d= -f2)
if [ -n "$DESC" ] && [ $(( (DESC >> 53) & 1 )) -eq 1 ] && [ $(( (DESC >> 54) & 1 )) -eq 0 ]; then
    pass "[0.7] CS=0x0008 is a 64-bit code segment (descriptor $DESC: L=1, D=0)"
else
    fail "[0.7] CS descriptor not 64-bit code (${DESC:-missing})"
fi
expect "$LOG" "^CPU mode: CS=0x0008 .* TR=0x0018 "     "[0.7] task register 0x18 (the 64-bit TSS is loaded)"
expect "$LOG" "^CPU mode: 64-bit long mode active; 64-bit arithmetic PASS$" \
                                                      "[0.7] kernel reports long mode active; 64-bit multiply/divide above 2^32 correct"
expect "$LOG" "^Paging check: table walk counts .*, actual as expected: OK$" \
                                                      "[0.7] 4-level walk: independent recount of tables and pages matches"

# 0.8: the kernel runs in the higher half (0xFFFFFFFF80000000 + physical);
# the check is the same (RIP lies inside kernel code, which starts at the
# kernel's first page), at the new address.
echo "== [0.7] Kernel at its higher-half address; PML4 slots 508-510 in use"
RIPL=$(grep -m1 -oE '^Paging check: RIP \(running code\) 0x[0-9a-f]{16}-0x[0-9a-f]{16}: expected kernel ro exec, actual kernel ro exec \(1 pages\): OK$' "$LOG")
CODEL=$(grep -m1 -oE '^Paging check: kernel code 0xffffffff80100000-0x[0-9a-f]{16}: expected kernel ro exec, actual kernel ro exec \([0-9]+ pages\): OK$' "$LOG")
RIPA=$(grep -oE '0x[0-9a-f]{16}' <<< "$RIPL" | head -1)
CODEE=$(grep -oE '0x[0-9a-f]{16}' <<< "$CODEL" | sed -n 2p)
# Both are "negative" as signed 64-bit numbers; their order is preserved.
if [ -n "$RIPA" ] && [ -n "$CODEE" ] && [ $((RIPA)) -ge $((0xffffffff80100000)) ] && [ $((RIPA)) -lt $((CODEE)) ]; then
    pass "[0.7] kernel runs at its higher-half address: RIP $RIPA in kernel code 0xffffffff80100000-$CODEE"
else
    fail "[0.7] kernel higher-half address: RIP ${RIPA:-?}, code end ${CODEE:-?}"
fi
pml4() { echo $(( ($1 >> 39) & 511 )); }
TESTP=$(grep -m1 -oE '^Paging self-test: map and read back: 0x[0-9a-f]{16} -> A, .*: OK$' "$LOG" | grep -oE '0x[0-9a-f]{16}' | head -1)
WINP=$(grep -m1 -oE '^Paging check: window page table present 0x[0-9a-f]{16}-.*: OK$' "$LOG" | grep -oE '0x[0-9a-f]{16}' | head -1)
if [ -n "$TESTP" ] && [ -n "$WINP" ] && [ "$(pml4 "$TESTP")" -eq 508 ] && [ "$(pml4 "$WINP")" -eq 508 ]; then
    pass "[0.7] PML4[508]: self-test page $TESTP mapped and read back; window table present at $WINP"
else
    fail "[0.7] PML4[508] in use (test page ${TESTP:-?}, window ${WINP:-?})"
fi
HEAPB=$(grep -m1 -oE '^Kernel heap: [0-9]+ KiB at 0x[0-9a-f]{16}, ' "$LOG" | grep -oE '0x[0-9a-f]{16}')
if [ -n "$HEAPB" ] && [ "$(pml4 "$HEAPB")" -eq 509 ] && grep -qE '^Kernel heap self-test: PASS' "$LOG"; then
    pass "[0.7] PML4[509]: kernel heap mapped at $HEAPB, heap self-test PASS"
else
    fail "[0.7] PML4[509] in use (heap ${HEAPB:-?})"
fi
RECA=$(grep -m1 -oE '^Paging self-test: recursive mapping: PML4\[510\] read at 0x[0-9a-f]{16} = 0x[0-9a-f]{16}: OK$' "$LOG" | grep -oE '0x[0-9a-f]{16}' | head -1)
if [ -n "$RECA" ] && [ "$(pml4 "$RECA")" -eq 510 ] &&
   grep -qE '^Paging check: recursive entry \(PML4\[510\]\) .*: expected points to the PML4, rw nx, actual as expected: OK$' "$LOG"; then
    pass "[0.7] PML4[510]: recursive entry points to the PML4, read back through $RECA"
else
    fail "[0.7] PML4[510] recursive entry (${RECA:-missing})"
fi

# ---- 0.7: no-execute (NX) ----

echo "== [0.7] NX at boot (QEMU default CPU: NX supported)"
LOG=$OUT/serial-boot.log
expect "$LOG" "^NX: supported by the CPU \(CPUID 0x80000001 EDX bit 20\); EFER.NXE enabled \(EFER=0x[0-9a-f]{16}, LMA still set\); NX bits used for everything but kernel code$" \
                                                      "[0.7] NX: supported by the CPU, EFER.NXE enabled, LMA kept"
EFER=$(grep -oE '^Paging: enabled; .* EFER=0x[0-9a-f]{16}' "$LOG" | grep -oE 'EFER=0x[0-9a-f]{16}' | head -1 | cut -d= -f2)
if [ -n "$EFER" ] && [ $(( EFER & 0x800 )) -ne 0 ] && [ $(( EFER & 0x400 )) -ne 0 ]; then
    pass "[0.7] EFER after loading the kernel tables: NXE and LMA set ($EFER)"
else
    fail "[0.7] EFER after loading the kernel tables: ${EFER:-missing}"
fi
expect "$LOG" "^Paging check: kernel code .*: expected kernel ro exec, actual kernel ro exec .*: OK$" \
                                                      "[0.7] kernel code executable (no NX bit)"
expect "$LOG" "^Paging check: interrupt entry stubs \(isr.asm\) .*: expected kernel ro exec, .*: OK$" \
                                                      "[0.7] interrupt entry stubs executable"
expect "$LOG" "^Paging check: panic_exception \(page-fault path\) .*: expected kernel ro exec, .*: OK$" \
                                                      "[0.7] page-fault handler executable"
expect "$LOG" "^Paging check: irq_dispatch \(timer, keyboard\) .*: expected kernel ro exec, .*: OK$" \
                                                      "[0.7] IRQ handlers (timer, keyboard) executable"
expect "$LOG" "^Paging check: kernel rodata .*: expected kernel ro nx, actual kernel ro nx .*: OK$" \
                                                      "[0.7] kernel rodata no-execute"
expect "$LOG" "^Paging check: kernel data\+bss .*: expected kernel rw nx, actual kernel rw nx .*: OK$" \
                                                      "[0.7] kernel data, bss, bitmaps no-execute"
expect "$LOG" "^Paging check: IST1 stack \(double faults\) .*: expected kernel rw nx, actual kernel rw nx .*: OK$" \
                                                      "[0.7] IST1 stack no-execute"
expect "$LOG" "^Paging check: RSP \(current stack\) .*: expected kernel rw nx, actual kernel rw nx .*: OK$" \
                                                      "[0.7] current stack no-execute"
expect "$LOG" "^Kernel heap: [0-9]+ KiB at 0xfffffe8000000000, max 16384 KiB, pages no-execute \(NX bit set\)$" \
                                                      "[0.7] heap pages no-execute"
expect_in_order "$OUT/serial-console.log" "[0.7] mem: NX supported, enabled" \
    "^madire> mem$" "^Page tables: +[0-9]+$" "^NX: +supported, enabled$"

echo "== [0.7] NX: instruction fetch from a no-execute page (test=pfnx)"
LOG=$OUT/serial-pfnx.log
boot "$LOG" 8 -m 32M -kernel "$KERNEL" -append "test=pfnx"
expect "$LOG" "^NX test: executable page: kernel code at 0x[0-9a-f]{16} \(no NX bit\) ran, returned 0x5a5a5a5a \(correct\)$" \
                                                      "[0.7] pfnx: kernel code ran first"
NXPAGE=$(grep -oE '^NX test: data page at 0x[0-9a-f]{16} \(NX bit set\)' "$LOG" | grep -oE '0x[0-9a-f]{16}' | head -1)
echo "      data page: ${NXPAGE:-missing}"
expect "$LOG" "^NX test: data page at ${NXPAGE:-X} \(NX bit set\); NX in use: this must fault; calling it$" \
                                                      "[0.7] pfnx: target page has the NX bit"
expect "$LOG" "CPU exception 14: Page Fault \(#PF\)"  "[0.7] pfnx: exception 14"
expect "$LOG" "Error code: 0x00000011"                "[0.7] pfnx: error code 0x11 (present, instruction fetch)"
expect_in_order "$LOG" "[0.7] pfnx: decoded as an NX violation at the page" \
    "^ Page fault: protection violation, read, kernel mode$" \
    "^ Details: instruction fetch \(NX violation\)$" "^ Address: ${NXPAGE:-X}$"
expect "$LOG" "CR2=${NXPAGE:-X}"                      "[0.7] pfnx: CR2 is the fetched address"
reject "$LOG" "System ready|not detected"             "[0.7] pfnx: halted, fault raised"

echo "== [0.7] CPU without NX (-cpu qemu64,-nx)"
LOG=$OUT/serial-nonx.log
boot "$LOG" 10 -cpu qemu64,-nx -m 32M -kernel "$KERNEL"
expect "$LOG" "^NX: not supported by this CPU \(CPUID 0x80000001 EDX bit 20 clear\); EFER.NXE left clear, no NX bits in the page tables" \
                                                      "[0.7] no NX: reported as not supported"
expect "$LOG" "^Paging walk-check: PASS \([0-9]+ items, 0 mismatches\)$" "[0.7] no NX: walk-check PASS"
expect "$LOG" "^Paging check: kernel data\+bss .*: expected kernel rw exec, actual kernel rw exec .*: OK$" \
                                                      "[0.7] no NX: no NX bits in the tables"
expect "$LOG" "^Paging self-test: PASS \([0-9]+ checks, 0 mismatches\)$" "[0.7] no NX: paging self-test PASS"
expect "$LOG" "^Paging enabled: +OK"                  "[0.7] no NX: paging enabled"
expect "$LOG" "^Kernel heap: +OK"                     "[0.7] no NX: kernel heap OK"
expect "$LOG" "System ready\."                        "[0.7] no NX: boots to System ready"
reject "$LOG" "PANIC|MISMATCH|FAILED"                 "[0.7] no NX: no failures or panic"

echo "== [0.7] CPU without NX: test=pfnx must not fault"
LOG=$OUT/serial-nonx-pfnx.log
boot "$LOG" 10 -cpu qemu64,-nx -m 32M -kernel "$KERNEL" -append "test=pfnx"
expect "$LOG" "^NX test: NOT APPLICABLE: NX not supported by this CPU; the data page executed without a fault, so no-execute protection could not be tested$" \
                                                      "[0.7] no NX: pfnx reports NOT APPLICABLE (not a pass of the NX test)"
expect "$LOG" "^NX test: NOT APPLICABLE \(NX not supported by this CPU\)$" \
                                                      "[0.7] no NX: pfnx screen message says NOT APPLICABLE"
expect "$LOG" "System ready\."                        "[0.7] no NX: pfnx, system ready afterwards"
reject "$LOG" "PANIC"                                 "[0.7] no NX: pfnx, no panic"

# ---- 0.7: stack guard pages, IST1 double-fault stack ----

echo "== [0.7] Stack guard pages (walk-check before enabling, self-test after)"
LOG=$OUT/serial-boot.log
expect "$LOG" "^Paging check: kernel stack 0x[0-9a-f]{16}-0x[0-9a-f]{16}: expected kernel rw nx, actual kernel rw nx \(4 pages\): OK$" \
                                                      "[0.7] kernel stack: 16 KiB, rw nx"
expect "$LOG" "^Paging check: IST1 stack \(double faults\) 0x[0-9a-f]{16}-0x[0-9a-f]{16}: expected kernel rw nx, actual kernel rw nx \(2 pages\): OK$" \
                                                      "[0.7] IST1 stack: 8 KiB, rw nx"
for g in "kernel stack guard page" "IST1 stack guard page"; do
    n=$(grep -cE "^Paging check: $g 0x[0-9a-f]{16}-0x[0-9a-f]{16}: expected unmapped, actual unmapped \(1 pages\): OK$" "$LOG")
    if [ "$n" -eq 2 ]; then pass "[0.7] $g UNMAPPED (walk-check and self-test)"
    else fail "[0.7] $g UNMAPPED (walk-check and self-test): $n of 2 lines"; fi
done
expect "$LOG" "^Paging check: kernel stack guard directly below the stack .*, actual as expected: OK$" \
                                                      "[0.7] kernel stack guard directly below the stack"
expect "$LOG" "^Paging check: IST1 guard directly below the IST1 stack .*, actual as expected: OK$" \
                                                      "[0.7] IST1 guard directly below the IST1 stack"
expect "$LOG" "^Paging check: kernel data\+bss .*, actual kernel rw nx \([0-9]+ pages, 2 guard pages unmapped\): OK$" \
                                                      "[0.7] data+bss: both guard pages left as holes"
KSTACK=$(grep -oE '^Paging check: kernel stack 0x[0-9a-f]{16}' "$LOG" | grep -oE '0x[0-9a-f]{16}' | head -1)
KGUARD=$(grep -oE '^Paging check: kernel stack guard page 0x[0-9a-f]{16}' "$LOG" | grep -oE '0x[0-9a-f]{16}' | head -1)
ISTACK=$(grep -oE '^Paging check: IST1 stack \(double faults\) 0x[0-9a-f]{16}' "$LOG" | grep -oE '0x[0-9a-f]{16}' | head -1)
IGUARD=$(grep -oE '^Paging check: IST1 stack guard page 0x[0-9a-f]{16}' "$LOG" | grep -oE '0x[0-9a-f]{16}' | head -1)
echo "      kernel stack ${KSTACK:-?} (guard ${KGUARD:-?}), IST1 stack ${ISTACK:-?} (guard ${IGUARD:-?})"

# Deep normal use: touching a guard page is a #PF on a present stack
# (or a #DF when the stack is the guard), so none of these may show one.
for l in boot kbdflood console heapexhaust exhaust-16M exhaust-128M; do
    reject "$OUT/serial-$l.log" "CPU exception|Double fault|PANIC" \
                                                      "[0.7] $l: never touched a stack guard page"
done

echo "== [0.7] Kernel stack overflow (test=stackoverflow)"
LOG=$OUT/serial-stackoverflow.log
boot "$LOG" 8 -m 32M -kernel "$KERNEL" -append "test=stackoverflow"
G=$(grep -oE '^Paging check: kernel stack guard page 0x[0-9a-f]{16}' "$LOG" | grep -oE '0x[0-9a-f]{16}' | head -1)
expect "$LOG" "^Stack test: kernel stack 0x[0-9a-f]{16}-0x[0-9a-f]{16}, RSP 0x[0-9a-f]{16}, guard page ${G:-X} \(not mapped\); recursing until the stack overflows$" \
                                                      "[0.7] stackoverflow: started, guard page not mapped"
expect "$LOG" "CPU exception 8: Double Fault \(#DF\)" "[0.7] stackoverflow: exception 8"
expect "$LOG" "Error code: 0x00000000"                "[0.7] stackoverflow: error code 0"
expect "$LOG" "^ Double fault: kernel stack overflow \(RSP is in the stack guard page\)$" \
                                                      "[0.7] stackoverflow: reported as a kernel stack overflow"
RSPG=$(grep -oE '^ RSP=0x[0-9a-f]{16}  guard page 0x[0-9a-f]{16}-0x[0-9a-f]{16}$' "$LOG" | head -1)
read -r R GS GE <<< "$(grep -oE '0x[0-9a-f]{16}' <<< "$RSPG" | tr '\n' ' ')"
echo "      ${RSPG:-no RSP/guard line}"
if [ -n "${GE:-}" ] && [ "$GS" = "${G:-X}" ] && [ $((R)) -ge $((GS)) ] && [ $((R)) -le $((GE)) ]; then
    pass "[0.7] stackoverflow: RSP $R inside the guard page $GS-$GE"
else
    fail "[0.7] stackoverflow: RSP not inside the walk-checked guard page (${RSPG:-missing}, guard ${G:-?})"
fi
expect "$LOG" "RIP=0x[0-9a-f]{16}  CS=0x0008"         "[0.7] stackoverflow: full register dump (handler ran on IST1)"
expect "$LOG" "System halted\. Power off the machine\." "[0.7] stackoverflow: halted"
n=$(grep -c '^boot32: start' "$LOG")
if [ "$n" -eq 1 ]; then pass "[0.7] stackoverflow: no reset (booted once)"
else fail "[0.7] stackoverflow: no reset (boot32: start seen $n times)"; fi
reject "$LOG" "System ready|not detected|recursion returned" \
                                                      "[0.7] stackoverflow: overflow caught, not survived"

# ---- 0.7: no screen line reaches column 80 ----

# The crash screen writes through vga_putc, which moves to the next row
# after column 79; a line of exactly 80 characters would therefore leave a
# blank line (and a longer one wraps). The console (textbuf) has a pending
# wrap instead, but the same limit is used there: at most 79 characters.
# max_width <log> <start regex> <end regex>: longest line from the first
# line matching <start> to the next line matching <end> (both included).
max_width() {
    awk -v s="$2" -v e="$3" '
        !on && $0 ~ s { on = 1 }
        on { if (length($0) > m) m = length($0) }
        on && $0 ~ e && !($0 ~ s) { on = 0 }
        END { print m + 0 }' "$1"
}
echo "== [0.7] Screen width: crash screens and mem output stay within 79 columns"
for l in divzero ud gpf doublefree freereserved pfnull pfunmapped pfreadonly pfnx stackoverflow \
         heapdouble heapinvalid heapcorrupt; do
    w=$(max_width "$OUT/serial-$l.log" "KERNEL PANIC" "System halted")
    if grep -q "KERNEL PANIC" "$OUT/serial-$l.log" && [ "$w" -le 79 ]; then
        pass "[0.7] $l: crash screen lines at most 79 characters (longest $w)"
    else
        fail "[0.7] $l: crash screen line of $w characters (limit 79), or no crash screen"
    fi
done
w=$(max_width "$OUT/serial-console.log" "^madire> mem$" "^madire> ")
if grep -q "^madire> mem$" "$OUT/serial-console.log" && [ "$w" -le 79 ]; then
    pass "[0.7] console mem: every line at most 79 characters (longest $w)"
else
    fail "[0.7] console mem: a line of $w characters (limit 79)"
fi

# ---- 0.7: CPUs that cannot run Madire OS ----

# no_lm_test <label> <expected message> <qemu binary> <args...>: the stub
# prints the message and halts before any mode switch. QEMU must still be
# running at the timeout (exit 124): with -no-reboot a triple fault would
# have ended the run early.
no_lm_test() {
    local label=$1 message=$2 bin=$3 rc
    shift 3
    echo "== [0.7] No 64-bit CPU: $label"
    local log=$OUT/serial-nolm-${label//[^a-z0-9]/-}.log
    rm -f "$log"
    timeout 6 "$bin" -display none -no-reboot -serial file:"$log" -m 32M -kernel "$KERNEL" \
        "$@" >/dev/null 2>&1
    rc=$?
    [ -f "$log" ] || : > "$log"
    expect_in_order "$log" "[0.7] $label: stub started, then refused the CPU" \
        "^boot32: start$" "^$message$"
    reject "$log" "boot32: CPUID ok|boot64|PANIC|System ready" \
                                                      "[0.7] $label: no mode switch attempted"
    if [ "$rc" -eq 124 ]; then pass "[0.7] $label: halted, still running at the timeout (no reset)"
    else fail "[0.7] $label: QEMU exited with $rc before the timeout (reset or crash)"; fi
}
NO_LM="Madire OS needs a 64-bit \(x86-64\) CPU: long mode not supported\. Halted\."
NO_PAE="Madire OS needs a 64-bit \(x86-64\) CPU: PAE not supported\. Halted\."
no_lm_test "qemu32"         "$NO_LM"  qemu-system-x86_64 -cpu qemu32
no_lm_test "i386"           "$NO_LM"  qemu-system-i386
no_lm_test "486"            "$NO_LM"  qemu-system-x86_64 -cpu 486
no_lm_test "qemu64 no lm"   "$NO_LM"  qemu-system-x86_64 -cpu qemu64,-lm
no_lm_test "qemu64 no pae"  "$NO_PAE" qemu-system-x86_64 -cpu qemu64,-pae

# ---- 0.8: higher-half kernel, empty lower half, physical view ----

echo "== [0.8] Higher-half kernel and physical view (normal boot)"
LOG=$OUT/serial-boot.log
expect "$LOG" "^boot64: running at the higher half, RIP=0xffffffff80[0-9a-f]{6}$" \
                                                      "[0.8] boot stub jumped to the higher half"
expect "$LOG" "^Paging check: kernel slot \(PML4\[511\]\) .*, actual as expected: OK$" \
                                                      "[0.8] PML4[511] maps the kernel image"
expect "$LOG" "^Paging check: physical view slot \(PML4\[256\]\) .*, actual as expected: OK$" \
                                                      "[0.8] PML4[256] maps the physical view"
# count_lines <log> <regex> <expected count> <description>
count_lines() {
    local n
    n=$(grep -cE -- "$2" "$1")
    if [ "$n" -eq "$3" ]; then pass "$4"; else fail "$4 ($n lines, expected $3)"; fi
}
count_lines "$LOG" "^Paging check: lower half empty: PML4\[0\] not present .*, actual as expected: OK$" 2 \
    "[0.8] lower half empty: PML4[0] not present (walk-check and self-test)"
count_lines "$LOG" "^Paging check: low address .*: expected unmapped, actual unmapped \(1 pages\): OK$" 10 \
    "[0.8] 0, 0x100000, 0xB8000, 0xFFFFF000 and the Multiboot info address all unmapped (walk-check and self-test)"
count_lines "$LOG" "^Paging check: kernel image not in the physical view 0xffff800000100000-0xffff8000[0-9a-f]{8}: expected unmapped, actual unmapped \([0-9]+ pages\): OK$" 2 \
    "[0.8] no kernel-image page in the physical view (walk-check and self-test)"
count_lines "$LOG" "^Paging check: physical view: page 0 0xffff800000000000-.*: expected unmapped, actual unmapped \(1 pages\): OK$" 2 \
    "[0.8] page 0 not in the physical view (walk-check and self-test)"
expect "$LOG" "^Paging check: PML4 frame \(free RAM\) in the physical view .*: expected unmapped, actual unmapped .*: OK$" \
                                                      "[0.8] free RAM (a page-table frame) not in the physical view"
expect "$LOG" "^Paging check: VGA text buffer 0xffff8000000b8000-.*: expected phys-view rw nx, actual phys-view rw nx .*: OK$" \
                                                      "[0.8] VGA reached through the physical view"
expect "$LOG" "^Paging check: reservation multiboot info 0xffff8000[0-9a-f]{8}-.*: expected phys-view rw nx, actual phys-view rw nx .*: OK$" \
                                                      "[0.8] boot data (Multiboot info) reached through the physical view"
count_lines "$OUT/serial-nonx.log" "^Paging check: lower half empty: PML4\[0\] not present .*: OK$" 2 \
    "[0.8] no NX: lower half empty as well"

echo "== [0.8] Page fault on the kernel's old low address (test=pflow)"
LOG=$OUT/serial-pflow.log
boot "$LOG" 8 -m 32M -kernel "$KERNEL" -append "test=pflow"
expect "$LOG" "^Page-fault test: read at 0x0000000000100000 \(paging on; not mapped\)$" \
                                                      "[0.8] pflow: 0x100000 reported not mapped"
expect "$LOG" "CPU exception 14: Page Fault \(#PF\)"  "[0.8] pflow: exception 14"
expect "$LOG" "Error code: 0x00000000"                "[0.8] pflow: error code 0 (not present, read, kernel)"
expect_in_order "$LOG" "[0.8] pflow: decoded as not present, at 0x100000" \
    "^ Page fault: not present, read, kernel mode$" "^ Address: 0x0000000000100000$"
expect "$LOG" "CR2=0x0000000000100000"                "[0.8] pflow: CR2 is 0x100000"
expect "$LOG" "System halted\. Power off the machine\." "[0.8] pflow: halted"
count_lines "$LOG" "^boot32: start$" 1                "[0.8] pflow: no reset (booted once)"
reject "$LOG" "System ready|not detected"             "[0.8] pflow: the read faulted"
w=$(max_width "$LOG" "KERNEL PANIC" "System halted")
if [ "$w" -le 79 ]; then pass "[0.8] pflow: crash screen lines at most 79 characters (longest $w)"
else fail "[0.8] pflow: crash screen line of $w characters (limit 79)"; fi

# ---- 0.8: stack high-water mark, boot module on screen ----

echo "== [0.8] Kernel stack high-water mark"
expect "$OUT/serial-boot.log" "^Kernel stack: 0x[0-9a-f]{16}-0x[0-9a-f]{16}; painted 0x[0-9a-f]{16}-0x[0-9a-f]{16}, 256 bytes below RSP 0x[0-9a-f]{16} \(live frames untouched\)$" \
                                                      "[0.8] stack painted below RSP minus 256 bytes"
# peak_ok <log> <description>: the last peak line, 0 < peak < 16384.
peak_ok() {
    local p
    p=$(grep -oE '^Kernel stack: +[0-9]+ bytes at peak, of 16384$' "$1" | tail -1 | grep -oE '[0-9]+' | head -1)
    echo "      $(basename "$1"): peak ${p:-?} bytes"
    if [ -n "$p" ] && [ "$p" -gt 0 ] && [ "$p" -lt 16384 ]; then pass "$2 ($p bytes)"
    else fail "$2 (${p:-missing})"; fi
}
peak_ok "$OUT/serial-boot.log"     "[0.8] stack peak at System ready: 0 < peak < 16 KiB"
peak_ok "$OUT/serial-kbdflood.log" "[0.8] stack peak after the keyboard flood: 0 < peak < 16 KiB"
peak_ok "$OUT/serial-console.log"  "[0.8] stack peak after the console session (mem row): 0 < peak < 16 KiB"
expect_in_order "$OUT/serial-console.log" "[0.8] mem: the stack row comes last" \
    "^madire> mem$" "^Heap free: +[0-9]+ KiB, largest block [0-9]+ KiB$" "^Kernel stack: +[0-9]+ bytes at peak, of 16384$"

echo "== [0.8] Boot module result on the screen"
expect "$OUT/serial-mem-module.log" "^Boot module 1: test pattern intact \(14016 bytes\)$" \
                                                      "[0.8] module (QEMU): result on the message line"
expect "$OUT/serial-grub-module.log" "^Boot module 1: test pattern intact \(14016 bytes\)$" \
                                                      "[0.8] module (GRUB): result on the message line"
expect "$OUT/serial-mem-module-corrupt.log" "^Boot module 1: test pattern CORRUPTED \(14016 bytes\)$" \
                                                      "[0.8] corrupted module: shown as CORRUPTED"
reject "$OUT/serial-boot.log" "^Boot module [0-9]"     "[0.8] normal boot: no module message (screen unchanged)"

# ---- 0.8: reproducible build ----

# `make repro` (run by `make test` before this script) built the kernel
# twice from clean, in two trees created in opposite file orders, and
# recorded the sha256 sums in build/repro/sha256.txt.
echo "== [0.8] Reproducible build (make repro)"
SUMS=$OUT/repro/sha256.txt
cat "$SUMS" 2>/dev/null | sed 's/^/      /'
sum_of() { awk -v f="$1" '$2 == f { print $1 }' "$SUMS" 2>/dev/null; }
same_sum() {   # same_sum <description> <file> <file>
    local x y
    x=$(sum_of "$2"); y=$(sum_of "$3")
    if [ -n "$x" ] && [ "$x" = "$y" ]; then pass "$1 ($x)"
    else fail "$1 (${x:-missing} vs ${y:-missing})"; fi
}
same_sum "[0.8] madire.elf byte-identical in two clean builds" a/build/madire.elf b/build/madire.elf
same_sum "[0.8] madire.kernel byte-identical in two clean builds" a/build/madire.kernel b/build/madire.kernel
same_sum "[0.8] the kernel under test is the reproducible one" a/build/madire.kernel ../madire.kernel

# ---- 0.9: kernel threads ----

echo "== [0.9] Scheduler self-test and the thread stack area (normal boot)"
LOG=$OUT/serial-boot.log
expect "$LOG" "^Scheduler self-test: PASS \([0-9]+ checks, 0 mismatches\)$" \
                                                      "[0.9] scheduler self-test: PASS"
expect "$LOG" "^Scheduler self-test: cooperative ping-pong: 100 yields, strictly alternating: OK$" \
                                                      "[0.9] self-test: two threads alternate strictly through yield"
expect "$LOG" "^Scheduler self-test: everything returned: .*: OK$" \
                                                      "[0.9] self-test: frames, stack slots and threads all returned"
expect "$LOG" "^Scheduler self-test: preemptions during the ping-pong: [0-9]+: OK$" \
                                                      "[0.9] self-test: preemptions during the ping-pong logged"
expect "$LOG" "^Scheduler self-test: preemption: a thread that never yields was preempted [0-9]+ times in 30 ticks: OK$" \
                                                      "[0.9] self-test: a thread that never yields is preempted (at least 3 times in 30 ticks)"
expect "$LOG" "^Scheduler self-test: sleep: sleep\(10\) woke after 1[01] ticks \(10 or 11 expected\): OK$" \
                                                      "[0.9] self-test: sleep(10) wakes after 10 or 11 ticks"
expect "$LOG" "^Paging check: kernel thread stack area \(PML4\[507\]\), empty at boot 0xfffffd8000000000-0xfffffd8000200000: expected unmapped, actual unmapped \(512 pages\): OK$" \
                                                      "[0.9] walk-check: thread stack area (PML4[507]) empty at boot"
expect "$LOG" "^Paging check: kernel thread stack page table present .*: OK$" \
                                                      "[0.9] walk-check: thread stack page table made at boot"
reject "$LOG" "Screen output from thread|Boot screen used from thread" \
                                                      "[0.9] normal boot: no screen-rule violation"
expect "$OUT/serial-nonx.log" "^Scheduler self-test: PASS " "[0.9] no NX: scheduler self-test PASS"
expect "$LOG" "^Scheduler self-test: longest turn: [0-9]+ ticks \(slice 5\); highest thread stack peak: [0-9]+ of 16384 bytes$" \
                                                      "[0.9] self-test: longest turn and highest stack peak logged"

# thread_diag <name> <seconds>: boots test=<name> into serial-<name>.log.
thread_diag() {
    LOG=$OUT/serial-$1.log
    boot "$LOG" "$2" -m 32M -kernel "$KERNEL" -append "test=$1"
}
# diag_clean <label>: no panic, no mismatch, System ready afterwards, and the
# longest-turn metric (0.9 known limitation of the deferred switch) logged.
diag_clean() {
    reject "$LOG" "PANIC|MISMATCH"                    "[0.9] $1: no panic, no mismatch"
    expect "$LOG" "System ready\."                     "[0.9] $1: System ready afterwards"
    expect "$LOG" ": longest turn: [0-9]+ ticks \(slice 5\); highest thread stack peak: [0-9]+ of 16384 bytes$" \
                                                      "[0.9] $1: longest turn and highest stack peak logged"
    grep -E ": longest turn: " "$LOG" | tail -1 | sed 's/^/      /'
}

echo "== [0.9] Cooperative context switches (test=threads)"
thread_diag threads 15
expect "$LOG" "^Thread test: create: 8 threads: OK$"  "[0.9] threads: 8 threads created"
expect "$LOG" "^Thread test: order: 800 turns, strict round robin \(w0\.\.w7, 100 rounds\): OK$" \
                                                      "[0.9] threads: 800 turns in strict round-robin order"
expect "$LOG" "^Thread test: callee-saved registers preserved: 800 switches checked, 0 with a changed register: OK$" \
                                                      "[0.9] threads: rbx, rbp, r12-r15 preserved across every switch"
expect "$LOG" "^Thread test: stack contents preserved: 0 mismatches: OK$" \
                                                      "[0.9] threads: each thread's stack contents intact"
expect "$LOG" "^Thread test: interrupt flag 1 after every switch: 0 checks with IF=0: OK$" \
                                                      "[0.9] threads: IF=1 after every switch"
expect "$LOG" "^Thread test: everything returned: .*: OK$" \
                                                      "[0.9] threads: frames, stack slots and threads all returned"
expect "$LOG" "^Thread test: preemptions during the cooperative phase: [0-9]+: OK$" \
                                                      "[0.9] threads: preemptions during the cooperative phase logged"
expect "$LOG" "^Thread test: PASS \("                 "[0.9] threads: PASS"
diag_clean threads

echo "== [0.9] Turn order: a forced preemption takes the relaxed branch (test=pingpongpreempt)"
thread_diag pingpongpreempt 12
grep -E "^Ping-pong preemption test: (cooperative ping-pong|preemptions during|forced)" "$LOG" | sed 's/^/      /'
expect "$LOG" "^Ping-pong preemption test: cooperative ping-pong: 100 yields, order differs at [1-9][0-9]* point\(s\), each right after a preemption: OK$" \
                                                      "[0.9] pingpongpreempt: the order differs only right after the preemption"
expect "$LOG" "^Ping-pong preemption test: forced preemption accepted by the relaxed branch: .*: OK$" \
                                                      "[0.9] pingpongpreempt: accepted through the relaxed branch"
expect "$LOG" "^Ping-pong preemption test: join: exit codes 11 and 22: OK$" \
                                                      "[0.9] pingpongpreempt: both threads completed (codes 11, 22)"
expect "$LOG" "^Ping-pong preemption test: everything returned: .*: OK$" \
                                                      "[0.9] pingpongpreempt: frames, stack slots and threads all returned"
expect "$LOG" "^Ping-pong preemption test: PASS \("   "[0.9] pingpongpreempt: PASS"
diag_clean pingpongpreempt

echo "== [0.9] Turn order: a wrong order without preemption still fails (test=pingpongbad)"
thread_diag pingpongbad 12
grep -E "^Ping-pong order test: a " "$LOG" | sed 's/^/      /'
expect "$LOG" "^Ping-pong order test: a skipped turn without preemption: detected: out of order without a preemption \(100 turns, 0 preemptions\): OK$" \
                                                      "[0.9] pingpongbad: a skipped turn is rejected (out of order)"
expect "$LOG" "^Ping-pong order test: a repeated turn without preemption: detected: wrong number of turns \(101 turns, 0 preemptions\): OK$" \
                                                      "[0.9] pingpongbad: a repeated turn is rejected (wrong count)"
expect "$LOG" "^Ping-pong order test: everything returned: .*: OK$" \
                                                      "[0.9] pingpongbad: frames, stack slots and threads all returned"
expect "$LOG" "^Ping-pong order test: PASS \("        "[0.9] pingpongbad: PASS"
diag_clean pingpongbad

echo "== [0.9] Join, exit codes, detached threads (test=join)"
thread_diag join 15
expect "$LOG" "^Join test: exit codes: 6 of 6 exact .*: OK$" \
                                                      "[0.9] join: exit codes 0, 1, 255, 0x7fffffff, -1, INT32_MIN exact"
expect "$LOG" "^Join test: join by a thread other than 0 \(blocked, then woken\): .*: code 78: OK$" \
                                                      "[0.9] join: a thread other than 0 blocks in join and is woken"
expect "$LOG" "^Join test: detached thread: .*: OK$"  "[0.9] join: detached thread reaped by thread 0"
expect "$LOG" "^Join test: everything returned: .*: OK$" \
                                                      "[0.9] join: frames, stack slots and threads all returned"
expect "$LOG" "^Join test: PASS \("                   "[0.9] join: PASS"
diag_clean join

echo "== [0.9] Interrupt flag across switches (test=ifstate)"
thread_diag ifstate 15
expect "$LOG" "^IF-state test: new thread starts with IF=1 \(trampoline\): IF=1 at the first instruction of a new thread: OK$" \
                                                      "[0.9] ifstate: a new thread starts with IF=1"
expect "$LOG" "^IF-state test: yield with IF=1 resumes with IF=1: 10 yields, 0 resumed with IF=0: OK$" \
                                                      "[0.9] ifstate: yield with IF=1 resumes with IF=1"
expect "$LOG" "^IF-state test: yield with IF=0 resumes with IF=0: resumed with IF=0; meanwhile another thread ran with IF=1 and received a timer tick: OK$" \
                                                      "[0.9] ifstate: yield with IF=0 resumes with IF=0 (own flags)"
expect "$LOG" "^IF-state test: thread 0 keeps IF=1: IF=1 after joining: OK$" \
                                                      "[0.9] ifstate: thread 0 keeps IF=1"
expect "$LOG" "^IF-state test: preempted thread resumes through iretq with its own flags: AC=1 IF=1 after [1-9][0-9]* preemptions; other thread AC=0, thread 0 AC=0: OK$" \
                                                      "[0.9] ifstate: a preempted thread resumes through iretq with its own flags (no leak)"
expect "$LOG" "^IF-state test: PASS \("               "[0.9] ifstate: PASS"
diag_clean ifstate

# preempt_panic <name> <function>: blocking with preemption disabled is a
# kernel bug: crash screen naming the call, the count and the thread.
preempt_panic() {
    echo "== [0.9] $2 with preemption disabled (test=$1)"
    thread_diag "$1" 8
    expect "$LOG" "$2: called with preemption disabled \(count 1, thread 0 'kernel'\)\." \
                                                      "[0.9] $1: panics naming the call, the count and the thread"
    expect "$LOG" "System halted\. Power off the machine\." "[0.9] $1: halted"
    count_lines "$LOG" "^boot32: start$" 1            "[0.9] $1: no reset (booted once)"
    reject "$LOG" "returned \(WRONG\)|System ready"   "[0.9] $1: the call never returned"
    w=$(max_width "$LOG" "KERNEL PANIC" "System halted")
    if [ "$w" -le 79 ]; then pass "[0.9] $1: crash screen lines at most 79 characters (longest $w)"
    else fail "[0.9] $1: crash screen line of $w characters (limit 79)"; fi
}
preempt_panic preemptyield thread_yield
preempt_panic preemptjoin thread_join
preempt_panic preemptsleep thread_sleep

echo "== [0.9] Preemption: three threads that never yield (test=preempt, keys typed meanwhile)"
LOG=$OUT/serial-preempt.log
boot_keys "$LOG" 14 "Preempt test: 3 threads spinning" "a,b,c,d,e,f,g,h" 0.15 \
    -m 32M -kernel "$KERNEL" -append "test=preempt"
grep -E "^Preempt test: (fair shares|preemptions)" "$LOG" | sed 's/^/      /'
expect "$LOG" "^Preempt test: fair shares: .* \(each 25-42%\): OK$" \
                                                      "[0.9] preempt: each thread got 25-42% of the CPU"
expect "$LOG" "^Preempt test: preemptions: [0-9]+ preemptions in 300 ticks .*: OK$" \
                                                      "[0.9] preempt: one preemption per 5-tick slice (40-80 in 300 ticks)"
expect "$LOG" "^Preempt test: every thread made progress: .*: OK$" \
                                                      "[0.9] preempt: every thread made progress"
expect "$LOG" "^Preempt test: timer kept ticking: .*: OK$" "[0.9] preempt: the timer kept ticking"
expect "$LOG" "^Preempt test: everything returned: .*: OK$" \
                                                      "[0.9] preempt: frames, stack slots and threads all returned"
expect "$LOG" "^Preempt test: PASS \("                "[0.9] preempt: PASS"
expect_in_order "$LOG" "[0.9] preempt: keys typed during the test all arrive, in order (IRQ1 not starved)" \
    "^key: press .*ascii='a'" "^key: press .*ascii='b'" "^key: press .*ascii='c'" "^key: press .*ascii='d'" \
    "^key: press .*ascii='e'" "^key: press .*ascii='f'" "^key: press .*ascii='g'" "^key: press .*ascii='h'"
reject "$LOG" "events dropped"                       "[0.9] preempt: no key events dropped"
diag_clean preempt

echo "== [0.9] Deferred switch while preemption is disabled (test=preemptoff)"
thread_diag preemptoff 10
grep -E "^Preemptoff test: switch within" "$LOG" | sed 's/^/      /'
expect "$LOG" "^Preemptoff test: no switch while the guard is held: preemption off for [0-9]+ ticks: no switch, the other thread did not run: OK$" \
                                                      "[0.9] preemptoff: no switch while the guard is held (20 ticks)"
expect "$LOG" "^Preemptoff test: switch request kept pending \(need_resched\) while held: yes: OK$" \
                                                      "[0.9] preemptoff: the switch request stays pending"
expect "$LOG" "^Preemptoff test: switch within one tick after the release: .*: OK$" \
                                                      "[0.9] preemptoff: the other thread runs at most one tick after the release"
expect "$LOG" "^Preemptoff test: everything returned: .*: OK$" \
                                                      "[0.9] preemptoff: frames, stack slots and threads all returned"
expect "$LOG" "^Preemptoff test: PASS \("             "[0.9] preemptoff: PASS"
diag_clean preemptoff

echo "== [0.9] Sleep next to a spinning thread (test=sleep)"
thread_diag sleep 10
for n in 1 10 100; do
    expect "$LOG" "^Sleep test: sleep\($n\): woke after ($n|$((n + 1))) ticks .*: OK$" \
                                                      "[0.9] sleep: sleep($n) wakes after $n or $((n + 1)) ticks"
done
expect "$LOG" "^Sleep test: a sleeping thread uses no CPU: .*: OK$" \
                                                      "[0.9] sleep: the spinner gets at least 95% while the other sleeps"
expect "$LOG" "^Sleep test: thread 0 sleeps by polling: .*: OK$" \
                                                      "[0.9] sleep: thread 0 sleeps 10 ticks by polling (10 or 11)"
expect "$LOG" "^Sleep test: everything returned: .*: OK$" \
                                                      "[0.9] sleep: frames, stack slots and threads all returned"
expect "$LOG" "^Sleep test: PASS \("                  "[0.9] sleep: PASS"
diag_clean sleep

echo "== [0.9] The heap under preemption (test=heapthreads)"
thread_diag heapthreads 12
grep -E "^Heap threads test: concurrent" "$LOG" | sed 's/^/      /'
expect "$LOG" "^Heap threads test: concurrent use under preemption: .*: OK$" \
                                                      "[0.9] heapthreads: 4 threads, thousands of kmalloc/kfree, preempted meanwhile"
expect "$LOG" "^Heap threads test: every payload intact: 0 damaged blocks: OK$" \
                                                      "[0.9] heapthreads: every tagged payload intact"
expect "$LOG" "^Heap threads test: heap consistent and empty again: heap_check OK; .*: OK$" \
                                                      "[0.9] heapthreads: heap_check OK, nothing left in use"
expect "$LOG" "^Heap threads test: frames accounted for exactly .*: OK$" \
                                                      "[0.9] heapthreads: free pages = before - heap growth - new tables, exactly"
expect "$LOG" "^Heap threads test: PASS \("           "[0.9] heapthreads: PASS"
diag_clean heapthreads

echo "== [0.9] The frame allocator and the window under preemption (test=framethreads)"
thread_diag framethreads 12
grep -E "^Frame threads test: concurrent" "$LOG" | sed 's/^/      /'
expect "$LOG" "^Frame threads test: concurrent use under preemption: .*: OK$" \
                                                      "[0.9] framethreads: 4 threads, thousands of allocations, preempted meanwhile"
expect "$LOG" "^Frame threads test: every frame tag intact: 0 frames lost their tag .*: OK$" \
                                                      "[0.9] framethreads: no frame handed out twice (every tag intact)"
expect "$LOG" "^Frame threads test: everything returned: free pages ([0-9]+) -> \1, .*: OK$" \
                                                      "[0.9] framethreads: free pages restored exactly"
expect "$LOG" "^Frame threads test: PASS \("          "[0.9] framethreads: PASS"
diag_clean framethreads

echo "== [0.9] 2000 threads created and freed; per-thread stack peaks (test=threadleak)"
thread_diag threadleak 40
grep -E "^Thread churn test: (stack peak|2000 threads|exit codes)" "$LOG" | sed 's/^/      /'
expect "$LOG" "^Thread churn test: stack peak reported per thread: .*: OK$" \
                                                      "[0.9] threadleak: a thread over 8 KiB deep reports a peak of 8192..16383 bytes"
expect "$LOG" "^Thread 1 'deep' exited: code [0-9]+, stack peak (8[2-9][0-9]{2}|9[0-9]{3}|1[0-5][0-9]{3}|16[0-2][0-9]{2}|163[0-7][0-9]|1638[0-3]) of 16384$" \
                                                      "[0.9] threadleak: its exit line shows that peak"
expect "$LOG" "^Thread churn test: 2000 threads created and freed: 2000 created \([0-9]+ joined, [0-9]+ detached reaped\), 0 create failures: OK$" \
                                                      "[0.9] threadleak: 2000 threads created, run and freed (joined and detached)"
expect "$LOG" "^Thread churn test: exit codes and limits: 0 wrong exit codes; .*: OK$" \
                                                      "[0.9] threadleak: every exit code right, never over the limits"
expect "$LOG" "^Thread churn test: heap unchanged: .*: OK$" "[0.9] threadleak: heap use unchanged"
expect "$LOG" "^Thread churn test: everything returned: free pages ([0-9]+) -> \1, stack slots in use 0, live threads 1: OK$" \
                                                      "[0.9] threadleak: free pages, stack slots and threads exactly as before"
count_lines "$LOG" "^Thread [0-9]+ 'churn' exited" 0  "[0.9] threadleak: no exit line per churn thread (logging off)"
expect "$LOG" "^Thread churn test: PASS \("           "[0.9] threadleak: PASS"
diag_clean threadleak

echo "== [0.9] Every thread's exit line has a stack peak below the stack size"
peaks_ok=0; peaks_bad=0
for l in boot threads join ifstate preempt preemptoff sleep heapthreads framethreads threadleak \
         pingpongpreempt pingpongbad; do
    while read -r p; do
        if [ "$p" -gt 0 ] && [ "$p" -lt 16384 ]; then peaks_ok=$((peaks_ok + 1)); else peaks_bad=$((peaks_bad + 1)); fi
    done < <(grep -oE "^Thread [0-9]+ '[^']*' exited: code -?[0-9]+, stack peak [0-9]+ of 16384$" "$OUT/serial-$l.log" |
             grep -oE 'stack peak [0-9]+' | grep -oE '[0-9]+')
done
echo "      $peaks_ok exit lines in range, $peaks_bad out of range"
if [ "$peaks_ok" -ge 30 ] && [ "$peaks_bad" -eq 0 ]; then
    pass "[0.9] every exit line: 0 < stack peak < 16384 ($peaks_ok threads)"
else
    fail "[0.9] exit-line stack peaks: $peaks_ok in range, $peaks_bad out of range"
fi

echo "== [0.9] Kernel thread stack overflow (test=threadoverflow)"
thread_diag threadoverflow 10
expect "$LOG" "CPU exception 8: Double Fault \(#DF\)" "[0.9] threadoverflow: exception 8 (#DF)"
expect "$LOG" "Double fault: kernel thread stack overflow \(thread [0-9]+ 'overflow'\)" \
                                                      "[0.9] threadoverflow: diagnosed by name"
# The RSP printed with the diagnosis must lie inside the guard range it names.
GL=$(grep -oE "RSP=0x[0-9a-f]{16}  guard 0x[0-9a-f]{16}-0x[0-9a-f]{16}" "$LOG" | head -1)
GR=$(grep -oE '0x[0-9a-f]{16}' <<< "$GL" | sed -n 1p); GS=$(grep -oE '0x[0-9a-f]{16}' <<< "$GL" | sed -n 2p)
GE=$(grep -oE '0x[0-9a-f]{16}' <<< "$GL" | sed -n 3p)
if [ -n "$GR" ] && [[ "$GS" == 0xfffffd80* ]] && [[ ! "$GR" < "$GS" ]] && [[ ! "$GR" > "$GE" ]]; then
    pass "[0.9] threadoverflow: RSP $GR inside the thread's guard $GS-$GE (slot 507)"
else
    fail "[0.9] threadoverflow: RSP ${GR:-?} not inside guard ${GS:-?}-${GE:-?}"
fi
expect "$LOG" "System halted\. Power off the machine\." "[0.9] threadoverflow: halted"
count_lines "$LOG" "^boot32: start$" 1                "[0.9] threadoverflow: no reset (booted once)"
reject "$LOG" "the thread returned \(WRONG\)|System ready" "[0.9] threadoverflow: the thread never returned"
w=$(max_width "$LOG" "KERNEL PANIC" "System halted")
if [ "$w" -le 79 ]; then pass "[0.9] threadoverflow: crash screen lines at most 79 characters (longest $w)"
else fail "[0.9] threadoverflow: crash screen line of $w characters (limit 79)"; fi

echo "== [0.9] Console: ps"
LOG=$OUT/serial-ps.log
boot_text "$LOG" 60 "System ready." "x{ret}ps{ret}help{ret}echo end-of-ps-test{ret}" 0.05 \
    $'\nend-of-ps-test\n' -kernel "$KERNEL"
expect_in_order "$LOG" "[0.9] ps: header, thread 0 'kernel' running, count" \
    "^madire> ps$" "^TID  STATE      TICKS    STACK PEAK    NAME$" \
    "^  0  running +[0-9]+ +[0-9]+/16384   kernel$" "^1 thread$"
expect_in_order "$LOG" "[0.9] help: ps listed after the seven commands" \
    "^madire> help$" "^  echo     Print text: echo <text>$" "^  ps       Show threads$"
w=$(max_width "$LOG" "^madire> ps$" "^madire> help$")
if grep -q "^madire> ps$" "$LOG" && [ "$w" -le 79 ]; then pass "[0.9] ps: every line at most 79 characters (longest $w)"
else fail "[0.9] ps: a line of $w characters (limit 79)"; fi
reject "$LOG" "PANIC|Screen output from thread"      "[0.9] ps: no panic, no screen-rule violation"

# The boot screen rule (0.9): the checker passes on the real sources and
# rejects a violation, both from another file and from non-boot code in
# kernel.c (scratch trees under build/).
echo "== [0.9] Boot screen used only by the boot sequence and BOOTSCREEN_SAFE()"
CHECK=$(dirname "$0")/../scripts/check-bootscreen.sh
if sh "$CHECK" >/dev/null; then pass "[0.9] boot screen check passes on the sources"
else fail "[0.9] boot screen check fails on the sources"; fi
bs_tree() {   # bs_tree <dir>: a scratch tree with the checker and an allowed kernel.c
    rm -rf "$1" && mkdir -p "$1/scripts" "$1/kernel" "$1/drivers"
    cp "$CHECK" "$1/scripts/"
    printf 'void kernel_main(void)\n{\n    bootscreen_ready("x");\n}\n' > "$1/kernel/kernel.c"
}
T=$OUT/bscheck-a; bs_tree "$T"
printf 'void f(void)\n{\n    bootscreen_message("x");\n}\n' > "$T/kernel/other.c"
if sh "$T/scripts/check-bootscreen.sh" >/dev/null; then fail "[0.9] boot screen check missed a call from another file"
else pass "[0.9] boot screen check rejects a call from another file"; fi
T=$OUT/bscheck-b; bs_tree "$T"
printf 'static void idle_loop(void)\n{\n    bootscreen_uptime(1);\n}\n' >> "$T/kernel/kernel.c"
if sh "$T/scripts/check-bootscreen.sh" >/dev/null; then fail "[0.9] boot screen check missed an unwrapped call in kernel.c"
else pass "[0.9] boot screen check rejects an unwrapped call outside the boot sequence"; fi

echo
if [ "$failures" -eq 0 ]; then
    echo "ALL TESTS PASSED ($passes checks: $((passes - passes_new - passes_05 - passes_06 - passes_07 - passes_08 - passes_09)) 0.3 regression + $passes_new from 0.4 + $passes_05 from 0.5 + $passes_06 from 0.6 + $passes_07 from 0.7 + $passes_08 from 0.8 + $passes_09 new in 0.9)"
else
    echo "$failures TEST(S) FAILED"
    exit 1
fi
