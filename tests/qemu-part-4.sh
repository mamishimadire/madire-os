# Madire OS QEMU tests, part 4 (0.12, stage 0c): [0.10] GRUB Diagnostics
# (27 boots through GRUB). Sourced by tests/qemu-tests.sh (a full run, or
# --shard 4). Apart from this header, every line was moved unchanged from
# the single script. 0.12 stage 2b adds the [0.12] self-check boots at the
# end (5 boots).

echo "== [0.10] GRUB Diagnostics: the user-mode entries (appended; existing entries unchanged)"
CFG="$OUT/grub.cfg"
# Every Diagnostics entry, in menu order: "title|test option".
DIAGS=$(awk '/^submenu "Diagnostics"/ { in_sub = 1; next }
             in_sub && /^ *menuentry "/ { t = $0; sub(/^ *menuentry "/, "", t); sub(/" \{$/, "", t) }
             in_sub && /^ *multiboot / { o = $0; sub(/.*test=/, "", o); print t "|" o }' "$CFG")
OLD16="Crash test: divide by zero|divzero
Crash test: invalid opcode|ud
Crash test: general protection fault|gpf
Page-fault test: read from NULL|pfnull
Page-fault test: read from unmapped address|pfunmapped
Page-fault test: write to read-only kernel code|pfreadonly
Page-fault test: execute from no-execute data (NX)|pfnx
Page-fault test: read the kernel's old low address|pflow
Crash test: kernel stack overflow|stackoverflow
Memory test: allocate every page|memexhaust
Memory test: double free|doublefree
Memory test: free a kernel page|freereserved
Timer test: measure against RTC|timer
Keyboard test: show key codes|keyboard
Thread test: preemption|preempt
Crash test: kernel thread stack overflow|threadoverflow"
if [ "$(head -n 16 <<< "$DIAGS")" = "$OLD16" ]; then
    pass "[0.10] GRUB Diagnostics: the 16 existing entries unchanged, in their positions 0-15"
else
    fail "[0.10] GRUB Diagnostics: an existing entry moved or changed"
fi
UENTRIES="userspin|User-mode test: PASS|30
userkill|User kill test: PASS|60
userfault|User fault test: PASS|60
userpriv|User privilege test: PASS|60
userkmem|User kernel-memory test: PASS|60
usersys|User syscall test: PASS|60
userio|User I/O test: PASS|60
userpreempt|User preemption test: PASS|40
userchurn|User churn test: PASS|180"
pos=16
while IFS='|' read -r opt passline secs; do
    entry=$(sed -n "$((pos + 1))p" <<< "$DIAGS")
    if [ "${entry##*|}" = "$opt" ]; then
        pass "[0.10] GRUB Diagnostics position $pos: '${entry%|*}' (test=$opt)"
    else
        fail "[0.10] GRUB Diagnostics position $pos: '${entry:-none}', expected test=$opt"
    fi
    # Boot that entry through GRUB: a test-only ISO, the same grub.cfg with
    # the default set to Diagnostics > position $pos.
    D="$OUT/isodiag-$opt"
    rm -rf "$D" && mkdir -p "$D/boot/grub"
    cp "$KERNEL" "$D/boot/madire.kernel"
    sed -e "s/^set default=0$/set default=\"1>$pos\"/" -e 's/^set timeout=3$/set timeout=0/' "$CFG" > "$D/boot/grub/grub.cfg"
    LOG=$OUT/serial-grub-$opt.log
    if grub-mkrescue -o "$OUT/madire-os-diag-$opt.iso" "$D" >/dev/null 2>&1; then
        boot "$LOG" "$secs" -m 32M -cdrom "$OUT/madire-os-diag-$opt.iso"
        if grep -q "^$passline$" "$LOG" && ! grep -q "PANIC" "$LOG"; then
            pass "[0.10] GRUB Diagnostics position $pos boots through GRUB and reaches '$passline'"
        else
            fail "[0.10] GRUB Diagnostics position $pos (test=$opt): no '$passline' (or a panic) in $LOG"
        fi
    else
        fail "[0.10] GRUB Diagnostics position $pos: grub-mkrescue failed"
    fi
    pos=$((pos + 1))
done <<< "$UENTRIES"
# Stage 7: smep and smap at 25 and 26, each booted through GRUB twice: on
# the default CPU (no SMEP/SMAP: NOT APPLICABLE, then System ready) and on
# a CPU with both (the crash screen with #PF and the Cause line).
SENTRIES="smep|SMEP|0x00000011|SMEP violation \(the kernel executed a user page\)
smap|SMAP|0x00000001|SMAP violation \(the kernel read a user page, AC=0\)"
while IFS='|' read -r opt name err cause; do
    entry=$(sed -n "$((pos + 1))p" <<< "$DIAGS")
    if [ "${entry##*|}" = "$opt" ]; then
        pass "[0.10] GRUB Diagnostics position $pos: '${entry%|*}' (test=$opt)"
    else
        fail "[0.10] GRUB Diagnostics position $pos: '${entry:-none}', expected test=$opt"
    fi
    D="$OUT/isodiag-$opt"
    rm -rf "$D" && mkdir -p "$D/boot/grub"
    cp "$KERNEL" "$D/boot/madire.kernel"
    sed -e "s/^set default=0$/set default=\"1>$pos\"/" -e 's/^set timeout=3$/set timeout=0/' "$CFG" > "$D/boot/grub/grub.cfg"
    if grub-mkrescue -o "$OUT/madire-os-diag-$opt.iso" "$D" >/dev/null 2>&1; then
        LOG=$OUT/serial-grub-$opt.log
        boot "$LOG" 20 -m 32M -cdrom "$OUT/madire-os-diag-$opt.iso"
        if grep -q "^$name test: NOT APPLICABLE (not supported by this CPU)$" "$LOG" &&
           grep -q "System ready\." "$LOG" && ! grep -q "PANIC" "$LOG"; then
            pass "[0.10] GRUB Diagnostics position $pos, default CPU: NOT APPLICABLE, then System ready"
        else
            fail "[0.10] GRUB Diagnostics position $pos (test=$opt), default CPU: no NOT APPLICABLE / System ready, or a panic, in $LOG"
        fi
        LOG=$OUT/serial-grub-$opt-cpu.log
        boot "$LOG" 20 -m 32M -cpu qemu64,+smep,+smap -cdrom "$OUT/madire-os-diag-$opt.iso"
        if grep -q "CPU exception 14: Page Fault (#PF)" "$LOG" && grep -q "Error code: $err" "$LOG" &&
           grep -qE "^ Cause: $cause$" "$LOG" && ! grep -q "System ready" "$LOG"; then
            pass "[0.10] GRUB Diagnostics position $pos, SMEP+SMAP CPU: crash screen with #PF $err and the $name Cause line"
        else
            fail "[0.10] GRUB Diagnostics position $pos (test=$opt), SMEP+SMAP CPU: no #PF $err with the $name Cause line in $LOG"
        fi
    else
        fail "[0.10] GRUB Diagnostics position $pos: grub-mkrescue failed"
    fi
    pos=$((pos + 1))
done <<< "$SENTRIES"
if [ "$(wc -l <<< "$DIAGS")" -eq 27 ]; then pass "[0.10] GRUB Diagnostics: 27 entries (16 earlier, 9 user-mode, smep and smap)"
else fail "[0.10] GRUB Diagnostics: $(wc -l <<< "$DIAGS") entries, expected 27"; fi

echo "== [0.12] Self-check (stage 2b): each forbidden state panics, naming it"
# The always-on self-check: a destroyed address space loaded (thread_set_cr3)
# or switched to (switch_threads), a thread taken off the run queue while not
# ready, and a return to ring 3 by a thread whose process has ended or without
# a live address space (process_return_to_user). Each test= boot creates one
# such state on purpose (kernel/proc/usertest.c); "(WRONG)" means it did not
# fire. The messages are split over lines of at most 79 characters.
selfcheck_panic() {   # selfcheck_panic <test> <description> <line regex>...
    local name=$1 desc=$2
    shift 2
    echo "== [0.12] self-check: $desc (test=$name)"
    LOG=$OUT/serial-$name.log
    # Ends at the crash screen's last line; at the limit (10 s) if it never comes.
    boot_until "$LOG" 10 " System halted. Power off the machine." -m 32M -kernel "$KERNEL" \
        -append "test=$name"
    expect_lines "$LOG" "[0.12] $name: panics naming the state ($desc)" "$@"
    expect "$LOG" "System halted\. Power off the machine\." "[0.12] $name: halted"
    count_lines "$LOG" "^boot32: start$" 1                "[0.12] $name: no reset (booted once)"
    reject "$LOG" "\(WRONG\)|System ready"                "[0.12] $name: the bad state never went on"
    w=$(max_width "$LOG" "KERNEL PANIC" "System halted")
    if [ "$w" -le 79 ]; then pass "[0.12] $name: crash screen lines at most 79 characters (longest $w)"
    else fail "[0.12] $name: crash screen line of $w characters (limit 79)"; fi
}
selfcheck_panic selfcheckcr3 "a destroyed address space loaded" \
    "thread_set_cr3: thread 0 'kernel': CR3 0x[0-9a-f]{16}$" "^ is not a live address space\.$"
selfcheck_panic selfcheckswitch "a switch into a destroyed address space" \
    "switch_threads: thread [0-9]+ 'victim' would run in$" "^ CR3 0x[0-9a-f]{16}, not a live address space\.$"
selfcheck_panic selfcheckpick "a thread run while not ready" \
    "switch_threads: thread [0-9]+ was taken off the run queue$" "^ while blocked \(not ready\)\.$"
selfcheck_panic selfcheckring3 "ring 3 after its process ended" \
    "process_return_to_user: thread [0-9]+ returns to ring 3 at$" \
    "^ RIP 0x00000000004[0-9a-f]{5}, but its process 4242 has ended\.$"
selfcheck_panic selfcheckring3cr3 "ring 3 without a live address space" \
    "process_return_to_user: thread [0-9]+ returns to ring 3 at$" \
    "^ RIP 0x00000000004[0-9a-f]{5} without a live address space$" "^ \(CR3 0x[0-9a-f]{16}\)\.$"

echo "== [0.12] Bounded diagnostic waits: the timer stopped on purpose (test=waitstall)"
# 0.12 stage 3: with interrupts off on thread 0 the tick count cannot move;
# a bounded wait and thread 0's bounded sleep must each end as a stall
# after 100 x the boot calibration polls, logging one line that names the
# wait (file:line), the tick count and the polls, ending in MISMATCH.
LOG=$OUT/serial-waitstall.log
start=$(date +%s)
boot_until "$LOG" 120 "Wait-bound test: PASS" -m 32M -kernel "$KERNEL" -append "test=waitstall"
echo "      boot took $(( $(date +%s) - start )) s"
grep -E "^(Diagnostic wait|Wait-bound test)" "$LOG" | sed 's/^/      /'
expect "$LOG" "^Wait-bound test: PASS \(3 checks, 0 mismatches\)$" \
                                                      "[0.12] waitstall: PASS (calibrated; both waits ended as stalls, one line each)"
SL="^Diagnostic wait: kernel/proc/schedtest\.c:[0-9]+: stalled: the tick count stayed at [0-9]+ for [0-9]+ polls \(bound [0-9]+; [0-9]+ polls in all\): MISMATCH$"
count_lines "$LOG" "$SL" 2                            "[0.12] waitstall: two stall lines naming the wait (file:line), the tick count and the polls"
CAL=$(sed -nE 's/^Diagnostic waits: calibrated: at most ([0-9]+) polls per tick .*/\1/p' "$LOG" | head -n 1)
B=$(grep -E "$SL" "$LOG" | sed -nE 's/.*\(bound ([0-9]+);.*/\1/p' | sort -u)
if [ -n "$CAL" ] && [ "$B" = "$((CAL * 100))" ]; then
    pass "[0.12] waitstall: both stalls at the calibrated bound (100 x $CAL = $B polls)"
else
    fail "[0.12] waitstall: bounds '${B//$'\n'/ }' against the calibration ${CAL:-?}"
fi
reject "$LOG" "KERNEL PANIC|\(WRONG\)"                "[0.12] waitstall: no panic (the test ended normally)"
