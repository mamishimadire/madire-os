#!/usr/bin/env bash
# Madire OS, 1.3 stage 8: the Kali hand-off's short boot (scripts/remote-
# regress.ps1 -Trial; a local trial, never evidence). One boot of the real
# ISO (build/madire-os.iso, no test= option: the default path) under QEMU at
# -m 128M to System ready and the desktop's first frame; the screen against
# tests/screen_ref.py's desktop with the clock read from the kernel's own
# string (check-desktop). Writes build/trial/ and prints TRIAL_ lines; the
# last is TRIAL_RESULT=PASS or FAIL (exit status 0 or 1).
set -u
cd "$(dirname "$0")/.." || exit 1
OUT=build/trial
rm -rf "$OUT" && mkdir -p "$OUT"
LOG=$OUT/serial-trial-default.log
SOCK=$OUT/qmp.sock
CLOCK_V=$(nm build/madire.elf | awk '$3 == "clock" { print $1 }' | head -n 1)
CLOCK_P=$(printf '0x%x' $(( 0x${CLOCK_V:-0} - 0xffffffff80000000 )))
echo "TRIAL_CLOCK desktop.c's clock at 0x${CLOCK_V:-?} (physical $CLOCK_P)"
t0=$(date +%s)
timeout 150 qemu-system-x86_64 -display none -no-reboot -serial file:"$LOG" \
    -qmp unix:"$SOCK",server=on,wait=off -m 128M -cdrom build/madire-os.iso >/dev/null 2>&1 &
QPID=$!
shown=no
for _ in $(seq 240); do
    if grep -q "^Desktop: shown on " "$LOG" 2>/dev/null; then shown=yes; break; fi
    sleep 0.5
done
sleep 2
SCREEN=$(python3 tests/screen_ref.py check-desktop "$SOCK" "$CLOCK_P" 2>&1)
kill "$QPID" 2>/dev/null
wait "$QPID" 2>/dev/null
echo "TRIAL_SECONDS $(( $(date +%s) - t0 ))"
grep -E "^System ready|^Kernel stack: [0-9]+ bytes at peak|^Desktop" "$LOG" | sed 's/^/TRIAL_LINE /'
echo "TRIAL_SCREEN $SCREEN"
echo "$SCREEN" > "$OUT/screen.txt"
if [ "$shown" = yes ] && grep -qxF "System ready. Press any key to open the console." "$LOG" &&
   grep -qE "^Desktop: shown on 1024x768x32 \(scale 100 %\): .* \(the default path, 1\.3 stage 8\)$" "$LOG" &&
   grep -qE "^DESKTOP size=1024x768 differ=0 " <<< "$SCREEN" && ! grep -qE "PANIC|FAILED" "$LOG"; then
    echo "TRIAL_RESULT=PASS"
    exit 0
fi
echo "TRIAL_RESULT=FAIL"
exit 1
