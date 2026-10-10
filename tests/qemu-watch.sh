#!/usr/bin/env bash
# Stall watch (0.11, a test-harness diagnostic; it decides no check).
# qemu-tests.sh runs every QEMU boot through this wrapper (as
# "qemu-system-x86_64" first in PATH). It starts the real QEMU with one
# more monitor (HMP on a unix socket) and watches the serial log. If the
# log does not grow for QEMU_WATCH_SILENCE seconds (default 6; an idle
# kernel logs "uptime" every 5 s), or the boot is stopped (its time limit)
# after such a silence, tests/qemu_capture.py records the guest state
# before QEMU is stopped. Deliberate halts (a crash screen's "System
# halted", the 32-bit stub's "Halted.") are not captured, unless a check
# logged MISMATCH before the halt (a failed self-test). Once per boot.
#
# Environment: QEMU_REAL (the real binary), QEMU_WATCH_DIR (captures go
# there; stalls.txt gets one summary line each), QEMU_WATCH_ELF (symbols).
set -u
real=${QEMU_REAL:?}
dir=${QEMU_WATCH_DIR:?}
silence=${QEMU_WATCH_SILENCE:-6}
here=$(dirname "$0")

log=""
prev=""
for a in "$@"; do
    case "$prev:$a" in
    -serial:file:*) log=${a#file:} ;;
    esac
    prev=$a
done

mon="$dir/mon-$$.sock"
rm -f "$mon"
# Keep QEMU outside timeout's process group so stop() can read the monitor
# before it terminates QEMU and records the IRQ 1 counts.
setsid "$real" "$@" -monitor unix:"$mon",server=on,wait=off &
qpid=$!
captured=0

capture() {
    [ "$captured" -eq 0 ] && [ -n "$log" ] && [ -S "$mon" ] || return
    if grep -qE "System halted|Halted\.$" "$log" 2>/dev/null &&
       ! grep -q "MISMATCH" "$log" 2>/dev/null; then
        return                      # a deliberate halt; a failed self-test is captured
    fi
    captured=1
    local name
    name=$(basename "$log" .log)
    python3 "$here/qemu_capture.py" "$mon" "$dir/stall-$name-$$.txt" "${QEMU_WATCH_ELF:-}" \
        "$name (pid $$, $1)" >> "$dir/stalls.txt" 2>&1
}

quiet_for() {                       # seconds since the log last grew
    local now m
    now=$(date +%s)
    m=$(stat -c %Y "$log" 2>/dev/null || echo "$now")
    echo $((now - m))
}

stop() {
    if [ -n "$log" ] && [ -s "$log" ] && [ "$(quiet_for)" -ge "$silence" ]; then
        capture "stopped at its time limit after $(quiet_for) s of silence"
    fi
    # 0.11: a boot that typed keys (boot_keys sets QEMU_WATCH_IRQ1): QEMU's
    # IRQ 1 count and the kernel's counts[1], next to the serial log.
    if [ -n "${QEMU_WATCH_IRQ1:-}" ] && [ -n "$log" ] && [ -S "$mon" ]; then
        python3 "$here/qemu_capture.py" --irq1 "$mon" "${log%.log}.irq1.txt" \
            "${QEMU_WATCH_ELF:-}" > /dev/null 2>&1
    fi
    kill -TERM "$qpid" 2>/dev/null
    wait "$qpid"
    rm -f "$mon"
    exit 143
}
trap stop TERM INT

while kill -0 "$qpid" 2>/dev/null; do
    if [ -n "$log" ] && [ -s "$log" ] && [ "$captured" -eq 0 ] && [ "$(quiet_for)" -ge "$silence" ]; then
        capture "no serial output for $(quiet_for) s"
    fi
    sleep 0.5 &
    wait $!
done
wait "$qpid"
rc=$?
rm -f "$mon"
exit "$rc"
