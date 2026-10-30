# Madire OS QEMU tests, part 1 (0.12, stage 0c): [0.3] to [0.9], and the
# [0.9.2] key-logging checks (they read this part's logs). Sourced by
# tests/qemu-tests.sh (a full run, or --shard 1). Apart from this header
# and the fixture call after the first line, every line was moved
# unchanged from the single script. 0.12 stage 2 adds the [0.12] stage 2
# console session at the end (shard 1 is the shortest).
# 0.13 adds the [0.13] checks of where Limine places the boot data (the
# temporary stage 1 A/B section was removed again in stage 3b). 1.0 stage 1
# adds the [1.0] filesystem checks of the normal boot at the end; stage 2
# the data budget and the self-test's data step after them. 1.1 stage 5
# adds a report-only trace probe at the end. 1.2 stage 3 added the probes
# (report only) at the end; stage 4 removed them again, and stage 5 removed
# the probe build and its tools (rerun them from 287b355, docs/testing.md).
# 1.2 stage 5 adds the [1.2] Display checks of the text boots at the end;
# stage 5b, after them, the text boots' slot 506 checks and four framebuffer
# test ISOs (slot 506, test=fbpattern and tests/qmp_fb.py; the undo path,
# test=fbfail; the size refusal at 32M). Stage 6 adds the framebuffer
# console at the end: test=fbcon in five modes (the 1024x768 boot also
# scrolls the console) and the crash screen
# against tests/fbcon_ref.py (tests/qmp_fbcon.py), and the serial twins
# (tests/serial_twin.py). Stage 7 adds the mouse at the end: test=mouse
# under -kernel, a mouselog session on the 1024x768 framebuffer console
# (the packets, the edges, the pointer and its save-under against
# tests/pointer_ref.py), and test=mouse at 1024x768 and 2560x1440 (the
# scale and the timing; tests/qmp_mouse.py), then the give-up path under
# -kernel (test=mousefail=4, =6 and =7, the keyboard checked after it). The
# crash case now waits for the halt line instead of "System ready.". Stage 8
# adds the splash at the end: test=splash still at 1024x768 and 1280x1024
# (four screens each against scripts/make-wallpaper.py, tests/qmp_splash.py)
# and its timing at 2560x1440, then Esc, the boot option verbose, a FAILED
# status line (test=splashfail) and a panic (test=splashpanic). Stage 10:
# the mouselog session gets the test option pointer (the console has no
# pointer without it), and a session without it follows; the splash cases
# take a fifth screen, the ready state at System ready, and the Esc case's
# first screen is that state; at the end, the early crash screen
# (test=earlypanic). 1.3 stage 8 (the switch): after the real ISO's slot
# 506, its default path (the desktop at System ready; the keylog ISO's
# too); the twins, verbose and test=splashfail sections gain a [1.3] check
# each; the two mouselog sessions get `verbose` (1.2's full-screen console);
# the Esc case presses Esc during the splash, and its ready-state check
# moved to part 2's handover section.

section "Normal boot (ISO through the boot menu)"
fixture_normal_boot     # 0.12: tests/qemu-lib.sh
expect "$LOG" "Logo: 36x24, $LOGO_PIXELS pixels lit" "logo rendered ($LOGO_PIXELS pixels, matches reference)"
expect "$LOG" "M A D I R E   O S"                     "title shown"
expect "$LOG" "Madire OS v$VRE"                      "version shown"
expect "$LOG" "GDT loaded: +OK"                       "GDT loaded"
expect "$LOG" "IDT loaded \(256 entries\): +OK"       "IDT loaded"
expect "$LOG" "Interrupt controller: +OK"             "PIC remapped and masked"
expect "$LOG" "Exception handling: +OK"               "int3 self-test returned"
expect "$LOG" "Display initialization: +OK"           "display initialised"
expect "$LOG" "System ready\."                        "reached System ready"
reject "$LOG" "FAILED|PANIC"                          "no failures or panic"
expect "$LOG" "Memory self-test: PASS"                "boot memory self-test passed"
expect "$LOG" "Memory initialization: +OK"            "memory status OK (after self-test)"
reject "$LOG" "PENDING"                               "no subsystem left pending"
expect "$LOG" "Physical RAM detected: +127 MB"        "panel: physical RAM detected"
expect "$LOG" "Usable RAM \(<4 GiB\): +127 MB"        "panel: usable RAM below 4 GiB"
expect "$LOG" "RAM above 4 GiB: +0 MB"                "panel: RAM above 4 GiB"
expect "$LOG" "Page size: +4096 bytes"                "panel: page size"
expect "$LOG" "Usable pages: +32639$"                 "panel: usable pages"
expect "$LOG" "Free pages: +[0-9]+$"                  "panel: free pages"
memory_check "$LOG" "ISO 128M" --usable-pages 32639 --ram-mb 127 --above4g-mb 0

# 0.4 additions to the normal boot
expect "$LOG" "Timer self-test: PASS \(PIT counting, IRQ0 delivered [0-9]+ ticks, divisor 11932\)" \
                                                      "[0.4] timer self-test: PIT counting, IRQ0 delivered"
expect "$LOG" "Timer \(PIT, 100 Hz\): +OK"            "[0.4] timer status OK"
expect "$LOG" "Keyboard self-test: PASS \(controller set up, echo via IRQ1, decoder and queue checked\)" \
                                                      "[0.4] keyboard self-test: controller, IRQ1, decoder, queue"
expect "$LOG" "Keyboard \(PS/2\): +OK"                "[0.4] keyboard status OK"
expect "$LOG" "IRQ mask: 0xfffc \(expected 0xfffc\)"  "[0.4] only IRQ0 and IRQ1 unmasked"
expect "$LOG" "^uptime: 5 s$"                         "[0.4] uptime advances (5 s logged)"
LOG=$KLOG                                             # 0.9.2: the keylog test ISO
expect "$LOG" "key: press sc=0x2d key=CHAR ascii='x' flags=0x01" \
                                                      "[0.4] key typed after boot reaches the kernel"
expect "$LOG" "key: release sc=0xad key=CHAR ascii='x' flags=0x00" \
                                                      "[0.4] key release reaches the kernel"
LOG=$OUT/serial-boot.log                              # 0.9.2: back to the real ISO
reject "$LOG" "Unexpected IRQ|events dropped"         "[0.4] no unexpected IRQs, no dropped keys"

# 0.5 additions to the normal boot
expect "$LOG" "Console self-test: PASS \(line editor ok, parser ok, shell ok, text grid ok\)" \
                                                      "[0.5] console/shell pure self-test passed"
expect "$LOG" "Console & shell: +OK"                  "[0.5] console status OK"
expect "$LOG" "System ready\. Press any key to open the console\." \
                                                      "[0.5] console offered after System ready"
LOG=$KLOG                                             # 0.9.2: the keylog test ISO
expect_in_order "$LOG" "[0.5] the typed key opened the console" \
    "System ready\. Press any key" "key: press sc=0x2d" "console: started"
LOG=$OUT/serial-boot.log                              # 0.9.2: back to the real ISO

# Memory at different RAM sizes (QEMU's own Multiboot loader). The pinned
# page counts come from the memory maps QEMU 8.2.2 reported when inspected
# with GRUB 2.12 lsmmap; the kernel itself always calculates them from the
# map. A different QEMU version may legitimately change them.
#   usable pages = 159 (below 640 KiB) + whole pages from 1 MiB to the
#   top-of-RAM reserved area; -m 4G also places 1 GiB above 4 GiB.
# ram_test <label> <-m size> <usable pages> <physical MB> <above 4 GiB MB>
ram_test() {
    local label=$1 mem=$2 pages=$3 ram=$4 above=$5
    section "Memory: $label"
    local log=$OUT/serial-mem-$label.log
    boot "$log" 10 -m "$mem" -kernel "$KERNEL"
    expect "$log" "System ready\." "$label: boots to System ready"
    reject "$log" "PANIC"          "$label: no panic"
    expect "$log" "Memory self-test: PASS" "$label: boot memory self-test passed"
    expect "$log" "Memory initialization: +OK" "$label: memory status OK"
    expect "$log" "RAM above 4 GiB: +$above MB" "$label: panel shows $above MB above 4 GiB"
    memory_check "$log" "$label" --usable-pages "$pages" --ram-mb "$ram" --above4g-mb "$above"
}

ram_test 16M  16M 3967   15   0
ram_test 32M  32M 8063   31   0
ram_test 1G   1G  262015 1023 0
ram_test 4G   4G  786303 4095 1024

section "Memory: boot module reserved (-initrd)"
LOG=$OUT/serial-mem-module.log
# 0.7: a test module (kernel/bootmod.h): "MADIRETM", payload length, FNV-1a
# of the payload, then 14000 random payload bytes. The kernel checks it after
# start-up; "corrupt" flips one payload byte after the checksum is taken.
make_test_module() {
    python3 - "$1" "$2" "${3:-}" <<'PY'
import os, struct, sys
path, length, corrupt = sys.argv[1], int(sys.argv[2]), sys.argv[3] == "corrupt"
payload = bytearray(os.urandom(length))
h = 0x811C9DC5
for b in payload:
    h = ((h ^ b) * 0x01000193) & 0xFFFFFFFF
if corrupt:
    payload[length // 2] ^= 0xFF
open(path, "wb").write(b"MADIRETM" + struct.pack("<II", length, h) + payload)
PY
}
make_test_module "$OUT/test-module.bin" 14000
boot "$LOG" 10 -m 32M -kernel "$KERNEL" -initrd "$OUT/test-module.bin"
expect "$LOG" "System ready\." "module: boots to System ready"
memory_check "$LOG" "module" --expect-module
expect "$LOG" "^Boot module 1: 0x[0-9a-f]{8}-0x[0-9a-f]{8} \(14016 bytes\): test pattern intact" \
                                                      "[0.7] module (QEMU -kernel/-initrd): contents intact after boot"

section "[0.7] Boot module: corruption is detected"
LOG=$OUT/serial-mem-module-corrupt.log
make_test_module "$OUT/test-module-corrupt.bin" 14000 corrupt
boot "$LOG" 10 -m 32M -kernel "$KERNEL" -initrd "$OUT/test-module-corrupt.bin"
expect "$LOG" "^Boot module 1: .*: test pattern CORRUPTED \(FNV-1a 0x[0-9a-f]{8}, expected 0x[0-9a-f]{8}\)" \
                                                      "[0.7] module: a changed byte is reported as CORRUPTED"

section "[0.7] Boot module through the boot menu (ISO with a module entry; 0.13: Limine)"
LOG=$OUT/serial-bootmenu-module.log
limine_tree "$OUT/isomod"
cp "$OUT/test-module.bin" "$OUT/isomod/boot/test-module.bin"
# 1.2 stage 5: "textmode: yes" (then in every entry of the real limine.conf;
# since stage 10b in its Diagnostics entries) keeps text mode now that the
# kernel's header asks for a framebuffer.
cat > "$OUT/isomod/boot/limine/limine.conf" <<'CFG'
timeout: 0

/Madire OS (module test)
    protocol: multiboot1
    textmode: yes
    path: boot():/boot/madire.kernel
    module_path: boot():/boot/test-module.bin
    module_string: test-module
CFG
if limine_iso "$OUT/isomod" "$OUT/madire-os-module.iso"; then
    boot "$LOG" 12 -m 128M -cdrom "$OUT/madire-os-module.iso"
    expect "$LOG" "^Boot module 1: 0x[0-9a-f]{8}-0x[0-9a-f]{8} \(14016 bytes\): test pattern intact" \
                                                      "[0.7] module (boot menu): contents intact after boot"
    expect "$LOG" "System ready\."                    "[0.7] module (boot menu): boots to System ready"
else
    fail "[0.7] module (boot menu): the test ISO could not be made"
fi

# exhaust_test <-m size>: allocate every free page, verify, free them all.
exhaust_test() {
    local mem=$1
    section "Memory exhaustion: $mem"
    local log=$OUT/serial-exhaust-$mem.log
    boot "$log" 20 -m "$mem" -kernel "$KERNEL" -append "test=memexhaust"
    local free inuse held expected
    free=$(grep -oE 'free_pages=[0-9]+' "$log" | head -1 | cut -d= -f2)
    # 0.6: paging (page tables + directory) and the kernel heap hold frames
    # before the test starts. The kernel reports them; the test must leave
    # exactly that many alone and allocate everything else.
    inuse=$(grep -oE 'frames in use before the test: [0-9]+ page tables \+ [0-9]+ directory \+ [0-9]+ heap pages = [0-9]+' "$log" | head -1)
    if [ -n "$free" ] && [ -n "$inuse" ]; then
        read -r t d hp held <<< "$(grep -oE '[0-9]+' <<< "$inuse" | tr '\n' ' ')"
        if [ $((t + d + hp)) -eq "$held" ]; then expected=$((free - held)); else held=""; fi
    fi
    expect "$log" "Memory exhaustion test: PASS \(${expected:-X} pages allocated, verified and freed; [0-9]+ checks; ${held:-X} pages already in use left alone\)" \
        "exhaust $mem: all ${expected:-?} free pages allocated, verified, freed (${free:-?} minus ${held:-?} paging + heap frames, exactly ${held:-?} left alone)"
    expect "$log" "System ready\." "exhaust $mem: system still ready afterwards"
    reject "$log" "PANIC"          "exhaust $mem: no panic"
    expect "$log" "IRQ mask: 0xfffc" "[0.4] exhaust $mem: ran with IRQ0/IRQ1 live"
}

exhaust_test 16M
exhaust_test 128M

section "Memory: double free must be caught"
LOG=$OUT/serial-doublefree.log
boot "$LOG" 8 -kernel "$KERNEL" -append "test=doublefree"
expect "$LOG" "KERNEL PANIC"                              "doublefree: crash screen shown"
expect "$LOG" "Double free of physical page 0x[0-9a-f]{8}" "doublefree: reported with page address"
reject "$LOG" "System ready|not detected"                  "doublefree: halted, detection worked"

section "Memory: freeing a reserved page must be caught"
LOG=$OUT/serial-freereserved.log
boot "$LOG" 8 -kernel "$KERNEL" -append "test=freereserved"
expect "$LOG" "KERNEL PANIC"                                "freereserved: crash screen shown"
expect "$LOG" "Attempt to free reserved page 0x00100000"    "freereserved: kernel page rejected"
reject "$LOG" "System ready|not detected"                    "freereserved: halted, detection worked"

# crash_test <name> <vector> <exception name> <error code>
crash_test() {
    local name=$1 vector=$2 exname=$3 errcode=$4
    section "Crash test: $name"
    local log=$OUT/serial-$name.log
    boot "$log" 8 -kernel "$KERNEL" -append "test=$name"
    expect "$log" "KERNEL PANIC"                        "$name: crash screen shown"
    expect "$log" "CPU exception $vector: $exname"      "$name: reports vector $vector ($exname)"
    expect "$log" "Error code: 0x$errcode"              "$name: error code 0x$errcode"
    expect "$log" "RIP=0x[0-9a-f]{16}  CS=0x0008"       "$name: registers dumped, CS is our GDT code segment"
    reject "$log" "System ready"                        "$name: halted instead of continuing"
    expect "$log" "IRQ mask: 0xfffc"                    "[0.4] $name: exception raised with IRQ0/IRQ1 live"
}

crash_test divzero 0  "Divide Error"             00000000
crash_test ud      6  "Invalid Opcode"           00000000
crash_test gpf     13 "General Protection Fault" 00000028

# ---- 0.4: timer, keyboard and IRQ layer ----

section "[0.4] Timer rate against the RTC"
# Deterministic time (-icount shift=0,sleep=off -rtc clock=vm) was tried and
# does not reach a result: the diagnostic busy-polls the RTC and each port
# access costs far more real time than the virtual time it advances. The
# documented fallback is QEMU's normal clock with the kernel's +-10% band.
LOG=$OUT/serial-timer.log
boot "$LOG" 15 -m 32M -kernel "$KERNEL" -append "test=timer"
expect "$LOG" "Timer diagnostic: PASS \([0-9]+ ticks in 3 RTC seconds, expected 270-330\)" \
                                                      "[0.4] timer: 300 +-10% ticks in 3 RTC seconds"
grep -o "Timer diagnostic: [A-Z]* ([0-9]* ticks" "$LOG" | sed 's/^/      measured: /'
expect "$LOG" "System ready\."                        "[0.4] timer: system ready afterwards"
reject "$LOG" "PANIC"                                 "[0.4] timer: no panic"

section "[0.4] Keyboard: scripted keys via QMP"
LOG=$OUT/serial-keyboard.log
boot_keys "$LOG" 20 "Keyboard test: ready" \
    "a,shift+b,caps_lock,a,caps_lock,shift+1,up,ctrl_r,ret,kp_enter" 0.15 \
    -m 32M -kernel "$KERNEL" -append "test=keyboard"
expect_in_order "$LOG" "[0.4] keyboard: every key decoded, in order" \
    "key: press sc=0x1e key=CHAR ascii='a' flags=0x01" \
    "key: release sc=0x9e key=CHAR ascii='a' flags=0x00" \
    "key: press sc=0x2a key=LSHIFT" \
    "key: press sc=0x30 key=CHAR ascii='B' flags=0x03" \
    "key: press sc=0x3a key=CAPSLOCK ascii=none flags=0x11" \
    "key: press sc=0x1e key=CHAR ascii='A' flags=0x11" \
    "key: press sc=0x3a key=CAPSLOCK ascii=none flags=0x01" \
    "key: press sc=0x02 key=CHAR ascii='!' flags=0x03" \
    "key: press sc=0x48 key=UP ascii=none flags=0x21" \
    "key: press sc=0x1d key=RCTRL ascii=none flags=0x25" \
    "key: press sc=0x1c key=ENTER ascii=none flags=0x01" \
    "key: press sc=0x1c key=KP_ENTER ascii=none flags=0x21"
expect "$LOG" "key: release sc=0x9d key=RCTRL"        "[0.4] keyboard: extended key release decoded"
reject "$LOG" "events dropped|UNKNOWN|PANIC"          "[0.4] keyboard: nothing dropped or unknown"

section "[0.4] Keyboard queue overflow"
LOG=$OUT/serial-kbdflood.log
# 50 keys = 100 events (make + break) sent while the kernel is not reading
# for 3 s. The queue must end up full (64 kept, oldest first) and every
# other event must be accounted for: dropped (counted) or, if it arrived
# after the 3 s window, delivered normally afterwards. How many arrive
# inside the window depends on host timing, so the check is conservation
# (kept + dropped + late = 100), not a fixed dropped count.
FLOOD=$(printf 'a,b,c,d,e,f,g,h,i,j,k,l,m,n,o,p,q,r,s,t,u,v,w,x,y,%.0s' 1 2)
boot_keys "$LOG" 20 "Keyboard flood test: ready" "$FLOOD" 0.02 \
    -m 32M -kernel "$KERNEL" -append "test=kbdflood"
expect "$LOG" "Keyboard flood test: kept=64 dropped=[1-9][0-9]* \(queue holds 64\)" \
                                                      "[0.4] flood: queue full (64 kept), overflow dropped and counted"
KEPT=$(grep -oE 'kept=[0-9]+' "$LOG" | head -1 | cut -d= -f2)
DROPPED=$(grep -oE 'kept=[0-9]+ dropped=[0-9]+' "$LOG" | head -1 | sed 's/.*dropped=//')
LATE=$(awk '/Keyboard flood test: kept=/ { after = 1; next } after && /^key: / { n++ } END { print n + 0 }' "$LOG")
echo "      kept=${KEPT:-?} dropped=${DROPPED:-?} late=$LATE"
if [ "$(( ${KEPT:-0} + ${DROPPED:-0} + LATE ))" -eq 100 ]; then
    pass "[0.4] flood: all 100 events accounted for (kept + dropped + late)"
else
    fail "[0.4] flood: kept ${KEPT:-?} + dropped ${DROPPED:-?} + late $LATE != 100 events sent"
fi
expect "$LOG" "Keyboard: ${DROPPED:-X} events dropped \(queue full\)" \
                                                      "[0.4] flood: drop count reported from normal context"
expect_in_order "$LOG" "[0.4] flood: kept events are the oldest, in order" \
    "Keyboard flood test: ready" \
    "key: press sc=0x1e key=CHAR ascii='a'" \
    "key: release sc=0x9e key=CHAR ascii='a'" \
    "key: press sc=0x30 key=CHAR ascii='b'" \
    "key: release sc=0xb0 key=CHAR ascii='b'"
expect "$LOG" "System ready\."                        "[0.4] flood: system ready afterwards"
reject "$LOG" "PANIC"                                 "[0.4] flood: no panic"

section "[0.4] Unexpected IRQ"
LOG=$OUT/serial-irqunexpected.log
boot "$LOG" 8 -m 32M -kernel "$KERNEL" -append "test=irqunexpected"
expect "$LOG" "Unexpected IRQ test: PASS \(IRQ 2 counted and acknowledged, total 1\)" \
                                                      "[0.4] unexpected IRQ counted and acknowledged"
expect "$LOG" "Unexpected IRQ 2 \(total 1\)"          "[0.4] unexpected IRQ reported from normal context"
expect "$LOG" "System ready\."                        "[0.4] unexpected IRQ: system ready afterwards"
reject "$LOG" "PANIC"                                 "[0.4] unexpected IRQ: no panic"

# ---- 0.5: console and shell, typed through the real keyboard path ----
# Every key goes QMP -> i8042 -> IRQ1 -> 0.4 driver -> decoder -> key queue
# -> idle loop -> console. The console copies each submitted line to serial
# as "madire> <line>" followed by the command's output lines.

section "[0.5] Console session (scripted keys via QMP)"
LOG=$OUT/serial-console.log
A100=$(printf 'a%.0s' $(seq 1 100))
S="x{wait:1}"                                        # first key: only opens the console
S+="{ret}"                                           # 1  empty line
S+="   {ret}"                                        # 2  blank line
S+="help{ret}"                                       # 3
S+="version{ret}"                                    # 4
S+="uptime{ret}{wait:2}uptime{ret}"                  # 5, 6
S+="mem{ret}"                                        # 7
S+="cpu{ret}"                                        # 8
S+="echo   Hello   World!{ret}"                      # 9  Shift + multiple spaces
S+="ech{tab}a {tab}b{ret}"                          # 10 Tab completes the command name (1.1)
S+="{caps}echo abc{caps}{ret}"                       # 11 Caps Lock
S+="echo hellx{bs}o{ret}"                            # 12 Backspace
S+="echo abXc{left}{left}{del}{ret}"                 # 13 Left + Delete
S+="cho world{home}e{end}!{ret}"                     # 14 Home / End / insert
S+="echo hllo{home}{right}{right}{right}{right}{right}{right}e{end}{ret}"   # 15 Right
S+="{ctrl+x}{alt+x}echo ok{ret}"                     # 16 Ctrl / Alt ignored
S+="foo{ret}"                                        # 17 unknown command
S+="version x{ret}"                                  # 18 argument error
S+="echo 1 2 3 4 5 6 7 8{ret}"                       # 19 too many words
S+="echo 1 2 3 4 5 6 7{ret}"                         # 20 exactly 8 words
S+="echo ${A100}{ret}"                               # 21 long input
S+="version{ret}version{ret}version{ret}version{ret}version{ret}"   # 22-26
S+="help{ret}help{ret}help{ret}"                     # 27-29 enough output to scroll
S+="clear{ret}"                                      # 30
S+="version{ret}"                                    # 31
S+="echo end-of-console-test{ret}"                   # 32 marks the end of the session
boot_text "$LOG" 180 "System ready." "$S" 0.03 $'\nend-of-console-test\n' -kernel "$KERNEL"
A66=$(printf 'a%.0s' $(seq 1 66))

expect_in_order "$LOG" "[0.5] console opens on the first key, which is not typed" \
    "System ready\. Press any key" "console: started" "^madire> $"
PROMPTS=$(grep -c '^madire> ' "$LOG")
if [ "$PROMPTS" -eq 32 ]; then pass "[0.5] Enter: all 32 lines submitted"
else fail "[0.5] Enter: expected 32 submitted lines, got $PROMPTS"; fi
expect_next "$LOG" "[0.5] empty line: new prompt, no output"   "^madire> $"      "^madire>    $"
expect_next "$LOG" "[0.5] blank line: new prompt, no output"   "^madire>    $"   "^madire> help$"
# 1.0 stage 10 (D2): help lists the names by section, several per line
# (the purposes: help <section>, help <command>).
expect_in_order "$LOG" "[0.5] help lists all seven commands" \
    "^madire> help$" "^Available commands:$" "^shell: The shell's own commands$" \
    "^  help  clear  echo  " "^system: Information about the system$" "^  version  uptime  mem  cpu  "
expect_next "$LOG" "[0.5] version"                 "^madire> version$" "^Madire OS v$VRE \(64-bit x86-64, Multiboot 1\)$"
UP=$(grep -A1 '^madire> uptime$' "$LOG" | grep -oE '^Uptime: [0-9]{2}:[0-9]{2}:[0-9]{2} \([0-9]+ ticks at 100 Hz\)$')
hms_to_s() { grep -oE '[0-9]{2}:[0-9]{2}:[0-9]{2}' | awk -F: '{ print $1 * 3600 + $2 * 60 + $3 }'; }
U1=$(sed -n 1p <<< "$UP" | hms_to_s)
U2=$(sed -n 2p <<< "$UP" | hms_to_s)
echo "      uptime: ${U1:-?} s, then ${U2:-?} s"
if [ -n "$U1" ] && [ -n "$U2" ] && [ "$U2" -ge $((U1 + 1)) ]; then
    pass "[0.5] uptime: HH:MM:SS with ticks, and it advances"
else
    fail "[0.5] uptime output missing or not advancing ($UP)"
fi
expect_in_order "$LOG" "[0.5] mem shows the 0.3 memory figures" \
    "^madire> mem$" "^Physical RAM detected: +127 MB$" "^Usable RAM \(<4 GiB\): +127 MB$" \
    "^RAM above 4 GiB: +0 MB$" "^Page size: +4096 bytes$" "^Usable pages: +32639$" \
    "^Free pages \(now\): +[0-9]+$" "^Reserved ranges: +[0-9]+$"
expect_in_order "$LOG" "[0.5] cpu reports CPUID information" \
    "^madire> cpu$" "^CPUID: +supported$" "^Vendor: +[A-Za-z]+" \
    "^Family/model: [0-9]+ / [0-9]+, stepping [0-9]+$" "^Features: +[a-z]" \
    "^Mode: +64-bit long mode, paging on, 1 CPU in use$"
expect_next "$LOG" "[0.5] Shift and multiple spaces"   "^madire> echo   Hello   World!$" "^Hello World!$"
expect_next "$LOG" "[1.1] Tab completes the command name (ech: echo), no tab character typed" "^madire> echo a b$" "^a b$"
expect_next "$LOG" "[0.5] Caps Lock (and names are case-sensitive)" \
    "^madire> ECHO ABC$" "^Unknown command: ECHO\. Type 'help' for a list of commands\.$"
# Only the final, edited line is logged: "hellx" + Backspace + "o" -> "hello".
expect_in_order "$LOG" "[0.5] Backspace" "^madire> ECHO ABC$" "^madire> echo hello$" "^hello$"
reject "$LOG" "hellx"                                 "[0.5] Backspace: the deleted character is gone"
expect_next "$LOG" "[0.5] Left and Delete"             "^madire> echo abc$"    "^abc$"
expect_next "$LOG" "[0.5] Home, End and insert at cursor" "^madire> echo world!$" "^world!$"
expect_in_order "$LOG" "[0.5] Right and insert at cursor" \
    "^madire> echo world!$" "^madire> echo hello$" "^hello$"
expect_next "$LOG" "[0.5] Ctrl+key and Alt+key ignored" "^madire> echo ok$"   "^ok$"
expect_next "$LOG" "[0.5] unknown command"             "^madire> foo$" \
    "^Unknown command: foo\. Type 'help' for a list of commands\.$"
expect_next "$LOG" "[0.5] argument error shows usage"  "^madire> version x$" "^Usage: version$"
expect_next "$LOG" "[0.5] more than 8 words refused"   "^madire> echo 1 2 3 4 5 6 7 8$" \
    "^Too many arguments \(max 8\)\.$"
expect_next "$LOG" "[0.5] exactly 8 words accepted"    "^madire> echo 1 2 3 4 5 6 7$" "^1 2 3 4 5 6 7$"
expect "$LOG" "^console: input line full \(71 characters\)$" "[0.5] long input: refusal logged"
expect_next "$LOG" "[0.5] long input: exactly 71 characters kept" "^madire> echo ${A66}$" "^${A66}$"
VERSIONS=$(grep -c "^Madire OS v$VRE (64-bit x86-64, Multiboot 1)\$" "$LOG")
if [ "$VERSIONS" -eq 7 ]; then pass "[0.5] repeated commands: 7 identical version outputs"
else fail "[0.5] repeated commands: expected 7 version outputs, got $VERSIONS"; fi
expect "$LOG" "^console: scrolled [0-9]+ lines$"      "[0.5] output scrolls"
expect_in_order "$LOG" "[0.5] clear, then the console keeps working" \
    "^madire> clear$" "^console: cleared$" "^madire> version$" "^Madire OS v$VRE"
reject "$LOG" "events dropped|PANIC|Unexpected IRQ"   "[0.5] no dropped keys, no panic, no unexpected IRQs"

# ---- 0.6: paging and kernel heap ----

section "[0.6] Paging and heap at boot (ISO)"
LOG=$OUT/serial-boot.log
expect "$LOG" "^Paging walk-check: PASS \([0-9]+ items, 0 mismatches\)$" "[0.6] walk-check before enabling: PASS"
expect "$LOG" "^Paging check: page 0 .*expected unmapped, actual unmapped .*: OK$" \
                                                      "[0.6] page 0 (NULL, BIOS data area) left unmapped"
expect "$LOG" "^Paging self-test: TLB flush on remap: .*: OK$"      "[0.6] self-test: TLB flush on remap"
expect "$LOG" "^Paging self-test: physical-access window: .*: OK$" "[0.6] self-test: window map, access, unmap"
expect "$LOG" "^Paging self-test: no page leak: free pages ([0-9]+) -> \1: OK$" \
                                                      "[0.6] paging self-test: no page leak"
expect "$LOG" "^Paging self-test: PASS \([0-9]+ checks, 0 mismatches\)$" "[0.6] paging self-test: PASS"
# 0.7: 4-level paging in long mode also needs CR4.PAE and EFER.LMA.
REGS=$(grep -oE '^Paging: enabled; CR0=0x[0-9a-f]{8} CR3=0x[0-9a-f]{16} CR4=0x[0-9a-f]{16} EFER=0x[0-9a-f]{16}' "$LOG" | head -1)
CR0=$(grep -oE 'CR0=0x[0-9a-f]{8}' <<< "$REGS" | cut -d= -f2)
CR3=$(grep -oE 'CR3=0x[0-9a-f]{16}' <<< "$REGS" | cut -d= -f2)
CR4=$(grep -oE 'CR4=0x[0-9a-f]{16}' <<< "$REGS" | cut -d= -f2)
EFER=$(grep -oE 'EFER=0x[0-9a-f]{16}' <<< "$REGS" | cut -d= -f2)
echo "      ${REGS:-no CR0/CR3 line}"
if [ -n "$CR0" ] && [ $(( CR0 & 0x80010000 )) -eq $(( 0x80010000 )) ] &&
   [ -n "$CR3" ] && [ $(( CR3 )) -ne 0 ] && [ $(( CR3 & 0xfff )) -eq 0 ] &&
   [ -n "$CR4" ] && [ $(( CR4 & 0x20 )) -ne 0 ] && [ -n "$EFER" ] && [ $(( EFER & 0x400 )) -ne 0 ]; then
    pass "[0.6] CR0.PG, CR0.WP, CR4.PAE and EFER.LMA set, CR3 holds a page-aligned PML4"
else
    fail "[0.6] CR0/CR3 after enabling paging: ${REGS:-missing}"
fi
expect "$LOG" "^Paging enabled: +OK"                  "[0.6] status: paging enabled OK"
expect "$LOG" "^Kernel heap self-test: no page leak: .*: OK$" "[0.6] heap self-test: no page leak"
expect "$LOG" "^Kernel heap self-test: PASS \([0-9]+ checks, 0 mismatches\)$" "[0.6] heap self-test: PASS"
expect "$LOG" "^Kernel heap: +OK"                     "[0.6] status: kernel heap OK"
reject "$LOG" "MISMATCH"                              "[0.6] no walk-check or self-test mismatches"

section "[0.6] Paging and heap at every RAM size"
for label in 16M 32M 1G 4G module; do
    log=$OUT/serial-mem-$label.log
    expect "$log" "^Paging enabled: +OK" "[0.6] $label: paging enabled"
    expect "$log" "^Kernel heap: +OK"    "[0.6] $label: kernel heap OK"
done

# pf_test <name> <CR2> <decoded line>: a deliberate page fault.
pf_test() {
    local name=$1 cr2=$2 decoded=$3
    section "[0.6] Page fault: $name"
    local log=$OUT/serial-$name.log
    boot "$log" 8 -m 32M -kernel "$KERNEL" -append "test=$name"
    expect "$log" "CPU exception 14: Page Fault \(#PF\)" "[0.6] $name: exception 14"
    expect "$log" "CR2=0x$cr2"                           "[0.6] $name: CR2=0x$cr2"
    # 0.7: the address has its own line (no crash-screen line reaches column 80).
    expect_in_order "$log" "[0.6] $name: decoded: $decoded" \
        "^ Page fault: $decoded$" "^ Address: 0x$cr2$"
    reject "$log" "System ready|not detected"            "[0.6] $name: halted, fault raised"
}
pf_test pfnull     0000000000000000 "not present, read, kernel mode"
pf_test pfunmapped 0000000040000000 "not present, read, kernel mode"
pf_test pfreadonly ffffffff80100000 "protection violation, write, kernel mode"   # 0.8: kernel code is high

# heap_panic_test <name> <message regex>: a kfree misuse must be caught.
heap_panic_test() {
    local name=$1 message=$2
    section "[0.6] Heap: $name"
    local log=$OUT/serial-$name.log
    boot "$log" 8 -m 32M -kernel "$KERNEL" -append "test=$name"
    expect "$log" "KERNEL PANIC"                         "[0.6] $name: crash screen shown"
    expect "$log" "$message"                             "[0.6] $name: reported"
    reject "$log" "System ready|not detected"            "[0.6] $name: halted, detection worked"
}
heap_panic_test heapdouble  "Heap: double free of 0xfffffe80000[0-9a-f]{5}\."
heap_panic_test heapinvalid "Heap: invalid free of 0x[0-9a-f]{16} \(pointer not from the heap\)\."
heap_panic_test heapcorrupt "Heap: corrupted block at 0xfffffe80000[0-9a-f]{5} \(freeing 0xfffffe80000[0-9a-f]{5}\)\."

section "[0.6] Heap exhaustion (32M: the 16 MiB cap is reachable)"
LOG=$OUT/serial-heapexhaust.log
boot "$LOG" 30 -m 32M -kernel "$KERNEL" -append "test=heapexhaust"
expect "$LOG" "Heap exhaustion test: [0-9]+ blocks, arena 16384 KiB \(16 MiB cap reached\), [0-9]+ free pages left, further kmalloc still NULL" \
                                                      "[0.6] heapexhaust: NULL at the 16 MiB cap, and still NULL after"
expect "$LOG" "Heap exhaustion test: PASS \([0-9]+ blocks freed; heap check OK;" \
                                                      "[0.6] heapexhaust: everything freed, heap check OK"
ACCT=$(grep -oE 'free pages [0-9]+ -> [0-9]+ = [0-9]+ heap pages \+ [0-9]+ page tables' "$LOG" | head -1)
read -r HB HA HG HT <<< "$(grep -oE '[0-9]+' <<< "$ACCT" | tr '\n' ' ')"
echo "      ${ACCT:-no page accounting line}"
if [ -n "$HT" ] && [ $((HB - HA)) -eq $((HG + HT)) ]; then
    pass "[0.6] heapexhaust: pages used are exactly the heap's growth plus its page tables"
else
    fail "[0.6] heapexhaust: page accounting does not add up (${ACCT:-missing})"
fi
expect "$LOG" "System ready\."                        "[0.6] heapexhaust: system ready afterwards"
reject "$LOG" "PANIC"                                 "[0.6] heapexhaust: no panic"

section "[0.6] Console: mem shows paging and heap"
LOG=$OUT/serial-console.log
expect_in_order "$LOG" "[0.6] mem: paging and heap sections, in order" \
    "^madire> mem$" "^Reserved ranges: +[0-9]+$" \
    "^Paging: +enabled \(4-level, 4 KiB, higher-half, NX (on|off)\)$" "^Mapped pages: +[0-9]+$" \
    "^Page tables: +[0-9]+$" "^Kernel heap: +[0-9]+ KiB mapped, max 16384 KiB$" \
    "^Heap in use: +[0-9]+ KiB in [0-9]+ blocks?$" "^Heap free: +[0-9]+ KiB, largest block [0-9]+ KiB$"

# ---- 0.7: long mode, 4-level layout ----

section "[0.7] Long mode active (normal boot, ISO)"
LOG=$OUT/serial-boot.log
expect_in_order "$LOG" "[0.7] stub markers: boot32 start, CPU checks passed, entered 64-bit mode" \
    "^boot32: start$" "^boot32: CPUID ok, long mode and PAE present$" "^boot64: entered 64-bit long mode$"
MODE=$(grep -m1 -oE '^CPU mode: EFER=0x[0-9a-f]{16} .* CR0=0x[0-9a-f]{8} .* CR4=0x[0-9a-f]{16}' "$LOG")
read -r ME MC MR <<< "$(grep -oE '(EFER|CR0|CR4)=0x[0-9a-f]+' <<< "$MODE" | cut -d= -f2 | tr '\n' ' ')"
echo "      ${MODE:-no CPU mode line}"
if [ -n "${MR:-}" ] && [ $(( ME & 0x100 )) -ne 0 ] && [ $(( ME & 0x400 )) -ne 0 ] &&
   [ $(( MC & 0x1 )) -ne 0 ] && [ $(( MC & 0x80000000 )) -ne 0 ] && [ $(( MR & 0x20 )) -ne 0 ]; then
    pass "[0.7] EFER.LME and LMA, CR0.PE and PG, CR4.PAE set (bits checked by the script)"
else
    fail "[0.7] long-mode control bits: ${MODE:-missing}"
fi
DESC=$(grep -m1 -oE '^CPU mode: CS=0x0008 descriptor=0x[0-9a-f]{16} \(L=1 D=0\) TR=0x0018' "$LOG" |
       grep -oE 'descriptor=0x[0-9a-f]{16}' | cut -d= -f2)
if [ -n "$DESC" ] && [ $(( (DESC >> 53) & 1 )) -eq 1 ] && [ $(( (DESC >> 54) & 1 )) -eq 0 ]; then
    pass "[0.7] CS=0x0008 is a 64-bit code segment (descriptor $DESC: L=1, D=0)"
else
    fail "[0.7] CS descriptor not 64-bit code (${DESC:-missing})"
fi
expect "$LOG" "^CPU mode: CS=0x0008 .* TR=0x0018 "     "[0.7] task register 0x18 (the 64-bit TSS is loaded)"
expect "$LOG" "^CPU mode: 64-bit long mode active; 64-bit arithmetic PASS$" \
                                                      "[0.7] kernel reports long mode active; 64-bit multiply/divide above 2^32 correct"
expect "$LOG" "^Paging check: table walk counts .*, actual as expected: OK$" \
                                                      "[0.7] 4-level walk: independent recount of tables and pages matches"

# 0.8: the kernel runs in the higher half (0xFFFFFFFF80000000 + physical);
# the check is the same (RIP lies inside kernel code, which starts at the
# kernel's first page), at the new address.
section "[0.7] Kernel at its higher-half address; PML4 slots 508-510 in use"
RIPL=$(grep -m1 -oE '^Paging check: RIP \(running code\) 0x[0-9a-f]{16}-0x[0-9a-f]{16}: expected kernel ro exec, actual kernel ro exec \(1 pages\): OK$' "$LOG")
CODEL=$(grep -m1 -oE '^Paging check: kernel code 0xffffffff80100000-0x[0-9a-f]{16}: expected kernel ro exec, actual kernel ro exec \([0-9]+ pages\): OK$' "$LOG")
RIPA=$(grep -oE '0x[0-9a-f]{16}' <<< "$RIPL" | head -1)
CODEE=$(grep -oE '0x[0-9a-f]{16}' <<< "$CODEL" | sed -n 2p)
# Both are "negative" as signed 64-bit numbers; their order is preserved.
if [ -n "$RIPA" ] && [ -n "$CODEE" ] && [ $((RIPA)) -ge $((0xffffffff80100000)) ] && [ $((RIPA)) -lt $((CODEE)) ]; then
    pass "[0.7] kernel runs at its higher-half address: RIP $RIPA in kernel code 0xffffffff80100000-$CODEE"
else
    fail "[0.7] kernel higher-half address: RIP ${RIPA:-?}, code end ${CODEE:-?}"
fi
pml4() { echo $(( ($1 >> 39) & 511 )); }
TESTP=$(grep -m1 -oE '^Paging self-test: map and read back: 0x[0-9a-f]{16} -> A, .*: OK$' "$LOG" | grep -oE '0x[0-9a-f]{16}' | head -1)
WINP=$(grep -m1 -oE '^Paging check: window page table present 0x[0-9a-f]{16}-.*: OK$' "$LOG" | grep -oE '0x[0-9a-f]{16}' | head -1)
if [ -n "$TESTP" ] && [ -n "$WINP" ] && [ "$(pml4 "$TESTP")" -eq 508 ] && [ "$(pml4 "$WINP")" -eq 508 ]; then
    pass "[0.7] PML4[508]: self-test page $TESTP mapped and read back; window table present at $WINP"
else
    fail "[0.7] PML4[508] in use (test page ${TESTP:-?}, window ${WINP:-?})"
fi
HEAPB=$(grep -m1 -oE '^Kernel heap: [0-9]+ KiB at 0x[0-9a-f]{16}, ' "$LOG" | grep -oE '0x[0-9a-f]{16}')
if [ -n "$HEAPB" ] && [ "$(pml4 "$HEAPB")" -eq 509 ] && grep -qE '^Kernel heap self-test: PASS' "$LOG"; then
    pass "[0.7] PML4[509]: kernel heap mapped at $HEAPB, heap self-test PASS"
else
    fail "[0.7] PML4[509] in use (heap ${HEAPB:-?})"
fi
RECA=$(grep -m1 -oE '^Paging self-test: recursive mapping: PML4\[510\] read at 0x[0-9a-f]{16} = 0x[0-9a-f]{16}: OK$' "$LOG" | grep -oE '0x[0-9a-f]{16}' | head -1)
if [ -n "$RECA" ] && [ "$(pml4 "$RECA")" -eq 510 ] &&
   grep -qE '^Paging check: recursive entry \(PML4\[510\]\) .*: expected points to the PML4, rw nx, actual as expected: OK$' "$LOG"; then
    pass "[0.7] PML4[510]: recursive entry points to the PML4, read back through $RECA"
else
    fail "[0.7] PML4[510] recursive entry (${RECA:-missing})"
fi

# ---- 0.7: no-execute (NX) ----

section "[0.7] NX at boot (QEMU default CPU: NX supported)"
LOG=$OUT/serial-boot.log
expect "$LOG" "^NX: supported by the CPU \(CPUID 0x80000001 EDX bit 20\); EFER.NXE enabled \(EFER=0x[0-9a-f]{16}, LMA still set\); NX bits used for everything but kernel code$" \
                                                      "[0.7] NX: supported by the CPU, EFER.NXE enabled, LMA kept"
EFER=$(grep -oE '^Paging: enabled; .* EFER=0x[0-9a-f]{16}' "$LOG" | grep -oE 'EFER=0x[0-9a-f]{16}' | head -1 | cut -d= -f2)
if [ -n "$EFER" ] && [ $(( EFER & 0x800 )) -ne 0 ] && [ $(( EFER & 0x400 )) -ne 0 ]; then
    pass "[0.7] EFER after loading the kernel tables: NXE and LMA set ($EFER)"
else
    fail "[0.7] EFER after loading the kernel tables: ${EFER:-missing}"
fi
expect "$LOG" "^Paging check: kernel code .*: expected kernel ro exec, actual kernel ro exec .*: OK$" \
                                                      "[0.7] kernel code executable (no NX bit)"
expect "$LOG" "^Paging check: interrupt entry stubs \(isr.asm\) .*: expected kernel ro exec, .*: OK$" \
                                                      "[0.7] interrupt entry stubs executable"
expect "$LOG" "^Paging check: panic_exception \(page-fault path\) .*: expected kernel ro exec, .*: OK$" \
                                                      "[0.7] page-fault handler executable"
expect "$LOG" "^Paging check: irq_dispatch \(timer, keyboard\) .*: expected kernel ro exec, .*: OK$" \
                                                      "[0.7] IRQ handlers (timer, keyboard) executable"
expect "$LOG" "^Paging check: kernel rodata .*: expected kernel ro nx, actual kernel ro nx .*: OK$" \
                                                      "[0.7] kernel rodata no-execute"
expect "$LOG" "^Paging check: kernel data\+bss .*: expected kernel rw nx, actual kernel rw nx .*: OK$" \
                                                      "[0.7] kernel data, bss, bitmaps no-execute"
expect "$LOG" "^Paging check: IST1 stack \(double faults\) .*: expected kernel rw nx, actual kernel rw nx .*: OK$" \
                                                      "[0.7] IST1 stack no-execute"
expect "$LOG" "^Paging check: RSP \(current stack\) .*: expected kernel rw nx, actual kernel rw nx .*: OK$" \
                                                      "[0.7] current stack no-execute"
expect "$LOG" "^Kernel heap: [0-9]+ KiB at 0xfffffe8000000000, max 16384 KiB, pages no-execute \(NX bit set\)$" \
                                                      "[0.7] heap pages no-execute"
expect_in_order "$OUT/serial-console.log" "[0.7] mem: NX supported, enabled" \
    "^madire> mem$" "^Page tables: +[0-9]+$" "^NX: +supported, enabled$"

section "[0.7] NX: instruction fetch from a no-execute page (test=pfnx)"
LOG=$OUT/serial-pfnx.log
boot "$LOG" 8 -m 32M -kernel "$KERNEL" -append "test=pfnx"
expect "$LOG" "^NX test: executable page: kernel code at 0x[0-9a-f]{16} \(no NX bit\) ran, returned 0x5a5a5a5a \(correct\)$" \
                                                      "[0.7] pfnx: kernel code ran first"
NXPAGE=$(grep -oE '^NX test: data page at 0x[0-9a-f]{16} \(NX bit set\)' "$LOG" | grep -oE '0x[0-9a-f]{16}' | head -1)
echo "      data page: ${NXPAGE:-missing}"
expect "$LOG" "^NX test: data page at ${NXPAGE:-X} \(NX bit set\); NX in use: this must fault; calling it$" \
                                                      "[0.7] pfnx: target page has the NX bit"
expect "$LOG" "CPU exception 14: Page Fault \(#PF\)"  "[0.7] pfnx: exception 14"
expect "$LOG" "Error code: 0x00000011"                "[0.7] pfnx: error code 0x11 (present, instruction fetch)"
expect_in_order "$LOG" "[0.7] pfnx: decoded as an NX violation at the page" \
    "^ Page fault: protection violation, read, kernel mode$" \
    "^ Details: instruction fetch \(NX violation\)$" "^ Address: ${NXPAGE:-X}$"
expect "$LOG" "CR2=${NXPAGE:-X}"                      "[0.7] pfnx: CR2 is the fetched address"
reject "$LOG" "System ready|not detected"             "[0.7] pfnx: halted, fault raised"

section "[0.7] CPU without NX (-cpu qemu64,-nx)"
LOG=$OUT/serial-nonx.log
boot "$LOG" 10 -cpu qemu64,-nx -m 32M -kernel "$KERNEL"
expect "$LOG" "^NX: not supported by this CPU \(CPUID 0x80000001 EDX bit 20 clear\); EFER.NXE left clear, no NX bits in the page tables" \
                                                      "[0.7] no NX: reported as not supported"
expect "$LOG" "^Paging walk-check: PASS \([0-9]+ items, 0 mismatches\)$" "[0.7] no NX: walk-check PASS"
expect "$LOG" "^Paging check: kernel data\+bss .*: expected kernel rw exec, actual kernel rw exec .*: OK$" \
                                                      "[0.7] no NX: no NX bits in the tables"
expect "$LOG" "^Paging self-test: PASS \([0-9]+ checks, 0 mismatches\)$" "[0.7] no NX: paging self-test PASS"
expect "$LOG" "^Paging enabled: +OK"                  "[0.7] no NX: paging enabled"
expect "$LOG" "^Kernel heap: +OK"                     "[0.7] no NX: kernel heap OK"
expect "$LOG" "System ready\."                        "[0.7] no NX: boots to System ready"
reject "$LOG" "PANIC|MISMATCH|FAILED"                 "[0.7] no NX: no failures or panic"

section "[0.7] CPU without NX: test=pfnx must not fault"
LOG=$OUT/serial-nonx-pfnx.log
boot "$LOG" 10 -cpu qemu64,-nx -m 32M -kernel "$KERNEL" -append "test=pfnx"
expect "$LOG" "^NX test: NOT APPLICABLE: NX not supported by this CPU; the data page executed without a fault, so no-execute protection could not be tested$" \
                                                      "[0.7] no NX: pfnx reports NOT APPLICABLE (not a pass of the NX test)"
expect "$LOG" "^NX test: NOT APPLICABLE \(NX not supported by this CPU\)$" \
                                                      "[0.7] no NX: pfnx screen message says NOT APPLICABLE"
expect "$LOG" "System ready\."                        "[0.7] no NX: pfnx, system ready afterwards"
reject "$LOG" "PANIC"                                 "[0.7] no NX: pfnx, no panic"

# ---- 0.7: stack guard pages, IST1 double-fault stack ----

section "[0.7] Stack guard pages (walk-check before enabling, self-test after)"
LOG=$OUT/serial-boot.log
expect "$LOG" "^Paging check: kernel stack 0x[0-9a-f]{16}-0x[0-9a-f]{16}: expected kernel rw nx, actual kernel rw nx \(4 pages\): OK$" \
                                                      "[0.7] kernel stack: 16 KiB, rw nx"
expect "$LOG" "^Paging check: IST1 stack \(double faults\) 0x[0-9a-f]{16}-0x[0-9a-f]{16}: expected kernel rw nx, actual kernel rw nx \(2 pages\): OK$" \
                                                      "[0.7] IST1 stack: 8 KiB, rw nx"
for g in "kernel stack guard page" "IST1 stack guard page"; do
    n=$(grep -cE "^Paging check: $g 0x[0-9a-f]{16}-0x[0-9a-f]{16}: expected unmapped, actual unmapped \(1 pages\): OK$" "$LOG")
    if [ "$n" -eq 2 ]; then pass "[0.7] $g UNMAPPED (walk-check and self-test)"
    else fail "[0.7] $g UNMAPPED (walk-check and self-test): $n of 2 lines"; fi
done
expect "$LOG" "^Paging check: kernel stack guard directly below the stack .*, actual as expected: OK$" \
                                                      "[0.7] kernel stack guard directly below the stack"
expect "$LOG" "^Paging check: IST1 guard directly below the IST1 stack .*, actual as expected: OK$" \
                                                      "[0.7] IST1 guard directly below the IST1 stack"
expect "$LOG" "^Paging check: kernel data\+bss .*, actual kernel rw nx \([0-9]+ pages, 2 guard pages unmapped\): OK$" \
                                                      "[0.7] data+bss: both guard pages left as holes"
KSTACK=$(grep -oE '^Paging check: kernel stack 0x[0-9a-f]{16}' "$LOG" | grep -oE '0x[0-9a-f]{16}' | head -1)
KGUARD=$(grep -oE '^Paging check: kernel stack guard page 0x[0-9a-f]{16}' "$LOG" | grep -oE '0x[0-9a-f]{16}' | head -1)
ISTACK=$(grep -oE '^Paging check: IST1 stack \(double faults\) 0x[0-9a-f]{16}' "$LOG" | grep -oE '0x[0-9a-f]{16}' | head -1)
IGUARD=$(grep -oE '^Paging check: IST1 stack guard page 0x[0-9a-f]{16}' "$LOG" | grep -oE '0x[0-9a-f]{16}' | head -1)
echo "      kernel stack ${KSTACK:-?} (guard ${KGUARD:-?}), IST1 stack ${ISTACK:-?} (guard ${IGUARD:-?})"

# Deep normal use: touching a guard page is a #PF on a present stack
# (or a #DF when the stack is the guard), so none of these may show one.
# 0.10: "CPU exception N:" is the kernel's crash line (panic.c); a fault in
# a program, contained by the kernel, is logged as "ended by CPU exception
# N (...)" or "killed by a CPU exception: ..." and is not a kernel fault.
for l in boot kbdflood console heapexhaust exhaust-16M exhaust-128M; do
    reject "$OUT/serial-$l.log" "CPU exception [0-9]+:|Double fault|PANIC" \
                                                      "[0.7] $l: never touched a stack guard page"
done

section "[0.7] Kernel stack overflow (test=stackoverflow)"
LOG=$OUT/serial-stackoverflow.log
boot "$LOG" 8 -m 32M -kernel "$KERNEL" -append "test=stackoverflow"
G=$(grep -oE '^Paging check: kernel stack guard page 0x[0-9a-f]{16}' "$LOG" | grep -oE '0x[0-9a-f]{16}' | head -1)
expect "$LOG" "^Stack test: kernel stack 0x[0-9a-f]{16}-0x[0-9a-f]{16}, RSP 0x[0-9a-f]{16}, guard page ${G:-X} \(not mapped\); recursing until the stack overflows$" \
                                                      "[0.7] stackoverflow: started, guard page not mapped"
expect "$LOG" "CPU exception 8: Double Fault \(#DF\)" "[0.7] stackoverflow: exception 8"
expect "$LOG" "Error code: 0x00000000"                "[0.7] stackoverflow: error code 0"
expect "$LOG" "^ Double fault: kernel stack overflow \(RSP is in the stack guard page\)$" \
                                                      "[0.7] stackoverflow: reported as a kernel stack overflow"
RSPG=$(grep -oE '^ RSP=0x[0-9a-f]{16}  guard page 0x[0-9a-f]{16}-0x[0-9a-f]{16}$' "$LOG" | head -1)
read -r R GS GE <<< "$(grep -oE '0x[0-9a-f]{16}' <<< "$RSPG" | tr '\n' ' ')"
echo "      ${RSPG:-no RSP/guard line}"
if [ -n "${GE:-}" ] && [ "$GS" = "${G:-X}" ] && [ $((R)) -ge $((GS)) ] && [ $((R)) -le $((GE)) ]; then
    pass "[0.7] stackoverflow: RSP $R inside the guard page $GS-$GE"
else
    fail "[0.7] stackoverflow: RSP not inside the walk-checked guard page (${RSPG:-missing}, guard ${G:-?})"
fi
expect "$LOG" "RIP=0x[0-9a-f]{16}  CS=0x0008"         "[0.7] stackoverflow: full register dump (handler ran on IST1)"
expect "$LOG" "System halted\. Power off the machine\." "[0.7] stackoverflow: halted"
n=$(grep -c '^boot32: start' "$LOG")
if [ "$n" -eq 1 ]; then pass "[0.7] stackoverflow: no reset (booted once)"
else fail "[0.7] stackoverflow: no reset (boot32: start seen $n times)"; fi
reject "$LOG" "System ready|not detected|recursion returned" \
                                                      "[0.7] stackoverflow: overflow caught, not survived"

# ---- 0.7: no screen line reaches column 80 ----

# The crash screen writes through vga_putc, which moves to the next row
# after column 79; a line of exactly 80 characters would therefore leave a
# blank line (and a longer one wraps). The console (textbuf) has a pending
# wrap instead, but the same limit is used there: at most 79 characters.
section "[0.7] Screen width: crash screens and mem output stay within 79 columns"
for l in divzero ud gpf doublefree freereserved pfnull pfunmapped pfreadonly pfnx stackoverflow \
         heapdouble heapinvalid heapcorrupt; do
    w=$(max_width "$OUT/serial-$l.log" "KERNEL PANIC" "System halted")
    if grep -q "KERNEL PANIC" "$OUT/serial-$l.log" && [ "$w" -le 79 ]; then
        pass "[0.7] $l: crash screen lines at most 79 characters (longest $w)"
    else
        fail "[0.7] $l: crash screen line of $w characters (limit 79), or no crash screen"
    fi
done
w=$(max_width "$OUT/serial-console.log" "^madire> mem$" "^madire> ")
if grep -q "^madire> mem$" "$OUT/serial-console.log" && [ "$w" -le 79 ]; then
    pass "[0.7] console mem: every line at most 79 characters (longest $w)"
else
    fail "[0.7] console mem: a line of $w characters (limit 79)"
fi

# ---- 0.7: CPUs that cannot run Madire OS ----

# no_lm_test <label> <expected message> <qemu binary> <args...>: the stub
# prints the message and halts before any mode switch. QEMU must still be
# running at the timeout (exit 124): with -no-reboot a triple fault would
# have ended the run early.
no_lm_test() {
    local label=$1 message=$2 bin=$3 rc
    shift 3
    section "[0.7] No 64-bit CPU: $label"
    local log=$OUT/serial-nolm-${label//[^a-z0-9]/-}.log
    rm -f "$log"
    timeout 6 "$bin" -display none -no-reboot -serial file:"$log" -m 32M -kernel "$KERNEL" \
        "$@" >/dev/null 2>&1
    rc=$?
    [ -f "$log" ] || : > "$log"
    expect_in_order "$log" "[0.7] $label: stub started, then refused the CPU" \
        "^boot32: start$" "^$message$"
    reject "$log" "boot32: CPUID ok|boot64|PANIC|System ready" \
                                                      "[0.7] $label: no mode switch attempted"
    if [ "$rc" -eq 124 ]; then pass "[0.7] $label: halted, still running at the timeout (no reset)"
    else fail "[0.7] $label: QEMU exited with $rc before the timeout (reset or crash)"; fi
}
NO_LM="Madire OS needs a 64-bit \(x86-64\) CPU: long mode not supported\. Halted\."
NO_PAE="Madire OS needs a 64-bit \(x86-64\) CPU: PAE not supported\. Halted\."
no_lm_test "qemu32"         "$NO_LM"  qemu-system-x86_64 -cpu qemu32
no_lm_test "i386"           "$NO_LM"  qemu-system-i386
no_lm_test "486"            "$NO_LM"  qemu-system-x86_64 -cpu 486
no_lm_test "qemu64 no lm"   "$NO_LM"  qemu-system-x86_64 -cpu qemu64,-lm
no_lm_test "qemu64 no pae"  "$NO_PAE" qemu-system-x86_64 -cpu qemu64,-pae

# ---- 0.8: higher-half kernel, empty lower half, physical view ----

section "[0.8] Higher-half kernel and physical view (normal boot)"
LOG=$OUT/serial-boot.log
expect "$LOG" "^boot64: running at the higher half, RIP=0xffffffff80[0-9a-f]{6}$" \
                                                      "[0.8] boot stub jumped to the higher half"
expect "$LOG" "^Paging check: kernel slot \(PML4\[511\]\) .*, actual as expected: OK$" \
                                                      "[0.8] PML4[511] maps the kernel image"
expect "$LOG" "^Paging check: physical view slot \(PML4\[256\]\) .*, actual as expected: OK$" \
                                                      "[0.8] PML4[256] maps the physical view"
count_lines "$LOG" "^Paging check: lower half empty: PML4\[0\] not present .*, actual as expected: OK$" 2 \
    "[0.8] lower half empty: PML4[0] not present (walk-check and self-test)"
count_lines "$LOG" "^Paging check: low address .*: expected unmapped, actual unmapped \(1 pages\): OK$" 10 \
    "[0.8] 0, 0x100000, 0xB8000, 0xFFFFF000 and the Multiboot info address all unmapped (walk-check and self-test)"
count_lines "$LOG" "^Paging check: kernel image not in the physical view 0xffff800000100000-0xffff8000[0-9a-f]{8}: expected unmapped, actual unmapped \([0-9]+ pages\): OK$" 2 \
    "[0.8] no kernel-image page in the physical view (walk-check and self-test)"
count_lines "$LOG" "^Paging check: physical view: page 0 0xffff800000000000-.*: expected unmapped, actual unmapped \(1 pages\): OK$" 2 \
    "[0.8] page 0 not in the physical view (walk-check and self-test)"
expect "$LOG" "^Paging check: PML4 frame \(free RAM\) in the physical view .*: expected unmapped, actual unmapped .*: OK$" \
                                                      "[0.8] free RAM (a page-table frame) not in the physical view"
expect "$LOG" "^Paging check: VGA text buffer 0xffff8000000b8000-.*: expected phys-view rw nx, actual phys-view rw nx .*: OK$" \
                                                      "[0.8] VGA reached through the physical view"
expect "$LOG" "^Paging check: reservation multiboot info 0xffff8000[0-9a-f]{8}-.*: expected phys-view rw nx, actual phys-view rw nx .*: OK$" \
                                                      "[0.8] boot data (Multiboot info) reached through the physical view"
count_lines "$OUT/serial-nonx.log" "^Paging check: lower half empty: PML4\[0\] not present .*: OK$" 2 \
    "[0.8] no NX: lower half empty as well"

section "[0.8] Page fault on the kernel's old low address (test=pflow)"
LOG=$OUT/serial-pflow.log
boot "$LOG" 8 -m 32M -kernel "$KERNEL" -append "test=pflow"
expect "$LOG" "^Page-fault test: read at 0x0000000000100000 \(paging on; not mapped\)$" \
                                                      "[0.8] pflow: 0x100000 reported not mapped"
expect "$LOG" "CPU exception 14: Page Fault \(#PF\)"  "[0.8] pflow: exception 14"
expect "$LOG" "Error code: 0x00000000"                "[0.8] pflow: error code 0 (not present, read, kernel)"
expect_in_order "$LOG" "[0.8] pflow: decoded as not present, at 0x100000" \
    "^ Page fault: not present, read, kernel mode$" "^ Address: 0x0000000000100000$"
expect "$LOG" "CR2=0x0000000000100000"                "[0.8] pflow: CR2 is 0x100000"
expect "$LOG" "System halted\. Power off the machine\." "[0.8] pflow: halted"
count_lines "$LOG" "^boot32: start$" 1                "[0.8] pflow: no reset (booted once)"
reject "$LOG" "System ready|not detected"             "[0.8] pflow: the read faulted"
w=$(max_width "$LOG" "KERNEL PANIC" "System halted")
if [ "$w" -le 79 ]; then pass "[0.8] pflow: crash screen lines at most 79 characters (longest $w)"
else fail "[0.8] pflow: crash screen line of $w characters (limit 79)"; fi

# ---- 0.8: stack high-water mark, boot module on screen ----

section "[0.8] Kernel stack high-water mark"
expect "$OUT/serial-boot.log" "^Kernel stack: 0x[0-9a-f]{16}-0x[0-9a-f]{16}; painted 0x[0-9a-f]{16}-0x[0-9a-f]{16}, 256 bytes below RSP 0x[0-9a-f]{16} \(live frames untouched\)$" \
                                                      "[0.8] stack painted below RSP minus 256 bytes"
# peak_ok <log> <description>: the last peak line, 0 < peak < 16384.
peak_ok() {
    local p
    p=$(grep -oE '^Kernel stack: +[0-9]+ bytes at peak, of 16384$' "$1" | tail -1 | grep -oE '[0-9]+' | head -1)
    echo "      $(basename "$1"): peak ${p:-?} bytes"
    if [ -n "$p" ] && [ "$p" -gt 0 ] && [ "$p" -lt 16384 ]; then pass "$2 ($p bytes)"
    else fail "$2 (${p:-missing})"; fi
}
peak_ok "$OUT/serial-boot.log"     "[0.8] stack peak at System ready: 0 < peak < 16 KiB"
peak_ok "$OUT/serial-kbdflood.log" "[0.8] stack peak after the keyboard flood: 0 < peak < 16 KiB"
peak_ok "$OUT/serial-console.log"  "[0.8] stack peak after the console session (mem row): 0 < peak < 16 KiB"
expect_in_order "$OUT/serial-console.log" "[0.8] mem: the stack row comes last" \
    "^madire> mem$" "^Heap free: +[0-9]+ KiB, largest block [0-9]+ KiB$" "^Kernel stack: +[0-9]+ bytes at peak, of 16384$"

section "[0.8] Boot module result on the screen"
expect "$OUT/serial-mem-module.log" "^Boot module 1: test pattern intact \(14016 bytes\)$" \
                                                      "[0.8] module (QEMU): result on the message line"
expect "$OUT/serial-bootmenu-module.log" "^Boot module 1: test pattern intact \(14016 bytes\)$" \
                                                      "[0.8] module (boot menu): result on the message line"
expect "$OUT/serial-mem-module-corrupt.log" "^Boot module 1: test pattern CORRUPTED \(14016 bytes\)$" \
                                                      "[0.8] corrupted module: shown as CORRUPTED"
reject "$OUT/serial-boot.log" "^Boot module [0-9]"     "[0.8] normal boot: no module message (screen unchanged)"

# ---- 0.9: kernel threads ----

section "[0.9] Scheduler self-test and the thread stack area (normal boot)"
LOG=$OUT/serial-boot.log
expect "$LOG" "^Scheduler self-test: PASS \([0-9]+ checks, 0 mismatches\)$" \
                                                      "[0.9] scheduler self-test: PASS"
expect "$LOG" "^Scheduler self-test: cooperative ping-pong: 100 yields, strictly alternating: OK$" \
                                                      "[0.9] self-test: two threads alternate strictly through yield"
expect "$LOG" "^Scheduler self-test: everything returned: .*: OK$" \
                                                      "[0.9] self-test: frames, stack slots and threads all returned"
expect "$LOG" "^Scheduler self-test: preemptions during the ping-pong: [0-9]+: OK$" \
                                                      "[0.9] self-test: preemptions during the ping-pong logged"
expect "$LOG" "^Scheduler self-test: preemption: a thread that never yields was preempted [0-9]+ times in 30 ticks: OK$" \
                                                      "[0.9] self-test: a thread that never yields is preempted (at least 3 times in 30 ticks)"
expect "$LOG" "^Scheduler self-test: sleep: sleep\(10\) woke after 1[01] ticks \(10 or 11 expected\): OK$" \
                                                      "[0.9] self-test: sleep(10) wakes after 10 or 11 ticks"
expect "$LOG" "^Paging check: kernel thread stack area \(PML4\[507\]\), empty at boot 0xfffffd8000000000-0xfffffd8000200000: expected unmapped, actual unmapped \(512 pages\): OK$" \
                                                      "[0.9] walk-check: thread stack area (PML4[507]) empty at boot"
expect "$LOG" "^Paging check: kernel thread stack page table present .*: OK$" \
                                                      "[0.9] walk-check: thread stack page table made at boot"
reject "$LOG" "Screen output from thread|Boot screen used from thread" \
                                                      "[0.9] normal boot: no screen-rule violation"
expect "$OUT/serial-nonx.log" "^Scheduler self-test: PASS " "[0.9] no NX: scheduler self-test PASS"
expect "$LOG" "^Scheduler self-test: longest turn: [0-9]+ ticks \(slice 5\); highest thread stack peak: [0-9]+ of 16384 bytes$" \
                                                      "[0.9] self-test: longest turn and highest stack peak logged"


section "[0.11] The longest turn's owner (the 0.10 watch item)"
expect "$LOG" "^Scheduler self-test: longest turn taken by thread [0-9]+ '[^']+' \(kernel thread\)" \
                                                      "[0.11] self-test: its longest turn was a kernel thread's (no process exists yet)"
LT=$(grep -m1 -E "^Scheduler self-test: longest turn: [0-9]+ ticks" "$LOG" | sed -nE 's/.*longest turn: ([0-9]+) ticks.*/\1/p')
owner_check "self-test" "Scheduler self-test" "$LT"

# diag_clean <label>: no panic, no mismatch, System ready afterwards, and the
# longest-turn metric (0.9 known limitation of the deferred switch) logged.
diag_clean() {
    reject "$LOG" "PANIC|MISMATCH"                    "[0.9] $1: no panic, no mismatch"
    expect "$LOG" "System ready\."                     "[0.9] $1: System ready afterwards"
    expect "$LOG" ": longest turn: [0-9]+ ticks \(slice 5\); highest thread stack peak: [0-9]+ of 16384 bytes$" \
                                                      "[0.9] $1: longest turn and highest stack peak logged"
    grep -E ": longest turn: " "$LOG" | tail -1 | sed 's/^/      /'
    local lt
    lt=$(grep -E ": longest turn: [0-9]+ ticks" "$LOG" | tail -1 | sed -nE 's/.*: longest turn: ([0-9]+) ticks.*/\1/p')
    owner_check "$1" ".*" "$lt"
}

section "[0.9] Cooperative context switches (test=threads)"
thread_diag threads 15
expect "$LOG" "^Thread test: create: 8 threads: OK$"  "[0.9] threads: 8 threads created"
expect "$LOG" "^Thread test: order: 800 turns, strict round robin \(w0\.\.w7, 100 rounds\): OK$" \
                                                      "[0.9] threads: 800 turns in strict round-robin order"
expect "$LOG" "^Thread test: callee-saved registers preserved: 800 switches checked, 0 with a changed register: OK$" \
                                                      "[0.9] threads: rbx, rbp, r12-r15 preserved across every switch"
expect "$LOG" "^Thread test: stack contents preserved: 0 mismatches: OK$" \
                                                      "[0.9] threads: each thread's stack contents intact"
expect "$LOG" "^Thread test: interrupt flag 1 after every switch: 0 checks with IF=0: OK$" \
                                                      "[0.9] threads: IF=1 after every switch"
expect "$LOG" "^Thread test: everything returned: .*: OK$" \
                                                      "[0.9] threads: frames, stack slots and threads all returned"
expect "$LOG" "^Thread test: preemptions during the cooperative phase: [0-9]+: OK$" \
                                                      "[0.9] threads: preemptions during the cooperative phase logged"
expect "$LOG" "^Thread test: PASS \("                 "[0.9] threads: PASS"
diag_clean threads

section "[0.9] Turn order: a forced preemption takes the relaxed branch (test=pingpongpreempt)"
thread_diag pingpongpreempt 12
grep -E "^Ping-pong preemption test: (cooperative ping-pong|preemptions during|forced)" "$LOG" | sed 's/^/      /'
expect "$LOG" "^Ping-pong preemption test: cooperative ping-pong: 100 yields, order differs at [1-9][0-9]* point\(s\), each right after a preemption: OK$" \
                                                      "[0.9] pingpongpreempt: the order differs only right after the preemption"
expect "$LOG" "^Ping-pong preemption test: forced preemption accepted by the relaxed branch: .*: OK$" \
                                                      "[0.9] pingpongpreempt: accepted through the relaxed branch"
expect "$LOG" "^Ping-pong preemption test: join: exit codes 11 and 22: OK$" \
                                                      "[0.9] pingpongpreempt: both threads completed (codes 11, 22)"
expect "$LOG" "^Ping-pong preemption test: everything returned: .*: OK$" \
                                                      "[0.9] pingpongpreempt: frames, stack slots and threads all returned"
expect "$LOG" "^Ping-pong preemption test: PASS \("   "[0.9] pingpongpreempt: PASS"
diag_clean pingpongpreempt

section "[0.9] Turn order: a wrong order without preemption still fails (test=pingpongbad)"
thread_diag pingpongbad 12
grep -E "^Ping-pong order test: a " "$LOG" | sed 's/^/      /'
expect "$LOG" "^Ping-pong order test: a skipped turn without preemption: detected: out of order without a preemption \(100 turns, 0 preemptions\): OK$" \
                                                      "[0.9] pingpongbad: a skipped turn is rejected (out of order)"
expect "$LOG" "^Ping-pong order test: a repeated turn without preemption: detected: wrong number of turns \(101 turns, 0 preemptions\): OK$" \
                                                      "[0.9] pingpongbad: a repeated turn is rejected (wrong count)"
expect "$LOG" "^Ping-pong order test: everything returned: .*: OK$" \
                                                      "[0.9] pingpongbad: frames, stack slots and threads all returned"
expect "$LOG" "^Ping-pong order test: PASS \("        "[0.9] pingpongbad: PASS"
diag_clean pingpongbad

section "[0.9] Join, exit codes, detached threads (test=join)"
thread_diag join 15
expect "$LOG" "^Join test: exit codes: 6 of 6 exact .*: OK$" \
                                                      "[0.9] join: exit codes 0, 1, 255, 0x7fffffff, -1, INT32_MIN exact"
expect "$LOG" "^Join test: join by a thread other than 0 \(blocked, then woken\): .*: code 78: OK$" \
                                                      "[0.9] join: a thread other than 0 blocks in join and is woken"
expect "$LOG" "^Join test: detached thread: .*: OK$"  "[0.9] join: detached thread reaped by thread 0"
expect "$LOG" "^Join test: everything returned: .*: OK$" \
                                                      "[0.9] join: frames, stack slots and threads all returned"
expect "$LOG" "^Join test: PASS \("                   "[0.9] join: PASS"
diag_clean join

section "[0.9] Interrupt flag across switches (test=ifstate)"
thread_diag ifstate 15
expect "$LOG" "^IF-state test: new thread starts with IF=1 \(trampoline\): IF=1 at the first instruction of a new thread: OK$" \
                                                      "[0.9] ifstate: a new thread starts with IF=1"
expect "$LOG" "^IF-state test: yield with IF=1 resumes with IF=1: 10 yields, 0 resumed with IF=0: OK$" \
                                                      "[0.9] ifstate: yield with IF=1 resumes with IF=1"
expect "$LOG" "^IF-state test: yield with IF=0 resumes with IF=0: resumed with IF=0; meanwhile another thread ran with IF=1 and received a timer tick: OK$" \
                                                      "[0.9] ifstate: yield with IF=0 resumes with IF=0 (own flags)"
expect "$LOG" "^IF-state test: thread 0 keeps IF=1: IF=1 after joining: OK$" \
                                                      "[0.9] ifstate: thread 0 keeps IF=1"
expect "$LOG" "^IF-state test: preempted thread resumes through iretq with its own flags: AC=1 IF=1 after [1-9][0-9]* preemptions; other thread AC=0, thread 0 AC=0: OK$" \
                                                      "[0.9] ifstate: a preempted thread resumes through iretq with its own flags (no leak)"
expect "$LOG" "^IF-state test: PASS \("               "[0.9] ifstate: PASS"
diag_clean ifstate

# preempt_panic <name> <function>: blocking with preemption disabled is a
# kernel bug: crash screen naming the call, the count and the thread.
preempt_panic() {
    section "[0.9] $2 with preemption disabled (test=$1)"
    thread_diag "$1" 8
    expect "$LOG" "$2: called with preemption disabled \(count 1, thread 0 'kernel'\)\." \
                                                      "[0.9] $1: panics naming the call, the count and the thread"
    expect "$LOG" "System halted\. Power off the machine\." "[0.9] $1: halted"
    count_lines "$LOG" "^boot32: start$" 1            "[0.9] $1: no reset (booted once)"
    reject "$LOG" "returned \(WRONG\)|System ready"   "[0.9] $1: the call never returned"
    w=$(max_width "$LOG" "KERNEL PANIC" "System halted")
    if [ "$w" -le 79 ]; then pass "[0.9] $1: crash screen lines at most 79 characters (longest $w)"
    else fail "[0.9] $1: crash screen line of $w characters (limit 79)"; fi
}
preempt_panic preemptyield thread_yield
preempt_panic preemptjoin thread_join
preempt_panic preemptsleep thread_sleep

section "[0.9] Preemption: three threads that never yield (test=preempt, keys typed meanwhile)"
LOG=$OUT/serial-preempt.log
boot_keys "$LOG" 14 "Preempt test: 3 threads spinning" "a,b,c,d,e,f,g,h" 0.15 \
    -m 32M -kernel "$KERNEL" -append "test=preempt keylog"
grep -E "^Preempt test: (fair shares|preemptions)" "$LOG" | sed 's/^/      /'
expect "$LOG" "^Preempt test: fair shares: .* \(each 25-42%\): OK$" \
                                                      "[0.9] preempt: each thread got 25-42% of the CPU"
expect "$LOG" "^Preempt test: preemptions: [0-9]+ preemptions in 300 ticks .*: OK$" \
                                                      "[0.9] preempt: one preemption per 5-tick slice (40-80 in 300 ticks)"
expect "$LOG" "^Preempt test: every thread made progress: .*: OK$" \
                                                      "[0.9] preempt: every thread made progress"
expect "$LOG" "^Preempt test: timer kept ticking: .*: OK$" "[0.9] preempt: the timer kept ticking"
expect "$LOG" "^Preempt test: everything returned: .*: OK$" \
                                                      "[0.9] preempt: frames, stack slots and threads all returned"
expect "$LOG" "^Preempt test: PASS \("                "[0.9] preempt: PASS"
expect_in_order "$LOG" "[0.9] preempt: keys typed during the test all arrive, in order (IRQ1 not starved)" \
    "^key: press .*ascii='a'" "^key: press .*ascii='b'" "^key: press .*ascii='c'" "^key: press .*ascii='d'" \
    "^key: press .*ascii='e'" "^key: press .*ascii='f'" "^key: press .*ascii='g'" "^key: press .*ascii='h'"
reject "$LOG" "events dropped"                       "[0.9] preempt: no key events dropped"
diag_clean preempt

section "[0.9] Deferred switch while preemption is disabled (test=preemptoff)"
thread_diag preemptoff 10
grep -E "^Preemptoff test: switch within" "$LOG" | sed 's/^/      /'
expect "$LOG" "^Preemptoff test: no switch while the guard is held: preemption off for [0-9]+ ticks: no switch, the other thread did not run: OK$" \
                                                      "[0.9] preemptoff: no switch while the guard is held (20 ticks)"
expect "$LOG" "^Preemptoff test: switch request kept pending \(need_resched\) while held: yes: OK$" \
                                                      "[0.9] preemptoff: the switch request stays pending"
expect "$LOG" "^Preemptoff test: switch within one tick after the release: .*: OK$" \
                                                      "[0.9] preemptoff: the other thread runs at most one tick after the release"
expect "$LOG" "^Preemptoff test: everything returned: .*: OK$" \
                                                      "[0.9] preemptoff: frames, stack slots and threads all returned"
expect "$LOG" "^Preemptoff test: PASS \("             "[0.9] preemptoff: PASS"
diag_clean preemptoff

section "[0.9] Sleep next to a spinning thread (test=sleep)"
thread_diag sleep 10
for n in 1 10 100; do
    expect "$LOG" "^Sleep test: sleep\($n\): woke after ($n|$((n + 1))) ticks .*: OK$" \
                                                      "[0.9] sleep: sleep($n) wakes after $n or $((n + 1)) ticks"
done
expect "$LOG" "^Sleep test: a sleeping thread uses no CPU: .*: OK$" \
                                                      "[0.9] sleep: the spinner gets at least 95% while the other sleeps"
expect "$LOG" "^Sleep test: thread 0 sleeps by polling: .*: OK$" \
                                                      "[0.9] sleep: thread 0 sleeps 10 ticks by polling (10 or 11)"
expect "$LOG" "^Sleep test: everything returned: .*: OK$" \
                                                      "[0.9] sleep: frames, stack slots and threads all returned"
expect "$LOG" "^Sleep test: PASS \("                  "[0.9] sleep: PASS"
diag_clean sleep

section "[0.9] The heap under preemption (test=heapthreads)"
thread_diag heapthreads 12
grep -E "^Heap threads test: concurrent" "$LOG" | sed 's/^/      /'
expect "$LOG" "^Heap threads test: concurrent use under preemption: .*: OK$" \
                                                      "[0.9] heapthreads: 4 threads, thousands of kmalloc/kfree, preempted meanwhile"
expect "$LOG" "^Heap threads test: every payload intact: 0 damaged blocks: OK$" \
                                                      "[0.9] heapthreads: every tagged payload intact"
expect "$LOG" "^Heap threads test: heap consistent and empty again: heap_check OK; .*: OK$" \
                                                      "[0.9] heapthreads: heap_check OK, nothing left in use"
expect "$LOG" "^Heap threads test: frames accounted for exactly .*: OK$" \
                                                      "[0.9] heapthreads: free pages = before - heap growth - new tables, exactly"
expect "$LOG" "^Heap threads test: PASS \("           "[0.9] heapthreads: PASS"
diag_clean heapthreads

section "[0.9] The frame allocator and the window under preemption (test=framethreads)"
thread_diag framethreads 12
grep -E "^Frame threads test: concurrent" "$LOG" | sed 's/^/      /'
expect "$LOG" "^Frame threads test: concurrent use under preemption: .*: OK$" \
                                                      "[0.9] framethreads: 4 threads, thousands of allocations, preempted meanwhile"
expect "$LOG" "^Frame threads test: every frame tag intact: 0 frames lost their tag .*: OK$" \
                                                      "[0.9] framethreads: no frame handed out twice (every tag intact)"
expect "$LOG" "^Frame threads test: everything returned: free pages ([0-9]+) -> \1, .*: OK$" \
                                                      "[0.9] framethreads: free pages restored exactly"
expect "$LOG" "^Frame threads test: PASS \("          "[0.9] framethreads: PASS"
diag_clean framethreads

section "[0.9] 2000 threads created and freed; per-thread stack peaks (test=threadleak)"
thread_diag threadleak 40
grep -E "^Thread churn test: (stack peak|2000 threads|exit codes)" "$LOG" | sed 's/^/      /'
expect "$LOG" "^Thread churn test: stack peak reported per thread: .*: OK$" \
                                                      "[0.9] threadleak: a thread over 8 KiB deep reports a peak of 8192..16383 bytes"
expect "$LOG" "^Thread 1 'deep' exited: code [0-9]+, stack peak (8[2-9][0-9]{2}|9[0-9]{3}|1[0-5][0-9]{3}|16[0-2][0-9]{2}|163[0-7][0-9]|1638[0-3]) of 16384$" \
                                                      "[0.9] threadleak: its exit line shows that peak"
expect "$LOG" "^Thread churn test: 2000 threads created and freed: 2000 created \([0-9]+ joined, [0-9]+ detached reaped\), 0 create failures: OK$" \
                                                      "[0.9] threadleak: 2000 threads created, run and freed (joined and detached)"
expect "$LOG" "^Thread churn test: exit codes and limits: 0 wrong exit codes; .*: OK$" \
                                                      "[0.9] threadleak: every exit code right, never over the limits"
expect "$LOG" "^Thread churn test: heap unchanged: .*: OK$" "[0.9] threadleak: heap use unchanged"
expect "$LOG" "^Thread churn test: everything returned: free pages ([0-9]+) -> \1, stack slots in use 0, live threads 1: OK$" \
                                                      "[0.9] threadleak: free pages, stack slots and threads exactly as before"
count_lines "$LOG" "^Thread [0-9]+ 'churn' exited" 0  "[0.9] threadleak: no exit line per churn thread (logging off)"
expect "$LOG" "^Thread churn test: PASS \("           "[0.9] threadleak: PASS"
diag_clean threadleak

section "[0.9] Every thread's exit line has a stack peak below the stack size"
peaks_ok=0; peaks_bad=0
for l in boot threads join ifstate preempt preemptoff sleep heapthreads framethreads threadleak \
         pingpongpreempt pingpongbad; do
    while read -r p; do
        if [ "$p" -gt 0 ] && [ "$p" -lt 16384 ]; then peaks_ok=$((peaks_ok + 1)); else peaks_bad=$((peaks_bad + 1)); fi
    done < <(grep -oE "^Thread [0-9]+ '[^']*' exited: code -?[0-9]+, stack peak [0-9]+ of 16384$" "$OUT/serial-$l.log" |
             grep -oE 'stack peak [0-9]+' | grep -oE '[0-9]+')
done
echo "      $peaks_ok exit lines in range, $peaks_bad out of range"
if [ "$peaks_ok" -ge 30 ] && [ "$peaks_bad" -eq 0 ]; then
    pass "[0.9] every exit line: 0 < stack peak < 16384 ($peaks_ok threads)"
else
    fail "[0.9] exit-line stack peaks: $peaks_ok in range, $peaks_bad out of range"
fi

section "[0.9] Kernel thread stack overflow (test=threadoverflow)"
thread_diag threadoverflow 10
expect "$LOG" "CPU exception 8: Double Fault \(#DF\)" "[0.9] threadoverflow: exception 8 (#DF)"
expect "$LOG" "Double fault: kernel thread stack overflow \(thread [0-9]+ 'overflow'\)" \
                                                      "[0.9] threadoverflow: diagnosed by name"
# The RSP printed with the diagnosis must lie inside the guard range it names.
GL=$(grep -oE "RSP=0x[0-9a-f]{16}  guard 0x[0-9a-f]{16}-0x[0-9a-f]{16}" "$LOG" | head -1)
GR=$(grep -oE '0x[0-9a-f]{16}' <<< "$GL" | sed -n 1p); GS=$(grep -oE '0x[0-9a-f]{16}' <<< "$GL" | sed -n 2p)
GE=$(grep -oE '0x[0-9a-f]{16}' <<< "$GL" | sed -n 3p)
if [ -n "$GR" ] && [[ "$GS" == 0xfffffd80* ]] && [[ ! "$GR" < "$GS" ]] && [[ ! "$GR" > "$GE" ]]; then
    pass "[0.9] threadoverflow: RSP $GR inside the thread's guard $GS-$GE (slot 507)"
else
    fail "[0.9] threadoverflow: RSP ${GR:-?} not inside guard ${GS:-?}-${GE:-?}"
fi
expect "$LOG" "System halted\. Power off the machine\." "[0.9] threadoverflow: halted"
count_lines "$LOG" "^boot32: start$" 1                "[0.9] threadoverflow: no reset (booted once)"
reject "$LOG" "the thread returned \(WRONG\)|System ready" "[0.9] threadoverflow: the thread never returned"
w=$(max_width "$LOG" "KERNEL PANIC" "System halted")
if [ "$w" -le 79 ]; then pass "[0.9] threadoverflow: crash screen lines at most 79 characters (longest $w)"
else fail "[0.9] threadoverflow: crash screen line of $w characters (limit 79)"; fi

section "[0.9] Console: ps"
LOG=$OUT/serial-ps.log
boot_text "$LOG" 60 "System ready." "x{ret}ps{ret}help{ret}echo end-of-ps-test{ret}" 0.05 \
    $'\nend-of-ps-test\n' -kernel "$KERNEL"
expect_in_order "$LOG" "[0.9] ps: header, thread 0 'kernel' running, count" \
    "^madire> ps$" "^TID  STATE      TICKS    STACK PEAK    NAME$" \
    "^  0  running +[0-9]+ +[0-9]+/16384   kernel$" "^1 thread$"
expect_in_order "$LOG" "[0.9] help: ps listed after the seven commands" \
    "^madire> help$" "^  help  clear  echo  " "^  version  uptime  mem  cpu  " "^  ps  run  kill$"
w=$(max_width "$LOG" "^madire> ps$" "^madire> help$")
if grep -q "^madire> ps$" "$LOG" && [ "$w" -le 79 ]; then pass "[0.9] ps: every line at most 79 characters (longest $w)"
else fail "[0.9] ps: a line of $w characters (limit 79)"; fi
reject "$LOG" "PANIC|Screen output from thread"      "[0.9] ps: no panic, no screen-rule violation"

# The boot screen rule (0.9): the checker passes on the real sources and
# rejects a violation, both from another file and from non-boot code in
# kernel.c (scratch trees under build/).
section "[0.9] Boot screen used only by the boot sequence and BOOTSCREEN_SAFE()"
CHECK=$(dirname "$0")/../scripts/check-bootscreen.sh
if sh "$CHECK" >/dev/null; then pass "[0.9] boot screen check passes on the sources"
else fail "[0.9] boot screen check fails on the sources"; fi
bs_tree() {   # bs_tree <dir>: a scratch tree with the checker and an allowed kernel.c
    rm -rf "$1" && mkdir -p "$1/scripts" "$1/kernel" "$1/drivers"
    cp "$CHECK" "$1/scripts/"
    printf 'void kernel_main(void)\n{\n    bootscreen_ready("x");\n}\n' > "$1/kernel/kernel.c"
}
T=$OUT/bscheck-a; bs_tree "$T"
printf 'void f(void)\n{\n    bootscreen_message("x");\n}\n' > "$T/kernel/other.c"
if sh "$T/scripts/check-bootscreen.sh" >/dev/null; then fail "[0.9] boot screen check missed a call from another file"
else pass "[0.9] boot screen check rejects a call from another file"; fi
T=$OUT/bscheck-b; bs_tree "$T"
printf 'static void idle_loop(void)\n{\n    bootscreen_uptime(1);\n}\n' >> "$T/kernel/kernel.c"
if sh "$T/scripts/check-bootscreen.sh" >/dev/null; then fail "[0.9] boot screen check missed an unwrapped call in kernel.c"
else pass "[0.9] boot screen check rejects an unwrapped call outside the boot sequence"; fi

# ---- 0.9.2: key logging off by default, console scrollback ----

section "[0.9.2] Key events are logged only with 'keylog' or a keyboard diagnostic"
NOTLOGGED="^Key events: not logged \(boot option 'keylog' logs them; for tests only\)$"
LOG=$OUT/serial-boot.log
expect "$LOG" "$NOTLOGGED"                            "[0.9.2] normal boot (real ISO): 'Key events: not logged'"
reject "$LOG" "^key: "                                "[0.9.2] normal boot: a key was typed, but no key event is logged"
expect_in_order "$LOG" "[0.9.2] normal boot: the typed key still opened the console" \
    "System ready\. Press any key" "^console: started$"
expect "$KLOG" "^Key events: logged \(boot option 'keylog'\)$" \
                                                      "[0.9.2] keylog test ISO: 'Key events: logged (boot option 'keylog')'"
# 0.13: the boot menu is limine.conf; the keylog variant adds exactly one line,
# "cmdline: keylog", right after the default entry's "path:" line.
KDIFF=$(diff "$OUT/limine.conf" "$OUT/isokeylog/boot/limine/limine.conf" | grep -c '^[<>]')
KADD=$(diff "$OUT/limine.conf" "$OUT/isokeylog/boot/limine/limine.conf" | grep '^[<>]')
KAT=$(grep -n -m1 '^ *path: boot():/boot/madire\.kernel$' "$OUT/limine.conf" | cut -d: -f1)
KNEXT=$(sed -n "$((KAT + 1))p" "$OUT/isokeylog/boot/limine/limine.conf")
if [ "$KDIFF" -eq 1 ] && [ "$KADD" = ">     cmdline: keylog" ] && [ "$KNEXT" = "    cmdline: keylog" ]; then
    pass "[0.9.2] keylog test ISO: its boot menu differs from the real one only by 'keylog' on one line"
else
    fail "[0.9.2] keylog test ISO: limine.conf differs in $KDIFF diff lines ('$KADD')"
fi
LOG=$OUT/serial-nokeylog.log
boot_keys "$LOG" 15 "System ready." "x,a" 0.15 -m 32M -kernel "$KERNEL" -append "nokeylog"
expect "$LOG" "$NOTLOGGED"                            "[0.9.2] 'nokeylog' does not turn key logging on (whole-word match)"
reject "$LOG" "^key: "                                "[0.9.2] 'nokeylog': keys typed, no key event logged"
expect "$OUT/serial-keyboard.log" "^Key events: logged \(keyboard diagnostic\)$" \
                                                      "[0.9.2] test=keyboard turns key logging on"
expect "$OUT/serial-kbdflood.log" "^Key events: logged \(keyboard diagnostic\)$" \
                                                      "[0.9.2] test=kbdflood turns key logging on"
LOG=$OUT/serial-nokeys-dmesg.log
boot_text "$LOG" 60 "System ready." "x{wait:1}echo hello{ret}dmesg 30{ret}echo end-of-keylog-test{ret}" 0.05 \
    $'\nend-of-keylog-test\n' -kernel "$KERNEL"
DM=$(awk '/^madire> dmesg 30$/ && !n { n = 1; next } n == 1 && /^madire> echo end-of-keylog-test$/ { n = 2 } n == 1' "$LOG")
if grep -q '^madire> echo hello$' <<< "$DM" && ! grep -q '^key: ' <<< "$DM"; then
    pass "[0.9.2] dmesg on a normal boot: the commands are there, no key event"
else
    fail "[0.9.2] dmesg on a normal boot: key events present or the log is missing"
fi
reject "$LOG" "^key: "                                "[0.9.2] normal boot with typed commands: no key event in the serial log"

section "[0.12] Console: set, unset, export, env, sleep, presskey, time and kill -y"
# 0.12 stage 2, in a boot of its own (here because shard 1 is the shortest).
# The boot self-test uses process IDs 1-3, so the programs started here are
# 4 (spin), 5 (spin) and 6 (hello). Keys typed while "sleep" waits are
# ignored, so every sleep is followed by a pause longer than it. About 900
# keys: the {wait:4} pauses let QEMU's key queue drain (see stage 1).
LOG=$OUT/serial-shell0122.log
V63=$(printf 'v%.0s' $(seq 1 63))
S="x{wait:1}"
S+="run -b spin{ret}{wait:1}kill -y 4{ret}{wait:1}"
S+="kill -y 0{ret}kill -y 99{ret}kill -y 4{ret}kill -y x{ret}kill -y{ret}kill{ret}kill --help{ret}"
S+="run -b spin{ret}{wait:1}time kill -y 5{ret}{wait:1}"
S+="{wait:4}time echo hi{ret}time run hello{ret}{wait:2}"
S+="time time echo x{ret}time nosuch{ret}alias ll echo hi{ret}time ll{ret}time{ret}time --help{ret}"
S+="{wait:4}sleep 1{ret}{wait:4}sleep 0{ret}sleep 3601{ret}sleep x{ret}sleep{ret}sleep --help{ret}"
S+="sleep 10{ret}{wait:1}echo lost{ret}{wait:1}{ctrl+c}{wait:1}"
S+="time sleep 1{ret}{wait:4}"
S+="presskey{ret}{wait:1}{shift+ctrl}{wait:1}q{wait:1}echo after-presskey{ret}"
S+="presskey {ret}{wait:1}{ctrl+c}{wait:1}"
S+="time presskey{ret}{wait:1}z{wait:1}presskey x{ret}help presskey{ret}"
S+="{wait:4}set{ret}env{ret}set A=1{ret}set A{ret}set B=b=c{ret}set E={ret}set W=x   y  z{ret}set W{ret}"
S+="set a=small{ret}set _u=under{ret}set {ret}"
S+="{wait:4}set nosuch{ret}set A b{ret}set 1abc=v{ret}set =v{ret}set abcdefghijklmnopq=v{ret}"
S+="set abcdefghijklmnop=16{ret}help set{ret}"
S+="{wait:4}set V=${V63}{ret}set V=${V63}v{ret}set V{ret}"
# The env lines differ in trailing spaces (ignored by the shell), so each
# logged line is unique.
S+="{wait:4}export A{ret}export a{ret}env {ret}set A=2{ret}env  {ret}unset a{ret}env   {ret}set a=back{ret}env    {ret}"
S+="{wait:4}unset nosuch{ret}unset{ret}export nosuch{ret}export B=2{ret}export{ret}env x{ret}"
for i in $(seq -w 0 23); do
    case $i in 08|16) S+="{wait:4}" ;; esac
    S+="set V$i=$((10#$i)){ret}"
done
S+="{wait:4}set V24=24{ret}set A=3{ret}unset V00{ret}set V24=24{ret}set  {ret}"
S+="echo end-of-0122-test{ret}"
boot_text "$LOG" 240 "System ready." "$S" 0.03 $'\nend-of-0122-test\n' -m 32M -kernel "$KERNEL" \
    -initrd "$OUT/user/spin.elf,$OUT/user/hello.elf"
grep -E "^madire> (run|kill|time|sleep|presskey)|^\[process |^time: |^console: (kill -y|sleep|presskey|Ctrl\+C)|^(sleep|presskey): " \
    "$LOG" | sed 's/^/      /'

# The kernel logs "uptime: N s" every 5 s between any two lines; it may
# follow a command's output (before the screen is full no "scrolled" line
# comes first), and it may fall inside a wait, so the sequences across a
# wait are checked in order.
NEXT="^(console: |madire> |uptime: [0-9]+ s$)"
TL="^time: [0-9]+ ticks? \([0-9]+\.[0-9]{2} s\)$"
# kill -y
expect "$LOG" "^\[process 4 'spin' started in the background\]$" \
                                                      "[0.12] kill -y session: spin started in the background as process 4"
expect_next "$LOG" "[0.12] kill -y 4: no question, kill -y logged at once" \
    "^madire> kill -y 4$" "^console: kill -y for process 4$"
expect_in_order "$LOG" "[0.12] kill -y 4: its end line, then the next prompt" \
    "^madire> kill -y 4$" "^console: kill -y for process 4$" "^\[process 4 'spin' killed\]$" "^madire> kill -y 0$"
reject "$LOG" "^End process|^yes/no> "                "[0.12] kill -y never asks (no question, no yes/no prompt)"
expect_next "$LOG" "[0.12] kill -y 0: refused (the kernel, not a process)" \
    "^madire> kill -y 0$" "^kill: PID 0 is the kernel, not a process\.$"
expect_next "$LOG" "[0.12] kill -y 99: refused (no such process)" \
    "^madire> kill -y 99$" "^kill: no process with PID 99\.$"
expect_in_order "$LOG" "[0.12] kill -y 4 after it ended: no process with PID 4" \
    "^madire> kill -y 99$" "^madire> kill -y 4$" "^kill: no process with PID 4\.$"
expect_next "$LOG" "[0.12] kill -y x: refused (not a process ID)" \
    "^madire> kill -y x$" "^kill: 'x' is not a process ID\.$"
expect_next "$LOG" "[0.12] kill -y without a PID: usage" "^madire> kill -y$" "^Usage: kill \[-y\] <pid>$"
expect_next "$LOG" "[0.12] kill alone: the usage names -y" "^madire> kill$" "^Usage: kill \[-y\] <pid>$"
expect_lines "$LOG" "[0.12] kill --help: the row's text, then the usage with -y" \
    "^madire> kill --help$" "^kill: End a process: kill <pid> \(asks first\)$" "^Usage: kill \[-y\] <pid>$" "$NEXT"
expect_in_order "$LOG" "[0.12] time kill -y 5: killed, then the time line, then the prompt" \
    "^madire> time kill -y 5$" "^console: kill -y for process 5$" "^\[process 5 'spin' killed\]$" "$TL" \
    "^madire> time echo hi$"
# time
expect_in_order "$LOG" "[0.12] time echo hi: its output, then 0 or 1 tick" \
    "^madire> time echo hi$" "^hi$" "^time: [01] ticks? \(0\.0[01] s\)$" "^madire> time run hello$"
expect_in_order "$LOG" "[0.12] time run hello: waits for the end line, then the time line" \
    "^madire> time run hello$" "^\[process 6 'hello' exited, status 0\]$" "$TL" "^madire> time time echo x$"
expect_next "$LOG" "[0.12] time time: refused" "^madire> time time echo x$" "^time: cannot time time$"
expect_next "$LOG" "[0.12] time nosuch: not a built-in command" \
    "^madire> time nosuch$" "^time: 'nosuch' is not a built-in command\.$"
expect_next "$LOG" "[0.12] time ll: an alias is not expanded" \
    "^madire> time ll$" "^time: 'll' is an alias; time runs built-in commands only\.$"
expect_next "$LOG" "[0.12] time alone: usage" "^madire> time$" "^Usage: time <command\.\.\.>$"
expect_lines "$LOG" "[0.12] time --help: time's own help" \
    "^madire> time --help$" "^time: Time a command: time <command>$" "^Usage: time <command\.\.\.>$" "$NEXT"
count_lines "$LOG" "^time: [0-9]" 5              "[0.12] exactly 5 time lines (one per timed command, none for the refusals)"
# sleep
expect_in_order "$LOG" "[0.12] sleep 1: the wait logged, then done, then the next prompt" \
    "^madire> sleep 1$" "^console: sleep 1 s$" "^console: sleep done after [0-9]+ ticks$" "^madire> sleep 0$"
t=$(sed -nE '0,/^console: sleep done after/s/^console: sleep done after ([0-9]+) ticks$/\1/p' "$LOG")
if [ -n "$t" ] && [ "$t" -ge 100 ] && [ "$t" -le 101 ]; then pass "[0.12] sleep 1: woke after $t ticks (100 or 101)"
else fail "[0.12] sleep 1: woke after ${t:-?} ticks (expected 100 or 101)"; fi
for a in 0 3601 x; do
    expect_next "$LOG" "[0.12] sleep $a: refused" "^madire> sleep $a$" "^sleep: give whole seconds from 1 to 3600$"
done
expect_next "$LOG" "[0.12] sleep alone: usage" "^madire> sleep$" "^Usage: sleep <seconds>$"
expect_lines "$LOG" "[0.12] sleep --help" \
    "^madire> sleep --help$" "^sleep: Wait: sleep <seconds> \(1-3600, Ctrl\+C stops\)$" "^Usage: sleep <seconds>$"
expect_in_order "$LOG" "[0.12] sleep 10, Ctrl+C: cancelled (logged as Ctrl+C, then the message)" \
    "^madire> sleep 10$" "^console: sleep 10 s$" "^console: Ctrl\+C: sleep cancelled after [0-9]+ ticks$" \
    "^sleep: cancelled\.$" "^madire> time sleep 1$"
t=$(sed -nE 's/^console: Ctrl\+C: sleep cancelled after ([0-9]+) ticks$/\1/p' "$LOG")
if [ -n "$t" ] && [ "$t" -lt 1000 ]; then pass "[0.12] sleep 10, Ctrl+C: ended early, after $t ticks (fewer than 1000)"
else fail "[0.12] sleep 10, Ctrl+C: cancelled after ${t:-?} ticks (expected fewer than 1000)"; fi
reject "$LOG" "^madire> echo lost$|^lost$"            "[0.12] keys typed during sleep are ignored (echo lost never ran)"
expect_in_order "$LOG" "[0.12] time sleep 1: the time line after the wait, 100 or 101 ticks" \
    "^madire> time sleep 1$" "^console: sleep 1 s$" "^console: sleep done after" "^time: 10[01] ticks \(1\.0[01] s\)$" \
    "^madire> presskey$"
# presskey
expect_in_order "$LOG" "[0.12] presskey: the text, then one key ends it (Shift+Ctrl alone ignored, the key not typed)" \
    "^madire> presskey$" "^Press any key to continue\.\.\.$" "^console: presskey: waiting for a key$" \
    "^console: presskey: key pressed$" "^madire> echo after-presskey$" "^after-presskey$"
expect_in_order "$LOG" "[0.12] presskey, Ctrl+C: cancelled" \
    "^madire> presskey $" "^Press any key to continue\.\.\.$" "^console: presskey: waiting for a key$" \
    "^console: Ctrl\+C: presskey cancelled$" "^presskey: cancelled\.$" "^madire> time presskey$"
expect_in_order "$LOG" "[0.12] time presskey: the time line after the key" \
    "^madire> time presskey$" "^Press any key to continue\.\.\.$" "^console: presskey: key pressed$" "$TL" \
    "^madire> presskey x$"
count_lines "$LOG" "^console: presskey: key pressed$" 2 "[0.12] exactly 2 key presses ended presskey"
expect_next "$LOG" "[0.12] presskey x: usage" "^madire> presskey x$" "^Usage: presskey$"
expect_lines "$LOG" "[0.12] help presskey" \
    "^madire> help presskey$" "^presskey: Wait for a key press \(Ctrl\+C cancels\)$" "^Usage: presskey$"
# set, unset, export, env
expect_lines "$LOG" "[0.12] set: no variables yet" "^madire> set$" "^No variables\.$" "$NEXT"
expect_lines "$LOG" "[0.12] env: nothing exported yet" "^madire> env$" "^No exported variables\.$" "$NEXT"
expect_next "$LOG" "[0.12] set A=1: nothing printed" "^madire> set A=1$" "$NEXT"
expect_next "$LOG" "[0.12] set A: shows one" "^madire> set A$" "^A=1$"
expect_next "$LOG" "[0.12] set W=x   y  z: the words joined with single spaces" "^madire> set W$" "^W=x y z$"
expect_lines "$LOG" "[0.12] set: all, by name in byte order (the first '=' ends the name; an empty value)" \
    "^madire> set $" "^A=1$" "^B=b=c$" "^E=$" "^W=x y z$" "^_u=under$" "^a=small$" "$NEXT"
expect_next "$LOG" "[0.12] set nosuch: not set" "^madire> set nosuch$" "^set: nosuch is not set$"
expect_next "$LOG" "[0.12] set A b: usage" "^madire> set A b$" "^Usage: set \[NAME\[=value\]\] \(names are case-sensitive\)$"
expect_next "$LOG" "[0.12] set 1abc=v: invalid name" "^madire> set 1abc=v$" "^set: invalid name: 1abc$"
expect_next "$LOG" "[0.12] set =v: invalid name (empty)" "^madire> set =v$" "^set: invalid name: \(empty\)$"
expect_next "$LOG" "[0.12] set with a 17-character name: invalid name" \
    "^madire> set abcdefghijklmnopq=v$" "^set: invalid name: abcdefghijklmnopq$"
expect_next "$LOG" "[0.12] set with a 16-character name: accepted" "^madire> set abcdefghijklmnop=16$" "$NEXT"
expect_lines "$LOG" "[0.12] help set: case-sensitive names in the usage" \
    "^madire> help set$" "^set: Show or set variables: set \[NAME\[=value\]\]$" \
    "^Usage: set \[NAME\[=value\]\] \(names are case-sensitive\)$"
expect_next "$LOG" "[0.12] set with a 63-character value: accepted" "^madire> set V=v{63}$" "$NEXT"
expect_next "$LOG" "[0.12] set with a 64-character value: refused" \
    "^madire> set V=v{64}$" "^set: value longer than 63 characters$"
expect_next "$LOG" "[0.12] set V: the 63-character value kept" "^madire> set V$" "^V=v{63}$"
expect_lines "$LOG" "[0.12] env after export A, export a: both, sorted" "^madire> env $" "^A=1$" "^a=small$" "$NEXT"
expect_lines "$LOG" "[0.12] set A=2: the new value keeps the export mark" "^madire> env  $" "^A=2$" "^a=small$" "$NEXT"
expect_lines "$LOG" "[0.12] unset a: gone from env" "^madire> env   $" "^A=2$" "$NEXT"
expect_lines "$LOG" "[0.12] set a again after unset: not exported (unset removed the mark)" \
    "^madire> env    $" "^A=2$" "$NEXT"
expect_next "$LOG" "[0.12] unset nosuch: not set" "^madire> unset nosuch$" "^unset: nosuch is not set$"
expect_next "$LOG" "[0.12] unset alone: usage" "^madire> unset$" "^Usage: unset <NAME>$"
expect_next "$LOG" "[0.12] export nosuch: not set" "^madire> export nosuch$" "^export: nosuch is not set$"
expect_next "$LOG" "[0.12] export B=2: refused, with what to do instead" \
    "^madire> export B=2$" "^export: NAME=value is not supported; use set NAME=value, then export NAME\.$"
expect_next "$LOG" "[0.12] export alone: usage" "^madire> export$" "^Usage: export <NAME>$"
expect_next "$LOG" "[0.12] env x: usage" "^madire> env x$" "^Usage: env$"
expect_next "$LOG" "[0.12] the 32nd variable: accepted" "^madire> set V23=23$" "$NEXT"
expect_next "$LOG" "[0.12] a 33rd variable: refused (no room)" "^madire> set V24=24$" "^set: no room \(32 variables\)$"
expect_next "$LOG" "[0.12] with 32 variables a value can still change" "^madire> set A=3$" "$NEXT"
expect_in_order "$LOG" "[0.12] after unset: room again" \
    "^madire> set V24=24$" "^set: no room" "^madire> unset V00$" "^madire> set V24=24$" "^madire> set  $"
P=("^madire> set  $" "^A=3$" "^B=b=c$" "^E=$" "^V=v{63}$")
for i in $(seq -w 1 24); do P+=("^V$i=$((10#$i))$"); done
P+=("^W=x y z$" "^_u=under$" "^a=back$" "^abcdefghijklmnop=16$" "$NEXT")
expect_lines "$LOG" "[0.12] set: all 32, sorted, values as set" "${P[@]}"
w=$(max_width "$LOG" "^console: started$" "^end-of-0122-test$")
if grep -q "^end-of-0122-test$" "$LOG" && [ "$w" -le 79 ]; then
    pass "[0.12] stage 2 session: every console line at most 79 characters (longest $w)"
else
    fail "[0.12] stage 2 session: a console line of $w characters (limit 79), or the session did not finish"
fi
reject "$LOG" "^key: "                                "[0.12] stage 2 session: no key events logged (keylog off; presskey's key never logged)"
reject "$LOG" "PANIC|events dropped|Screen output from thread|Unexpected IRQ" \
                                                      "[0.12] stage 2 session: no panic, dropped key or screen-rule violation"

section "[0.12] The uptime heartbeat: serial log only, never in dmesg (stage 4, option B)"
# The console idles for 15 s (at least 10 s of guest time, so two or more
# heartbeats even on a slow host), then "dmesg 5". The command runs on
# thread 0, which also writes the heartbeat, so no heartbeat can come
# between the command's line and its 5 output lines.
LOG=$OUT/serial-dmesgidle.log
boot_text "$LOG" 90 "System ready." "x{wait:1}echo before-idle{ret}{wait:15}dmesg 5{ret}echo end-of-dmesgidle-test{ret}" \
    0.05 $'\nend-of-dmesgidle-test\n' -kernel "$KERNEL"
DM=$(awk '/^madire> dmesg 5$/ && !n { n = 1; next } n >= 1 && n <= 5 { print; n++ }' "$LOG")
sed 's/^/      dmesg 5: /' <<< "$DM"
if [ "$(grep -c '' <<< "$DM")" -eq 5 ] && ! grep -q '^uptime: ' <<< "$DM" &&
   [ "$(sed -n 4p <<< "$DM")" = "madire> echo before-idle" ] && [ "$(sed -n 5p <<< "$DM")" = "madire> dmesg 5" ]; then
    pass "[0.12] dmesg 5 after 15 s idle: no uptime line; the last two are the command before the pause and dmesg 5"
else
    fail "[0.12] dmesg 5 after 15 s idle: an uptime line in dmesg, or not the last 5 kernel-log lines"
fi
HB=$(awk '/^madire> echo before-idle$/ && !d { n = 1; next } /^madire> dmesg 5$/ && n { n = 0; d = 1 }
          n && /^uptime: [0-9]+ s$/' "$LOG" | grep -c '')
if [ "$HB" -ge 2 ]; then
    pass "[0.12] the heartbeat is still in the serial log during the idle period ($HB lines)"
else
    fail "[0.12] the heartbeat is missing from the serial log during the idle period ($HB lines, at least 2 expected)"
fi

# ---- 0.13: where Limine places the boot data (the real ISO's normal boot) ----

section "[0.13] Limine boot: the Multiboot information and the modules"
LOG=$OUT/serial-boot.log
# Every boot-data structure the kernel reserves (the information block, the
# memory map, the command line, the module table, the module names) lies in
# the low 1 MiB, which the kernel reserves whole anyway.
BDBAD=$(awk '/^reserve: start=0x[0-9a-f]+ end=0x[0-9a-f]+ (multiboot info|multiboot memory map|kernel command line|boot module table|boot module name)$/ {
                 s = $2; e = $3; sub(/start=0x/, "", s); sub(/end=0x/, "", e); n++
                 if (length(e) > 16 || strtonum_hex(e) > 1048576) print $0 }
             function strtonum_hex(h,   i, v) { v = 0; for (i = 1; i <= length(h); i++) v = v * 16 + index("0123456789abcdef", substr(h, i, 1)) - 1; return v }
             END { if (n < 5) print "only " n " boot-data lines" }' "$LOG")
if [ -z "$BDBAD" ]; then
    pass "[0.13] Limine boot: the Multiboot information (block, memory map, command line, module table and names) lies in the reserved low 1 MiB"
else
    fail "[0.13] Limine boot: boot data outside the low 1 MiB: ${BDBAD//$'\n'/; }"
fi
# The 7 program modules lie above the kernel image and below 4 GiB.
MODBAD=$(awk 'function hex(h,   i, v) { v = 0; for (i = 1; i <= length(h); i++) v = v * 16 + index("0123456789abcdef", substr(h, i, 1)) - 1; return v }
              /^reserve: start=0x[0-9a-f]+ end=0x[0-9a-f]+ kernel image / { k = $3; sub(/end=0x/, "", k); kend = hex(k) }
              /^reserve: start=0x[0-9a-f]+ end=0x[0-9a-f]+ boot module$/ {
                  s = $2; e = $3; sub(/start=0x/, "", s); sub(/end=0x/, "", e); n++
                  if (kend == 0 || hex(s) < kend || hex(e) > 4294967296) print $0 }
              END { if (n != 7) print n " boot modules, expected 7" }' "$LOG")
if [ -z "$MODBAD" ]; then
    pass "[0.13] Limine boot: the 7 modules lie above the kernel image and below 4 GiB"
else
    fail "[0.13] Limine boot: module placement: ${MODBAD//$'\n'/; }"
fi

# ---- 1.0: the live RAM filesystem and its boot self-test (the normal boot) ----

section "[1.0] Filesystem: the live RAM filesystem and its boot self-test"
LOG=$OUT/serial-boot.log
grep -E "^Filesystem" "$LOG" | sed 's/^/      /'
expect "$LOG" "^Filesystem: live RAM filesystem, 1024 nodes, 2 MiB per file" \
                                                      "[1.0] filesystem: the live RAM filesystem, 1024 nodes, 2 MiB per file"
expect "$LOG" "^Filesystem: / holds home, tmp; 3 nodes, 0 data pages$" \
                                                      "[1.0] filesystem: / holds home and tmp at boot (3 nodes, 0 data pages)"
expect "$LOG" "^Filesystem self-test: create and look up: 3 folders and 5 files under /tmp/fstest, each found by its absolute and its relative path: OK$" \
                                                      "[1.0] filesystem self-test: create, and look up by absolute and relative path"
expect "$LOG" "^Filesystem self-test: paths: '\.', '\.\.', '\.\.' of '/', repeated '/', a trailing '/' \(folders only\), the empty path: OK$" \
                                                      "[1.0] filesystem self-test: '.', '..', '..' of '/', repeated and trailing '/', the empty path"
expect "$LOG" "^Filesystem self-test: limits: a 255-byte name created and found, 256 bytes ENAMETOOLONG; a 4095-byte path found, 4096 bytes ENAMETOOLONG; nothing created: OK$" \
                                                      "[1.0] filesystem self-test: NAME_MAX 255 and PATH_MAX 4096 exactly, nothing created past them"
expect "$LOG" "^Filesystem self-test: errors: 16 cases answered as Linux does \(EEXIST, ENOENT, ENOTDIR, EISDIR, ENOTEMPTY, EINVAL, EBUSY\): OK$" \
                                                      "[1.0] filesystem self-test: 16 error cases answered as Linux does"
expect "$LOG" "^Filesystem self-test: rename: in place, into another folder, over a file, a folder over an empty folder; into its own subtree EINVAL, a file onto a folder EISDIR, a folder onto a file ENOTDIR, the same node unchanged: OK$" \
                                                      "[1.0] filesystem self-test: rename (moves, replacements, refusals, the same node)"
expect "$LOG" "^Filesystem self-test: depth: a chain of 64 folders created, looked up by one path, removed: OK$" \
                                                      "[1.0] filesystem self-test: a chain of 64 folders looked up by one path"
FSC=$(grep -m1 -E '^Filesystem self-test: cleanup: ' "$LOG")
read -r N0 N1 H0 H1 <<< "$(sed -nE 's/.*nodes ([0-9]+) -> ([0-9]+), heap in use ([0-9]+) -> ([0-9]+) bytes: OK$/\1 \2 \3 \4/p' <<< "$FSC")"
if grep -qE '^Filesystem self-test: cleanup: everything removed \(a held file freed at its drop\); ' <<< "$FSC" &&
   [ "${N0:-x}" = 3 ] && [ "${N1:-y}" = 3 ] && [ -n "${H0:-}" ] && [ "$H0" = "${H1:-}" ]; then
    pass "[1.0] filesystem self-test: everything removed; nodes 3 -> 3, heap in use $H0 -> $H1 bytes (no node or byte left)"
else
    fail "[1.0] filesystem self-test: cleanup: ${FSC:-no line}"
fi
expect_in_order "$LOG" "[1.0] filesystem self-test: PASS, after the kernel heap and before System ready" \
    "^Kernel heap: +OK" "^Filesystem self-test: PASS$" "^System ready\."

# 1.0 stage 2: the data budget (B5, B6) and the self-test's data step, on
# the normal boot (the ISO, QEMU's default 128 MiB). The first stage 1
# check above matches the start of the B6 line, which now ends with
# ", budget N pages"; its regex and condition are unchanged.
section "[1.0] Filesystem: the data budget and the self-test's data step"
expect "$LOG" "^Filesystem: live RAM filesystem, 1024 nodes, 2 MiB per file, budget 8192 pages$" \
                                                      "[1.0] filesystem: budget 8192 pages on the 128 MiB boot"
FSB=$(grep -m1 -E '^Filesystem: budget: ' "$LOG")
read -r BCAP BFREE BPAGES BKIB <<< "$(sed -nE 's/^Filesystem: budget: min\(([0-9]+) pages, 3\/4 of ([0-9]+) free pages\) = ([0-9]+) pages \(([0-9]+) KiB\), 0 taken at boot$/\1 \2 \3 \4/p' <<< "$FSB")"
if [ -n "${BFREE:-}" ] && [ "$BCAP" = 8192 ]; then
    BWANT=$((BFREE * 3 / 4))
    if [ "$BWANT" -gt 8192 ]; then BWANT=8192; fi
fi
if [ -n "${BWANT:-}" ] && [ "$BPAGES" = "$BWANT" ] && [ "$BKIB" = $((BWANT * 4)) ]; then
    pass "[1.0] filesystem: budget = min(8192, 3/4 of the $BFREE free pages at boot) = $BPAGES pages, recomputed from the log"
else
    fail "[1.0] filesystem: budget line wrong or not recomputable (${FSB:-no line}; expected ${BWANT:-?} pages)"
fi
FSD=$(grep -m1 -E '^Filesystem self-test: data: ' "$LOG")
echo "      ${FSD:-no data line}"
if grep -qE '^Filesystem self-test: data: 8192 bytes written across 3 pages and read back, a hole at 1 MiB reads as zeros and costs nothing, truncate frees pages and growth takes none; .*: OK$' <<< "$FSD"; then
    pass "[1.0] filesystem self-test: data written across page boundaries and read back; a hole reads as zeros and costs nothing; truncate frees pages"
else
    fail "[1.0] filesystem self-test: data step: ${FSD:-no line}"
fi
read -r D0 D1 P0 P1 <<< "$(sed -nE 's/.*; data pages ([0-9]+) -> ([0-9]+), free pages ([0-9]+) -> ([0-9]+): OK$/\1 \2 \3 \4/p' <<< "$FSD")"
if [ "${D0:-x}" = 0 ] && [ "${D1:-y}" = 0 ] && [ -n "${P0:-}" ] && [ "$P0" = "${P1:-}" ]; then
    pass "[1.0] filesystem self-test: data pages 0 -> 0, free pages $P0 -> $P1 (every data and index page given back)"
else
    fail "[1.0] filesystem self-test: pages not given back (${FSD:-no line})"
fi

# 1.1 stage 6: Num Lock (on at boot), the keypad with Num Lock off and the
# ISO key 0x56, typed over QMP into a keylog boot (K1-K8).
section "[1.1] Keys: Num Lock, the keypad and the ISO key (keylog)"
LOG=$OUT/serial-keys11.log
S="x{wait:1}"                                         # opens the console
S+="echo {kp_1}{kp_2}{ret}"                           # K1 Num Lock on (boot)
S+="{num_lock}{shift+kp_9}"                           # K8 Num Lock off, Shift + keypad 9
S+="echo ab{kp_4}{kp_4}X{ret}"                        # K2 keypad 4 = Left
S+="{kp_8}{ret}"                                      # K3, K4 keypad 8 = Up (history)
S+="{num_lock}echo {kp_8}{ret}"                       # K5 Num Lock on again
S+="echo {less}{shift+less}{ret}"                     # K6 the ISO key
S+="echo end-of-keys11-test{ret}"
boot_text "$LOG" 90 "System ready." "$S" 0.05 $'\nend-of-keys11-test\n' -m 32M -kernel "$KERNEL" -append "keylog"
expect_next "$LOG" "[1.1] keypad with Num Lock on (the boot state) types digits: echo 12" \
    '^madire> echo 12$' '^12$'
expect_next "$LOG" "[1.1] keypad with Num Lock off: 4 moves left (twice), the line runs as echo Xab" \
    '^madire> echo Xab$' '^Xab$'
expect_in_order "$LOG" "[1.1] keypad with Num Lock off: 8 recalls the last line (Up)" \
    '^madire> echo Xab$' '^Xab$' '^madire> echo Xab$' '^Xab$'
expect "$LOG" 'key: press sc=0x48 key=UP ascii=none flags=0x01' \
    "[1.1] keypad 8 with Num Lock off decodes as UP without the extended flag"
expect_next "$LOG" "[1.1] Num Lock on again: keypad 8 types 8" \
    '^madire> echo 8$' '^8$'
expect_in_order "$LOG" "[1.1] the ISO key 0x56 types a backslash, and with Shift a vertical bar" \
    'key: press sc=0x56 key=CHAR ascii=.\\. flags=0x01' \
    'key: press sc=0x56 key=CHAR ascii=.\|. flags=0x03' \
    '^madire> echo \\\|$' \
    '^\\\|$'
reject "$LOG" 'events dropped|key=UNKNOWN|PANIC' \
    "[1.1] keys11 boot: nothing dropped or unknown, no panic"
expect_in_order "$LOG" "[1.1] Shift + keypad 9 with Num Lock off pages the scrollback like Shift+PgUp" \
    'key: press sc=0x49 key=PGUP ascii=none flags=0x03' '^console: scrollback is empty$'

# 1.1 stage 7: the keyboard lights. L1 on the real ISO's normal boot; L2-L5
# and L7 in a keylog boot whose PS/2 device side is traced by QEMU
# (ps2_write_keyboard: every byte the guest writes to the keyboard;
# ps2_set_ledstate: the state QEMU stores). The trace file is not
# serial-*.log, so it adds no boot count.
section "[1.1] Keyboard lights"
expect "$OUT/serial-boot.log" '^Keyboard lights: Num Lock on, Caps Lock off, Scroll Lock off \(acknowledged\)$' \
    "[1.1] keyboard lights: set at boot (Num Lock on, Caps Lock off, Scroll Lock off), acknowledged"
LOG=$OUT/serial-lights.log
LTRACE=$OUT/lights-trace.txt
rm -f "$LTRACE"
S="x{wait:1}"
S+="{caps}{wait:0.5}{num_lock}{wait:0.5}{scroll_lock}{wait:0.5}"
S+="{caps}{wait:0.5}{num_lock}{wait:0.5}{scroll_lock}{wait:0.5}"   # 6, 4, 5, 1, 3, 2
S+="echo a{caps}b{caps}c{ret}"                                      # L5
S+="echo end-of-lights-test{ret}"
boot_text "$LOG" 90 "System ready." "$S" 0.05 $'\nend-of-lights-test\n' -m 32M -kernel "$KERNEL" \
    -append "keylog" -D "$LTRACE" -trace ps2_write_keyboard -trace ps2_set_ledstate
# Report only (decides no check): the start of the trace, the firmware's
# writes before the kernel's.
echo "      lights-trace.txt, first 20 lines:"
head -n 20 "$LTRACE" 2>/dev/null | sed 's/^/      /'
# The trace as tokens in file order: w<byte written> and s<state stored>.
LSEQ=" $(sed -nE 's/.*ps2_write_keyboard 0x[0-9a-f]+ val ([0-9]+).*/w\1/p
                 s/.*ps2_set_ledstate 0x[0-9a-f]+ ledstate ([0-9]+).*/s\1/p' "$LTRACE" 2>/dev/null | tr '\n' ' ')"
# L2: keyboard_init's ECHO (238), the boot exchange (237, 2; QEMU stores 2),
# then the self-test's ECHO (238), one after another.
LBOOT=" w238 w237 w2 s2 w238 "
if [[ $LSEQ == *"$LBOOT"* ]]; then
    pass "[1.1] keyboard lights: between the kernel's two ECHO bytes QEMU's keyboard got 0xED and the state 2, and stored it"
else
    fail "[1.1] keyboard lights: no '${LBOOT# }' in the trace (trace:${LSEQ})"
fi
# L3: after the second ECHO, every stored state comes right after the
# writes 237 and that state; the states begin 6, 4, 5, 1, 3, 2 and the last
# is 2 (L5's two Caps Lock presses may be synced once, twice or not at all).
LTAIL=${LSEQ#*"$LBOOT"}
[[ $LSEQ == *"$LBOOT"* ]] || LTAIL=""
read -r -a LTOK <<< "$LTAIL"
lok=1 lstates=""
for ((li = 0; li < ${#LTOK[@]}; li++)); do
    lt=${LTOK[li]}
    case "$lt" in
    s*) if [ "$li" -lt 2 ] || [ "${LTOK[li-2]}" != w237 ] || [ "${LTOK[li-1]}" != "w${lt#s}" ]; then lok=0; fi
        lstates+="${lt#s} " ;;
    esac
done
case "$lstates" in "6 4 5 1 3 2 "*) ;; *) lok=0 ;; esac
case "$lstates" in *" 2 ") ;; *) lok=0 ;; esac
if [ "$lok" = 1 ]; then
    pass "[1.1] keyboard lights: six lock presses set QEMU's state to 6, 4, 5, 1, 3, 2 in order (each after 0xED), the last state is 2"
else
    fail "[1.1] keyboard lights: states after the second ECHO: ${lstates:-none} (trace after it:${LTAIL:- none})"
fi
expect_in_order "$LOG" "[1.1] keyboard lights: with keylog each change is logged as its state, acknowledged (6, 4, 5, 1, 3, 2)" \
    '^Keyboard lights: Num Lock on, Caps Lock off, Scroll Lock off \(acknowledged\)$' \
    '^Keyboard lights: Num Lock on, Caps Lock on, Scroll Lock off \(acknowledged\)$' \
    '^Keyboard lights: Num Lock off, Caps Lock on, Scroll Lock off \(acknowledged\)$' \
    '^Keyboard lights: Num Lock off, Caps Lock on, Scroll Lock on \(acknowledged\)$' \
    '^Keyboard lights: Num Lock off, Caps Lock off, Scroll Lock on \(acknowledged\)$' \
    '^Keyboard lights: Num Lock on, Caps Lock off, Scroll Lock on \(acknowledged\)$' \
    '^Keyboard lights: Num Lock on, Caps Lock off, Scroll Lock off \(acknowledged\)$'
expect_next "$LOG" "[1.1] keyboard lights: typed text exact across two light changes (echo aBc)" \
    '^madire> echo aBc$' '^aBc$'
reject "$LOG" 'Keyboard lights: not acknowledged|events dropped|key=UNKNOWN|PANIC' \
    "[1.1] lights boot: no light failure, nothing dropped or unknown, no panic"

# 1.1 stage 8: Ctrl+C, Esc and Tab at the prompt, in a normal boot (no
# keylog): C1-C10 and L6. The cleared lines' text must never reach the
# serial log (C3: the history and dmesg output is on it too).
section "[1.1] Console keys: Ctrl+C, Esc and Tab (no keylog)"
LOG=$OUT/serial-conskeys11.log
S="x{wait:1}"
S+="echo secretmarker11{ctrl+c}"                      # C1 Ctrl+C with text
S+="{ctrl+c}"                                         # C1 Ctrl+C on an empty line
S+="echo escmarker11{esc}"                            # C2 Esc
S+="echo after-clear{ret}"
S+="history{ret}"                                     # C3 (the markers must not be listed)
S+="reboot{ret}{wait:1}ye{ctrl+c}{wait:1}"            # C4 Ctrl+C at the question
S+="his{tab}{ret}"                                    # C5 unique: "history "
S+="un{tab}{tab}{ctrl+c}"                             # C6 ambiguous: the list
S+="echo a{tab}b{ret}"                                # C7 a later word: nothing
S+="zz{tab}{bs}{bs}echo nomatch-ok{ret}"              # C7 no match: nothing
S+="echo {caps}q{caps}r{ret}"                         # C8, L6 Caps Lock without keylog
S+="dmesg 9999{ret}"                                  # C3 dmesg's text on the serial log
S+="echo end-of-conskeys-test{ret}"
boot_text "$LOG" 120 "System ready." "$S" 0.05 $'\nend-of-conskeys-test\n' -m 32M -kernel "$KERNEL"
expect_in_order "$LOG" "[1.1] Ctrl+C at the prompt clears the line, with text and on an empty line (logged only as a fixed line)" \
    '^console: Ctrl\+C: line cleared$' '^console: Ctrl\+C: line cleared$' '^madire> echo after-clear$'
expect_next "$LOG" "[1.1] Esc clears the line: the next line runs exactly as echo after-clear" \
    '^madire> echo after-clear$' '^after-clear$'
reject "$LOG" 'secretmarker11|escmarker11' \
    "[1.1] cleared lines reach neither the serial log, the history nor dmesg (secretmarker11, escmarker11)"
expect_in_order "$LOG" "[1.1] Ctrl+C at a yes/no question answers no: Restart cancelled., no restart" \
    '^madire> reboot$' '^console: Ctrl\+C: answered no$' '^Restart cancelled\.$' '^madire> history $'
expect "$LOG" '^madire> history $' \
    "[1.1] Tab completes a unique command name and adds a space (his: history)"
expect_in_order "$LOG" "[1.1] Tab on an ambiguous name: the second Tab lists the matches (un: unalias  uname  unset)" \
    '^unalias  uname  unset$' '^console: Ctrl\+C: line cleared$'
expect_in_order "$LOG" "[1.1] Tab in a later word, or with no match, types nothing (echo ab, echo nomatch-ok)" \
    '^madire> echo ab$' '^ab$' '^madire> echo nomatch-ok$' '^nomatch-ok$'
expect_next "$LOG" "[1.1] Caps Lock in a normal boot types exactly (echo Qr)" \
    '^madire> echo Qr$' '^Qr$'
reject "$LOG" '^Keyboard lights: Num Lock on, Caps Lock on|^key: ' \
    "[1.1] keyboard lights: Caps Lock changes not logged without keylog (no light or key lines)"
expect_next "$LOG" "[1.1] console keys boot: the session ran to its end" \
    '^madire> echo end-of-conskeys-test$' '^end-of-conskeys-test$'
reject "$LOG" 'PANIC|events dropped|Keyboard lights: not acknowledged' \
    "[1.1] console keys boot: no panic, nothing dropped, no light failure"

# ---- 1.2: the Display line of the text boots (stage 5) ----

section "[1.2] Display: a -kernel boot (VGA text) and the real ISO (graphics, since stage 10b)"
# The logs of two boots made above (no new boot): QEMU's -kernel gives no
# framebuffer fields (flag bit 12 clear) and keeps VGA text (decision 4).
# The real ISO's default entry, without a "textmode" line since the switch
# (stage 10b), gets the header's 1024x768x32 framebuffer and its console
# on it (a new check in 10b). (Until 10a it had "textmode: yes" and logged
# Limine's text mode; that check reads the text twin since 10b, below, and
# the Diagnostics entries are checked in part 4.)
expect "$OUT/serial-mem-32M.log" "^Display: VGA text 80x25 \(no framebuffer\)$" \
    "[1.2] -kernel boot: Display: VGA text 80x25 (no framebuffer)"
if grep -qE "^Display: framebuffer 1024x768x32 at 0x[0-9a-f]+, pitch 4096, RGB at bits 16/8/0$" "$OUT/serial-boot.log" &&
   grep -qxF "Display: console on the framebuffer: 80x25 cells of 8x16 pixels at (192, 184), scale 100 %" "$OUT/serial-boot.log"; then
    pass "[1.2] real ISO, default entry (graphics): Display: framebuffer 1024x768x32, pitch 4096, RGB at bits 16/8/0, and the console on it: 80x25 cells of 8x16 pixels at (192, 184), scale 100 %"
else
    fail "[1.2] real ISO, default entry: not the 1024x768x32 framebuffer with its console at scale 100 % ($(grep -E '^Display: ' "$OUT/serial-boot.log" | tr '\n' ' '))"
fi

# ---- 1.2 stage 5b: slot 506 and the framebuffer test ISOs ----

section "[1.2] The real ISO (graphics, since stage 10b): slot 506"
# The real ISO's normal boot now builds slot 506 (the framebuffer and its
# back buffer) and freezes it with the kernel half (7 entries), and
# tests/pagetables.py, which models slot 506 from the "Display: slot 506"
# line, agrees with the kernel's count. (Stage 5b's two checks of a text
# boot without slot 506 read the text twin since 10b, below.)
LOG=$OUT/serial-boot.log
if grep -qE "^Display: slot 506: framebuffer 0x[0-9a-f]+-0x[0-9a-f]+ at 0xfffffd0000000000 \([0-9]+ pages\), back buffer at 0xfffffd0040000000 \([0-9]+ pages of free RAM, pitch 4096\), [0-9]+ page tables$" "$LOG" &&
   grep -qxF "Kernel half frozen: PML4 slots 256, 506, 507, 508, 509, 510, 511 (7 entries); every process shares them, a new one now panics" "$LOG"; then
    pass "[1.2] real ISO (graphics): slot 506 built (its Display: slot 506 line), PML4[506] frozen with the kernel half: slots 256, 506, 507, 508, 509, 510, 511 (7 entries)"
else
    fail "[1.2] real ISO (graphics): no Display: slot 506 line, or the kernel half frozen with other slots, in $LOG"
fi
FB_PT=$(python3 "$(dirname "$0")/pagetables.py" "$LOG" --label iso)
sed 's/^/      /' <<< "$FB_PT"
if grep -qE "^PAGETABLES_RESULT label=iso computed=([0-9]+) logged=\1 .* display_slot=([0-9]+)/\2 MATCH$" <<< "$FB_PT"; then
    pass "[1.2] real ISO (graphics): tests/pagetables.py models slot 506: computed = logged, and the slot's tables as logged"
else
    fail "[1.2] real ISO (graphics): tests/pagetables.py: $(grep '^PAGETABLES_RESULT' <<< "$FB_PT")"
fi
# Report only (decides no check): the boots' paging figures. 5a's (run
# 39909205323): the real ISO 14 page tables, 573 mapped pages, free pages
# 32160 (a text boot then); -kernel -m 32M free pages 7597; kernel end
# 0x232000. A text boot's mapped pages may differ from 5a's only by the
# kernel image's own pages (the change of __kernel_end, printed here).
fb_figures() {   # fb_figures <log> <label>
    local t m f e
    t=$(sed -nE 's/^Paging tables: ([0-9]+) page tables \+ PML4, ([0-9]+) mapped pages.*/\1/p' "$1" | head -n 1)
    m=$(sed -nE 's/^Paging tables: ([0-9]+) page tables \+ PML4, ([0-9]+) mapped pages.*/\2/p' "$1" | head -n 1)
    f=$(grep -oE '^memory: .* free_pages=[0-9]+' "$1" | grep -oE '[0-9]+$' | head -n 1)
    e=$(sed -nE 's/^Kernel: text .* end 0x([0-9a-f]+)$/\1/p' "$1" | head -n 1)
    echo "      FIGURES $2: ${t:-?} page tables, ${m:-?} mapped pages, free pages ${f:-?} (memory_init), kernel end 0x${e:-?} ($(( (0x${e:-232000} - 0x232000) / 4096 )) kernel pages against 5a's 0x232000)"
    grep -E "^Paging self-test: no page leak|^Kernel half frozen|^Display: slot 506|^Kernel stack: [0-9]+ bytes at peak" "$1" | sed "s/^/      FIGURES $2: /"
}
fb_figures "$OUT/serial-boot.log" "framebuffer boot (real ISO since 10b; 5a's text boot: 14, 573, 32160)"
fb_figures "$OUT/serial-mem-32M.log" "text boot (-kernel -m 32M; 5a: free 7597)"

# ---- 1.3 stage 8: the switch: the real ISO's default path ----

section "[1.3] The real ISO's default path: the desktop at System ready (stage 8)"
# The logs of the normal boot of the real ISO and of the keylog test ISO
# (fixture_normal_boot; no new boot). Neither has a test= option, so at the
# idle loop's first pass after System ready the splash hands the screen to
# the desktop (kernel/display/desktop.h); the key typed after System ready
# ("x") then opens the Console window and is not typed, and the console
# starts in it. tests/pagetables.py models slot 506 with the background
# cache and the window surfaces from the desktop's own lines.
LOG=$OUT/serial-boot.log
DP_SHOWN="^Desktop: shown on 1024x768x32 \(scale 100 %\): the wallpaper with the M, the taskbar, the clock [0-9]{2}:[0-9]{2}; a key opens the Console window \(the default path, 1\.3 stage 8\)$"
DP_KEY="Desktop: a key opened the Console window (the key is not typed)"
grep -E "^Desktop" "$LOG" | sed 's/^/      /'
if grep -qE "$DP_SHOWN" "$LOG" &&
   grep -qE "^Desktop: slot 506: background cache at 0xfffffd0080000000 \([0-9]+ pages of free RAM, pitch 4096\), [0-9]+ page tables; " "$LOG" &&
   grep -qE "^Desktop: slot 506 now: [0-9]+ page tables, [0-9]+ pages mapped \(walked\)$" "$LOG" && ! grep -q "^Desktop: not started" "$LOG"; then
    pass "[1.3] real ISO: the default path shows the desktop at System ready (Desktop: shown on 1024x768x32 (scale 100 %) ... (the default path, 1.3 stage 8)); the background cached in slot 506; slot 506 walked"
else
    fail "[1.3] real ISO: no default-path Desktop: shown line, no background cache or slot 506 walk line, or a 'not started' line (see $LOG)"
fi
DP_PT=$(python3 "$(dirname "$0")/pagetables.py" "$LOG" --label iso-desktop)
grep -E "^PAGETABLES_(DESKTOP|SURFACES)" <<< "$DP_PT" | sed 's/^/      /'
if grep -qE "^PAGETABLES_DESKTOP label=iso-desktop cache=present computed=([0-9]+/[0-9]+) logged=\1 MATCH$" <<< "$DP_PT"; then
    pass "[1.3] real ISO: tests/pagetables.py models slot 506 with the background cache: its tables and pages equal the kernel's walk, the cache's tables as logged"
else
    fail "[1.3] real ISO: tests/pagetables.py, the background cache: $(grep -E '^PAGETABLES_(RESULT|DESKTOP)' <<< "$DP_PT" | tr '\n' ' ')"
fi
if grep -qE "^PAGETABLES_SURFACES label=iso-desktop cache=present surfaces=Console,About computed=([0-9]+/[0-9]+) logged=\1 MATCH$" <<< "$DP_PT"; then
    pass "[1.3] real ISO: tests/pagetables.py models slot 506 with the window surfaces (Console, then About, after the cache): its tables and pages equal the kernel's walk"
else
    fail "[1.3] real ISO: tests/pagetables.py, the window surfaces: $(grep -E '^PAGETABLES_SURFACES' <<< "$DP_PT" | tr '\n' ' ')"
fi
DP_SHOWN_AT=$(grep -nE "$DP_SHOWN" "$LOG" | head -n 1 | cut -d: -f1)
DP_KEY_AT=$(grep -nxF "$DP_KEY" "$LOG" | head -n 1 | cut -d: -f1)
DP_CON_AT=$(grep -nxF "console: started" "$LOG" | head -n 1 | cut -d: -f1)
if [ -n "$DP_SHOWN_AT" ] && [ -n "$DP_KEY_AT" ] && [ -n "$DP_CON_AT" ] && [ "$DP_SHOWN_AT" -lt "$DP_KEY_AT" ] &&
   [ "$DP_KEY_AT" -lt "$DP_CON_AT" ] && [ "$(grep -cxF "$DP_KEY" "$LOG")" -eq 1 ]; then
    pass "[1.3] real ISO: the key typed after System ready opened the Console window (once, after the desktop showed), where the console started; the key not typed"
else
    fail "[1.3] real ISO: the key did not open the Console window once, after the desktop and before console: started (see $LOG)"
fi
LOG=$KLOG
grep -E "^Desktop|^key: press" "$LOG" | sed 's/^/      /'
expect_in_order "$LOG" "[1.3] keylog test ISO: the default path shows the desktop; the key typed after System ready is logged, opens the Console window (not typed), and the console starts" \
    "$DP_SHOWN" "^key: press sc=0x2d key=CHAR ascii='x' flags=0x01$" "^Desktop: a key opened the Console window \(the key is not typed\)$" "^console: started$"

section "[1.2] Framebuffer boots: slot 506 and test=fbpattern (two test ISOs, tests/qmp_fb.py)"
# Two test ISOs with one entry each and no "textmode" line (in stage 5b the
# real ISO and every other test ISO kept "textmode: yes", decision 3; since
# 10b the real ISO's default entry has none either): the header's
# request (1024x768x32), and Limine's per-entry "resolution: 800x600x24",
# which overrides the executable's request for multiboot1 (Limine 12.9.3:
# CONFIG.md; common/protos/multiboot1.c reads RESOLUTION when the header's
# mode type is 0, as Madire's is). Each boots with test=fbpattern, paused,
# with a QMP socket: tests/qmp_fb.py waits for the kernel's "display:
# frame 1" line, stops the guest, takes a screendump and lets it go on to
# System ready. The screen's size must be the logged mode and its CRC-32
# the one the kernel logged for the framebuffer (read back after present).
QMPFB=$(dirname "$0")/qmp_fb.py
# fb_boot <log> <iso>: boots <iso> paused at -m 128M, runs qmp_fb.py, then
# stops QEMU. FB_OUT holds qmp_fb.py's output (also kept in <log>.fb.txt).
fb_boot() {
    local log=$1 iso=$2 sock=$OUT/qmp-fb.sock t0 qpid
    rm -f "$log" "$sock"
    t0=$(date +%s%N)
    timeout 90 qemu-system-x86_64 -S -display none -no-reboot -serial file:"$log" \
        -qmp unix:"$sock",server=on,wait=off -m 128M -cdrom "$iso" >/dev/null 2>&1 &
    qpid=$!
    FB_OUT=$(python3 "$QMPFB" "$sock" "$log" "display: frame 1" --until "System ready." --timeout 75 2>&1)
    sed 's/^/      /' <<< "$FB_OUT"
    echo "$FB_OUT" > "${log%.log}.fb.txt"
    sleep 0.5
    kill "$qpid" 2>/dev/null
    wait "$qpid" 2>/dev/null
    [ -f "$log" ] || : > "$log"
    boot_seconds "$log" "$t0"
}
# fb_case <name> <mode WxHxD> <label> <the first check's text> [a limine.conf line]
fb_case() {
    local name=$1 mode=$2 label=$3 first=$4 extra=${5:-} dir=$OUT/isofb-$1 iso=$OUT/madire-os-fb-$1.iso
    local log=$OUT/serial-fb-$1.log w h d pt crc
    w=${mode%%x*}; h=${mode#*x}; h=${h%%x*}; d=${mode##*x}
    limine_tree "$dir"
    {
        printf 'timeout: 0\n\n/Madire OS (framebuffer test)\n    protocol: multiboot1\n'
        [ -z "$extra" ] || printf '    %s\n' "$extra"
        printf '    path: boot():/boot/madire.kernel\n    cmdline: test=fbpattern\n'
    } > "$dir/boot/limine/limine.conf"
    sed 's/^/      limine.conf: /' "$dir/boot/limine/limine.conf"
    FB_OUT=""
    if limine_iso "$dir" "$iso"; then fb_boot "$log" "$iso"; else : > "$log"; echo "      the test ISO could not be made"; fi
    grep -E "^Display: |^Paging walk-check: |^Paging self-test: PASS|^Paging check: display|^Kernel half frozen|^fbpattern: |^display: frame " "$log" | sed 's/^/      /'
    fb_figures "$log" "$label"
    expect "$log" "^Display: framebuffer $mode at 0x[0-9a-f]+, pitch [0-9]+, RGB at bits 16/8/0$" "$first"
    if grep -qE "^Display: slot 506: framebuffer 0x[0-9a-f]+-0x[0-9a-f]+ at 0xfffffd0000000000 \([0-9]+ pages\), back buffer at 0xfffffd0040000000 \([0-9]+ pages of free RAM, pitch $((w * d / 8))\), [0-9]+ page tables$" "$log" &&
       grep -qE "^Paging check: display framebuffer .*: OK$" "$log" &&
       grep -qE "^Paging check: display back buffer .*: OK$" "$log" &&
       grep -qE "^Paging walk-check: PASS \([0-9]+ items, 0 mismatches\)$" "$log" &&
       grep -qE "^Paging self-test: PASS \([0-9]+ checks, 0 mismatches\)$" "$log" &&
       grep -qxF "Kernel half frozen: PML4 slots 256, 506, 507, 508, 509, 510, 511 (7 entries); every process shares them, a new one now panics" "$log"; then
        pass "[1.2] $label: slot 506 built (framebuffer and back buffer, walk-checked), paging self-test PASS, PML4[506] frozen with the kernel half (7 entries)"
    else
        fail "[1.2] $label: slot 506 not built, not walk-checked or not frozen with the kernel half, or paging failed (see $log)"
    fi
    pt=$(python3 "$(dirname "$0")/pagetables.py" "$log" --label fb)
    sed 's/^/      /' <<< "$pt"
    if grep -qE "^PAGETABLES_RESULT label=fb computed=([0-9]+) logged=\1 .* display_slot=([0-9]+)/\2 MATCH$" <<< "$pt"; then
        pass "[1.2] $label: tests/pagetables.py models slot 506: computed = logged, and the slot's tables as logged"
    else
        fail "[1.2] $label: tests/pagetables.py: $(grep '^PAGETABLES_RESULT' <<< "$pt")"
    fi
    crc=$(sed -nE "s/^fbpattern: $mode: [0-9]+ fills and [0-9]+ blits drawn on the back buffer, presented; CRC-32 of the RGB bytes: back buffer (0x[0-9a-f]{8}), framebuffer \1 \(equal\)$/\1/p" "$log" | head -n 1)
    if [ -n "$crc" ] && grep -qxF "display: frame 1" "$log"; then
        pass "[1.2] $label: test=fbpattern drew and presented: the back buffer's and the framebuffer's CRC-32 equal ($crc), display: frame 1 logged"
    else
        fail "[1.2] $label: no fbpattern line with equal CRCs, or no display: frame 1 (see $log)"
    fi
    if grep -qxE "SCREEN size=${w}x${h} crc=0x[0-9a-f]{8} colours=[0-9]+" <<< "$FB_OUT"; then
        pass "[1.2] $label: the screendump is ${w}x${h}, the logged mode"
    else
        fail "[1.2] $label: no ${w}x${h} screendump after display: frame 1 ($(grep -E '^(SCREEN|MARKER|qmp_fb)' <<< "$FB_OUT" | tr '\n' ' '))"
    fi
    if [ -n "$crc" ] && grep -qxE "SCREEN size=${w}x${h} crc=$crc colours=[0-9]+" <<< "$FB_OUT"; then
        pass "[1.2] $label: the screendump's CRC-32 equals the framebuffer's logged CRC-32 ($crc)"
    else
        fail "[1.2] $label: the screendump's CRC-32 is not the logged ${crc:-(none)} ($(grep '^SCREEN' <<< "$FB_OUT"))"
    fi
    # 1.2 stage 6: also no FAILED status line (in 5b these boots logged
    # "Display initialization: FAILED" unnoticed; slot 506 planned now
    # counts as a working display, kernel/kernel.c).
    if grep -q "^System ready\." "$log" && ! grep -q "FAILED" "$log" && ! grep -qE "PANIC|MISMATCH" "$log"; then
        pass "[1.2] $label: System ready, no FAILED status line, no panic, no paging mismatch"
    else
        fail "[1.2] $label: no System ready, a FAILED status line, or a panic or a paging mismatch (see $log)"
    fi
}
fb_case 1024 1024x768x32 "framebuffer boot (the header's 1024x768x32)" \
    "[1.2] framebuffer boot: the header's request is given: Display: framebuffer 1024x768x32, RGB at bits 16/8/0"
fb_case 800x24 800x600x24 "framebuffer boot (resolution: 800x600x24)" \
    "[1.2] Limine's per-entry resolution: 800x600x24 overrides the header's 1024x768x32: Display: framebuffer 800x600x24, RGB at bits 16/8/0" \
    "resolution: 800x600x24"

section "[1.2] Framebuffer boots that keep VGA text: the undo path (test=fbfail) and the size refusal (32M)"
# Two more test ISOs, booted without QMP to System ready. Neither gets slot
# 506, so each must look like a text boot from paging on: no PML4[506], the
# page tables tests/pagetables.py computes without slot 506, and every
# frame back. fb_text_boot <name> <memory> <limine.conf line or ""> <cmdline>
FB_READY="System ready. Press any key to open the console."
fb_text_boot() {
    local dir=$OUT/isofb-$1 iso=$OUT/madire-os-fb-$1.iso log=$OUT/serial-fb-$1.log
    limine_tree "$dir"
    {
        printf 'timeout: 0\n\n/Madire OS (framebuffer test)\n    protocol: multiboot1\n'
        [ -z "$3" ] || printf '    %s\n' "$3"
        printf '    path: boot():/boot/madire.kernel\n    cmdline: %s\n' "$4"
    } > "$dir/boot/limine/limine.conf"
    sed 's/^/      limine.conf: /' "$dir/boot/limine/limine.conf"
    if limine_iso "$dir" "$iso"; then boot_until "$log" 60 "$FB_READY" -m "$2" -cdrom "$iso"
    else : > "$log"; echo "      the test ISO could not be made"; fi
    grep -E "^Display: |^Free pages: |^Paging tables: |^Paging walk-check: |^Paging self-test: (PASS|no page leak)|^Kernel half frozen|^fbpattern: " "$log" | sed 's/^/      /'
    fb_figures "$log" "$1"
}
# fb_kept <log> <label>: the checks both boots share.
fb_kept() {
    local log=$1 pt
    if grep -qxF "Kernel half frozen: PML4 slots 256, 507, 508, 509, 510, 511 (6 entries); every process shares them, a new one now panics" "$log" &&
       ! grep -q "^Display: slot 506:" "$log" && ! grep -q "^Paging check: display" "$log"; then
        pass "[1.2] $2: no PML4[506]: the kernel half frozen with slots 256, 507, 508, 509, 510, 511 (6 entries) as before 1.2, no slot 506 line or walk-check item"
    else
        fail "[1.2] $2: a slot 506 line or walk-check item, or the kernel half frozen with other slots (see $log)"
    fi
    pt=$(python3 "$(dirname "$0")/pagetables.py" "$log" --label kept)
    sed 's/^/      /' <<< "$pt"
    FB_KEPT_TABLES=$(sed -nE 's/^PAGETABLES_RESULT label=kept computed=([0-9]+) logged=\1 .* display_slot=none MATCH$/\1/p' <<< "$pt")
    if [ -n "$FB_KEPT_TABLES" ]; then
        pass "[1.2] $2: tests/pagetables.py computed = logged ($FB_KEPT_TABLES page tables), slot 506 not modelled"
    else
        fail "[1.2] $2: tests/pagetables.py: $(grep '^PAGETABLES_RESULT' <<< "$pt")"
    fi
    if grep -qxF "fbpattern: no framebuffer mapped (see the Display: lines); nothing drawn" "$log" &&
       grep -qxF "$FB_READY" "$log" && grep -qE "^Paging walk-check: PASS \([0-9]+ items, 0 mismatches\)$" "$log" &&
       ! grep -qE "PANIC|MISMATCH" "$log"; then
        pass "[1.2] $2: VGA text and serial kept (fbpattern: no framebuffer mapped), walk-check PASS, System ready, no panic"
    else
        fail "[1.2] $2: no 'no framebuffer mapped' line, no walk-check PASS or System ready, or a panic or mismatch (see $log)"
    fi
}

# The undo path: test=fbfail=868 on the 1024x768x32 boot. Steps 1-768 are
# the framebuffer's pages, so step 868 is the back buffer's page 99 (from 0):
# by then 99 back-buffer frames are taken, and 6 page tables (the PDPT, the
# framebuffer's directory and its 2 page tables, the back buffer's
# directory and its first page table); all of them must come back.
fb_text_boot fail 128M "" "test=fbfail=868"
LOG=$OUT/serial-fb-fail.log
expect "$LOG" "^Display: slot 506 not built \(back buffer page at 0xfffffd0040063000: test=fbfail\); 768 framebuffer pages unmapped, 99 back-buffer frames and 6 page tables returned; VGA text and serial kept$" \
    "[1.2] test=fbfail=868: Display: slot 506 not built (back buffer page at 0xfffffd0040063000: test=fbfail); 768 framebuffer pages unmapped, 99 back-buffer frames and 6 page tables returned; VGA text and serial kept"
fb_kept "$LOG" "test=fbfail=868"
# Every frame back: the free pages the paging self-test finds equal what a
# text boot of the same machine has there: the free pages when slot 506 was
# planned (the boot screen's "Free pages:", logged after the memory
# self-test, with nothing allocated until paging_init) minus the kernel's
# own tables, as many as tests/pagetables.py computes from this log without
# slot 506, plus the PML4.
FB_F=$(sed -nE 's/^Free pages: +([0-9]+)$/\1/p' "$LOG" | head -n 1)
FB_P=$(sed -nE 's/^Paging self-test: no page leak: free pages ([0-9]+) -> \1: OK$/\1/p' "$LOG" | head -n 1)
FB_T=${FB_KEPT_TABLES:-}
if [ -n "$FB_F" ] && [ -n "$FB_P" ] && [ -n "$FB_T" ] && [ "$FB_P" -eq $((FB_F - FB_T - 1)) ]; then
    pass "[1.2] test=fbfail=868: every frame returned: free pages at the paging self-test $FB_P = the text-mode figure $FB_F - $FB_T page tables - the PML4"
else
    fail "[1.2] test=fbfail=868: free pages at the paging self-test ${FB_P:-?}, expected ${FB_F:-?} - ${FB_T:-?} - 1"
fi

# The size refusal: 1920x1080x32 at 32M. Its back buffer is 2025 pages and
# slot 506 would need 11 page tables: 2036 pages, more than a quarter of a
# 32M machine's free pages (about 7600).
fb_text_boot 1920 32M "resolution: 1920x1080x32" "test=fbpattern"
LOG=$OUT/serial-fb-1920.log
FB_A=$(sed -nE 's/^Display: framebuffer 1920x1080x32 at (0x[0-9a-f]+), pitch [0-9]+, RGB at bits 16\/8\/0$/\1/p' "$LOG" | head -n 1)
FB_F=$(sed -nE 's/^Free pages: +([0-9]+)$/\1/p' "$LOG" | head -n 1)
FB_WANT="Display: framebuffer 1920x1080x32 at ${FB_A:-?} not mapped (back buffer and page tables: 2036 pages, over a quarter of the ${FB_F:-?} free pages); VGA text and serial kept"
if [ -n "$FB_A" ] && [ -n "$FB_F" ] && grep -qxF "$FB_WANT" "$LOG"; then
    pass "[1.2] 1920x1080x32 at 32M: Display: framebuffer 1920x1080x32 at $FB_A not mapped (back buffer and page tables: 2036 pages, over a quarter of the $FB_F free pages); VGA text and serial kept"
else
    fail "[1.2] 1920x1080x32 at 32M: no line '$FB_WANT' in $LOG"
fi
fb_kept "$LOG" "1920x1080x32 at 32M"

# ---- 1.2 stage 6: the framebuffer console ----

section "[1.2] The framebuffer console: its screens against the reference renderer (tests/qmp_fbcon.py)"
# Test ISOs with one entry, no "textmode" line and "cmdline: test=fbcon":
# the kernel shows the boot screen, then a chart of the 256 characters, the
# 16 x 16 colour pairs and the cursor, each after "display: frame N" and
# held still for 3 s. tests/qmp_fbcon.py stops the guest at each marker,
# takes a screendump, saves the kernel's cell grid (vga_shadow, read at its
# physical address) and compares the screen with tests/fbcon_ref.py's
# rendering of that grid, made from the BDF files themselves (never the
# generated C). The expected layout of each mode (decision 19) is written
# out below, not computed.
QMPFBCON=$(dirname "$0")/qmp_fbcon.py
FONTDIR=$(dirname "$0")/../third_party/terminus
SHADOW_V=$(nm "$OUT/madire.elf" | awk '$3 == "vga_shadow" { print $1 }')
SHADOW_P=$(printf '0x%x' $(( 0x${SHADOW_V:-0} - 0xffffffff80000000 )))
echo "      vga_shadow at 0x${SHADOW_V:-?} (physical $SHADOW_P)"
# fbcon_boot <log> <iso> <memory> <markers...>: boots <iso> paused, runs
# qmp_fbcon.py with the markers, then stops QEMU. FBC_OUT holds its output
# (also kept in <log>.fbcon.txt).
fbcon_boot() {
    local log=$1 iso=$2 mem=$3 sock=$OUT/qmp-fbcon.sock t0 qpid
    shift 3
    rm -f "$log" "$sock"
    t0=$(date +%s%N)
    timeout 120 qemu-system-x86_64 -S -display none -no-reboot -serial file:"$log" \
        -qmp unix:"$sock",server=on,wait=off -m "$mem" -cdrom "$iso" >/dev/null 2>&1 &
    qpid=$!
    FBC_OUT=$(python3 "$QMPFBCON" "$sock" "$log" "$SHADOW_P" "$FONTDIR" "$@" --until "${FBC_UNTIL:-System ready.}" \
              --timeout 100 2>&1)
    sed 's/^/      /' <<< "$FBC_OUT"
    echo "$FBC_OUT" > "${log%.log}.fbcon.txt"
    sleep 0.5
    kill "$qpid" 2>/dev/null
    wait "$qpid" 2>/dev/null
    [ -f "$log" ] || : > "$log"
    boot_seconds "$log" "$t0"
}
# fbcon_iso <name> <cmdline> [a limine.conf line]: the test ISO
# madire-os-fbcon-<name>.iso; FBC_ISO is its path, empty if it failed.
fbcon_iso() {
    local dir=$OUT/isofbcon-$1
    FBC_ISO=$OUT/madire-os-fbcon-$1.iso
    limine_tree "$dir"
    {
        printf 'timeout: 0\n\n/Madire OS (framebuffer console test)\n    protocol: multiboot1\n'
        [ -z "${3:-}" ] || printf '    %s\n' "$3"
        printf '    path: boot():/boot/madire.kernel\n'
        [ -z "$2" ] || printf '    cmdline: %s\n' "$2"
    } > "$dir/boot/limine/limine.conf"
    sed 's/^/      limine.conf: /' "$dir/boot/limine/limine.conf"
    limine_iso "$dir" "$FBC_ISO" || { echo "      the test ISO could not be made"; FBC_ISO=""; }
}
# fbcon_case <name> <mode WxHxD> <cell WxH> <x> <y> <percent> <label> [a limine.conf line]
#            [more qmp_fbcon.py steps]
fbcon_case() {
    local name=$1 mode=$2 cell=$3 x=$4 y=$5 pct=$6 label=$7 extra=${8:-} log=$OUT/serial-fbcon-$1.log w h
    shift $(( $# < 8 ? $# : 8 ))
    w=${mode%%x*}; h=${mode#*x}; h=${h%%x*}
    FBC_W=$w FBC_H=$h FBC_LABEL=$label FBC_LOG=$log
    fbcon_iso "$name" "test=fbcon" "$extra"
    FBC_OUT=""
    if [ -n "$FBC_ISO" ]; then fbcon_boot "$log" "$FBC_ISO" 128M "display: frame 1" "display: frame 2" "$@"
    else : > "$log"; fi
    grep -E "^Display: |^fbcon |^display: frame " "$log" | sed 's/^/      /'
    grep -E "^fbcon timing: " "$log" | sed "s/^/      FBCON_TIMING $mode: /"
    expect "$log" "^Display: framebuffer $mode at 0x[0-9a-f]+, pitch [0-9]+, RGB at bits 16/8/0$" \
        "[1.2] $label: Display: framebuffer $mode"
    expect "$log" "^Display: console on the framebuffer: 80x25 cells of $cell pixels at \($x, $y\), scale $pct %$" \
        "[1.2] $label: Display: console on the framebuffer: 80x25 cells of $cell pixels at ($x, $y), scale $pct %"
    if grep -qE "^SCREEN 1 size=${w}x${h} held=yes differ=0 \(" <<< "$FBC_OUT"; then
        pass "[1.2] $label: screen 1 (the boot screen, held still) equals the reference rendering of the cell grid pixel for pixel"
    else
        fail "[1.2] $label: screen 1 differs from the reference rendering, or was not held still ($(grep -E '^(MARKER 1|SCREEN 1|qmp_fbcon)' <<< "$FBC_OUT" | tr '\n' ' '))"
    fi
    if grep -qE "^SCREEN 2 size=${w}x${h} held=yes differ=0 \(.*cursor at row 21, column 14\)$" <<< "$FBC_OUT"; then
        pass "[1.2] $label: screen 2 (the 256 characters, the 16 x 16 colour pairs, the cursor) equals the reference rendering pixel for pixel"
    else
        fail "[1.2] $label: screen 2 differs from the reference rendering, or was not held still ($(grep -E '^(MARKER 2|SCREEN 2|qmp_fbcon)' <<< "$FBC_OUT" | tr '\n' ' '))"
    fi
    if grep -qxF "fbcon test: done: 2 screens held, the boot screen back; 0 draws refused off thread 0" "$log" &&
       grep -q "^System ready\." "$log" && ! grep -q "FAILED" "$log" && ! grep -qE "PANIC|MISMATCH" "$log"; then
        pass "[1.2] $label: test=fbcon done, 0 draws refused off thread 0; System ready, no FAILED status line, no panic, no mismatch"
    else
        fail "[1.2] $label: test=fbcon not done, a draw refused, no System ready, a FAILED status line, or a panic or mismatch (see $log)"
    fi
}
# The 1024x768x32 boot then opens the console and runs dmesg, whose
# ~250 lines scroll it (the console logs "console: scrolled N lines");
# screen 3 is taken 1 s after that line, at the prompt, and must equal the
# reference rendering of the scrolled grid, with the prompt on the last row.
fbcon_case 1024 1024x768x32 8x16 192 184 100 "fbcon at 1024x768x32 (the header's mode)" "" \
    "wait:$FB_READY" "type:{ret}" "wait:console: started" "type:dmesg{ret}" "regex:^console: scrolled [0-9]+ lines$"
SCROLLED=$(sed -nE 's/^console: scrolled ([0-9]+) lines$/\1/p' "$FBC_LOG" | head -n 1)
if [ "${SCROLLED:-0}" -gt 0 ] && grep -qE "^SCREEN 3 size=${FBC_W}x${FBC_H} held=yes differ=0 \(" <<< "$FBC_OUT" &&
   grep -qE "^ROWS 3 top='.*' bottom='madire[^']*>'$" <<< "$FBC_OUT"; then
    pass "[1.2] $FBC_LABEL: screen 3 (the console after dmesg scrolled it $SCROLLED lines, the prompt on the last row) equals the reference rendering pixel for pixel"
else
    fail "[1.2] $FBC_LABEL: screen 3: no scroll logged, the screen differs, or the prompt is not on the last row ($(grep -E '^(MARKER 3|SCREEN 3|ROWS 3|WAIT|qmp_fbcon)' <<< "$FBC_OUT" | tr '\n' ' '))"
fi
fbcon_case 800x24 800x600x24 8x16 80 100 100 "fbcon at 800x600x24" "resolution: 800x600x24"
fbcon_case 1280x1024 1280x1024x32 10x20 240 262 125 "fbcon at 1280x1024x32" "resolution: 1280x1024x32"
fbcon_case 1920 1920x1080x32 12x24 480 240 150 "fbcon at 1920x1080x32" "resolution: 1920x1080x32"
fbcon_case 2560 2560x1440x32 16x32 640 320 200 "fbcon at 2560x1440x32" "resolution: 2560x1440x32"

section "[1.2] The crash screen on the framebuffer (test=gpflimit)"
LOG=$OUT/serial-fbcon-crash.log
fbcon_iso crash "test=gpflimit"
FBC_OUT=""
# 1.2 stage 7: --until the halt line (the marker itself), not "System
# ready.", which a halted kernel never prints (the case took 100.5 s).
if [ -n "$FBC_ISO" ]; then
    FBC_UNTIL="System halted. Power off the machine." fbcon_boot "$LOG" "$FBC_ISO" 128M " System halted. Power off the machine."
else : > "$LOG"; fi
grep -E "^Display: console|CPU exception|System halted" "$LOG" | sed 's/^/      /'
if grep -q "CPU exception 13: General Protection Fault (#GP)" "$LOG" && ! grep -q "FAILED" "$LOG" &&
   grep -qE "^SCREEN 1 size=1024x768 held=yes differ=0 \(" <<< "$FBC_OUT"; then
    pass "[1.2] crash screen on the framebuffer (test=gpflimit): no FAILED status line before it, CPU exception 13 logged, and the screen equals the reference rendering of the cell grid pixel for pixel"
else
    fail "[1.2] crash screen on the framebuffer: a FAILED status line, no CPU exception 13, or the screen differs ($(grep -E '^(MARKER|SCREEN|qmp_fbcon)' <<< "$FBC_OUT" | tr '\n' ' '))"
fi

section "[1.2] Serial twins: a framebuffer boot and its text twin (tests/serial_twin.py)"
# One test ISO entry without a command line, booted to System ready at
# -m 128M twice: with "textmode: yes" (VGA text) and without (the
# framebuffer and its console). The serial logs must be the same apart
# from the allowed differences tests/serial_twin.py names.
fbcon_iso twintext "" "textmode: yes"
TT_ISO=$FBC_ISO
fbcon_iso twinfb ""
TF_ISO=$FBC_ISO
TT_LOG=$OUT/serial-twin-text.log TF_LOG=$OUT/serial-twin-fb.log
if [ -n "$TT_ISO" ]; then boot_until "$TT_LOG" 60 "$FB_READY" -m 128M -cdrom "$TT_ISO"; else : > "$TT_LOG"; fi
if [ -n "$TF_ISO" ]; then boot_until "$TF_LOG" 60 "$FB_READY" -m 128M -cdrom "$TF_ISO"; else : > "$TF_LOG"; fi
grep -hE "^Display: " "$TT_LOG" "$TF_LOG" | sed 's/^/      /'
TWIN=$(python3 "$(dirname "$0")/serial_twin.py" "$TT_LOG" "$TF_LOG")
sed 's/^/      /' <<< "$TWIN"
if grep -qxF "$FB_READY" "$TT_LOG" && grep -qxF "$FB_READY" "$TF_LOG" &&
   ! grep -q "FAILED" "$TT_LOG" "$TF_LOG" && ! grep -qE "PANIC|MISMATCH" "$TT_LOG" "$TF_LOG" &&
   grep -qE "^Display: VGA text 80x25 \(the boot loader's text mode, 80x25 at 0xb8000\)$" "$TT_LOG" &&
   grep -qE "^Display: console on the framebuffer: 80x25 cells of 8x16 pixels at \(192, 184\), scale 100 %$" "$TF_LOG"; then
    pass "[1.2] serial twins: both reach System ready with no FAILED status line and no panic; the text twin on VGA text, the other with its console on the 1024x768 framebuffer"
else
    fail "[1.2] serial twins: no System ready, a FAILED status line, a panic, or not one VGA text and one framebuffer console boot (see $TT_LOG, $TF_LOG)"
fi
if grep -qE "^TWIN_RESULT lines=[1-9][0-9]* .* other=0 MATCH$" <<< "$TWIN"; then
    pass "[1.2] serial twins: the framebuffer boot's serial log equals the text boot's apart from the allowed differences (fb-only, frames, display, figures; tests/serial_twin.py)"
else
    fail "[1.2] serial twins: $(grep '^TWIN_RESULT' <<< "$TWIN")"
fi
# 1.3 stage 8: no command line is the default path: the framebuffer twin
# shows the desktop (its Desktop: lines are framebuffer-only for
# tests/serial_twin.py); the text twin, with no splash, does not.
if grep -qE "^Desktop: shown on 1024x768x32 \(scale 100 %\): .* \(the default path, 1\.3 stage 8\)$" "$TF_LOG" &&
   ! grep -q "^Desktop" "$TT_LOG"; then
    pass "[1.3] serial twins: the framebuffer twin (no command line) shows the desktop on the default path; the text twin has no Desktop: line"
else
    fail "[1.3] serial twins: no default-path Desktop: shown line in the framebuffer twin, or a Desktop: line in the text twin (see $TF_LOG, $TT_LOG)"
fi
# Stage 5's Display: check of a textmode: yes entry and stage 5b's two
# checks of a text boot without slot 506 (stage 10b: on the text twin, a
# test ISO entry with "textmode: yes" as every Diagnostics entry has;
# until 10a they read the real ISO's normal boot, then in text mode). The
# exact Display: line is checked here: tests/serial_twin.py lets it differ.
# The text boot has no PML4[506] entry: the kernel half is frozen with the
# same six slots as before 1.2, no "Display: slot 506" line is logged, and
# tests/pagetables.py, which models slot 506 only when that line is
# logged, agrees with the kernel's count.
expect "$TT_LOG" "^Display: VGA text 80x25 \(the boot loader's text mode, 80x25 at 0xb8000\)$" \
    "[1.2] the text twin (textmode: yes): Display: VGA text 80x25 (the boot loader's text mode, 80x25 at 0xb8000)"
LOG=$TT_LOG
if grep -qxF "Kernel half frozen: PML4 slots 256, 507, 508, 509, 510, 511 (6 entries); every process shares them, a new one now panics" "$LOG" &&
   ! grep -q "^Display: slot 506" "$LOG"; then
    pass "[1.2] text boot (the text twin, textmode: yes): no PML4[506]: the kernel half frozen with slots 256, 507, 508, 509, 510, 511 (6 entries) as before 1.2, no Display: slot 506 line"
else
    fail "[1.2] text boot (the text twin): a slot 506 line, or the kernel half frozen with other slots, in $LOG"
fi
FB_PT=$(python3 "$(dirname "$0")/pagetables.py" "$LOG" --label text)
sed 's/^/      /' <<< "$FB_PT"
if grep -qE "^PAGETABLES_RESULT label=text computed=([0-9]+) logged=\1 .* display_slot=none MATCH$" <<< "$FB_PT"; then
    pass "[1.2] text boot (the text twin): tests/pagetables.py computed = logged, slot 506 not modelled"
else
    fail "[1.2] text boot (the text twin): tests/pagetables.py: $(grep '^PAGETABLES_RESULT' <<< "$FB_PT")"
fi

# ---- 1.2 stage 7: the PS/2 mouse and the pointer ----

# The helpers (mouse_boot, mouse_done_ok, expect_line) are in tests/qemu-lib.sh.

section "[1.2] The mouse: test=mouse under QEMU -kernel (no framebuffer: no pointer)"
LOG=$OUT/serial-mouse-kernel.log
MB_OUT=""
mouse_boot "$LOG" 120 -kernel "$KERNEL" -append "test=mouse" -- "wait:$MOUSE_READY" \
    move:10,5 move:-30,-20 move:127,-127 move:-127,127 btn:left:down btn:left:up btn:right:down \
    btn:middle:down btn:right:up btn:middle:up move:3,0 --until "System ready."
grep -E "^IRQ mask: |^Mouse|^Display: " "$LOG" | sed 's/^/      /'
expect_line "$LOG" "$MOUSE_START" "[1.2] -kernel test=mouse: $MOUSE_START"
expect_line "$LOG" "$MOUSE_MASK" "[1.2] -kernel test=mouse: $MOUSE_MASK"
if grep -qxF "PACKETS sent=11 exact=11" <<< "$MB_OUT"; then
    pass "[1.2] -kernel test=mouse: the 11 packets injected (moves both ways up to 127, each button pressed and released) decoded exactly, in order"
else
    fail "[1.2] -kernel test=mouse: not every injected packet decoded exactly ($(grep -E '^(PACKETS|MISMATCH|WAIT|qmp_mouse)' <<< "$MB_OUT" | head -n 3 | tr '\n' ' '))"
fi
if mouse_done_ok "$LOG" 11 &&
   grep -qxF "Mouse test: motion dx -17 dy -15; pressed L 1 M 1 R 1, released L 1 M 1 R 1" "$LOG"; then
    pass "[1.2] -kernel test=mouse: IRQ12 delivered every byte (33 read, one interrupt each), no bad byte, overflow or drop; the motion and button totals as injected"
else
    fail "[1.2] -kernel test=mouse: the counts or the totals are not as injected (see $LOG)"
fi
if grep -q "^System ready\." "$LOG" && ! grep -q "FAILED" "$LOG" && ! grep -qE "PANIC|MISMATCH" "$LOG"; then
    pass "[1.2] -kernel test=mouse: System ready, no FAILED status line, no panic"
else
    fail "[1.2] -kernel test=mouse: no System ready, a FAILED status line, or a panic (see $LOG)"
fi

section "[1.2] The mouse on the framebuffer console: a mouselog session at 1024x768x32"
# The idle loop's path (the boot option mouselog, no test=): the console is
# opened, then 30 packets: a first move (screen 1), moves both ways and
# every button, 5 past the top-left corner (screen 2 at (0, 0)), 10 past
# the bottom-right one (screen 3 at (1023, 767), the sprite clipped to one
# pixel), and 5 back near the centre (screen 4). Each screen must equal
# the reference: the console's cells with the pointer at the logged place
# and nowhere else, so every earlier place shows the console again exactly
# (save-under). 1.2 stage 10: with the test option pointer (the console
# draws none without it; the next section). 1.3 stage 8: with `verbose`
# too, which keeps 1.2's full-screen console (no splash, so no desktop:
# kernel/display/desktop.h) and changes nothing else this session reads.
fbcon_iso mouse1024 "mouselog pointer verbose"
LOG=$OUT/serial-mouse-1024.log
MB_OUT=""
if [ -n "$FBC_ISO" ]; then
    mouse_boot "$LOG" 150 -cdrom "$FBC_ISO" -- --screen 1024x768 --shadow "$SHADOW_P" --fontdir "$FONTDIR" \
        "wait:$FB_READY" "type:{ret}" "wait:console: started" move:10,5 screen \
        move:-30,-20 move:127,-127 move:-127,127 btn:left:down btn:left:up btn:right:down btn:middle:down \
        btn:right:up btn:middle:up move:-120,-100 move:-120,-100 move:-120,-100 move:-120,-100 move:-120,-100 screen \
        move:120,100 move:120,100 move:120,100 move:120,100 move:120,100 move:120,100 move:120,100 \
        move:120,100 move:120,100 move:120,100 screen \
        move:-102,-77 move:-102,-77 move:-102,-77 move:-102,-77 move:-102,-77 screen
else : > "$LOG"; fi
grep -E "^Mouse|^Display: console" "$LOG" | sed 's/^/      /'
expect_line "$LOG" "$MOUSE_START" "[1.2] mouse at 1024x768: $MOUSE_START"
expect_line "$LOG" "$MOUSE_MASK" "[1.2] mouse at 1024x768: $MOUSE_MASK"
expect "$LOG" "^Mouse: pointer 12x18 pixels \(scale 100 %\), hidden until the first packet, then at \(512, 384\) moved by it$" \
    "[1.2] mouse at 1024x768: Mouse: pointer 12x18 pixels (scale 100 %), hidden until the first packet, then at (512, 384) moved by it"
if grep -qxF "PACKETS sent=30 exact=30" <<< "$MB_OUT" &&
   grep -qE "^mouse: packet 15: .* at \(0, 0\)$" "$LOG" && grep -qE "^mouse: packet 25: .* at \(1023, 767\)$" "$LOG"; then
    pass "[1.2] mouse at 1024x768: the 30 packets (moves both ways, every button, past the top-left and the bottom-right edges) decoded exactly, the position clamped to (0, 0) and (1023, 767)"
else
    fail "[1.2] mouse at 1024x768: not every packet decoded exactly with its clamped position ($(grep -E '^(PACKETS|MISMATCH|WAIT|qmp_mouse)' <<< "$MB_OUT" | head -n 3 | tr '\n' ' '))"
fi
mouse_screen() {   # mouse_screen <n> <check text>
    if grep -qE "^SCREEN $1 size=1024x768 differ=0 \(" <<< "$MB_OUT"; then pass "$2"
    else fail "$2 ($(grep -E "^(SCREEN $1|qmp_mouse)" <<< "$MB_OUT" | tr '\n' ' '))"; fi
}
mouse_screen 1 "[1.2] mouse at 1024x768: screen 1, the pointer at (522, 389) on the console, equals the reference rendering pixel for pixel"
mouse_screen 2 "[1.2] mouse at 1024x768: screen 2, the pointer clamped at (0, 0), equals the reference rendering pixel for pixel"
mouse_screen 3 "[1.2] mouse at 1024x768: screen 3, the pointer clamped at (1023, 767) and clipped, equals the reference rendering pixel for pixel"
mouse_screen 4 "[1.2] mouse at 1024x768: screen 4, the pointer moved away to (513, 382), equals the reference rendering pixel for pixel: every earlier place shows the console again (save-under exact)"
if grep -qxF "$FB_READY" "$LOG" && grep -qxF "Mouse: packets logged on serial (boot option 'mouselog' or test=mouse)" "$LOG" &&
   ! grep -q "FAILED" "$LOG" && ! grep -qE "PANIC|MISMATCH" "$LOG"; then
    pass "[1.2] mouse at 1024x768: mouselog on, System ready, no FAILED status line, no panic, no mismatch"
else
    fail "[1.2] mouse at 1024x768: no mouselog line or System ready, a FAILED status line, or a panic or mismatch (see $LOG)"
fi

section "[1.2] The console without a pointer: a mouselog session at 1024x768x32 (stage 10)"
# The boot option mouselog alone: the mouse runs and logs, but the console
# draws no pointer. The console is opened, then 4 packets (moves both ways,
# the left button); each must be logged exactly, without a position, and
# the screen (taken after the idle loop's next uptime line) must equal the
# console's cells alone. 1.3 stage 8: with `verbose` too (as above).
NOPTR_LINE="Mouse: no pointer on the console (the test option 'pointer', or test=mouse, draws one)"
fbcon_iso mousenoptr "mouselog verbose"
LOG=$OUT/serial-mouse-nopointer.log
MB_OUT=""
if [ -n "$FBC_ISO" ]; then
    mouse_boot "$LOG" 150 -cdrom "$FBC_ISO" -- --shadow "$SHADOW_P" --fontdir "$FONTDIR" \
        "wait:$FB_READY" "type:{ret}" "wait:console: started" move:10,5 move:-30,-20 btn:left:down btn:left:up console
else : > "$LOG"; fi
grep -E "^Mouse|^Display: console" "$LOG" | sed 's/^/      /'
expect_line "$LOG" "$NOPTR_LINE" "[1.2] mouse without the pointer option: $NOPTR_LINE"
if grep -qxF "PACKETS sent=4 exact=4" <<< "$MB_OUT" && ! grep -qE "^mouse: packet .* at \(" "$LOG"; then
    pass "[1.2] mouse without the pointer option: the 4 packets (moves both ways, the left button) logged exactly, with no position"
else
    fail "[1.2] mouse without the pointer option: not every packet logged exactly without a position ($(grep -E '^(PACKETS|MISMATCH|WAIT|qmp_mouse)' <<< "$MB_OUT" | head -n 3 | tr '\n' ' '))"
fi
if grep -qE "^SCREEN 1 size=1024x768 differ=0 \(" <<< "$MB_OUT" && ! grep -q "^pointer: drawn" "$LOG"; then
    pass "[1.2] mouse without the pointer option: after the packets the screen equals the reference rendering of the console's cell grid pixel for pixel (no pointer drawn, no pointer: drawn line)"
else
    fail "[1.2] mouse without the pointer option: a pointer drawn, or the screen differs from the cell grid's rendering ($(grep -E '^(SCREEN|qmp_mouse)' <<< "$MB_OUT" | head -n 2 | tr '\n' ' '))"
fi
if grep -qxF "$FB_READY" "$LOG" && ! grep -q "FAILED" "$LOG" && ! grep -qE "PANIC|MISMATCH" "$LOG"; then
    pass "[1.2] mouse without the pointer option: System ready, no FAILED status line, no panic, no mismatch"
else
    fail "[1.2] mouse without the pointer option: no System ready, a FAILED status line, or a panic or mismatch (see $LOG)"
fi

section "[1.2] The pointer's scale and timing: test=mouse at 1024x768x32 and 2560x1440x32"
# mouse_scale <name> <mode WxH> <sprite WxH> <percent> <x> <y> [a limine.conf line]:
# test=mouse on the framebuffer console; one move of (40, 30) from the
# centre, a screen there, the report-only timing line.
mouse_scale() {
    local name=$1 wh=$2 sprite=$3 pct=$4 x=$5 y=$6 extra=${7:-} log=$OUT/serial-mouse-$1.log label="test=mouse at $2"
    fbcon_iso "mouse$name" "test=mouse" "$extra"
    MB_OUT=""
    if [ -n "$FBC_ISO" ]; then
        mouse_boot "$log" 150 -cdrom "$FBC_ISO" -- --screen "$wh" --shadow "$SHADOW_P" --fontdir "$FONTDIR" \
            "wait:$MOUSE_READY" move:40,30 screen --until "System ready."
    else : > "$log"; fi
    grep -E "^Mouse|^Display: console" "$log" | sed 's/^/      /'
    grep -E "^pointer timing: " "$log" | sed "s/^/      POINTER_TIMING $wh: /"
    expect "$log" "^Mouse: pointer $sprite pixels \(scale $pct %\), hidden until the first packet, then at \($(( ${wh%x*} / 2 )), $(( ${wh#*x} / 2 ))\) moved by it$" \
        "[1.2] $label: Mouse: pointer $sprite pixels (scale $pct %), hidden until the first packet, then at ($(( ${wh%x*} / 2 )), $(( ${wh#*x} / 2 ))) moved by it"
    if grep -qE "^SCREEN 1 size=$wh differ=0 \(" <<< "$MB_OUT"; then
        pass "[1.2] $label: the pointer at ($x, $y) on the boot screen equals the reference rendering pixel for pixel"
    else
        fail "[1.2] $label: the screen with the pointer differs from the reference ($(grep -E '^(SCREEN|PACKETS|MISMATCH|WAIT|qmp_mouse)' <<< "$MB_OUT" | head -n 3 | tr '\n' ' '))"
    fi
    if grep -qxF "PACKETS sent=1 exact=1" <<< "$MB_OUT" && mouse_done_ok "$log" 1 &&
       grep -qE "^pointer timing: one move \(${wh}x32, the $sprite sprite at $pct %: .*: [1-9][0-9]* in [0-9]+ ticks, about [0-9]+ us each$" "$log" &&
       grep -q "^System ready\." "$log" && ! grep -q "FAILED" "$log" && ! grep -qE "PANIC|MISMATCH" "$log"; then
        pass "[1.2] $label: the packet decoded exactly, IRQ12 once per byte, the timing line logged; System ready, no FAILED status line, no panic"
    else
        fail "[1.2] $label: the packet, the counts, the timing line, System ready, a FAILED status line or a panic (see $log)"
    fi
}
mouse_scale scale1024 1024x768 12x18 100 552 414
mouse_scale scale2560 2560x1440 24x36 200 1320 750 "resolution: 2560x1440x32"

section "[1.2] The mouse's give-up path: test=mousefail=N under QEMU -kernel"
# What a machine without a PS/2 mouse goes through: start-up step N fails
# as if no reply came (drivers/mouse/mouse_core.h: 4 is set defaults,
# before port 2 is owned; 6 the configuration with IRQ12 on and 7 port 1
# back on, after it).
# The undo must leave the controller and the PIC as before the start, and
# the keyboard must still work: a key session opens the console and types
# "version".
# mousefail_case <N> <the failed step, as the Mouse: none line gives it>
mousefail_case() {
    local log=$OUT/serial-mousefail-$1.log label="test=mousefail=$1"
    local none="Mouse: none ($2); configuration 0x61 restored, port 2 not used; boot continues without a mouse"
    local after
    boot_text "$log" 90 "System ready." "x{wait:1}version{ret}echo end-of-mousefail-test{ret}" 0.05 \
        $'\nend-of-mousefail-test\n' -kernel "$KERNEL" -append "test=mousefail=$1"
    grep -E "^IRQ mask: |^Mouse|^madire> version|^Madire OS v" "$log" | sed 's/^/      /'
    after=$(grep -E '^Mouse: after the undo: ' "$log" | head -n 1)
    expect_line "$log" "$none" "[1.2] $label: $none"
    if grep -qxF "$none" "$log" && grep -qE '^Mouse: after the undo: configuration 0x61, ' <<< "$after"; then
        pass "[1.2] $label: the configuration read back after the undo is 0x61, the logged starting value"
    else
        fail "[1.2] $label: the configuration after the undo is not the starting value 0x61 (${after:-no after-the-undo line})"
    fi
    if grep -qE ', port 2 not owned, ' <<< "$after"; then
        pass "[1.2] $label: port 2 not owned after the undo (IRQ1 reads its bytes again, as before the start)"
    else
        fail "[1.2] $label: port 2 ownership not given back (${after:-no after-the-undo line})"
    fi
    if grep -qE ', IRQ mask 0xfffc \(before the start: 0xfffc\)$' <<< "$after" && grep -qxF "IRQ mask: 0xfffc (expected 0xfffc)" "$log"; then
        pass "[1.2] $label: the IRQ mask after the undo is 0xfffc, as before the start (IRQ12 and IRQ2 masked, IRQ1 unmasked)"
    else
        fail "[1.2] $label: the IRQ mask after the undo is not the one before the start (${after:-no after-the-undo line})"
    fi
    if grep -qxF "Mouse fault test: start-up step $1 failed on purpose (the Mouse: lines above); the keyboard and the console go on" "$log" &&
       grep -q "^System ready\." "$log" && ! grep -q "^Mouse: IRQ12 " "$log" && ! grep -q "FAILED" "$log" && ! grep -qE "PANIC|MISMATCH" "$log"; then
        pass "[1.2] $label: no mouse, System ready, no FAILED status line, no panic"
    else
        fail "[1.2] $label: no fault-test line or System ready, a mouse started, a FAILED status line, or a panic (see $log)"
    fi
    expect_next "$log" "[1.2] $label: the keyboard still works: the console opened and 'version' typed on it answered 'Madire OS v$VERSION (64-bit x86-64, Multiboot 1)'" \
        "^madire> version$" "^Madire OS v$VRE \(64-bit x86-64, Multiboot 1\)$"
}
mousefail_case 4 "set defaults: no reply (test=mousefail=4)"
mousefail_case 6 "the configuration with IRQ12 on: no reply (test=mousefail=6)"
mousefail_case 7 "port 1 back on: no reply (test=mousefail=7)"

# ---- 1.2 stage 8: the splash ----

section "[1.2] The splash: test=splash with the dots frozen (still), against make-wallpaper.py (tests/qmp_splash.py)"
# Test ISOs with one entry, no "textmode" line and "cmdline: test=splash
# still": the splash shows from the hand-off; test=splash then draws the
# dots with 0, 1, 2 and 3 lit, each after "display: frame N" and held still
# for 3 s, and logs the report-only timing lines. tests/qmp_splash.py stops
# the guest at each marker, takes a screendump and compares it with the
# splash scripts/make-wallpaper.py renders (the one reference algorithm,
# rendered before the guest starts: --size). The expected scale of each
# mode (decision 19) is written out below.
QMPSPLASH=$(dirname "$0")/qmp_splash.py
# splash_boot <log> <iso> <WxH to render first, or ""> <steps...>: boots <iso>
# paused at -m 128M, runs qmp_splash.py with the steps, then stops QEMU.
# SPL_OUT holds its output (also kept in <log>.splash.txt); SPL_UNTIL is the
# line it waits for at the end (default "System ready.").
splash_boot() {
    local log=$1 iso=$2 size=$3 sock=$OUT/qmp-splash.sock t0 qpid
    shift 3
    rm -f "$log" "$sock"
    t0=$(date +%s%N)
    timeout 240 qemu-system-x86_64 -S -display none -no-reboot -serial file:"$log" \
        -qmp unix:"$sock",server=on,wait=off -m 128M -cdrom "$iso" >/dev/null 2>&1 &
    qpid=$!
    SPL_OUT=$(python3 "$QMPSPLASH" "$sock" "$log" "$SHADOW_P" "$FONTDIR" "$@" ${size:+--size "$size"} \
              --until "${SPL_UNTIL:-System ready.}" --timeout 180 2>&1)
    sed 's/^/      /' <<< "$SPL_OUT"
    echo "$SPL_OUT" > "${log%.log}.splash.txt"
    sleep 0.5
    kill "$qpid" 2>/dev/null
    wait "$qpid" 2>/dev/null
    [ -f "$log" ] || : > "$log"
    boot_seconds "$log" "$t0"
}
# splash_done <log> <label>: test=splash's last line, its 3 timing lines,
# and the boot's health.
splash_done() {
    if grep -qxF "splash test: done: 4 screens held; the splash still shown" "$1" &&
       [ "$(grep -cE '^splash timing: .*: [1-9][0-9]* in [0-9]+ ticks, about [0-9]+ us each$' "$1")" -eq 3 ] &&
       grep -q "^System ready\." "$1" && ! grep -q "FAILED" "$1" && ! grep -qE "PANIC|MISMATCH" "$1"; then
        pass "[1.2] $2: test=splash done (4 screens held, its 3 timing lines logged); System ready, no FAILED status line, no panic"
    else
        fail "[1.2] $2: test=splash not done, not 3 timing lines, no System ready, a FAILED status line, or a panic (see $1)"
    fi
}
# splash_case <name> <mode WxHxD> <percent> <label> [a limine.conf line]
splash_case() {
    local name=$1 mode=$2 pct=$3 label=$4 extra=${5:-} log=$OUT/serial-splash-$1.log w h n
    w=${mode%%x*}; h=${mode#*x}; h=${h%%x*}
    fbcon_iso "splash$name" "test=splash still" "$extra"
    SPL_OUT=""
    if [ -n "$FBC_ISO" ]; then
        splash_boot "$log" "$FBC_ISO" "${w}x${h}" "screen:dots0:display: frame 1" "screen:dots1:display: frame 2" \
            "screen:dots2:display: frame 3" "screen:dots3:display: frame 4" "screen:ready:$FB_READY"
    else : > "$log"; fi
    grep -E "^Display: |^Splash|^splash test|^display: frame " "$log" | sed 's/^/      /'
    grep -E "^splash timing: " "$log" | sed "s/^/      SPLASH_TIMING $mode: /"
    expect "$log" "^Splash: shown on $mode \(scale $pct %, frame 1\); the dots frozen \(boot option 'still'\)$" \
        "[1.2] $label: Splash: shown on $mode (scale $pct %, frame 1); the dots frozen (boot option 'still')"
    for n in 1 2 3 4; do
        if grep -qE "^SCREEN $n size=${w}x${h} held=yes differ=0 \(" <<< "$SPL_OUT"; then
            pass "[1.2] $label: screen $n (the splash with $((n - 1)) of the 3 dots lit, held still) equals make-wallpaper.py's rendering pixel for pixel"
        else
            fail "[1.2] $label: screen $n differs from make-wallpaper.py's rendering, or was not held still ($(grep -E "^(MARKER $n|SCREEN $n|REFERENCE|qmp_splash)" <<< "$SPL_OUT" | tr '\n' ' '))"
        fi
    done
    # 1.2 stage 10: screen 5, at System ready (drawn before that line).
    if grep -qE "^SCREEN 5 size=${w}x${h} held=yes differ=0 \(" <<< "$SPL_OUT" && grep -qxF "$READY_LINE" "$log"; then
        pass "[1.2] $label: screen 5 (at System ready the ready state: the dots stopped, \"Press any key to open the console\") equals make-wallpaper.py's rendering pixel for pixel"
    else
        fail "[1.2] $label: screen 5 differs from make-wallpaper.py's ready state, or no '$READY_LINE' line ($(grep -E "^(MARKER 5|SCREEN 5|qmp_splash)" <<< "$SPL_OUT" | tr '\n' ' '))"
    fi
    splash_done "$log" "$label"
}
READY_LINE="Splash: ready: the dots stopped, \"Press any key to open the console\" shown"
splash_case 1024 1024x768x32 100 "splash at 1024x768x32 (the header's mode)"
splash_case 1280 1280x1024x32 125 "splash at 1280x1024x32" "resolution: 1280x1024x32"

section "[1.2] The splash's timing at 2560x1440x32 (test=splash still; no screens compared)"
fbcon_iso splash2560 "test=splash still" "resolution: 2560x1440x32"
LOG=$OUT/serial-splash-2560.log
if [ -n "$FBC_ISO" ]; then boot_until "$LOG" 150 "$FB_READY" -m 128M -cdrom "$FBC_ISO"; else : > "$LOG"; fi
grep -E "^Display: |^Splash|^splash test" "$LOG" | sed 's/^/      /'
grep -E "^splash timing: " "$LOG" | sed "s/^/      SPLASH_TIMING 2560x1440x32: /"
expect "$LOG" "^Splash: shown on 2560x1440x32 \(scale 200 %, frame 1\); the dots frozen \(boot option 'still'\)$" \
    "[1.2] splash at 2560x1440x32: Splash: shown on 2560x1440x32 (scale 200 %, frame 1); the dots frozen (boot option 'still')"
splash_done "$LOG" "splash at 2560x1440x32"

section "[1.2] The splash closed by Esc; the next key opens the console (1024x768x32, boot option still)"
# No test= option: the boot a user sees, with "still" (the dots frozen, the
# splash's lines logged). At System ready the splash's ready state (1.2
# stage 10; before it, the splash with none lit); Esc shows the text boot
# screen and opens nothing; then "x" opens the console.
# 1.3 stage 8: the default path now shows the desktop at the idle loop's
# first pass, unless the first key press queued by then is Esc (the
# stage 8 decision D1). So Esc is pressed during the splash: the guest is
# stopped at a line logged after the keyboard and the mouse have started
# and well before System ready, the Esc sent, the guest let go on
# (tests/qmp_splash.py, stopkey: its "ready_before" must be "no"). The
# ready-state check of this section moved to part 2's handover section
# (tests/qemu-lib.sh, desktop_handover_case), where that state is held.
ESC_LINE="Splash: closed (Esc): the text boot screen shown"
ESC_AT_LINE="Mouse: no pointer on the console (the test option 'pointer', or test=mouse, draws one)"
fbcon_iso splashesc "still"
LOG=$OUT/serial-splash-esc.log
SPL_OUT=""
if [ -n "$FBC_ISO" ]; then
    SPL_UNTIL="console: started" splash_boot "$LOG" "$FBC_ISO" 1024x768 "stopkey:{esc}:$ESC_AT_LINE" \
        "screen:console:$ESC_LINE" "type:{wait:1}x"
else : > "$LOG"; fi
grep -E "^Splash|^System ready|^console: started|^Desktop" "$LOG" | sed 's/^/      /'
grep -E "^STOPKEY " <<< "$SPL_OUT" | sed 's/^/      /'
if grep -qE "^SCREEN 1 size=1024x768 held=yes differ=0 \(" <<< "$SPL_OUT" &&
   grep -qE "^ROWS 1 top='.*' bottom='  System ready\. Press any key to open the console\.'$" <<< "$SPL_OUT" &&
   grep -qE "^STOPKEY .* ready_before=no$" <<< "$SPL_OUT" && ! grep -q "^Desktop:" "$LOG"; then
    pass "[1.2] Esc during the splash closes it: $ESC_LINE, no desktop, and the screen equals the reference rendering of the console's cell grid (the boot screen at System ready) pixel for pixel"
else
    fail "[1.2] Esc: not pressed during the splash, no closed line, a Desktop: line, the screen differs from the cell grid's rendering, or the grid is not the boot screen at System ready ($(grep -E '^(STOPKEY|MARKER 1|SCREEN 1|ROWS 1|WAIT|qmp_splash)' <<< "$SPL_OUT" | tr '\n' ' '))"
fi
ESC_AT=$(grep -nxF "$ESC_LINE" "$LOG" | head -n 1 | cut -d: -f1)
CON_AT=$(grep -nxF "console: started" "$LOG" | head -n 1 | cut -d: -f1)
if [ -n "$ESC_AT" ] && [ -n "$CON_AT" ] && [ "$CON_AT" -gt "$ESC_AT" ] && [ "$(grep -cxF "console: started" "$LOG")" -eq 1 ] &&
   ! grep -q "FAILED" "$LOG" && ! grep -qE "PANIC|MISMATCH" "$LOG"; then
    pass "[1.2] Esc opened no console; the next key ('x') opened it (console: started once, after the Esc line); no FAILED status line, no panic"
else
    fail "[1.2] Esc: the console started before or without the next key, or a FAILED status line or a panic (see $LOG)"
fi

section "[1.2] The boot option verbose: the text boot screen, no splash (1024x768x32)"
fbcon_iso splashverbose "verbose"
LOG=$OUT/serial-splash-verbose.log
SPL_OUT=""
if [ -n "$FBC_ISO" ]; then splash_boot "$LOG" "$FBC_ISO" "" "screen:console:$FB_READY"; else : > "$LOG"; fi
if grep -qE "^SCREEN 1 size=1024x768 held=yes differ=0 \(" <<< "$SPL_OUT" &&
   grep -qE "^ROWS 1 top='.*' bottom='  System ready\. Press any key to open the console\.'$" <<< "$SPL_OUT"; then
    pass "[1.2] verbose at 1024x768x32: no splash: at System ready the screen equals the reference rendering of the console's cell grid (the boot screen) pixel for pixel"
else
    fail "[1.2] verbose at 1024x768x32: the screen at System ready is not the boot screen's rendering ($(grep -E '^(MARKER 1|SCREEN 1|ROWS 1|qmp_splash)' <<< "$SPL_OUT" | tr '\n' ' '))"
fi
if grep -qxF "$FB_READY" "$LOG" && ! grep -q "^Splash" "$LOG" && ! grep -q "FAILED" "$LOG" && ! grep -qE "PANIC|MISMATCH" "$LOG"; then
    pass "[1.2] verbose: System ready, no Splash: line, no FAILED status line, no panic"
else
    fail "[1.2] verbose: no System ready, a Splash: line, a FAILED status line, or a panic (see $LOG)"
fi
# 1.3 stage 8: no test= option, but no splash: no desktop either (D2).
if grep -qxF "$FB_READY" "$LOG" && grep -qE "^SCREEN 1 size=1024x768 held=yes differ=0 \(" <<< "$SPL_OUT" &&
   ! grep -q "^Desktop" "$LOG"; then
    pass "[1.3] verbose: no desktop (the default path needs the splash): at System ready 1.2's boot screen, no Desktop: line"
else
    fail "[1.3] verbose: a Desktop: line, or not 1.2's boot screen at System ready (see $LOG)"
fi

section "[1.2] A FAILED status line closes the splash (test=splashfail, 1024x768x32)"
# test=splashfail reports the Console & shell line FAILED on purpose: the
# splash must close there, and the console is disabled as for a real one.
FAIL_READY="System ready. Console self-test failed: console disabled."
fbcon_iso splashfail "test=splashfail"
LOG=$OUT/serial-splash-fail.log
SPL_OUT=""
if [ -n "$FBC_ISO" ]; then splash_boot "$LOG" "$FBC_ISO" "" "screen:console:$FAIL_READY"; else : > "$LOG"; fi
grep -E "^Splash|^Console & shell|^System ready" "$LOG" | sed 's/^/      /'
CS_AT=$(grep -nE "^Console & shell: +FAILED" "$LOG" | head -n 1 | cut -d: -f1)
CL_AT=$(grep -nxF "Splash: closed (a FAILED status line): the text boot screen shown" "$LOG" | head -n 1 | cut -d: -f1)
if grep -qxF "Splash: shown on 1024x768x32 (scale 100 %, frame 1); the dots move with the boot" "$LOG" &&
   [ -n "$CS_AT" ] && [ -n "$CL_AT" ] && [ "$CL_AT" -gt "$CS_AT" ]; then
    pass "[1.2] test=splashfail: the splash shown, then closed by the FAILED status line: Splash: closed (a FAILED status line): the text boot screen shown"
else
    fail "[1.2] test=splashfail: no splash, or not closed after the Console & shell FAILED line (see $LOG)"
fi
if grep -qE "^SCREEN 1 size=1024x768 held=yes differ=0 \(" <<< "$SPL_OUT" &&
   grep -qE "^ROWS 1 top='.*' bottom='  $FAIL_READY'$" <<< "$SPL_OUT" && ! grep -qE "PANIC|MISMATCH" "$LOG"; then
    pass "[1.2] test=splashfail: at System ready the screen equals the reference rendering of the console's cell grid (the boot screen with the FAILED line) pixel for pixel; no panic"
else
    fail "[1.2] test=splashfail: the screen at System ready is not the boot screen's rendering, or a panic ($(grep -E '^(MARKER 1|SCREEN 1|ROWS 1|qmp_splash)' <<< "$SPL_OUT" | tr '\n' ' '))"
fi
# 1.3 stage 8: a FAILED status line keeps 1.2's path: no desktop, and no
# Desktop: line (its refusals are logged on test=desktop boots only, D2).
if grep -qxF "$FAIL_READY" "$LOG" && ! grep -q "^Desktop" "$LOG"; then
    pass "[1.3] test=splashfail: a FAILED status line keeps 1.2's path: no desktop, no Desktop: line"
else
    fail "[1.3] test=splashfail: a Desktop: line, or no '$FAIL_READY' line (see $LOG)"
fi

section "[1.2] A panic while the splash shows: the crash screen (test=splashpanic, 1024x768x32)"
fbcon_iso splashpanic "test=splashpanic"
LOG=$OUT/serial-splash-panic.log
SPL_OUT=""
if [ -n "$FBC_ISO" ]; then
    SPL_UNTIL="System halted. Power off the machine." splash_boot "$LOG" "$FBC_ISO" "" \
        "screen:console: System halted. Power off the machine."
else : > "$LOG"; fi
grep -E "^Splash|KERNEL PANIC|Splash test:|System halted" "$LOG" | sed 's/^/      /'
if grep -qxF "Splash: shown on 1024x768x32 (scale 100 %, frame 1); the dots move with the boot" "$LOG" &&
   grep -qxF " Splash test: a panic while the splash is shown (on purpose)." "$LOG"; then
    pass "[1.2] test=splashpanic: the splash shown, then the panic: Splash test: a panic while the splash is shown (on purpose)."
else
    fail "[1.2] test=splashpanic: no splash, or no panic line (see $LOG)"
fi
if grep -qE "^SCREEN 1 size=1024x768 held=yes differ=0 \(" <<< "$SPL_OUT" &&
   grep -qE "^ROWS 1 top='' bottom='.*'$" <<< "$SPL_OUT"; then
    pass "[1.2] test=splashpanic: the crash screen equals the reference rendering of the console's cell grid pixel for pixel (the console took the screen back from the splash)"
else
    fail "[1.2] test=splashpanic: the crash screen differs from the cell grid's rendering ($(grep -E '^(MARKER 1|SCREEN 1|ROWS 1|qmp_splash)' <<< "$SPL_OUT" | tr '\n' ' '))"
fi

section "[1.2] The early crash screen: a panic before the framebuffer console (test=earlypanic, 1024x768x32; stage 10)"
# test=earlypanic panics just before memory_init(): still on the boot page
# tables, before slot 506 and the console. The crash screen must be drawn
# directly on the framebuffer (option A, decided at stage 8) and equal the
# reference rendering of the console's cell grid, the panic message on it.
EP_LINE=" Early panic test: a panic before the framebuffer console (on purpose)."
EP_DIRECT="Display: the crash screen drawn directly on the framebuffer (the console was not on it yet)"
fbcon_iso earlypanic "test=earlypanic"
LOG=$OUT/serial-early-panic.log
SPL_OUT=""
if [ -n "$FBC_ISO" ]; then
    SPL_UNTIL="System halted. Power off the machine." splash_boot "$LOG" "$FBC_ISO" "" \
        "screen:console: System halted. Power off the machine." --rows
else : > "$LOG"; fi
grep -E "^Display|^Early panic|KERNEL PANIC|Early panic test:|System halted" "$LOG" | sed 's/^/      /'
if grep -qE "^Display: framebuffer 1024x768x32 " "$LOG" && grep -qxF "$EP_DIRECT" "$LOG" && grep -qxF "$EP_LINE" "$LOG" &&
   ! grep -q "^Display: console on the framebuffer" "$LOG" && ! grep -q "^Memory" "$LOG"; then
    pass "[1.2] test=earlypanic: the panic before memory, paging and the console, on a 1024x768x32 framebuffer: $EP_DIRECT"
else
    fail "[1.2] test=earlypanic: no framebuffer, no early panic, the console already on the framebuffer, or no '$EP_DIRECT' line (see $LOG)"
fi
if grep -qE "^SCREEN 1 size=1024x768 held=yes differ=0 \(" <<< "$SPL_OUT" &&
   grep -qxF "ROW 1 3 '$EP_LINE'" <<< "$SPL_OUT"; then
    pass "[1.2] test=earlypanic: the crash screen equals the reference rendering of the console's cell grid pixel for pixel, the panic message on row 3"
else
    fail "[1.2] test=earlypanic: the crash screen differs from the cell grid's rendering, or the message is not on row 3 ($(grep -E '^(MARKER 1|SCREEN 1|ROW 1 3|qmp_splash)' <<< "$SPL_OUT" | tr '\n' ' '))"
fi