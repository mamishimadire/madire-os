# Madire OS QEMU tests, part 4 (0.12, stage 0c): [0.10] boot menu Diagnostics
# (27 boots through the boot menu; GRUB until 0.13, Limine since). Sourced
# by tests/qemu-tests.sh (a full run, or --shard 4). Apart from this header,
# every line was moved unchanged from the single script. 0.12 stage 2b adds
# the [0.12] self-check boots at the end (5 boots). 0.13: the Diagnostics
# come from limine.conf and every test ISO is made with Limine (stage 3a);
# the checks and their logs are named "boot menu" and serial-bootmenu-*
# (stage 3b), and the [0.13] module_string checks are added at the end.
# 1.0 stage 1 adds the padded test kernel's [1.0] checks at the end; stage
# 6 the folder calls' [1.0] checks after them (test=dirops, test=fsunlink);
# stage 7 the System V start-up's (test=startup and a console session).

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

echo "== [1.0] self-check (stage 7b): a yield while holding the VFS lock (test=selfcheckyieldlock)"
# kernel/proc/klock.h: a holder runs with preemption off and never yields,
# sleeps, joins or exits. Thread 0 takes the lock and calls thread_yield;
# "(WRONG)" means it returned.
LOG=$OUT/serial-selfcheckyieldlock.log
boot_until "$LOG" 10 " System halted. Power off the machine." -m 32M -kernel "$KERNEL" \
    -append "test=selfcheckyieldlock"
expect_lines "$LOG" "[1.0] self-check: a yield while holding the VFS lock panics, naming the thread" \
    "thread_yield: thread 0 'kernel' holds the VFS lock:$" "^ a holder never yields, sleeps, joins or exits\.$"
if grep -q "System halted\. Power off the machine\." "$LOG" && [ "$(grep -c '^boot32: start$' "$LOG")" = 1 ] &&
   ! grep -qE "\(WRONG\)|System ready" "$LOG"; then
    pass "[1.0] self-check: the yield's panic screen shown and the kernel halted"
else
    fail "[1.0] self-check: no halt, a reset, a returned yield or System ready in $LOG"
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

# ---- 1.0 stage 6: the folder calls (B1, B3, B4, B5, B7) ----
# Deviations (approved): getcwd answers ERANGE for size 0, the system
# call's answer (B1 said EINVAL, the C library's; S6-1); ENAMETOOLONG for
# a current folder whose path is over 4096 bytes (S6-2); getdents64 ENOENT
# on a removed folder (S6-3); '..' of a removed folder ENOENT (S6-4: Linux
# follows the old parent); programs start at / until stage 8 (S6-5); a
# file's pages given back in steps of at most 64 per hold on unlink,
# rename-over, a last close and a teardown (S6-6); test=dirops added to
# B7 (S6-8); getdents64's count read as 32 bits (recorded with F17).

echo "== [1.0] test=dirops: getcwd, chdir, mkdir, mkdirat, rmdir, unlink, unlinkat, rename, getdents64 (-m 32M)"
LOG=$OUT/serial-dirops.log
DREADY="System ready. Press any key to open the console."
boot_until "$LOG" 300 "$DREADY" -m 32M -kernel "$KERNEL" -append "test=dirops"
grep -E "^Dir test: " "$LOG" | sed 's/^/      /'
# do_case <arg> <description>: dirops <arg> exited with status 0 (every
# result it checked was right).
do_case() {
    expect "$LOG" "^Dir test: dirops $1 \(.*\): process [0-9]+ exited, status 0 \(expected 0\): OK$" "$2"
}
do_case 1 \
    "[1.0] dirops: getcwd at start is / (returns 2, the length with its NUL); chdir to /tmp, .., tmp, ./tmp/ and / each moves the current folder and getcwd follows (/tmp returns 5)"
do_case 2 \
    "[1.0] dirops: getcwd: ERANGE for a buffer one byte short (the exact size works) and for size 0; EFAULT (NULL, unmapped, read-only and kernel buffers); no byte written past the NUL"
do_case 3 \
    "[1.0] dirops: chdir: ENOENT (a missing folder, the empty path), ENOTDIR (a file, a file on the way, a file with a trailing '/'), ENAMETOOLONG (a 256-byte name, no NUL within 4096 bytes), EFAULT (NULL, unmapped, kernel paths); the current folder unchanged after each"
do_case 4 \
    "[1.0] dirops: relative paths start at the current folder: after chdir /tmp, open, stat, mkdir, unlink and rename with relative names act in /tmp; AT_FDCWD is the current folder for openat, newfstatat (also \"\" with AT_EMPTY_PATH), mkdirat and unlinkat; a folder dirfd and an absolute path ignore it"
do_case 5 \
    "[1.0] dirops: mkdir makes a folder (S_IFDIR, mode & 01777 stored, the parent's links + 1), also with a trailing '/'; EEXIST (a folder, a file, '/', '.', '..', /tmp/.), ENOENT (a missing parent, the empty path, inside a removed folder), ENOTDIR (a file on the way), ENAMETOOLONG, EFAULT"
do_case 6 \
    "[1.0] dirops: mkdirat: a folder dirfd, AT_FDCWD and an absolute path with a bad dirfd; EBADF (not open, relative path), ENOTDIR (a file or the console as dirfd)"
do_case 7 \
    "[1.0] dirops: rmdir removes an empty folder (the parent's links - 1, the name then ENOENT), also with a trailing '/'; EBUSY ('/'), EINVAL ('.', d/.), ENOTEMPTY ('..', d/.., a folder with entries), ENOTDIR (a file, file/), ENOENT (missing, the empty path), ENAMETOOLONG, EFAULT"
do_case 8 \
    "[1.0] dirops: unlink removes a file (then ENOENT); EISDIR (a folder, '/', '.', '..', d/); ENOTDIR (file/, a file on the way); ENOENT (missing, missing/, the empty path); ENAMETOOLONG, EFAULT"
do_case 9 \
    "[1.0] dirops: unlinkat: flags 0 as unlink, AT_REMOVEDIR as rmdir (ENOTDIR on a file, EISDIR without it on a folder); EINVAL for any other flag bit (0x100, 0x400, 0x1000, combined); EBADF and ENOTDIR for dirfd; AT_FDCWD the current folder"
do_case 10 \
    "[1.0] dirops: rename: a file across folders (same node number, the old name ENOENT), a file over a file (the new name has the moved node), a folder over an empty folder, a folder across folders with the current folder inside it (getcwd shows the new path), the same name and the same node by two paths 0 with nothing changed"
do_case 11 \
    "[1.0] dirops: rename errors: ENOENT (missing source, missing target folder, the empty path), ENOTDIR (a folder onto a file, a file on the way, a file with a trailing '/' on either side), EISDIR (a file onto a folder), ENOTEMPTY (onto a folder with entries, a/b onto a), EINVAL (a folder into its own subfolder), EBUSY ('/', '.' or '..' on either side), ENAMETOOLONG, EFAULT (either path); nothing changed after each"
do_case 12 \
    "[1.0] dirops: getdents64 lists '.' and '..' first (the folder's and the parent's node numbers; both 1 on /), then the entries in creation order with d_type DT_DIR or DT_REG, d_reclen 8-aligned (19 + name + NUL), names NUL-terminated; 0 at the end"
do_case 13 \
    "[1.0] dirops: getdents64 with small buffers: one record per call when the buffer holds exactly one; a buffer one byte short of the next record EINVAL with the offset unchanged; only whole records returned"
do_case 14 \
    "[1.0] dirops: getdents64 between calls: d_off strictly increasing and lseek to a record's d_off resumes after it; an entry removed before it was read is not returned, none is returned twice across these removals and creations (not across renames: POSIX leaves a rename during a listing unspecified), an entry created during the listing is returned once at the end; lseek 0 starts again"
do_case 15 \
    "[1.0] dirops: getdents64 errors: EBADF (closed, 16, -1), ENOTDIR (a file fd, the console fds), EFAULT (NULL, unmapped, read-only, kernel; offset unchanged); a buffer that turns bad after the first record returns that record"
do_case 16 \
    "[1.0] dirops: name and path limits: a 255-byte name made, renamed and listed (d_reclen 280), 256 bytes ENAMETOOLONG; getcwd of a current folder whose path is over 4096 bytes ENAMETOOLONG"
expect "$LOG" "^Dir test: errno matrix: nothing changed: nodes ([0-9]+) -> \1, data pages ([0-9]+) -> \2, open files ([0-9]+) -> \3, heap in use ([0-9]+) -> \4 bytes: OK$" \
    "[1.0] dirops: the errno matrix changed nothing: nodes, data pages, open files and heap bytes back to the start"
expect "$LOG" "^Dir test: ENOSPC: 1024 nodes in use \(the limit 1024; [0-9]+ made by the kernel\); mkdir ENOSPC, /tmp/do-x not created, nodes still 1024; a rename \(and back\) still worked \(dirops 17: process [0-9]+ exited, status 0, expected 0\): OK$" \
    "[1.0] dirops: ENOSPC: with 1024 nodes in use mkdir returns ENOSPC, nothing created; rename still works"
expect "$LOG" "^Dir test: current folders: dirops 18 in /tmp/cw1 exited \(status 0\), dirops 19 in /tmp/cw2 killed by a CPU exception \(vector 14\), dirops 20 in /tmp/cw3 killed; their holds given back \(opens 0 0 0\), 3 current folders dropped by their teardowns; the folders removed; nodes ([0-9]+) -> \1: OK$" \
    "[1.0] dirops: current folders released: processes ended by exit, by a fault and by a kill while in /tmp/cw1..3: every hold given back (each folder's opens 0), each folder then removable, nodes back to the start"
expect "$LOG" "^Dir test: VFS lock holds: the folder calls and dirops 21's listing of a folder of 1000 entries made by the kernel \(process [0-9]+ exited, status 0, expected 0; 1002 records, at most 4096 bytes of records per hold\) held the VFS lock [0-9]+ times, longest hold [0-2] ticks; getdents64 and lseek holds [1-9][0-9]*, longest [0-2] ticks; each at most 2: OK$" \
    "[1.0] dirops: the folder calls held the VFS lock only in bounded pieces (getdents64 on a folder of 1000 entries among them, at most 4096 bytes of records per hold): longest hold at most 2 ticks"
expect "$LOG" "^Dir test: cleanup: open files ([0-9]+) -> \1, nodes ([0-9]+) -> \2, data pages ([0-9]+) -> \3, heap in use ([0-9]+) -> \4 bytes, processes left 0, the VFS lock free with no ticket out: OK$" \
    "[1.0] dirops: cleanup: open files, nodes, data pages, heap bytes and the VFS lock back to the start"
expect "$LOG" "^Dir test: PASS$"                     "[1.0] dirops: PASS"
reject "$LOG" "PANIC"                                 "[1.0] dirops: no crash screen, no panic"

echo "== [1.0] test=fsunlink: open files and folders unlinked, renamed over, removed (-m 32M)"
LOG=$OUT/serial-fsunlink.log
boot_until "$LOG" 300 "$DREADY" -m 32M -kernel "$KERNEL" -append "test=fsunlink"
grep -E "^Unlink test: |^\[pid [0-9]+\] dirops: polls" "$LOG" | sed 's/^/      /'
expect "$LOG" "^Unlink test: unlinked open file: dirops 30 unlinked /tmp/un-a \(10000 bytes\) open as its fd 3, read and wrote it \(process [0-9]+ exited, status 0, expected 0\); after it the node lived on the kernel's hold: links 0, size 10100, 4 pages, the 100 bytes it wrote read back, nodes ([0-9]+) -> \1, data pages ([0-9]+) -> \2; freed at the last close: nodes [0-9]+, data pages [0-9]+: OK$" \
    "[1.0] fsunlink: an unlinked open file stays readable and writable through its fd (fstat links 0, size kept); its pages counted until the last close, then freed exactly"
expect "$LOG" "^Unlink test: unlink of a file not open: dirops 31 unlinked /tmp/un-big \(2 MiB, 513 pages; process [0-9]+ exited, status 0, expected 0\): its pages given back in 9 holds of at most 64; nodes [0-9]+ -> [0-9]+, data pages [0-9]+ -> [0-9]+: OK$" \
    "[1.0] fsunlink: unlink of a 2 MiB file not open frees its 513 pages in 9 holds of at most 64 pages"
expect "$LOG" "^Unlink test: last close by a program: /tmp/un-big2 \(2 MiB\) unlinked and the kernel's hold closed while dirops 32 held it as fd 3 \(data pages still ([0-9]+), \1 before\); its close\(3\) freed it \(process [0-9]+ exited, status 0, expected 0\): pages given back in 9 holds of at most 64; nodes [0-9]+ -> [0-9]+, data pages [0-9]+ -> [0-9]+: OK$" \
    "[1.0] fsunlink: the last close (by a program) of an unlinked 2 MiB file frees it in 9 holds of at most 64 pages"
expect "$LOG" "^Unlink test: teardowns: dirops 33 \(killed\) and dirops 34 \(killed by a CPU exception, vector 14\) held the last fd of an unlinked 2 MiB file each \(data pages still ([0-9]+), \1 before\); their teardowns gave the pages back in 18 holds of at most 64; nodes [0-9]+ -> [0-9]+, data pages [0-9]+ -> [0-9]+: OK$" \
    "[1.0] fsunlink: processes killed and faulted holding the last fd of an unlinked file: their teardowns free it in steps, nothing leaked"
expect "$LOG" "^Unlink test: writers: dirops 35 and 36 wrote 32 x 32 KiB each to /tmp/un-w and /tmp/un-v, every write the full count; after 8 writes un-w was unlinked \(dirops 37: exited, status 0\) and un-v renamed over \(dirops 38: exited, status 0\); both read their 1 MiB back through the fd \(statuses 0, 0\); freed at their close: nodes ([0-9]+) -> [0-9]+, data pages ([0-9]+) -> [0-9]+ \(\+2: dirops 38's file\), then \1, \2: OK$" \
    "[1.0] fsunlink: unlink and rename-over while another process writes 32 x 32 KiB: every write returns the full count, the data stays readable through the fd until close, then freed"
expect "$LOG" "^Unlink test: rename onto an open file: dirops 39 \(process [0-9]+ exited, status 0, expected 0\) renamed /tmp/un-new over /tmp/un-old open as its fd 3, read the old 100 bytes through it and the new 200 through the name; the replaced node freed at its last close: nodes ([0-9]+) -> [0-9]+, data pages ([0-9]+) -> [0-9]+ \(the remaining file\), then \1, \2: OK$" \
    "[1.0] fsunlink: rename onto an open file: the open fd reads the old data, the new name the new; the replaced node freed at its last close"
expect "$LOG" "^Unlink test: rmdir of the current folder: getcwd, creating, stat '.' and '..' and chdir '..' answered, chdir / freed it \(dirops 40: process [0-9]+ exited, status 0, expected 0\): nodes ([0-9]+) -> \1: OK$" \
    "[1.0] fsunlink: rmdir of the current folder returns 0; then getcwd ENOENT, creating a relative name ENOENT, stat \".\" links 0, \"..\" ENOENT; chdir / works and frees the removed folder's node"
expect "$LOG" "^Unlink test: rmdir of a folder open as an fd: getdents64 ENOENT, fstat links 0, freed at its close \(dirops 41: process [0-9]+ exited, status 0, expected 0\): nodes ([0-9]+) -> \1: OK$" \
    "[1.0] fsunlink: rmdir of a folder open as an fd: getdents64 ENOENT, fstat links 0; the node freed at its close"
expect "$LOG" "^Unlink test: cleanup: open files ([0-9]+) -> \1, nodes ([0-9]+) -> \2, data pages ([0-9]+) -> \3, heap in use ([0-9]+) -> \4 bytes, processes left 0, the VFS lock free with no ticket out: OK$" \
    "[1.0] fsunlink: cleanup: open files, nodes, data pages, heap bytes and the VFS lock back to the start"
expect "$LOG" "^Unlink test: PASS$"                  "[1.0] fsunlink: PASS"
reject "$LOG" "PANIC"                                 "[1.0] fsunlink: no crash screen, no panic"

# ---- 1.0 stage 7: System V start-up (D3) ----
# Decisions (approved): no AT_RANDOM (S7-2, recorded as a deviation); the
# environment is the shell's exported variables (S7-3); a program refuses
# a bad number itself (u_arg, status 2; S7-4); args enters at its own stub
# (args_entry, linked with -e) which records the registers and jumps to
# crt0's _start (S7-5); the start block is at most 4096 bytes, E2BIG beyond
# (S7-6, recorded with F17). args is a built-in image and, here, a module
# (QEMU -initrd); it is not in limine.conf.

echo "== [1.0] Built-in images: entry points from the ELF header (report only, no check)"
# scripts/user-images.sh takes each image's entry from e_entry since stage
# 7 (it took nm's _start before): the two agree for every program but args.
same=0 total=0
for e in "$OUT"/user/*.elf; do
    total=$((total + 1))
    ee=$(readelf -h "$e" | awk '$1 == "Entry" && $2 == "point" { print $4 }')
    ss=$(nm "$e" | awk '$3 == "_start" { print $1 }')
    if [ -n "$ee" ] && [ -n "$ss" ] && [ "$(( ee ))" -eq "$(( 0x$ss ))" ]; then
        same=$((same + 1))
    else
        echo "      $(basename "$e"): e_entry $ee, _start 0x$ss (differ)"
    fi
done
echo "      e_entry == _start for $same of $total programs"

# 1.0 stage 7 fix (F1): st_join <log> <out>. The kernel cuts a program's
# output into log lines of at most SYSCALL_LINE_MAX (100) characters of text
# after "[pid N] " (kernel/proc/syscall_core.c), with no continuation
# marker. This joins a [pid N] line whose text is exactly 100 characters
# with the next line of the same pid (again while that part is 100). Used
# only by the stage 7 startup checks below. Limitation: a genuine
# 100-character line followed by another line of the same pid is joined too.
st_join() {
    awk '
        {
            if (match($0, /^\[pid [0-9]+\] /)) {
                p = substr($0, 1, RLENGTH); t = substr($0, RLENGTH + 1)
                if (p in held) { line = held[p] t; delete held[p] } else line = $0
                if (length(t) == 100) { held[p] = line; next }
                print line
            } else print
        }
        END { for (p in held) print held[p] }' "$1" > "$2"
}

echo "== [1.0] test=startup: argc, argv, envp and auxv (System V start-up, -m 32M)"
LOG=$OUT/serial-startup.log
boot_until "$LOG" 300 "$DREADY" -m 32M -kernel "$KERNEL" -initrd "$OUT/user/args.elf" -append "test=startup"
st_join "$LOG" "$OUT/startup-joined.txt"
LOG=$OUT/startup-joined.txt
grep -E "^Startup test: |^\[pid [0-9]+\] args: (auxv|AT_|layout|registers|[0-9]+ wrong)" "$LOG" | sed 's/^/      /'
# st_pid <case>: the process of "Startup test: case <case> (...)".
st_pid() { grep -m1 -E "^Startup test: case $1 \(" "$LOG" | sed -nE 's/.*: process ([0-9]+) exited.*/\1/p'; }
# st_lines <pid>: its args lines, without the prefix.
st_lines() { sed -nE "s/^\[pid $1\] (args: .*)$/\1/p" "$LOG"; }
P1=$(st_pid 1); P2=$(st_pid 2); P3=$(st_pid 3); P4=$(st_pid 4); P5=$(st_pid 5)
P1=${P1:-x}; P2=${P2:-x}; P3=${P3:-x}; P4=${P4:-x}; P5=${P5:-x}
expect_in_order "$LOG" "[1.0] startup: built-in args: argc 4, argv 'args' 'one' 'two' 'three' received exactly" \
    "^\[pid $P1\] args: argc 4$" "^\[pid $P1\] args: argv\[0\] 'args'$" "^\[pid $P1\] args: argv\[1\] 'one'$" \
    "^\[pid $P1\] args: argv\[2\] 'two'$" "^\[pid $P1\] args: argv\[3\] 'three'$" "^\[pid $P1\] args: envc 2$"
expect_in_order "$LOG" "[1.0] startup: built-in args: envc 2, envp 'HOME=/home' 'X=1' received exactly, envp[2] NULL" \
    "^\[pid $P1\] args: envc 2$" "^\[pid $P1\] args: envp\[0\] 'HOME=/home'$" "^\[pid $P1\] args: envp\[1\] 'X=1'$" \
    "^\[pid $P1\] args: auxv: "
expect_in_order "$LOG" "[1.0] startup: auxv: each type once, AT_NULL last, PAGESZ 4096, CLKTCK 100, ENTRY = the program's entry point, BASE/FLAGS/SECURE/UID/EUID/GID/EGID 0, HWCAP = CPUID.1:EDX, HWCAP2 0; a built-in's AT_PHDR 0, AT_PHNUM 0" \
    "^\[pid $P1\] args: auxv: 18 entries, each type once, AT_NULL last: OK$" \
    "^\[pid $P1\] args: auxv values: PAGESZ 4096, CLKTCK 100, ENTRY = the entry point, BASE FLAGS SECURE UID EUID GID EGID HWCAP2 0, HWCAP = CPUID\.1:EDX: OK$" \
    "^\[pid $P1\] args: AT_PHDR 0x0000000000000000, AT_PHENT 56, AT_PHNUM 0: OK$"
expect "$LOG" "^\[pid $P1\] args: AT_PLATFORM 'x86_64', AT_EXECFN 'args': OK$" \
    "[1.0] startup: AT_PLATFORM 'x86_64' and AT_EXECFN 'args' point inside the start block"
expect "$LOG" "^\[pid $P1\] args: layout: RSP 0x00007fffffffe[0-9a-f]{2}0 16-byte aligned, argc/argv/envp/auxv contiguous, strings above auxv in order, 8 zero bytes at the top: OK$" \
    "[1.0] startup: layout: RSP 16-byte aligned at entry, argc/argv/envp/auxv contiguous, strings above auxv in order, 8 zero bytes at the stack top, block inside the top page"
REGS="registers at entry: 0 except RSP, RFLAGS 0x0000000000000202: OK"
if grep -qxF "[pid $P1] args: $REGS" "$LOG" && grep -qxF "[pid $P2] args: $REGS" "$LOG"; then
    pass "[1.0] startup: args (built-in and module, through its args_entry stub): every general register 0 at entry except RSP, RFLAGS 0x202"
else
    fail "[1.0] startup: args's register line missing or wrong for process $P1 (built-in) or $P2 (module)"
fi
expect_in_order "$LOG" "[1.0] startup: built-in args exited with status 0 (no wrong value)" \
    "^\[pid $P1\] args: 0 wrong$" "^Startup test: case 1 \(built-in, .*\): process $P1 exited, status 0 \(expected 0\): OK$"
# The module: AT_PHDR and AT_PHNUM as Linux computes them from args.elf's
# headers (the address of the headers if a PT_LOAD's file bytes hold them,
# else 0); every other line the same as the built-in's.
AE=$OUT/user/args.elf
PHNUM=$(readelf -hW "$AE" | awk -F: '/Number of program headers/ { gsub(/ /, "", $2); print $2 }')
PHOFF=$(readelf -hW "$AE" | awk -F: '/Start of program headers/ { split($2, f, " "); print f[1] }')
PHDR=0
while read -r off va fsz; do
    if [ $(( off )) -le "$PHOFF" ] && [ "$PHOFF" -lt $(( off + fsz )) ]; then PHDR=$(( va + PHOFF - off )); break; fi
done < <(readelf -lW "$AE" | awk '$1 == "LOAD" { print $2, $3, $5 }')
PHLINE=$(printf 'args: AT_PHDR 0x%016x, AT_PHENT 56, AT_PHNUM %d: OK' "$PHDR" "${PHNUM:-0}")
echo "      args.elf: e_phoff $PHOFF, e_phnum $PHNUM: expected '$PHLINE'"
if [ "$P1" != x ] && [ "$P2" != x ] && grep -qxF "[pid $P2] $PHLINE" "$LOG" &&
   [ "$(st_lines "$P1" | grep -v '^args: AT_PHDR ')" = "$(st_lines "$P2" | grep -v '^args: AT_PHDR ')" ] &&
   grep -qE "^Startup test: case 2 \(module, .*\): process $P2 exited, status 0 \(expected 0\): OK$" "$LOG"; then
    pass "[1.0] startup: ELF module args: the same argv, envp, auxv and layout lines as the built-in; AT_PHDR/AT_PHNUM from its own headers; status 0"
else
    fail "[1.0] startup: module args (process $P2) differs from the built-in (process $P1), or its AT_PHDR line is not '$PHLINE'"
fi
expect_in_order "$LOG" "[1.0] startup: argc 1 and no environment: envc 0, envp[0] NULL, status 0" \
    "^\[pid $P3\] args: argc 1$" "^\[pid $P3\] args: argv\[0\] 'args'$" "^\[pid $P3\] args: envc 0$" \
    "^\[pid $P3\] args: layout: .*: OK$" "^\[pid $P3\] args: 0 wrong$" \
    "^Startup test: case 3 \(.*\): process $P3 exited, status 0 \(expected 0\): OK$"
L255=$(printf 'abcdefghijklmnopqrstuvwxyz%.0s' 1 2 3 4 5 6 7 8 9 10)
L255=${L255:0:255}
expect_in_order "$LOG" "[1.0] startup: an empty argument and a 255-byte argument received exactly" \
    "^\[pid $P4\] args: argc 3$" "^\[pid $P4\] args: argv\[1\] ''$" "^\[pid $P4\] args: argv\[2\] '$L255'$" \
    "^\[pid $P4\] args: 0 wrong$" "^Startup test: case 4 \(.*\): process $P4 exited, status 0 \(expected 0\): OK$"
XL=$(sed -nE "s/^Startup test: a start block of exactly 4096 bytes \(argv 'args' and ([0-9]+) bytes\): process ([0-9]+) exited, status 0 \(expected 0\): OK$/\1 \2/p" "$LOG")
XN=${XL% *}; XP=${XL#* }; XN1=x
[ -n "$XL" ] && XN1=$(( XN + 1 ))
if [ -n "$XL" ] && grep -qxF "[pid $XP] args: argv[1] $XN bytes" "$LOG" &&
   grep -qE "^\[pid $XP\] args: layout: RSP 0x00007fffffffe000 .*: OK$" "$LOG" && grep -qxF "[pid $XP] args: 0 wrong" "$LOG"; then
    pass "[1.0] startup: a start block of exactly 4096 bytes starts (RSP at the top page's start), status 0"
else
    fail "[1.0] startup: the 4096-byte start block (argument '${XN:-?}' bytes, process ${XP:-?})"
fi
expect "$LOG" "^Startup test: one byte more \(${XN1}\): refused with -7 \(E2BIG\), processes ([0-9]+) -> \1, heap ([0-9]+) -> \2 bytes, ([0-9]+) -> \3 blocks, start blocks live 0: OK$" \
    "[1.0] startup: one byte more refused with E2BIG: no process slot taken, heap used bytes and blocks unchanged"
expect_in_order "$LOG" "[1.0] startup: kernel numeric start: argv 'args' '7' (argc 2)" \
    "^\[pid $P5\] args: argc 2$" "^\[pid $P5\] args: argv\[0\] 'args'$" "^\[pid $P5\] args: argv\[1\] '7'$" \
    "^Startup test: case 5 \(numeric start, .*\): process $P5 exited, status 0 \(expected 0\): OK$"
expect "$LOG" "^Startup test: 20 starts killed before ring 3: 20 killed, 20 with no return to ring 3; start blocks live 0 -> 0; heap ([0-9]+) -> \1 bytes, ([0-9]+) -> \2 blocks; processes ([0-9]+) -> \3: OK$" \
    "[1.0] startup: 20 starts killed before ring 3: every start block freed (live 0), heap used bytes and blocks back to the start"
expect "$LOG" "^Startup test: waits: the longest bounded wait [0-9]+ ticks of its 300-tick limit \(margin [0-9]+%\)$" \
    "[1.0] startup: waits within their limits (margins reported)"
if grep -qxF "Startup test: PASS" "$LOG" && ! grep -q "PANIC" "$LOG"; then
    pass "[1.0] startup: PASS, no panic"
else
    fail "[1.0] startup: no PASS line, or a panic"
fi

echo "== [1.0] run with arguments and exported variables (a console session through QMP)"
LOG=$OUT/serial-startup-run.log
S="x{ret}run args a b{ret}{wait:1}set X=1{ret}set Y=2{ret}export X{ret}run args{ret}{wait:1}"
S+="echo end-of-startup-test{ret}"
boot_text "$LOG" 60 "System ready." "$S" 0.05 $'\nend-of-startup-test\n' -m 32M -kernel "$KERNEL" -initrd "$OUT/user/args.elf"
grep -E "^madire> |^\[pid [0-9]+\] args: (arg|env)|^\[process " "$LOG" | sed 's/^/      /'
expect_in_order "$LOG" "[1.0] run args a b: argv reaches a module program through the shell (argc 3, 'args' 'a' 'b')" \
    "^madire> run args a b$" "^\[pid [0-9]+\] args: argc 3$" "^\[pid [0-9]+\] args: argv\[0\] 'args'$" \
    "^\[pid [0-9]+\] args: argv\[1\] 'a'$" "^\[pid [0-9]+\] args: argv\[2\] 'b'$" "^\[pid [0-9]+\] args: envc 0$" \
    "^\[process [0-9]+ 'args' exited, status 0\]$"
expect_in_order "$LOG" "[1.0] run args with X exported (set, export): envp holds exactly 'X=1'; Y, set but not exported, is absent" \
    "^madire> run args$" "^\[pid [0-9]+\] args: argc 1$" "^\[pid [0-9]+\] args: envc 1$" \
    "^\[pid [0-9]+\] args: envp\[0\] 'X=1'$" "^\[pid [0-9]+\] args: 0 wrong$" "^\[process [0-9]+ 'args' exited, status 0\]$"

# ---- 1.0 stage 8: the console's file commands and the Live label ----

echo "== [1.0] Console: the file commands (pwd, cd, ls, mkdir, rmdir, touch, stat, cat) and the Live label"
# One console session through QMP at the pace proven in stage 0d (a 50 ms
# gap and a 20 ms hold). The test program fsmake (user/prog/fsmake.c) is a
# module given with -initrd (not in limine.conf): it makes files with known
# contents, as stage 8 has no command that writes data. Keys typed while a
# foreground program or "cat" runs are not typed (only Ctrl+C reaches
# them), so the session waits after each. Each check finds its typed line
# by the prompt logged in front of it; a typed line that comes twice is
# read in order (expect_in_order) from an earlier unique line, or typed with
# trailing spaces, which the shell ignores, so its logged line is unique
# ("pwd ", "pwd  ", "pwd   "). Between two typed lines the console may log
# "console: scrolled N lines", so a check that spans several typed lines
# matches each one's output with fs_lines.
# fs_lines <log> <start regex> <regex>...: expect_lines without counting a
# check (a part of a check).
fs_lines() {
    local log=$1 start=$2
    shift 2
    START="$start" PATS="$(printf '%s\034' "$@")" awk '
        BEGIN { n = split(ENVIRON["PATS"], p, "\034") - 1 }
        on && !/^key: / { i++; if ($0 !~ p[i]) exit; if (i == n) { ok = 1; exit } }
        !on && $0 ~ ENVIRON["START"] { on = 1 }
        END { exit !ok }' "$log"
}
LOG=$OUT/serial-files.log
FSOCK=$OUT/qmp-files.sock
S="x{wait:1}pwd{ret}version{ret}help files{ret}help cat{ret}ls --help{ret}"
S+="ls /{ret}ls -l /{ret}cd /tmp{ret}cd{ret}pwd {ret}"
S+="mkdir docs{ret}mkdir /home/docs{ret}mkdir /nosuch/x{ret}stat /home{ret}"
S+="touch docs/a.txt{ret}stat docs/a.txt{ret}{wait:2}touch /home/docs/a.txt{ret}stat /home/docs/a.txt{ret}"
S+="{wait:2}cd docs{ret}pwd  {ret}cd ..{ret}pwd   {ret}cd /home/docs/a.txt{ret}cd /nosuch{ret}"
S+="run -b fsmake big.dat 2097152{ret}{wait:1}"
S+="run fsmake data.txt 12300{ret}{wait:2}ls /home{ret}cat data.txt{ret}{wait:2}"
S+="run fsmake bin.dat 300 bin{ret}{wait:2}cat bin.dat{ret}{wait:1}"
S+="run fsmake long.txt 200 long{ret}{wait:2}cat long.txt{ret}{wait:1}"
S+="cat docs{ret}cat nosuch{ret}rmdir docs{ret}rmdir /{ret}rmdir .{ret}"
S+="{wait:2}mkdir empty{ret}rmdir empty{ret}ls -l /home{ret}"
S+="mkdir{ret}rmdir{ret}touch{ret}stat{ret}cat{ret}ls a b{ret}ls -x{ret}"
S+="{wait:3}cat big.dat{ret}{ctrl+c}{wait:2}echo after-cancel{ret}"
S+="mkdir /tmp/gone{ret}cd /tmp/gone{ret}rmdir /tmp/gone{ret}pwd{ret}ls{ret}cd ..{ret}cd /{ret}pwd{ret}"
S+="echo end-of-fs-test{ret}"
rm -f "$LOG" "$FSOCK"
T0F=$(date +%s%N)
timeout 240 qemu-system-x86_64 -display none -no-reboot -serial file:"$LOG" \
    -qmp unix:"$FSOCK",server=on,wait=off -m 64M -kernel "$KERNEL" -initrd "$OUT/user/fsmake.elf" \
    >/dev/null 2>&1 &
FQPID=$!
FOUT=$(python3 "$QMPKEYS" "$FSOCK" "$LOG" "System ready." --text "$S" --gap 0.05 --hold 20 \
       --done $'\nend-of-fs-test\n' --done-timeout 200 2>&1)
echo "      $FOUT"
echo "$FOUT" > "${LOG%.log}.keys.txt"
sleep 1
kill "$FQPID" 2>/dev/null
wait "$FQPID" 2>/dev/null
[ -f "$LOG" ] || : > "$LOG"
boot_seconds "$LOG" "$T0F"
grep -E "^madire> |^\[pid [0-9]+\] fsmake|^\[process |^console: (cat|Ctrl\+C)" "$LOG" | sed 's/^/      /'

NEXT="^(console: |madire> |uptime: [0-9]+ s$)"
D="[0-9]{4}-[0-9]{2}-[0-9]{2} [0-9]{2}:[0-9]{2}"
expect_in_order "$LOG" "[1.0] Live label: the console's first lines after the title say Live mode and that nothing is saved" \
    "System ready\. Press any key" "^Live mode: files are kept in memory only\.$" \
    "^Nothing is saved: all files are lost when the computer restarts\.$" "^console: started$"
expect_lines "$LOG" "[1.0] version: the version line, then 'Live mode (files in memory only)'" \
    "^madire> version$" "^Madire OS v$VRE \(64-bit x86-64, Multiboot 1\)$" "^Live mode \(files in memory only\)$" "$NEXT"
expect_lines "$LOG" "[1.0] pwd: the shell starts in /home" "^madire> pwd$" "^/home$" "$NEXT"
expect_lines "$LOG" "[1.0] help files: the section's title and its 8 commands in help order" \
    "^madire> help files$" "^files: Files and folders$" "^  pwd      Show the current folder$" \
    "^  cd       Change the current folder: cd \[path\]$" "^  ls       List a folder: ls \[-l\] \[path\]$" \
    "^  mkdir    Create a folder: mkdir <path>$" "^  rmdir    Remove an empty folder: rmdir <path>$" \
    "^  touch    Create an empty file, or update its time$" \
    "^  stat     Show a file's or folder's details: stat <path>$" \
    "^  cat      Show a file's contents: cat <path>$" "$NEXT"
expect_lines "$LOG" "[1.0] help cat: purpose and usage" \
    "^madire> help cat$" "^cat: Show a file's contents: cat <path>$" "^Usage: cat <path>$" "$NEXT"
expect_lines "$LOG" "[1.0] ls --help: purpose and usage" \
    "^madire> ls --help$" "^ls: List a folder: ls \[-l\] \[path\]$" "^Usage: ls \[-l\] \[path\]$" "$NEXT"
expect_lines "$LOG" "[1.0] ls: / holds home/ and tmp/ (folders marked /)" "^madire> ls /$" "^home/$" "^tmp/$" "$NEXT"
expect_lines "$LOG" "[1.0] ls -l /: type, size, date and name per row" \
    "^madire> ls -l /$" "^d       0 $D home/$" "^d       0 $D tmp/$" "$NEXT"
if fs_lines "$LOG" "^madire> cd /tmp$" "$NEXT" && fs_lines "$LOG" "^madire> cd$" "$NEXT" &&
        fs_lines "$LOG" "^madire> pwd $" "^/home$" "$NEXT"; then
    pass "[1.0] cd with no argument: /home"
else
    fail "[1.0] cd with no argument: not /home after cd /tmp, cd"
fi
expect_lines "$LOG" "[1.0] mkdir docs: created, nothing printed" "^madire> mkdir docs$" "$NEXT"
expect_lines "$LOG" "[1.0] mkdir of an existing name: already exists" \
    "^madire> mkdir /home/docs$" "^mkdir: '/home/docs': already exists\.$"
expect_lines "$LOG" "[1.0] mkdir in a missing folder: no such file or folder" \
    "^madire> mkdir /nosuch/x$" "^mkdir: '/nosuch/x': no such file or folder\.$"
if fs_lines "$LOG" "^madire> touch docs/a\.txt$" "$NEXT" &&
        fs_lines "$LOG" "^madire> stat docs/a\.txt$" "^File:   docs/a\.txt$" "^Type:   file$" \
            "^Size:   0 bytes$" "^Pages:  0 \(4 KiB each, the index page included\)$" "^Node:   [0-9]+$" \
            "^Links:  1$" "^Mode:   0644$"; then
    pass "[1.0] touch docs/a.txt: an empty file (stat: file, 0 bytes, 0 pages, mode 0644)"
else
    fail "[1.0] touch docs/a.txt: not silent, or stat does not show an empty file with mode 0644"
fi
M1=$(awk '/^madire> stat docs\/a\.txt$/ { on = 1 } on && /^Modify: / { print substr($0, 9); exit }' "$LOG")
M2=$(awk '/^madire> stat \/home\/docs\/a\.txt$/ { on = 1 } on && /^Modify: / { print substr($0, 9); exit }' "$LOG")
echo "      modify times: '$M1', then '$M2'"
if [ -n "$M1" ] && [ -n "$M2" ] && [[ "$M2" > "$M1" ]]; then
    pass "[1.0] touch of an existing file: its modify time moves forward"
else
    fail "[1.0] touch of an existing file: modify time '$M1' then '$M2'"
fi
expect_lines "$LOG" "[1.0] stat of a file: type, size, pages, node, links, mode and three times" \
    "^madire> stat docs/a\.txt$" "^File:   docs/a\.txt$" "^Type:   file$" "^Size:   0 bytes$" \
    "^Pages:  0 \(4 KiB each, the index page included\)$" "^Node:   [1-9][0-9]*$" "^Links:  1$" \
    "^Mode:   0644$" "^Access: $D:[0-9]{2}$" "^Modify: $D:[0-9]{2}$" "^Change: $D:[0-9]{2}$" "$NEXT"
expect_lines "$LOG" "[1.0] stat of a folder: type folder, links 2 + subfolders" \
    "^madire> stat /home$" "^File:   /home$" "^Type:   folder$" "^Size:   0 bytes$" \
    "^Pages:  0 \(4 KiB each, the index page included\)$" "^Node:   [1-9][0-9]*$" "^Links:  3$" \
    "^Mode:   0755$" "^Access: " "^Modify: " "^Change: " "$NEXT"
if fs_lines "$LOG" "^madire> cd docs$" "$NEXT" && fs_lines "$LOG" "^madire> pwd  $" "^/home/docs$" "$NEXT" &&
        fs_lines "$LOG" "^madire> cd \.\.$" "$NEXT" && fs_lines "$LOG" "^madire> pwd   $" "^/home$" "$NEXT"; then
    pass "[1.0] cd docs then cd ..: pwd follows (/home/docs, then /home)"
else
    fail "[1.0] cd docs then cd ..: pwd does not show /home/docs, then /home"
fi
expect_lines "$LOG" "[1.0] cd to a file: not a folder" \
    "^madire> cd /home/docs/a\.txt$" "^cd: '/home/docs/a\.txt': not a folder\.$"
expect_lines "$LOG" "[1.0] cd to a missing folder: no such file or folder" \
    "^madire> cd /nosuch$" "^cd: '/nosuch': no such file or folder\.$"
expect_in_order "$LOG" "[1.0] run starts a program in the shell's current folder: fsmake reports cwd /home" \
    "^madire> run fsmake data\.txt 12300$" "^\[pid [0-9]+\] fsmake: cwd /home, wrote to data\.txt: 12300$" \
    "^\[process [0-9]+ 'fsmake' exited, status 0\]$"
expect_in_order "$LOG" "[1.0] the program's relative file is in the shell's folder: ls /home shows data.txt" \
    "^madire> ls /home$" "^docs/$" "^data\.txt$"
# The text cat printed for a file, as the console prints it: the lines after
# "madire> cat <file>" up to its "console: cat:" line, without the lines
# other threads may log meanwhile; compared with the file fsmake made.
CATCHK=$(python3 - "$LOG" <<'PYEOF'
import re, sys
log = open(sys.argv[1], encoding="latin-1").read().split("\n")
def shown(data):
    lines, cur = [], ""
    for b in data:
        if b == 10:
            lines.append(cur); cur = ""; continue
        if len(cur) == 79:
            lines.append(cur); cur = ""
        cur += chr(b) if 0x20 <= b <= 0x7E else "?"
    if cur:
        lines.append(cur)
    return lines
def text(n):
    out = bytearray()
    for off in range(n):
        line, col = off // 64 + 1, off % 64
        if col == 63: out.append(10)
        elif col < 11: out += ("line %05d " % line).encode()[col:col + 1]
        else: out.append(ord("a") + (line + col) % 26)
    return out
def printed(name):
    try:
        i = log.index("madire> cat " + name)
    except ValueError:
        return None, None
    got = []
    for l in log[i + 1:]:
        if l.startswith("console: cat: "):
            return got, l
        if l.startswith("madire> "):
            return got, None
        if re.match(r"^(\[pid \d+\] |\[process \d+ |uptime: \d+ s$|console: )", l):
            continue
        got.append(l)
    return got, None
cases = [("data.txt", text(12300), 12300),
         ("bin.dat", bytes(i % 256 for i in range(300)), 300),
         ("long.txt", bytes(ord("a") + i % 26 for i in range(200)), 200)]
for name, data, size in cases:
    got, end = printed(name)
    want = shown(data)
    ok = got == want and end is not None and end.startswith("console: cat: %d bytes in " % size)
    print("%s %s %d %d" % (name, "OK" if ok else "BAD", len(got or []), len(want)))
PYEOF
)
echo "$CATCHK" | sed 's/^/      /'
if grep -qx "data.txt OK 193 193" <<< "$CATCHK"; then
    pass "[1.0] cat: a 12,300-byte file (4 chunks) prints its 192 lines and its last 12 bytes exactly"
else
    fail "[1.0] cat: the 12,300-byte file not printed exactly ($(grep '^data' <<< "$CATCHK"))"
fi
if grep -qx "bin.dat OK 6 6" <<< "$CATCHK"; then
    pass "[1.0] cat: bytes outside 0x20-0x7E (except newline) shown as '?'"
else
    fail "[1.0] cat: bin.dat not shown as expected ($(grep '^bin' <<< "$CATCHK"))"
fi
if grep -qx "long.txt OK 3 3" <<< "$CATCHK"; then
    pass "[1.0] cat: a 200-character line shown in lines of 79, 79 and 42 characters"
else
    fail "[1.0] cat: long.txt not cut 79, 79, 42 ($(grep '^long' <<< "$CATCHK"))"
fi
expect_in_order "$LOG" "[1.0] cat: the 2 MiB file written by a background run (status 0) before the cat" \
    "^madire> run -b fsmake big\.dat 2097152$" "^\[pid [0-9]+\] fsmake: cwd /home, wrote to big\.dat: 2097152$" \
    "^\[process [0-9]+ 'fsmake' exited, status 0\]$" "^madire> cat big\.dat$"
CB=$(sed -nE 's/^console: Ctrl\+C: cat cancelled after ([0-9]+) bytes \([0-9]+ ticks\)$/\1/p' "$LOG" | head -n 1)
echo "      cat big.dat: cancelled after ${CB:-?} bytes"
CC=$(awk '/^madire> cat big\.dat$/ { on = 1; next } on && /^madire> / { exit } on && /^cat: cancelled\.$/ { n++ } END { print n + 0 }' "$LOG")
if [ -n "$CB" ] && [ "$CB" -lt 2097152 ] && [ "$CC" = 1 ]; then
    pass "[1.0] cat of a 2 MiB file: Ctrl+C cancels it (cat: cancelled.) after fewer than 2,097,152 bytes"
else
    fail "[1.0] cat of a 2 MiB file: not cancelled (${CB:-no cancel line} bytes)"
fi
expect_in_order "$LOG" "[1.0] after a cancelled cat the next command runs" \
    "^cat: cancelled\.$" "^madire> echo after-cancel$" "^after-cancel$"
expect_lines "$LOG" "[1.0] cat of a folder: is a folder" "^madire> cat docs$" "^cat: 'docs': is a folder\.$"
expect_lines "$LOG" "[1.0] cat of a missing file: no such file or folder" \
    "^madire> cat nosuch$" "^cat: 'nosuch': no such file or folder\.$"
expect_lines "$LOG" "[1.0] rmdir of a non-empty folder: refused" \
    "^madire> rmdir docs$" "^rmdir: 'docs': the folder is not empty\.$"
if fs_lines "$LOG" "^madire> rmdir /$" "^rmdir: '/': the root folder cannot be removed\.$" &&
        fs_lines "$LOG" "^madire> rmdir \.$" "^rmdir: '\.': the path ends in '\.'\.$"; then
    pass "[1.0] rmdir / and rmdir .: refused (root, '.')"
else
    fail "[1.0] rmdir / and rmdir .: not refused as expected"
fi
RE=$(awk '/^madire> rmdir empty$/ { on = 1; next } on { print; exit }' "$LOG")
if grep -qE "$NEXT" <<< "$RE" &&
        [ "$(awk '/^madire> ls -l \/home$/ { on = 1; next } on && /^madire> / { exit } on' "$LOG" | grep -c 'empty/$')" = 0 ] &&
        [ "$(awk '/^madire> ls -l \/home$/ { on = 1; next } on && /^madire> / { exit } on' "$LOG" | grep -cE '^-   12300 [0-9-]+ [0-9:]+ data\.txt$')" = 1 ]; then
    pass "[1.0] rmdir of an empty folder: removed, ls no longer lists it"
else
    fail "[1.0] rmdir of an empty folder: not removed silently, or still listed"
fi
expect_in_order "$LOG" "[1.0] the shell's current folder removed: pwd says so" \
    "^madire> rmdir /tmp/gone$" "^madire> pwd$" "^pwd: the current folder was removed \(cd / to leave it\)\.$"
expect_in_order "$LOG" "[1.0] in a removed current folder, ls and cd .. fail with no such file or folder and the hint" \
    "^madire> rmdir /tmp/gone$" "^madire> ls$" "^ls: '\.': no such file or folder\.$" \
    "^\(The current folder was removed: use an absolute path, for example cd /\.\)$" \
    "^madire> cd \.\.$" "^cd: '\.\.': no such file or folder\.$" \
    "^\(The current folder was removed: use an absolute path, for example cd /\.\)$"
expect_in_order "$LOG" "[1.0] cd / leaves a removed folder: pwd shows /" \
    "^madire> rmdir /tmp/gone$" "^madire> cd /$" "^madire> pwd$" "^/$"
if fs_lines "$LOG" "^madire> mkdir$" "^Usage: mkdir <path>$" && fs_lines "$LOG" "^madire> rmdir$" "^Usage: rmdir <path>$" &&
        fs_lines "$LOG" "^madire> touch$" "^Usage: touch <path>$" && fs_lines "$LOG" "^madire> stat$" "^Usage: stat <path>$" &&
        fs_lines "$LOG" "^madire> cat$" "^Usage: cat <path>$" && fs_lines "$LOG" "^madire> ls a b$" "^Usage: ls \[-l\] \[path\]$" &&
        fs_lines "$LOG" "^madire> ls -x$" "^Usage: ls \[-l\] \[path\]$"; then
    pass "[1.0] usage errors: mkdir, rmdir, touch, stat and cat without a path; ls with two paths; ls -x"
else
    fail "[1.0] usage errors: a usage line missing or different"
fi
w=$(max_width "$LOG" "^console: started$" "^end-of-fs-test$")
if grep -q "^end-of-fs-test$" "$LOG" && [ "$w" -le 79 ]; then
    pass "[1.0] file session: every console line at most 79 characters (longest $w)"
else
    fail "[1.0] file session: a console line of $w characters (limit 79), or the session did not finish"
fi
reject "$LOG" "PANIC|events dropped|Screen output from thread|Unexpected IRQ" \
                                                      "[1.0] file session: no panic, dropped key, screen-rule violation or self-check"
