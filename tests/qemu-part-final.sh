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


section "[1.5] The boot-phase lines (every boot)"
# 1.5 stage 3 (docs/plans/1.5.md, C10 and H1): every boot that reached
# System ready logged the six lines "boot-phase: <name> tsc=<n> ticks=<n>"
# before it, each once, in the fixed order; the TSC count rises from phase
# to phase and the ticks never fall (0 before the timer runs, at least 1
# once it does). A log holding several boots (a restart) is split at each
# "boot32: start" line (boot/boot.asm), and each boot that reached System
# ready is judged up to its first System ready line (dmesg's copies of the
# lines come after it). The lines' numbers are report only.
PH_ORDER="kernel entry,memory ready,interrupts on,filesystem ready,storage done,boot done"
nb=0; no=0; nf=0
for f in "$OUT"/serial-*.log; do
    while IFS='|' read -r ph_order ph_fig; do
        nb=$((nb + 1))
        [ "$ph_order" = ok ] || { no=$((no + 1)); echo "      $(basename "$f"): $ph_order"; }
        [ "$ph_fig" = ok ] || { nf=$((nf + 1)); echo "      $(basename "$f"): figures: $ph_fig"; }
    done < <(ORDER="$PH_ORDER" awk '
        BEGIN { split(ENVIRON["ORDER"], want, ",") }
        /^boot32: start$/ { n = 0; ready = 0; bad = ""; next }
        ready { next }
        /^boot-phase: / {
            n++
            if (n > 6) { bad = bad " line " n " is one too many"; next }
            if ($0 !~ /^boot-phase: [a-z ]+ tsc=[0-9]+ ticks=[0-9]+$/) { bad = bad " line " n " malformed"; next }
            name = $0; sub(/^boot-phase: /, "", name); sub(/ tsc=.*$/, "", name)
            if (name != want[n]) bad = bad " line " n " is " name
            t = $0; sub(/^.* tsc=/, "", t); sub(/ ticks=.*$/, "", t); tsc[n] = t + 0
            k = $0; sub(/^.* ticks=/, "", k); tk[n] = k + 0
            next
        }
        /^System ready\. / {
            ready = 1
            if (n != 6 || bad != "") { printf "%d lines before System ready:%s|-\n", n, bad; next }
            fig = "ok"
            if (tk[1] != 0 || tk[2] != 0) fig = "ticks not 0 before the timer runs"
            else if (tk[3] < 1) fig = "ticks 0 at interrupts on"
            for (i = 2; i <= 6; i++) {
                if (tsc[i] <= tsc[i - 1]) fig = "tsc not rising at " want[i]
                if (tk[i] < tk[i - 1]) fig = "ticks falling at " want[i]
            }
            print "ok|" fig
        }' "$f")
done
grep -E "^boot-phase: " "$OUT/serial-boot.log" | head -n 6 | sed 's/^/      normal boot (report only): /'
if [ "$nb" -gt 0 ] && [ "$no" -eq 0 ]; then
    pass "[1.5] boot phases: in every one of the $nb boots that reached System ready, the six lines 'boot-phase: <name> tsc=<n> ticks=<n>' before System ready, each once, in the order kernel entry, memory ready, interrupts on, filesystem ready, storage done, boot done"
else
    fail "[1.5] boot phases: missing, repeated, malformed or out of order in $no of $nb boots that reached System ready"
fi
if [ "$nb" -gt 0 ] && [ "$no" -eq 0 ] && [ "$nf" -eq 0 ]; then
    pass "[1.5] boot phases: in every one of the $nb boots the TSC count rises from phase to phase and the ticks never fall: 0 at kernel entry and memory ready (before the timer runs), at least 1 from interrupts on"
else
    fail "[1.5] boot phases: the figures are wrong in $nf of $nb boots (or the lines are, in $no)"
fi
expect_in_order "$OUT/serial-progmod.log" "[1.5] boot phases: the six lines are in dmesg, in order (dmesg 9999 in part 3's program-module boot)" \
    "^madire> dmesg 9999$" "^boot-phase: kernel entry tsc=[0-9]+ ticks=[0-9]+$" "^boot-phase: memory ready tsc=" \
    "^boot-phase: interrupts on tsc=" "^boot-phase: filesystem ready tsc=" "^boot-phase: storage done tsc=" \
    "^boot-phase: boot done tsc=[0-9]+ ticks=[0-9]+$" "^madire> echo end-of-progmod-test$"
