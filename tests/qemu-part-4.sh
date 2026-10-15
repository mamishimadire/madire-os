# Madire OS QEMU tests, part 4 (0.12, stage 0c): [0.10] boot menu Diagnostics
# (27 boots through the boot menu; GRUB until 0.13, Limine since). Sourced
# by tests/qemu-tests.sh (a full run, or --shard 4). Apart from this header,
# every line was moved unchanged from the single script. 0.12 stage 2b adds
# the [0.12] self-check boots at the end (5 boots). 0.13: the Diagnostics
# come from limine.conf and every test ISO is made with Limine (stage 3a);
# the checks and their logs are named "boot menu" and serial-bootmenu-*
# (stage 3b), and the [0.13] module_string checks are added at the end.
# 1.0 stage 1 adds the padded test kernel's [1.0] checks at the end.

echo "== [0.10] Boot menu Diagnostics: the user-mode entries (appended; existing entries unchanged)"
# 0.13: the boot menu is limine.conf: the "/Diagnostics" directory and its
# "//<title>" sub-entries, each with "cmdline: test=<option>".
CFG="$OUT/limine.conf"
# Every Diagnostics entry, in menu order: "title|test option".
DIAGS=$(awk '/^\/Diagnostics$/ { in_sub = 1; next }
             /^\/[^\/]/ { in_sub = 0 }
             in_sub && /^\/\/[^\/]/ { t = substr($0, 3) }
             in_sub && /^ *cmdline: test=/ { o = $0; sub(/.*test=/, "", o); print t "|" o }' "$CFG")
# diag_iso <option> <title>: a test-only ISO, the same limine.conf with no
# timeout and the default entry set to Diagnostics > <title> (before 0.13,
# GRUB's 'set default="1>N"'). Fails if the ISO cannot be made.
diag_iso() {
    D="$OUT/isodiag-$1"
    limine_tree "$D"
    T="$2" awk '/^timeout: 3$/ { print "timeout: 0"; print "default_entry: Diagnostics/" ENVIRON["T"]; next } { print }' \
        "$CFG" > "$D/boot/limine/limine.conf"
    limine_iso "$D" "$OUT/madire-os-diag-$1.iso"
}
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
    pass "[0.10] boot menu Diagnostics: the 16 existing entries unchanged, in their positions 0-15"
else
    fail "[0.10] boot menu Diagnostics: an existing entry moved or changed"
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
        pass "[0.10] boot menu Diagnostics position $pos: '${entry%|*}' (test=$opt)"
    else
        fail "[0.10] boot menu Diagnostics position $pos: '${entry:-none}', expected test=$opt"
    fi
    # Boot that entry through the boot menu: a test-only ISO with the
    # default set to Diagnostics > position $pos (0.13: Limine).
    LOG=$OUT/serial-bootmenu-$opt.log
    if diag_iso "$opt" "${entry%|*}"; then
        boot "$LOG" "$secs" -m 32M -cdrom "$OUT/madire-os-diag-$opt.iso"
        if grep -q "^$passline$" "$LOG" && ! grep -q "PANIC" "$LOG"; then
            pass "[0.10] boot menu Diagnostics position $pos boots through the boot menu and reaches '$passline'"
        else
            fail "[0.10] boot menu Diagnostics position $pos (test=$opt): no '$passline' (or a panic) in $LOG"
        fi
    else
        fail "[0.10] boot menu Diagnostics position $pos: the test ISO could not be made"
    fi
    pos=$((pos + 1))
done <<< "$UENTRIES"
# Stage 7: smep and smap at 25 and 26, each booted through the boot menu twice: on
# the default CPU (no SMEP/SMAP: NOT APPLICABLE, then System ready) and on
# a CPU with both (the crash screen with #PF and the Cause line).
SENTRIES="smep|SMEP|0x00000011|SMEP violation \(the kernel executed a user page\)
smap|SMAP|0x00000001|SMAP violation \(the kernel read a user page, AC=0\)"
while IFS='|' read -r opt name err cause; do
    entry=$(sed -n "$((pos + 1))p" <<< "$DIAGS")
    if [ "${entry##*|}" = "$opt" ]; then
        pass "[0.10] boot menu Diagnostics position $pos: '${entry%|*}' (test=$opt)"
    else
        fail "[0.10] boot menu Diagnostics position $pos: '${entry:-none}', expected test=$opt"
    fi
    if diag_iso "$opt" "${entry%|*}"; then
        LOG=$OUT/serial-bootmenu-$opt.log
        boot "$LOG" 20 -m 32M -cdrom "$OUT/madire-os-diag-$opt.iso"
        if grep -q "^$name test: NOT APPLICABLE (not supported by this CPU)$" "$LOG" &&
           grep -q "System ready\." "$LOG" && ! grep -q "PANIC" "$LOG"; then
            pass "[0.10] boot menu Diagnostics position $pos, default CPU: NOT APPLICABLE, then System ready"
        else
            fail "[0.10] boot menu Diagnostics position $pos (test=$opt), default CPU: no NOT APPLICABLE / System ready, or a panic, in $LOG"
        fi
        LOG=$OUT/serial-bootmenu-$opt-cpu.log
        boot "$LOG" 20 -m 32M -cpu qemu64,+smep,+smap -cdrom "$OUT/madire-os-diag-$opt.iso"
        if grep -q "CPU exception 14: Page Fault (#PF)" "$LOG" && grep -q "Error code: $err" "$LOG" &&
           grep -qE "^ Cause: $cause$" "$LOG" && ! grep -q "System ready" "$LOG"; then
            pass "[0.10] boot menu Diagnostics position $pos, SMEP+SMAP CPU: crash screen with #PF $err and the $name Cause line"
        else
            fail "[0.10] boot menu Diagnostics position $pos (test=$opt), SMEP+SMAP CPU: no #PF $err with the $name Cause line in $LOG"
        fi
    else
        fail "[0.10] boot menu Diagnostics position $pos: the test ISO could not be made"
    fi
    pos=$((pos + 1))
done <<< "$SENTRIES"
if [ "$(wc -l <<< "$DIAGS")" -eq 27 ]; then pass "[0.10] boot menu Diagnostics: 27 entries (16 earlier, 9 user-mode, smep and smap)"
else fail "[0.10] boot menu Diagnostics: $(wc -l <<< "$DIAGS") entries, expected 27"; fi

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

echo "== [1.0] self-check (stage 4): a user copy while holding the VFS lock (test=selfcheckcopylock)"
# kernel/memory/usercopy.h: user memory is never touched under the VFS lock.
# Thread 0 takes the lock and calls copy_to_user; "(WRONG)" means it returned.
LOG=$OUT/serial-selfcheckcopylock.log
boot_until "$LOG" 10 " System halted. Power off the machine." -m 32M -kernel "$KERNEL" \
    -append "test=selfcheckcopylock"
expect_lines "$LOG" "[1.0] self-check: a user copy while holding the VFS lock panics, naming the thread" \
    "copy_to_user: thread 0 'kernel' holds the VFS lock:$" "^ user memory is never touched under it\.$"
if grep -q "System halted\. Power off the machine\." "$LOG" && [ "$(grep -c '^boot32: start$' "$LOG")" = 1 ] &&
   ! grep -qE "\(WRONG\)|System ready" "$LOG"; then
    pass "[1.0] self-check: the panic screen shown and the kernel halted"
else
    fail "[1.0] self-check: no halt, a reset, a returned copy or System ready in $LOG"
fi

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

echo "== [0.13] A program module without module_string (Limine passes an empty string)"
# The default entry of limine.conf, with one more module after the 7: hello.elf
# again, without "module_string:". Limine then passes "" as its string; the
# kernel must refuse it by name (progmod_name: an empty string is "bad name";
# the unit case is refused("") in tests/unit/test_progmod.c) and boot on.
D=$OUT/isonostring
limine_tree "$D"
cp -r "$OUT/isodir/boot/prog" "$D/boot/prog"
awk '{ print } /^ *module_string: lines$/ && !done { print "    module_path: boot():/boot/prog/hello.elf"; done = 1 }' \
    "$OUT/limine.conf" > "$D/boot/limine/limine.conf"
LOG=$OUT/serial-nostring.log
if limine_iso "$D" "$OUT/madire-os-nostring.iso"; then
    boot "$LOG" 15 -cdrom "$OUT/madire-os-nostring.iso"
else
    : > "$LOG"
fi
grep -E '^Program module' "$LOG" | sed 's/^/      /'
expect "$LOG" "^Program module 8: refused: bad name \(''\)$" \
                                                      "[0.13] a program module without module_string is refused: 'Program module 8: refused: bad name ('')'"
if grep -q "^Program modules: 7 accepted, 1 refused (at most 8)$" "$LOG" && grep -q "System ready\." "$LOG" &&
   ! grep -q "PANIC" "$LOG"; then
    pass "[0.13] a program module without module_string: the boot goes on (the other 7 accepted, System ready)"
else
    fail "[0.13] a module without module_string: no '7 accepted, 1 refused' or no System ready (or a panic) in $LOG"
fi

# ---- 1.0 stage 1 (D1): a kernel past 2 MiB boots; the ISO holds the real kernel ----

echo "== [1.0] The padded test kernel (make padkernel: KERNEL_PAD=2M in build/pad), under QEMU -kernel"
# The same boot twice (-m 64M -kernel, no module), with the kernel under test
# and with the padded one. tests/pagetables.py recomputes each boot's page
# tables from its log (the kernel's end, the reserve: lines, the window and
# the kernel-stack tables) and compares them with the logged count. Under
# -kernel, QEMU puts the command line right after the kernel. 1.0 stage 5:
# the kernel itself ends past 2 MiB, so the normal boot has 14 tables (12
# below 2 MiB: the kernel slot +1, and the physical view +1 for the command
# line in 2-4 MiB), and the pad is 2 MiB so that the padded kernel crosses
# the next boundary, 4 MiB: the kernel slot +1 again (the view keeps two
# tables: 0-2 and 4-6 MiB), a total of 15. The logs stay outside
# serial-*.log (part final's every-boot checks keep their boots).
PADK=$OUT/pad/madire.kernel
PADLOGS=$OUT/pad-logs
rm -rf "$PADLOGS" && mkdir -p "$PADLOGS"
READY="System ready. Press any key to open the console."
if [ -f "$PADK" ]; then
    boot_until "$PADLOGS/normal.log" 90 "$READY" -m 64M -kernel "$KERNEL"
    boot_until "$PADLOGS/padded.log" 90 "$READY" -m 64M -kernel "$PADK"
else
    echo "      no $PADK (make padkernel failed?)"
    : > "$PADLOGS/normal.log"; : > "$PADLOGS/padded.log"
fi
PTN=$(python3 "$(dirname "$0")/pagetables.py" "$PADLOGS/normal.log" --label normal)
PTP=$(python3 "$(dirname "$0")/pagetables.py" "$PADLOGS/padded.log" --label padded)
sed 's/^/      /' <<< "$PTN"
sed 's/^/      /' <<< "$PTP"
LOG=$PADLOGS/padded.log
PEND=$(sed -nE 's/^Kernel: text .* end 0x([0-9a-f]+)$/\1/p' "$LOG" | head -n 1)
if [ -n "$PEND" ] && [ $((0x$PEND)) -gt $((0x400000)) ] && grep -q "^Paging walk-check: PASS" "$LOG" &&
   grep -q "^Filesystem self-test: PASS$" "$LOG" && grep -qxF "$READY" "$LOG" && ! grep -q "PANIC" "$LOG"; then
    pass "[1.0] padded test kernel: its image ends at 0x$PEND, past 4 MiB; walk-check PASS, filesystem self-test PASS, System ready, no panic"
else
    fail "[1.0] padded test kernel: end 0x${PEND:-?}, or no walk-check PASS, filesystem PASS or System ready, or a panic, in $LOG"
fi
ptr() { sed -nE "s/^PAGETABLES_RESULT label=[a-z]+ computed=([0-9]+) logged=([0-9]+) kernel_slot=([0-9]+) .*/\\$1/p" <<< "$2"; }
NC=$(ptr 1 "$PTN"); NL=$(ptr 2 "$PTN"); NK=$(ptr 3 "$PTN")
PC=$(ptr 1 "$PTP"); PL=$(ptr 2 "$PTP"); PK=$(ptr 3 "$PTP")
# The anchor: 12 page tables in a -kernel boot below 2 MiB (logged in runs
# 38291205719's -kernel boots, end 0x1e0000, and recomputed from them);
# 14 since 1.0 stage 5, when the kernel crossed 2 MiB (see above).
if [ "${NC:-x}" = 14 ] && [ "${NL:-y}" = 14 ] && [ -n "$PC" ] && [ "$PC" = "${PL:-}" ] &&
   [ -n "$NK" ] && [ "${PK:-x}" = "$((NK + 1))" ]; then
    pass "[1.0] page tables recomputed from the log: normal boot $NC = logged $NL (the known 14); padded boot $PC = logged $PL; the kernel slot $NK -> $PK (+1)"
else
    fail "[1.0] page tables from the log: normal computed ${NC:-?} logged ${NL:-?} (14 expected), padded computed ${PC:-?} logged ${PL:-?}, kernel slot ${NK:-?} -> ${PK:-?}"
fi
rm -f "$OUT/iso-kernel.bin"
xorriso -osirrox on -indev "$ISO" -extract /boot/madire.kernel "$OUT/iso-kernel.bin" >/dev/null 2>&1
SI=$(sha256sum < "$OUT/iso-kernel.bin" 2>/dev/null | cut -d' ' -f1)
SK=$(sha256sum < "$KERNEL" | cut -d' ' -f1)
SP=$(sha256sum < "$PADK" 2>/dev/null | cut -d' ' -f1)
echo "      ISO /boot/madire.kernel ${SI:-missing}; $KERNEL $SK; $PADK ${SP:-missing}"
if [ -n "$SI" ] && [ "$SI" = "$SK" ] && [ -n "$SP" ] && [ "$SP" != "$SK" ]; then
    pass "[1.0] the ISO's /boot/madire.kernel is the kernel under test, not the padded test kernel"
else
    fail "[1.0] the ISO's kernel (${SI:-missing}) against the kernel (${SK}) and the padded kernel (${SP:-missing})"
fi
