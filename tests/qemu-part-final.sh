# Madire OS QEMU tests, part final (0.12, stage 0c): the check over every
# boot's log, after all other parts (a sharded run: in the combining job,
# over every job's logs). Sourced by tests/qemu-tests.sh (a full run, or
# --shard final). Apart from this header, every line was moved unchanged
# from the single script. 1.0 stage 1 adds the [1.0] filesystem check;
# 1.3 stage 8 the [1.3] check that no boot off the framebuffer logs a
# Desktop: line.

section "[0.10] The user-mode boot self-test (every boot)"
LOG=$OUT/serial-boot.log
grep -E "^(User-mode self-test: |\[pid [1-3]\] )" "$LOG" | sed 's/^/      /'
expect "$LOG" "^User-mode self-test: hello \(one line through write\): process 1 exited, status 0 \(expected 0\): OK$" \
                                                      "[0.10] boot self-test: hello ran as process 1 and exited with status 0"
expect "$LOG" "^\[pid 1\] Hello from user mode$"      "[0.10] boot self-test: hello's line in the log"
expect "$LOG" "^User-mode self-test: exitcode \(exit\(42\)\): process 2 exited, status 42 \(expected 42\): OK$" \
                                                      "[0.10] boot self-test: exitcode ran as process 2, status 42 collected"
expect "$LOG" "^User-mode self-test: nullptr \(read of address 0\): process 3 killed by a CPU exception: #PF, address 0x0000000000000000, instruction 48 8b 04 25: OK$" \
                                                      "[0.10] boot self-test: nullptr ran as process 3 and ended by #PF at address 0, at its instruction"
TD=$(grep -m1 -E '^User-mode self-test: [0-9]+ address spaces torn down' "$LOG")
F0=$(sed -nE 's/.*free pages ([0-9]+) -> ([0-9]+),.*/\1/p' <<< "$TD")
F1=$(sed -nE 's/.*free pages ([0-9]+) -> ([0-9]+),.*/\2/p' <<< "$TD")
if grep -qE '^User-mode self-test: 3 address spaces torn down, 0 wrong; .*processes left 0: OK$' <<< "$TD" &&
   [ -n "$F0" ] && [ "$F0" = "$F1" ]; then
    pass "[0.10] boot self-test: free pages ($F0 -> $F1), page tables, stack slots, threads and process table exactly restored"
else
    fail "[0.10] boot self-test: teardown or free pages wrong (${TD:-no line})"
fi
expect "$LOG" "^User-mode self-test: PASS$"           "[0.10] boot self-test: PASS on the normal boot"
expect "$OUT/serial-smap.log" "^User-mode self-test: PASS$" \
                                                      "[0.10] boot self-test: PASS with SMEP and SMAP on"
nb=0; nm=0
for f in "$OUT"/serial-*.log; do
    grep -q "System ready\." "$f" || continue
    nb=$((nb + 1))
    grep -q "^User-mode self-test: PASS$" "$f" || { nm=$((nm + 1)); echo "      no self-test PASS in $(basename "$f")"; }
done
if [ "$nb" -gt 0 ] && [ "$nm" -eq 0 ]; then pass "[0.10] boot self-test: PASS in every one of the $nb boots that reached System ready"
else fail "[0.10] boot self-test: missing in $nm of $nb boots that reached System ready"; fi


section "[0.12] Bounded diagnostic waits (every boot)"
# 0.12 stage 3: every boot that ran the scheduler self-test calibrated the
# waits' stall bound first (logged), and no wait stalled or missed its limit
# in any boot except test=waitstall, which stops the timer on purpose.
nb=0; nm=0; cals=""
for f in "$OUT"/serial-*.log; do
    grep -q "^Scheduler self-test: " "$f" || continue
    nb=$((nb + 1))
    c=$(sed -nE 's/^Diagnostic waits: calibrated: at most ([1-9][0-9]*) polls per tick \(5 whole ticks measured, [0-9]+ ticks spent\); stall bound at least [0-9]+ polls$/\1/p' "$f" | head -n 1)
    if [ -n "$c" ]; then cals+="$c "; else nm=$((nm + 1)); echo "      no calibration in $(basename "$f")"; fi
done
CS=$(tr ' ' '\n' <<< "$cals" | grep . | sort -n)
echo "      calibrated polls per tick: lowest $(head -n 1 <<< "$CS"), highest $(tail -n 1 <<< "$CS") ($nb boots)"
grep -h -E "^Diagnostic waits: calibrated: " "$OUT/serial-boot.log" | sed 's/^/      normal boot: /'
if [ "$nb" -gt 0 ] && [ "$nm" -eq 0 ]; then pass "[0.12] diagnostic waits calibrated in every boot that ran the scheduler self-test"
else fail "[0.12] diagnostic waits: no calibration in $nm of $nb boots that ran the scheduler self-test"; fi
nl=0; nw=0
for f in "$OUT"/serial-*.log; do
    [ "$(basename "$f")" = serial-waitstall.log ] && continue
    nl=$((nl + 1))
    if grep -qE "^Diagnostic wait: |^Diagnostic waits: not calibrated" "$f"; then
        nw=$((nw + 1)); grep -E "^Diagnostic wait: |^Diagnostic waits: not calibrated" "$f" | sed "s|^|      $(basename "$f"): |"
    fi
done
echo "      $nl boot logs checked (all but serial-waitstall.log)"
if [ "$nw" -eq 0 ]; then pass "[0.12] no diagnostic wait stalled or missed its limit in any other boot"
else fail "[0.12] a diagnostic wait stalled or missed its limit (or calibration failed) in $nw of $nl boot logs"; fi


section "[1.0] The filesystem boot self-test (every boot)"
# 1.0 stage 1: every boot that reached System ready ran the filesystem's
# self-test and passed it. The padded test kernel's boots (part 4) keep
# their logs outside serial-*.log, so this check and the ones above count
# the same boots as before.
nb=0; nm=0
for f in "$OUT"/serial-*.log; do
    grep -q "System ready\." "$f" || continue
    nb=$((nb + 1))
    grep -q "^Filesystem self-test: PASS$" "$f" || { nm=$((nm + 1)); echo "      no filesystem self-test PASS in $(basename "$f")"; }
done
if [ "$nb" -gt 0 ] && [ "$nm" -eq 0 ]; then pass "[1.0] filesystem self-test: PASS in every one of the $nb boots that reached System ready"
else fail "[1.0] filesystem self-test: missing in $nm of $nb boots that reached System ready"; fi


section "[1.3] The desktop only on the framebuffer (every boot)"
# 1.3 stage 8: the default path's desktop needs the splash, so a boot whose
# console is not on the framebuffer (every -kernel boot, every textmode
# entry, a framebuffer refused) logs no "Desktop:" line at all: the
# refusals are logged on test=desktop boots only (the stage 8 decision D2).
nb=0; nm=0
for f in "$OUT"/serial-*.log; do
    grep -q "^Display: console on the framebuffer" "$f" && continue
    nb=$((nb + 1))
    if grep -q "^Desktop" "$f"; then nm=$((nm + 1)); echo "      a Desktop: line in $(basename "$f")"; fi
done
if [ "$nb" -gt 0 ] && [ "$nm" -eq 0 ]; then pass "[1.3] no Desktop: line in any of the $nb boots without the framebuffer console"
else fail "[1.3] a Desktop: line in $nm of $nb boots without the framebuffer console"; fi
