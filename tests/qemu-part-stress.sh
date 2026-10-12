# Madire OS QEMU tests, part "stress" (0.12, stage 2b): the kill-path stress
# test. Sourced by tests/qemu-tests.sh only with --shard stress (never in a
# full run or a normal sharded run: it is not in PARTS). Its checks are
# tagged [stress] and counted apart from the agreed per-tag counts.
#
# 1. test=killrace, 3 boots: 400 processes each, killed at 8 points of their
#    start (before their thread ran, while loading, after 1-3 ticks, proven
#    in ring 3, asleep in nanosleep); judged by guest time
#    (tests/killrace_watch.py); since stage 3 each boot has its own seed.
# 2. Console sessions, 6 boots of 42 kills each: run -b spin + kill -y (at
#    once and after 0.4 s), run spin + Ctrl+C (at once and after 0.4 s),
#    run -b spin + kill + yes, run -b sleep 2 + kill -y (asleep), run sleep 2
#    + Ctrl+C (asleep), run lines + Ctrl+C (writing), run flood + Ctrl+C
#    (waiting on the full output queue); ps after every 8 must show 0
#    processes, and free pages must match at the start and the end.
# 3. Key pace (0.12 stage 3): the same 861-key session at 30 ms and at
#    50 ms, presses and releases reported; one check (no dropped key).
#    1.0 stage 0d: also at a 50 ms gap with a 20 ms hold (report only).
# 4. The VFS lock (1.0 stage 3, plan C2): test=klock, 30 boots, one check
#    each; KLOCK lines and a KLOCK_SPREAD summary report the figures.
# The always-on self-check (stage 2b) panics on a switch to a thread that is
# not ready, a CR3 that is not a live address space, or a return to ring 3
# without a live process; the stall capture records the tables if a boot
# goes silent.

echo "== [stress] Kill race: 400 processes killed at 8 points of their start (test=killrace, 3 boots)"
# tests/killrace_watch.py judges each boot by guest time (the kernel's tick
# count, read over QMP): progress lines stopping while the ticks advance
# 3000 past the last one is STUCK (a real failure; the stall watch records
# the tables); ticks not advancing, or the wall-clock limit coming first, is
# INCONCLUSIVE (host starvation): reported, counted neither way.
#
# 0.12 stage 3: the seed. KILLRACE_SEED if set, otherwise the workflow's
# run number (GITHUB_RUN_NUMBER, set by GitHub in every job), otherwise 1.
# Boot b gets seed=SEED*3+b-1 (each run its own three seeds); the kernel
# varies the delay before each kill from it. To re-run a failing seed on
# Actions (the workflow has no input for it), commit on a wip branch the
# next line as KILLRACE_SEED=<the STRESS_SEED value> and dispatch the stress
# part on that commit.
KILLRACE_SEED=${KILLRACE_SEED:-}
if [ -n "$KILLRACE_SEED" ]; then SEED=$KILLRACE_SEED SEED_FROM=KILLRACE_SEED
elif [ -n "${GITHUB_RUN_NUMBER:-}" ]; then SEED=$GITHUB_RUN_NUMBER SEED_FROM=GITHUB_RUN_NUMBER
else SEED=1 SEED_FROM=default; fi
if ! [[ "$SEED" =~ ^[0-9]{1,9}$ ]]; then
    echo "FATAL: seed '$SEED' (from $SEED_FROM) is not a number of at most 9 digits"
    exit 1
fi
SEED=$((10#$SEED))
echo "STRESS_SEED killrace=$SEED from=$SEED_FROM boots=$((SEED * 3)),$((SEED * 3 + 1)),$((SEED * 3 + 2))"
inconclusive=0
for b in 1 2 3; do
    LOG=$OUT/serial-killrace-$b.log
    SOCK=$OUT/qmp-killrace.sock
    KSEED=$((SEED * 3 + b - 1))
    rm -f "$LOG" "$SOCK"
    timeout 300 qemu-system-x86_64 -display none -no-reboot -serial file:"$LOG" \
        -qmp unix:"$SOCK",server=on,wait=off -m 64M -kernel "$KERNEL" -append "test=killrace seed=$KSEED" \
        >/dev/null 2>&1 &
    QPID=$!
    V=$(python3 "$(dirname "$0")/killrace_watch.py" "$SOCK" "$LOG" "$OUT/madire.elf" 280)
    kill "$QPID" 2>/dev/null
    wait "$QPID" 2>/dev/null
    [ -f "$LOG" ] || : > "$LOG"
    grep -E "^Kill race test: " "$LOG" | sed 's/^/      /'
    echo "      $V"
    case "$V" in
    *"KILLRACE_WATCH DONE"*) pass "[stress] killrace $b: finished, never stuck in guest time" ;;
    *"KILLRACE_WATCH STUCK"*) fail "[stress] killrace $b: STUCK ($V)"; continue ;;
    *) inconclusive=$((inconclusive + 1))
       echo "INCONCLUSIVE: [stress] killrace $b (not a Madire result): $V"
       continue ;;
    esac
    expect "$LOG" "^Kill race test: PASS$"                  "[stress] killrace $b: PASS"
    # 1.0 stage 0e: the bound is one tick of work steps, calibrated by the
    # kernel at the start of the test (it was a fixed 500000). The same check
    # also fails if the bound is 0, below 0.9 x the measured steps per tick,
    # or not the bound the seed line used.
    D="[stress] killrace $b: seed $KSEED used and logged (its delays and their sum), bound at least 0.9 tick"
    read -r KSTEPS KBOUND <<< "$(sed -nE 's/^Kill race test: calibration: ([0-9]+) work steps per tick \(.*\); bound ([0-9]+) steps$/\1 \2/p' "$LOG" | head -n 1)"
    KUSED=$(sed -nE "s/^Kill race test: seed $KSEED: 0 to ([0-9]+) extra work steps before each kill of modes 1-7\$/\1/p" "$LOG" | head -n 1)
    echo "      KILLRACE_BOUND boot=$b seed=$KSEED steps_per_tick=${KSTEPS:-none} bound=${KBOUND:-none} seed_line=${KUSED:-none}"
    if [ -z "$KSTEPS" ] || [ -z "$KBOUND" ] || [ "$KUSED" != "$KBOUND" ]; then
        fail "$D (calibration or seed line missing, or their bounds differ: '$KBOUND' vs '$KUSED')"
    elif [ "$KBOUND" -eq 0 ] || [ $((KBOUND * 10)) -lt $((KSTEPS * 9)) ]; then
        fail "$D (bound $KBOUND, $KSTEPS steps per tick)"
    else
        expect_in_order "$LOG" "$D" \
            "^Kill race test: calibration: $KSTEPS work steps per tick \(.*\); bound $KBOUND steps$" \
            "^Kill race test: seed $KSEED: 0 to $KBOUND extra work steps before each kill of modes 1-7$" \
            "^Kill race test: seed $KSEED: [0-9]+ extra work steps in all$"
    fi
    count_lines "$LOG" "^Kill race test: progress: [0-9]+ of 400 cycles, tick [0-9]+$" 8 \
                                                            "[stress] killrace $b: 8 progress lines (every 50 cycles)"
    n=$(grep -cE "^Kill race test: mode [0-7]: 50 of 50 killed, [0-9]+ before any return to ring 3$" "$LOG")
    if [ "$n" -eq 8 ]; then pass "[stress] killrace $b: all 8 modes killed 50 of 50"
    else fail "[stress] killrace $b: $n of 8 modes killed 50 of 50"; fi
    expect "$LOG" "^Kill race test: kills before the first return to ring 3: mode 0 all \(50\), mode 6 none \(0\): OK$" \
                                                            "[stress] killrace $b: mode 0 never reached ring 3, mode 6 always did"
    expect "$LOG" "^Kill race test: cleanup: 0 processes left, .*: OK$" \
                                                            "[stress] killrace $b: cleanup exact (processes, frames, stacks, threads, address spaces)"
    reject "$LOG" "KERNEL PANIC|MISMATCH|events dropped"    "[stress] killrace $b: no panic, no mismatch"
done
echo "STRESS_INCONCLUSIVE killrace=$inconclusive"

echo "== [stress] Console kills: 6 boots, 42 kills each, every kill path"
PM="$OUT/user/spin.elf,$OUT/user/sleep.elf,$OUT/user/lines.elf,$OUT/test-user/flood.elf"
for b in 1 2 3 4 5 6; do
    LOG=$OUT/serial-stress-$b.log
    S="x{wait:1}mem{ret}"
    p=4                                 # the boot self-test uses process IDs 1-3
    kills=0 ky=0 kc=0 kyes=0 ps=0
    for c in $(seq 0 41); do
        case $c in
        20|41) S+="run flood{ret}{wait:1}{ctrl+c}{wait:1}"; kc=$((kc + 1)) ;;
        *)
            case $((c % 8)) in
            0) S+="run -b spin{ret}kill -y $p{ret}{wait:0.6}"; ky=$((ky + 1)) ;;
            1) S+="run -b spin{ret}{wait:0.4}kill -y $p{ret}{wait:0.6}"; ky=$((ky + 1)) ;;
            2) S+="run spin{ret}{ctrl+c}{wait:0.6}"; kc=$((kc + 1)) ;;
            3) S+="run spin{ret}{wait:0.4}{ctrl+c}{wait:0.6}"; kc=$((kc + 1)) ;;
            4) S+="run -b spin{ret}kill $p{ret}yes{ret}{wait:0.6}"; kyes=$((kyes + 1)) ;;
            5) S+="run -b sleep 2{ret}{wait:0.3}kill -y $p{ret}{wait:0.6}"; ky=$((ky + 1)) ;;
            6) S+="run sleep 2{ret}{wait:0.3}{ctrl+c}{wait:0.6}"; kc=$((kc + 1)) ;;
            7) S+="run lines 65636{ret}{wait:0.3}{ctrl+c}{wait:0.6}"; kc=$((kc + 1)) ;;
            esac ;;
        esac
        p=$((p + 1))
        kills=$((kills + 1))
        if [ $((c % 8)) -eq 7 ]; then S+="ps{ret}{wait:4}"; ps=$((ps + 1)); fi
    done
    S+="ps{ret}mem{ret}echo end-of-stress-$b{ret}"
    ps=$((ps + 1))
    boot_text "$LOG" 420 "System ready." "$S" 0.03 $'\nend-of-stress-'"$b"$'\n' -m 64M -kernel "$KERNEL" -initrd "$PM"
    echo "      boot $b: $kills kills ($ky kill -y, $kc Ctrl+C, $kyes kill + yes), $ps ps"
    expect "$LOG" "^end-of-stress-$b$"                      "[stress] console $b: the session finished"
    count_lines "$LOG" "^\[process [0-9]+ '(spin|sleep|lines|flood)' killed\]$" "$kills" \
                                                            "[stress] console $b: all $kills processes killed"
    reject "$LOG" "^\[process [0-9]+ '.*' (exited|ended by|could not|load failed)" \
                                                            "[stress] console $b: no process ended any other way"
    count_lines "$LOG" "^console: kill -y for process [0-9]+$" "$ky" "[stress] console $b: $ky kill -y"
    count_lines "$LOG" "^console: Ctrl\+C: kill requested for process [0-9]+$" "$kc" \
                                                            "[stress] console $b: $kc Ctrl+C"
    count_lines "$LOG" "^console: kill confirmed for process [0-9]+$" "$kyes" \
                                                            "[stress] console $b: $kyes kill + yes"
    count_lines "$LOG" "^0 processes$" "$ps"                "[stress] console $b: ps showed 0 processes all $ps times"
    F=$(grep -E "^Free pages \(now\):" "$LOG" | awk '{print $NF}')
    F1=$(sed -n 1p <<< "$F"); F2=$(sed -n 2p <<< "$F")
    if [ -n "$F1" ] && [ "$F1" = "$F2" ]; then pass "[stress] console $b: free pages the same at the start and the end ($F1)"
    else fail "[stress] console $b: free pages ${F1:-?} at the start, ${F2:-?} at the end"; fi
    reject "$LOG" "KERNEL PANIC|^kill: |events dropped|Screen output from thread" \
                                                            "[stress] console $b: no panic, no refused kill, no dropped key"
    if grep -q "serial-stress-$b " "$QEMU_WATCH_DIR/stalls.txt"; then
        fail "[stress] console $b: a stall capture (see $QEMU_WATCH_DIR)"
    else
        pass "[stress] console $b: no stall capture"
    fi
done

echo "== [stress] Key pace: the same long key session at 30 ms, at 50 ms and at 50 ms with a 20 ms hold (report only)"
# 0.12 stage 3: stage 1 found key releases lost from about the 360th key of
# a long session at 30 ms (gap and hold), found in the Codespace. Both paces
# run here, in the same job, so they compare on the same machine: 861 single
# keys without pauses (no Shift), logged with keylog. tests/key_pace.py
# reports the presses, releases and the first press without its release
# (KEY_PACE lines, report only). The one check: the kernel reported no
# dropped key event at either pace.
#
# 1.0 stage 0d: a third pace, a 20 ms hold at a 50 ms gap (the hold shorter
# than the interval), to test whether the loss is a hold-time effect. It is
# report only, like the other two: its KEY_PACE line decides nothing, and the
# one check below still reads the 30 ms and 50 ms logs only.
S="x{wait:1}"
for i in $(seq -w 1 30); do S+="echo kp-$i abcdefghijklmnop{ret}"; done
for pace in 30:30 50:50 50:20; do
    gap=${pace%:*} hold=${pace#*:}
    if [ "$hold" = "$gap" ]; then ms=$gap label="$gap ms"
    else ms=${gap}h$hold label="$gap ms, hold $hold ms"; fi
    LOG=$OUT/serial-keypace-$ms.log
    SOCK=$OUT/qmp.sock
    rm -f "$LOG" "$SOCK"
    timeout 300 qemu-system-x86_64 -display none -no-reboot -serial file:"$LOG" \
        -qmp unix:"$SOCK",server=on,wait=off -m 32M -kernel "$KERNEL" -append "keylog" >/dev/null 2>&1 &
    QPID=$!
    out=$(python3 "$QMPKEYS" "$SOCK" "$LOG" "System ready." --text "${S}echo end-of-pace-$ms{ret}" \
          --gap "0.0$gap" --hold "$hold" --done $'\nend-of-pace-'"$ms"$'\n' --done-timeout 120 2>&1)
    echo "      $label: $out"
    sleep 2
    kill "$QPID" 2>/dev/null
    wait "$QPID" 2>/dev/null
    [ -f "$LOG" ] || : > "$LOG"
    echo "      $(python3 "$(dirname "$0")/key_pace.py" "$LOG" "gap=${gap}ms hold=${hold}ms")"
done
if grep -qE "events dropped" "$OUT/serial-keypace-30.log" "$OUT/serial-keypace-50.log"; then
    fail "[stress] key pace: the kernel reported dropped key events"
else
    pass "[stress] key pace: the kernel reported no dropped key event at 30 ms or 50 ms"
fi

echo "== [stress] The VFS lock: test=klock, 30 boots (1.0 stage 3, plan C2)"
# Each boot is part 2's test=klock boot (tests/qemu-lib.sh klock_boot: -m
# 64M, keys typed during the test at a 50 ms gap and a 20 ms hold). One
# check per boot: all nine result lines OK (the same regexes as part 2's
# checks 1-9, with check 6's ticket bound recomputed), the PASS line and
# the exact echo of the keys (check 10).
# KLOCK lines report each boot's figures; KLOCK_SPREAD the lowest and
# highest of each over the 30 boots (report only: the checks decide).
spread() { sort -n | sed -n '1p;$p' | paste -sd' ' | awk '{ printf "%s-%s", $1, $2 }'; }
KL_ITERS="" KL_HOLD="" KL_TURN="" KL_WAIT0="" KL_SECS="" KL_TMIN="" KL_TMAX=""
for b in $(seq -w 1 30); do
    LOG=$OUT/serial-klockstress-$b.log
    out=$(klock_boot "$LOG")
    echo "$out"
    secs=$(sed -nE 's/^BOOT_SECONDS [^ ]+ ([0-9.]+)$/\1/p' <<< "$out")
    read -r it ho tu w0 tmin tmax <<< "$(klock_figures "$LOG")"
    echo "      KLOCK boot=$b iterations=${it:-none} hold=${ho:-none} turn=${tu:-none} wait0=${w0:-none} takes_per_thread=${tmin:-none}-${tmax:-none} seconds=${secs:-none}"
    KL_ITERS+="${it:-0}"$'\n' KL_HOLD+="${ho:-0}"$'\n' KL_TURN+="${tu:-0}"$'\n'
    KL_WAIT0+="${w0:-0}"$'\n' KL_SECS+="${secs:-0}"$'\n'
    KL_TMIN+="${tmin:-0}"$'\n' KL_TMAX+="${tmax:-0}"$'\n'
    bad=""
    for re in "$KLOCK_RE_CONTENTION" "$KLOCK_RE_COUNTER" "$KLOCK_RE_RECORDS" "$KLOCK_RE_HOLD" \
              "$KLOCK_RE_TURN" "$KLOCK_RE_THREAD0" "$KLOCK_RE_SELFCHECK" "$KLOCK_RE_PREEMPT" \
              "$KLOCK_RE_RETURNED" "^Filesystem lock test: PASS \("; do
        grep -qE -- "$re" "$LOG" || bad+=" /$re/"
    done
    klock_bound_ok "$LOG" || bad+=" (thread 0's wait past the ticket bound recomputed from its line)"
    klock_keys_ok "$LOG" || bad+=" (keys: echo not exact, a dropped key, no done line, no System ready, or a panic)"
    if [ -z "$bad" ]; then
        pass "[stress] klock $b: every result line OK, PASS, the keys' echo exact"
    else
        fail "[stress] klock $b: missing or failed in $LOG:$bad"
        grep -E "^Filesystem lock test: " "$LOG" | sed 's/^/      /'
    fi
done
echo "KLOCK_SPREAD boots=30 iterations=$(printf '%s' "$KL_ITERS" | spread) hold=$(printf '%s' "$KL_HOLD" | spread) turn=$(printf '%s' "$KL_TURN" | spread) turn_max=$(printf '%s' "$KL_TURN" | sort -n | tail -n 1) wait0=$(printf '%s' "$KL_WAIT0" | spread) takes_per_thread_min=$(printf '%s' "$KL_TMIN" | sort -n | head -n 1) takes_per_thread_max=$(printf '%s' "$KL_TMAX" | sort -n | tail -n 1) seconds=$(printf '%s' "$KL_SECS" | spread)"
