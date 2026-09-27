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
failures=0

pass() { echo "PASS: $1"; }
fail() { echo "FAIL: $1"; failures=$((failures + 1)); }

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
expect "$LOG" "Madire OS v0\.2"                       "version shown"
expect "$LOG" "GDT loaded: +OK"                       "GDT loaded"
expect "$LOG" "IDT loaded \(256 entries\): +OK"       "IDT loaded"
expect "$LOG" "Interrupt controller: +OK"             "PIC remapped and masked"
expect "$LOG" "Exception handling: +OK"               "int3 self-test returned"
expect "$LOG" "Display initialization: +OK"           "display initialised"
expect "$LOG" "System ready\."                        "reached System ready"
reject "$LOG" "FAILED|PANIC"                          "no failures or panic"

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
    echo "ALL TESTS PASSED"
else
    echo "$failures TEST(S) FAILED"
    exit 1
fi
