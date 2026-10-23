# Madire OS QEMU tests: what more than one part uses (0.12, stage 0c).
# tests/qemu-tests.sh sources this file after its own helpers and before the
# parts. Apart from this header, the fixtures' comments, fixture_log, the
# first three lines and the closing brace of fixture_normal_boot and blank
# lines, every line below was moved unchanged from the single script.
# 0.13 stage 3a adds limine_iso and limine_tree, and builds the keylog test
# ISO with Limine instead of GRUB. 1.2 stage 7 adds the mouse sessions'
# helpers at the end (parts 1 and stress).

# limine_iso <dir> <iso> (0.13): makes a bootable ISO from <dir> as `make iso`
# does (xorriso, then limine bios-install). <dir> must hold boot/limine/ with
# limine-bios.sys, limine-bios-cd.bin and limine.conf. Fails if either step
# fails. Every test ISO is made with it.
LIMINE=${LIMINE:-limine}
LIMINE_DIR=${LIMINE_DIR:-/usr/local/share/limine}
limine_iso() {
    rm -f "$2"
    SOURCE_DATE_EPOCH=1791763200 xorriso -as mkisofs -R -r -J -b boot/limine/limine-bios-cd.bin \
        -no-emul-boot -boot-load-size 4 -boot-info-table --protective-msdos-label -partition_offset 16 \
        -o "$2" "$1" >/dev/null 2>&1 && "$LIMINE" bios-install "$2" >/dev/null 2>&1
}

# limine_tree <dir> (0.13): a fresh test-ISO tree holding the kernel under test
# and Limine's BIOS files (from the real ISO's tree); the caller writes
# <dir>/boot/limine/limine.conf. 1.1: also the boot menu's wallpaper, so
# every test ISO looks like the real one.
limine_tree() {
    rm -rf "$1" && mkdir -p "$1/boot/limine"
    cp "$KERNEL" "$1/boot/madire.kernel"
    cp "$OUT/isodir/boot/limine/limine-bios.sys" "$OUT/isodir/boot/limine/limine-bios-cd.bin" \
       "$OUT/isodir/boot/limine/wallpaper.png" "$1/boot/limine/"
}

# fixture_normal_boot: the normal boot of the real ISO (serial-boot.log; since
# 0.13 through Limine) and the boot of the keylog test ISO (KLOG), whose logs
# parts 1, 2 and 3 read. A full
# run boots them once, in part 1; a shard run of part 2 or 3 boots them
# itself. A boot is not a check: the checks on these logs stay in their
# sections and run once. (The body keeps its original indentation.)
fixture_normal_boot() {
[ -z "${FIXTURE_NORMAL_BOOT:-}" ] || return 0
FIXTURE_NORMAL_BOOT=done
LOG=$OUT/serial-boot.log
# 0.4: also type one key after boot, to prove input works in normal mode.
boot_keys "$LOG" 15 "System ready." "x" 0.15 -cdrom "$ISO"
# 0.9.2: key events are logged only with the boot option "keylog" (off by
# default). The checks below that read key lines use a test-only ISO: the
# same kernel and boot menu, with "keylog" added to the default entry only
# (0.13: limine.conf gains the line "cmdline: keylog" after the default
# entry's "path:" line; the ISO is made with Limine).
KLOG=$OUT/serial-boot-keylog.log
KISO=$OUT/madire-os-keylog.iso
limine_tree "$OUT/isokeylog"
cp -r "$OUT/isodir/boot/prog" "$OUT/isokeylog/boot/prog"   # 0.11: the modules the default entry loads
sed '0,/^\( *\)path: boot():\/boot\/madire\.kernel$/s//&\n\1cmdline: keylog/' \
    "$OUT/limine.conf" > "$OUT/isokeylog/boot/limine/limine.conf"
limine_iso "$OUT/isokeylog" "$KISO" || : > "$KISO"
boot_keys "$KLOG" 15 "System ready." "x" 0.15 -cdrom "$KISO"
}

# fixture_log <log> <seconds> <qemu args...>: a boot whose log a part reads
# but an earlier part makes (the same command as there). A full run made it
# already, so only a shard run boots it. A boot is not a check.
fixture_log() {
    [ "$SHARD" = all ] && return 0
    boot "$@"
}

# max_width <log> <start regex> <end regex>: longest line from the first
# line matching <start> to the next line matching <end> (both included).
max_width() {
    helper_trace width "$1" "$2" "$3"
    S="$2" E="$3" awk '
        !on && $0 ~ ENVIRON["S"] { on = 1 }
        on { if (length($0) > m) m = length($0) }
        on && $0 ~ ENVIRON["E"] && !($0 ~ ENVIRON["S"]) { on = 0 }
        END { print m + 0 }' "$1"
}

# count_lines <log> <regex> <expected count> <description>
count_lines() {
    local n
    n=$(grep -cE -- "$2" "$1")
    if [ "$n" -eq "$3" ]; then pass "$4"; else fail "$4 ($n lines, expected $3)"; fi
}

# owner_check <label> <line prefix regex> <ticks>: 0.11, the line "<prefix>:
# longest turn taken by ..." exists, names the thread (with its process, or
# "kernel thread", or "ring-3 thread, no process") and gives exactly the
# ticks the existing longest-turn line reported. The last such line in $LOG
# is the one checked (a diagnostic's own lines come after the boot's).
owner_check() {
    local ow
    ow=$(grep -E "^$2: longest turn taken by " "$LOG" | tail -1)
    echo "      ${ow:-no owner line}"
    if [ -n "$3" ] && { grep -qE ": longest turn taken by thread [0-9]+ '[^']+' \((kernel thread|user process [0-9]+|ring-3 thread, no process)\): $3 ticks$" <<< "$ow" ||
         { [ "$3" = 0 ] && grep -qE ": longest turn taken by no thread \(no turn of 1 tick or more\)$" <<< "$ow"; }; }; then
        pass "[0.11] $1: the longest turn's owner logged, with the $3 ticks reported"
    else
        fail "[0.11] $1: owner line missing or inconsistent (${ow:-none}; reported ${3:-?} ticks)"
    fi
}

# boot_until <log> <seconds> <line> <qemu args...>: like boot, but stops QEMU
# as soon as a line of the log is exactly <line> (the whole line, compared as
# text), so a deliberate halt need not wait for its time limit (0.12 stage
# 2b). If the line never comes, QEMU runs to its time limit as with boot, and
# the checks on the log fail.
boot_until() {
    local log=$1 secs=$2 line=$3 t0
    shift 3
    rm -f "$log"
    t0=$(date +%s%N)
    timeout "$secs" qemu-system-x86_64 -display none -no-reboot \
        -serial file:"$log" "$@" >/dev/null 2>&1 &
    local qpid=$!
    while kill -0 "$qpid" 2>/dev/null; do
        if [ -f "$log" ] && grep -qxF -- "$line" "$log"; then
            sleep 0.5                   # anything still in flight reaches the log
            kill "$qpid" 2>/dev/null
            break
        fi
        sleep 0.2
    done
    wait "$qpid" 2>/dev/null
    [ -f "$log" ] || : > "$log"
    boot_seconds "$log" "$t0"           # 1.0 stage 3: report only
}

# thread_diag <name> <seconds>: boots test=<name> into serial-<name>.log.
thread_diag() {
    LOG=$OUT/serial-$1.log
    boot "$LOG" "$2" -m 32M -kernel "$KERNEL" -append "test=$1"
}

# expect_lines <log> <description> <start regex> <regex>...: the lines right
# after the first line matching <start>, ignoring "key:" event lines, must
# match the regexes one by one. (The regexes go through the environment, so
# awk does not process their backslashes.)
expect_lines() {
    local log=$1 desc=$2 start=$3
    shift 3
    if START="$start" PATS="$(printf '%s\034' "$@")" awk '
            BEGIN { n = split(ENVIRON["PATS"], p, "\034") - 1 }
            on && !/^key: / { i++; if ($0 !~ p[i]) exit; if (i == n) { ok = 1; exit } }
            !on && $0 ~ ENVIRON["START"] { on = 1 }
            END { exit !ok }' "$log"; then
        pass "$desc"
    else
        fail "$desc (the lines after /$start/ in $log do not match)"
    fi
}

# ---- 1.0 stage 3: test=klock (the VFS lock under contention) ----

# klock_boot <log>: test=klock under QEMU -kernel -m 64M, with keys typed
# during the test over QMP at the pace proven in stage 0d (a 50 ms gap and
# a 20 ms hold, through qmp_keys.py's own --gap and --hold): "x", which
# opens the console once the test has ended (that key is not typed), then
# "echo klock-keys-ok" and Enter. All 20 keys (40 events) wait in the
# kernel's key queue (64 events) while thread 0 runs the test. QEMU stops
# once the echo's output is in the log, or at its time limit. Prints the
# injector's output and BOOT_SECONDS.
KLOCK_READY="(keys may be typed now)"
KLOCK_KEYS="xecho klock-keys-ok{ret}"
klock_boot() {
    local log=$1 sock=$OUT/qmp-klock.sock t0 out qpid
    rm -f "$log" "$sock"
    t0=$(date +%s%N)
    timeout 180 qemu-system-x86_64 -display none -no-reboot -serial file:"$log" \
        -qmp unix:"$sock",server=on,wait=off -m 64M -kernel "$KERNEL" -append "test=klock" \
        >/dev/null 2>&1 &
    qpid=$!
    out=$(python3 "$QMPKEYS" "$sock" "$log" "$KLOCK_READY" --text "$KLOCK_KEYS" \
          --gap 0.05 --hold 20 --done $'\nklock-keys-ok\n' --done-timeout 150 2>&1)
    echo "      $out"
    sleep 1
    kill "$qpid" 2>/dev/null
    wait "$qpid" 2>/dev/null
    [ -f "$log" ] || : > "$log"
    boot_seconds "$log" "$t0"
}

# The result lines of test=klock (kernel/fs/fstest.c), one regex each, in
# the order of the checks in part 2 (1-9; 10 is the keys; 1.0 stage 7b:
# KLOCK_RE_TICKS, a tick inside a hold never switches, and KLOCK_RE_PREEMPT
# changed from "0 at a release" to "1 at every release", the hold's own).
KLOCK_RE_CONTENTION="^Filesystem lock test: contention: 7 of 7 threads took the lock, each at least once after waiting; takes per thread [0-9]+-[0-9]+ \(mean [0-9]+, each at least 25% of it\); every worker took it while thread 0's loop ran \(fewest [1-9][0-9]*\); [0-9]+ takes, [0-9]+ after waiting: OK$"
KLOCK_RE_COUNTER="^Filesystem lock test: counter: ([1-9][0-9]*) increments made, the file holds \1: OK$"
KLOCK_RE_RECORDS="^Filesystem lock test: records: [1-9][0-9]* written, [1-9][0-9]* read across a page boundary \(4096 bytes at 2048\), 0 torn: OK$"
KLOCK_RE_HOLD="^Filesystem lock test: longest hold: [0-2] ticks \(at most 2\): OK$"
KLOCK_RE_TURN="^Filesystem lock test: longest turn: [0-6] ticks \(at most 6\), taken by (thread [0-9]+ 'klock[0-5]' \(kernel thread\): [0-6] ticks|no thread \(no turn of 1 tick or more\)): OK$"
KLOCK_RE_THREAD0="^Filesystem lock test: thread 0: longest wait [0-9]+ ticks, bound [0-9]+ \(at most [0-9]+ tickets ahead x \(longest hold [0-9]+ \+ 1\) \+ 1\); [1-9][0-9]* wait steps, all by yielding: OK$"
KLOCK_RE_SELFCHECK="^Filesystem lock test: thread-0 self-check: never fired; checked at each of thread 0's [1-9][0-9]* wait steps: OK$"
KLOCK_RE_TICKS="^Filesystem lock test: ticks inside holds: [1-9][0-9]* holds kept the lock across the next tick \(longest [0-9]+ ticks of their 4-tick limit, margin [0-9]+%\), 0 switched inside a hold: OK$"
KLOCK_RE_PREEMPT="^Filesystem lock test: preemption count: at every release 1 \(lowest 1, highest 1: the hold's own\), now 0 \(1 and 0 expected\): OK$"
KLOCK_RE_RETURNED="^Filesystem lock test: threads ([0-9]+) -> \1, stack slots ([0-9]+) -> \2, heap in use ([0-9]+) -> \3 bytes, the lock free, 0 errors: OK$"

# klock_keys_ok <log>: the keys typed during the test reached the console
# exactly: one "madire> echo klock-keys-ok" line, the next line (key event
# lines aside) exactly "klock-keys-ok", the test's "done" line, System
# ready, no dropped key event, no panic.
klock_keys_ok() {
    local log=$1
    grep -qxF "Filesystem lock test: done" "$log" && grep -qxF "System ready. Press any key to open the console." "$log" &&
        [ "$(grep -c '^madire> echo' "$log")" = 1 ] && ! grep -q "events dropped" "$log" &&
        ! grep -q "PANIC" "$log" &&
        [ "$(awk '/^key:/ { next } on { print; exit } $0 == "madire> echo klock-keys-ok" { on = 1 }' "$log")" = "klock-keys-ok" ]
}

# klock_figures <log>: "iterations hold turn wait0 takes_min takes_max" from
# the figures line.
klock_figures() {
    sed -nE 's/^Filesystem lock test: figures: iterations ([0-9]+), longest hold ([0-9]+), longest turn ([0-9]+), thread 0 longest wait ([0-9]+), takes per thread ([0-9]+)-([0-9]+)$/\1 \2 \3 \4 \5 \6/p' "$1" | head -n 1
}

# klock_bound_ok <log>: the stage 3 fix's bound, recomputed from thread 0's
# line: the bound is (tickets ahead) x (longest hold + 1) + 1 and the
# longest wait is at most it.
klock_bound_ok() {
    local w b a h
    read -r w b a h <<< "$(sed -nE 's/^Filesystem lock test: thread 0: longest wait ([0-9]+) ticks, bound ([0-9]+) \(at most ([0-9]+) tickets ahead x \(longest hold ([0-9]+) \+ 1\) \+ 1\);.*/\1 \2 \3 \4/p' "$1" | head -n 1)"
    [ -n "${h:-}" ] && [ "$b" -eq $((a * (h + 1) + 1)) ] && [ "$w" -le "$b" ]
}

# ---- 1.2 stage 7: the mouse sessions (parts 1 and stress) ----

# tests/qmp_mouse.py injects every packet over QMP (input-send-event) and
# compares each of the kernel's "mouse: packet" lines with the one it
# predicts (the motion, the buttons, the edges and, on the framebuffer, the
# clamped position); at "screen" it compares the screendump with
# tests/fbcon_ref.py's cell grid plus tests/pointer_ref.py's pointer (the
# sprite's rows from kernel/display/pointer_core.c, the colours from the
# Design Guide). QEMU's controller starts with configuration 0x61 (port 2's
# clock off; stage 3); VirtualBox's 0x45 is a unit-test case (test_mouse).
QMPMOUSE=$(dirname "$0")/qmp_mouse.py
MOUSE_START="Mouse: PS/2 aux port enabled and tested, defaults set, reporting on; configuration 0x61 -> 0x43 (port 2 clock on, IRQ12 on)"
MOUSE_MASK="Mouse: IRQ12 and the cascade IRQ2 unmasked: IRQ mask 0xeff8 (expected 0xeff8)"
MOUSE_READY="Mouse test: ready: every packet is logged; the test ends 5 s after the last packet, or after 60 s without any"
# mouse_boot <log> <seconds> <qemu disk args> -- <qmp_mouse.py args...>:
# boots paused with a QMP socket at -m 128M, runs qmp_mouse.py, then stops
# QEMU. MB_OUT holds its output (also kept in <log>.mouse.txt).
mouse_boot() {
    local log=$1 secs=$2 sock=$OUT/qmp-mouse.sock t0 qpid qargs=()
    shift 2
    while [ "$1" != "--" ]; do qargs+=("$1"); shift; done
    shift
    rm -f "$log" "$sock"
    t0=$(date +%s%N)
    timeout "$secs" qemu-system-x86_64 -S -display none -no-reboot -serial file:"$log" \
        -qmp unix:"$sock",server=on,wait=off -m 128M "${qargs[@]}" >/dev/null 2>&1 &
    qpid=$!
    MB_OUT=$(python3 "$QMPMOUSE" "$sock" "$log" "$@" --timeout $((secs - 15)) 2>&1)
    grep -v '^MISMATCH ' <<< "$MB_OUT" | sed 's/^/      /'
    grep '^MISMATCH ' <<< "$MB_OUT" | head -n 5 | sed 's/^/      /'
    echo "$MB_OUT" > "${log%.log}.mouse.txt"
    sleep 0.5
    kill "$qpid" 2>/dev/null
    wait "$qpid" 2>/dev/null
    [ -f "$log" ] || : > "$log"
    boot_seconds "$log" "$t0"
}
# expect_line <log> <line> <description>: <line> is a whole line of <log>.
expect_line() {
    if grep -qxF -- "$2" "$1"; then pass "$3"; else fail "$3 (no line '$2' in $1)"; fi
}
# mouse_done_ok <log> <packets>: test=mouse's summary: every packet from
# 3 bytes each, no bad byte, overflow or drop; IRQ12 delivered once per
# byte read (plus at most one edge latched while it was masked, with
# nothing waiting), never with a port 1 byte. Prints the figures.
mouse_done_ok() {
    local d i b e
    d=$(grep -E '^Mouse test: done: ' "$1" | head -n 1)
    echo "      ${d:-no Mouse test: done line}"
    read -r i b e <<< "$(sed -nE "s/^Mouse test: done: $2 packets from $(( $2 * 3 )) bytes \(0 bad bytes, 0 overflows, 0 dropped: queue full\); IRQ12 delivered ([0-9]+) times: ([0-9]+) bytes read, ([0-9]+) with nothing waiting, 0 with a port 1 byte$/\1 \2 \3/p" <<< "$d")"
    [ -n "${e:-}" ] && [ "$b" -eq $(( $2 * 3 )) ] && [ "$e" -le 1 ] && [ "$i" -eq $(( b + e )) ]
}
