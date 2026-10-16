# Madire OS QEMU tests, part 3 (0.12, stage 0c): [0.10] usersys to
# userchurn, and [0.11]. Sourced by tests/qemu-tests.sh (a full run, or
# --shard 3). Apart from this header and the fixture calls, every line was
# moved unchanged from the single script.

fixture_normal_boot     # 0.12: tests/qemu-lib.sh (the normal boot is read below)
# Logs of part 1 that this part reads: the most-modules check reads the 32M
# boot's reserved ranges, the policy (b) checks the thread diagnostics'.
fixture_log "$OUT/serial-mem-32M.log" 10 -m 32M -kernel "$KERNEL"
fixture_log "$OUT/serial-preemptoff.log" 10 -m 32M -kernel "$KERNEL" -append "test=preemptoff"
fixture_log "$OUT/serial-heapthreads.log" 12 -m 32M -kernel "$KERNEL" -append "test=heapthreads"
fixture_log "$OUT/serial-framethreads.log" 12 -m 32M -kernel "$KERNEL" -append "test=framethreads"

echo "== [0.10] The first system calls: exit, exit_group, getpid, sched_yield, -ENOSYS (test=usersys)"
# The MSRs, read back at every boot (here the normal boot of the first group).
SC=$(grep -m1 -E '^SYSCALL: ' "$OUT/serial-boot.log")
echo "      $SC"
ENTRY=$(nm "$OUT/madire.elf" | awk '$3 == "syscall_entry" { print $1 }')
if grep -qE "^SYSCALL: EFER=0x[0-9a-f]{15}[13579bdf] \(SCE=1\) STAR=0x002b000800000000 LSTAR=0x$ENTRY SFMASK=0x0000000000047700, read back: OK$" <<< "$SC" &&
   [ -n "$ENTRY" ]; then
    pass "[0.10] SYSCALL MSRs read back: EFER.SCE=1, STAR 0x002b000800000000, LSTAR = syscall_entry (0x$ENTRY), SFMASK TF|IF|DF|IOPL|NT|AC"
else
    fail "[0.10] SYSCALL MSRs wrong (${SC:-no line}; syscall_entry 0x${ENTRY:-?})"
fi
LOG=$OUT/serial-usersys.log
boot_text "$LOG" 90 "System ready." "x{ret}version{ret}echo end-of-sys-test{ret}" 0.05 \
    $'\nend-of-sys-test\n' -m 32M -kernel "$KERNEL" -append "test=usersys"
grep -E "^User syscall test: " "$LOG" | grep -vE ': (PASS|FAIL)$|system calls\.\.\.$' | sed 's/^/      /'
for c in "exitcode 0 42" "exitcode 1 43" "exitcode 2 7" "exitcode 3 44" "yield 0 0" "enosys 0 0" "sysregs 0 0"; do
    read -r cp ca cs <<< "$c"
    expect "$LOG" "^User syscall test: $cp $ca \(.*\): process [0-9]+ exited, status $cs \(expected $cs\): OK$" \
                                                      "[0.10] usersys: $cp $ca: the kernel collected exit status $cs"
done
GP=$(grep -m1 -E "^User syscall test: getpid 0 " "$LOG")
GPID=$(sed -nE 's/.*: process ([0-9]+) exited, status ([0-9]+) .*/\1/p' <<< "$GP")
GST=$(sed -nE 's/.*: process ([0-9]+) exited, status ([0-9]+) .*/\2/p' <<< "$GP")
if [ -n "$GPID" ] && [ "$GST" = "$(( GPID & 255 ))" ] && grep -qE ': OK$' <<< "$GP"; then
    pass "[0.10] usersys: getpid returned the kernel's process ID (exit status $GST = PID $GPID & 0xFF)"
else
    fail "[0.10] usersys: getpid wrong (${GP:-no line})"
fi
# 1.0 stage 4: 1024 unknown numbers (1028 until read, close, dup and dup2
# were implemented; enosys skips those four). 1.0 stage 5: 1016 (open,
# stat, fstat, lstat, lseek, ftruncate, openat and newfstatat skipped too).
# 1.0 stage 6: 1007 (getcwd, chdir, rename, mkdir, rmdir, unlink,
# getdents64, mkdirat and unlinkat skipped too).
expect "$LOG" "^User syscall test: system calls made: getpid 3, sched_yield 100, exit 7, exit_group 1, write 4, -ENOSYS 1007: OK$" \
                                                      "[0.10] usersys: exactly the calls made were counted; 1007 unknown numbers (0-1023, x32, >= 2^32, -1, Madire range) gave -ENOSYS"
expect "$LOG" "^User syscall test: spin kept running in ring 3 meanwhile, then was killed: OK$" \
                                                      "[0.10] usersys: another process kept running in ring 3"
TD=$(grep -m1 -E '^User syscall test: [0-9]+ address spaces torn down' "$LOG")
F0=$(sed -nE 's/.*free pages ([0-9]+) -> ([0-9]+),.*/\1/p' <<< "$TD")
F1=$(sed -nE 's/.*free pages ([0-9]+) -> ([0-9]+),.*/\2/p' <<< "$TD")
if grep -qE '^User syscall test: 9 address spaces torn down by their own threads, 0 wrong; .*processes left 0: OK$' <<< "$TD" &&
   [ -n "$F0" ] && [ "$F0" = "$F1" ]; then
    pass "[0.10] usersys: free pages ($F0 -> $F1), page tables, stack slots, threads and process table exactly restored"
else
    fail "[0.10] usersys: teardown or free pages wrong (${TD:-no line})"
fi
expect "$LOG" "^User syscall test: PASS$"             "[0.10] usersys: PASS"
reject "$LOG" "PANIC"                                 "[0.10] usersys: no crash screen, no panic"
expect "$LOG" "System ready\."                        "[0.10] usersys: System ready afterwards"
expect_next "$LOG" "[0.10] usersys: the console still works afterwards (version)" \
    "^madire> version$" "^Madire OS v$VRE \(64-bit x86-64, Multiboot 1\)$"
LOG=$OUT/serial-usersys-smap.log
boot "$LOG" 60 -m 32M -cpu qemu64,+smep,+smap -kernel "$KERNEL" -append "test=usersys"
expect "$LOG" "^User syscall test: PASS$"             "[0.10] usersys with SMEP and SMAP on: PASS"
reject "$LOG" "PANIC"                                 "[0.10] usersys with SMEP and SMAP on: no crash screen, no panic"
expect "$LOG" "System ready\."                        "[0.10] usersys with SMEP and SMAP on: System ready afterwards"

echo "== [0.10] System calls with pointer arguments: write, nanosleep, bad pointers (test=userio)"
LOG=$OUT/serial-userio.log
boot_text "$LOG" 90 "System ready." "x{ret}version{ret}echo end-of-io-test{ret}" 0.05 \
    $'\nend-of-io-test\n' -m 32M -kernel "$KERNEL" -append "test=userio"
grep -E "^(User I/O test: |\[pid )" "$LOG" | grep -vE ': (PASS|FAIL)$|bad pointers\.\.\.$|^\[pid [0-9]+\] [a-z]{100}$' | sed 's/^/      /'
# uio_pid <case text>: the process ID of a case, from its result line
uio_pid() { grep -m1 -E "^User I/O test: $1.*: process [0-9]+ exited" "$LOG" | sed -nE 's/.*: process ([0-9]+) exited.*/\1/p'; }
for c in "hello 0" "hello 1" "hello 2" "sleep 0" "sleep 1" "badptr 0" "isolate 1" "isolate 2"; do
    expect "$LOG" "^User I/O test: $c \(.*\): process [0-9]+ exited, status 0 \(expected 0\): OK$" \
                                                      "[0.10] userio: $c exited with status 0 (every result it checked was right)"
done
P=$(uio_pid "hello 0")
expect "$LOG" "^\[pid $P\] Hello from user mode$"     "[0.10] userio: hello's line in the log, prefixed [pid $P]"
P=$(uio_pid "hello 1")
expect "$LOG" "^\[pid $P\] A\?\[2JB\?C\?D\?E\?F\?G$" \
                                                      "[0.10] userio: ESC, BEL, CR, TAB, DEL and 0xFF written by a program reach the log as '?'"
expect "$LOG" "^\[pid $P\] Process 99 'spin' ended by CPU exception 13 \(#GP General Protection Fault\); the kernel continues$" \
                                                      "[0.10] userio: a fake kernel line written by a program carries its [pid $P] prefix"
reject "$LOG" "^Process 99 "                          "[0.10] userio: no unprefixed fake kernel line in the log"
n=$(LC_ALL=C grep -cE '^\[pid [0-9]+\] .*[^ -~]' "$LOG")
if [ "$n" -eq 0 ]; then pass "[0.10] userio: no program line in the log holds a byte outside 0x20-0x7E"
else fail "[0.10] userio: $n program lines hold control or non-ASCII bytes"; fi
P=$(uio_pid "hello 2")
nl=$(grep -cE "^\[pid $P\] " "$LOG")
nc=$(grep -E "^\[pid $P\] " "$LOG" | sed -E "s/^\[pid $P\] //" | tr -d '\n' | wc -c)
n100=$(grep -cE "^\[pid $P\] [a-z]{100}$" "$LOG")
if [ "$nl" -eq 41 ] && [ "$nc" -eq 4096 ] && [ "$n100" -eq 40 ]; then
    pass "[0.10] userio: a 5000-byte write took 4096 bytes (short count), logged as 40 lines of 100 and one of 96"
else
    fail "[0.10] userio: 5000-byte write logged as $nl lines, $nc characters ($n100 of 100)"
fi
expect "$LOG" "^User I/O test: nanosleep 100 ms: 10 ticks asked for, 1[01] slept: OK$" \
                                                      "[0.10] userio: nanosleep 100 ms = 10 ticks, slept 10 or 11"
P=$(uio_pid "sleep 1")
expect "$LOG" "^\[pid $P\] sleep: wrong results: 0$"  "[0.10] userio: nanosleep argument errors: -EINVAL (tv_nsec 1e9 and -1, tv_sec -1), -EFAULT (NULL, kernel, across the top), 0 for 0, rem never written"
P=$(uio_pid "badptr 0")
BL=$(grep -E "^\[pid $P\] " "$LOG")
if [ "$BL" = "[pid $P] badptr: valid write
[pid $P] xxxxxxx
[pid $P] badptr: wrong results: 0" ]; then
    pass "[0.10] userio: badptr: every bad pointer gave -EFAULT (bad fd -EBADF) and logged nothing; only its 3 valid writes reached the log"
else
    fail "[0.10] userio: badptr's log lines: $(echo "$BL" | tr '\n' '|')"
fi
n=$(grep -cE "^\[pid [0-9]+\] isolate: foreign values seen: 0$" "$LOG")
if [ "$n" -eq 2 ]; then pass "[0.10] userio: two isolate processes at the same addresses, at the same time, each saw only its own data"
else fail "[0.10] userio: $n of 2 isolate processes saw only their own data"; fi
expect "$LOG" "^User I/O test: spin kept running in ring 3 meanwhile, then was killed: OK$" \
                                                      "[0.10] userio: another process kept running in ring 3"
TD=$(grep -m1 -E '^User I/O test: [0-9]+ address spaces torn down' "$LOG")
F0=$(sed -nE 's/.*free pages ([0-9]+) -> ([0-9]+),.*/\1/p' <<< "$TD")
F1=$(sed -nE 's/.*free pages ([0-9]+) -> ([0-9]+),.*/\2/p' <<< "$TD")
if grep -qE '^User I/O test: 9 address spaces torn down by their own threads, 0 wrong; .*processes left 0: OK$' <<< "$TD" &&
   [ -n "$F0" ] && [ "$F0" = "$F1" ]; then
    pass "[0.10] userio: free pages ($F0 -> $F1), page tables, stack slots, threads and process table exactly restored"
else
    fail "[0.10] userio: teardown or free pages wrong (${TD:-no line})"
fi
expect "$LOG" "^User I/O test: PASS$"                 "[0.10] userio: PASS"
reject "$LOG" "PANIC"                                 "[0.10] userio: no crash screen, no panic"
expect "$LOG" "System ready\."                        "[0.10] userio: System ready afterwards"
expect_next "$LOG" "[0.10] userio: the console still works afterwards (version)" \
    "^madire> version$" "^Madire OS v$VRE \(64-bit x86-64, Multiboot 1\)$"
LOG=$OUT/serial-userio-smap.log
boot "$LOG" 60 -m 32M -cpu qemu64,+smep,+smap -kernel "$KERNEL" -append "test=userio"
expect "$LOG" "^User I/O test: PASS$"                 "[0.10] userio with SMEP and SMAP on: PASS (copy_from_user's stac/clac)"
reject "$LOG" "PANIC"                                 "[0.10] userio with SMEP and SMAP on: no crash screen, no panic"
expect "$LOG" "System ready\."                        "[0.10] userio with SMEP and SMAP on: System ready afterwards"

echo "== [0.10] User processes under preemption: per-process longest turn (test=userpreempt)"
thread_diag userpreempt 40
grep -E "^User preemption test: " "$LOG" | grep -vE ': (PASS|FAIL)$|for 3 seconds\.\.\.$' | sed 's/^/      /'
n=$(grep -cE "^User preemption test: spin [0-3] \(.*\): process [0-9]+, thread [0-9]+: longest turn [0-6] ticks \(at most 6\), [0-9]+ ticks run, preempted [0-9]+ times, back in ring 3 [1-9][0-9]* times: OK$" "$LOG")
if [ "$n" -eq 5 ]; then pass "[0.10] userpreempt: each of the 5 user processes (2 pure loops, getpid, sched_yield, nanosleep) had a longest turn of at most 6 ticks"
else fail "[0.10] userpreempt: $n of 5 user processes within 6 ticks (policy (a) rule: STOP and report)"; fi
expect "$LOG" "^User preemption test: kernel frame thread: longest turn [0-9]+ ticks .*, 0 tag mismatches: OK$" \
                                                      "[0.10] userpreempt: the frame-allocating kernel thread ran alongside, no frame handed out twice (its longest turn logged)"
# 0.11: the record covers every thread the test started (it is reset at the
# start), so its ticks must be the largest of the six per-thread figures.
LT=$(grep -E "^User preemption test: (spin [0-9]+ \(.*\): process [0-9]+, thread [0-9]+|kernel frame thread): longest turn [0-9]+ ticks" "$LOG" |
     sed -nE 's/.*: longest turn ([0-9]+) ticks.*/\1/p' | sort -n | tail -1)
owner_check "userpreempt" "User preemption test" "$LT"
expect "$LOG" "^User preemption test: all killed; free pages ([0-9]+) -> \1, .*processes left 0: OK$" \
                                                      "[0.10] userpreempt: all killed; free pages, tables, stacks, threads and process table restored"
expect "$LOG" "^User preemption test: PASS$"          "[0.10] userpreempt: PASS"
reject "$LOG" "PANIC"                                 "[0.10] userpreempt: no panic"
expect "$LOG" "System ready\."                        "[0.10] userpreempt: System ready afterwards"

echo "== [0.10] Process churn: 1000 processes (test=userchurn)"
thread_diag userchurn 180
grep -E "^User churn test: " "$LOG" | grep -vE ': (PASS|FAIL)$|started and ended\.\.\.$' | sed 's/^/      /'
expect "$LOG" "^User churn test: 500 hello processes one after another: 500 exited with status 0: OK$" \
                                                      "[0.10] userchurn: 500 hello processes in sequence, each status 0"
expect "$LOG" "^User churn test: 200 nullptr processes one after another: 200 ended by #PF: OK$" \
                                                      "[0.10] userchurn: 200 nullptr processes in sequence, each ended by #PF"
expect "$LOG" "^User churn test: 10 batches of 30 at once \(hello and nullptr alternating; at most 30 alive\): 300 of 300 results right: OK$" \
                                                      "[0.10] userchurn: 10 batches of 30 processes alive at once, every result right"
TD=$(grep -m1 -E '^User churn test: [0-9]+ address spaces torn down' "$LOG")
F0=$(sed -nE 's/.*free pages ([0-9]+) -> ([0-9]+),.*/\1/p' <<< "$TD")
F1=$(sed -nE 's/.*free pages ([0-9]+) -> ([0-9]+),.*/\2/p' <<< "$TD")
if grep -qE '^User churn test: 1000 address spaces torn down by their own threads: [0-9]+ user frames and 6000 lower-half tables freed, 0 wrong; .*processes left 0: OK$' <<< "$TD" &&
   [ -n "$F0" ] && [ "$F0" = "$F1" ]; then
    pass "[0.10] userchurn: 1000 address spaces torn down; free pages ($F0 -> $F1), tables, stack slots, threads and process table exactly restored"
else
    fail "[0.10] userchurn: teardown or free pages wrong (${TD:-no line})"
fi
expect "$LOG" "^User churn test: PASS$"               "[0.10] userchurn: PASS"
reject "$LOG" "PANIC"                                 "[0.10] userchurn: no panic"
expect "$LOG" "System ready\."                        "[0.10] userchurn: System ready afterwards"

echo "== [0.11] ELF checks on the host: the built programs and the malformed fixtures"
# The kernel's own elf_core.c, built as a host tool (build/elfcheck), on the
# 26 built programs (19 until stage 2b added lines, 20 until 1.0 stage 4
# added fdops and fdhold, 22 until stage 5 added fileio, fileerr and bigio,
# 25 until stage 6 added dirops) and on fixtures made from hello.elf by
# tests/elf-fixtures.py (one broken rule each; manifest.txt holds the exact
# reason each must be refused with). No kernel uses elf_core yet (stage 1).
ELFCHECK=$OUT/elfcheck
FIX=$OUT/elf-fixtures
EC=$("$ELFCHECK" "$OUT"/user/*.elf 2>&1)
nprog=$(ls "$OUT"/user/*.elf 2>/dev/null | wc -l)
nok=$(grep -cE '^[a-z0-9_]+\.elf: OK, [1-4] segments, [0-9]+ pages, entry 0x400[0-9a-f]{3}$' <<< "$EC")
if [ "$nprog" -eq 26 ] && [ "$nok" -eq 26 ]; then pass "[0.11] elfcheck: all 26 built programs accepted"
else fail "[0.11] elfcheck: $nok of $nprog built programs accepted: $(grep -v ': OK' <<< "$EC" | head -3)"; fi
expect_line=$(grep -E '^hello\.elf: ' <<< "$EC")
echo "      $expect_line"
if [ "$expect_line" = "hello.elf: OK, 2 segments, 3 pages, entry 0x400000" ]; then
    pass "[0.11] elfcheck: hello's load plan exact (2 segments, 3 pages, entry 0x400000)"
else
    fail "[0.11] elfcheck: hello's plan: ${expect_line:-none}"
fi
rm -rf "$FIX"
if python3 "$(dirname "$0")/elf-fixtures.py" "$OUT/user/hello.elf" "$FIX" && [ "$(wc -l < "$FIX/manifest.txt")" -eq 30 ]; then
    pass "[0.11] elf-fixtures.py: 30 fixtures made from hello.elf (29 malformed, 1 unchanged)"
else
    fail "[0.11] elf-fixtures.py: fixtures not made"
fi
while IFS=$'\t' read -r name want; do
    got=$("$ELFCHECK" "$FIX/$name.elf" 2>&1)
    if [ "$want" = "OK" ]; then
        ok=0; grep -qE "^$name\.elf: OK, " <<< "$got" && ok=1
    else
        ok=0; [ "$got" = "$name.elf: refused: $want" ] && ok=1
    fi
    if [ "$ok" -eq 1 ]; then pass "[0.11] elfcheck fixture $name: ${want}"
    else fail "[0.11] elfcheck fixture $name: got '${got}', expected '${want}'"; fi
done < "$FIX/manifest.txt"

echo "== [0.11] Program modules found at boot (QEMU -initrd; nothing loaded yet)"
# Every boot module that is not a test module is a program candidate: named
# from its command line, checked by elf_core, kept (at most 8) or refused,
# one line each on serial and in dmesg, never on the screen.
PROGS7="hello spin exitcode nullptr sleep getpid yield"
PM=""
for p in $PROGS7; do PM+="${PM:+,}$OUT/user/$p.elf"; done
LOG=$OUT/serial-progmod.log
boot_text "$LOG" 60 "System ready." "x{ret}dmesg 9999{ret}echo end-of-progmod-test{ret}" 0.05 \
    $'\nend-of-progmod-test\n' -m 32M -kernel "$KERNEL" -initrd "$PM"
grep -E '^Program module' "$LOG" | head -8 | sed 's/^/      /'
n=1
for p in $PROGS7; do
    size=$(stat -c%s "$OUT/user/$p.elf")
    hp=$("$ELFCHECK" "$OUT/user/$p.elf" | sed -nE 's/^[a-z0-9_]+\.elf: OK, ([0-9]+) segments, ([0-9]+) pages, entry (0x[0-9a-f]+)$/\1 segments, \2 pages, entry \3/p')
    if [ -n "$hp" ] && grep -qxF "Program module $n '$p': $size bytes, ELF checked: $hp" "$LOG"; then
        pass "[0.11] program module $n '$p' accepted ($size bytes), its plan the same as the host elfcheck's ($hp)"
    else
        fail "[0.11] program module $n '$p': no line 'Program module $n '$p': $size bytes, ELF checked: ${hp:-?}'"
    fi
    n=$((n + 1))
done
expect "$LOG" "^Program modules: 7 accepted, 0 refused \(at most 8\)$" "[0.11] program modules: 7 accepted, 0 refused"
reject "$LOG" "^Boot module [0-9]"                   "[0.11] program modules: no 'Boot module' line (they are not test modules)"
expect_in_order "$LOG" "[0.11] program modules: the lines are in dmesg" \
    "^madire> dmesg 9999$" "^Program module 1 'hello': " "^Program module 7 'yield': " "^Program modules: 7 accepted"
reject "$LOG" "PANIC"                                "[0.11] program modules: no panic"
expect "$LOG" "System ready\."                       "[0.11] program modules: System ready"

# The screen's message line (row 23) shows nothing about program modules.
LOG=$OUT/serial-progmod-screen.log
SOCK=$OUT/qmp.sock
rm -f "$LOG" "$SOCK"
timeout 40 qemu-system-x86_64 -display none -no-reboot -serial file:"$LOG" \
    -qmp unix:"$SOCK",server=on,wait=off -m 32M -kernel "$KERNEL" -initrd "$PM" >/dev/null 2>&1 &
QPID=$!
for i in $(seq 60); do grep -q "System ready\." "$LOG" 2>/dev/null && break; sleep 0.5; done
if python3 "$QMPSCREEN" "$SOCK" "$OUT/screen-progmod.bin" --log "$LOG" --wait '^System ready\.' 1 \
       --not-row 23 'Program module|Boot module|ELF' > "$OUT/screen-progmod.txt" 2>&1 &&
   grep -q "^Program modules: 7 accepted" "$LOG"; then
    pass "[0.11] program modules: nothing about them on the screen (row 23 read from the VGA buffer)"
else
    fail "[0.11] program modules: screen check failed (see $OUT/screen-progmod.txt)"
fi
kill "$QPID" 2>/dev/null; wait "$QPID" 2>/dev/null

# The 30 fixtures as modules, 10 per boot (each module takes 2 of the 32
# reserved ranges): each refused with its exact reason, the kernel going on.
mapfile -t FXN < <(cut -f1 "$FIX/manifest.txt")
mapfile -t FXW < <(cut -f2 "$FIX/manifest.txt")
for batch in 0 1 2; do
    PM=""
    for k in $(seq 0 9); do PM+="${PM:+,}$FIX/${FXN[$((batch * 10 + k))]}.elf"; done
    LOG=$OUT/serial-progmod-fixtures-$batch.log
    boot "$LOG" 20 -m 32M -kernel "$KERNEL" -initrd "$PM"
    acc=0; ref=0
    for k in $(seq 0 9); do
        name=${FXN[$((batch * 10 + k))]}; want=${FXW[$((batch * 10 + k))]}; n=$((k + 1))
        size=$(stat -c%s "$FIX/$name.elf")
        if [ "$want" = "OK" ]; then
            acc=$((acc + 1)); line="Program module $n '$name': $size bytes, ELF checked: "
            got=$(grep -F "$line" "$LOG" | head -1)
        else
            ref=$((ref + 1)); line="Program module $n '$name': $size bytes, refused: $want"
            got=$(grep -xF "$line" "$LOG" | head -1)
        fi
        if [ -n "$got" ]; then pass "[0.11] fixture module $name: ${want}"
        else fail "[0.11] fixture module $name: no line '$line'"; fi
    done
    if grep -qxF "Program modules: $acc accepted, $ref refused (at most 8)" "$LOG" &&
       grep -q "System ready\." "$LOG" && ! grep -q "PANIC" "$LOG"; then
        pass "[0.11] fixture modules, boot $((batch + 1)) of 3: $acc accepted, $ref refused; System ready, no panic"
    else
        fail "[0.11] fixture modules, boot $((batch + 1)) of 3: summary, System ready or no-panic missing"
    fi
done

# Names: a duplicate and three bad names refused; the rest kept.
LOG=$OUT/serial-progmod-names.log
U=$OUT/user
boot "$LOG" 20 -m 32M -kernel "$KERNEL" \
    -initrd "$U/hello.elf,$U/spin.elf hello,$U/spin.elf Bad-Name,$U/spin.elf abcdefghijklmnop,$U/spin.elf one two,$U/getpid.elf"
grep -E '^Program module' "$LOG" | sed 's/^/      /'
expect "$LOG" "^Program module 1 'hello': [0-9]+ bytes, ELF checked: " "[0.11] names: 'hello' from the path accepted"
expect "$LOG" "^Program module 2 'hello': [0-9]+ bytes, refused: duplicate name$" "[0.11] names: a second 'hello' (second word) refused as a duplicate"
expect "$LOG" "^Program module 3: refused: bad name \('.*Bad-Name'\)$" "[0.11] names: 'Bad-Name' (capitals, '-') refused"
expect "$LOG" "^Program module 4: refused: bad name \('.*'\)$" "[0.11] names: a 16-character name refused"
expect "$LOG" "^Program module 5: refused: bad name \('.*one two'\)$" "[0.11] names: three words refused"
expect "$LOG" "^Program module 6 'getpid': [0-9]+ bytes, ELF checked: " "[0.11] names: 'getpid' after the refusals accepted"
expect "$LOG" "^Program modules: 2 accepted, 4 refused \(at most 8\)$" "[0.11] names: 2 accepted, 4 refused"

# The most modules a boot can take: 32 reserved ranges, of which a -kernel
# boot without modules uses BASE; each module adds 2 (itself and its name)
# and the module table 1. 13 programs: 8 kept, 5 refused (at most 8); one
# module more no longer fits the reservation table (the protected
# reserve.c refuses it at boot: a documented limit, unchanged).
BASE=$(sed -nE 's/^Memory initialization: .*\(([0-9]+) reserved ranges.*/\1/p' "$OUT/serial-mem-32M.log" | head -1)
MAXMOD=$(( (32 - ${BASE:-32} - 1) / 2 ))
echo "      reserved ranges without modules: ${BASE:-?}; modules that fit: $MAXMOD"
ALL13="badptr enosys exitcode fpu getpid hello int80 isolate kread kwrite nullptr privileged sleep"
PM=""
for p in $(echo $ALL13 | cut -d' ' -f1-"$MAXMOD"); do PM+="${PM:+,}$U/$p.elf"; done
LOG=$OUT/serial-progmod-max.log
boot "$LOG" 20 -m 32M -kernel "$KERNEL" -initrd "$PM"
RR=$(sed -nE 's/^Memory initialization: .*\(([0-9]+) reserved ranges.*/\1/p' "$LOG" | head -1)
echo "      $MAXMOD modules: ${RR:-?} reserved ranges (limit 32)"
if [ "$MAXMOD" -eq 13 ] && [ -n "$RR" ] && [ "$RR" -eq $(( BASE + 1 + 2 * MAXMOD )) ] && [ "$RR" -le 32 ]; then
    pass "[0.11] most modules: 13 modules take $RR of the 32 reserved ranges ($BASE + 1 + 2 x 13)"
else
    fail "[0.11] most modules: $MAXMOD modules, ${RR:-?} reserved ranges (base ${BASE:-?})"
fi
na=$(grep -cE "^Program module [0-9]+ '[a-z0-9_]+': [0-9]+ bytes, ELF checked: " "$LOG")
nf=$(grep -cE "^Program module (9|1[0-3]) '[a-z0-9_]+': [0-9]+ bytes, refused: more than 8 program modules$" "$LOG")
if [ "$na" -eq 8 ] && [ "$nf" -eq 5 ] && grep -q "^Program modules: 8 accepted, 5 refused (at most 8)$" "$LOG" &&
   grep -q "System ready\." "$LOG"; then
    pass "[0.11] most modules: the first 8 programs kept, modules 9-13 refused (more than 8 program modules); System ready"
else
    fail "[0.11] most modules: $na kept, $nf refused as over the limit"
fi
LOG=$OUT/serial-progmod-over.log
boot "$LOG" 15 -m 32M -kernel "$KERNEL" -initrd "$PM,$U/stackover.elf"
if grep -q "Memory: cannot reserve kernel and boot data: too many reserved ranges\." "$LOG" &&
   ! grep -q "System ready" "$LOG"; then
    pass "[0.11] most modules: a 14th module exceeds the 32 reserved ranges and is stopped at boot (documented limit)"
else
    fail "[0.11] most modules: 14 modules did not stop at the reservation limit"
fi

echo "== [0.11] The default boot entry loads 7 programs (the real ISO)"
# The default entry's program modules (0.13: limine.conf's "module_path:
# boot():/boot/prog/<name>.elf" lines, each with "module_string: <name>");
# make iso copies exactly those files. The normal-boot logs (the real ISO
# and the keylog test ISO) must show all 7 accepted, nothing refused.
ISOPROGS="hello spin exitcode nullptr sleep getpid lines"
CFGPROGS=$(awk '/^ *module_path: boot\(\):\/boot\/prog\/[a-z0-9_]+\.elf$/ { p = $2; sub(/.*\//, "", p); sub(/\.elf$/, "", p); next }
                /^ *module_string: / && p != "" { if ($2 == p) printf "%s%s", (n++ ? " " : ""), p; p = "" }' "$OUT/limine.conf")
ISOFILES=$(ls "$OUT/isodir/boot/prog" 2>/dev/null | sed 's/\.elf$//' | sort | tr '\n' ' ' | sed 's/ $//')
if [ "$CFGPROGS" = "$ISOPROGS" ] && [ "$ISOFILES" = "$(tr ' ' '\n' <<< "$ISOPROGS" | sort | tr '\n' ' ' | sed 's/ $//')" ]; then
    pass "[0.11] ISO: the boot menu loads the 7 programs ($CFGPROGS) and the ISO holds exactly those files"
else
    fail "[0.11] ISO: limine.conf modules '$CFGPROGS', ISO files '$ISOFILES'"
fi
LOG=$OUT/serial-boot.log
grep -E '^Program module|^Memory initialization|^Free pages|^Paging tables|walk-check: PASS|Paging self-test: PASS' "$LOG" | sed 's/^/      /'
n=1
for p in $ISOPROGS; do
    size=$(stat -c%s "$OUT/user/$p.elf")
    expect "$LOG" "^Program module $n '$p': $size bytes, ELF checked: [1-4] segments, [0-9]+ pages, entry 0x400000$" \
                                                  "[0.11] normal boot: module $n '$p' accepted ($size bytes)"
    n=$((n + 1))
done
expect "$LOG" "^Program modules: 7 accepted, 0 refused \(at most 8\)$" "[0.11] normal boot: 7 program modules accepted, none refused"
expect "$KLOG" "^Program modules: 7 accepted, 0 refused \(at most 8\)$" "[0.11] keylog test ISO: the same 7 program modules accepted"
RR=$(sed -nE 's/^Memory initialization: .*\(([0-9]+) reserved ranges.*/\1/p' "$LOG" | head -1)
if [ "${RR:-0}" -eq $(( 5 + 1 + 2 * 7 )) ]; then
    pass "[0.11] normal boot: 20 reserved ranges (5 without modules + the module table + 2 x 7)"
else
    fail "[0.11] normal boot: ${RR:-?} reserved ranges, expected 20"
fi
LOG=$OUT/serial-iso-screen.log
SOCK=$OUT/qmp.sock
rm -f "$LOG" "$SOCK"
timeout 40 qemu-system-x86_64 -display none -no-reboot -serial file:"$LOG" \
    -qmp unix:"$SOCK",server=on,wait=off -cdrom "$ISO" >/dev/null 2>&1 &
QPID=$!
for i in $(seq 80); do grep -q "System ready\." "$LOG" 2>/dev/null && break; sleep 0.5; done
if python3 "$QMPSCREEN" "$SOCK" "$OUT/screen-iso.bin" --log "$LOG" --wait '^System ready\.' 1 \
       --not-row 23 'Program module|Boot module|ELF' > "$OUT/screen-iso.txt" 2>&1 &&
   grep -q "^Program modules: 7 accepted" "$LOG"; then
    pass "[0.11] normal boot: no module line on the screen (row 23 read from the VGA buffer)"
else
    fail "[0.11] normal boot: screen check failed (see $OUT/screen-iso.txt)"
fi
kill "$QPID" 2>/dev/null; wait "$QPID" 2>/dev/null

echo "== [0.11] ELF programs loaded into processes (test=elfrun)"
# One loading path: built-in images and ELF modules both become load plans.
# Each module program with a built-in twin must give the twin's result; two
# isolate instances run at once; no user page is a module page; the modules
# are unchanged afterwards; every fixture is refused again at load.
ER8="hello spin exitcode nullptr getpid sleep isolate lines"
PM=""
for p in $ER8; do PM+="${PM:+,}$OUT/user/$p.elf"; done
for cpu in default smep; do
    LOG=$OUT/serial-elfrun.log
    CPUARG=()
    label="elfrun"
    if [ "$cpu" = "smep" ]; then
        LOG=$OUT/serial-elfrun-smap.log
        CPUARG=(-cpu qemu64,+smep,+smap)
        label="elfrun with SMEP and SMAP on"
    fi
    boot "$LOG" 60 -m 32M "${CPUARG[@]}" -kernel "$KERNEL" -initrd "$PM" -append "test=elfrun"
    grep -E "^ELF run test: " "$LOG" | grep -vE ': (PASS|FAIL)$|against their built-in twins\.\.\.$' | sed 's/^/      /'
    if [ "$cpu" = "default" ]; then
        for n in hello exitcode getpid sleep spin; do
            expect "$LOG" "^ELF run test: $n: built-in process [0-9]+ [a-z ]+ \(-?[0-9]+\); module process [0-9]+ [a-z ]+ \(-?[0-9]+\); ring-3 returns [0-9]+, [0-9]+: OK$" \
                                              "[0.11] elfrun: '$n' from its module gives the same result as the built-in image"
        done
        expect "$LOG" "^ELF run test: nullptr: built-in process [0-9]+ killed by a CPU exception \(14\) at RIP (0x[0-9a-f]{16}); module process [0-9]+ killed by a CPU exception \(14\) at RIP \1, the same instruction: OK$" \
                                              "[0.11] elfrun: 'nullptr' from its module faults at the same instruction as the built-in image (#PF at address 0)"
        HL=$(grep -m1 -E '^ELF run test: hello: ' "$LOG")
        P1=$(sed -nE 's/.*built-in process ([0-9]+) .*/\1/p' <<< "$HL"); P2=$(sed -nE 's/.*module process ([0-9]+) .*/\1/p' <<< "$HL")
        if [ -n "$P1" ] && [ -n "$P2" ] && grep -qx "\[pid $P1\] Hello from user mode" "$LOG" &&
           grep -qx "\[pid $P2\] Hello from user mode" "$LOG"; then
            pass "[0.11] elfrun: hello's output the same from both (processes $P1 and $P2)"
        else
            fail "[0.11] elfrun: hello's output missing (processes ${P1:-?} and ${P2:-?})"
        fi
        expect "$LOG" "^ELF run test: 6 module programs compared with their built-in twins$" \
                                              "[0.11] elfrun: all 6 twins compared"
        expect "$LOG" "^ELF run test: isolate 1 and isolate 2 \(module, same addresses, at once\): processes [0-9]+ and [0-9]+ exited \(0\) and exited \(0\), each saw only its own data: OK$" \
                                              "[0.11] elfrun: two instances of one module program isolated"
        expect "$LOG" "^ELF run test: [1-9][0-9]* loads, [1-9][0-9]* pages mapped, 0 of them inside a boot module: OK$" \
                                              "[0.11] elfrun: every user page a fresh frame, none inside a boot module"
        expect "$LOG" "^ELF run test: checksums of 8 modules after the programs ran and wrote their data: 0 changed: OK$" \
                                              "[0.11] elfrun: all 8 modules unchanged after the programs ran"
        expect "$LOG" "^ELF run test: cleanup: free pages ([0-9]+) -> \1, kernel page tables ([0-9]+) -> \2, stack slots ([0-9]+) -> \3, live threads ([0-9]+) -> \4, processes left 0: OK$" \
                                              "[0.11] elfrun: cleanup exact"
    fi
    expect "$LOG" "^ELF run test: longest turn taken by thread [0-9]+ '[^']+' \(user process [0-9]+\): [0-6] ticks$" \
                                              "[0.11] $label: longest turn a user process's, at most 6 ticks (owner shown)"
    expect "$LOG" "^ELF run test: PASS$"     "[0.11] $label: PASS"
    reject "$LOG" "PANIC"                    "[0.11] $label: no panic"
done

# The fixtures as modules once more, now with test=elfrun: every file refused
# at boot is refused again at load, for the same reason, and never gets an
# address space (so never reaches ring 3); the kernel goes on.
for batch in 0 1 2; do
    PM=""
    for k in $(seq 0 9); do PM+="${PM:+,}$FIX/${FXN[$((batch * 10 + k))]}.elf"; done
    LOG=$OUT/serial-elfrun-fixtures-$batch.log
    boot "$LOG" 30 -m 32M -kernel "$KERNEL" -initrd "$PM" -append "test=elfrun"
    nref=0
    for k in $(seq 0 9); do
        name=${FXN[$((batch * 10 + k))]}; want=${FXW[$((batch * 10 + k))]}
        [ "$want" = "OK" ] && continue
        nref=$((nref + 1))
        if grep -qxF "ELF run test: refused file '$name' (module $((k + 1))): process $(grep -m1 -oE "^ELF run test: refused file '$name' \(module [0-9]+\): process [0-9]+" "$LOG" | grep -oE '[0-9]+$') load failed: $want (at boot: $want): OK" "$LOG" &&
           grep -qE "^Process [0-9]+ '$name': load failed: " "$LOG"; then
            pass "[0.11] elfrun fixture $name: refused again at load ($want)"
        else
            fail "[0.11] elfrun fixture $name: not refused again at load with '$want'"
        fi
    done
    expect "$LOG" "^ELF run test: $nref refused files refused again at load, 0 address spaces built for them: OK$" \
                                              "[0.11] elfrun fixtures, boot $((batch + 1)) of 3: $nref refused again, no address space built"
    expect "$LOG" "^ELF run test: PASS$"     "[0.11] elfrun fixtures, boot $((batch + 1)) of 3: PASS"
    reject "$LOG" "PANIC"                    "[0.11] elfrun fixtures, boot $((batch + 1)) of 3: no panic"
done

echo "== [0.11] Policy (b): preempt_enable switches at once, turns stay within 6 ticks"
# turn_bound <label> <log> <line prefix>: the "longest turn taken by" line
# names the thread, and its turn is at most 6 ticks (a slice of 5 plus the
# rest of one guarded section). Policy (b) rule: a turn over 6 ticks is a
# STOP, never a relaxed bound.
turn_bound() {
    local ow t
    ow=$(grep -E "^$3: longest turn taken by " "$2" | tail -1)
    echo "      ${ow:-no owner line}"
    t=$(sed -nE "s/^$3: longest turn taken by thread [0-9]+ '[^']+' \((kernel thread|user process [0-9]+)\): ([0-9]+) ticks$/\2/p" <<< "$ow")
    if [ -n "$t" ] && [ "$t" -le 6 ]; then
        pass "[0.11] $1: longest turn $t ticks (at most 6), its owner named"
    else
        fail "[0.11] $1: longest turn ${t:-?} ticks or no owner (${ow:-none}); over 6 is a STOP"
    fi
}
# Logs of the [0.9] and [0.10] groups above, read again.
turn_bound framethreads "$OUT/serial-framethreads.log" "Frame threads test"
turn_bound heapthreads "$OUT/serial-heapthreads.log" "Heap threads test"
LOG=$OUT/serial-userpreempt.log
kf=$(grep -E "^User preemption test: kernel frame thread: longest turn " "$LOG")
echo "      ${kf:-no kernel frame thread line}"
t=$(sed -nE "s/^User preemption test: kernel frame thread: longest turn ([0-9]+) ticks \(thread [0-9]+ 'frames'; policy \(b\): .*: OK$/\1/p" <<< "$kf")
if [ -n "$t" ] && [ "$t" -le 6 ]; then
    pass "[0.11] userpreempt: the kernel frame thread's longest turn $t ticks (at most 6), the thread named"
else
    fail "[0.11] userpreempt: the kernel frame thread's longest turn ${t:-?} ticks or not named; over 6 is a STOP"
fi
LOG=$OUT/serial-preemptoff.log
# 0.11 stage 4a (test-only change): 0 or 1 tick after the release, and the
# switch recorded at the release (by preempt_enable, or by the timer
# interrupt right after it, as a preemption). A tick that lands at the
# release must not fail a correct kernel; the strict proof of policy (b)
# is the next check.
po=$(grep -E "^Preemptoff test: switch at the release: by " "$LOG")
echo "      ${po:-no switch-at-release line}"
pa=$(sed -nE "s/^Preemptoff test: switch at the release: by (preempt_enable|the timer interrupt right after it); preemptions ([0-9]+) -> ([0-9]+)$/\2 \3/p" <<< "$po")
if grep -qE "^Preemptoff test: switch within one tick after the release: released at tick [0-9]+, other thread ran at tick [0-9]+: [01] tick\(s\) later \(at most 1\): OK$" "$LOG" &&
   [ -n "$pa" ] && [ "${pa#* }" -gt "${pa% *}" ]; then
    pass "[0.11] preemptoff: the other thread runs 0 or 1 tick after the release, the switch recorded at the release"
else
    fail "[0.11] preemptoff: no switch recorded at the release within 1 tick (${po:-none})"
fi
expect "$LOG" "^Preemptoff test: switch inside preempt_enable at the release \(policy \(b\)\): yes: OK$" \
                                              "[0.11] preemptoff: the switch happens inside the releasing preempt_enable"
po=$(grep -E "^Preemptoff test: thread [0-9]+ 'other': longest turn " "$LOG")
echo "      ${po:-no per-thread line}"
t=$(sed -nE "s/^Preemptoff test: thread [0-9]+ 'other': longest turn ([0-9]+) ticks; .*/\1/p" <<< "$po")
if [ -n "$t" ] && [ "$t" -le 6 ]; then pass "[0.11] preemptoff: the other thread's longest turn $t ticks (at most 6)"
else fail "[0.11] preemptoff: the other thread's longest turn ${t:-?} ticks; over 6 is a STOP"; fi
# The holder's turn is the test's own guarded section: exempt, by name.
ow=$(grep -E "^Preemptoff test: longest turn taken by " "$LOG")
t=$(sed -nE "s/^Preemptoff test: longest turn taken by thread [0-9]+ 'holder' \(kernel thread\): ([0-9]+) ticks$/\1/p" <<< "$ow")
if [ -n "$t" ] && [ "$t" -ge 20 ] && [ "$t" -le 21 ]; then
    pass "[0.11] preemptoff holder: deliberate 20-tick hold, exempt (its turn $t ticks, the test's own guarded section)"
else
    fail "[0.11] preemptoff holder: the longest turn is not the holder's 20-tick hold (${ow:-none})"
fi

echo "== [0.11] Threads that live in guarded sections (test=preemptlong)"
thread_diag preemptlong 15
grep -E "^Preemptlong test: " "$LOG" | grep -vE ': (PASS|FAIL)$|for 3 seconds\.\.\.$' | sed 's/^/      /'
expect "$LOG" "^Preemptlong test: create: 4 threads that keep preemption disabled until each next tick: OK$" \
                                              "[0.11] preemptlong: 4 threads created"
for i in 0 1 2 3; do
    expect "$LOG" "^Preemptlong test: long$i: thread [0-9]+: (9[5-9]|100)\.[0-9]% in guarded sections \([0-9]+\), ([5-9]|[1-9][0-9]+) turns, longest turn [0-6] ticks \(at most 6\): OK$" \
                                              "[0.11] preemptlong: long$i over 95% in guarded sections, at least 5 turns, longest turn at most 6 ticks"
done
expect "$LOG" "^Preemptlong test: everything returned: free pages ([0-9]+) -> \1, .*: OK$" \
                                              "[0.11] preemptlong: everything returned"
turn_bound preemptlong "$LOG" "Preemptlong test"
expect "$LOG" "^Preemptlong test: PASS \("   "[0.11] preemptlong: PASS"
reject "$LOG" "PANIC|MISMATCH"               "[0.11] preemptlong: no panic, no mismatch"
expect "$LOG" "System ready\."               "[0.11] preemptlong: System ready afterwards"

echo "== [0.11] Console: run, run -b and ps (a console session through QMP)"
# Four program modules; the console waits for a foreground program (keys
# are ignored meanwhile, hence the pauses) and collects it, then prompts.
LOG=$OUT/serial-run.log
PM=""
for p in hello exitcode nullptr spin; do PM+="${PM:+,}$OUT/user/$p.elf"; done
S="x{ret}"
S+="run hello{ret}{wait:1}"
S+="run exitcode{ret}{wait:1}"
S+="run nullptr{ret}{wait:1}"
S+="run -b spin{ret}{wait:1}"
S+="ps{ret}"
S+="run nosuch{ret}"
S+="run hello x{ret}"
S+="echo end-of-run-test{ret}"
boot_text "$LOG" 90 "System ready." "$S" 0.05 $'\nend-of-run-test\n' -m 32M -kernel "$KERNEL" -initrd "$PM"
grep -E "^madire> (run|ps)|^\[process |^run: |^PID |^ *[0-9]+  (running|ended) |^[0-9]+ process|Hello from user mode|'nullptr' ended by" "$LOG" | sed 's/^/      /'
expect_in_order "$LOG" "[0.11] run hello: its line in the log, then 'exited, status 0', then the prompt" \
    "^madire> run hello$" "^\[pid [0-9]+\] Hello from user mode$" "^\[process [0-9]+ 'hello' exited, status 0\]$" \
    "^madire> run exitcode$"
expect_in_order "$LOG" "[0.11] run exitcode: 'exited, status 42'" \
    "^madire> run exitcode$" "^\[process [0-9]+ 'exitcode' exited, status 42\]$" "^madire> run nullptr$"
NP=$(sed -nE "s/^\[process ([0-9]+) 'nullptr' ended by #PF at (0x[0-9a-f]{16})\]$/\1 \2/p" "$LOG")
KA=$(sed -nE "s/^Process ${NP% *} 'nullptr' ended by CPU exception 14 \(#PF Page Fault\) at RIP 0x[0-9a-f]+: address (0x[0-9a-f]{16}),.*/\1/p" "$LOG")
if [ -n "$NP" ] && [ "${NP#* }" = "0x0000000000000000" ] && [ "$KA" = "${NP#* }" ]; then
    pass "[0.11] run nullptr: 'ended by #PF at 0x0000000000000000', the address the kernel logged"
else
    fail "[0.11] run nullptr: end line '${NP:-none}', kernel address '${KA:-none}'"
fi
BP=$(sed -nE "s/^\[process ([0-9]+) 'spin' started in the background\]$/\1/p" "$LOG")
expect_in_order "$LOG" "[0.11] run -b spin: started in the background, the prompt at once" \
    "^madire> run -b spin$" "^\[process [0-9]+ 'spin' started in the background\]$" "^madire> ps$"
expect_in_order "$LOG" "[0.11] ps: the thread table as before, then the processes with PID, state and name" \
    "^madire> ps$" "^TID  STATE      TICKS    STACK PEAK    NAME$" "^  0  running +[0-9]+ +[0-9]+/16384   kernel$" \
    "^[0-9]+ threads?$" "^PID  STATE    TID  NAME$" "^ *${BP:-x}  running +[0-9]+  spin$" "^1 process$" \
    "^madire> run nosuch$"
expect_next "$LOG" "[0.11] run nosuch: refused, no program of that name" \
    "^madire> run nosuch$" "^run: no program named 'nosuch'\.$"
expect_next "$LOG" "[0.11] run hello x: a bad argument refused" \
    "^madire> run hello x$" "^run: 'x' is not a number from 0 to 4294967295\.$"
# (after the first "run hello": the boot self-test runs hello once too)
n=$(sed -n '/^madire> run hello$/,$p' "$LOG" | grep -cE "^\[pid [0-9]+\] Hello from user mode$")
if [ "$n" -eq 1 ]; then pass "[0.11] run: hello ran exactly once (the refused lines started nothing)"
else fail "[0.11] run: hello's line $n times, expected 1"; fi
# The console's own lines (the serial log also holds kernel log lines,
# which never reach the screen).
w=$(grep -E "^madire> |^\[process |^run: |^PID |^TID |^ *[0-9]+  |^[0-9]+ (threads?|process(es)?)$|^end-of-run-test$" "$LOG" |
    awk '{ if (length($0) > m) m = length($0) } END { print m + 0 }')
if grep -q "^end-of-run-test$" "$LOG" && [ "$w" -le 79 ]; then pass "[0.11] run session: every line at most 79 characters (longest $w)"
else fail "[0.11] run session: a line of $w characters (limit 79), or the session did not end"; fi
reject "$LOG" "^key: "                             "[0.11] run session: no key events logged (keylog off)"
reject "$LOG" "PANIC|Screen output from thread"    "[0.11] run session: no panic, no screen-rule violation"

echo "== [0.11] Console: kill (asks first) and Ctrl+C (a console session through QMP)"
# The boot self-test uses process IDs 1-3, so the first program is 4.
LOG=$OUT/serial-kill.log
PM=""
for p in spin sleep lines; do PM+="${PM:+,}$OUT/user/$p.elf"; done
S="x{ret}"
S+="mem{ret}"
S+="run -b spin{ret}{wait:1}"                        # process 4, in the background
S+="kill 4{ret}no{ret}"
S+="ps{ret}"
S+="kill 4{ret}yes{ret}{wait:1}"
S+="kill 0{ret}kill 99{ret}"
S+="run spin{ret}{wait:1}{ctrl+c}{wait:1}"           # process 5
S+="run sleep 2{ret}{wait:1}{ctrl+c}{wait:1}"        # process 6: a 10 s nanosleep
S+="run lines 65636{ret}{wait:1}{ctrl+c}{wait:1}"    # process 7: 100 lines, 100 ms apart
S+="ps{ret}mem{ret}"
S+="echo end-of-kill-test{ret}"
boot_text "$LOG" 120 "System ready." "$S" 0.05 $'\nend-of-kill-test\n' -m 32M -kernel "$KERNEL" -initrd "$PM"
grep -E "^madire> (run|kill|ps)|^yes/no> |^End process|^Kill cancelled|^kill: |^\[process |^console: (Ctrl\+C|kill confirmed|process [0-9]+ collected)|kill requested while asleep|^ *[0-9]+  (running|ended) |^[0-9]+ process|^Free pages \(now\)" "$LOG" | sed 's/^/      /'
expect "$LOG" "^\[process 4 'spin' started in the background\]$" \
                                              "[0.11] kill session: spin started in the background as process 4"
expect_next "$LOG" "[0.11] kill 4: asks first" \
    "^madire> kill 4$" "^End process 4 'spin'\? Type 'yes' to confirm:$"
expect_in_order "$LOG" "[0.11] kill 4, then 'no': cancelled, and ps still shows process 4 running" \
    "^madire> kill 4$" "^yes/no> no$" "^Kill cancelled\.$" "^madire> ps$" "^PID  STATE    TID  NAME$" \
    "^  4  running +[0-9]+  spin$" "^1 process$"
expect_in_order "$LOG" "[0.11] kill 4, then 'yes': kill confirmed, then its end line before the prompt" \
    "^madire> ps$" "^madire> kill 4$" "^yes/no> yes$" "^console: kill confirmed for process 4$" \
    "^\[process 4 'spin' killed\]$" "^madire> kill 0$"
expect_next "$LOG" "[0.11] kill 0: refused (the kernel, not a process)" \
    "^madire> kill 0$" "^kill: PID 0 is the kernel, not a process\.$"
expect_next "$LOG" "[0.11] kill 99: refused (no such process)" \
    "^madire> kill 99$" "^kill: no process with PID 99\.$"
expect_in_order "$LOG" "[0.11] Ctrl+C on spin: kill requested, then 'killed'" \
    "^madire> run spin$" "^console: Ctrl\+C: kill requested for process 5$" "^\[process 5 'spin' killed\]$" \
    "^madire> run sleep 2$"
expect_in_order "$LOG" "[0.11] Ctrl+C on sleep: its 10 s nanosleep woken early, then 'killed'" \
    "^madire> run sleep 2$" "^Process 6: kill requested while asleep; woken early$" \
    "^console: Ctrl\+C: kill requested for process 6$" "^\[process 6 'sleep' killed\]$"
expect_in_order "$LOG" "[0.11] Ctrl+C on lines: kill requested, then 'killed'" \
    "^madire> run lines 65636$" "^console: Ctrl\+C: kill requested for process 7$" "^\[process 7 'lines' killed\]$"
for p in 5 6 7; do
    t=$(sed -nE "s/^console: process $p collected ([0-9]+) ticks after Ctrl\+C$/\1/p" "$LOG")
    if [ -n "$t" ] && [ "$t" -le 2 ]; then pass "[0.11] Ctrl+C on process $p: the prompt back $t tick(s) later (at most 2)"
    else fail "[0.11] Ctrl+C on process $p: collected ${t:-?} ticks after Ctrl+C (at most 2)"; fi
done
n=$(grep -cE "^\[pid 7\] line [0-9]+ of 100$" "$LOG")
if [ "$n" -lt 100 ]; then pass "[0.11] Ctrl+C on lines: it stopped early ($n of 100 lines written)"
else fail "[0.11] Ctrl+C on lines: all 100 lines written (not stopped)"; fi
expect_in_order "$LOG" "[0.11] ps afterwards: no process left" \
    "^madire> ps$" "^madire> ps$" "^PID  STATE    TID  NAME$" "^0 processes$"
F=$(grep -E "^Free pages \(now\):" "$LOG" | awk '{print $NF}')
F1=$(sed -n 1p <<< "$F"); F2=$(sed -n 2p <<< "$F")
echo "      free pages before the first program ${F1:-?}, after the last ${F2:-?}"
if [ -n "$F1" ] && [ "$F1" = "$F2" ]; then pass "[0.11] kill session: free pages the same before and after (teardown exact)"
else fail "[0.11] kill session: free pages ${F1:-?} before, ${F2:-?} after"; fi
w=$(grep -E "^madire> |^yes/no> |^End process|^Kill cancelled|^kill: |^\[process |^PID |^TID |^ *[0-9]+  |^[0-9]+ (threads?|process(es)?)$|^end-of-kill-test$" "$LOG" |
    awk '{ if (length($0) > m) m = length($0) } END { print m + 0 }')
if grep -q "^end-of-kill-test$" "$LOG" && [ "$w" -le 79 ]; then pass "[0.11] kill session: every line at most 79 characters (longest $w)"
else fail "[0.11] kill session: a line of $w characters (limit 79), or the session did not end"; fi
reject "$LOG" "^key: "                             "[0.11] kill session: no key events logged (keylog off; Ctrl+C never logged as a key)"
reject "$LOG" "PANIC|Screen output from thread"    "[0.11] kill session: no panic, no screen-rule violation"

echo "== [0.11] Foreground program output on screen, scrollback and Q8 back-pressure"
LOG=$OUT/serial-program-output.log
SOCK=$OUT/qmp-program-output.sock
PM="$OUT/user/hello.elf,$OUT/user/lines.elf,$OUT/test-user/flood.elf"
rm -f "$LOG" "$SOCK" "$OUT"/screen-program-*.bin
timeout 240 qemu-system-x86_64 -display none -no-reboot -serial file:"$LOG" \
    -qmp unix:"$SOCK",server=on,wait=off -m 32M -kernel "$KERNEL" -initrd "$PM" \
    >/dev/null 2>&1 &
QPID=$!
program_text() {
    python3 "$QMPKEYS" "$SOCK" "$LOG" "System ready." --text "$1" --gap 0.05 \
        --done "$2" --done-timeout 90
}
program_keys() {
    python3 "$QMPKEYS" "$SOCK" "$LOG" "System ready." "$1" --gap 0.05 \
        --done "$2" --done-timeout 90
}
program_screen() {
    local name=$1 pattern=$2
    python3 "$QMPSCREEN" "$SOCK" "$OUT/$name.bin" --log "$LOG" --wait "$pattern" 1 \
        > "$OUT/$name.txt" 2>&1
}
screen_width_ok() {
    awk -F'|' '$1 ~ /^row [0-9][0-9]$/ { if (length($2) > 79) bad=1 } END { exit bad }' \
        "$OUT/$1.txt"
}

program_text "x{ret}run hello{ret}" "[process 4 'hello' exited, status 0]"
if program_screen screen-program-hello "^\\[process 4 'hello' exited, status 0\\]$" &&
   grep -qF "Hello from user mode" "$OUT/screen-program-hello.txt" &&
   screen_width_ok screen-program-hello; then
    pass "[0.11] run hello: qmp_screen sees its text on the VGA screen"
else
    fail "[0.11] run hello: text missing from the VGA screen (see $OUT/screen-program-hello.txt)"
fi
if [ "$(grep -cFx '[pid 4] Hello from user mode' "$LOG")" -eq 1 ] &&
   ! grep -qF '[pid 4]' "$OUT/screen-program-hello.txt"; then
    pass "[0.11] run hello: one unchanged [pid 4] serial line, no prefixed duplicate on screen"
else
    fail "[0.11] run hello: serial line missing/duplicated or [pid] prefix reached screen"
fi
program_text "dmesg 100{ret}echo dmesg-output-seen{ret}" "dmesg-output-seen"
expect_in_order "$LOG" "[0.11] run hello: its existing [pid 4] line remains in dmesg" \
    "^madire> dmesg 100$" "^\\[pid 4\\] Hello from user mode$" "^madire> echo dmesg-output-seen$"

program_text "clear{ret}run lines{ret}" "[process 5 'lines' exited, status 0]"
if program_screen screen-program-lines "^\\[process 5 'lines' exited, status 0\\]$" &&
   python3 - "$OUT/screen-program-lines.txt" <<'PY'
import re
import sys

rows = {}
for line in open(sys.argv[1], encoding="utf-8"):
    match = re.match(r"row (\d\d)\|(.*)\|$", line.rstrip("\n"))
    if match:
        rows[int(match.group(1))] = match.group(2)
numbered = {row for row in rows.values() if re.fullmatch(r"line (?:[1-9]|10) of 10", row)}
long_rows = [(row, text) for row, text in rows.items() if text.startswith("long: ")]
letters = "".join(chr(ord("a") + i % 26) for i in range(200))
expected = ["long: " + letters[:73], letters[73:152], letters[152:]]
long_ok = len(long_rows) == 1
if long_ok:
    first = long_rows[0][0]
    long_ok = [rows.get(first + i) for i in range(3)] == expected
controls = "ctl: tab?here bell? esc?[31mred cr?end del?"
ok = ("line 1 of 10" in numbered and "line 10 of 10" in numbered and long_ok and
      controls in rows.values() and max(map(len, rows.values()), default=0) <= 79)
print(f"screen rows: {len(rows)}, longest {max(map(len, rows.values()), default=0)} columns")
print(f"numbered lines: {len(numbered)}, exact 79-column wrapping: {long_ok}, controls: {controls in rows.values()}")
sys.exit(0 if ok else 1)
PY
then
    pass "[0.11] run lines: numbered text, exact 79-column wrap, controls as '?', all screen rows at most 79"
else
    fail "[0.11] run lines: numbered output, wrapping, controls or screen width failed (see $OUT/screen-program-lines.txt)"
fi
if [ "$(grep -cFx '[pid 5] line 1 of 10' "$LOG")" -eq 1 ] &&
   [ "$(grep -cFx '[pid 5] line 10 of 10' "$LOG")" -eq 1 ] &&
   [ "$(grep -cFx '[pid 5] ctl: tab?here bell? esc?[31mred cr?end del?' "$LOG")" -eq 1 ] &&
   [ "$(grep -cFx '[pid 5] lines: done' "$LOG")" -eq 1 ]; then
    pass "[0.11] run lines: serial [pid 5] records each appear once, with no doubled screen copy"
else
    fail "[0.11] run lines: serial records missing or duplicated"
fi

program_text "clear{ret}run -b hello{ret}" "[process 6 'hello' exited, status 0]"
if program_screen screen-program-background "^\\[process 6 'hello' exited, status 0\\]$" &&
   ! grep -qF "Hello from user mode" "$OUT/screen-program-background.txt" &&
   screen_width_ok screen-program-background; then
    pass "[0.11] run -b hello: background program payload never reaches the VGA screen"
else
    fail "[0.11] run -b hello: background output appeared on screen (see $OUT/screen-program-background.txt)"
fi
if [ "$(grep -cFx '[pid 6] Hello from user mode' "$LOG")" -eq 1 ]; then
    pass "[0.11] run -b hello: its serial [pid 6] line remains unchanged and appears once"
else
    fail "[0.11] run -b hello: serial output missing or duplicated"
fi

program_text "clear{ret}run lines 60{ret}" "[process 7 'lines' exited, status 0]"
program_screen screen-program-scroll-bottom "^\\[process 7 'lines' exited, status 0\\]$"
program_keys "shift+pgup" "console: scrolled back:"
if program_screen screen-program-scrollback "^console: scrolled back: " &&
   python3 - "$OUT/screen-program-scroll-bottom.txt" "$OUT/screen-program-scrollback.txt" <<'PY'
import re
import sys

def numbers(path):
    found = []
    for line in open(path, encoding="utf-8"):
        match = re.match(r"row \d\d\|line (\d+) of 60\|$", line.rstrip("\n"))
        if match:
            found.append(int(match.group(1)))
    return found

bottom, back = numbers(sys.argv[1]), numbers(sys.argv[2])
widths = []
for path in sys.argv[1:]:
    for line in open(path, encoding="utf-8"):
        match = re.match(r"row \d\d\|(.*)\|$", line.rstrip("\n"))
        if match:
            widths.append(len(match.group(1)))
ok = bool(bottom and back and min(back) < min(bottom) and max(widths, default=0) <= 79)
print(f"live lines start at {min(bottom) if bottom else 'none'}; scrolled view starts at {min(back) if back else 'none'}")
print(f"longest captured scrollback screen row: {max(widths, default=0)} columns")
sys.exit(0 if ok else 1)
PY
then
    pass "[0.11] Shift+PgUp shows earlier numbered program output from scrollback"
else
    fail "[0.11] Shift+PgUp did not show earlier program output (see $OUT/screen-program-scrollback.txt)"
fi

program_text "x{bs}run flood{ret}" "console: process 8 waiting for output queue"
program_text "{ctrl+c}" "[process 8 'flood' killed]"
expect_in_order "$LOG" "[0.11] Q8 FLOOD: queue filled, then Ctrl+C ended the waiting writer" \
    "^console: process 8 waiting for output queue$" "^console: Ctrl\\+C: kill requested for process 8$" \
    "^\\[process 8 'flood' killed\\]$"
if program_screen screen-program-flood "^\\[process 8 'flood' killed\\]$"; then
    if screen_width_ok screen-program-flood; then
        pass "[0.11] Q8 FLOOD: screen remains readable and every screen row is at most 79 columns"
    else
        fail "[0.11] Q8 FLOOD: a screen row exceeded 79 columns"
    fi
else
    fail "[0.11] Q8 FLOOD: no final screen capture (see $OUT/screen-program-flood.txt)"
fi
program_text "echo q8-console-responsive{ret}ps{ret}" "0 processes"
expect "$LOG" "^q8-console-responsive$" "[0.11] Q8 FLOOD: the prompt accepts another command after Ctrl+C"
expect_in_order "$LOG" "[0.11] Q8 FLOOD: ps shows no leftover process" "^madire> ps$" "^0 processes$"
reject "$LOG" "^key: " "[0.11] program output session: no key events logged"
reject "$LOG" "PANIC|Screen output from thread" "[0.11] program output session: no panic or screen-rule violation"
kill "$QPID" 2>/dev/null
wait "$QPID" 2>/dev/null

