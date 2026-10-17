#!/usr/bin/env bash
# Madire OS automated tests. Boots the kernel headless in QEMU and checks the
# serial output (the kernel mirrors all screen text to COM1).
#
#   bash tests/qemu-tests.sh build/madire.kernel build/madire-os.iso
#   bash tests/qemu-tests.sh build/madire.kernel build/madire-os.iso --shard <part>
#   bash tests/qemu-tests.sh --list-parts
set -u

# 0.12 (stage 0c): the checks are in parts (tests/qemu-part-<part>.sh, the
# shared helpers in tests/qemu-lib.sh). A full run (make test) sources every
# part in this order; a shard run (make test-shard SHARD=<part>) sources one,
# so the regression can run as parallel jobs. Which part a section is in
# changes no check (see docs/testing.md).
PARTS="static 1 2 3 4 final"
# 0.12 stage 2b: parts run only on request (--shard <part>), never in a full
# or a normal sharded run, and not listed by --list-parts. Their checks are
# tagged by part ([stress]) and kept out of the agreed per-tag counts.
EXTRA_PARTS="stress"
if [ "${1:-}" = "--list-parts" ]; then echo "$PARTS"; exit 0; fi

KERNEL=$1
ISO=$2
OUT=$(dirname "$KERNEL")
: > "$OUT/helper-calls.trace"     # 0.12: see helper_trace
rm -rf "$OUT/helper-logs"
LOGO_PIXELS=293      # from: python3 scripts/logo-preview.py
MEMCHECK=$(dirname "$0")/memory_check.py
QMPKEYS=$(dirname "$0")/qmp_keys.py
QMPSCREEN=$(dirname "$0")/qmp_screen.py   # 0.9.2: reads the VGA text screen through QMP

# 0.11: stall watch, a harness diagnostic that decides no check. Every QEMU
# boot below runs through tests/qemu-watch.sh ("qemu-system-x86_64" first
# in PATH): a boot whose serial log stays silent for 6 s is captured
# (registers, PIC, IRQ counts, local APIC; tests/qemu_capture.py) before
# QEMU stops. The captures are listed at the end of the run.
QEMU_REAL=$(command -v qemu-system-x86_64)
QEMU_WATCH_DIR=$OUT/stall
QEMU_WATCH_ELF=$OUT/madire.elf
export QEMU_REAL QEMU_WATCH_DIR QEMU_WATCH_ELF
rm -rf "$QEMU_WATCH_DIR"
mkdir -p "$QEMU_WATCH_DIR/bin"
: > "$QEMU_WATCH_DIR/stalls.txt"
printf '#!/bin/sh\nexec bash "%s" "$@"\n' "$(cd "$(dirname "$0")" && pwd)/qemu-watch.sh" \
    > "$QEMU_WATCH_DIR/bin/qemu-system-x86_64"
chmod +x "$QEMU_WATCH_DIR/bin/qemu-system-x86_64"
PATH=$QEMU_WATCH_DIR/bin:$PATH
failures=0
passes=0
passes_new=0         # checks added in 0.4, tagged "[0.4]"
passes_05=0          # checks added in 0.5, tagged "[0.5]"
passes_06=0          # checks added in 0.6, tagged "[0.6]"
passes_07=0          # checks added in 0.7, tagged "[0.7]"
passes_08=0          # checks added in 0.8, tagged "[0.8]"
passes_09=0          # checks added in 0.9, tagged "[0.9]"
passes_091=0         # checks added in 0.9.1, tagged "[0.9.1]"
passes_092=0         # checks added in 0.9.2, tagged "[0.9.2]"; the rest are 0.3 regression
passes_010=0         # checks added in 0.10, tagged "[0.10]"
passes_011=0         # checks added in 0.11, tagged "[0.11]"
passes_012=0         # checks added in 0.12, tagged "[0.12]"
passes_013=0         # checks added in 0.13, tagged "[0.13]"
passes_10=0          # checks added in 1.0, tagged "[1.0]"
passes_11=0          # checks added in 1.1, tagged "[1.1]"
passes_stress=0      # 0.12 stage 2b: the stress part's checks, "[stress]" (on request only)

# 0.10: the version is read once, from kernel/kernel.h, and every check that
# contains the version builds its regex from VRE, so a new version changes no
# check. A missing or malformed version stops the run (an empty VRE would
# make those checks match any version).
VERSION=$(sed -n 's/^#define MADIRE_OS_VERSION "\([0-9][0-9.]*\)"$/\1/p' "$(dirname "$0")/../kernel/kernel.h")
case "$VERSION" in
""|*[!0-9.]*|.*|*.|*..*) echo "FATAL: cannot read MADIRE_OS_VERSION from kernel/kernel.h"; exit 1 ;;
esac
VRE=$(printf '%s' "$VERSION" | sed 's/\./\\./g')    # 0.9.2 -> 0\.9\.2 (sed: same in every bash version)

# 0.12 (stage 1): the hashes of the kernel and of every program module, in
# regression.log in a full and in every sharded job, so the stage report
# can compare them across jobs. They decide no check.
sha256sum "$OUT/madire.elf" "$KERNEL" "$OUT"/user/*.elf 2>&1 | sed 's/^/BUILD_SHA256 /'

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
    "[0.9.1]"*) passes_091=$((passes_091 + 1)) ;;
    "[0.9.2]"*) passes_092=$((passes_092 + 1)) ;;
    "[0.10]"*) passes_010=$((passes_010 + 1)) ;;
    "[0.11]"*) passes_011=$((passes_011 + 1)) ;;
    "[0.12]"*) passes_012=$((passes_012 + 1)) ;;
    "[0.13]"*) passes_013=$((passes_013 + 1)) ;;
    "[1.0]"*) passes_10=$((passes_10 + 1)) ;;
    "[1.1]"*) passes_11=$((passes_11 + 1)) ;;
    "[stress]"*) passes_stress=$((passes_stress + 1)) ;;
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
    local log=$1 secs=$2 t0
    shift 2
    rm -f "$log"
    t0=$(date +%s%N)
    timeout "$secs" qemu-system-x86_64 -display none -no-reboot \
        -serial file:"$log" "$@" >/dev/null 2>&1
    [ -f "$log" ] || : > "$log"
    boot_seconds "$log" "$t0"
}

# boot_seconds <log> <start, date +%s%N>: 1.0 stage 3, report only (decides
# no check): "BOOT_SECONDS <log> <s>", the boot's wall time in seconds with
# one decimal, printed by boot and boot_until.
boot_seconds() {
    local ms=$(( ($(date +%s%N) - $2) / 1000000 ))
    echo "BOOT_SECONDS ${1##*/} $((ms / 1000)).$((ms % 1000 / 100))"
}

# boot_keys <log> <seconds> <ready text> <keys> <gap> <qemu args...>
# Like boot, but types <keys> over QMP once <ready text> is in the log
# (see tests/qmp_keys.py for the key syntax).
boot_keys() {
    local log=$1 secs=$2 ready=$3 keys=$4 gap=$5 sock=$OUT/qmp.sock
    shift 5
    rm -f "$log" "$sock" "${log%.log}.keys.txt" "${log%.log}.irq1.txt"
    QEMU_WATCH_IRQ1=1 timeout "$secs" qemu-system-x86_64 -display none -no-reboot -serial file:"$log" \
        -qmp unix:"$sock",server=on,wait=off "$@" >/dev/null 2>&1 &
    local qpid=$!
    local out
    out=$(python3 "$QMPKEYS" "$sock" "$log" "$ready" "$keys" --gap "$gap" 2>&1)
    echo "$out"
    # 0.11 (harness addition, decides no check): the injector's output and
    # the IRQ 1 counts (stall watch, at the time limit) are kept next to
    # the serial log, so a boot with missing keys can be classified.
    echo "$out" > "${log%.log}.keys.txt"
    case "$out" in *ERROR*) fail "[0.4] key injection into $log" ;; esac
    wait "$qpid"
    [ -f "$log" ] || : > "$log"
    if [ -f "${log%.log}.irq1.txt" ]; then sed 's/^/      /' "${log%.log}.irq1.txt"; fi
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
# 0.12: patterns reach awk through the environment (ENVIRON), never "-v":
# "-v" processes backslash escapes, and gawk drops the backslash in "\(" or
# "\." where mawk keeps it, so a check could mean different things on
# different hosts. ENVIRON passes the pattern exactly as written.
# helper_trace <fields...>: one record per pattern-helper call in
# $OUT/helper-calls.trace (fields separated by 0x1f), so the calls can be
# replayed under other awk implementations (0.12). The log is saved as it
# is at the call, under its SHA-256 in $OUT/helper-logs/ (some log files
# are rewritten by later boots), and the record names that copy. It
# decides nothing.
helper_trace() {
    local kind=$1 log=$2 sum IFS=$'\x1f'
    shift 2
    sum=$(sha256sum < "$log" 2>/dev/null) || return 0
    sum=${sum%% *}
    mkdir -p "$OUT/helper-logs"
    [ -f "$OUT/helper-logs/$sum" ] || cp "$log" "$OUT/helper-logs/$sum"
    printf '%s\n' "$kind$IFS$OUT/helper-logs/$sum$IFS$*" >> "$OUT/helper-calls.trace"
}
expect_next() {
    local log=$1 desc=$2
    helper_trace next "$log" "$3" "$4"
    if PAT="$3" NXT="$4" awk '
            found && !/^key: / { ok = ($0 ~ ENVIRON["NXT"]); exit }
            !found && $0 ~ ENVIRON["PAT"] { found = 1 }
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
        helper_trace inorder "$log" "$line" "$pattern"
        n=$(START="$line" PAT="$pattern" awk 'NR > ENVIRON["START"] + 0 && $0 ~ ENVIRON["PAT"] { print NR; exit }' "$log")
        if [ -z "$n" ]; then
            fail "$desc (no /$pattern/ after line $line of $log)"
            return
        fi
        line=$n
    done
    pass "$desc"
}

# 0.12: the parts this run takes: all of them, or the one after --shard.
SHARD=all
if [ "${3:-}" = "--shard" ]; then
    SHARD=${4:-}
    case " $PARTS $EXTRA_PARTS " in
    *" $SHARD "*) ;;
    *) echo "FATAL: unknown part '$SHARD' (parts: $PARTS; on request: $EXTRA_PARTS)"; exit 1 ;;
    esac
fi
. "$(dirname "$0")/qemu-lib.sh"
for qt_part in $PARTS $EXTRA_PARTS; do
    # 0.12 stage 2b: a part in EXTRA_PARTS runs only when asked for by name.
    case " $EXTRA_PARTS " in *" $qt_part "*) [ "$SHARD" = "$qt_part" ] || continue ;; esac
    [ "$SHARD" = all ] || [ "$SHARD" = "$qt_part" ] || continue
    echo "SHARD_PART_BEGIN $qt_part"
    . "$(dirname "$0")/qemu-part-$qt_part.sh"
    echo "SHARD_PART_END $qt_part"
done

echo "== Stall watch (harness diagnostic; decides no check)"
echo "      $(grep -c '^STALL CAPTURE' "$QEMU_WATCH_DIR/stalls.txt") capture(s) in $QEMU_WATCH_DIR"
sed 's/^/      /' "$QEMU_WATCH_DIR/stalls.txt"

# 0.12: the counts of this run in one line (tests/check-counts.sh sums the
# lines of a sharded run). A shard run ends here.
COUNTS="SHARD_COUNTS part=$SHARD failures=$failures passes=$passes 0.3=$((passes - passes_stress - passes_new - passes_05 - passes_06 - passes_07 - passes_08 - passes_09 - passes_091 - passes_092 - passes_010 - passes_011 - passes_012 - passes_013 - passes_10 - passes_11)) 0.4=$passes_new 0.5=$passes_05 0.6=$passes_06 0.7=$passes_07 0.8=$passes_08 0.9=$passes_09 0.9.1=$passes_091 0.9.2=$passes_092 0.10=$passes_010 0.11=$passes_011 0.12=$passes_012 0.13=$passes_013 1.0=$passes_10 1.1=$passes_11"
echo "$COUNTS"
# 0.12 stage 2b: a part run on request reports its own checks.
if [ "$passes_stress" -gt 0 ]; then echo "STRESS_COUNTS passes=$passes_stress failures=$failures"; fi
if [ "$SHARD" != all ]; then
    echo
    if [ "$failures" -eq 0 ]; then echo "SHARD $SHARD PASSED ($passes checks)"; exit 0; fi
    echo "SHARD $SHARD: $failures TEST(S) FAILED"
    exit 1
fi

echo
if [ "$failures" -eq 0 ]; then
    echo "ALL TESTS PASSED ($passes checks: $((passes - passes_new - passes_05 - passes_06 - passes_07 - passes_08 - passes_09 - passes_091 - passes_092 - passes_010 - passes_011 - passes_012 - passes_013 - passes_10 - passes_11)) 0.3 regression + $passes_new from 0.4 + $passes_05 from 0.5 + $passes_06 from 0.6 + $passes_07 from 0.7 + $passes_08 from 0.8 + $passes_09 from 0.9 + $passes_091 from 0.9.1 + $passes_092 from 0.9.2 + $passes_010 from 0.10 + $passes_011 from 0.11 + $passes_012 from 0.12 + $passes_013 from 0.13 + $passes_10 from 1.0 + $passes_11 new in 1.1)"
else
    echo "$failures TEST(S) FAILED"
    exit 1
fi
# 0.12: the counts must equal the agreed ones (tests/expected-checks.txt).
echo "$COUNTS" | bash "$(dirname "$0")/check-counts.sh" || exit 1
