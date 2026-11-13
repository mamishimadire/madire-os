# Madire OS QEMU tests, part 5 (1.5 stage 2, plan section E, decision 16):
# a sixth shard for the shards' balance and 1.5's desktop and Files
# sessions. Sourced by tests/qemu-tests.sh (a full run, or --shard 5).
# In stage 2, apart from this header, every line was moved unchanged: from
# part static the self-test of boot_until's stop with a fake QEMU (1.3
# stage 7a), the handover's rule on the references with the VirtualBox
# desktop group's plan and references (1.3 stage 8) and the hidden boot
# menu's checks (1.1; moved to part static in 1.3 stage 8a); from part 4
# the console's file commands and the Live label (1.0 stage 8). New since:
# the per-disk I/O lock's boots (1.5 stage 4, the last section). Each
# section boots or builds what it reads itself (no file or variable from
# another part).

# ---- 1.3 stage 7a: boot_until's stop ----

section "[1.3] boot_until's stop: its four paths with a fake QEMU (stage 7a)"
# The real boot_until (tests/qemu-lib.sh) through the real wrapper chain
# (timeout -> qemu-system-x86_64 -> tests/qemu-watch.sh -> setsid), with
# tests/boot-until-fake-qemu.sh as QEMU_REAL for these four boots only. Each
# must report its path on its BOOT_UNTIL line, and afterwards no process may
# name the self-test's folder on its command line (ps; the fake's own
# "sleep 0.2" children name nothing and end within 0.2 s). The logs stay
# outside serial-*.log (part final's every-boot checks keep their boots).
BU_DIR=$OUT/boot-until-selftest
rm -rf "$BU_DIR" && mkdir -p "$BU_DIR"
printf '#!/bin/sh\nexec bash "%s" "$@"\n' "$(cd "$(dirname "$0")" && pwd)/boot-until-fake-qemu.sh" \
    > "$BU_DIR/fake-qemu"
chmod +x "$BU_DIR/fake-qemu"
BU_REAL=$QEMU_REAL
for bu in "term 20 line_seen=[0-9]+\.[0-9] stop=term:SIGTERM ends it" \
          "ignore 20 line_seen=[0-9]+\.[0-9] stop=forced:it ignores SIGTERM, so SIGKILL ends it after 5 s" \
          "limit 3 line_seen=none stop=limit:it never writes the line, so timeout ends it at its 3 s limit" \
          "exited 20 line_seen=none stop=exited:it ends by itself without the line"; do
    read -r bu_mode bu_secs bu_want <<< "${bu%%:*}"
    export QEMU_REAL=$BU_DIR/fake-qemu BOOT_UNTIL_FAKE=$bu_mode
    boot_until "$BU_DIR/$bu_mode.log" "$bu_secs" "System ready." -m 32M > "$BU_DIR/$bu_mode.out" 2>&1
    export QEMU_REAL=$BU_REAL
    unset BOOT_UNTIL_FAKE
    sed 's/^/      /' "$BU_DIR/$bu_mode.out"
    sleep 1                             # anything killed is reaped by now
    bu_left=$(ps -eo pid=,args= | BU_DIR=$BU_DIR awk 'index($0, ENVIRON["BU_DIR"])')
    [ -z "$bu_left" ] || printf '%s\n' "$bu_left" | sed 's/^/      left: /'
    if grep -qxE "BOOT_UNTIL $bu_mode\.log $bu_want" "$BU_DIR/$bu_mode.out" && [ -z "$bu_left" ]; then
        pass "[1.3] boot_until self-test, the fake QEMU's $bu_mode case (${bu#*:}): its BOOT_UNTIL line says ${bu_want##* }, and none of its processes is left"
    else
        fail "[1.3] boot_until self-test, the fake QEMU's $bu_mode case: its BOOT_UNTIL line does not say ${bu_want##* }, or one of its processes is left"
    fi
done

# ---- 1.3 stage 8: the switch ----

section "[1.3] The splash-to-desktop handover and the VirtualBox desktop group's references (stage 8)"
# The handover's rule on the references themselves, in the five modes of the
# desktop's checks above: make-wallpaper.py's splash in its ready state
# against desktop_ref.py's desktop (tests/desktop_ref.py's handover_check):
# the M's box identical pixel for pixel, every other difference inside the
# wordmark, the ready line or the taskbar (the QEMU handover sections of
# parts 2 and 3 check the kernel's own two screens with the same rule).
HO=$(python3 - "$(dirname "$0")" <<'EOF' 2>&1
import sys
sys.path.insert(0, sys.argv[1])
sys.dont_write_bytecode = True
import desktop_ref as D
for w, h in ((800, 600), (1024, 768), (1280, 800), (1920, 1080), (2560, 1440)):
    md, out, b, first = D.handover_check(w, h, D.MW.splash(w, h, "ready"), D.desktop(w, h, D.FIXED_CLOCK))
    print("HANDOVER %dx%d m_box=%d,%d,%dx%d m_differ=%d outside=%d%s" % (
        (w, h) + b["m"] + (md, out, "" if first is None else " first outside at %d,%d" % first)))
EOF
)
sed 's/^/      /' <<< "$HO"
if [ "$(grep -cE '^HANDOVER [0-9]+x[0-9]+ m_box=[0-9]+,[0-9]+,[0-9]+x[0-9]+ m_differ=0 outside=0$' <<< "$HO")" -eq 5 ]; then
    pass "[1.3] the handover on the references in five modes: make-wallpaper.py's ready state and desktop_ref.py's desktop have the M's box identical pixel for pixel and differ only in the wordmark, the ready line and the taskbar"
else
    fail "[1.3] the handover on the references: $(grep -vE 'm_differ=0 outside=0$' <<< "$HO" | tr '\n' ' ')"
fi
# The VirtualBox desktop group's plan and references (tests/screen_ref.py;
# the group runs in stage 9, on the owner's host): the plan replays its steps
# through desktop_ref.py's model without an error, and after the run every
# shot gets its reference; here with a fixed usable memory (4095 MB). The
# graphics group's two references (the desktop at System ready, the Console
# window after clear) are made too, and the first equals the plan's "ready".
VB=$OUT/vbox-desktop-ref
rm -rf "$VB" && mkdir -p "$VB"
SR=$(dirname "$0")/screen_ref.py
VBP=$(python3 "$SR" desktop-plan 1024 768 2>&1)
VBR=$(python3 "$SR" desktop-render 1024 768 "$VB" 4095 2>&1)
VBG=$( { python3 "$SR" desktop 1024 768 "$VB/desktop.rgb" && python3 "$SR" desktop-console 1024 768 "$VB/desktop-console.rgb"; } 2>&1)
grep -vE '^MOUSE ' <<< "$VBP" | tr '\n' '|' | sed 's/^/      plan: /'; echo
echo "      plan: $(grep -c '^MOUSE ' <<< "$VBP") mouse packets; $(grep -c '^REFERENCE ' <<< "$VBR") references rendered"
VB_SHOTS=$(grep -c '^SHOT ' <<< "$VBP")
VB_FILES=$(find "$VB" -name 'desktop-*.rgb' ! -name 'desktop-console.rgb' -size "$((1024 * 768 * 3))c" | wc -l)
if [ "$VB_SHOTS" -gt 0 ] && [ "$VB_FILES" -eq "$VB_SHOTS" ] && ! grep -q "ERROR" <<< "$VBP$VBR$VBG" &&
   grep -qxF "MASK 968 740 40 16 (the clock's cells)" <<< "$VBG" && cmp -s "$VB/desktop.rgb" "$VB/desktop-ready.rgb" &&
   [ "$(stat -c%s "$VB/desktop-console.rgb")" -eq $((1024 * 768 * 3)) ]; then
    pass "[1.3] the VirtualBox desktop group's plan replays through desktop_ref.py's model ($VB_SHOTS shots), each shot's reference is rendered, and the graphics group's references are made (the desktop at System ready equal to the plan's first)"
else
    fail "[1.3] the VirtualBox desktop group's plan or references: $VB_SHOTS shots, $VB_FILES references ($(grep -E 'ERROR' <<< "$VBP$VBR$VBG" | head -n 2 | tr '\n' ' '))"
fi

section "[1.1] The boot menu: hidden behind a key (the real ISO, driven over QMP)"
# 1.1 stage 2 (docs/plans/1.1.md, D1 Q1-Q6): the real ISO's boot menu
# (limine.conf: quiet, timeout 2, term_margin 240), driven by
# tests/qmp_menu.py, never together with qmp_keys.py. QEMU starts paused,
# so power-on is time 0. Each state's screen is compared pixel by pixel with
# the ISO tree's wallpaper.png; the panel, header, hint and version boxes
# come from scripts/make-wallpaper.py --layout-json (one source). The marker
# rows: the up markers in y 288-303, the down markers in y 464-479 (plan,
# section E). The logs are in $OUT/menu/, not serial-*.log, so part final
# counts the same boots as before (as the padded test kernel's).
MENU_T0=$(date +%s)
QMPMENU=$(dirname "$0")/qmp_menu.py
MENUDIR=$OUT/menu
rm -rf "$MENUDIR" && mkdir -p "$MENUDIR"
MENU_WP=$OUT/isodir/boot/limine/wallpaper.png
MENU_LAYOUT=$MENUDIR/layout.json
python3 "$(dirname "$0")/../scripts/make-wallpaper.py" "$MENUDIR/wallpaper-layout.png" --version "$VERSION" \
    --layout-json "$MENU_LAYOUT" | sed 's/^/      /'
echo "      layout: $(tr -d ' \n' < "$MENU_LAYOUT" 2>/dev/null)"
# menu_boot <log> <seconds> <qmp_menu.py mode> <its arguments after the
# socket and the log...>: boots the real ISO paused with a QMP socket, runs
# qmp_menu.py, then stops QEMU. MENU_OUT holds qmp_menu.py's output (also
# printed and kept in <log>.menu.txt).
menu_boot() {
    local log=$1 secs=$2 mode=$3 sock=$MENUDIR/qmp.sock t0 qpid
    shift 3
    rm -f "$log" "$sock"
    t0=$(date +%s%N)
    timeout "$secs" qemu-system-x86_64 -S -display none -no-reboot -serial file:"$log" \
        -qmp unix:"$sock",server=on,wait=off -cdrom "$ISO" >/dev/null 2>&1 &
    qpid=$!
    MENU_OUT=$(python3 "$QMPMENU" "$mode" "$sock" "$log" "$@" 2>&1)
    sed 's/^/      /' <<< "$MENU_OUT"
    echo "$MENU_OUT" > "${log%.log}.menu.txt"
    sleep 0.5
    kill "$qpid" 2>/dev/null
    wait "$qpid" 2>/dev/null
    [ -f "$log" ] || : > "$log"
    boot_seconds "$log" "$t0"
}
# menu_state <name>: qmp_menu.py's STATE line of that shot, if 1024x768.
menu_state() { grep -E "^STATE $1 size=1024x768 " <<< "$MENU_OUT"; }

# Q1-Q4: one boot. Home every 300 ms until the menu shows, 5 s held, then
# the four states: collapsed; Down, Enter (Diagnostics open, the top of
# the list); Down x27 (position 26, the end of the list); Up x28 (back at
# the top).
LOG=$MENUDIR/reveal.log
menu_boot "$LOG" 150 menu "$MENU_WP" "$MENU_LAYOUT" hold:5 shot:collapsed down ret shot:open \
    'down*27' shot:position-26 'up*28' shot:back-top --band up=288-303 --band down=464-479 --shots "$MENUDIR"
if grep -qE '^REVEAL [0-9]+\.[0-9] s, [0-9]+ presses, 1024x768$' <<< "$MENU_OUT" &&
   grep -qxF 'HOLD 5 s: serial 0 bytes, screen 1024x768, header match' <<< "$MENU_OUT"; then
    pass "[1.1] boot menu: a key during the 2 s shows the menu (1024x768), and nothing boots for 5 s"
else
    fail "[1.1] boot menu: no 1024x768 menu within 30 s of keys, or the serial log not empty 5 s later (see ${LOG%.log}.menu.txt)"
fi
ok=1
for s in collapsed open position-26 back-top; do
    menu_state "$s" | grep -qE ' drawn=[1-9][0-9]* inside_panel=yes ' || ok=0
done
if [ "$ok" = 1 ]; then
    pass "[1.1] boot menu: in every state Limine draws only inside the panel (collapsed, Diagnostics open, scrolled to position 26, back at the top)"
else
    fail "[1.1] boot menu: a state drew outside the panel, nothing, or was not 1024x768 (see ${LOG%.log}.menu.txt)"
fi
ok=1
for s in collapsed open position-26 back-top; do
    menu_state "$s" | grep -qE ' header=0 hint=0 version=0 ' || ok=0
done
if [ "$ok" = 1 ]; then
    pass "[1.1] boot menu: no Limine name or help text (the header, hint and version areas are the wallpaper's own pixels)"
else
    fail "[1.1] boot menu: Limine drew in the header, hint or version area, or a state was not 1024x768 (see ${LOG%.log}.menu.txt)"
fi
if menu_state collapsed | grep -qE ' band_up=0 band_down=0$' &&
   menu_state open | grep -qE ' band_down=[1-9][0-9]*$' &&
   menu_state position-26 | grep -qE ' band_up=[1-9][0-9]* '; then
    pass "[1.1] boot menu: scroll markers only when the list overflows (down markers at the top of the list, up markers at position 26)"
else
    fail "[1.1] boot menu: scroll markers wrong (collapsed: none in y 288-303 and 464-479; open: some in 464-479; position 26: some in 288-303; see ${LOG%.log}.menu.txt)"
fi

# Q5: a second boot, the keys of the VirtualBox automation: Down, Enter,
# Down x27, Enter (1 s after the menu shows; Home only selects the first
# entry, so a Home still queued changes nothing). "No PANIC" relies on
# QEMU's default CPU lacking SMAP: position 26 then reports NOT APPLICABLE.
# With a SMAP-capable -cpu the crash test would panic by design, and this
# check would need changing.
LOG=$MENUDIR/position-26.log
menu_boot "$LOG" 150 menu "$MENU_WP" "$MENU_LAYOUT" hold:1 down ret 'down*27' ret 'until:SMAP test:'
if grep -qE '^REVEAL ' <<< "$MENU_OUT" && grep -qxF 'HOLD 1 s: serial 0 bytes, screen 1024x768, header match' <<< "$MENU_OUT" &&
   grep -q '^SMAP test: ' "$LOG" && ! grep -q "PANIC" "$LOG"; then
    pass "[1.1] boot menu: Down, Enter, Down x27 boots Diagnostics position 26 to 'SMAP test:'"
else
    fail "[1.1] boot menu: Down, Enter, Down x27, Enter did not boot position 26 to 'SMAP test:' (or booted early, or a panic; see $LOG)"
fi

# Q6: no key: a screendump every 250 ms until System ready. 1.2 stage 10b
# (docs/plans/1.2.md, D2): the real ISO now boots in graphics at 1024x768,
# so the width no longer tells the menu apart. Each screendump is placed
# by the serial log's size read just before it: before the first serial
# byte (Limine's time) only 640x480, 720x400 or a blank screen in the
# kernel's mode; after it, only the kernel's mode; no screendump at any
# time with the menu's header box; one boot (boot32: start once).
LOG=$MENUDIR/no-key.log
menu_boot "$LOG" 150 nokey "System ready." --wallpaper "$MENU_WP" --layout "$MENU_LAYOUT"
Q6MODE=$(sed -n 's/^MODE //p' <<< "$MENU_OUT")
if grep -qE "^BEFORE( (640x480|720x400)(/blank)?:[0-9]+| ${Q6MODE:-none}/blank:[0-9]+)+$" <<< "$MENU_OUT" &&
   grep -qE "^AFTER( ${Q6MODE:-none}(/blank)?:[0-9]+)+$" <<< "$MENU_OUT" &&
   grep -qxF "HEADER matches=0" <<< "$MENU_OUT" && grep -qxF "BOOTS boot32=1" <<< "$MENU_OUT" &&
   [ "${Q6MODE:-none}" != none ] && grep -qE "^UNTIL 'System ready\.': seen after " <<< "$MENU_OUT"; then
    pass "[1.1] no key: the real ISO boots to System ready and Limine's menu is never shown (before the first serial byte only 640x480, 720x400 or a blank screen; no screen ever matches the menu wallpaper's header)"
else
    fail "[1.1] no key: no System ready, a non-blank or other-size screen before the first serial byte, or a screen matching the menu wallpaper's header (see ${LOG%.log}.menu.txt)"
fi
echo "      [1.1] boot menu checks: $(( $(date +%s) - MENU_T0 )) s (report only)"

# ---- 1.0 stage 8: the console's file commands and the Live label ----

section "[1.0] Console: the file commands (pwd, cd, ls, mkdir, rmdir, touch, stat, cat) and the Live label"
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
# 1.0 stage 9: rm, cp, mv, df and du after cat (13 commands; was 8). 1.4
# stage 12: sync and disk after du (15).
expect_lines "$LOG" "[1.0] help files: the section's title and its 15 commands in help order" \
    "^madire> help files$" "^files: Files and folders$" "^  pwd      Show the current folder$" \
    "^  cd       Change the current folder: cd \[path\]$" "^  ls       List a folder: ls \[-l\] \[path\]$" \
    "^  mkdir    Create a folder: mkdir <path>$" "^  rmdir    Remove an empty folder: rmdir <path>$" \
    "^  touch    Create an empty file, or update its time$" \
    "^  stat     Show a file's or folder's details: stat <path>$" \
    "^  cat      Show a file's contents: cat <path>$" \
    "^  rm       Remove a file: rm \[-y\] <path> \(asks first\)$" "^  cp       Copy a file: cp \[-y\] <from> <to>$" \
    "^  mv       Move or rename: mv \[-y\] <from> <to>$" "^  df       Show the filesystem's nodes and pages in use$" \
    "^  du       Show the pages under a folder: du \[path\]$" "^  sync     Save changed files to the disk$" \
    "^  disk     Show disks and how they are used$" "$NEXT"
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

# ---- 1.5 stage 4: the per-disk I/O lock ----

section "[1.5] The per-disk I/O lock: test=blkslow, test=diskcontend and the self-check boots (stage 4)"
# docs/plans/1.5.md, C1 (rules L1-L6), risks 44, 45, 60. Each boot has its
# own marked read-write test disk (16 MiB, 1 KiB blocks; tests/disk-lib.sh
# and tests/disk_image.py, as part static's [1.4] disk sections make them).
# (1-3) test=blkslow=500: after each write and FLUSH command the thread
# waits 500 ms more, yielding, when it holds the disk lock with preemption
# on (a slow disk); the automatic save is on in this boot. At the console a
# file is written under /home; the automatic save sends its writes and its
# FLUSH while thread 0 sits in its idle loop; then sync. Each slow wait
# logs what ran meanwhile (thread 0's idle loop, a ticker thread) and the
# write and FLUSH commands sent while the VFS lock was held since boot,
# which must stay 0 from the boot mount's save 0 to the last command
# after System ready. The image: e2fsck -fn reports nothing, the file
# exact (debugfs). (4-6) test=diskcontend: the program bigio (writer 1)
# appends 8 writes of 32 KiB to /home/contend.bin while a kernel thread
# saves whenever something is dirty; both finish, the file is exact in the
# kernel and on the image (debugfs, its SHA-256 computed here), e2fsck -fn
# reports nothing. (7-10) test=selfcheckdisklock (the disk lock taken
# while the VFS lock is held: L1) and test=selfcheckdiskrelock (taken
# again by its owner: L3), in Disk mode: each panics with its rule's text
# and halts. The disk lock's figures are report only.
DL_T0=$(date +%s)
DL_W=$OUT/disklock
rm -rf "${DL_W:?}" && mkdir -p "$DL_W"
DL_READY="System ready. Press any key to open the console."
DL_BOOT_OK=1
# dl_disk <name>: a marked read-write test disk at $DL_W/<name>-disk.img.
dl_disk() {
    disk_mkfs "$DL_W/$1-fs.img" 16384 1024 256 2048 "" > "$DL_W/$1-mkfs.txt" 2>&1 &&
        disk_image mbr "$DL_W/$1-fs.img" "$DL_W/$1-disk.img" > /dev/null 2>&1
}
# dl_drive <name>: QEMU's options for that disk on ich9-ahci.
dl_drive() {
    printf '%s\n' -drive "file=$DL_W/$1-disk.img,if=none,id=dl$1-disk,format=raw" \
        -device "ich9-ahci,id=dl$1-ahci,addr=0x5" -device "ide-hd,drive=dl$1-disk,bus=dl$1-ahci.0"
}
# dl_fsck <name>: e2fsck -fn on the disk's partition; true when it exits 0
# and prints only its frame lines (nothing to report).
dl_fsck() {
    local part=$DL_W/$1-part.img out rc extra
    disk_image extract "$DL_W/$1-disk.img" "$part" > /dev/null 2>&1 || { echo "      e2fsck ($1): no partition"; return 1; }
    out=$(disk_fsck "$part"); rc=$?
    extra=$(grep -vE '^(e2fsck [0-9]+\.[0-9]+.*|Pass [1-5]: .*|madire-test: [0-9]+/[0-9]+ files \(.*\), [0-9]+/[0-9]+ blocks|)$' <<< "$out")
    sed "s/^/      e2fsck ($1, exit $rc): /" <<< "$out"
    [ "$rc" -eq 0 ] && [ -z "$extra" ]
}
# dl_health <log>: System ready, no PANIC or FAILED line (case-sensitive).
dl_health() { [ -s "$1" ] && grep -q '^System ready\.' "$1" && ! grep -qE 'PANIC|FAILED' "$1"; }
DL_MOUNT0="^blk: the mount: save 0 under the disk lock only; 0 write or FLUSH commands sent while the VFS lock was held \(since boot\); "

# (1-3) test=blkslow=500.
dl_disk blkslow || DL_BOOT_OK=0
mapfile -t DL_DRV < <(dl_drive blkslow)
DL_SL=$OUT/serial-blkslow.log
DL_SLTEXT=blkslow-line
boot_text "$DL_SL" 180 "System ready." "x{wait:1}echo $DL_SLTEXT > /home/slow.txt{ret}{wait:20}sync{ret}" 0.05 \
    $'\next2: sync: ' -m 128M -kernel "$KERNEL" -append "test=blkslow=500" "${DL_DRV[@]}"
grep -E '^(blkslow|blk: test=blkslow|blk: the mount|mount: changes|ext2: sync)' "$DL_SL" | sed 's/^/      blkslow: /'
DL_FL=$(sed -nE "s/^blk: test=blkslow: thread [0-9]+ 'autosave': FLUSH CACHE EXT done, then 500 ms more, yielding: thread 0's idle loop ran ([0-9]+) times, the ticker thread ([0-9]+) times; write or FLUSH commands sent while the VFS lock was held, since boot: ([0-9]+)$/\1 \2 \3/p" "$DL_SL" | head -n 1)
read -r dl_idle dl_tick dl_vfs <<< "${DL_FL:-0 0 x}"
echo "      [1.5] blkslow: during the automatic save's first 500 ms FLUSH wait: thread 0's idle loop $dl_idle passes, the ticker $dl_tick wake-ups (report: the counts)"
if [ "$DL_BOOT_OK" -eq 1 ] && [ -n "$DL_FL" ] && [ "$dl_idle" -ge 25 ] && [ "$dl_tick" -ge 25 ] &&
   grep -qE '^blkslow: armed: after every write and FLUSH command the thread waits 500 ms more, yielding, when it holds the disk lock with preemption on; the ticker thread started \(thread [0-9]+\); ' "$DL_SL" &&
   grep -qE '^mount: changes are kept in memory .*; automatic save every 5 s: on \(test=blkslow\)$' "$DL_SL"; then
    pass "[1.5] test=blkslow=500: the automatic save's FLUSH CACHE EXT was followed by a 500 ms wait, yielding, holding the disk lock; meanwhile thread 0's idle loop ran ($dl_idle passes) and the ticker thread ran ($dl_tick wake-ups, at least 25 of the wait's 50 ticks)"
else
    fail "[1.5] test=blkslow=500: no slow FLUSH by the automatic save, or thread 0's idle loop or the ticker did not run during it (idle ${dl_idle:-?}, ticker ${dl_tick:-?}; see serial-blkslow.log)"
fi
DL_SLN=$(grep -cE '^blk: test=blkslow: thread [0-9]+ ' "$DL_SL")
DL_SLBAD=$(grep -E '^blk: test=blkslow: thread [0-9]+ ' "$DL_SL" | grep -cvE '; write or FLUSH commands sent while the VFS lock was held, since boot: 0$')
DL_SLAFTER=$(awk '/^System ready\./ { r = 1 } r && /^blk: test=blkslow: thread [0-9]+ / { n++ } END { print n + 0 }' "$DL_SL")
DL_SLBEFORE=$(awk '/^System ready\./ { exit } /^blk: test=blkslow: thread [0-9]+ .kernel.: / { n++ } END { print n + 0 }' "$DL_SL")
if [ "$DL_SLN" -ge 2 ] && [ "$DL_SLBAD" -eq 0 ] && [ "$DL_SLAFTER" -ge 1 ] && [ "$DL_SLBEFORE" -ge 1 ] &&
   [ "$(grep -cE "$DL_MOUNT0" "$DL_SL")" -eq 1 ]; then
    pass "[1.5] test=blkslow=500: no write or FLUSH command was sent while the VFS lock was held, the boot mount included: 0 on the mount's line and on all $DL_SLN slow commands' lines ($DL_SLBEFORE by the mount's save 0 before System ready, $DL_SLAFTER after it)"
else
    fail "[1.5] test=blkslow=500: a write or FLUSH was sent under the VFS lock, or the slow commands' lines are missing ($DL_SLN lines, $DL_SLBAD not 0, $DL_SLBEFORE by the mount, $DL_SLAFTER after System ready; see serial-blkslow.log)"
fi
DL_SLGOT=$(disk_image extract "$DL_W/blkslow-disk.img" "$DL_W/blkslow-part.img" > /dev/null 2>&1 &&
           disk_debugfs "$DL_W/blkslow-part.img" "cat /slow.txt" | sha256sum | cut -d' ' -f1)
DL_SLWANT=$(printf '%s\n' "$DL_SLTEXT" | sha256sum | cut -d' ' -f1)
if dl_fsck blkslow && [ "$DL_SLGOT" = "$DL_SLWANT" ] && dl_health "$DL_SL" && grep -qE '^ext2: sync: saved; ' "$DL_SL"; then
    pass "[1.5] test=blkslow=500: after the boot e2fsck -fn reports nothing, /slow.txt on the image is exact ('$DL_SLTEXT' and a newline), sync said saved; no PANIC or FAILED line"
else
    fail "[1.5] test=blkslow=500: e2fsck -fn reported something, /slow.txt differs (${DL_SLGOT:-none}), sync did not save, or the boot was not healthy (see serial-blkslow.log)"
fi

# (4-6) test=diskcontend.
dl_disk contend || DL_BOOT_OK=0
mapfile -t DL_DRV < <(dl_drive contend)
DL_CT=$OUT/serial-diskcontend.log
boot_until "$DL_CT" 240 "$DL_READY" -m 128M -kernel "$KERNEL" -append "test=diskcontend" "${DL_DRV[@]}"
grep -E '^(diskcontend|blk: the mount): ' "$DL_CT" | sed 's/^/      diskcontend: /'
echo "      [1.5] diskcontend: $(grep -c '^ext2: sync: ' "$DL_CT") sync lines (report only)"
if [ "$DL_BOOT_OK" -eq 1 ] &&
   grep -qE "^diskcontend: a program \(bigio, writer 1\) appended 8 writes of 32 KiB to /home/contend\.bin through an O_APPEND open file while a kernel thread saved whenever something was dirty: [1-9][0-9]* saves \([1-9][0-9]* ended while the program ran\), 0 failed; the program finished \(exit status 0\); " "$DL_CT" &&
   grep -qE "^diskcontend: the file holds 262144 bytes, 8 blocks of 32 KiB each one write's words in order; the last sync: saved; write or FLUSH commands sent while the VFS lock was held, since boot: 0; " "$DL_CT" &&
   grep -qxF "diskcontend: DONE (the program and the saves both finished; the file exact; nothing sent under the VFS lock)" "$DL_CT"; then
    pass "[1.5] test=diskcontend: a program appended 8 writes of 32 KiB to /home/contend.bin while a kernel thread saved (saves ended while it ran, none failed); both finished (exit status 0); the file holds exactly the program's 262144 bytes; nothing sent under the VFS lock"
else
    fail "[1.5] test=diskcontend: not DONE, the program or the saves did not finish, a save failed, or the file is not exact (see serial-diskcontend.log)"
fi
DL_CTGOT=$(disk_image extract "$DL_W/contend-disk.img" "$DL_W/contend-part.img" > /dev/null 2>&1 &&
           disk_debugfs "$DL_W/contend-part.img" "cat /contend.bin" | sha256sum | cut -d' ' -f1)
DL_CTWANT=$(python3 -I -B -c 'import hashlib, struct; print(hashlib.sha256(b"".join(struct.pack("<Q", (1 << 56) | (c << 48) | k) for c in range(8) for k in range(4096))).hexdigest())')
echo "      diskcontend: /contend.bin on the image: SHA-256 ${DL_CTGOT:-none} (expected $DL_CTWANT)"
if dl_fsck contend && [ "$DL_CTGOT" = "$DL_CTWANT" ]; then
    pass "[1.5] test=diskcontend: after the boot e2fsck -fn reports nothing and debugfs reads /contend.bin with the program's SHA-256"
else
    fail "[1.5] test=diskcontend: e2fsck -fn reported something, or /contend.bin on the image differs from the program's bytes (see $DL_W)"
fi
if dl_health "$DL_CT" && [ "$(grep -cE "$DL_MOUNT0" "$DL_CT")" -eq 1 ] &&
   grep -qE "^mount: ext2 'madire-test' \(port 0, partition 1\) on /home as filesystem 1, read-write; " "$DL_CT"; then
    pass "[1.5] test=diskcontend: mounted read-write with save 0 under the disk lock only (0 write or FLUSH commands under the VFS lock at the mount); System ready; no PANIC or FAILED line"
else
    fail "[1.5] test=diskcontend: not mounted read-write, the mount's line not 0, or the boot was not healthy (see serial-diskcontend.log)"
fi

# (7-10) The self-check boots (Disk mode).
for dl_sc in "selfcheckdisklock|disk lock taken while the VFS lock is held \(L1\)|the disk lock taken while the VFS lock is held (L1)" \
             "selfcheckdiskrelock|disk lock taken again by its owner \(L3\)|the disk lock taken again by its owner (L3)"; do
    IFS='|' read -r dl_name dl_re dl_what <<< "$dl_sc"
    dl_disk "$dl_name" || DL_BOOT_OK=0
    mapfile -t DL_DRV < <(dl_drive "$dl_name")
    LOG=$OUT/serial-$dl_name.log
    boot_until "$LOG" 30 " System halted. Power off the machine." -m 128M -kernel "$KERNEL" -append "test=$dl_name" \
        "${DL_DRV[@]}"
    expect_lines "$LOG" "[1.5] self-check: $dl_what panics with its rule's text, naming the function and the thread (test=$dl_name)" \
        "$dl_re\$" "^ disklock_acquire: thread 0 'kernel'\.\$"
    if [ "$DL_BOOT_OK" -eq 1 ] && grep -q "System halted\. Power off the machine\." "$LOG" &&
       [ "$(grep -c '^boot32: start$' "$LOG")" = 1 ] && ! grep -qE "\(WRONG\)|System ready" "$LOG" &&
       grep -qE "^mount: ext2 'madire-test' \(port 0, partition 1\) on /home as filesystem 1, read-write; " "$LOG"; then
        pass "[1.5] self-check (test=$dl_name): in Disk mode (mounted read-write), the panic screen shown and the kernel halted; booted once, no (WRONG) and no System ready"
    else
        fail "[1.5] self-check (test=$dl_name): not mounted read-write, no halt, a reset, a returned take or System ready in $LOG"
    fi
done
echo "      [1.5] the per-disk I/O lock: $(( $(date +%s) - DL_T0 )) s (report only)"
