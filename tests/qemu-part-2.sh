# Madire OS QEMU tests, part 2 (0.12, stage 0c): [0.9.1], [0.9.2]
# scrollback, and [0.10] up to userkmem. Sourced by tests/qemu-tests.sh
# (a full run, or --shard 2). Apart from this header and the fixture call,
# every line was moved unchanged from the single script; the [0.12] shell
# session at the end was added in stage 1. 1.0 stage 2 adds the [1.0]
# storage diagnostics (test=fslimits, test=fsfill at 32M and 64M) last.

fixture_normal_boot     # 0.12: tests/qemu-lib.sh (the normal boot is read below)

# ---- 0.9.1: shell commands ----


echo "== [0.9.1] Console: help <command>, --help, uname, hostname, date, dmesg, history, reboot"
LOG=$OUT/serial-cmds091.log
A45=$(printf 'a%.0s' $(seq 1 45))
S="x{wait:1}"
S+="help{ret}"                                       # 1
S+="help mem{ret}"                                   # 2
S+="help nosuch{ret}"                                # 3
S+="help ${A45}{ret}"                                # 4  a long unknown name
S+="mem --help{ret}"                                 # 5
S+="echo --help{ret}"                                # 6
S+="reboot --help{ret}"                              # 7  help only: no question
S+="uname{ret}uname -a{ret}uname -x{ret}"            # 8, 9, 10
S+="hostname{ret}"                                   # 11
S+="date{ret}"                                       # 12
S+="dmesg 5{ret}"                                    # 13
S+="dmesg 0{ret}"                                    # 14
S+="echo one{ret}echo two{ret}"                      # 15, 16
S+="{up}{up}{down}{ret}"                             # 17 recalls "echo two"
S+="history{ret}"                                    # 18: lists 3..18
S+="{up}{ret}"                                       # 19 recalls "history"
S+="reboot{ret}no{ret}"                              # 20; "no" is an answer, not a command
S+="history{ret}"                                    # 21: lists 6..21
S+="echo end-of-091-test{ret}"
boot_text "$LOG" 120 "System ready." "$S" 0.03 $'\nend-of-091-test\n' -kernel "$KERNEL" -append "keylog"

expect_in_order "$LOG" "[0.9.1] help lists the six new commands after ps, in order" \
    "^madire> help$" "^  ps       Show threads and processes$" \
    "^  history  Show previous commands \(Up/Down recall\)$" "^  uname    Show the system name: uname \[-a\]$" \
    "^  hostname Show the computer's name$" "^  date     Show the date and time \(RTC\)$" \
    "^  dmesg    Show kernel messages: dmesg \[lines\]$" "^  reboot   Restart the computer \(asks first\)$"
expect_lines "$LOG" "[0.9.1] help mem: purpose and usage" \
    "^madire> help mem$" "^mem: Show memory information$" "^Usage: mem$"
expect_lines "$LOG" "[0.9.1] help nosuch: unknown command" \
    "^madire> help nosuch$" "^No help for 'nosuch': unknown command\.$"
expect_lines "$LOG" "[0.9.1] help with a 45-character name: shortened to fit" \
    "^madire> help ${A45}$" "^No help for 'a{37}\.\.\.': unknown command\.$"
expect_lines "$LOG" "[0.9.1] mem --help: the same as help mem" \
    "^madire> mem --help$" "^mem: Show memory information$" "^Usage: mem$"
expect_lines "$LOG" "[0.9.1] echo --help: echo's help, not the text" \
    "^madire> echo --help$" "^echo: Print text: echo <text>$" "^Usage: echo <text>$"
expect_lines "$LOG" "[0.9.1] reboot --help: help only, no restart question" \
    "^madire> reboot --help$" "^reboot: Restart the computer \(asks first\)$" "^Usage: reboot$" "^(console: |madire> )"
expect_lines "$LOG" "[0.9.1] uname" "^madire> uname$" "^Madire OS$"
expect_lines "$LOG" "[0.9.1] uname -a: name, host, version, architecture" \
    "^madire> uname -a$" "^Madire OS madire $VRE x86_64$"
expect_lines "$LOG" "[0.9.1] uname -x: usage" "^madire> uname -x$" "^Usage: uname \[-a\]$"
expect_lines "$LOG" "[0.9.1] hostname: the fixed name" "^madire> hostname$" "^madire$"
expect_lines "$LOG" "[0.9.1] date: YYYY-MM-DD HH:MM:SS from the RTC, no time zone" \
    "^madire> date$" "^[0-9]{4}-[0-9]{2}-[0-9]{2} [0-9]{2}:[0-9]{2}:[0-9]{2} \(RTC; time zone not set\)$"
D=$(grep -aA1 '^madire> date$' "$LOG" | grep -oE '^[0-9]{4}-[0-9]{2}-[0-9]{2} [0-9]{2}:[0-9]{2}:[0-9]{2}')
DIFF=$(( $(date -u +%s) - $(date -u -d "${D:-1970-01-01 00:00:00}" +%s) ))
if [ -n "$D" ] && [ "${DIFF#-}" -le 600 ]; then
    pass "[0.9.1] date: within 10 minutes of the host's UTC clock (QEMU's RTC is UTC; ${DIFF} s)"
else
    fail "[0.9.1] date: '${D}' is ${DIFF} s from the host's UTC clock"
fi
# dmesg 5: its 5 lines are the last 5 kernel-log lines, which are also the 5
# serial lines just before the command's own log line (the keys typed are
# logged too), and the newest is that line itself. 0.12: the "uptime: N s"
# heartbeat goes to the serial port only, not into the kernel log, so those
# serial lines are skipped.
if awk '
        /^madire> dmesg 5$/ && !seen { seen = 1; for (i = 1; i <= 4; i++) want[i] = prev[5 - i]; want[5] = $0; n = 0; next }
        !seen && /^uptime: [0-9]+ s$/ { next }
        !seen { for (i = 4; i > 1; i--) prev[i] = prev[i - 1]; prev[1] = $0; next }
        seen && n < 5 { n++; if ($0 != want[n]) bad = 1; next }
        END { exit !(seen && n == 5 && !bad) }' "$LOG"; then
    pass "[0.9.1] dmesg 5: the last 5 kernel-log lines, the newest being the command itself"
else
    fail "[0.9.1] dmesg 5: output does not match the kernel log"
fi
expect_lines "$LOG" "[0.9.1] dmesg 0: usage" "^madire> dmesg 0$" "^Usage: dmesg \[lines\]$"
count_lines "$LOG" "^madire> echo two$" 2           "[0.9.1] Up, Up, Down recalls and runs 'echo two' again"
expect_lines "$LOG" "[0.9.1] history: the last 16 lines, numbered since boot, itself last" \
    "^madire> history$" "^    3  help nosuch$" "^    4  help a{45}$" "^    5  mem --help$" \
    "^    6  echo --help$" "^    7  reboot --help$" "^    8  uname$" "^    9  uname -a$" \
    "^   10  uname -x$" "^   11  hostname$" "^   12  date$" "^   13  dmesg 5$" "^   14  dmesg 0$" \
    "^   15  echo one$" "^   16  echo two$" "^   17  echo two$" "^   18  history$" "^(console: |madire> )"
count_lines "$LOG" "^madire> history$" 3            "[0.9.1] Up recalls 'history' and runs it again"
expect_in_order "$LOG" "[0.9.1] reboot, then 'no': cancelled, the console continues" \
    "^madire> reboot$" "^Restart Madire OS now\? Type 'yes' to confirm:$" "^yes/no> no$" \
    "^Restart cancelled\.$" "^madire> history$"
expect_in_order "$LOG" "[0.9.1] the answer is not kept in the history" \
    "^yes/no> no$" "^madire> history$" "^    6  echo --help$" "^   19  history$" "^   20  reboot$" \
    "^   21  history$"
w=$(max_width "$LOG" "^console: started$" "^end-of-091-test$")
if grep -q "^end-of-091-test$" "$LOG" && [ "$w" -le 79 ]; then
    pass "[0.9.1] every console line at most 79 characters (longest $w)"
else
    fail "[0.9.1] a console line of $w characters (limit 79)"
fi
reject "$LOG" "PANIC|events dropped|Screen output from thread|Unexpected IRQ" \
                                                      "[0.9.1] no panic, dropped key or screen-rule violation"

echo "== [0.9.1] reboot, then 'yes': restart through the keyboard controller"
LOG=$OUT/serial-reboot.log
T0=$(date +%s)
boot_keys "$LOG" 40 "System ready." "x,r,e,b,o,o,t,ret,y,e,s,ret" 0.15 -m 32M -kernel "$KERNEL"
T1=$(date +%s)
expect_in_order "$LOG" "[0.9.1] reboot: question, yes, Restarting..." \
    "^madire> reboot$" "^Restart Madire OS now\? Type 'yes' to confirm:$" "^yes/no> yes$" \
    "^Restarting\.\.\.$" "^console: restart confirmed$"
expect "$LOG" "^Restart: keyboard controller reset \(0xfe to port 0x64\)$" \
                                                      "[0.9.1] reboot: reset through the keyboard controller"
reject "$LOG" "did not reset the machine|forcing a triple fault" \
                                                      "[0.9.1] reboot: the keyboard controller reset worked (no fallback)"
if [ $((T1 - T0)) -lt 35 ]; then pass "[0.9.1] reboot: the machine reset (QEMU -no-reboot exited after $((T1 - T0)) s)"
else fail "[0.9.1] reboot: QEMU still running at the timeout ($((T1 - T0)) s)"; fi
reject "$LOG" "PANIC"                                 "[0.9.1] reboot: no panic"

echo "== [0.9.1] Restart fallback: triple fault (test=reboottriple, QEMU only)"
LOG=$OUT/serial-reboottriple.log
T0=$(date +%s)
boot "$LOG" 20 -m 32M -kernel "$KERNEL" -append "test=reboottriple"
T1=$(date +%s)
expect_in_order "$LOG" "[0.9.1] reboottriple: the fallback without the keyboard controller" \
    "^Restart test: forcing a triple fault \(no keyboard controller\)$" \
    "^Restart: forcing a triple fault \(empty interrupt table, then int3\)$"
reject "$LOG" "keyboard controller reset \(0xfe"    "[0.9.1] reboottriple: the keyboard controller was not used"
if [ $((T1 - T0)) -lt 15 ]; then pass "[0.9.1] reboottriple: the machine reset (QEMU -no-reboot exited after $((T1 - T0)) s)"
else fail "[0.9.1] reboottriple: QEMU still running at the timeout ($((T1 - T0)) s)"; fi
count_lines "$LOG" "^boot32: start$" 1                "[0.9.1] reboottriple: booted once"
reject "$LOG" "KERNEL PANIC|System ready"             "[0.9.1] reboottriple: no crash screen, never reached System ready"

echo "== [0.9.2] Console scrollback: Shift+PgUp / Shift+PgDn, the indicator on the VGA screen"
LOG=$OUT/serial-scroll.log
SOCK=$OUT/qmp.sock
rm -f "$LOG" "$SOCK" "$OUT"/screen-*.bin
timeout 400 qemu-system-x86_64 -display none -no-reboot -serial file:"$LOG" \
    -qmp unix:"$SOCK",server=on,wait=off -kernel "$KERNEL" >/dev/null 2>&1 &
QPID=$!
sb_text() { python3 "$QMPKEYS" "$SOCK" "$LOG" "System ready." --text "$1" --gap 0.05 >/dev/null 2>&1; }
sb_keys() { python3 "$QMPKEYS" "$SOCK" "$LOG" "System ready." "$1" --gap 0.2 >/dev/null 2>&1; }
sb_screen() {   # sb_screen <n> <qmp_screen.py options...>: dump the screen to screen-<n>.txt
    local n=$1
    shift
    python3 "$QMPSCREEN" "$SOCK" "$OUT/screen-$n.bin" --log "$LOG" "$@" > "$OUT/screen-$n.txt" 2>&1
}
sb_back() { grep -c '^console: scrolled back: ' "$LOG"; }
sb_last() { grep '^console: scrolled back: ' "$LOG" | tail -1; }
IND='^-- scrolled back: line [0-9]+ of [0-9]+ \(Shift\+PgUp/PgDn; any key returns\) --$'

sb_text "x{wait:1}{shift+pgup}"
sb_screen 0 --wait '^console: scrollback is empty$' 1 --not-row 24 'scrolled back'
expect "$LOG" "^console: scrollback is empty$"        "[0.9.2] scrollback: Shift+PgUp with nothing kept says so and changes nothing"
T=""
for i in $(seq -w 1 40); do T+="echo line$i{ret}"; done
sb_text "$T{shift+pgup}"
if sb_screen 1 --wait '^console: scrolled back: ' 1 --row 24 "$IND" --attr 24 0x70; then
    pass "[0.9.2] scrollback: the VGA screen shows the indicator on row 24, every cell attribute 0x70"
else
    fail "[0.9.2] scrollback: indicator not on row 24 in reverse colours (see $OUT/screen-1.txt)"
fi
L1=$(sb_last)
X1=$(sed -E 's/.*line ([0-9]+) of.*/\1/' <<< "$L1"); Y1=$(sed -E 's/.* of ([0-9]+),.*/\1/' <<< "$L1")
TOP1=$(sed -E "s/.*, top '(.*)'$/\1/" <<< "$L1")
ROW0=$(sed -n 's/^row 00|\(.*\)|$/\1/p' "$OUT/screen-1.txt" | cut -c1-40)
echo "      $L1"
if [ -n "$TOP1" ] && [ "$ROW0" = "$TOP1" ]; then pass "[0.9.2] scrollback: row 0 on the screen is the logged top line ('$TOP1')"
else fail "[0.9.2] scrollback: row 0 '$ROW0' differs from the logged top '$TOP1'"; fi
if [ "${X1:-0}" -eq $(( ${Y1:-0} - 48 )) ]; then pass "[0.9.2] scrollback: first page = the 24 lines above the screen (X $X1 = Y $Y1 - 48)"
else fail "[0.9.2] scrollback: X $X1, Y $Y1 (expected X = Y - 48)"; fi
SCROLLED=$(awk '/^console: scrolled back: / { exit } /^console: scrolled [0-9]+ lines$/ { s += $3 } END { print s + 0 }' "$LOG")
if [ "$(( ${Y1:-0} - 25 ))" -eq "$SCROLLED" ]; then pass "[0.9.2] scrollback: every line that scrolled off is kept (Y - 25 = $SCROLLED)"
else fail "[0.9.2] scrollback: Y - 25 = $(( ${Y1:-0} - 25 )) but $SCROLLED lines scrolled off"; fi

sb_keys "shift+pgup,shift,shift+pgdn"
sb_screen 2 --wait '^console: scrolled back: ' 3 --row 24 "$IND"
X2=$(grep '^console: scrolled back: ' "$LOG" | sed -n 2p | sed -E 's/.*line ([0-9]+) of.*/\1/')
X3=$(grep '^console: scrolled back: ' "$LOG" | sed -n 3p | sed -E 's/.*line ([0-9]+) of.*/\1/')
if [ "${X2:-0}" -eq $(( ${X1:-0} - 24 )) ]; then pass "[0.9.2] scrollback: a second Shift+PgUp moves 24 lines back (line $X2)"
else fail "[0.9.2] scrollback: second Shift+PgUp went to line $X2 (from $X1)"; fi
if [ "${X3:-0}" -eq "${X1:-1}" ] && ! grep -q '^console: back at the bottom$' "$LOG"; then
    pass "[0.9.2] scrollback: Shift alone stays scrolled back; Shift+PgDn moves 24 lines forward (line $X3)"
else
    fail "[0.9.2] scrollback: after Shift and Shift+PgDn: line $X3, back at the bottom $(grep -c 'back at the bottom' "$LOG") times"
fi

sb_keys "a"
if sb_screen 3 --wait '^console: back at the bottom$' 1 --not-row 24 'scrolled back' --row 24 '^madire> a$'; then
    pass "[0.9.2] scrollback: another key returns to the live screen and is typed (row 24: 'madire> a')"
else
    fail "[0.9.2] scrollback: not back at the bottom after 'a' (see $OUT/screen-3.txt)"
fi
B=$(sb_back)
sb_keys "backspace,pgup,pgdn"
sb_text "echo after-pgup{ret}"
sb_screen 4 --wait '^after-pgup$' 1
if [ "$(sb_back)" -eq "$B" ] && [ "$(grep -c '^console: scrollback is empty$' "$LOG")" -eq 1 ]; then
    pass "[0.9.2] scrollback: plain PgUp/PgDn at the bottom do nothing"
else
    fail "[0.9.2] scrollback: plain PgUp/PgDn changed the view"
fi

sb_keys "$(printf 'shift+pgup,%.0s' $(seq 1 45))shift+pgup"
sb_screen 5 --wait '^console: scrolled back: line 1 of ' 1 --row 24 '^-- scrolled back: line 1 of '
if grep -q '^console: scrolled back: line 1 of ' "$LOG" && ! grep -q '^console: scrolled back: line 0 ' "$LOG"; then
    pass "[0.9.2] scrollback: paging back stops at line 1"
else
    fail "[0.9.2] scrollback: paging back did not stop at line 1"
fi

B=$(sb_back)
sb_text "a{bs}clear{ret}{shift+pgup}"
sb_screen 6 --wait '^console: scrolled back: ' $((B + 1)) --row 24 "$IND"
if grep -q '^row [0-9][0-9]|madire> clear|$' "$OUT/screen-6.txt" && grep -q '^row [0-9][0-9]|line40|$' "$OUT/screen-6.txt"; then
    pass "[0.9.2] clear keeps the screen: Shift+PgUp after clear shows 'madire> clear' and the lines before it"
else
    fail "[0.9.2] clear: the lines on screen before clear are not in the scrollback (see $OUT/screen-6.txt)"
fi

B=$(sb_back)
sb_text "a{bs}dmesg 9999{ret}dmesg 9999{ret}dmesg 9999{ret}dmesg 9999{ret}{shift+pgup}"
if sb_screen 7 --wait '^console: scrolled back: ' $((B + 1)) --row 24 ' of 1025 \(' \
        && [ "$(sb_last | sed -E 's/.* of ([0-9]+),.*/\1/')" = 1025 ]; then
    pass "[0.9.2] scrollback holds at most 1000 lines: Y = 1025 (1000 + 25) after 4 x 'dmesg 9999'"
else
    fail "[0.9.2] scrollback: Y is '$(sb_last | sed -E 's/.* of ([0-9]+),.*/\1/')', expected 1025"
fi
sb_text "a{bs}echo end-of-scroll-test{ret}"
sb_screen 8 --wait '^end-of-scroll-test$' 1
kill "$QPID" 2>/dev/null
wait "$QPID" 2>/dev/null
w=$(max_width "$LOG" "^console: started$" "^end-of-scroll-test$")
if grep -q "^end-of-scroll-test$" "$LOG" && [ "$w" -le 79 ]; then
    pass "[0.9.2] scrollback: every console line at most 79 characters (longest $w)"
else
    fail "[0.9.2] scrollback: a console line of $w characters (limit 79), or the session did not finish"
fi
reject "$LOG" "PANIC|events dropped|Screen output from thread|^key: " \
                                                      "[0.9.2] scrollback: no panic, dropped key, screen-rule violation or key event"

# ---- 0.10: user mode, part 1 (CPU set-up; no program runs yet) ----

# The descriptor checks decode the logged values here, independently of the
# kernel's own decoding (bit 47 present, 45-46 DPL, 44 S, 43 code, 41 R/W,
# 53 L, 54 D).
echo "== [0.10] GDT, IDT and TSS for user mode (normal boot)"
LOG=$OUT/serial-boot.log
GDTL=$(grep -m1 -E '^GDT: [0-9]+ entries' "$LOG")
echo "      ${GDTL:-no GDT line}"
read -r GP GD GC <<< "$(grep -oE '=0x[0-9a-f]{16}' <<< "$GDTL" | cut -c2- | tr '\n' ' ')"
if grep -qE '^GDT: 8 entries \(limit 0x003f\);' <<< "$GDTL"; then
    pass "[0.10] GDT: 8 entries, limit 0x3F (read back with sgdt)"
else
    fail "[0.10] GDT size: ${GDTL:-missing}"
fi
if [ -n "${GP:-}" ] && [ $(( GP )) -eq 0 ]; then pass "[0.10] GDT entry 5 (0x28) is all zero"
else fail "[0.10] GDT entry 5 (0x28): ${GP:-missing}"; fi
if [ -n "${GD:-}" ] && [ $(( (GD >> 47) & 1 )) -eq 1 ] && [ $(( (GD >> 45) & 3 )) -eq 3 ] &&
   [ $(( (GD >> 44) & 1 )) -eq 1 ] && [ $(( (GD >> 43) & 1 )) -eq 0 ] && [ $(( (GD >> 41) & 1 )) -eq 1 ]; then
    pass "[0.10] GDT 0x30: user data segment (present, DPL 3, writable data) $GD"
else
    fail "[0.10] GDT 0x30 not a ring-3 data segment (${GD:-missing})"
fi
if [ -n "${GC:-}" ] && [ $(( (GC >> 47) & 1 )) -eq 1 ] && [ $(( (GC >> 45) & 3 )) -eq 3 ] &&
   [ $(( (GC >> 43) & 1 )) -eq 1 ] && [ $(( (GC >> 53) & 1 )) -eq 1 ] && [ $(( (GC >> 54) & 1 )) -eq 0 ]; then
    pass "[0.10] GDT 0x38: user code segment (present, DPL 3, code, L=1 D=0) $GC"
else
    fail "[0.10] GDT 0x38 not a ring-3 64-bit code segment (${GC:-missing})"
fi
expect "$LOG" "^IDT: IST1 on vector 8; IST2 on vectors 2, 18; 0 other vectors on an IST; the rest on the current stack$" \
                                                      "[0.10] IDT: double fault on IST1, NMI and machine check on IST2, nothing else (read back with sidt)"
TSSL=$(grep -m1 -E '^TSS: RSP0=' "$LOG")
read -r R0 I1 I2 <<< "$(grep -oE '=0x[0-9a-f]{16}' <<< "$TSSL" | cut -c2- | tr '\n' ' ')"
STOP=$(grep -m1 -oE '^Kernel stack: 0x[0-9a-f]{16}-0x[0-9a-f]{16}' "$LOG" | grep -oE '0x[0-9a-f]{16}$')
if [ -n "${I2:-}" ] && [ $(( I1 )) -ne 0 ] && [ $(( I2 )) -ne 0 ] && [ "$I1" != "$I2" ] &&
   [ $(( I1 & 15 )) -eq 0 ] && [ $(( I2 & 15 )) -eq 0 ]; then
    pass "[0.10] TSS: IST1 and IST2 are two different 16-byte-aligned stacks ($I1, $I2)"
else
    fail "[0.10] TSS IST stacks: ${TSSL:-missing}"
fi
if [ -n "${R0:-}" ] && [ -n "$STOP" ] && [ $(( R0 )) -eq $(( STOP )) ]; then
    pass "[0.10] TSS: RSP0 at boot is thread 0's stack top ($R0, as the stack line reports)"
else
    fail "[0.10] TSS RSP0 ${R0:-missing}, boot stack top ${STOP:-missing}"
fi

echo "== [0.10] SMEP, SMAP, CR0.EM and the freeze (default QEMU CPU: no SMEP/SMAP)"
expect "$LOG" "^SMEP: NOT APPLICABLE \(not supported by this CPU\)$" "[0.10] default CPU: SMEP not applicable"
expect "$LOG" "^SMAP: NOT APPLICABLE \(not supported by this CPU\)$" "[0.10] default CPU: SMAP not applicable"
expect "$LOG" "^CR0.EM: set: x87, MMX and SSE instructions fault \(the kernel uses none\)$" "[0.10] CR0.EM reported set"
UCPU=$(grep -m1 -E '^User-mode CPU set-up: CR0=0x[0-9a-f]{8} CR4=0x[0-9a-f]{16}$' "$LOG")
read -r UC0 UC4 <<< "$(grep -oE '=0x[0-9a-f]+' <<< "$UCPU" | cut -c2- | tr '\n' ' ')"
if [ -n "${UC4:-}" ] && [ $(( UC0 & 0x4 )) -ne 0 ] && [ $(( UC4 & (3 << 20) )) -eq 0 ]; then
    pass "[0.10] default CPU: CR0.EM set, CR4.SMEP and CR4.SMAP clear (bits checked by the script)"
else
    fail "[0.10] default CPU control registers: ${UCPU:-missing}"
fi
expect "$LOG" "^Kernel half frozen: PML4 slots 256, 507, 508, 509, 510, 511 \(6 entries\); every process shares them, a new one now panics$" \
                                                      "[0.10] kernel half frozen at boot: slots 256, 507-511"

LOG=$OUT/serial-clac.log
boot "$LOG" 15 -m 32M -kernel "$KERNEL" -append "test=clac"
expect "$LOG" "^clac test: SMAP off; [0-9]+ interrupts taken with AC=1 in the interrupted code; AC=1 seen by the handler [0-9]+ times; clac executed 0 times$" \
                                                      "[0.10] default CPU: clac never executed (no #UD without SMAP)"
expect "$LOG" "^clac test: PASS$"                     "[0.10] default CPU: clac test PASS"
reject "$LOG" "PANIC|Invalid Opcode"                  "[0.10] default CPU: clac test without a panic or #UD"

echo "== [0.10] SMEP and SMAP on (-cpu qemu64,+smep,+smap)"
LOG=$OUT/serial-smap.log
boot "$LOG" 15 -m 32M -cpu qemu64,+smep,+smap -kernel "$KERNEL"
expect "$LOG" "^SMEP: enabled \(CR4.SMEP=1\): the kernel cannot execute user pages$" "[0.10] SMEP CPU: SMEP enabled"
expect "$LOG" "^SMAP: enabled \(CR4.SMAP=1\): clac on every kernel entry$" "[0.10] SMAP CPU: SMAP enabled"
UCPU=$(grep -m1 -E '^User-mode CPU set-up: CR0=0x[0-9a-f]{8} CR4=0x[0-9a-f]{16}$' "$LOG")
read -r UC0 UC4 <<< "$(grep -oE '=0x[0-9a-f]+' <<< "$UCPU" | cut -c2- | tr '\n' ' ')"
if [ -n "${UC4:-}" ] && [ $(( UC0 & 0x4 )) -ne 0 ] && [ $(( UC4 & (3 << 20) )) -eq $(( 3 << 20 )) ]; then
    pass "[0.10] SMEP/SMAP CPU: CR0.EM, CR4.SMEP and CR4.SMAP set (bits checked by the script)"
else
    fail "[0.10] SMEP/SMAP CPU control registers: ${UCPU:-missing}"
fi
expect "$LOG" "^Scheduler self-test: PASS"            "[0.10] SMEP/SMAP CPU: scheduler self-test PASS (interrupts and switches with clac)"
expect "$LOG" "System ready\."                        "[0.10] SMEP/SMAP CPU: System ready"
reject "$LOG" "PANIC"                                 "[0.10] SMEP/SMAP CPU: no panic"
LOG=$OUT/serial-smap-clac.log
boot "$LOG" 15 -m 32M -cpu qemu64,+smep,+smap -kernel "$KERNEL" -append "test=clac"
CL=$(grep -m1 -E '^clac test: SMAP on; ' "$LOG")
echo "      ${CL:-no clac line}"
if grep -qE '^clac test: SMAP on; [0-9]+ interrupts taken with AC=1 in the interrupted code; AC=1 seen by the handler 0 times; clac executed [1-9][0-9]* times$' <<< "$CL"; then
    pass "[0.10] SMAP CPU: every handler ran with AC=0 and clac was executed"
else
    fail "[0.10] SMAP CPU: clac evidence missing (${CL:-no line})"
fi
expect "$LOG" "^clac test: PASS$"                     "[0.10] SMAP CPU: clac test PASS"

echo "== [0.10] SMEP and SMAP enforced: the kernel touches a user page (test=smep, test=smap)"
UVA=0x0000000000400000
LOG=$OUT/serial-smep.log
boot "$LOG" 8 -m 32M -cpu qemu64,+smep,+smap -kernel "$KERNEL" -append "test=smep"
expect "$LOG" "^SMEP test: user page $UVA \(present, user, executable: ret\); CR4.SMEP=1: the kernel calls it, this must fault$" \
                                                      "[0.10] smep: present, executable user page built and entered first"
expect "$LOG" "KERNEL PANIC"                          "[0.10] smep: crash screen shown"
expect "$LOG" "CPU exception 14: Page Fault \(#PF\)"  "[0.10] smep: exception 14"
expect "$LOG" "Error code: 0x00000011"                "[0.10] smep: error code 0x11 (present, instruction fetch, kernel mode)"
expect_in_order "$LOG" "[0.10] smep: decoded and named as a SMEP violation at the user page" \
    "^ Page fault: protection violation, read, kernel mode$" "^ Details: instruction fetch$" \
    "^ Cause: SMEP violation \(the kernel executed a user page\)$" "^ Address: $UVA$"
expect "$LOG" "RIP=$UVA  CS=0x0008"                   "[0.10] smep: RIP is the user page, CS the kernel's (ring 0)"
expect "$LOG" "CR2=$UVA"                              "[0.10] smep: CR2 is the fetched user address"
reject "$LOG" "NX violation"                          "[0.10] smep: not misnamed as an NX violation"
reject "$LOG" "System ready|without a fault"          "[0.10] smep: halted, the fetch was refused"
w=$(max_width "$LOG" "KERNEL PANIC" "System halted")
if grep -q "KERNEL PANIC" "$LOG" && [ "$w" -le 79 ]; then
    pass "[0.10] smep: crash screen lines at most 79 characters (longest $w)"
else
    fail "[0.10] smep: crash screen line of $w characters (limit 79), or no crash screen"
fi
UVA=0x0000000000500000
LOG=$OUT/serial-smapread.log
boot "$LOG" 8 -m 32M -cpu qemu64,+smep,+smap -kernel "$KERNEL" -append "test=smap"
expect "$LOG" "^SMAP test: user page $UVA \(present, user, writable, holds 0x5a5a5a5a5a5a5a5a\); RFLAGS.AC=0, CR4.SMAP=1: the kernel reads it, this must fault$" \
                                                      "[0.10] smap: present user data page built and entered first"
expect "$LOG" "KERNEL PANIC"                          "[0.10] smap: crash screen shown"
expect "$LOG" "CPU exception 14: Page Fault \(#PF\)"  "[0.10] smap: exception 14"
expect "$LOG" "Error code: 0x00000001"                "[0.10] smap: error code 0x01 (present, read, kernel mode)"
expect_in_order "$LOG" "[0.10] smap: decoded and named as a SMAP violation at the user page" \
    "^ Page fault: protection violation, read, kernel mode$" \
    "^ Cause: SMAP violation \(the kernel read a user page, AC=0\)$" "^ Address: $UVA$"
expect "$LOG" "CR2=$UVA"                              "[0.10] smap: CR2 is the user address read"
RF=$(grep -m1 -oE 'CS=0x0008  RFLAGS=0x[0-9a-f]{16}' "$LOG" | grep -oE '0x[0-9a-f]{16}$')
if [ -n "${RF:-}" ] && [ $(( RF & 0x40000 )) -eq 0 ]; then
    pass "[0.10] smap: faulted in ring 0 with RFLAGS.AC clear ($RF)"
else
    fail "[0.10] smap: ring-0 RFLAGS with AC clear not found (${RF:-missing})"
fi
reject "$LOG" "System ready|without a fault"          "[0.10] smap: halted, the read was refused"
w=$(max_width "$LOG" "KERNEL PANIC" "System halted")
if grep -q "KERNEL PANIC" "$LOG" && [ "$w" -le 79 ]; then
    pass "[0.10] smap: crash screen lines at most 79 characters (longest $w)"
else
    fail "[0.10] smap: crash screen line of $w characters (limit 79), or no crash screen"
fi
for t in smep smap; do
    T=$(tr 'a-z' 'A-Z' <<< "$t")
    LOG=$OUT/serial-$t-default.log
    boot "$LOG" 15 -m 32M -kernel "$KERNEL" -append "test=$t"
    expect "$LOG" "^$T test: NOT APPLICABLE \(not supported by this CPU\)$" \
                                                      "[0.10] $t on the default CPU: NOT APPLICABLE"
    expect "$LOG" "System ready\."                    "[0.10] $t on the default CPU: boots normally"
    reject "$LOG" "PANIC|$T test: user page"          "[0.10] $t on the default CPU: no access tried, no panic"
done

echo "== [0.10] RSP0 follows every thread switch (test=rsp0)"
thread_diag rsp0 15
RL=$(grep -m1 -E '^RSP0 test: [0-9]+ checks' "$LOG")
echo "      ${RL:-no RSP0 line}"
expect "$LOG" "^RSP0 test: [1-9][0-9]* checks in 4 threads after [0-9]+ switches \([0-9]+ preemptions\), 4 distinct RSP0 values, 0 mismatches$" \
                                                      "[0.10] rsp0: every check after a switch saw the running thread's own stack top"
expect "$LOG" "^RSP0 test: PASS$"                     "[0.10] rsp0: PASS (preempted and yielding threads)"
reject "$LOG" "PANIC"                                 "[0.10] rsp0: no panic"

echo "== [0.10] GDT selector past the end (test=gpflimit)"
LOG=$OUT/serial-gpflimit.log
boot "$LOG" 8 -kernel "$KERNEL" -append "test=gpflimit"
expect "$LOG" "KERNEL PANIC"                          "[0.10] gpflimit: crash screen shown"
expect "$LOG" "CPU exception 13: General Protection Fault" "[0.10] gpflimit: reports vector 13"
expect "$LOG" "Error code: 0x00000040"                "[0.10] gpflimit: error code 0x00000040 (selector 0x40, past the limit 0x3F)"
expect "$LOG" "RIP=0x[0-9a-f]{16}  CS=0x0008"         "[0.10] gpflimit: registers dumped, CS is our GDT code segment"
reject "$LOG" "System ready"                          "[0.10] gpflimit: halted instead of continuing"
w=$(max_width "$LOG" "KERNEL PANIC" "System halted")
if grep -q "KERNEL PANIC" "$LOG" && [ "$w" -le 79 ]; then
    pass "[0.10] gpflimit: crash screen lines at most 79 characters (longest $w)"
else
    fail "[0.10] gpflimit: crash screen line of $w characters (limit 79), or no crash screen"
fi

echo "== [0.10] The freeze: a new kernel-half PML4 entry panics (test=freezeviolation)"
LOG=$OUT/serial-freezeviolation.log
boot "$LOG" 8 -kernel "$KERNEL" -append "test=freezeviolation"
expect "$LOG" "KERNEL PANIC"                          "[0.10] freezeviolation: crash screen shown"
expect "$LOG" "Kernel-half PML4 entry created after the freeze: 0xffff808000000000 \(PML4 slot 257\)\." \
                                                      "[0.10] freezeviolation: reported with the address and slot"
reject "$LOG" "Freeze test: FAIL|System ready"        "[0.10] freezeviolation: the entry was not created, halted"

echo "== [0.10] Address spaces: build, switch, check, tear down (test=uaspace)"
thread_diag uaspace 30
grep -E "^Address-space test: " "$LOG" | grep -vE ': (PASS|FAIL)|built, entered and torn down\.\.\.' | sed 's/^/      /'
expect "$LOG" "^Address-space test: heap grew by [1-9][0-9]* KiB and [1-9][0-9]* page table\(s\) under a process CR3; the block read back under the kernel CR3 with 0 wrong bytes; kernel PML4 entries changed: 0: OK$" \
                                                      "[0.10] uaspace: heap growth under a process CR3 (a new heap page table) is seen under the kernel CR3; no PML4 entry changed"
expect "$LOG" "^Address-space test: 500 address spaces built, entered and checked by 2 threads \(13 user pages, 6 lower-half tables and a PML4 each\); 0 mapping and 0 content mismatches: OK$" \
                                                      "[0.10] uaspace: 500 address spaces built and checked through the recursive mapping (frames, user bit, permissions, contents)"
expect "$LOG" "^Address-space test: kernel half identical in every PML4, slot 510 its own: 0 wrong; upper-half entries with the user bit: 0 \(500 walks\): OK$" \
                                                      "[0.10] uaspace: kernel half identical in all 500 PML4s; no user bit anywhere in the upper half"
SW=$(grep -m1 -E '^Address-space test: switches in the middle: ' "$LOG")
if grep -qE '^Address-space test: switches in the middle: [1-9][0-9]* builds and [1-9][0-9]* teardowns preempted, [1-9][0-9]* yields; CR3 right after every switch: [1-9][0-9]* checks, 0 wrong \([0-9]+ preemptions\): OK$' <<< "$SW"; then
    pass "[0.10] uaspace: switches (preemptions and yields) in the middle of builds and teardowns; CR3 right after every one"
else
    fail "[0.10] uaspace: switch evidence missing (${SW:-no line})"
fi
TD=$(grep -m1 -E '^Address-space test: torn down bottom-up: ' "$LOG")
F0=$(sed -nE 's/.*free pages ([0-9]+) -> ([0-9]+),.*/\1/p' <<< "$TD")
F1=$(sed -nE 's/.*free pages ([0-9]+) -> ([0-9]+),.*/\2/p' <<< "$TD")
if grep -qE '^Address-space test: torn down bottom-up: 6500 user frames and 3000 lower-half tables freed, 0 wrong; .*: OK$' <<< "$TD" &&
   [ -n "$F0" ] && [ "$F0" = "$F1" ]; then
    pass "[0.10] uaspace: every user frame (6500) and lower-half table (3000) freed; free pages exactly restored ($F0 -> $F1)"
else
    fail "[0.10] uaspace: teardown or free pages wrong (${TD:-no line})"
fi
expect "$LOG" "^Address-space test: PASS$"            "[0.10] uaspace: PASS"
reject "$LOG" "PANIC"                                 "[0.10] uaspace: no panic"
expect "$LOG" "System ready\."                        "[0.10] uaspace: System ready afterwards"
LOG=$OUT/serial-uaspace-smap.log
boot "$LOG" 30 -m 32M -cpu qemu64,+smep,+smap -kernel "$KERNEL" -append "test=uaspace"
expect "$LOG" "^Address-space test: PASS$"            "[0.10] uaspace with SMEP and SMAP on: PASS (the kernel never touches a user page directly)"
reject "$LOG" "PANIC"                                 "[0.10] uaspace with SMEP and SMAP on: no panic"

echo "== [0.10] The first program in ring 3: spin, preempted by the timer (test=userspin)"
thread_diag userspin 30
grep -E "^User-mode test: " "$LOG" | grep -vE ': (PASS|FAIL)$' | sed 's/^/      /'
expect "$LOG" "^User-mode test: [1-9][0-9]* built-in images checked: ok; 'spin': [1-9][0-9]* bytes of code at 0x0000000000400000, entry 0x00000000004[0-9a-f]{5}, [0-9]+ bytes of data and bss: OK$" \
                                                      "[0.10] userspin: every built-in image checked; spin found"
expect "$LOG" "^User-mode test: thread [1-9][0-9]* 'spin' loaded the image into its own address space: [1-9][0-9]* code, [0-9]+ data and 8 stack pages: OK$" \
                                                      "[0.10] userspin: spin's code, data and 32 KiB stack mapped in its own address space"
expect "$LOG" "^User-mode test: IRETQ frame RIP=0x00000000004[0-9a-f]{5} CS=0x003b RFLAGS=0x0000000000000202 RSP=0x00007ffffffff000 SS=0x0033 \(IF=1, IOPL=0, RIP inside the code\): OK$" \
                                                      "[0.10] userspin: enter_user's IRETQ frame: CS 0x3B, SS 0x33, IF=1, IOPL=0, RIP inside the code"
FI=$(grep -m1 -E '^User-mode test: first timer interrupt from ring 3, ' "$LOG")
FAT=$(sed -nE 's/.*frame at 0x([0-9a-f]{16}) = RSP0 0x([0-9a-f]{16}) - 176, RSP0 = the thread.s stack top 0x([0-9a-f]{16}): OK$/\1 \2 \3/p' <<< "$FI")
read -r FA FR FT <<< "$FAT"
if grep -qE '^User-mode test: first timer interrupt from ring 3, in thread [1-9][0-9]*: CS=0x003b RIP=0x00000000004[0-9a-f]{5} RSP=0x00007ffffff[0-9a-f]{5} SS=0x0033 RFLAGS=0x0000000000000[2a][0-9a-f]{2}; ' <<< "$FI" &&
   [ -n "$FA" ] && [ $(( 0x$FA + 176 )) -eq $(( 0x$FR )) ] && [ "$FR" = "$FT" ] &&
   [ $(( 0x$FR )) -ge $(( 0xfffffd8000000000 )) ] && [ $(( 0x$FR )) -lt $(( 0xfffffe0000000000 )) ]; then
    pass "[0.10] userspin: a timer interrupt taken in ring 3 (CS 0x3B, user RIP and RSP) arrived on the thread's RSP0 stack (frame 0x$FA + 176 = 0x$FR, in slot 507)"
else
    fail "[0.10] userspin: first ring-3 timer interrupt wrong (${FI:-no line})"
fi
expect "$LOG" "^User-mode test: [1-9][0-9]+ timer interrupts from ring 3: 0 with CS not 0x3B, 0 with SS not 0x33, 0 with IF clear, 0 with IOPL set, 0 not on the thread's RSP0 stack, 0 in another thread; RIP 0x4[0-9a-f]{5}-0x4[0-9a-f]{5} \(inside the code\), RSP 0x7ffffff[0-9a-f]{5}-0x7ffffff[0-9a-f]{5} \(inside the stack\): OK$" \
                                                      "[0.10] userspin: every ring-3 timer interrupt checked (at least 10): all CS 0x3B, SS 0x33, IF=1, IOPL 0, on the RSP0 stack"
PR=$(grep -m1 -E '^User-mode test: spin preempted ' "$LOG")
if grep -qE '^User-mode test: spin preempted [0-9]+ times from ring 3 and back in ring 3 after a preemption [1-9][0-9]* times; [0-9]+ switches, [0-9]+ ticks run by spin; longest turn [0-6] ticks \(slice 5, at most 6\): OK$' <<< "$PR"; then
    pass "[0.10] userspin: spin preempted from ring 3 (at least 5 times), resumed in ring 3, longest turn at most 6 ticks"
else
    fail "[0.10] userspin: preemption evidence missing (${PR:-no line})"
fi
LT=$(sed -nE 's/.*; longest turn ([0-9]+) ticks .*/\1/p' <<< "$PR")
owner_check "userspin" "User-mode test" "$LT"
expect "$LOG" "^User-mode test: PASS$"                "[0.10] userspin: PASS"
reject "$LOG" "PANIC"                                 "[0.10] userspin: no panic"
expect "$LOG" "System ready\."                        "[0.10] userspin: System ready while spin keeps running"
LOG=$OUT/serial-userspin-smap.log
boot "$LOG" 30 -m 32M -cpu qemu64,+smep,+smap -kernel "$KERNEL" -append "test=userspin"
expect "$LOG" "^SMEP: enabled"                        "[0.10] userspin with SMEP and SMAP on: SMEP enabled"
expect "$LOG" "^User-mode test: PASS$"                "[0.10] userspin with SMEP and SMAP on: PASS"
reject "$LOG" "PANIC"                                 "[0.10] userspin with SMEP and SMAP on: no panic"

echo "== [0.10] The first process teardown: kill at the next return to ring 3 (test=userkill)"
thread_diag userkill 60
grep -E "^User kill test: " "$LOG" | grep -vE ': (PASS|FAIL)$|started and killed\.\.\.$' | sed 's/^/      /'
expect "$LOG" "^User kill test: 20 rounds of two spin processes: 60 started \(PIDs 4-63, one more each time, none reused\), 40 had returned to ring 3 before the kill; ended at a return to ring 3: 40 results 'killed' \(code 0\), 0 wrong: OK$" \
                                                      "[0.10] userkill: 40 processes killed after running in ring 3, each ended at a return to ring 3; PIDs 4-63 (after the boot self-test's 1-3) never reused"
expect "$LOG" "^User kill test: the second process kept running in ring 3 after the first one ended: 20 of 20 rounds: OK$" \
                                                      "[0.10] userkill: killing one process leaves the other running in ring 3"
expect "$LOG" "^User kill test: 20 processes killed right after they were started \([0-9]+ before entering ring 3\): 20 results 'killed', 0 wrong: OK$" \
                                                      "[0.10] userkill: 20 processes killed right after the start (before ring 3) end cleanly"
TD=$(grep -m1 -E '^User kill test: [0-9]+ address spaces torn down' "$LOG")
F0=$(sed -nE 's/.*free pages ([0-9]+) -> ([0-9]+),.*/\1/p' <<< "$TD")
F1=$(sed -nE 's/.*free pages ([0-9]+) -> ([0-9]+),.*/\2/p' <<< "$TD")
if grep -qE '^User kill test: 60 address spaces torn down by their own threads: 540 user frames and 360 lower-half tables freed, 0 wrong; .*processes left 0: OK$' <<< "$TD" &&
   [ -n "$F0" ] && [ "$F0" = "$F1" ]; then
    pass "[0.10] userkill: 60 address spaces torn down by their own threads (540 frames, 360 tables); free pages exactly restored ($F0 -> $F1)"
else
    fail "[0.10] userkill: teardown or free pages wrong (${TD:-no line})"
fi
expect "$LOG" "^User kill test: longest turn [0-6] ticks \(slice 5, at most 6\): OK$" \
                                                      "[0.10] userkill: longest turn at most 6 ticks"
LT=$(grep -m1 -E "^User kill test: longest turn [0-9]+ ticks" "$LOG" | sed -nE 's/.*longest turn ([0-9]+) ticks.*/\1/p')
owner_check "userkill" "User kill test" "$LT"
expect "$LOG" "^User kill test: PASS$"                "[0.10] userkill: PASS"
reject "$LOG" "PANIC"                                 "[0.10] userkill: no panic"
expect "$LOG" "System ready\."                        "[0.10] userkill: System ready afterwards"
LOG=$OUT/serial-userkill-smap.log
boot "$LOG" 60 -m 32M -cpu qemu64,+smep,+smap -kernel "$KERNEL" -append "test=userkill"
expect "$LOG" "^User kill test: PASS$"                "[0.10] userkill with SMEP and SMAP on: PASS"
reject "$LOG" "PANIC"                                 "[0.10] userkill with SMEP and SMAP on: no panic"

echo "== [0.10] Fault containment, part 1: programs that make a mistake end alone (test=userfault)"
# Booted with the console session afterwards: version, then an end marker.
LOG=$OUT/serial-userfault.log
boot_text "$LOG" 60 "System ready." "x{ret}version{ret}echo end-of-fault-test{ret}" 0.05 \
    $'\nend-of-fault-test\n' -m 32M -kernel "$KERNEL" -append "test=userfault"
grep -E "^(User fault test: |Process [0-9]+ )" "$LOG" | grep -vE ': (PASS|FAIL)$|must end alone\.\.\.$' | sed 's/^/      /'
URIP='at RIP 0x00000000004[0-9a-f]{5}'
expect "$LOG" "^Process [0-9]+ 'nullptr' ended by CPU exception 14 \(#PF Page Fault\) $URIP: address 0x0000000000000000, not present, read, user mode; the kernel continues$" \
                                                      "[0.10] userfault: nullptr ended by #PF reading address 0 (one-line message)"
expect "$LOG" "^Process [0-9]+ 'ud2' ended by CPU exception 6 \(#UD Invalid Opcode\) $URIP, error code 0x0; the kernel continues$" \
                                                      "[0.10] userfault: ud2 ended by #UD (one-line message)"
expect "$LOG" "^Process [0-9]+ 'stackover' ended by CPU exception 14 \(#PF Page Fault\) $URIP: address 0x00007fffffff6[0-9a-f]{3}, not present, write, user mode; the kernel continues$" \
                                                      "[0.10] userfault: stackover ended by #PF in the page below its 32 KiB stack (one-line message)"
expect "$LOG" "^Process [0-9]+ 'wx' ended by CPU exception 14 \(#PF Page Fault\) $URIP: address 0x00000000004[0-9a-f]{5}, protection violation, write, user mode; the kernel continues$" \
                                                      "[0.10] userfault: wx 0 ended by #PF writing its own code (one-line message)"
expect "$LOG" "^Process [0-9]+ 'wx' ended by CPU exception 14 \(#PF Page Fault\) at RIP (0x00000000005000[0-9a-f]{2}): address \1, protection violation, read, user mode, instruction fetch \(NX violation\); the kernel continues$" \
                                                      "[0.10] userfault: wx 1 ended by #PF (NX) executing its own data (one-line message)"
for c in "nullptr 0" "ud2 0" "stackover 0" "wx 0" "wx 1"; do
    expect "$LOG" "^User fault test: $c \(.*\): process [0-9]+ killed by a CPU exception: #(PF|UD) at RIP .*: OK$" \
                                                      "[0.10] userfault: $c: result 'killed by a CPU exception' with its vector, fault details checked"
done
n=$(grep -cE "^User fault test: .*, instruction ([0-9a-f]{2}( [0-9a-f]{2})*) \(expected \1\), .*: OK$" "$LOG")
if [ "$n" -eq 5 ]; then pass "[0.10] userfault: each of the 5 faults came from its expected instruction (bytes at RIP, read with copy_from_user)"
else fail "[0.10] userfault: $n of 5 faults at their expected instruction"; fi
expect "$LOG" "^User fault test: spin kept running in ring 3 through the faults, then was killed: OK$" \
                                                      "[0.10] userfault: another process kept running in ring 3 through every fault"
TD=$(grep -m1 -E '^User fault test: [0-9]+ address spaces torn down' "$LOG")
F0=$(sed -nE 's/.*free pages ([0-9]+) -> ([0-9]+),.*/\1/p' <<< "$TD")
F1=$(sed -nE 's/.*free pages ([0-9]+) -> ([0-9]+),.*/\2/p' <<< "$TD")
if grep -qE '^User fault test: 6 address spaces torn down by their own threads, 0 wrong; .*processes left 0: OK$' <<< "$TD" &&
   [ -n "$F0" ] && [ "$F0" = "$F1" ]; then
    pass "[0.10] userfault: free pages ($F0 -> $F1), page tables, stack slots, threads and process table exactly restored"
else
    fail "[0.10] userfault: teardown or free pages wrong (${TD:-no line})"
fi
expect "$LOG" "^User fault test: PASS$"               "[0.10] userfault: PASS"
reject "$LOG" "PANIC"                                 "[0.10] userfault: no crash screen, no panic"
expect "$LOG" "System ready\."                        "[0.10] userfault: System ready afterwards"
expect_next "$LOG" "[0.10] userfault: the console still works afterwards (version)" \
    "^madire> version$" "^Madire OS v$VRE \(64-bit x86-64, Multiboot 1\)$"
LOG=$OUT/serial-userfault-smap.log
boot "$LOG" 60 -m 32M -cpu qemu64,+smep,+smap -kernel "$KERNEL" -append "test=userfault"
expect "$LOG" "^User fault test: PASS$"               "[0.10] userfault with SMEP and SMAP on: PASS"
reject "$LOG" "PANIC"                                 "[0.10] userfault with SMEP and SMAP on: no crash screen, no panic"
expect "$LOG" "System ready\."                        "[0.10] userfault with SMEP and SMAP on: System ready afterwards"

echo "== [0.10] Fault containment, part 2: programs that try what ring 3 may not (test=userpriv)"
LOG=$OUT/serial-userpriv.log
boot_text "$LOG" 90 "System ready." "x{ret}version{ret}echo end-of-priv-test{ret}" 0.05 \
    $'\nend-of-priv-test\n' -m 32M -kernel "$KERNEL" -append "test=userpriv"
grep -E "^(User privilege test: |Process [0-9]+ )" "$LOG" | grep -vE ': (PASS|FAIL)$|may not\.\.\.$' | sed 's/^/      /'
GPMSG="^Process [0-9]+ 'privileged' ended by CPU exception 13 \(#GP General Protection Fault\) $URIP, error code 0x0; the kernel continues$"
n=$(grep -cE "$GPMSG" "$LOG")
if [ "$n" -eq 9 ]; then pass "[0.10] userpriv: 9 privileged instructions, each ended its process by #GP(0) (one-line messages)"
else fail "[0.10] userpriv: $n of 9 privileged processes ended by #GP(0)"; fi
for a in "0 cli" "1 hlt" "2 out" "3 in" "4 mov from cr3" "5 lgdt" "6 wrmsr" "7 rdmsr" "8 invlpg"; do
    expect "$LOG" "^User privilege test: privileged ${a%% *} \(${a#* }.*\): process [0-9]+ killed by a CPU exception: #GP at RIP .*, error code 0x0 \(result code 13\): OK$" \
                                                      "[0.10] userpriv: ${a#* } in ring 3: #GP, process ended"
done
for g in "3 0x1a int3" "0x80 0x402 int 0x80" "0x20 0x102 int 0x20"; do
    read -r gate gerr gname <<< "$g"
    expect "$LOG" "^Process [0-9]+ 'int80' ended by CPU exception 13 \(#GP General Protection Fault\) $URIP, error code $gerr; the kernel continues$" \
                                                      "[0.10] userpriv: $gname to a ring-0 gate: #GP with error code $gerr (gate $gate), process ended"
done
expect "$LOG" "^Process [0-9]+ 'fpu' ended by CPU exception 7 \(#NM Device Not Available\) $URIP, error code 0x0; the kernel continues$" \
                                                      "[0.10] userpriv: x87 with CR0.EM set: #NM, process ended"
n=$(grep -cE "^Process [0-9]+ 'fpu' ended by CPU exception 6 \(#UD Invalid Opcode\) $URIP, error code 0x0; the kernel continues$" "$LOG")
if [ "$n" -eq 2 ]; then pass "[0.10] userpriv: MMX and SSE with CR0.EM set: #UD, processes ended"
else fail "[0.10] userpriv: $n of 2 MMX/SSE processes ended by #UD"; fi
n=$(grep -cE "^User privilege test: (privileged|int80|fpu) [0-9] \(.*\): process [0-9]+ killed by a CPU exception: .*: OK$" "$LOG")
if [ "$n" -eq 15 ]; then pass "[0.10] userpriv: all 15 results 'killed by a CPU exception' with the right vector and error code"
else fail "[0.10] userpriv: $n of 15 results right"; fi
n=$(grep -cE "^User privilege test: .*, instruction ([0-9a-f]{2}( [0-9a-f]{2})*) \(expected \1\), .*: OK$" "$LOG")
if [ "$n" -eq 15 ]; then pass "[0.10] userpriv: each of the 15 faults came from its expected instruction (bytes at RIP, read with copy_from_user)"
else fail "[0.10] userpriv: $n of 15 faults at their expected instruction"; fi
expect "$LOG" "^User privilege test: spin kept running in ring 3 through the faults, then was killed: OK$" \
                                                      "[0.10] userpriv: another process kept running in ring 3 through every fault"
TD=$(grep -m1 -E '^User privilege test: [0-9]+ address spaces torn down' "$LOG")
F0=$(sed -nE 's/.*free pages ([0-9]+) -> ([0-9]+),.*/\1/p' <<< "$TD")
F1=$(sed -nE 's/.*free pages ([0-9]+) -> ([0-9]+),.*/\2/p' <<< "$TD")
if grep -qE '^User privilege test: 16 address spaces torn down by their own threads, 0 wrong; .*processes left 0: OK$' <<< "$TD" &&
   [ -n "$F0" ] && [ "$F0" = "$F1" ]; then
    pass "[0.10] userpriv: free pages ($F0 -> $F1), page tables, stack slots, threads and process table exactly restored"
else
    fail "[0.10] userpriv: teardown or free pages wrong (${TD:-no line})"
fi
expect "$LOG" "^User privilege test: PASS$"           "[0.10] userpriv: PASS"
reject "$LOG" "PANIC"                                 "[0.10] userpriv: no crash screen, no panic"
expect "$LOG" "System ready\."                        "[0.10] userpriv: System ready afterwards"
expect_next "$LOG" "[0.10] userpriv: the console still works afterwards (version)" \
    "^madire> version$" "^Madire OS v$VRE \(64-bit x86-64, Multiboot 1\)$"
LOG=$OUT/serial-userpriv-smap.log
boot "$LOG" 60 -m 32M -cpu qemu64,+smep,+smap -kernel "$KERNEL" -append "test=userpriv"
expect "$LOG" "^User privilege test: PASS$"           "[0.10] userpriv with SMEP and SMAP on: PASS"
reject "$LOG" "PANIC"                                 "[0.10] userpriv with SMEP and SMAP on: no crash screen, no panic"
expect "$LOG" "System ready\."                        "[0.10] userpriv with SMEP and SMAP on: System ready afterwards"

echo "== [0.10] Fault containment, part 3: sti, int 0x0e and kernel memory (test=userkmem)"
LOG=$OUT/serial-userkmem.log
boot_text "$LOG" 90 "System ready." "x{ret}version{ret}echo end-of-kmem-test{ret}" 0.05 \
    $'\nend-of-kmem-test\n' -m 32M -kernel "$KERNEL" -append "test=userkmem"
grep -E "^(User kernel-memory test: |Process [0-9]+ )" "$LOG" | grep -vE ': (PASS|FAIL)$|for the kernel\.\.\.$' | sed 's/^/      /'
expect "$LOG" "^Process [0-9]+ 'privileged' ended by CPU exception 13 \(#GP General Protection Fault\) $URIP, error code 0x0; the kernel continues$" \
                                                      "[0.10] userkmem: sti in ring 3: #GP(0), process ended (one-line message)"
expect "$LOG" "^Process [0-9]+ 'int80' ended by CPU exception 13 \(#GP General Protection Fault\) $URIP, error code 0x72; the kernel continues$" \
                                                      "[0.10] userkmem: int 0x0e to a ring-0 gate: #GP with error code 0x72 (gate 0x0e), process ended"
for k in "kread ffffffff80100000 read kernel text" "kread fffffe8000000000 read kernel data (heap)" "kread ffffff7fbfdfe000 read the PML4" \
         "kwrite ffffffff80100000 write kernel text" "kwrite fffffe8000000000 write kernel data (heap)" "kwrite ffffff7fbfdfe000 write the PML4"; do
    read -r kp ka kacc kwhat <<< "$k"
    expect "$LOG" "^Process [0-9]+ '$kp' ended by CPU exception 14 \(#PF Page Fault\) $URIP: address 0x$ka, protection violation, $kacc, user mode; the kernel continues$" \
                                                      "[0.10] userkmem: $kp: $kacc of $kwhat (0x$ka): #PF, protection violation, user mode; process ended"
done
n=$(grep -cE "^User kernel-memory test: (privileged|int80|kread|kwrite) [0-9] \(.*\): process [0-9]+ killed by a CPU exception: .*: OK$" "$LOG")
if [ "$n" -eq 8 ]; then pass "[0.10] userkmem: all 8 results 'killed by a CPU exception' with the right vector, error code and address"
else fail "[0.10] userkmem: $n of 8 results right"; fi
n=$(grep -cE "^User kernel-memory test: .*, instruction ([0-9a-f]{2}( [0-9a-f]{2})*) \(expected \1\), .*: OK$" "$LOG")
if [ "$n" -eq 8 ]; then pass "[0.10] userkmem: each of the 8 faults came from its expected instruction (bytes at RIP, read with copy_from_user)"
else fail "[0.10] userkmem: $n of 8 faults at their expected instruction"; fi
KCS=$(grep -m1 -E '^User kernel-memory test: kernel text and read-only data' "$LOG")
if grep -qE 'checksum (0x[0-9a-f]{16}) before, \1 after; kwrite.s value found at 0 of its 3 targets: OK$' <<< "$KCS"; then
    pass "[0.10] userkmem: kernel text, read-only data, kernel PML4 and heap header checksum identical before and after; kwrite's value nowhere"
else
    fail "[0.10] userkmem: kernel memory changed (${KCS:-no line})"
fi
expect "$LOG" "^User kernel-memory test: spin kept running in ring 3 through the faults, then was killed: OK$" \
                                                      "[0.10] userkmem: another process kept running in ring 3 through every fault"
TD=$(grep -m1 -E '^User kernel-memory test: [0-9]+ address spaces torn down' "$LOG")
F0=$(sed -nE 's/.*free pages ([0-9]+) -> ([0-9]+),.*/\1/p' <<< "$TD")
F1=$(sed -nE 's/.*free pages ([0-9]+) -> ([0-9]+),.*/\2/p' <<< "$TD")
if grep -qE '^User kernel-memory test: 9 address spaces torn down by their own threads, 0 wrong; .*processes left 0: OK$' <<< "$TD" &&
   [ -n "$F0" ] && [ "$F0" = "$F1" ]; then
    pass "[0.10] userkmem: free pages ($F0 -> $F1), page tables, stack slots, threads and process table exactly restored"
else
    fail "[0.10] userkmem: teardown or free pages wrong (${TD:-no line})"
fi
expect "$LOG" "^User kernel-memory test: PASS$"       "[0.10] userkmem: PASS"
reject "$LOG" "PANIC"                                 "[0.10] userkmem: no crash screen, no panic"
expect "$LOG" "System ready\."                        "[0.10] userkmem: System ready afterwards"
expect_next "$LOG" "[0.10] userkmem: the console still works afterwards (version)" \
    "^madire> version$" "^Madire OS v$VRE \(64-bit x86-64, Multiboot 1\)$"
LOG=$OUT/serial-userkmem-smap.log
boot "$LOG" 60 -m 32M -cpu qemu64,+smep,+smap -kernel "$KERNEL" -append "test=userkmem"
expect "$LOG" "^User kernel-memory test: PASS$"       "[0.10] userkmem with SMEP and SMAP on: PASS"
reject "$LOG" "PANIC"                                 "[0.10] userkmem with SMEP and SMAP on: no crash screen, no panic"
expect "$LOG" "System ready\."                        "[0.10] userkmem with SMEP and SMAP on: System ready afterwards"

# ---- 0.12 (stage 1): help sections, Did you mean, prompt, alias ----

echo "== [0.12] Console: help sections, Did you mean, prompt, alias and unalias"
# The harness never waits for the prompt: it waits for "System ready.",
# types the keys at a fixed pace and waits for the final echo's output; the
# checks find each typed line by the prompt the console logs in front of it.
# The prompt is changed only between "prompt abc>" and "prompt --reset" in
# this boot of its own, so every other line here and in every other boot is
# logged after "madire> ".
LOG=$OUT/serial-shell012.log
# About 840 keys: QEMU 8.2 queues each key's release behind its hold time,
# and over a session this long without pauses the queue fell behind and
# releases were lost from about the 360th key (a Shift stayed down; the
# same on the stage 0c kernel, so not the kernel). The {wait:4} pauses let
# it drain: with them all 848 presses and 848 releases arrived.
X58=$(printf 'x%.0s' $(seq 1 58))
S="x{wait:1}"
S+="help{ret}help shell{ret}help system{ret}help programs{ret}"
S+="help shel{ret}help nosuch{ret}"
S+="hlep{ret}ru{ret}Echo x{ret}ECHO x{ret}kil 5{ret}"
S+="prompt{ret}prompt abc>{ret}echo under-new-prompt{ret}prompt{ret}"
S+="prompt 123456789{ret}prompt -x{ret}prompt a b{ret}prompt --reset{ret}prompt{ret}"
S+="{wait:4}alias{ret}alias ll echo hi there{ret}alias ll{ret}ll{ret}ll more{ret}"
S+="alias ll echo replaced{ret}ll{ret}"
S+="alias echo echo x{ret}alias zz ll{ret}"
S+="{wait:4}alias abcdefghijklmnop echo x{ret}alias abcdefghijklmno echo 15{ret}abcdefghijklmno{ret}"
S+="alias Bad echo x{ret}alias v echo ${X58}{ret}alias v{ret}"
S+="{wait:4}unalias nosuch{ret}unalias{ret}"
S+="alias e5 echo 1 2 3 4 5{ret}e5 6 7{ret}e5 6 7 8{ret}lll{ret}"
S+="{wait:4}"
for i in 02 03 04 05 06 07 08 09 10 11 12 13; do S+="alias t$i echo $i{ret}"; done
# The last "alias " has a trailing space, so its logged line is unique
# (the parser ignores the space; expect_lines starts at the first match).
S+="{wait:4}alias t14 echo 14{ret}unalias t13{ret}alias t14 echo 14{ret}t14{ret}alias {ret}"
S+="echo end-of-012-test{ret}"
boot_text "$LOG" 180 "System ready." "$S" 0.03 $'\nend-of-012-test\n' -kernel "$KERNEL"

NEXT="^(console: |madire> )"
expect_lines "$LOG" "[0.12] help: today's list unchanged, then prompt, alias, unalias, the stage 2 commands and the sections" \
    "^madire> help$" "^Available commands:$" "^  help     Show this list$" \
    "^  clear    Clear the screen$" "^  version  Show the Madire OS version$" \
    "^  uptime   Show time since boot$" "^  mem      Show memory information$" \
    "^  cpu      Show processor information$" "^  echo     Print text: echo <text>$" \
    "^  ps       Show threads and processes$" \
    "^  history  Show previous commands \(Up/Down recall\)$" "^  uname    Show the system name: uname \[-a\]$" \
    "^  hostname Show the computer's name$" "^  date     Show the date and time \(RTC\)$" \
    "^  dmesg    Show kernel messages: dmesg \[lines\]$" "^  reboot   Restart the computer \(asks first\)$" \
    "^  run      Run a program: run \[-b\] <name> \[n\]$" "^  kill     End a process: kill <pid> \(asks first\)$" \
    "^  prompt   Show or change the prompt: prompt \[text\]$" \
    "^  alias    Show or make short names: alias \[name \[command\]\]$" \
    "^  unalias  Remove a short name: unalias <name>$" \
    "^  set      Show or set variables: set \[NAME\[=value\]\]$" "^  unset    Remove a variable: unset <NAME>$" \
    "^  export   Mark a variable as exported: export <NAME>$" "^  env      Show the exported variables$" \
    "^  sleep    Wait: sleep <seconds> \(1-3600, Ctrl\+C stops\)$" \
    "^  presskey Wait for a key press \(Ctrl\+C cancels\)$" "^  time     Time a command: time <command>$" \
    "^Sections: shell system programs \(help <section>\)$" "$NEXT"
expect_lines "$LOG" "[0.12] help shell: the shell's commands" \
    "^madire> help shell$" "^shell: The shell's own commands$" "^  help     " "^  clear    " "^  echo     " \
    "^  history  " "^  prompt   " "^  alias    " "^  unalias  " "^  set      " "^  unset    " "^  export   " \
    "^  env      " "^  sleep    " "^  presskey " "^  time     " "$NEXT"
expect_lines "$LOG" "[0.12] help system: the system's commands" \
    "^madire> help system$" "^system: Information about the system$" "^  version  " "^  uptime   " \
    "^  mem      " "^  cpu      " "^  uname    " "^  hostname " "^  date     " "^  dmesg    " "^  reboot   " "$NEXT"
expect_lines "$LOG" "[0.12] help programs: programs and processes" \
    "^madire> help programs$" "^programs: Programs and processes$" "^  ps       " "^  run      " \
    "^  kill     " "$NEXT"
# Every command "help" lists is in exactly one section, and no section name
# is a command (stage 2's new commands must be placed in a section).
if awk '
        /^madire> / { cur = $0; next }
        cur == "madire> help" && /^  [a-z]/ { split($0, f, " "); all[f[1]] = 1; next }
        cur ~ /^madire> help (shell|system|programs)$/ && /^  [a-z]/ { split($0, f, " "); in_sec[f[1]]++; next }
        END {
            na = 0; for (c in all) { na++; if (in_sec[c] != 1) bad = 1 }
            for (c in in_sec) if (!(c in all)) bad = 1
            split("shell system programs", s, " ")
            for (i = 1; i <= 3; i++) if (s[i] in all) bad = 1
            exit !(na == 26 && !bad)
        }' "$LOG"; then
    pass "[0.12] help sections: each of the 26 commands in exactly one section, no section named like a command"
else
    fail "[0.12] help sections: a command missing from the sections, in two, or a section named like a command"
fi
expect_lines "$LOG" "[0.12] help shel: unknown, then Did you mean over sections and commands" \
    "^madire> help shel$" "^No help for 'shel': unknown command\.$" "^Did you mean: shell, help, set\?$" "$NEXT"
expect_lines "$LOG" "[0.12] help nosuch: today's line only, no suggestion" \
    "^madire> help nosuch$" "^No help for 'nosuch': unknown command\.$" "$NEXT"
expect_lines "$LOG" "[0.12] hlep: Did you mean: help, sleep?" \
    "^madire> hlep$" "^Unknown command: hlep\. Type 'help' for a list of commands\.$" "^Did you mean: help, sleep\?$" "$NEXT"
expect_lines "$LOG" "[0.12] ru: nearest first, then alphabetically (run, cpu, ps)" \
    "^madire> ru$" "^Unknown command: ru\. " "^Did you mean: run, cpu, ps\?$" "$NEXT"
expect_lines "$LOG" "[0.12] Echo: case-sensitive, distance 1 (echo)" \
    "^madire> Echo x$" "^Unknown command: Echo\. " "^Did you mean: echo\?$" "$NEXT"
expect_lines "$LOG" "[0.12] ECHO: four letters differ, no suggestion" \
    "^madire> ECHO x$" "^Unknown command: ECHO\. " "$NEXT"
expect_lines "$LOG" "[0.12] kil 5: kill suggested, never run (no question)" \
    "^madire> kil 5$" "^Unknown command: kil\. " "^Did you mean: kill\?$" "$NEXT"
reject "$LOG" "^End process|^console: (running|started in the background)" \
                                                      "[0.12] suggestions never run a command (no kill question, no program)"
expect_lines "$LOG" "[0.12] prompt: the default" \
    "^madire> prompt$" "^Prompt: 'madire> ' \(the default\)$" "$NEXT"
expect_lines "$LOG" "[0.12] prompt abc>: set, padded to 8, used for the next line" \
    "^madire> prompt abc>$" "^Prompt set to 'abc>    '\.$" "^(console: |abc>    echo under-new-prompt$)"
expect_lines "$LOG" "[0.12] a line typed after the new prompt runs normally" \
    "^abc>    echo under-new-prompt$" "^under-new-prompt$"
expect_lines "$LOG" "[0.12] prompt: shows the new prompt (not the default)" \
    "^abc>    prompt$" "^Prompt: 'abc>    '$"
expect_lines "$LOG" "[0.12] prompt with 9 characters: refused" \
    "^abc>    prompt 123456789$" "^prompt: at most 8 characters \(this text has 9\)\.$"
expect_lines "$LOG" "[0.12] prompt -x: refused (may not start with '-')" \
    "^abc>    prompt -x$" "^prompt: the text may not start with '-' \(to reset: prompt --reset\)\.$"
expect_lines "$LOG" "[0.12] prompt a b: usage (no spaces)" \
    "^abc>    prompt a b$" "^Usage: prompt \[text \| --reset\]$"
expect_lines "$LOG" "[0.12] prompt --reset: the default again, for the next line" \
    "^abc>    prompt --reset$" "^Prompt reset to 'madire> '\.$" "$NEXT"
count_lines "$LOG" "^abc>    " 6                   "[0.12] exactly the 6 lines between the change and the reset used the new prompt"
expect_in_order "$LOG" "[0.12] after the reset: the default prompt, shown as the default" \
    "^abc>    prompt --reset$" "^madire> prompt$" "^Prompt: 'madire> ' \(the default\)$"
expect_lines "$LOG" "[0.12] alias: none yet" "^madire> alias$" "^0 aliases \(at most 16\)$" "$NEXT"
expect_lines "$LOG" "[0.12] alias ll echo hi there: added" \
    "^madire> alias ll echo hi there$" "^Alias 'll' added\.$"
expect_lines "$LOG" "[0.12] alias ll: shows one" "^madire> alias ll$" "^ll {14}echo hi there$" "$NEXT"
expect_lines "$LOG" "[0.12] ll: the alias runs its command" "^madire> ll$" "^hi there$"
expect_lines "$LOG" "[0.12] ll more: the first word only is replaced" "^madire> ll more$" "^hi there more$"
expect_in_order "$LOG" "[0.12] alias ll again: replaced, the new command runs" \
    "^madire> alias ll echo replaced$" "^Alias 'll' replaced\.$" "^madire> ll$" "^replaced$"
expect_lines "$LOG" "[0.12] alias echo ...: a built-in's name is refused" \
    "^madire> alias echo echo x$" "^alias: 'echo' is a built-in command\.$"
expect_lines "$LOG" "[0.12] alias zz ll: must start with a built-in (no alias to an alias)" \
    "^madire> alias zz ll$" "^alias: 'll' is not a built-in command\.$"
expect_lines "$LOG" "[0.12] alias name of 16 characters: refused" \
    "^madire> alias abcdefghijklmnop echo x$" "^alias: a name has at most 15 characters \(this one has 16\)\.$"
expect_in_order "$LOG" "[0.12] alias name of 15 characters: added and runs" \
    "^madire> alias abcdefghijklmno echo 15$" "^Alias 'abcdefghijklmno' added\.$" "^madire> abcdefghijklmno$" "^15$"
expect_lines "$LOG" "[0.12] alias name with a capital: refused" \
    "^madire> alias Bad echo x$" "^alias: a name uses only a-z, 0-9 and _\.$"
expect_lines "$LOG" "[0.12] alias with a 63-character command (the longest console line): added" \
    "^madire> alias v echo x{58}$" "^Alias 'v' added\.$"
expect_lines "$LOG" "[0.12] alias v: the row is 79 columns" "^madire> alias v$" "^v {15}echo x{58}$"
expect_lines "$LOG" "[0.12] unalias nosuch: a clear error" \
    "^madire> unalias nosuch$" "^unalias: no alias named 'nosuch'\.$"
expect_lines "$LOG" "[0.12] unalias without a name: usage" "^madire> unalias$" "^Usage: unalias <name>$"
expect_lines "$LOG" "[0.12] an alias's line with 8 words: runs" "^madire> e5 6 7$" "^1 2 3 4 5 6 7$"
expect_lines "$LOG" "[0.12] an alias's line with 9 words: refused" "^madire> e5 6 7 8$" "^Too many arguments \(max 8\)\.$"
expect_lines "$LOG" "[0.12] lll: aliases are suggested too (ll, kill)" \
    "^madire> lll$" "^Unknown command: lll\. " "^Did you mean: ll, kill\?$" "$NEXT"
expect_lines "$LOG" "[0.12] the 16th alias: added" "^madire> alias t13 echo 13$" "^Alias 't13' added\.$"
expect_lines "$LOG" "[0.12] a 17th alias: refused (table full)" \
    "^madire> alias t14 echo 14$" "^alias: the table is full \(16 aliases\); remove one with unalias\.$"
expect_in_order "$LOG" "[0.12] after unalias: room again, the new alias runs" \
    "^madire> unalias t13$" "^Alias 't13' removed\.$" "^madire> alias t14 echo 14$" "^Alias 't14' added\.$" \
    "^madire> t14$" "^14$"
expect_lines "$LOG" "[0.12] alias: all 16, sorted by name" \
    "^madire> alias $" "^abcdefghijklmno echo 15$" "^e5 {14}echo 1 2 3 4 5$" "^ll {14}echo replaced$" \
    "^t02 {13}echo 02$" "^t03 " "^t04 " "^t05 " "^t06 " "^t07 " "^t08 " "^t09 " "^t10 " "^t11 " "^t12 " \
    "^t14 {13}echo 14$" "^v {15}echo x{58}$" "^16 aliases \(at most 16\)$"
w=$(max_width "$LOG" "^console: started$" "^end-of-012-test$")
if grep -q "^end-of-012-test$" "$LOG" && [ "$w" -le 79 ]; then
    pass "[0.12] every console line at most 79 characters (longest $w)"
else
    fail "[0.12] a console line of $w characters (limit 79), or the session did not finish"
fi
reject "$LOG" "PANIC|events dropped|Screen output from thread|Unexpected IRQ" \
                                                      "[0.12] no panic, dropped key or screen-rule violation"

# ---- 1.0 stage 2: the storage diagnostics (B5, B7) ----

READY_LINE="System ready. Press any key to open the console."

# fs_closing <log> <limits|fill> <description>: the test's closing line
# gives nodes 3 -> 3, data pages 0 -> 0, and free pages back to the start
# minus exactly the heap's growth and its new page tables (the heap never
# shrinks); with the PASS line, System ready and no panic.
fs_closing() {
    local l n0 n1 d0 d1 f0 f1 hp pt
    l=$(grep -m1 -E "^Filesystem $2 test: (PASS|FAIL) \(" "$1")
    read -r n0 n1 d0 d1 f0 f1 hp pt <<< "$(sed -nE 's/.*\(nodes ([0-9]+) -> ([0-9]+), data pages ([0-9]+) -> ([0-9]+), free pages ([0-9]+) -> ([0-9]+) = ([0-9]+) heap pages \+ ([0-9]+) page tables\)$/\1 \2 \3 \4 \5 \6 \7 \8/p' <<< "$l")"
    if [ "${n0:-x}" = 3 ] && [ "${n1:-y}" = 3 ] && [ "${d0:-x}" = 0 ] && [ "${d1:-y}" = 0 ] &&
       [ -n "${pt:-}" ] && [ "$f1" -eq $((f0 - hp - pt)) ]; then
        pass "$3 ($f0 -> $f1 = $hp heap pages + $pt page tables)"
    else
        fail "$3 (${l:-no closing line})"
    fi
}

echo "== [1.0] test=fslimits: every B5 limit hit once (-m 128M)"
LOG=$OUT/serial-fslimits.log
boot_until "$LOG" 240 "$READY_LINE" -m 128M -kernel "$KERNEL" -append "test=fslimits"
grep -E "^Filesystem limits test: " "$LOG" | sed 's/^/      /'
expect "$LOG" "^Filesystem limits test: nodes: [0-9]+ files created up to 1024 nodes; the next file ENOSPC, the next folder ENOSPC, nothing created; all removed: OK$" \
    "[1.0] fslimits: 1024 nodes: the next file and the next folder ENOSPC, nothing created; all removed"
expect "$LOG" "^Filesystem limits test: file size: 2 MiB written in 512 writes of 4096 bytes and read back \(513 pages with the index\); one more byte EFBIG; a write crossing 2 MiB returned 100 of 4096 bytes; a write at 3 MiB EFBIG; truncate to 2 MiB \+ 1 EFBIG, to 2 MiB OK: OK$" \
    "[1.0] fslimits: 2 MiB per file: a write crossing it returns the short count up to 2 MiB; at 2 MiB and past it EFBIG; truncate past it EFBIG"
expect "$LOG" "^Filesystem limits test: hole: one byte written at 1 MiB costs 2 pages \(index \+ data\); the 1 MiB before it reads as zeros: OK$" \
    "[1.0] fslimits: a byte written at 1 MiB costs 2 pages (index + data); the hole reads as zeros"
expect "$LOG" "^Filesystem limits test: budget: [0-9]+ files filled to 8192 pages = the budget, the next write ENOSPC; a write running out returned 2048 of 8192 bytes, the next ENOSPC; growing a file by truncate takes no page and is never ENOSPC, a write into the grown hole ENOSPC: OK$" \
    "[1.0] fslimits: the budget: files filled to exactly the budget, then ENOSPC; a write running out returns its short count, the next ENOSPC; growing a file by truncate takes no page and is never ENOSPC"
expect "$LOG" "^Filesystem limits test: page allocator: all [0-9]+ free pages taken, under the budget: a new file's first write ENOSPC with no index page left behind, a new page of a file ENOSPC, an existing page still written; all pages given back: OK$" \
    "[1.0] fslimits: the page allocator refusing a page under the budget: ENOSPC, no index page left behind"
expect "$LOG" "^Filesystem limits test: heap: filled to its 16 MiB cap \([0-9]+ blocks\): a file ENOMEM \(no room for its record\), a folder ENOMEM \(its record fit, its name did not\); nothing half-created; the heap given back: OK$" \
    "[1.0] fslimits: heap exhausted: create ENOMEM (no record; a record without room for its name), nothing half-created"
fs_closing "$LOG" limits \
    "[1.0] fslimits: everything freed: nodes 3, data pages 0, free pages back to the start minus the heap's growth and its page tables"
if grep -qE "^Filesystem limits test: PASS \(" "$LOG" && grep -qxF "$READY_LINE" "$LOG" && ! grep -q "PANIC" "$LOG"; then
    pass "[1.0] fslimits: PASS, System ready, no panic"
else
    fail "[1.0] fslimits: no PASS, no System ready, or a panic in $LOG"
fi

# fsfill_test <-m size>: files filled to the budget at that RAM size.
fsfill_test() {
    local mem=$1 log=$OUT/serial-fsfill-$1.log l b f bwant
    echo "== [1.0] test=fsfill at -m $mem"
    boot_until "$log" 240 "$READY_LINE" -m "$mem" -kernel "$KERNEL" -append "test=fsfill"
    grep -E "^Filesystem (fill test|: budget): " "$log" | sed 's/^/      /'
    l=$(grep -m1 -E '^Filesystem fill test: budget ' "$log")
    read -r b f <<< "$(sed -nE 's/^Filesystem fill test: budget ([0-9]+) pages = min\(8192, 3\/4 of ([0-9]+) free pages at boot\); [0-9]+ free pages now$/\1 \2/p' <<< "$l")"
    bwant=""
    if [ -n "${f:-}" ]; then bwant=$((f * 3 / 4)); fi
    if [ "$mem" = 32M ]; then
        if [ -n "$bwant" ] && [ "$bwant" -lt 8192 ] && [ "$b" = "$bwant" ]; then
            pass "[1.0] fsfill 32M: budget = 3/4 of the $f free pages at boot = $b pages (below the 8192 cap)"
        else
            fail "[1.0] fsfill 32M: budget not 3/4 of the free pages below the cap (${l:-no line})"
        fi
    else
        if [ -n "$bwant" ] && [ "$bwant" -ge 8192 ] && [ "$b" = 8192 ]; then
            pass "[1.0] fsfill $mem: budget = min(8192, 3/4 of the $f free pages at boot) = 8192 pages (the cap)"
        else
            fail "[1.0] fsfill $mem: budget not the 8192-page cap (${l:-no line})"
        fi
    fi
    expect "$log" "^Filesystem fill test: [0-9]+ files filled to ${b:-X} pages \(data \+ index\) = the budget, the next write ENOSPC; a write running out returned 2048 of 8192 bytes, the next ENOSPC; growing a file by truncate took no page; every byte read back \([0-9]+ bytes\): OK$" \
        "[1.0] fsfill $mem: filled to exactly ${b:-?} pages (data + index); the last write short, the next ENOSPC; every byte read back"
    if grep -qE "^Filesystem fill test: PASS \(" "$log" && grep -qxF "$READY_LINE" "$log" && ! grep -q "PANIC" "$log"; then
        fs_closing "$log" fill \
            "[1.0] fsfill $mem: everything freed: data pages 0, free pages back to the start minus the heap's growth and its page tables; PASS, System ready, no panic"
    else
        fail "[1.0] fsfill $mem: no PASS, no System ready, or a panic in $log"
    fi
}

fsfill_test 32M
fsfill_test 64M
