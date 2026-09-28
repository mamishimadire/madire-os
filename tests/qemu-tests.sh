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
passes_06=0          # checks added in 0.6, tagged "[0.6]"; the rest are 0.3 regression

pass() {
    echo "PASS: $1"
    passes=$((passes + 1))
    case "$1" in
    "[0.4]"*) passes_new=$((passes_new + 1)) ;;
    "[0.5]"*) passes_05=$((passes_05 + 1)) ;;
    "[0.6]"*) passes_06=$((passes_06 + 1)) ;;
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
    timeout "$secs" qemu-system-i386 -display none -no-reboot \
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
    timeout "$secs" qemu-system-i386 -display none -no-reboot -serial file:"$log" \
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
    timeout "$secs" qemu-system-i386 -display none -no-reboot -serial file:"$log" \
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
expect "$LOG" "Madire OS v0\.6"                       "version shown"
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
head -c 14000 /dev/urandom > "$OUT/test-module.bin"
boot "$LOG" 10 -m 32M -kernel "$KERNEL" -initrd "$OUT/test-module.bin"
expect "$LOG" "System ready\." "module: boots to System ready"
memory_check "$LOG" "module" --expect-module

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
    expect "$log" "EIP=0x[0-9a-f]{8}  CS=0x0008"        "$name: registers dumped, CS is our GDT code segment"
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
expect_next "$LOG" "[0.5] version"                 "^madire> version$" "^Madire OS v0\.6 \(32-bit x86, Multiboot 1\)$"
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
    "^Mode: +32-bit protected mode, paging on, 1 CPU in use$"
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
VERSIONS=$(grep -c '^Madire OS v0\.6 (32-bit x86, Multiboot 1)$' "$LOG")
if [ "$VERSIONS" -eq 7 ]; then pass "[0.5] repeated commands: 7 identical version outputs"
else fail "[0.5] repeated commands: expected 7 version outputs, got $VERSIONS"; fi
expect "$LOG" "^console: scrolled [0-9]+ lines$"      "[0.5] output scrolls"
expect_in_order "$LOG" "[0.5] clear, then the console keeps working" \
    "^madire> clear$" "^console: cleared$" "^madire> version$" "^Madire OS v0\.6"
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
REGS=$(grep -oE '^Paging: enabled; CR0=0x[0-9a-f]{8} CR3=0x[0-9a-f]{8}' "$LOG" | head -1)
CR0=$(grep -oE 'CR0=0x[0-9a-f]{8}' <<< "$REGS" | cut -d= -f2)
CR3=$(grep -oE 'CR3=0x[0-9a-f]{8}' <<< "$REGS" | cut -d= -f2)
echo "      ${REGS:-no CR0/CR3 line}"
if [ -n "$CR0" ] && [ $(( CR0 & 0x80010000 )) -eq $(( 0x80010000 )) ] &&
   [ -n "$CR3" ] && [ $(( CR3 )) -ne 0 ] && [ $(( CR3 & 0xfff )) -eq 0 ]; then
    pass "[0.6] CR0.PG and CR0.WP set, CR3 holds a page-aligned directory"
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
    expect "$log" "Page fault: $decoded, address 0x$cr2" "[0.6] $name: decoded: $decoded"
    reject "$log" "System ready|not detected"            "[0.6] $name: halted, fault raised"
}
pf_test pfnull     00000000 "not present, read, kernel mode"
pf_test pfunmapped 40000000 "not present, read, kernel mode"
pf_test pfreadonly 00100000 "protection violation, write, kernel mode"

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
heap_panic_test heapdouble  "Heap: double free of 0xd000[0-9a-f]{4}\."
heap_panic_test heapinvalid "Heap: invalid free of 0x[0-9a-f]{8} \(pointer not from the heap\)\."
heap_panic_test heapcorrupt "Heap: corrupted block at 0xd000[0-9a-f]{4} \(while freeing 0xd000[0-9a-f]{4}\)\."

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
    "^Paging: +enabled \(4 KiB pages, identity-mapped kernel\)$" "^Mapped pages: +[0-9]+$" \
    "^Page tables: +[0-9]+$" "^Kernel heap: +[0-9]+ KiB mapped, max 16384 KiB$" \
    "^Heap in use: +[0-9]+ KiB in [0-9]+ blocks?$" "^Heap free: +[0-9]+ KiB, largest block [0-9]+ KiB$"

echo
if [ "$failures" -eq 0 ]; then
    echo "ALL TESTS PASSED ($passes checks: $((passes - passes_new - passes_05 - passes_06)) 0.3 regression + $passes_new from 0.4 + $passes_05 from 0.5 + $passes_06 new in 0.6)"
else
    echo "$failures TEST(S) FAILED"
    exit 1
fi
