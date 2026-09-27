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
failures=0
passes=0

pass() { echo "PASS: $1"; passes=$((passes + 1)); }
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

echo "== Multiboot header"
if grub-file --is-x86-multiboot "$KERNEL"; then pass "kernel has a valid Multiboot header"
else fail "kernel has no valid Multiboot header"; fi

echo "== Normal boot (ISO through GRUB)"
LOG=$OUT/serial-boot.log
boot "$LOG" 15 -cdrom "$ISO"
expect "$LOG" "Logo: 36x24, $LOGO_PIXELS pixels lit" "logo rendered ($LOGO_PIXELS pixels, matches reference)"
expect "$LOG" "M A D I R E   O S"                     "title shown"
expect "$LOG" "Madire OS v0\.3"                       "version shown"
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
    local free
    free=$(grep -oE 'free_pages=[0-9]+' "$log" | head -1 | cut -d= -f2)
    expect "$log" "Memory exhaustion test: PASS \(${free:-X} pages allocated" \
        "exhaust $mem: all ${free:-?} free pages allocated, verified, freed"
    expect "$log" "System ready\." "exhaust $mem: system still ready afterwards"
    reject "$log" "PANIC"          "exhaust $mem: no panic"
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
}

crash_test divzero 0  "Divide Error"             00000000
crash_test ud      6  "Invalid Opcode"           00000000
crash_test gpf     13 "General Protection Fault" 00000028

echo
if [ "$failures" -eq 0 ]; then
    echo "ALL TESTS PASSED ($passes checks)"
else
    echo "$failures TEST(S) FAILED"
    exit 1
fi
