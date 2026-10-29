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

# ---- 1.3 stage 3b: the desktop behind test=desktop (parts 2 and 3) ----

# Test ISOs with one entry (no "textmode" line) and "cmdline: test=desktop
# ...": at System ready the desktop takes the screen (kernel/display/
# desktop.h). tests/qmp_desktop.py stops the guest at the desktop's frame
# marker and compares the screendump with tests/desktop_ref.py's desktop,
# its clock drawn from the logged "desktop: clock HH:MM" (no mask); then
# the pointer after relative moves (parked at 0, 0 first), and a key that
# hands the screen back to the console without being typed (since stage
# 4b: that opens the Console window without being typed). A second boot
# with desktopnocache must give the same screen. tests/pagetables.py
# models slot 506 with and without the background cache.
QMPDESKTOP=$(dirname "$0")/qmp_desktop.py
# desktop_iso <name> <cmdline> [a limine.conf line]: DT_ISO, empty if it failed.
desktop_iso() {
    local dir=$OUT/isodesktop-$1
    DT_ISO=$OUT/madire-os-desktop-$1.iso
    limine_tree "$dir"
    {
        printf 'timeout: 0\n\n/Madire OS (desktop test)\n    protocol: multiboot1\n'
        [ -z "${3:-}" ] || printf '    %s\n' "$3"
        printf '    path: boot():/boot/madire.kernel\n'
        printf '    cmdline: %s\n' "$2"
    } > "$dir/boot/limine/limine.conf"
    sed 's/^/      limine.conf: /' "$dir/boot/limine/limine.conf"
    limine_iso "$dir" "$DT_ISO" || { echo "      the test ISO could not be made"; DT_ISO=""; }
}
# desktop_boot <log> <iso> <WxH> <qmp_desktop.py steps...>: boots paused at
# -m 128M, runs qmp_desktop.py, then stops QEMU. DT_OUT holds its output
# (also kept in <log>.desktop.txt); it waits for DT_UNTIL at the end.
desktop_boot() {
    local log=$1 iso=$2 size=$3 sock=$OUT/qmp-desktop.sock t0 qpid
    shift 3
    rm -f "$log" "$sock"
    t0=$(date +%s%N)
    timeout 240 qemu-system-x86_64 -S -display none -no-reboot -serial file:"$log" \
        -qmp unix:"$sock",server=on,wait=off -m 128M -cdrom "$iso" >/dev/null 2>&1 &
    qpid=$!
    DT_OUT=$(python3 "$QMPDESKTOP" "$sock" "$log" "$@" --size "$size" ${DT_UNTIL:+--until "$DT_UNTIL"} \
             --timeout 180 2>&1)
    grep -v '^MISMATCH ' <<< "$DT_OUT" | sed 's/^/      /'
    grep '^MISMATCH ' <<< "$DT_OUT" | head -n 5 | sed 's/^/      /'
    echo "$DT_OUT" > "${log%.log}.desktop.txt"
    sleep 0.5
    kill "$qpid" 2>/dev/null
    wait "$qpid" 2>/dev/null
    [ -f "$log" ] || : > "$log"
    boot_seconds "$log" "$t0"
}
# desktop_moves <dx> <dy>: qmp_desktop.py move steps adding up to (dx, dy),
# none larger than 127 on either axis (one PS/2 packet each).
desktop_moves() {
    local dx=$1 dy=$2 sx sy
    while [ "$dx" -ne 0 ] || [ "$dy" -ne 0 ]; do
        sx=$dx; [ "$sx" -gt 127 ] && sx=127; [ "$sx" -lt -127 ] && sx=-127
        sy=$dy; [ "$sy" -gt 127 ] && sy=127; [ "$sy" -lt -127 ] && sy=-127
        printf 'move:%d,%d ' "$sx" "$sy"
        dx=$((dx - sx)); dy=$((dy - sy))
    done
}
# desktop_case <name> <mode WxHxD> <percent> <label> [a limine.conf line]:
# the desktop's 8 checks in one mode (two boots).
desktop_case() {
    local name=$1 mode=$2 pct=$3 label=$4 extra=${5:-} log w h pt px py first
    w=${mode%%x*}; h=${mode#*x}; h=${h%%x*}
    # Boot 1: the desktop (cached background), the pointer, a key.
    log=$OUT/serial-desktop-$name.log
    desktop_iso "$name" "test=desktop desktoplog mouselog" "$extra"
    px=$((w - 30)); py=$((h - 12))
    DT_OUT=""
    if [ -n "$DT_ISO" ]; then
        # shellcheck disable=SC2046
        DT_UNTIL="desktop-key-ok" desktop_boot "$log" "$DT_ISO" "${w}x${h}" screen \
            $(desktop_moves -635 -635) $(desktop_moves 127 100) pointer \
            $(desktop_moves $((px - 127)) $((py - 100))) pointer \
            "type:x{wait:1}echo desktop-key-ok{ret}"
    else : > "$log"; fi
    grep -E "^Desktop|^Mouse: pointer|^display: frame |^desktop: clock " "$log" | head -n 12 | sed 's/^/      /'
    first=$(sed -nE 's/^desktop: clock (.*)$/\1/p' "$log" | head -n 1)
    if grep -qxF "Desktop: shown on $mode (scale $pct %): the wallpaper with the M, the taskbar, the clock $first; a key opens the Console window (test=desktop, 1.3 stage 4b)" "$log" &&
       grep -qE "^Desktop: slot 506: background cache at 0xfffffd0080000000 \([0-9]+ pages of free RAM, pitch $((w * ${mode##*x} / 8))\), [0-9]+ page tables; " "$log" &&
       grep -qE "^Desktop: slot 506 now: [0-9]+ page tables, [0-9]+ pages mapped \(walked\)$" "$log"; then
        pass "[1.3] $label: Desktop: shown on $mode (scale $pct %) with its first clock; the background cached in slot 506 at 0xfffffd0080000000; slot 506 walked"
    else
        fail "[1.3] $label: no Desktop: shown line with the first logged clock, no background cache line, or no slot 506 walk line (see $log)"
    fi
    if grep -qE "^SCREEN 1 size=${w}x${h} differ=0 " <<< "$DT_OUT"; then
        pass "[1.3] $label: the desktop at its first frame (the wallpaper with the M at the splash's place, the taskbar, the clock as logged) equals tests/desktop_ref.py's rendering pixel for pixel"
    else
        fail "[1.3] $label: the desktop's first frame differs from tests/desktop_ref.py's rendering ($(grep -E '^(SCREEN 1|MARKER|qmp_desktop)' <<< "$DT_OUT" | tr '\n' ' '))"
    fi
    if grep -qE "^PACKETS sent=([0-9]+) exact=\1$" <<< "$DT_OUT" && grep -qE "^mouse: packet 5: .* at \(0, 0\)$" "$log" &&
       grep -qE "^SCREEN 2 size=${w}x${h} differ=0 " <<< "$DT_OUT"; then
        pass "[1.3] $label: the pointer parked at (0, 0), then moved to (127, 100): every packet exact, the screen (the pointer on the desktop) equals the reference pixel for pixel"
    else
        fail "[1.3] $label: a packet not as predicted, not parked at (0, 0), or the pointer's screen at (127, 100) differs ($(grep -E '^(PACKETS|SCREEN 2|MISMATCH|qmp_desktop)' <<< "$DT_OUT" | head -n 3 | tr '\n' ' '))"
    fi
    if grep -qE "^SCREEN 3 size=${w}x${h} differ=0 " <<< "$DT_OUT"; then
        pass "[1.3] $label: the pointer moved over the taskbar's clock to ($px, $py): the screen equals the reference pixel for pixel (its earlier place shows the desktop again)"
    else
        fail "[1.3] $label: the pointer's screen at ($px, $py) differs ($(grep -E '^(SCREEN 3|qmp_desktop)' <<< "$DT_OUT" | tr '\n' ' '))"
    fi
    if grep -qxF "Desktop: a key opened the Console window (the key is not typed)" "$log" &&
       grep -qxF "madire> echo desktop-key-ok" "$log" && grep -qxF "desktop-key-ok" "$log" &&
       ! grep -qE "^madire> x|PANIC|events dropped|FAILED|MISMATCH" "$log"; then
        pass "[1.3] $label: a key opens the Console window, where the console opens; the key not typed (madire> echo desktop-key-ok, no x); no FAILED line, panic or dropped key"
    else
        fail "[1.3] $label: the key did not open the Console window, was typed, or a FAILED line, panic or dropped key (see $log)"
    fi
    pt=$(python3 "$(dirname "$0")/pagetables.py" "$log" --label "desktop-$name")
    sed 's/^/      /' <<< "$pt"
    if grep -qE "^PAGETABLES_RESULT label=desktop-$name computed=([0-9]+) logged=\1 .* MATCH$" <<< "$pt" &&
       grep -qE "^PAGETABLES_DESKTOP label=desktop-$name cache=present computed=([0-9]+/[0-9]+) logged=\1 MATCH$" <<< "$pt"; then
        pass "[1.3] $label: tests/pagetables.py models slot 506 with the background cache: its tables and pages equal the kernel's walk, the cache's tables as logged"
    else
        fail "[1.3] $label: tests/pagetables.py: $(grep -E '^PAGETABLES_(RESULT|DESKTOP)' <<< "$pt" | tr '\n' ' ')"
    fi
    # Boot 2: desktopnocache: the background drawn per damaged rectangle.
    log=$OUT/serial-desktop-$name-nocache.log
    desktop_iso "$name-nocache" "test=desktop desktopnocache" "$extra"
    DT_OUT=""
    if [ -n "$DT_ISO" ]; then
        DT_UNTIL="" desktop_boot "$log" "$DT_ISO" "${w}x${h}" screen
    else : > "$log"; fi
    grep -E "^Desktop|^display: frame |^desktop: clock " "$log" | head -n 6 | sed 's/^/      /'
    if grep -qxF "Desktop: background not cached (test option desktopnocache): drawn per damaged rectangle" "$log" &&
       grep -qE "^SCREEN 1 size=${w}x${h} differ=0 " <<< "$DT_OUT" &&
       ! grep -qE "PANIC|FAILED|MISMATCH" "$log"; then
        pass "[1.3] $label, desktopnocache: the background not cached, drawn per damaged rectangle: the same screen as the reference pixel for pixel"
    else
        fail "[1.3] $label, desktopnocache: no 'not cached' line, a different screen, or a FAILED line or panic ($(grep -E '^(SCREEN 1|MARKER|qmp_desktop)' <<< "$DT_OUT" | tr '\n' ' '))"
    fi
    pt=$(python3 "$(dirname "$0")/pagetables.py" "$log" --label "desktop-$name-nocache")
    sed 's/^/      /' <<< "$pt"
    if grep -qE "^PAGETABLES_RESULT label=desktop-$name-nocache computed=([0-9]+) logged=\1 .* MATCH$" <<< "$pt" &&
       grep -qE "^PAGETABLES_DESKTOP label=desktop-$name-nocache cache=absent computed=([0-9]+/[0-9]+) logged=\1 MATCH$" <<< "$pt"; then
        pass "[1.3] $label, desktopnocache: tests/pagetables.py models slot 506 without the cache: its tables and pages equal the kernel's walk"
    else
        fail "[1.3] $label, desktopnocache: tests/pagetables.py: $(grep -E '^PAGETABLES_(RESULT|DESKTOP)' <<< "$pt" | tr '\n' ' ')"
    fi
}

# ---- 1.3 stage 4a: windows: the About window behind test=desktop desktopabout ----

# desktop_about_case <name> <mode WxHxD> <percent> <label> [a limine.conf
# line]: the window manager's 13 checks in one mode (two boots, both with
# desktoplog and mouselog). tests/qmp_desktop.py follows every packet and
# key with tests/desktop_ref.py's model of the window manager's rules (WM):
# its WSCREEN lines compare each screen with the desktop, the model's
# windows (with their logged About values, cut as ui_about.h says) and the
# pointer; its EVENTS line compares the kernel's window lines ("desktop:
# open|raise|focus|drag|close ...") with the model's, in order; its DRAG
# lines give where each drag asked to go and where the rules put it.
# Boot 1, the window surfaces (slot 506, after the cache): the first frame,
# a drag by the title bar, a drag that asks to go under the taskbar and
# past the left edge (clamped), hover, press and release on the close
# button. Boot 2, desktopnosurface (every window painted on exposure): the
# first frame, a drag, a click on the desktop (no window focused) and on
# the window (focused again), a key that does nothing, Esc, then a key
# back to the console (since stage 4b: a key that opens the Console
# window; tests/qmp_desktop.py's model follows it, so the EVENTS line
# includes its lines).
desktop_about_case() {
    local name=$1 mode=$2 pct=$3 label=$4 extra=${5:-} log w h s mb ok d1 d2 pt
    local ax ay gx gy ww wh bar left
    w=${mode%%x*}; h=${mode#*x}; h=${h%%x*}; s=$((pct / 25))
    # Boot 1: surfaces.
    log=$OUT/serial-desktop-about-$name.log
    desktop_iso "about-$name" "test=desktop desktopabout desktoplog mouselog" "$extra"
    DT_OUT=""
    if [ -n "$DT_ISO" ]; then
        DT_UNTIL="" desktop_boot "$log" "$DT_ISO" "${w}x${h}" wshot rel:-1000,-1000 to:title drag:-120,-60 wshot \
            to:title-right drag:-2000,1000 wshot to:close wshot press wshot release wshot --about
    else : > "$log"; fi
    grep -E "^Desktop|^desktop: (about|open|drag|close|focus)" "$log" | head -n 24 | sed 's/^/      /'
    mb=$(sed -nE 's/^Memory: [0-9]+ MB detected, ([0-9]+) MB usable below 4 GiB, .*/\1/p' "$log" | head -n 1)
    if grep -qxF "Desktop: the About window opened at start (test option desktopabout; stage 5's menus open it)" "$log" &&
       grep -qxF "desktop: about Version: $VERSION" "$log" && grep -qE "^desktop: about Boot loader: Limine" "$log" &&
       grep -qxF "desktop: about Display: $mode" "$log" && [ -n "$mb" ] && grep -qxF "desktop: about Memory: $mb MB" "$log" &&
       grep -qxF "desktop: about Mode: Live (memory only)" "$log"; then
        pass "[1.3] $label: the About window opened at start (desktopabout); its values logged in full: Version $VERSION, Boot loader Limine's name, Display $mode, Memory $mb MB (the boot's usable MB), Mode Live (memory only)"
    else
        fail "[1.3] $label: no 'About window opened at start' line, or an About value not as expected (see $log)"
    fi
    pt=$(python3 "$(dirname "$0")/pagetables.py" "$log" --label "desktop-about-$name")
    grep -E "window surface|PAGETABLES_SURFACES" <<< "$pt" | sed 's/^/      /'
    if grep -qE "^Desktop: slot 506: window surface Console at 0xfffffd00[0-9a-f]{8} \([0-9]+ pages of free RAM, [0-9]+x[0-9]+, pitch [0-9]+\), [0-9]+ page tables; " "$log" &&
       grep -qE "^Desktop: slot 506: window surface About at 0xfffffd00[0-9a-f]{8} \([0-9]+ pages of free RAM, $(( (352 * s + 2) / 4 ))x$(( (377 * s + 2) / 4 )), pitch [0-9]+\), [0-9]+ page tables; " "$log" &&
       grep -qE "^PAGETABLES_SURFACES label=desktop-about-$name cache=present surfaces=Console,About computed=([0-9]+/[0-9]+) logged=\1 MATCH$" <<< "$pt"; then
        pass "[1.3] $label: the window surfaces (Console, then About, 352x377 dp) in slot 506 right after the background cache, under the share; tests/pagetables.py models them: tables and pages equal the kernel's walk"
    else
        fail "[1.3] $label: window surface lines or tests/pagetables.py's model: $(grep -E '^PAGETABLES_SURFACES' <<< "$pt")"
    fi
    if grep -qE "^WSCREEN 1 size=${w}x${h} differ=0 " <<< "$DT_OUT"; then
        pass "[1.3] $label: the About window's first frame (active, centred above the taskbar, from its surface) equals tests/desktop_ref.py's rendering pixel for pixel"
    else
        fail "[1.3] $label: the About window's first frame differs ($(grep -E '^(WSCREEN 1|qmp_desktop)' <<< "$DT_OUT" | tr '\n' ' '))"
    fi
    d1=$(sed -nE 's/^DRAG 1 asked \((-?[0-9]+), (-?[0-9]+)\) got \(\1, \2\) ([0-9]+x[0-9]+) .*/\1, \2) \3/p' <<< "$DT_OUT")
    if [ -n "$d1" ] && grep -qxF "desktop: drag About to ($d1" "$log" && grep -qE "^WSCREEN 2 size=${w}x${h} differ=0 " <<< "$DT_OUT"; then
        pass "[1.3] $label: a drag by the title bar moves the window 1:1 (logged as predicted: to ($d1); the screen equals the reference pixel for pixel"
    else
        fail "[1.3] $label: the drag by the title bar: $(grep -E '^(DRAG 1|WSCREEN 2|qmp_desktop)' <<< "$DT_OUT" | tr '\n' ' ')"
    fi
    d2=$(sed -nE 's/^DRAG 2 asked \((-?[0-9]+), (-?[0-9]+)\) got \((-?[0-9]+), (-?[0-9]+)\) ([0-9]+)x([0-9]+) bar ([0-9]+) left (-?[0-9]+)$/\1 \2 \3 \4 \5 \6 \7 \8/p' <<< "$DT_OUT")
    ok=0
    if [ -n "$d2" ]; then
        read -r ax ay gx gy ww wh bar left <<< "$d2"
        if [ $((ay + wh)) -gt "$bar" ] && [ $((gy + wh)) -eq "$bar" ] && [ "$ax" -lt "$left" ] && [ "$gx" -eq "$left" ] &&
           [ "$left" -eq $(( (48 * s + 2) / 4 - ww )) ] && grep -qxF "desktop: drag About to ($gx, $gy) ${ww}x${wh}" "$log" &&
           grep -qE "^WSCREEN 3 size=${w}x${h} differ=0 " <<< "$DT_OUT"; then
            ok=1
        fi
    fi
    if [ "$ok" = 1 ]; then
        pass "[1.3] $label: a drag that asks to go under the taskbar and past the left edge is clamped (its bottom on the taskbar's top, 48 dp left on the screen; logged as predicted: ($gx, $gy)); the screen equals the reference"
    else
        fail "[1.3] $label: the clamped drag: $(grep -E '^(DRAG 2|WSCREEN 3|qmp_desktop)' <<< "$DT_OUT" | tr '\n' ' ')"
    fi
    if grep -qxF "desktop: close button of About: hover" "$log" && grep -qE "^WSCREEN 4 size=${w}x${h} differ=0 " <<< "$DT_OUT"; then
        pass "[1.3] $label: the pointer on the close button: hover (logged), filled in danger; the screen equals the reference pixel for pixel"
    else
        fail "[1.3] $label: the close button's hover: $(grep -E '^(WSCREEN 4|qmp_desktop)' <<< "$DT_OUT" | tr '\n' ' ')"
    fi
    if grep -qxF "desktop: close button of About: pressed" "$log" && grep -qE "^WSCREEN 5 size=${w}x${h} differ=0 " <<< "$DT_OUT"; then
        pass "[1.3] $label: the left button pressed on the close button: pressed (logged), filled in danger.pressed; the screen equals the reference pixel for pixel"
    else
        fail "[1.3] $label: the close button pressed: $(grep -E '^(WSCREEN 5|qmp_desktop)' <<< "$DT_OUT" | tr '\n' ' ')"
    fi
    if grep -qxF "desktop: close About (the close button)" "$log" && grep -qE "^WSCREEN 6 size=${w}x${h} differ=0 " <<< "$DT_OUT" &&
       grep -qE "^EVENTS predicted=([0-9]+) logged=\1 same=yes$" <<< "$DT_OUT" && grep -qE "^PACKETS sent=([0-9]+) exact=\1$" <<< "$DT_OUT" &&
       ! grep -qE "PANIC|FAILED|events dropped|bytes dropped|MISMATCH" "$log"; then
        pass "[1.3] $label: released on the close button: the window closes (logged), the screen back to the plain desktop with the pointer, pixel for pixel; every window line as the model predicts, every packet exact; no panic, FAILED line or drop"
    else
        fail "[1.3] $label: the close: $(grep -E '^(WSCREEN 6|EVENTS|PACKETS|qmp_desktop)' <<< "$DT_OUT" | tr '\n' ' ')"
    fi
    # Boot 2: desktopnosurface: every window painted on exposure (A1).
    log=$OUT/serial-desktop-about-$name-nosurface.log
    desktop_iso "about-$name-nosurface" "test=desktop desktopabout desktopnosurface desktoplog mouselog" "$extra"
    DT_OUT=""
    if [ -n "$DT_ISO" ]; then
        DT_UNTIL="desktop-about-ok" desktop_boot "$log" "$DT_ISO" "${w}x${h}" wshot rel:-1000,-1000 to:title \
            drag:-100,-50 wshot to:desktop press release wshot to:body press release wshot key:a esc wshot \
            "type:x{wait:1}echo desktop-about-ok{ret}" --about
    else : > "$log"; fi
    grep -E "^Desktop|^desktop: (open|drag|close|focus)" "$log" | head -n 20 | sed 's/^/      /'
    pt=$(python3 "$(dirname "$0")/pagetables.py" "$log" --label "desktop-about-$name-nosurface")
    grep -E "PAGETABLES_SURFACES" <<< "$pt" | sed 's/^/      /'
    if grep -qxF "Desktop: window Console drawn on exposure (test option desktopnosurface)" "$log" &&
       grep -qxF "Desktop: window About drawn on exposure (test option desktopnosurface)" "$log" &&
       grep -qE "^PAGETABLES_SURFACES label=desktop-about-$name-nosurface cache=present surfaces=none computed=([0-9]+/[0-9]+) logged=\1 MATCH$" <<< "$pt" &&
       grep -qE "^WSCREEN 1 size=${w}x${h} differ=0 " <<< "$DT_OUT"; then
        pass "[1.3] $label, desktopnosurface: no window surface (slot 506 modelled without them); the About window painted on exposure: its first frame equals the reference pixel for pixel (as with its surface)"
    else
        fail "[1.3] $label, desktopnosurface: no 'drawn on exposure' lines, slot 506 not as modelled, or the first frame differs ($(grep -E '^(WSCREEN 1|qmp_desktop)' <<< "$DT_OUT" | tr '\n' ' ') $(grep -E '^PAGETABLES_SURFACES' <<< "$pt"))"
    fi
    d1=$(sed -nE 's/^DRAG 1 asked \((-?[0-9]+), (-?[0-9]+)\) got \(\1, \2\) ([0-9]+x[0-9]+) .*/\1, \2) \3/p' <<< "$DT_OUT")
    if [ -n "$d1" ] && grep -qxF "desktop: drag About to ($d1" "$log" && grep -qE "^WSCREEN 2 size=${w}x${h} differ=0 " <<< "$DT_OUT"; then
        pass "[1.3] $label, desktopnosurface: a drag by the title bar (the damaged rectangles painted on exposure; logged as predicted: to ($d1); the screen equals the reference pixel for pixel"
    else
        fail "[1.3] $label, desktopnosurface: the drag: $(grep -E '^(DRAG 1|WSCREEN 2|qmp_desktop)' <<< "$DT_OUT" | tr '\n' ' ')"
    fi
    if grep -qxF "desktop: focus none" "$log" && grep -qE "^WSCREEN 3 size=${w}x${h} differ=0 " <<< "$DT_OUT" &&
       [ "$(grep -cxF "desktop: focus About" "$log")" -ge 2 ] && grep -qE "^WSCREEN 4 size=${w}x${h} differ=0 " <<< "$DT_OUT"; then
        pass "[1.3] $label, desktopnosurface: a click on the desktop leaves no window focused (the window inactive: its edge and title), a click on the window focuses it again (active); both screens equal the reference pixel for pixel"
    else
        fail "[1.3] $label, desktopnosurface: focus by clicks: $(grep -E '^(WSCREEN 3|WSCREEN 4|qmp_desktop)' <<< "$DT_OUT" | tr '\n' ' ')"
    fi
    if grep -qxF "desktop: close About (Esc)" "$log" &&
       ! sed -n '1,/^desktop: close About (Esc)$/p' "$log" | grep -qE "^Desktop: a key opened the Console window" &&
       grep -qE "^WSCREEN 5 size=${w}x${h} differ=0 " <<< "$DT_OUT" && grep -qE "^EVENTS predicted=([0-9]+) logged=\1 same=yes$" <<< "$DT_OUT"; then
        pass "[1.3] $label, desktopnosurface: with the About window focused a key (a) does nothing and Esc closes it (logged); the screen back to the plain desktop, pixel for pixel; every window line as the model predicts"
    else
        fail "[1.3] $label, desktopnosurface: the keys with the window focused: $(grep -E '^(WSCREEN 5|EVENTS|qmp_desktop)' <<< "$DT_OUT" | tr '\n' ' ')"
    fi
    if grep -qxF "Desktop: a key opened the Console window (the key is not typed)" "$log" &&
       grep -qxF "madire> echo desktop-about-ok" "$log" && grep -qxF "desktop-about-ok" "$log" &&
       ! grep -qE "^madire> (a|x)|PANIC|FAILED|events dropped|bytes dropped|MISMATCH" "$log" &&
       grep -qE "^PACKETS sent=([0-9]+) exact=\1$" <<< "$DT_OUT"; then
        pass "[1.3] $label, desktopnosurface: with no window focused a key opens the Console window, not typed (stage 4b); no panic, FAILED line or drop; every packet exact"
    else
        fail "[1.3] $label, desktopnosurface: the key that opens the Console window, or a panic, FAILED line or drop (see $log)"
    fi
}

# ---- 1.3 stage 4b: the Console window behind test=desktop ----

# desktop_console_case <name> <mode WxHxD> <percent> <label> [a limine.conf
# line]: the Console window's 7 checks in one mode (two boots, both with
# desktopabout, desktoplog and mouselog). tests/qmp_desktop.py's model
# (tests/desktop_ref.py's WM) follows every packet and key: with no window
# focused a key opens the Console window and is not typed, with it focused
# the console takes the key, with the About window focused keys do
# nothing. Each WSCREEN compares the screen with the desktop, the model's
# windows and the pointer, the Console window drawn from the kernel's cell
# grid (vga_shadow, saved over QMP at the same stop: --shadow); its GRID
# line gives the grid's CRC and rows 0 and 24. The commands typed in the
# window are checked against the same lines as on the full-screen console
# (version and uname -a as [0.5] and [0.9.1] read them, echo). Boot 1, the
# window surfaces: a click on the desktop (no window focused), a key opens
# the Console window, three commands, help and Shift+PgUp (scrolled back:
# row 0 is the logged top line), its close button hides it, a click on the
# desktop and a key show it again (the same grid), the Console window
# dragged aside, a click on the About window's title bar (keys do nothing
# there), a click on the Console window's body (keys go to the console
# again). Boot 2, desktopnosurface (both windows painted on exposure): the
# same without help and Shift+PgUp.
desktop_console_case() {
    local name=$1 mode=$2 pct=$3 label=$4 extra=${5:-} log w h s cw ch shv shp top g2 g3 g4 g5
    w=${mode%%x*}; h=${mode#*x}; h=${h%%x*}; s=$((pct / 25))
    # The Console window: 80 x 25 cells of the scale's font, the edge, the title bar.
    case $s in 4) cw=8; ch=16 ;; 5) cw=10; ch=20 ;; 6) cw=12; ch=24 ;; *) cw=16; ch=32 ;; esac
    local ww=$((80 * cw + 2 * ((s + 2) / 4))) wh=$(( (32 * s + 2) / 4 + 25 * ch + (s + 2) / 4 ))
    shv=$(nm "$OUT/madire.elf" | awk '$3 == "vga_shadow" { print $1 }')
    shp=$(printf '0x%x' $(( 0x${shv:-0} - 0xffffffff80000000 )))
    # Boot 1: surfaces.
    log=$OUT/serial-desktop-console-$name.log
    desktop_iso "console-$name" "test=desktop desktopabout desktoplog mouselog" "$extra"
    DT_OUT=""
    if [ -n "$DT_ISO" ]; then
        DT_UNTIL="" desktop_boot "$log" "$DT_ISO" "${w}x${h}" ready rel:-1000,-1000 to:desktop press release "type:x" wshot \
            "type:version{ret}uname -a{ret}echo console-window-ok{ret}" "wait:console-window-ok" wshot \
            "type:help{ret}echo help-done{ret}" "wait:help-done" "type:{shift+pgup}{wait:2}" wshot \
            to:close press release wshot to:desktop press release "type:y" wshot \
            to:title drag:400,0 to:title@About press release "type:a{wait:1}" wshot \
            to:body@Console press release "type:echo focus-ok{ret}" "wait:focus-ok" wshot \
            --about --shadow "$shp"
    else : > "$log"; fi
    grep -E "^Desktop|^desktop: (open|close|focus|raise)|^console: scrolled" "$log" | head -n 24 | sed 's/^/      /'
    grep -E "^GRID " <<< "$DT_OUT" | sed 's/^/      /'
    if grep -qE "^Desktop: shown on $mode \(scale $pct %\): .*; a key opens the Console window \(test=desktop, 1\.3 stage 4b\)$" "$log" &&
       grep -qxF "Desktop: a key opened the Console window (the key is not typed)" "$log" &&
       grep -qE "^desktop: open Console at \(-?[0-9]+, -?[0-9]+\) ${ww}x${wh}$" "$log" && grep -qxF "desktop: focus Console" "$log" &&
       ! grep -qE "^madire> x" "$log" && grep -qE "^WSCREEN 1 size=${w}x${h} differ=0 " <<< "$DT_OUT"; then
        pass "[1.3] $label: with no window focused a key opens the Console window (${ww}x${wh}: the 80x25 grid at $pct %), focused, the key not typed; its first frame (over the inactive About window) equals the reference drawn from vga_shadow pixel for pixel"
    else
        fail "[1.3] $label: the key did not open the Console window as predicted, was typed, or its first frame differs ($(grep -E '^(WSCREEN 1|qmp_desktop)' <<< "$DT_OUT" | tr '\n' ' '))"
    fi
    if grep -A1 -xF "madire> version" "$log" | grep -qE "^Madire OS v$VRE \(64-bit x86-64, Multiboot 1\)$" &&
       grep -A1 -xF "madire> uname -a" "$log" | grep -qE "^Madire OS madire $VRE x86_64$" &&
       grep -A1 -xF "madire> echo console-window-ok" "$log" | grep -qxF "console-window-ok" &&
       grep -qE "^WSCREEN 2 size=${w}x${h} differ=0 " <<< "$DT_OUT"; then
        pass "[1.3] $label: version, uname -a and echo typed in the Console window give the same serial lines as on the full-screen console; the window's screen equals the reference pixel for pixel"
    else
        fail "[1.3] $label: a command in the Console window not as on the full-screen console, or its screen differs ($(grep -E '^(WSCREEN 2|qmp_desktop)' <<< "$DT_OUT" | tr '\n' ' '))"
    fi
    top=$(sed -nE "s/^console: scrolled back: line [0-9]+ of [0-9]+, top '(.*)'$/\1/p" "$log" | head -n 1)
    g2=$(sed -nE 's/^GRID 2 crc=([0-9a-f]+) .*/\1/p' <<< "$DT_OUT")
    g3=$(sed -nE 's/^GRID 3 crc=([0-9a-f]+) .*/\1/p' <<< "$DT_OUT")
    if [ -n "$top" ] && grep -qxF "GRID 3 crc=$g3 row0='$top' row24=$(sed -nE 's/^GRID 3 crc=[0-9a-f]+ row0=.* row24=(.*)$/\1/p' <<< "$DT_OUT")" <<< "$DT_OUT" &&
       [ -n "$g2" ] && [ "$g2" != "$g3" ] && grep -qE "^WSCREEN 3 size=${w}x${h} differ=0 " <<< "$DT_OUT"; then
        pass "[1.3] $label: Shift+PgUp in the Console window scrolls back (logged; row 0 of the grid is the logged top line '$top'); the window shows it, equal to the reference pixel for pixel"
    else
        fail "[1.3] $label: Shift+PgUp in the Console window: no scrolled-back line, row 0 not its top line, or the screen differs ($(grep -E '^(WSCREEN 3|GRID 3|qmp_desktop)' <<< "$DT_OUT" | tr '\n' ' '))"
    fi
    g4=$(sed -nE 's/^GRID 4 crc=([0-9a-f]+) .*/\1/p' <<< "$DT_OUT")
    g5=$(sed -nE 's/^GRID 5 crc=([0-9a-f]+) .*/\1/p' <<< "$DT_OUT")
    if grep -qxF "desktop: close Console (the close button)" "$log" && grep -qE "^WSCREEN 4 size=${w}x${h} differ=0 " <<< "$DT_OUT" &&
       [ "$(grep -cxF "Desktop: a key opened the Console window (the key is not typed)" "$log")" -eq 2 ] &&
       [ -n "$g5" ] && [ "$g5" = "$g3" ] && [ -z "$g4" ] && ! grep -qE "^madire> .*y$" "$log" &&
       grep -qE "^WSCREEN 5 size=${w}x${h} differ=0 " <<< "$DT_OUT"; then
        pass "[1.3] $label: the close button hides the Console window (the About window alone, pixel for pixel); after a click on the desktop a key shows it again, not typed, with the same grid and cursor (CRC $g5), pixel for pixel"
    else
        fail "[1.3] $label: hiding and showing the Console window again: $(grep -E '^(WSCREEN 4|WSCREEN 5|GRID 5|qmp_desktop)' <<< "$DT_OUT" | tr '\n' ' ')"
    fi
    if grep -qE "^WSCREEN 6 size=${w}x${h} differ=0 " <<< "$DT_OUT" && grep -qE "^WSCREEN 7 size=${w}x${h} differ=0 " <<< "$DT_OUT" &&
       grep -qxF "madire> echo focus-ok" "$log" && grep -qxF "focus-ok" "$log" && ! grep -qE "^madire> a" "$log" &&
       grep -qE "^EVENTS predicted=([0-9]+) logged=\1 same=yes$" <<< "$DT_OUT" && grep -qE "^PACKETS sent=([0-9]+) exact=\1$" <<< "$DT_OUT" &&
       ! grep -qE "PANIC|FAILED|events dropped|bytes dropped|MISMATCH" "$log"; then
        pass "[1.3] $label: focus moves by clicks between the Console and About windows and keys go only to the focused one (a with About focused: nothing; echo focus-ok in the Console window); both screens pixel for pixel; every window line and packet as predicted; no panic, FAILED line or drop"
    else
        fail "[1.3] $label: focus between the Console and About windows: $(grep -E '^(WSCREEN 6|WSCREEN 7|EVENTS|PACKETS|qmp_desktop)' <<< "$DT_OUT" | tr '\n' ' ')"
    fi
    # Boot 2: desktopnosurface: both windows painted on exposure (A1).
    log=$OUT/serial-desktop-console-$name-nosurface.log
    desktop_iso "console-$name-nosurface" "test=desktop desktopabout desktopnosurface desktoplog mouselog" "$extra"
    DT_OUT=""
    if [ -n "$DT_ISO" ]; then
        DT_UNTIL="" desktop_boot "$log" "$DT_ISO" "${w}x${h}" ready rel:-1000,-1000 to:desktop press release "type:x" wshot \
            "type:version{ret}echo console-window-ok{ret}" "wait:console-window-ok" wshot \
            to:close press release wshot to:desktop press release "type:y" wshot \
            to:title drag:400,0 to:title@About press release "type:a{wait:1}" wshot \
            to:body@Console press release "type:echo focus-ok{ret}" "wait:focus-ok" wshot \
            --about --shadow "$shp"
    else : > "$log"; fi
    grep -E "^Desktop|^desktop: (open|close|focus|raise)" "$log" | head -n 20 | sed 's/^/      /'
    grep -E "^GRID " <<< "$DT_OUT" | sed 's/^/      /'
    if grep -qxF "Desktop: window Console drawn on exposure (test option desktopnosurface)" "$log" &&
       grep -qxF "Desktop: a key opened the Console window (the key is not typed)" "$log" && ! grep -qE "^madire> x" "$log" &&
       grep -A1 -xF "madire> version" "$log" | grep -qE "^Madire OS v$VRE \(64-bit x86-64, Multiboot 1\)$" &&
       grep -A1 -xF "madire> echo console-window-ok" "$log" | grep -qxF "console-window-ok" &&
       grep -qE "^WSCREEN 1 size=${w}x${h} differ=0 " <<< "$DT_OUT" && grep -qE "^WSCREEN 2 size=${w}x${h} differ=0 " <<< "$DT_OUT"; then
        pass "[1.3] $label, desktopnosurface: the Console window without a surface (painted from vga_shadow on exposure): a key opens it, not typed; version and echo as on the full-screen console; both screens equal the reference pixel for pixel (as with its surface)"
    else
        fail "[1.3] $label, desktopnosurface: no 'drawn on exposure' line, the key or a command not as expected, or a screen differs ($(grep -E '^(WSCREEN 1|WSCREEN 2|qmp_desktop)' <<< "$DT_OUT" | tr '\n' ' '))"
    fi
    g2=$(sed -nE 's/^GRID 2 crc=([0-9a-f]+) .*/\1/p' <<< "$DT_OUT")
    g4=$(sed -nE 's/^GRID 4 crc=([0-9a-f]+) .*/\1/p' <<< "$DT_OUT")
    if grep -qE "^WSCREEN 3 size=${w}x${h} differ=0 " <<< "$DT_OUT" && grep -qE "^WSCREEN 4 size=${w}x${h} differ=0 " <<< "$DT_OUT" &&
       [ -n "$g4" ] && [ "$g4" = "$g2" ] && grep -qE "^WSCREEN 5 size=${w}x${h} differ=0 " <<< "$DT_OUT" &&
       grep -qE "^WSCREEN 6 size=${w}x${h} differ=0 " <<< "$DT_OUT" && grep -qxF "focus-ok" "$log" && ! grep -qE "^madire> (a|.*y$)" "$log" &&
       grep -qE "^EVENTS predicted=([0-9]+) logged=\1 same=yes$" <<< "$DT_OUT" && grep -qE "^PACKETS sent=([0-9]+) exact=\1$" <<< "$DT_OUT" &&
       ! grep -qE "PANIC|FAILED|events dropped|bytes dropped|MISMATCH" "$log"; then
        pass "[1.3] $label, desktopnosurface: hidden and shown again with the same grid (CRC $g4), focus moved by clicks, keys only to the focused window; every screen pixel for pixel; every window line and packet as predicted; no panic, FAILED line or drop"
    else
        fail "[1.3] $label, desktopnosurface: hide, show and focus: $(grep -E '^(WSCREEN [3-6]|GRID 4|EVENTS|PACKETS|qmp_desktop)' <<< "$DT_OUT" | tr '\n' ' ')"
    fi
}
