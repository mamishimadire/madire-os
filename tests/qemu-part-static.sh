# Madire OS QEMU tests, part static (0.12, stage 0c): the checks that boot
# nothing (since 1.3 stage 8a, one section at the end boots: the hidden
# boot menu's, moved unchanged from part 4 for the shards' balance):
# Multiboot header, [0.8] reproducible build (needs make repro),
# [0.10] version read once. Sourced by tests/qemu-tests.sh (a full run, or
# --shard static). Apart from this header, every line was moved unchanged
# from the single script. 0.13 stage 3a: the Multiboot header check uses
# scripts/check-multiboot.py, the default-entry title comes from limine.conf,
# and the [0.13] checks of the Limine ISO are added at the end. 1.0 stage 0a:
# the [1.0] check of Madire's licence files at the ISO root. 1.0 stage 1:
# the 0.13 report's comparison with v0.12's kernel (decided no check) removed.
# 1.1 stage 1: the default entry's title is "Boot Madire OS" (C1), and the
# [1.1] checks of the boot menu's global options and its wallpaper (S1-S3)
# are added at the end. 1.2 stage 2: the size report (no check) at the end;
# stage 3 labels its lines shipped or test-only. 1.2 stage 6: the [1.2]
# checks of the console font (pinned files, regeneration, the ISO's
# notices, the name) before the size report. 1.2 stage 8: after them, the
# integer make-wallpaper.py against v1.1's wallpaper, the splash's
# generated data, and the kernel's splash code (tests/splashdump.c) against
# make-wallpaper.py in five modes. 1.2 stage 10: the splash's ready state
# in two more. 1.3 stage 2: before the size report, the [1.3] checks of
# the desktop toolkit (tokens.h against the Design Guide, the toolkit's
# objects compiled with the kernel's flags, tests/desktopdump.c against
# tests/desktop_ref.py in five modes). 1.3 stage 3b: after them, the
# desktop's background and taskbar against desktop_ref.py in five modes.
# 1.3 stage 4a: the windows and the contrast check; stage 4b: the Console
# window's scenes and their contrast check. 1.3 stage 7a: before the size
# report, the self-test of boot_until's stop with a fake QEMU. 1.3 stage 8:
# before the moved boot-menu section, the handover's rule on the references
# and the VirtualBox desktop group's plan and references. 1.4 stage 2: at
# the end, the test-image tools' probe and self-checks (tests/disk-lib.sh).
# 1.4 stage 3: after them, the PCI scan's three -kernel boots (tests/qmp_pci.py).
# 1.4 stage 4: at the end, the AHCI driver's read-only checks (source and
# madire.elf) and its test=diskread boot. 1.4 stage 6: at the end, the ext2
# read core's checks (tests/ext2dump.c against debugfs, the feature policy,
# the sanitizers over corrupted images); no boot.
# 1.5 stage 2: the boot_until self-test (1.3 stage 7a), the handover's
# references (1.3 stage 8) and the hidden boot menu's section moved,
# unchanged, to part 5 (tests/qemu-part-5.sh; the shards' balance).

section "Multiboot header"
# 0.13: scripts/check-multiboot.py (Madire's own, at least as strict as
# grub-file --is-x86-multiboot, which it replaced in 0.13).
MBCHECK=$(dirname "$0")/../scripts/check-multiboot.py
python3 "$MBCHECK" "$KERNEL" | sed 's/^/      /'
if python3 "$MBCHECK" "$KERNEL" >/dev/null; then pass "kernel has a valid Multiboot header"
else fail "kernel has no valid Multiboot header"; fi

# ---- 0.8: reproducible build ----

# `make repro` (run by `make test` before this script) built the kernel
# twice from clean, in two trees created in opposite file orders, and
# recorded the sha256 sums in build/repro/sha256.txt.
section "[0.8] Reproducible build (make repro)"
SUMS=$OUT/repro/sha256.txt
cat "$SUMS" 2>/dev/null | sed 's/^/      /'
sum_of() { F="$1" awk '$2 == ENVIRON["F"] { print $1 }' "$SUMS" 2>/dev/null; }
same_sum() {   # same_sum <description> <file> <file>
    local x y
    x=$(sum_of "$2"); y=$(sum_of "$3")
    if [ -n "$x" ] && [ "$x" = "$y" ]; then pass "$1 ($x)"
    else fail "$1 (${x:-missing} vs ${y:-missing})"; fi
}
same_sum "[0.8] madire.elf byte-identical in two clean builds" a/build/madire.elf b/build/madire.elf
same_sum "[0.8] madire.kernel byte-identical in two clean builds" a/build/madire.kernel b/build/madire.kernel
same_sum "[0.8] the kernel under test is the reproducible one" a/build/madire.kernel ../madire.kernel

section "[0.10] Version read once by the test harness"
if [ -n "$VERSION" ]; then pass "[0.10] version read from kernel/kernel.h: $VERSION (every version check uses it)"
else fail "[0.10] no version read from kernel/kernel.h"; fi
# 0.13: the boot menu is limine.conf; its first entry (the first line starting
# with a single "/") is the default one. 1.1: the title no longer carries the
# version; the wallpaper does (checked by S2a).
TITLE=$(grep -m1 -E '^/[^/]' "$OUT/limine.conf")
if [ "$TITLE" = "/Boot Madire OS" ]; then
    pass "[0.10] boot menu: the default entry is titled 'Boot Madire OS'"
else
    fail "[0.10] default boot entry: '$TITLE', expected '/Boot Madire OS'"
fi

# ---- 0.13: the Limine ISO (checks that boot nothing) ----

section "[0.13] scripts/check-multiboot.py: the real kernel, and each refusal"
MBOUT=$(python3 "$MBCHECK" "$KERNEL")
# 1.2 stage 5 (docs/plans/1.2.md, D1): flags 0x00010003 -> 0x00010007, the
# video request (bit 2) added; the mode fields are checked by [1.2] below.
if [ $? -eq 0 ] && grep -qE '^MULTIBOOT OK: header at offset [0-9]+, flags 0x00010007, a\.out kludge: load 0x00100000-0x[0-9a-f]{8}, bss end 0x[0-9a-f]{8}, entry 0x[0-9a-f]{8}$' <<< "$MBOUT"; then
    pass "[0.13] check-multiboot.py accepts the real kernel (flags 0x00010007, a.out kludge, video mode requested, loaded at 1 MiB)"
else
    fail "[0.13] check-multiboot.py on the real kernel: $MBOUT"
fi
mb_refuses() {   # mb_refuses <mutation> <reason regex> <description>
    local out
    out=$(python3 "$MBCHECK" "$KERNEL" --mutate "$1")
    if [ $? -eq 1 ] && grep -qE "^MULTIBOOT BAD: $2" <<< "$out"; then pass "[0.13] check-multiboot.py refuses $3"
    else fail "[0.13] check-multiboot.py with --mutate $1: $out"; fi
}
mb_refuses magic "no Multiboot magic" "a wrong magic"
mb_refuses checksum "checksum 0x[0-9a-f]{8} at offset [0-9]+" "a wrong checksum"
mb_refuses aout-outside "load_end_addr 0x[0-9a-f]{8} is beyond the file" "a.out fields outside the file"

section "[0.13] The ISO: Limine's files, the notices, no file of the previous boot loader"
# The files that stay byte-identical to the pinned v12.9.3 package (0.13
# stage 1: bios-install changes only the ISO's system area; xorriso's
# -boot-info-table changes limine-bios-cd.bin, so it is not compared).
LPIN="limine-bios.sys 67ba6238aaaa34d5e2c6206932b979f6115fd427b8203aca06c88c3a36a5b0ab
LICENSE f174b8ff78f6f8982a1fc992668f8486eb8cf598f6938923c22e9bb50f8338a8
3RDPARTY.md 292659d622cddafe3ddab7506e9c501d4a2f7af395090767d9357203e30029a8"
rm -rf "$OUT/iso-extract" && mkdir -p "$OUT/iso-extract"
xorriso -osirrox on -indev "$ISO" -extract /boot/limine "$OUT/iso-extract" >/dev/null 2>&1
LBAD=""
while read -r f want; do
    got=$(sha256sum < "$OUT/iso-extract/$f" 2>/dev/null | cut -d' ' -f1)
    echo "      /boot/limine/$f: ${got:-missing}"
    [ "$got" = "$want" ] || LBAD="$LBAD $f"
done <<< "$LPIN"
if [ -z "$LBAD" ]; then pass "[0.13] ISO: limine-bios.sys, LICENSE and 3RDPARTY.md match the pinned package's SHA-256"
else fail "[0.13] ISO: differs from the pinned package:$LBAD"; fi
ISOLIST=$(xorriso -indev "$ISO" -find / -type f 2>/dev/null | tr -d "'")
if grep -qx '/boot/limine/LICENSE' <<< "$ISOLIST" && grep -qx '/boot/limine/3RDPARTY.md' <<< "$ISOLIST" &&
   ! grep -qi 'grub' <<< "$ISOLIST"; then
    pass "[0.13] ISO: Limine's LICENSE and 3RDPARTY.md included, no file of the previous boot loader"
else
    fail "[0.13] ISO: notices missing or a file of the previous boot loader present ($(grep -ci grub <<< "$ISOLIST") paths)"
fi

section "[1.0] The ISO: Madire's licence files at its root"
# 1.0 stage 0a (Apache 2.0, section 4): each file extracted from the ISO root
# must have the SHA-256 of the repository's copy.
REPO=$(dirname "$0")/..
rm -rf "$OUT/iso-root" && mkdir -p "$OUT/iso-root"
RBAD=""
for f in LICENSE NOTICE THIRD_PARTY_NOTICES; do
    xorriso -osirrox on -indev "$ISO" -extract "/$f" "$OUT/iso-root/$f" >/dev/null 2>&1
    got=$(sha256sum < "$OUT/iso-root/$f" 2>/dev/null | cut -d' ' -f1)
    want=$(sha256sum < "$REPO/$f" | cut -d' ' -f1)
    echo "      /$f: ${got:-missing} (repository: $want)"
    [ -n "$got" ] && [ "$got" = "$want" ] || RBAD="$RBAD $f"
done
if [ -z "$RBAD" ]; then pass "[1.0] ISO: /LICENSE, /NOTICE and /THIRD_PARTY_NOTICES present and byte-identical to the repository's"
else fail "[1.0] ISO: missing or different from the repository's:$RBAD"; fi

section "[1.5] The ISO built again from the same files (make isorepro)"
# 1.5 stage 1 (plan C9): every staged file and folder gets the modification
# time SOURCE_DATE_EPOCH before xorriso runs, so a second build of the same
# files, at least 2 s later (make isorepro), must be byte-identical (before
# 1.5 two builds differed in 364 ISO 9660 recording times: the build time).
I1=$(sha256sum < "$ISO" | cut -d' ' -f1)
I2=$(sha256sum < "$OUT/isorepro/madire-os.iso" 2>/dev/null | cut -d' ' -f1)
echo "      first build:  $I1"
echo "      second build: ${I2:-missing}"
if [ -n "$I2" ] && [ "$I1" = "$I2" ]; then pass "[1.5] ISO byte-identical when built again from the same files ($I1)"
else fail "[1.5] ISO built again from the same files differs or is missing ($I1 vs ${I2:-missing})"; fi

section "[1.0] docs: every .md file valid UTF-8"
# 1.0 stage 10: two Windows-1252 bytes reached docs/commands.md in stage 8
# (5cd7daf); every Markdown file under docs/ must decode as UTF-8 (the
# images are not text and are left out).
UBAD="" UN=0
while IFS= read -r f; do
    UN=$((UN + 1))
    iconv -f UTF-8 -t UTF-8 "$f" >/dev/null 2>&1 || UBAD="$UBAD ${f#"$REPO"/}"
done < <(find "$REPO/docs" -type f -name '*.md' | LC_ALL=C sort)
echo "      $UN files"
if [ "$UN" -gt 0 ] && [ -z "$UBAD" ]; then pass "[1.0] docs: every .md file under docs/ is valid UTF-8"
else fail "[1.0] docs: not valid UTF-8:${UBAD:- no .md file found}"; fi

section "[0.13] limine.conf: every program module names itself"
# Limine passes only module_string to the kernel, so every program module of
# the default entry needs "module_string: <its file name without .elf>".
MSBAD=$(awk '/^\// { n++ } n == 1 && /^ *module_path: / { if (p != "") print "no string for " p; p = $2; sub(/.*\//, "", p); sub(/\.elf$/, "", p); next }
             n == 1 && /^ *module_string: / { if (p == "" || $2 != p || NF != 2) print "string \"" $2 "\" for \"" p "\""; p = ""; c++ }
             END { if (p != "") print "no string for " p; if (c == 0) print "no modules" }' "$OUT/limine.conf")
if [ -z "$MSBAD" ]; then pass "[0.13] limine.conf: every program module's module_string is its file name ($(grep -c '^ *module_string: ' "$OUT/limine.conf") modules)"
else fail "[0.13] limine.conf: ${MSBAD//$'\n'/; }"; fi

# ---- 1.1: the boot menu hidden behind a key, on Madire's wallpaper ----

section "[1.1] limine.conf: the global options (docs/plans/1.1.md, section E)"
# S1: the lines before the first entry, without comments and blank lines,
# must be exactly this list, and no default_entry line may exist (the first
# entry is the default one).
GWANT="timeout: 2
quiet: yes
mouse: no
interface_resolution: 1024x768
interface_branding:
interface_help_hidden: yes
editor_enabled: no
terse: yes
wallpaper: boot():/boot/limine/wallpaper.png
wallpaper_style: centered
backdrop: 0B1220
term_foreground: E6EDF3
term_background: FF000000
term_margin: 240
term_margin_gradient: 0"
GGOT=$(awk '/^\// { exit } /^#/ || /^[ \t]*$/ { next } { print }' "$OUT/limine.conf")
GDEF=$(grep -c '^[ \t]*default_entry:' "$OUT/limine.conf")
echo "      $(printf '%s\n' "$GGOT" | grep -c .) global lines, $GDEF default_entry line(s)"
if [ "$GGOT" = "$GWANT" ] && [ "$GDEF" -eq 0 ]; then
    pass "[1.1] boot menu: hidden behind a key (quiet, 2 s, no default_entry, mouse and editor off, terse, no branding or help, 1024x768, term_margin 240)"
else
    diff <(printf '%s\n' "$GWANT") <(printf '%s\n' "$GGOT") | sed 's/^/      /'
    fail "[1.1] boot menu: the global options differ from the plan's list, or $GDEF default_entry line(s)"
fi

section "[1.1] The ISO: the boot menu's wallpaper"
# S2a: the ISO's file (extracted with /boot/limine above) must be the bytes
# that scripts/make-wallpaper.py makes again here with the harness's version
# (read once from kernel/kernel.h): the proof that the version shown is the
# current one. Both are made by the same Python and zlib (one job).
WPGEN=$(dirname "$0")/../scripts/make-wallpaper.py
WPISO=$OUT/iso-extract/wallpaper.png
WPNEW=$OUT/wallpaper-regenerated.png
rm -f "$WPNEW"
python3 "$WPGEN" "$WPNEW" --version "$VERSION" 2>&1 | sed 's/^/      /'
echo "      ISO: $(sha256sum < "$WPISO" 2>/dev/null | cut -d' ' -f1) (regenerated: $(sha256sum < "$WPNEW" 2>/dev/null | cut -d' ' -f1))"
if [ -s "$WPISO" ] && [ -s "$WPNEW" ] && cmp -s "$WPISO" "$WPNEW"; then
    pass "[1.1] ISO: /boot/limine/wallpaper.png is byte-identical to make-wallpaper.py run again with v$VERSION"
else
    fail "[1.1] ISO: /boot/limine/wallpaper.png missing or different from make-wallpaper.py run again with v$VERSION"
fi
# S2b: the size gate and the readable version field (tEXt "Madire-Version");
# not the proof (S2a is). Prints "PNG <width>x<height> <bytes> <version>".
WPINFO=$(python3 - "$WPISO" <<'EOF' 2>&1
import struct, sys
d = open(sys.argv[1], "rb").read()
if d[:8] != b"\x89PNG\r\n\x1a\n":
    sys.exit("not a PNG")
i, kinds, w, h, ver = 8, [], 0, 0, "none"
while i + 8 <= len(d):
    n, kind = struct.unpack(">I", d[i:i + 4])[0], d[i + 4:i + 8]
    body = d[i + 8:i + 8 + n]
    kinds.append(kind)
    if kind == b"IHDR":
        w, h = struct.unpack(">II", body[:8])
    elif kind == b"tEXt" and body.startswith(b"Madire-Version\x00"):
        ver = body[15:].decode("ascii", "replace")
    i += 12 + n
if not kinds or kinds[0] != b"IHDR" or kinds[-1] != b"IEND" or i != len(d):
    sys.exit("malformed chunks")
print(f"PNG {w}x{h} {len(d)} {ver}")
EOF
)
echo "      $WPINFO"
read -r WPKIND WPDIM WPBYTES WPVER <<< "$WPINFO"
if [ "$WPKIND" = PNG ] && [ "$WPDIM" = 1024x768 ] && [ "$WPBYTES" -le 262144 ] 2>/dev/null && [ "$WPVER" = "$VERSION" ]; then
    pass "[1.1] ISO: wallpaper.png is a 1024x768 PNG of at most 256 KiB, its Madire-Version text is $VERSION"
else
    fail "[1.1] ISO: wallpaper.png: $WPINFO (expected a 1024x768 PNG of at most 262144 bytes, Madire-Version $VERSION)"
fi

section "[1.1] make-wallpaper.py: the M of kernel/logo.c"
# S3: the stroke table of kernel/logo.c (x1, y1, x2, y2 per stroke, in its
# order) must equal the one the wallpaper draws (--print-strokes).
SLOGO=$(awk '/strokes\[\] = \{/ { on = 1; next } on && /^\};/ { exit }
             on { sub(/\/\*.*/, ""); gsub(/[{},]/, " "); $1 = $1; if (NF == 4) print }' "$(dirname "$0")/../kernel/logo.c")
SWP=$(python3 "$WPGEN" --print-strokes 2>&1)
echo "      kernel/logo.c: ${SLOGO//$'\n'/; }"
echo "      make-wallpaper.py: ${SWP//$'\n'/; }"
if [ "$(printf '%s\n' "$SLOGO" | grep -c .)" -eq 4 ] && [ "$SLOGO" = "$SWP" ]; then
    pass "[1.1] make-wallpaper.py: the M's four strokes equal kernel/logo.c's"
else
    fail "[1.1] make-wallpaper.py: the M's strokes differ from kernel/logo.c's"
fi

# ---- 1.2: the video request, and text mode until the switch stage ----

section "[1.2] The Multiboot header's video request (stage 5)"
# The mode fields after the a.out kludge's (header offsets 32-44): a linear
# framebuffer (mode_type 0) of 1024x768x32 (docs/plans/1.2.md, decision 2).
MBMODE=$(python3 - "$KERNEL" <<'EOF' 2>&1
import struct, sys
d = open(sys.argv[1], "rb").read()
for off in range(0, min(len(d), 8192) - 48 + 1, 4):
    magic, flags, checksum = struct.unpack_from("<III", d, off)
    if magic == 0x1BADB002 and (magic + flags + checksum) & 0xFFFFFFFF == 0:
        print("MODE flags 0x%08x mode_type %d width %d height %d depth %d" % ((flags,) + struct.unpack_from("<IIII", d, off + 32)))
        break
else:
    print("MODE no header")
EOF
)
echo "      $MBMODE"
if [ "$MBMODE" = "MODE flags 0x00010007 mode_type 0 width 1024 height 768 depth 32" ]; then
    pass "[1.2] the Multiboot header asks for a 1024x768x32 linear framebuffer (flag bit 2; mode_type 0, width 1024, height 768, depth 32)"
else
    fail "[1.2] the Multiboot header's video request: $MBMODE"
fi

section "[1.2] limine.conf: the default entry in graphics, every other entry in text mode (stage 10b, decision 3)"
# Since the switch (stage 10b): the first entry (the default one, "/Boot
# Madire OS") holds no textmode line; every other entry (a line starting
# "/" with a "protocol:" line: the Diagnostics) holds exactly one
# "    textmode: yes" and no other textmode line; no textmode line outside
# an entry. (Stage 5's check: every entry exactly one "textmode: yes".)
TM=$(awk 'function done() { if (blk == "") return
                            if (proto && ent == 0) { ent++; def = blk; if (tm || other) bad = bad blk " (the default entry: " tm " textmode: yes, " other " other); " }
                            else if (proto) { ent++; if (tm != 1 || other) bad = bad blk " (" tm " textmode: yes, " other " other); " }
                            else if (tm || other) bad = bad blk " (a textmode line, no protocol); " }
          /^\// { done(); blk = $0; proto = 0; tm = 0; other = 0; next }
          /^[ \t]*textmode:/ { if (blk == "") bad = bad "a textmode line before the first entry; "
                               else if ($0 == "    textmode: yes") tm++; else other++; next }
          /^[ \t]*protocol:/ { proto = 1 }
          END { done(); printf "%d|%s|%s\n", ent, def, bad }' "$OUT/limine.conf")
TMN=${TM%%|*}
TMDEF=${TM#*|}
TMDEF=${TMDEF%%|*}
TMBAD=${TM##*|}
echo "      $TMN entries; the default entry: ${TMDEF:-none}"
if [ -z "$TMBAD" ] && [ "$TMDEF" = "/Boot Madire OS" ] && [ "$TMN" -gt 1 ]; then
    pass "[1.2] limine.conf: the default entry boots in graphics (no textmode line) and every other entry in text mode (one 'textmode: yes' in each of the other $((TMN - 1)) entries)"
else
    fail "[1.2] limine.conf: text mode: ${TMBAD:-the default entry '${TMDEF:-none}', $TMN entries}"
fi

# ---- 1.2 stage 6: the console font (decision 8) ----

section "[1.2] third_party/terminus: the pinned font files (stage 6)"
# The hashes are written here (as the Limine package's are above), not read
# from the repository's SHA256SUMS: the BDF files are the release package's
# (terminus-font-4.49.1.tar.gz, SHA-256 d961c1b7...ef79), OFL.txt its
# OFL.TXT with LF line endings.
TPIN="ter-u16n.bdf 5197662b22bf9f3e68d4af9f969a7fefa3edae40dd82ae969a147381130fb4ae
ter-u16b.bdf 92861fecd6f94c5ecc764fc43df5a69eb000ae13a75f652c6525db0495274f0a
ter-u20n.bdf b3868699145f51f1c059b955110c35db990d10a07536995cbcf7a4a901f82178
ter-u20b.bdf 3051598391e5f7f59f09dc5aa26a97700fcbbe57ac315495878c52fc74aba500
ter-u24n.bdf ff640e9e097983355e8f70ed8fb645bd850184a0d590de17f6fc9ceec1bf8eaf
ter-u24b.bdf 759e0dbbd137d0132cee5d83b681cea1c8a656f161e04f83e121525ea4eb1824
ter-u32n.bdf 5ad01972d58adfee75077e2c65ba5947e260043031664cb8ad8f98b5473b23a4
ter-u32b.bdf f6295e92ebbf95afbb6274f4621608d255c1287ec90d3fefbc50f077bf09dab4
OFL.txt 29e51260692fca30c51c3c81c5aa12205cfaa1be7180bf5e7c8192e745e78308"
TBAD=""
while read -r f want; do
    got=$(sha256sum < "$REPO/third_party/terminus/$f" 2>/dev/null | cut -d' ' -f1)
    echo "      third_party/terminus/$f: ${got:-missing}"
    [ "$got" = "$want" ] || TBAD="$TBAD $f"
done <<< "$TPIN"
if [ -z "$TBAD" ]; then pass "[1.2] third_party/terminus: the 8 BDF files and OFL.txt have their pinned SHA-256 (Terminus 4.49.1)"
else fail "[1.2] third_party/terminus: not the pinned file:$TBAD"; fi

section "[1.2] scripts/make-font.py: the font C files regenerate byte-identically (stage 6)"
# The build converted all 8 files (build/font/); converted again here into
# a fresh folder, each must be byte-identical.
rm -rf "$OUT/font-regen" && mkdir -p "$OUT/font-regen"
FBAD="" FN=0
for s in 8x16:16 10x20:20 12x24:24 16x32:32; do
    for w in n b; do
        name=font_${s%%:*}; [ "$w" = b ] && name=${name}_bold
        PYTHONDONTWRITEBYTECODE=1 python3 "$REPO/scripts/make-font.py" "$REPO/third_party/terminus/ter-u${s##*:}$w.bdf" \
            "$OUT/font-regen/$name.c" "$name" --sums "$REPO/third_party/terminus/SHA256SUMS" | sed 's/^/      /'
        FN=$((FN + 1))
        echo "      $name.c: build $(sha256sum < "$OUT/font/$name.c" 2>/dev/null | cut -c1-16), again $(sha256sum < "$OUT/font-regen/$name.c" 2>/dev/null | cut -c1-16)"
        [ -s "$OUT/font-regen/$name.c" ] && cmp -s "$OUT/font/$name.c" "$OUT/font-regen/$name.c" || FBAD="$FBAD $name.c"
    done
done
if [ "$FN" -eq 8 ] && [ -z "$FBAD" ]; then pass "[1.2] scripts/make-font.py regenerates the 8 font C files byte-identically from the pinned BDF files"
else fail "[1.2] font C files not regenerated byte-identically:${FBAD:- none made}"; fi

section "[1.2] The ISO: the font's notices; the font never named in the interface (stage 6)"
rm -rf "$OUT/iso-assets" && mkdir -p "$OUT/iso-assets"
ABAD=""
for f in THIRD_PARTY_ASSETS.md third_party/terminus/OFL.txt; do
    xorriso -osirrox on -indev "$ISO" -extract "/$f" "$OUT/iso-assets/${f##*/}" >/dev/null 2>&1
    got=$(sha256sum < "$OUT/iso-assets/${f##*/}" 2>/dev/null | cut -d' ' -f1)
    want=$(sha256sum < "$REPO/$f" | cut -d' ' -f1)
    echo "      /$f: ${got:-missing} (repository: $want)"
    [ -n "$got" ] && [ "$got" = "$want" ] || ABAD="$ABAD $f"
done
if [ -z "$ABAD" ]; then pass "[1.2] ISO: /THIRD_PARTY_ASSETS.md and /third_party/terminus/OFL.txt present and byte-identical to the repository's"
else fail "[1.2] ISO: missing or different from the repository's:$ABAD"; fi
# OFL: the converted font is a Modified Version and must not use the
# Reserved Font Name, so nothing the kernel can show names the font.
TNAME="madire.kernel $(grep -c 'Terminus' "$OUT/madire.kernel"), limine.conf $(grep -c 'Terminus' "$OUT/limine.conf")"
echo "      lines naming the font: $TNAME"
if [ -s "$OUT/madire.kernel" ] && ! grep -q 'Terminus' "$OUT/madire.kernel" "$OUT/limine.conf"; then
    pass "[1.2] the font is never named in the interface: no 'Terminus' in madire.kernel or limine.conf"
else
    fail "[1.2] 'Terminus' found: $TNAME"
fi

# ---- 1.2 stage 8: the wallpaper's integer maths and the splash ----

section "[1.2] make-wallpaper.py in integer maths: v1.1's boot-menu wallpaper, pixel for pixel (stage 8)"
# The script has no floating point since stage 8. Run with --version 1.1,
# its pixels must be those of the wallpaper.png v1.1's ISO shipped (made by
# the 1.1 floating-point script; run 39836152722, kernel a21b0cd6): the
# SHA-256 of their RGB bytes, row by row, is written out below. The PNG's
# own bytes depend on zlib, so the pixels are compared (S2a compares the
# shipped file's bytes in one job, as before).
WP11=$OUT/wallpaper-v1.1.png
rm -f "$WP11"
python3 "$WPGEN" "$WP11" --version 1.1 2>&1 | sed 's/^/      /'
WP11RGB=$(python3 - "$WP11" <<'EOF' 2>&1
import hashlib, struct, sys, zlib
d = open(sys.argv[1], "rb").read()
i, idat, w, h = 8, b"", 0, 0
while i + 8 <= len(d):
    n, kind = struct.unpack(">I", d[i:i + 4])[0], d[i + 4:i + 8]
    if kind == b"IHDR":
        w, h = struct.unpack(">II", d[i + 8:i + 16])
    if kind == b"IDAT":
        idat += d[i + 8:i + 8 + n]
    i += 12 + n
raw = zlib.decompress(idat)
rows = [raw[y * (w * 3 + 1):(y + 1) * (w * 3 + 1)] for y in range(h)]
if any(r[0] != 0 for r in rows):
    sys.exit("a row filter other than 0")
print(f"{w}x{h} {hashlib.sha256(b''.join(r[1:] for r in rows)).hexdigest()}")
EOF
)
echo "      RGB pixels: $WP11RGB (v1.1's: 1024x768 4d228736e103df153c041b196c401e3c7b06464968dee12b3d7ea98ed1945318)"
if [ "$WP11RGB" = "1024x768 4d228736e103df153c041b196c401e3c7b06464968dee12b3d7ea98ed1945318" ]; then
    pass "[1.2] make-wallpaper.py in integer maths renders v1.1's boot-menu wallpaper pixel for pixel (--version 1.1: the RGB bytes of the v1.1 ISO's wallpaper.png, SHA-256 4d228736...)"
else
    fail "[1.2] make-wallpaper.py --version 1.1: the pixels are not v1.1's ($WP11RGB)"
fi

section "[1.2] The splash's generated data (stage 8)"
# build/gen/splash_data.h: made by the build, made again here; and its copy
# of the M against kernel/logo.c's stroke table (SLOGO, read for S3 above).
SDGEN=$OUT/splash_data-regenerated.h
rm -f "$SDGEN"
PYTHONDONTWRITEBYTECODE=1 python3 "$WPGEN" --emit-c "$SDGEN"
echo "      splash_data.h: build $(sha256sum < "$OUT/gen/splash_data.h" 2>/dev/null | cut -c1-16), again $(sha256sum < "$SDGEN" 2>/dev/null | cut -c1-16)"
if [ -s "$SDGEN" ] && cmp -s "$OUT/gen/splash_data.h" "$SDGEN"; then
    pass "[1.2] make-wallpaper.py --emit-c regenerates build/gen/splash_data.h byte-identically"
else
    fail "[1.2] build/gen/splash_data.h missing or not regenerated byte-identically"
fi
SDLOGO=$(awk '/splash_logo_strokes\[4\]\[4\] = \{/ { on = 1; next } on && /^\};/ { exit }
              on { gsub(/[{},]/, " "); $1 = $1; if (NF == 4) print }' "$OUT/gen/splash_data.h" 2>/dev/null)
echo "      splash_data.h: ${SDLOGO//$'\n'/; }"
if [ "$(printf '%s\n' "$SDLOGO" | grep -c .)" -eq 4 ] && [ "$SDLOGO" = "$SLOGO" ]; then
    pass "[1.2] the splash's copy of the M (build/gen/splash_data.h) equals kernel/logo.c's four strokes"
else
    fail "[1.2] the splash's copy of the M differs from kernel/logo.c's strokes"
fi

section "[1.2] The kernel's splash code against make-wallpaper.py (tests/splashdump.c, stage 8)"
# wallpaper_core.c built as a host tool (build/splashdump, with the console
# fonts) draws the splash as the kernel does; make-wallpaper.py --splash, the
# reference, draws it from its own code and the BDF files. Four modes, the
# four scales and both pixel formats, one dot phase each. 1.2 stage 10: the
# ready state in two modes (splashdump draws it over the splash as the
# kernel does at System ready; make-wallpaper.py draws it whole).
splash_static() {   # splash_static <W> <H> <bytes per pixel> <phase or ready> <scale %>
    local a=$OUT/splash-c-$1x$2-$4.ppm b=$OUT/splash-py-$1x$2-$4.ppm t0 n what="$4 of the 3 dots lit"
    [ "$4" != ready ] || what="the ready state drawn over it at System ready"
    rm -f "$a" "$b"
    "$OUT/splashdump" "$1" "$2" "$3" "$4" "$a" 2>&1 | sed 's/^/      /'
    t0=$(date +%s)
    PYTHONDONTWRITEBYTECODE=1 python3 "$WPGEN" --splash "$1x$2" --phase "$4" "$b" 2>&1 | sed 's/^/      /'
    n=$(python3 - "$a" "$b" <<'EOF' 2>&1
import sys
a, b = open(sys.argv[1], "rb").read(), open(sys.argv[2], "rb").read()
print("same" if a == b and len(a) > 100 else
      "%d bytes differ" % sum(x != y for x, y in zip(a, b)) if len(a) == len(b) else "sizes differ")
EOF
)
    echo "      $1x$2x$(($3 * 8)), $what: $n (the reference in $(( $(date +%s) - t0 )) s)"
    if [ "$n" = same ]; then
        pass "[1.2] the kernel's splash code at $1x$2x$(($3 * 8)) (scale $5 %, $what) equals make-wallpaper.py's rendering pixel for pixel"
    else
        fail "[1.2] the kernel's splash code at $1x$2x$(($3 * 8)): $n"
    fi
}
splash_static 800 600 3 1 100
splash_static 1024 768 4 2 100
splash_static 1280 1024 4 3 125
splash_static 1920 1080 4 1 150
splash_static 2560 1440 4 0 200
splash_static 800 600 3 ready 100
splash_static 1280 1024 4 ready 125

# ---- 1.3 stage 2: the desktop toolkit (pure, not linked into the kernel) ----

section "[1.3] kernel/display/tokens.h against the Design Guide (stage 2)"
# tests/desktop_ref.py --tokens: every adopted token of the guide's three
# colour tables (section 2: v0.1, v0.2 and 1.3 stage 5) is a UI_* define of tokens.h with
# the same value and tokens.h has no other; the colour constants that
# predate it (wallpaper_core.h's WP_*, pointer_core.h's POINTER_*) each
# equal their token (the list is desktop_ref.py's TOKEN_TWINS).
DREF=$(dirname "$0")/desktop_ref.py
TOK=$(python3 "$DREF" --tokens 2>&1)
printf '%s\n' "$TOK" | sed 's/^/      /'
if [ "$(printf '%s\n' "$TOK" | tail -n 1)" = "TOKENS result: PASS" ] &&
   printf '%s\n' "$TOK" | grep -q '^TOKENS guide: 27 tokens in 3 tables .*; tokens.h: 27 defines$' &&
   [ "$(printf '%s\n' "$TOK" | grep -c '^TOKENS twin .* equal$')" -eq 12 ]; then
    pass "[1.3] kernel/display/tokens.h equals the Design Guide's three colour tables (27 tokens), and the 12 colour constants of wallpaper_core.h and pointer_core.h equal their tokens"
else
    fail "[1.3] kernel/display/tokens.h or a 1.2 colour constant differs from the Design Guide's colour tables"
fi

section "[1.3] The toolkit compiled with the kernel's flags, not linked (stage 2)"
# make toolkit built the toolkit (kernel/display/ui_*.c, icons.c,
# coverage_core.c) with the kernel's own CFLAGS, and the bold fonts it uses,
# none of them in madire.elf (1.3 stage 3: except coverage_core.c, now linked
# and built by the kernel's build; still checked here with the toolkit).
# Linked together (ld -r), every symbol they leave undefined must be one
# madire.elf defines, so linking them into the kernel (stage 3) needs
# nothing else; they must call no libgcc or libc
# helper and use no SSE/x87 register (the workflow's patterns).
TKOBJS=""
TKROOT=$(dirname "$0")/..
for f in "$TKROOT"/kernel/display/ui_*.c "$TKROOT"/kernel/display/icons.c "$TKROOT"/kernel/display/coverage_core.c; do
    f=${f#"$TKROOT"/}
    TKOBJS="$TKOBJS $OUT/${f%.c}.o"
done
for s in 8x16 10x20 12x24 16x32; do TKOBJS="$TKOBJS $OUT/font/font_${s}_bold.o"; done
TKLINK=$OUT/toolkit-linked.o
rm -f "$TKLINK"
# shellcheck disable=SC2086
ld -m elf_x86_64 -r -o "$TKLINK" $TKOBJS 2>&1 | sed 's/^/      /'
TKUNDEF=$(nm -u "$TKLINK" 2>/dev/null | awk '{ print $2 }' | LC_ALL=C sort -u)
ELFDEF=$(nm --defined-only "$OUT/madire.elf" 2>/dev/null | awk '{ print $3 }' | LC_ALL=C sort -u)
TKMISSING=$(LC_ALL=C comm -23 <(printf '%s\n' "$TKUNDEF" | grep .) <(printf '%s\n' "$ELFDEF"))
echo "      $(echo $TKOBJS | wc -w) objects; undefined after ld -r: $(printf '%s\n' "$TKUNDEF" | grep -c .) ($(echo $TKUNDEF)); not in madire.elf: $(printf '%s\n' "$TKMISSING" | grep -c .)"
if [ -s "$TKLINK" ] && [ -z "$TKMISSING" ]; then
    pass "[1.3] the toolkit's objects (kernel flags, linked together with the bold fonts) leave only symbols that madire.elf defines undefined"
else
    fail "[1.3] the toolkit's objects need symbols madire.elf does not define: $(echo $TKMISSING)"
fi
# shellcheck disable=SC2086
TKHELP=$(for o in $TKOBJS; do nm -u "$o"; done 2>&1 | grep -cE '__(udiv|div|mod|umod|mul|popcount|stack_chk)|^ *U (memset|memcpy|memmove|memcmp|strlen)$')
echo "      libgcc/libc helpers: $TKHELP"
if [ -s "$TKLINK" ] && [ "$TKHELP" -eq 0 ]; then
    pass "[1.3] the toolkit's objects call no libgcc or libc helper"
else
    fail "[1.3] the toolkit's objects call $TKHELP libgcc/libc helper(s)"
fi
TKSSE=$(objdump -d "$TKLINK" 2>/dev/null | grep -cE '%(x|y|z)mm[0-9]|%st(\(|$)|%mm[0-9]')
echo "      SSE/x87 register uses: $TKSSE"
if [ -s "$TKLINK" ] && [ "$TKSSE" -eq 0 ]; then
    pass "[1.3] the toolkit's objects use no SSE/x87 register"
else
    fail "[1.3] the toolkit's objects use SSE/x87 registers ($TKSSE)"
fi

section "[1.3] The toolkit against tests/desktop_ref.py (tests/desktopdump.c, stage 2)"
# The pure toolkit built as a host tool (build/desktopdump, with the
# console fonts) draws each scene as the kernel will; desktop_ref.py, the
# reference, draws it from the rules, the Design Guide's tables and the
# BDF files. Three scenes (primitives with clipping and the damage list;
# every icon at every scale; labels and buttons in each state the guide
# defines) in five modes: the scales 100 %, 150 % and 200 % (the icons
# and buttons also at 125 %) and both pixel formats.
desktop_static() {   # desktop_static <W> <H> <bytes per pixel> <scene>
    local a=$OUT/desktop-c-$1x$2-$4.ppm b=$OUT/desktop-py-$1x$2-$4.ppm t0 n
    rm -f "$a" "$b"
    "$OUT/desktopdump" "$1" "$2" "$3" "$4" "$a" 2>&1 | sed 's/^/      /'
    t0=$(date +%s)
    python3 "$DREF" "$1" "$2" "$4" "$b" 2>&1 | sed 's/^/      /'
    n=$(python3 - "$a" "$b" <<'EOF' 2>&1
import sys
a, b = open(sys.argv[1], "rb").read(), open(sys.argv[2], "rb").read()
print("same" if a == b and len(a) > 100 else
      "%d bytes differ" % sum(x != y for x, y in zip(a, b)) if len(a) == len(b) else "sizes differ")
EOF
)
    echo "      $1x$2x$(($3 * 8)), scene $4: $n (the reference in $(( $(date +%s) - t0 )) s)"
    if [ "$n" = same ]; then
        pass "[1.3] the toolkit's $4 scene at $1x$2x$(($3 * 8)) equals tests/desktop_ref.py's rendering pixel for pixel"
    else
        fail "[1.3] the toolkit's $4 scene at $1x$2x$(($3 * 8)): $n"
    fi
}
for mode in "800 600 4" "1024 768 4" "1280 800 3" "1920 1080 4" "2560 1440 4"; do
    for scene in prims icons widgets; do
        # shellcheck disable=SC2086
        desktop_static $mode $scene
    done
done

# ---- 1.3 stage 3b: the desktop's pictures ----

section "[1.3] The desktop's background and taskbar against tests/desktop_ref.py (stage 3b)"
# kernel/display/desktop_core.c built as the host tool (build/desktopdump)
# draws the desktop's background (the wallpaper with the M at the splash's
# place) tile by tile, as the compositor does without the cache, and the
# taskbar with its clock drawn again in its box with a fixed time;
# desktop_ref.py draws both from desktop_core.h's rules. Five modes, both
# pixel formats, every scale but 125 %. No text lies on the desktop's
# wallpaper (the owner's decision in stage 3b), so the Design Guide's
# section 12 text-box check has nothing to check there.
desktop3_static() {   # desktop3_static <W> <H> <bytes per pixel> <scene>
    local a=$OUT/desktop-c-$1x$2-$4.ppm b=$OUT/desktop-py-$1x$2-$4.ppm t0 n
    rm -f "$a" "$b"
    "$OUT/desktopdump" "$1" "$2" "$3" "$4" "$a" 2>&1 | sed 's/^/      /'
    t0=$(date +%s)
    python3 "$DREF" "$1" "$2" "$4" "$b" 2>&1 | sed 's/^/      /'
    n=$(python3 - "$a" "$b" <<'EOF' 2>&1
import sys
a, b = open(sys.argv[1], "rb").read(), open(sys.argv[2], "rb").read()
print("same" if a == b and len(a) > 100 else
      "%d bytes differ" % sum(x != y for x, y in zip(a, b)) if len(a) == len(b) else "sizes differ")
EOF
)
    echo "      $1x$2x$(($3 * 8)), scene $4: $n (the reference in $(( $(date +%s) - t0 )) s)"
    if [ "$n" = same ]; then
        pass "[1.3] the desktop's $4 at $1x$2x$(($3 * 8)) (desktop_core.c) equals tests/desktop_ref.py's rendering pixel for pixel"
    else
        fail "[1.3] the desktop's $4 at $1x$2x$(($3 * 8)): $n"
    fi
}
for mode in "800 600 4" "1024 768 4" "1280 800 3" "1920 1080 4" "2560 1440 4"; do
    for scene in background taskbar; do
        # shellcheck disable=SC2086
        desktop3_static $mode $scene
    done
done

# ---- 1.3 stage 4a: windows ----

section "[1.3] Windows against tests/desktop_ref.py, and the contrast check (stage 4a)"
# kernel/display/ui_window.c and ui_about.c built into the host tool
# (build/desktopdump) draw two About windows placed by the window manager
# (the second where the first's place was taken), the first inactive, the
# second active with its close button normal, on hover or pressed, each
# painted on its own surface and composed through its rounded shape (the
# compositor's way); windows_exposed is the first scene painted in tiles
# without surfaces (docs/plans/1.3.md, C4, A1) and must equal the same
# reference. desktop_ref.py draws them from ui_window.h's and ui_about.h's
# rules (the boot loader's value longer than its column: cut with "...").
# Five modes, both pixel formats, every scale but 125 %. Every run records
# the colour pairs it draws (desktopdump is built with -DUI_TRACE); with
# the taskbar's in the five modes, desktop_ref.py --contrast checks each
# against the Design Guide's section 12 table: a row for it whose result
# is pass, its ratio recomputed from tokens.h equal to the table's and at
# or above its rule's minimum; a pair that is no row fails.
PAIRS=$OUT/desktop-pairs.txt
rm -f "$PAIRS"
desktop4_static() {   # desktop4_static <W> <H> <bytes per pixel> <scene> [reference scene]
    local a=$OUT/desktop-c-$1x$2-$4.ppm b=$OUT/desktop-py-$1x$2-$4.ppm t0 n
    rm -f "$a" "$b"
    DESKTOP_PAIRS=$PAIRS "$OUT/desktopdump" "$1" "$2" "$3" "$4" "$a" 2>&1 | sed 's/^/      /'
    t0=$(date +%s)
    python3 "$DREF" "$1" "$2" "${5:-$4}" "$b" 2>&1 | sed 's/^/      /'
    n=$(python3 - "$a" "$b" <<'EOF' 2>&1
import sys
a, b = open(sys.argv[1], "rb").read(), open(sys.argv[2], "rb").read()
print("same" if a == b and len(a) > 100 else
      "%d bytes differ" % sum(x != y for x, y in zip(a, b)) if len(a) == len(b) else "sizes differ")
EOF
)
    echo "      $1x$2x$(($3 * 8)), scene $4: $n (the reference in $(( $(date +%s) - t0 )) s)"
    if [ "$n" = same ]; then
        pass "[1.3] the window scene $4 at $1x$2x$(($3 * 8)) (ui_window.c, ui_about.c) equals tests/desktop_ref.py's rendering pixel for pixel"
    else
        fail "[1.3] the window scene $4 at $1x$2x$(($3 * 8)): $n"
    fi
}
for mode in "800 600 4" "1024 768 4" "1280 800 3" "1920 1080 4" "2560 1440 4"; do
    for scene in windows windows_hover windows_pressed; do
        # shellcheck disable=SC2086
        desktop4_static $mode $scene
    done
    # shellcheck disable=SC2086
    desktop4_static $mode windows_exposed windows
    read -r mw mh mb <<< "$mode"
    DESKTOP_PAIRS=$PAIRS "$OUT/desktopdump" "$mw" "$mh" "$mb" taskbar "$OUT/desktop-pairs-taskbar.ppm" > /dev/null 2>&1 ||
        echo "PAIRS LOST taskbar-run-failed ${mw}x${mh}" >> "$PAIRS"
done
CONTRAST=$(python3 "$DREF" --contrast "$PAIRS" 2>&1)
printf '%s\n' "$CONTRAST" | sed 's/^/      /'
if [ "$(printf '%s\n' "$CONTRAST" | tail -n 1)" = "CONTRAST result: PASS" ] &&
   printf '%s\n' "$CONTRAST" | grep -qE '^CONTRAST tables: .*; [1-9][0-9]* distinct pairs from 5 scenes \(taskbar windows windows_exposed windows_hover windows_pressed\)$'; then
    pass "[1.3] contrast: every colour pair the toolkit draws in the window scenes and the taskbar (five modes) is a row of the Design Guide's section 12 table with a passing ratio, recomputed from tokens.h"
else
    fail "[1.3] contrast: a colour pair drawn is not in the Design Guide's section 12 table, or its ratio does not pass (see the CONTRAST lines)"
fi

# ---- 1.3 stage 4b: the Console window ----

section "[1.3] The Console window against tests/desktop_ref.py, and its contrast check (stage 4b)"
# kernel/display/ui_console.c (with ui_window.c and fbcon_core.c) built into
# the host tool (build/desktopdump) draws the Console window with a fixed
# grid (every character, colours by row and column, a full block in both
# bottom corners, the cursor in the bottom-left one), active (console) and
# inactive (console_inactive), and under an About window opened over it
# (console_about), each painted on its own surface and composed through
# its rounded shape; console_exposed is console_about painted in tiles
# without surfaces (docs/plans/1.3.md, C4, A1) and must equal the same
# reference. desktop_ref.py draws them from ui_console.h's rules (the grid
# in tests/fbcon_ref.py's way, only inside the window's inner rounded
# shape). Five modes, both pixel formats, every scale but 125 %. The
# colour pairs these scenes draw are checked against the Design Guide's
# section 12 table as in stage 4a (their own pairs file); the grid keeps
# 1.2's VGA palette, outside the table (section 12), and records no pair.
PAIRS4B=$OUT/desktop-pairs-console.txt
rm -f "$PAIRS4B"
desktop4b_static() {   # desktop4b_static <W> <H> <bytes per pixel> <scene> [reference scene]
    local a=$OUT/desktop-c-$1x$2-$4.ppm b=$OUT/desktop-py-$1x$2-$4.ppm t0 n
    rm -f "$a" "$b"
    DESKTOP_PAIRS=$PAIRS4B "$OUT/desktopdump" "$1" "$2" "$3" "$4" "$a" 2>&1 | sed 's/^/      /'
    t0=$(date +%s)
    python3 "$DREF" "$1" "$2" "${5:-$4}" "$b" 2>&1 | sed 's/^/      /'
    n=$(python3 - "$a" "$b" <<'EOF' 2>&1
import sys
a, b = open(sys.argv[1], "rb").read(), open(sys.argv[2], "rb").read()
print("same" if a == b and len(a) > 100 else
      "%d bytes differ" % sum(x != y for x, y in zip(a, b)) if len(a) == len(b) else "sizes differ")
EOF
)
    echo "      $1x$2x$(($3 * 8)), scene $4: $n (the reference in $(( $(date +%s) - t0 )) s)"
    if [ "$n" = same ]; then
        pass "[1.3] the Console window scene $4 at $1x$2x$(($3 * 8)) (ui_console.c, ui_window.c) equals tests/desktop_ref.py's rendering pixel for pixel"
    else
        fail "[1.3] the Console window scene $4 at $1x$2x$(($3 * 8)): $n"
    fi
}
for mode in "800 600 4" "1024 768 4" "1280 800 3" "1920 1080 4" "2560 1440 4"; do
    for scene in console console_inactive console_about; do
        # shellcheck disable=SC2086
        desktop4b_static $mode $scene
    done
    # shellcheck disable=SC2086
    desktop4b_static $mode console_exposed console_about
done
CONTRAST4B=$(python3 "$DREF" --contrast "$PAIRS4B" 2>&1)
printf '%s\n' "$CONTRAST4B" | sed 's/^/      /'
if [ "$(printf '%s\n' "$CONTRAST4B" | tail -n 1)" = "CONTRAST result: PASS" ] &&
   printf '%s\n' "$CONTRAST4B" | grep -qE '^CONTRAST tables: .*; [1-9][0-9]* distinct pairs from 4 scenes \(console console_about console_exposed console_inactive\)$'; then
    pass "[1.3] contrast: every colour pair the Console window scenes draw (five modes) is a row of the Design Guide's section 12 table with a passing ratio, recomputed from tokens.h; the grid's VGA colours record none"
else
    fail "[1.3] contrast: a colour pair the Console window scenes draw is not in the Design Guide's section 12 table, or its ratio does not pass (see the CONTRAST lines)"
fi

# ---- 1.3 stage 5: the menus, the M button and the Restart dialog ----

section "[1.3] The menus, the M button and the Restart dialog against tests/desktop_ref.py, and their contrast check (stage 5)"
# kernel/display/ui_menu.c, ui_dialog.c and desktop_core.c's M button
# states built into the host tool (build/desktopdump) draw, each on bg.deep
# with the taskbar: the M button on hover and pressed; the M menu as the
# mouse opens it (no row selected), as the Windows key opens it (About
# selected, the focus ring) and with Restart selected by the pointer; the
# desktop menu moved left and up to stay on the screen and above the
# taskbar, its second row selected; the Restart dialog with its close button
# on hover, the ring on Cancel with Restart on hover, the ring on Restart
# with Cancel on hover, Cancel pressed, Restart pressed (1.5 stage 6: the
# power dialog, Cancel, Restart and Shut down; the ring on Cancel with Shut
# down on hover, and a twelfth scene, Shut down pressed; the M menu's row
# Power...). menu_tiles and
# dialog_tiles are the key-opened M menu and the Tab-focused dialog painted
# in tiles (as the compositor paints them, clipped to each damaged
# rectangle) and must equal the same references. desktop_ref.py draws them
# from ui_menu.h's, ui_dialog.h's and desktop_core.h's rules. Five modes,
# both pixel formats, every scale but 125 %. The colour pairs the eleven
# whole scenes draw are checked against the Design Guide's section 12 table
# as in stage 4a (their own pairs file); a fill the table marks "fail alone"
# is recorded as carried: by its accent bar (which the same scene must have
# recorded, passing) or by the pointer.
PAIRS5=$OUT/desktop-pairs-menus.txt
rm -f "$PAIRS5"
desktop5_static() {   # desktop5_static <W> <H> <bytes per pixel> <scene> [reference scene]
    # 1.5 stage 6: the check's tag is D5_TAG's, [1.3] when it is not set.
    local a=$OUT/desktop-c-$1x$2-$4.ppm b=$OUT/desktop-py-$1x$2-$4.ppm t0 n tag=${D5_TAG:-1.3}
    rm -f "$a" "$b"
    DESKTOP_PAIRS=$PAIRS5 "$OUT/desktopdump" "$1" "$2" "$3" "$4" "$a" 2>&1 | sed 's/^/      /'
    t0=$(date +%s)
    python3 "$DREF" "$1" "$2" "${5:-$4}" "$b" 2>&1 | sed 's/^/      /'
    n=$(python3 - "$a" "$b" <<'EOF' 2>&1
import sys
a, b = open(sys.argv[1], "rb").read(), open(sys.argv[2], "rb").read()
print("same" if a == b and len(a) > 100 else
      "%d bytes differ" % sum(x != y for x, y in zip(a, b)) if len(a) == len(b) else "sizes differ")
EOF
)
    echo "      $1x$2x$(($3 * 8)), scene $4: $n (the reference in $(( $(date +%s) - t0 )) s)"
    if [ "$n" = same ]; then
        pass "[$tag] the menu scene $4 at $1x$2x$(($3 * 8)) (ui_menu.c, ui_dialog.c, desktop_core.c) equals tests/desktop_ref.py's rendering pixel for pixel"
    else
        fail "[$tag] the menu scene $4 at $1x$2x$(($3 * 8)): $n"
    fi
}
for mode in "800 600 4" "1024 768 4" "1280 800 3" "1920 1080 4" "2560 1440 4"; do
    for scene in mbutton_hover mbutton_pressed menu_mouse menu_key menu_hover menu_desktop dialog dialog_key \
                 dialog_tab dialog_pressed_cancel dialog_pressed_restart; do
        # shellcheck disable=SC2086
        desktop5_static $mode $scene
    done
    # 1.5 stage 6: the power dialog's Shut down pressed, a scene of its own ([1.5]).
    # shellcheck disable=SC2086
    D5_TAG=1.5 desktop5_static $mode dialog_pressed_shutdown
    # shellcheck disable=SC2086
    desktop5_static $mode menu_tiles menu_key
    # shellcheck disable=SC2086
    desktop5_static $mode dialog_tiles dialog_tab
done
CONTRAST5=$(python3 "$DREF" --contrast "$PAIRS5" 2>&1)
printf '%s\n' "$CONTRAST5" | sed 's/^/      /'
if [ "$(printf '%s\n' "$CONTRAST5" | tail -n 1)" = "CONTRAST result: PASS" ] &&
   printf '%s\n' "$CONTRAST5" | grep -qE '^CONTRAST tables: .*; [1-9][0-9]* distinct pairs from 12 scenes \(dialog dialog_key dialog_pressed_cancel dialog_pressed_restart dialog_pressed_shutdown dialog_tab mbutton_hover mbutton_pressed menu_desktop menu_hover menu_key menu_mouse\)$' &&
   [ "$(printf '%s\n' "$CONTRAST5" | grep -c '^CONTRAST carried pass ')" -eq 4 ]; then
    pass "[1.3] contrast: every colour pair the menus, the M button and the dialog draw (five modes) is a row of the Design Guide's section 12 table with a passing ratio, recomputed from tokens.h; each fill marked fail alone is carried by its passing accent bar in the same scene, or by the pointer"
else
    fail "[1.3] contrast: a colour pair the menus, the M button or the dialog draw is not in the Design Guide's section 12 table, does not pass, or a carried fill lacks its bar (see the CONTRAST lines)"
fi
# The contrast check's own self-test (the owner's mutation test, made
# permanent): two fixture pairs files (tests/contrast-fixtures/) that it
# must FAIL, each for the missing bar: a selected row's fill with no accent
# bar recorded, and one whose bar was recorded only in another scene.
for fx in row-no-bar:fixture_nobar bar-other-scene:fixture_row; do
    FXOUT=$(python3 "$DREF" --contrast "$(dirname "$0")/contrast-fixtures/${fx%%:*}.txt" 2>&1)
    printf '%s\n' "$FXOUT" | grep -E '^CONTRAST (carried|result)' | sed 's/^/      /'
    if [ "$(printf '%s\n' "$FXOUT" | tail -n 1)" = "CONTRAST result: FAIL (1)" ] &&
       printf '%s\n' "$FXOUT" | grep -qE "^CONTRAST carried FAIL carried-bar 112447 on 0C182A .*; no passing accent bar on it in: ${fx##*:}: "; then
        pass "[1.3] contrast self-test: the fixture ${fx%%:*} (a selected row's fill without its accent bar in the same scene) makes the contrast check FAIL, for the missing bar"
    else
        fail "[1.3] contrast self-test: the fixture ${fx%%:*} did not make the contrast check fail for the missing bar"
    fi
done

# ---- 1.2: the size report (decides no check) ----

section "Size report (1.2 stage 2; report only)"
# Stage 2 (docs/plans/1.2.md, decision 13): `size` of the kernel and of every
# program ELF, and the kernel margin below 4 MiB (computed as the workflow
# does) against the 524,288-byte trigger for a size-gate stage. Every line
# starts with SIZE or KERNEL_MARGIN_TRIGGER, so tests/combine-shards.sh can
# repeat them; none is a PASS or FAIL line. 1.2 stage 3: each file's line
# ends with its label: "shipped" (madire.elf, and every user/*.elf, which
# the kernel links as a built-in program) or "test-only" (test-user/*.elf,
# never in the kernel or the ISO).
(cd "$OUT" && size madire.elf user/*.elf test-user/*.elf 2>&1) |
    awk '$NF == "filename" { print $0 "\tlabel"; next }
         $NF == "madire.elf" || $NF ~ /^user\// { print $0 "\tshipped"; next }
         $NF ~ /^test-user\// { print $0 "\ttest-only"; next }
         { print }' | sed 's/^/SIZE /'
KEND=$(nm -n "$OUT/madire.elf" 2>/dev/null | awk '$3 == "__kernel_end" { print $1; exit }')
if [ -n "$KEND" ]; then
    KMARGIN=$(( 0x400000 - (0x$KEND - 0xffffffff80000000) ))
    if [ "$KMARGIN" -ge 524288 ]; then KVERDICT="at or above the trigger: no gate stage"
    else KVERDICT="BELOW the trigger: STOP for a gate stage"; fi
    echo "KERNEL_MARGIN_TRIGGER KERNEL_MARGIN_BYTES $KMARGIN, trigger 524288 (decision 13): $KVERDICT"
else
    echo "KERNEL_MARGIN_TRIGGER KERNEL_MARGIN_BYTES unknown (no __kernel_end in madire.elf), trigger 524288 (decision 13)"
fi

# ---- 1.4 stage 2: the test-image tools ----

section "[1.4] Test-image tools: mke2fs, e2fsck, debugfs and tests/disk_image.py (stage 2)"
# docs/plans/1.4.md, D13 option A: e2fsprogs as unmodified outside programs
# on the test machine only (tests/disk-lib.sh), the partition table and the
# Madire test marker from tests/disk_image.py. The probe lines (E2FSPROGS,
# SFDISK) and the sfdisk cross-check (SFDISK_CROSSCHECK) are report only;
# the seven checks below prove the tools themselves: an image made twice is
# the same byte for byte, e2fsck -fn finds nothing to change, debugfs reads
# back the expected files and the pinned parameters, and the marker and
# GPT CRC32s (disk_image.py's own, from the specification) equal zlib's. No
# QEMU boot; everything stays under $OUT/disk-tools (not serial-*.log).
DISK_T0=$(date +%s)
disk_probe
DISK_W=$OUT/disk-tools
rm -rf "$DISK_W" && mkdir -p "$DISK_W"
# The sample files, with fixed modes. Their change times cannot be set (the
# kernel sets them to the current second at every touch below); both images
# are made from this one folder, and a pair counts only when both passes saw
# the same change times (1.4 stage 15b, the release-run race).
DISK_WANT=$( { disk_image tree "$DISK_W/src"; echo "DIR /lost+found"; } | LC_ALL=C sort)
find "$DISK_W/src" -type d -exec chmod 0755 {} +
find "$DISK_W/src" -type f -exec chmod 0644 {} +
sed 's/^/      want: /' <<< "$DISK_WANT"
# Every path of the tree, the root first, from the listing (not by reading
# the tree). Stage 2 fix: mke2fs -d copies each source inode's access time,
# and reading the tree changes it, so both times are reset just before each
# mke2fs; touch sets them by path and reads no folder.
DISK_TOUCH=("$DISK_W/src")
while read -r _ p _; do [ "$p" = /lost+found ] || DISK_TOUCH+=("$DISK_W/src$p"); done <<< "$DISK_WANT"
echo "      times reset before each mke2fs: ${#DISK_TOUCH[@]} paths"
# An 8 MiB image, 1 KiB blocks (big.bin reaches the double indirect block),
# 128-byte inodes, 256 inodes; made twice. mke2fs -d copies each source
# inode's change time, which touch sets to the current second: the change
# times are read (stat %Z, metadata only) after each touch, and a pair whose
# passes saw different ones (a second boundary between the two touches) is
# made again, at most 3 attempts, each retry printed (1.4 stage 15b).
for DISK_TRY in 1 2 3; do
    DISK_MK=""
    for x in a b; do
        touch -h -a -m -d "@$DISK_TIME" "${DISK_TOUCH[@]}"
        printf -v "DISK_CT_$x" '%s' "$(stat -c %Z "${DISK_TOUCH[@]}" | tr '\n' ' ')"
        disk_mkfs "$DISK_W/$x.img" 8192 1024 128 256 "$DISK_W/src" > "$DISK_W/mkfs-$x.txt" 2>&1
        DISK_MK="$DISK_MK $?"
        sed "s/^/      mke2fs ($x): /" "$DISK_W/mkfs-$x.txt"
    done
    [ "$DISK_CT_a" = "$DISK_CT_b" ] && break
    echo "      attempt $DISK_TRY: the two passes saw different change times (a second boundary between the touches)$([ "$DISK_TRY" -lt 3 ] && echo "; made again")"
done
echo "      DISK_MKFS_ATTEMPTS $DISK_TRY (report only)"
DISK_SA=$(sha256sum < "$DISK_W/a.img" | cut -d' ' -f1)
DISK_SB=$(sha256sum < "$DISK_W/b.img" | cut -d' ' -f1)
echo "      mke2fs exit:$DISK_MK; a.img $DISK_SA; b.img $DISK_SB ($(stat -c%s "$DISK_W/a.img" 2>/dev/null) bytes)"
echo "DISK_IMAGE_SHA256 ext2-1k-128 $DISK_SA (report only: identical within a run when both passes saw the same change times (checked below); it varies between runs, since mke2fs -d copies the source files' change times, which the kernel sets at every touch and touch cannot set; the stage 9 configuration change did not alter it)"
if [ "$DISK_MK" = " 0 0" ] && [ -s "$DISK_W/a.img" ] && [ "$DISK_CT_a" = "$DISK_CT_b" ] && [ "$DISK_SA" = "$DISK_SB" ]; then
    pass "[1.4] disk tools: mke2fs with tests/mke2fs.conf, the fixed UUID, hash seed and time makes the same ext2 image twice (SHA-256 identical)"
else
    [ "$DISK_CT_a" = "$DISK_CT_b" ] || echo "      the two passes saw different change times in all $DISK_TRY attempts: a: $DISK_CT_a b: $DISK_CT_b"
    cmp -l "$DISK_W/a.img" "$DISK_W/b.img" 2>&1 | head -n 8 | sed 's/^/      differs (offset, octal bytes): /'
    echo "      $(cmp -l "$DISK_W/a.img" "$DISK_W/b.img" 2>/dev/null | wc -l) bytes differ"
    fail "[1.4] disk tools: mke2fs did not make the same ext2 image twice (exit:$DISK_MK)"
fi
# The partitioned disks, each made twice from a.img.
DISK_RC=""
for t in mbr gpt; do
    for n in 1 2; do
        disk_image "$t" "$DISK_W/a.img" "$DISK_W/$t-$n.img"
        DISK_RC="$DISK_RC $?"
    done
done
DISK_S=""
for f in mbr-1 mbr-2 gpt-1 gpt-2; do
    DISK_S="$DISK_S $(sha256sum < "$DISK_W/$f.img" 2>/dev/null | cut -d' ' -f1)"
done
read -r DISK_M1 DISK_M2 DISK_G1 DISK_G2 <<< "$DISK_S"
echo "      disk_image.py exit:$DISK_RC; mbr $DISK_M1 / $DISK_M2; gpt $DISK_G1 / $DISK_G2"
echo "DISK_IMAGE_SHA256 mbr $DISK_M1 gpt $DISK_G1 (report only)"
if [ "$DISK_RC" = " 0 0 0 0" ] && [ -n "$DISK_M1" ] && [ "$DISK_M1" = "$DISK_M2" ] && [ "$DISK_G1" = "$DISK_G2" ] &&
   [ "$DISK_M1" != "$DISK_G1" ]; then
    pass "[1.4] disk tools: tests/disk_image.py makes the same MBR disk and the same GPT disk twice (SHA-256 identical)"
else
    fail "[1.4] disk tools: tests/disk_image.py did not make the same MBR and GPT disks twice (exit:$DISK_RC)"
fi
# e2fsck -fn on the fresh image: nothing to change, and the image unchanged.
DISK_CK=$(disk_fsck "$DISK_W/a.img"); DISK_CKRC=$?
sed 's/^/      e2fsck a.img: /' <<< "$DISK_CK"
DISK_SA2=$(sha256sum < "$DISK_W/a.img" | cut -d' ' -f1)
if [ "$DISK_CKRC" -eq 0 ] && [ -s "$DISK_W/a.img" ] && [ "$DISK_SA2" = "$DISK_SA" ]; then
    pass "[1.4] disk tools: e2fsck -fn finds nothing to change on the fresh image (exit 0), and the image is unchanged by it"
else
    fail "[1.4] disk tools: e2fsck -fn on the fresh image: exit $DISK_CKRC, image $([ "$DISK_SA2" = "$DISK_SA" ] && echo unchanged || echo CHANGED)"
fi
# The partition of each disk: extracted, the same bytes as a.img after the
# marker's sector, and e2fsck -fn clean with the marker in place.
DISK_OK=1
for t in mbr gpt; do
    disk_image extract "$DISK_W/$t-1.img" "$DISK_W/part-$t.img" || DISK_OK=0
    DISK_CK=$(disk_fsck "$DISK_W/part-$t.img"); DISK_CKRC=$?
    sed "s/^/      e2fsck part-$t.img: /" <<< "$DISK_CK"
    [ "$DISK_CKRC" -eq 0 ] || DISK_OK=0
    cmp -s <(tail -c +513 "$DISK_W/a.img") <(tail -c +513 "$DISK_W/part-$t.img") || DISK_OK=0
    [ -s "$DISK_W/part-$t.img" ] || DISK_OK=0
done
if [ "$DISK_OK" = 1 ]; then
    pass "[1.4] disk tools: the ext2 partition of the MBR disk and of the GPT disk holds the image's bytes after the marker's sector, and e2fsck -fn finds nothing to change in either"
else
    fail "[1.4] disk tools: a partition could not be extracted, differs from the image, or e2fsck -fn found something in it"
fi
# debugfs: every expected folder and file (names, types, sizes, contents).
DISK_GOT=$(disk_listing "$DISK_W/a.img" / $(awk '$1 == "DIR" { print $2 }' <<< "$DISK_WANT"))
sed 's/^/      debugfs: /' <<< "$DISK_GOT"
if [ -n "$DISK_GOT" ] && [ "$DISK_GOT" = "$DISK_WANT" ]; then
    pass "[1.4] disk tools: debugfs lists exactly the expected folders and files (names, types, sizes) and reads back every file's contents (SHA-256)"
else
    diff <(echo "$DISK_WANT") <(echo "$DISK_GOT") | sed 's/^/      diff: /'
    fail "[1.4] disk tools: debugfs's listing differs from the sample files"
fi
# debugfs stats: the pinned parameters (the label, the UUID, revision 1,
# 1 KiB blocks, 128-byte inodes, no reserved blocks, the features of
# tests/mke2fs.conf, the hash seed, the creation time from DISK_TIME).
DISK_ST=$(TZ=UTC disk_debugfs "$DISK_W/a.img" stats)
disk_field() { sed -n "s/^$1:[[:space:]]*//p" <<< "$DISK_ST" | head -n 1; }
DISK_FEAT=$(disk_field "Filesystem features" | tr ' ' '\n' | grep -v '^$' | LC_ALL=C sort | tr '\n' ' ')
DISK_WHEN=$(LC_ALL=C TZ=UTC date -u -d "@$DISK_TIME" '+%a %b %e %H:%M:%S %Y')
for k in "Filesystem volume name" "Filesystem UUID" "Filesystem revision #" "Filesystem features" "Block size" \
         "Inode size" "Reserved block count" "Inode count" "Directory Hash Seed" "Default directory hash" \
         "Filesystem created" "Last write time" "Last mount time"; do
    echo "      stats: $k: $(disk_field "$k")"
done
if [ "$(disk_field "Filesystem volume name")" = "$DISK_LABEL" ] && [ "$(disk_field "Filesystem UUID")" = "$DISK_UUID" ] &&
   [ "$(disk_field "Filesystem revision #")" = "1 (dynamic)" ] && [ "$(disk_field "Block size")" = 1024 ] &&
   [ "$(disk_field "Inode size")" = 128 ] && [ "$(disk_field "Reserved block count")" = 0 ] &&
   [ "$(disk_field "Directory Hash Seed")" = "$DISK_HASH_SEED" ] &&
   [ "$DISK_FEAT" = "dir_index ext_attr filetype large_file resize_inode sparse_super " ] &&
   [ "$(disk_field "Filesystem created")" = "$DISK_WHEN" ]; then
    pass "[1.4] disk tools: debugfs reads the pinned parameters (label madire-test, the fixed UUID and hash seed, revision 1, 1 KiB blocks, 128-byte inodes, no reserved blocks, exactly tests/mke2fs.conf's features, created at the fixed time)"
else
    fail "[1.4] disk tools: debugfs stats differ from the pinned parameters (features: $DISK_FEAT; created: $(disk_field "Filesystem created"), want $DISK_WHEN)"
fi
# The marker and the CRC32s: disk_image.py's own CRC32 against zlib.crc32.
printf '123456789' > "$DISK_W/crc-check.txt"
DISK_VM=$(disk_image verify "$DISK_W/mbr-1.img"); DISK_VMRC=$?
DISK_VG=$(disk_image verify "$DISK_W/gpt-1.img"); DISK_VGRC=$?
DISK_VC=$(disk_image crc "$DISK_W/crc-check.txt")
sed 's/^/      mbr: /' <<< "$DISK_VM"; sed 's/^/      gpt: /' <<< "$DISK_VG"; sed 's/^/      /' <<< "$DISK_VC"
DISK_MRE='^MARKER magic=MADIRE-TEST-DISK version=1 length=48 start=2048 sectors=16384 crc=(0x[0-9a-f]{8}) own=\1 zlib=\1$'
if [ "$DISK_VMRC" -eq 0 ] && [ "$DISK_VGRC" -eq 0 ] &&
   grep -qxE '^TABLE mbr start=2048 sectors=16384 type=0x83 .*' <<< "$DISK_VM" && grep -qE "$DISK_MRE" <<< "$DISK_VM" &&
   grep -qx 'MARKER_RESULT ok' <<< "$DISK_VM" &&
   grep -qxE '^TABLE gpt start=2048 sectors=16384 type=0FC63DAF-8483-4772-8E79-3D69D8477DE4 name=madire-test .*' <<< "$DISK_VG" &&
   grep -qE "$DISK_MRE" <<< "$DISK_VG" && grep -qx 'MARKER_RESULT ok' <<< "$DISK_VG" &&
   grep -qE '^GPT header_crc=(0x[0-9a-f]{8}) own=\1 zlib=\1 entries_crc=(0x[0-9a-f]{8}) own=\2 zlib=\2$' <<< "$DISK_VG" &&
   grep -qx 'GPT_RESULT ok' <<< "$DISK_VG" && grep -qE ' own=0xcbf43926 zlib=0xcbf43926$' <<< "$DISK_VC"; then
    pass "[1.4] disk tools: both disks' marker reads back (MADIRE-TEST-DISK, version 1, start 2048 and 16384 sectors as in the partition table) with its CRC32 equal to zlib.crc32, as are the GPT header's and entry array's; the CRC32 of '123456789' is 0xCBF43926"
else
    fail "[1.4] disk tools: a marker, a partition table or a CRC32 does not read back as written (see the mbr: and gpt: lines)"
fi
# sfdisk's reading of the two tables (report only).
if [ -n "$(disk_tool sfdisk)" ]; then
    for t in mbr gpt; do
        DISK_SF=$(disk_sfdisk_dump "$DISK_W/$t-1.img")
        sed "s/^/      sfdisk $t: /" <<< "$DISK_SF"
        if [ "$t" = mbr ]; then DISK_SFRE='start= *2048, size= *16384, type=83'; DISK_SFL=dos
        else DISK_SFRE='start= *2048, size= *16384, type=0FC63DAF-8483-4772-8E79-3D69D8477DE4'; DISK_SFL=gpt; fi
        if grep -qx "label: $DISK_SFL" <<< "$DISK_SF" && grep -qiE "$DISK_SFRE" <<< "$DISK_SF" &&
           [ "$(grep -c 'start=' <<< "$DISK_SF")" -eq 1 ]; then
            echo "SFDISK_CROSSCHECK $t agrees (one partition, start 2048, 16384 sectors, its type)"
        else
            echo "SFDISK_CROSSCHECK $t DIFFERS (report only; see the sfdisk $t: lines)"
        fi
    done
else
    echo "SFDISK_CROSSCHECK skipped (sfdisk absent)"
fi
echo "      [1.4] test-image tools: $(( $(date +%s) - DISK_T0 )) s (report only)"

# ---- 1.4 stage 3: the PCI scan ----

section "[1.4] The PCI scan: the PCI: and Storage: lines against QEMU's own view (stage 3)"
# Three -kernel boots at -m 128M (docs/plans/1.4.md, S3): (a) the default
# pc machine, no AHCI controller; (b) -device ich9-ahci at 00:05.0; (c)
# ich9-ahci at slot 2 behind a pci-bridge at 00:06.0 (the firmware gives
# the bridge its secondary bus). Each boot types "mem" on the console
# through qmp_keys.py (the first key, x, opens the console and is not
# typed). tests/qmp_pci.py reads QEMU's own view over a second QMP socket
# at System ready (query-pci): the kernel's PCI: and Storage: lines must
# agree with it (the counts, the controller's bus:dev.fn, its BAR5 as the
# firmware assigned it). The scan is read only and maps nothing in stage
# 3, so the memory lines and mem's figures are the same on all three
# boots. The two lines sit between "Filesystem self-test: PASS" and "Key
# events:", outside every expect_lines window (risk 33).
# 1.4 stage 4 (changed checks, not added): the AHCI driver now starts on
# boots (b) and (c): the controller and its BAR5 are on the "ahci:
# controller at ..." line, and the Storage: line says "Storage: AHCI
# bb:dd.f, no disk (Live mode)" (no device on any port). The driver maps
# its registers in slot 508, so on (b) and (c) mem's "Page tables" and
# "Free pages (now)" differ from (a)'s by exactly the page tables its
# "ahci: slot 508 now" line says it added (and the DMA and cache pages,
# none without a device); everything else, the boot's memory lines
# included, is the same on all three. The driver's own lines ("ahci:",
# "blk:") come between the PCI: line and the Storage: line.
PCI_T0=$(date +%s)
PCI_READY="System ready. Press any key to open the console."
# 1.4 stage 12 (changed, the owner's approval): "1 bus" and "1 PCI-to-PCI
# bridge"; the words are captured (\2, \6) and pcitest_agree accepts only
# the singular for exactly 1 and the plural for any other count.
PCI_LINE_RE='^PCI: ([0-9]+) (bus|buses), ([0-9]+) devices, ([0-9]+) functions, ([0-9]+) PCI-to-PCI (bridge|bridges) \(([0-9]+) secondary buses refused\); storage: ([0-9]+) AHCI, ([0-9]+) other; configuration read only$'
# pcitest_boot <name> <qemu args...>: the boot (log serial-pci-<name>.log);
# PCI_Q holds qmp_pci.py's output (also kept in serial-pci-<name>.qpci.txt).
pcitest_boot() {
    local name=$1 log=$OUT/serial-pci-$1.log sock=$OUT/qmp-pci.sock qpid
    shift
    rm -f "$log" "$sock"
    python3 "$(dirname "$0")/qmp_pci.py" "$sock" "$log" "$PCI_READY" --timeout 50 \
        > "$OUT/serial-pci-$name.qpci.txt" 2>&1 &
    qpid=$!
    boot_text "$log" 60 "System ready." "x{wait:1}mem{ret}" 0.05 $'\nHeap free:' -m 128M -kernel "$KERNEL" \
        -qmp unix:"$sock",server=on,wait=off "$@"
    wait "$qpid"
    PCI_Q=$(cat "$OUT/serial-pci-$name.qpci.txt")
    sed 's/^/      /' <<< "$PCI_Q"
    grep -E '^(PCI|Storage): ' "$log" | sed 's/^/      /'
}
# pcitest_agree <log> <qmp_pci.py output>: exactly one PCI: and one
# Storage: line; the kernel's buses, devices, functions, bridges, AHCI
# controllers and other mass-storage functions equal QEMU's, none refused.
# Sets PK (the kernel's "buses devices functions bridges refused ahci
# other"), PQ_AHCI and PQ_BAR5 (QEMU's controller and its BAR5).
pcitest_agree() {
    local q b d f r qa q5 qx qo nb wb nr wr
    PK=$(sed -nE "s/$PCI_LINE_RE/\1 \3 \4 \5 \7 \8 \9/p" "$1")
    # 1.4 stage 12: each count's word exactly right (1 bus, 2 buses, 0 buses;
    # 1 bridge, 0 bridges); a wrong form leaves PK empty.
    read -r nb wb nr wr <<< "$(sed -nE "s/$PCI_LINE_RE/\1 \2 \5 \6/p" "$1")"
    if [ "${wb:-}" != "$([ "${nb:-}" = 1 ] && echo bus || echo buses)" ] ||
       [ "${wr:-}" != "$([ "${nr:-}" = 1 ] && echo bridge || echo bridges)" ]; then
        PK=""
    fi
    read -r b d f r qa q5 <<< "$(sed -nE 's/^QPCI_SUMMARY buses=([0-9]+) devices=([0-9]+) functions=([0-9]+) bridges=([0-9]+) ahci=([^ ]+) bar5=([^ ]+)$/\1 \2 \3 \4 \5 \6/p' <<< "$2")"
    qx=$(grep -cE '^QPCI .* class 01\.06 ' <<< "$2")
    qo=$(( $(grep -cE '^QPCI .* class 01\.' <<< "$2") - qx ))
    PQ_AHCI=${qa:-}; PQ_BAR5=${q5:-}
    [ "$(grep -c '^PCI: ' "$1")" -eq 1 ] && [ "$(grep -c '^Storage: ' "$1")" -eq 1 ] && [ -n "$PK" ] &&
        [ -n "${q5:-}" ] && [ "$PK" = "$b $d $f $r 0 $qx $qo" ]
}
pcitest_boot plain
PCI_A=$OUT/serial-pci-plain.log
if pcitest_agree "$PCI_A" "$PCI_Q" && [ "${PK%% *}" = 1 ] && [ "$PQ_AHCI" = none ] &&
   grep -qxF "Storage: no AHCI controller (Live mode)" "$PCI_A"; then
    pass "[1.4] PCI, the default pc machine: one PCI: line (1 bus, no bridge, no AHCI controller; its devices, functions and other storage functions as QEMU's query-pci lists them) and 'Storage: no AHCI controller (Live mode)'"
else
    fail "[1.4] PCI, the default pc machine: the PCI: or Storage: line differs from QEMU's view (kernel: ${PK:-no PCI: line}; $(grep -E '^QPCI_(SUMMARY|LINE)|ERROR' <<< "$PCI_Q" | tr '\n' ' '))"
fi
pcitest_boot ahci -device ich9-ahci,id=pcitest-ahci,addr=0x5
PCI_B=$OUT/serial-pci-ahci.log
if pcitest_agree "$PCI_B" "$PCI_Q" && [ "$(cut -d' ' -f1,4,6 <<< "$PK")" = "1 0 1" ] &&
   [ "$PQ_AHCI" = 00:05.0 ] && grep -qE '^0x[0-9a-f]{8}$' <<< "$PQ_BAR5" &&
   grep -qxF "ahci: controller at 00:05.0 (8086:2922, BAR5 $PQ_BAR5)" "$PCI_B" &&
   grep -qxF "Storage: AHCI 00:05.0, no disk (Live mode)" "$PCI_B"; then
    pass "[1.4] PCI, -device ich9-ahci: the controller found at 00:05.0 (8086:2922) with BAR5 $PQ_BAR5, as QEMU's query-pci gives it; the PCI: line's counts as QEMU's; with no device on it, 'Storage: AHCI 00:05.0, no disk (Live mode)'"
else
    fail "[1.4] PCI, -device ich9-ahci: the ahci: line does not name 00:05.0 with QEMU's BAR5, the Storage: line differs, or the counts differ (kernel: ${PK:-no PCI: line}; $(grep -E '^QPCI_(SUMMARY|LINE)|ERROR' <<< "$PCI_Q" | tr '\n' ' '))"
fi
pcitest_boot bridge -device pci-bridge,id=pcitest-br,chassis_nr=1,addr=0x6 \
    -device ich9-ahci,id=pcitest-ahci,bus=pcitest-br,addr=0x2
PCI_C=$OUT/serial-pci-bridge.log
if pcitest_agree "$PCI_C" "$PCI_Q" && [ "$(cut -d' ' -f1,4,6 <<< "$PK")" = "2 1 1" ] &&
   grep -qE '^0[1-9a-f]:02\.0$' <<< "$PQ_AHCI" && grep -qE '^0x[0-9a-f]{8}$' <<< "$PQ_BAR5" &&
   grep -qxF "ahci: controller at $PQ_AHCI (8086:2922, BAR5 $PQ_BAR5)" "$PCI_C" &&
   grep -qxF "Storage: AHCI $PQ_AHCI, no disk (Live mode)" "$PCI_C"; then
    pass "[1.4] PCI, ich9-ahci behind a pci-bridge: the bridge's secondary bus scanned (2 buses, 1 bridge); the controller found at $PQ_AHCI with BAR5 $PQ_BAR5, as QEMU's query-pci gives them; with no device on it, 'Storage: AHCI $PQ_AHCI, no disk (Live mode)'"
else
    fail "[1.4] PCI, ich9-ahci behind a pci-bridge: the controller not found on the secondary bus as QEMU gives it, or the counts differ (kernel: ${PK:-no PCI: line}; $(grep -E '^QPCI_(SUMMARY|LINE)|ERROR' <<< "$PCI_Q" | tr '\n' ' '))"
fi
# pcitest_mem <log>: the boot's memory lines and mem's output.
pcitest_mem() {
    grep -E '^(Memory: |Free pages:|Paging tables:|Kernel half frozen:)' "$1"
    awk '!on && $0 == "madire> mem" { on = 1; next } on { print } on && /^Heap free:/ { exit }' "$1"
}
PCI_MA=$(pcitest_mem "$PCI_A"); PCI_MB=$(pcitest_mem "$PCI_B"); PCI_MC=$(pcitest_mem "$PCI_C")
grep -E '^(Free pages|Paging tables|Mapped pages|Page tables)' <<< "$PCI_MA" | sed 's/^/      plain: /'
# pcitest_memdiff <the plain boot's figures> <a controller boot's figures>
# <its log>: 1.4 stage 4: the same but for mem's Page tables (more by the
# page tables the driver's "ahci: slot 508 now" line says it added) and
# Free pages (now) (fewer by those tables and its DMA and cache pages).
pcitest_memdiff() {
    local t d c pa pb fa fb
    read -r t d c <<< "$(sed -nE 's/^ahci: slot 508 now: [0-9]+ page tables, [0-9]+ pages mapped \(walked\); ([0-9]+) page tables added, ([0-9]+) DMA pages, ([0-9]+) cache pages$/\1 \2 \3/p' "$3")"
    echo "      $(basename "$3"): the driver added ${t:-?} page tables, ${d:-?} DMA pages, ${c:-?} cache pages"
    [ -n "${t:-}" ] && [ -n "${d:-}" ] && [ -n "${c:-}" ] || return 1
    [ "$(grep -vE '^(Page tables|Free pages \(now\)):' <<< "$1")" = "$(grep -vE '^(Page tables|Free pages \(now\)):' <<< "$2")" ] || return 1
    pa=$(sed -nE 's/^Page tables: +([0-9]+).*/\1/p' <<< "$1"); pb=$(sed -nE 's/^Page tables: +([0-9]+).*/\1/p' <<< "$2")
    fa=$(sed -nE 's/^Free pages \(now\): +([0-9]+).*/\1/p' <<< "$1"); fb=$(sed -nE 's/^Free pages \(now\): +([0-9]+).*/\1/p' <<< "$2")
    [ -n "$pa" ] && [ -n "$pb" ] && [ -n "$fa" ] && [ -n "$fb" ] && [ "$t" -gt 0 ] &&
        [ $((pb - pa)) -eq "$t" ] && [ $((fa - fb)) -eq $((t + d + c)) ]
}
if [ "$(grep -cE '^(Memory: |Free pages:|Paging tables:|Kernel half frozen:|Free pages \(now\):|Mapped pages:|Page tables:|Heap free:)' <<< "$PCI_MA")" -eq 8 ] &&
   ! grep -q '^ahci: ' "$PCI_A" && pcitest_memdiff "$PCI_MA" "$PCI_MB" "$PCI_B" && pcitest_memdiff "$PCI_MA" "$PCI_MC" "$PCI_C"; then
    pass "[1.4] PCI: the scan maps nothing: the memory lines (Memory, Free pages, Paging tables, Kernel half frozen) are the same with no controller, with one on bus 0 and with one behind a bridge, and so are mem's figures, but for Page tables and Free pages (now) with a controller, which differ by exactly the slot 508 page tables (and DMA and cache pages) the AHCI driver logs"
else
    fail "[1.4] PCI: the memory lines or mem's figures differ between the three boots otherwise than by the AHCI driver's slot 508 figures (or mem did not answer): $(diff <(echo "$PCI_MA") <(echo "$PCI_MB") | head -n 6 | tr '\n' ' ') $(diff <(echo "$PCI_MA") <(echo "$PCI_MC") | head -n 6 | tr '\n' ' ')"
fi
# pcitest_order <log>: the PCI: line right after "Filesystem self-test:
# PASS", the Storage: line right before "Key events:"; System ready; no
# PANIC or FAILED (case-sensitive: boot lines say "panic_exception" and
# "panics"). 1.4 stage 4: between the two lines only the AHCI driver's and
# the block cache's own lines ("ahci: ", "blk: "); without a controller
# none (the two lines next to each other, as in stage 3). 1.4 stage 5: also
# the partition table's and the write guard's ("part: ", "guard: "), which
# come after the attach, still between the two lines and never elsewhere
# (changed: the two prefixes added to both patterns; without a controller
# there are still none of any of the four). 1.4 stage 8 (changed): also
# the ext2 volume's and the mount's ("ext2: ", "mount: "), which come after
# the guard's on a boot whose disk the guard allowed, still between the
# two lines and never elsewhere (the two prefixes added to both patterns).
pcitest_order() {
    awk '$0 == "Filesystem self-test: PASS" { f = NR } /^PCI: / { p = NR } /^Storage: / { s = NR }
         /^Key events: / && !k { k = NR }
         p && !s && NR > p && !/^PCI: / && !/^(ahci|blk|part|guard|ext2|mount): / { other++ }
         /^(ahci|blk|part|guard|ext2|mount): / && (!p || s) { other++ }
         END { exit !(f && p == f + 1 && s > p && k == s + 1 && !other) }' "$1" &&
        grep -qxF "$PCI_READY" "$1" && ! grep -qE 'PANIC|FAILED' "$1"
}
if pcitest_order "$PCI_A" && pcitest_order "$PCI_B" && pcitest_order "$PCI_C" &&
   [ "$(grep -cE '^(ahci|blk|part|guard|ext2|mount): ' "$PCI_A")" -eq 0 ]; then
    pass "[1.4] PCI: on all three boots the PCI: line comes right after 'Filesystem self-test: PASS' and the Storage: line right before 'Key events:', with only the AHCI driver's ahci: lines between them (none without a controller), outside every expect_lines window; System ready; no PANIC or FAILED line"
else
    fail "[1.4] PCI: the PCI: and Storage: lines are not between 'Filesystem self-test: PASS' and 'Key events:', another line comes between them, or a boot did not reach System ready, or a PANIC or FAILED line (see serial-pci-*.log)"
fi
echo "      [1.4] PCI boots: $(( $(date +%s) - PCI_T0 )) s (report only)"

# ---- 1.4 stage 4: AHCI reads and the block cache ----

section "[1.4] AHCI reads and the block cache: read only (stage 4)"
# docs/plans/1.4.md, S4 (D1, D2, D4, D8, D9). Three checks that boot
# nothing: the driver can send no command that writes, proved from the
# source and from madire.elf (the opcode table's two bytes, read back from
# the ELF; no write-type ATA opcode stored as an immediate byte in any
# ahci_, blk_ or pci_ function), and the ELF scan's own self-test on a
# test-only sample that it must flag. Then one -kernel boot with test=diskread
# at -m 128M: ich9-ahci at 00:05.0 with a raw disk of fixed bytes on port
# 0 (tests/disk_image.py pattern: 64 MiB and 3 sectors, so the last page
# is partly filled) and an empty ATAPI CD drive on port 1. Its checks: the
# one configuration write and AHCI mode; IDENTIFY's model and size against
# the image; the CD ignored; the attach's read check and every
# test=diskread read against the CRC32s computed here from the image
# (reads beyond the end end with EIO); the image's SHA-256 the same before
# and after the boot; slot 508 against tests/pagetables.py's model (the
# registers uncached); the lines' place and the boot's health. The read
# speed and the longest turn are report only (risk 26).
AH_T0=$(date +%s)
AH_SRC=$(dirname "$0")/../kernel
# (1) The source. 1.4 stage 10 (changed, the owner's GUARDED-WRITE proof,
# where stage 4 proved no write existed): the opcode table holds exactly
# 0xEC, 0x25, 0x35 (WRITE DMA EXT) and 0xEA (FLUSH CACHE EXT); the FIS's
# command byte is set in one place, from it; the table is read in one
# place (ahci_opcode(op)), inside the one command builder (ahci_command),
# AFTER the builder's guard check refuses what ahci_guard_ok does not
# allow, and ahci_guard_ok refuses every write and flush unless
# ahci_guard_allows(); the builder is called from three places only
# (IDENTIFY, a read or write transfer, a flush); a command header's first
# word is set in one place, the write bit only for WRITE DMA EXT; only
# ahci_core.c issues commands (PxCI); the write and flush operations are
# named nowhere but ahci_core.[ch]. The unit suite ahci_core proves on its
# fake controller that no write or flush command is issued (PxCI never
# written) for a disk the guard refused or did not decide, outside the
# partition, in its first two sectors, or after the disk failed.
AH_TABLE=$(awk '/^static const uint8_t ahci_opcodes\[AHCI_OPS\] __attribute__\(\(used\)\) = \{$/ { on = 1; next }
                on && /^\};$/ { exit } on { print $1 }' "$AH_SRC/storage/ahci_core.c" | tr '\n' ' ')
AH_OPS=$(awk '/^enum ahci_op \{$/ { on = 1; next } on && /^\};$/ { exit } on { print $1 }' "$AH_SRC/storage/ahci_core.h" | tr '\n' ' ')
AH_FIS=$(grep -c 'fis\[2\]' "$AH_SRC/storage/ahci_core.c")
AH_HDR=$(grep -cE 'hdr\[0\] *=' "$AH_SRC/storage/ahci_core.c")
AH_CI=$(grep -rlE 'AHCI_PX_CI\b' "$AH_SRC" | grep -v 'storage/ahci_core\.[ch]$' | tr '\n' ' ')
AH_NAMES=$(grep -rlE 'AHCI_OP_(WRITE|FLUSH)\b' "$AH_SRC" | grep -v 'storage/ahci_core\.[ch]$' | tr '\n' ' ')
# The builder up to the line that reads the table, and the guard's test.
AH_BODY=$(awk '/^static enum ahci_end ahci_command\(/ { on = 1 } on { print } on && /uint8_t code = ahci_opcode\(op\);/ { exit }' \
          "$AH_SRC/storage/ahci_core.c")
AH_GOK=$(awk '/^static bool ahci_guard_ok\(/ { on = 1 } on { print } on && /^}$/ { exit }' "$AH_SRC/storage/ahci_core.c")
AH_BUILDS=$(grep -cE 'ahci_command\(h, p, ' "$AH_SRC/storage/ahci_core.c")
echo "      opcode table: $AH_TABLE; enum ahci_op: $AH_OPS; fis[2] lines: $AH_FIS; hdr[0] lines: $AH_HDR; builder calls: $AH_BUILDS; other files using PxCI: ${AH_CI:-none}; write or flush named elsewhere: ${AH_NAMES:-none}"
if [ "$AH_TABLE" = "0xECu, 0x25u, 0x35u, 0xEAu, " ] &&
   [ "$AH_OPS" = "AHCI_OP_IDENTIFY, AHCI_OP_READ, AHCI_OP_WRITE, AHCI_OP_FLUSH, AHCI_OPS, " ] &&
   [ "$AH_FIS" -eq 1 ] && grep -qxF '    fis[2] = code;' "$AH_SRC/storage/ahci_core.c" &&
   [ "$(grep -cF 'ahci_opcode(op)' "$AH_SRC/storage/ahci_core.c")" -eq 1 ] &&
   grep -qxF '    uint8_t code = ahci_opcode(op);' <<< "$AH_BODY" &&
   grep -qxF '    if (!ahci_guard_ok(h, p, op, lba, count)) {' <<< "$AH_BODY" &&
   grep -qxF '    if (op != AHCI_OP_WRITE && op != AHCI_OP_FLUSH)' <<< "$AH_GOK" &&
   grep -qxF '    if (!ahci_guard_allows(h, p))' <<< "$AH_GOK" && [ "$AH_BUILDS" -eq 3 ] &&
   [ "$AH_HDR" -eq 1 ] &&
   grep -qF 'hdr[0] = AHCI_CFL_H2D | (n << 16) | (op == AHCI_OP_WRITE ? AHCI_HDR_WRITE : 0u);' "$AH_SRC/storage/ahci_core.c" &&
   [ -z "$AH_CI" ] && [ -z "$AH_NAMES" ]; then
    pass "[1.4] AHCI guarded writes (source): the opcode table holds exactly IDENTIFY DEVICE (0xEC), READ DMA EXT (0x25), WRITE DMA EXT (0x35) and FLUSH CACHE EXT (0xEA); it is read once, inside the one command builder, after the builder's guard check, which refuses every write and flush unless ahci_guard_allows(); the builder is called from three places; the FIS's command byte is set once and from the table; the header's write bit only for WRITE DMA EXT; only ahci_core.c issues commands or names a write or flush"
else
    fail "[1.4] AHCI guarded writes (source): the opcode table, the builder's guard check, the FIS's command byte, the header's write bit, the builder's callers or the command issue is not as stage 10 allows (table: $AH_TABLE; ops: $AH_OPS; fis[2]: $AH_FIS; hdr[0]: $AH_HDR; builder calls: $AH_BUILDS; PxCI elsewhere: ${AH_CI:-none}; write or flush elsewhere: ${AH_NAMES:-none})"
fi
# (2) madire.elf: the table's bytes as linked, and no immediate byte store
# of a write-type ATA opcode (ACS-3: the WRITE, FLUSH, DOWNLOAD MICROCODE
# and SECURITY commands) in the storage functions. Not in the list: 0x3F
# (WRITE LOG EXT), which is also '?', stored by ahci_parse_identify for an
# unprintable model character, and 0x06 (DATA SET MANAGEMENT), a small
# number the compiler may store for other reasons; neither can be sent:
# the table above is the only source of an opcode.
# The S4 fix (after runs 41222612792 and 41222637807): an instruction line
# is "<address>:" with or without leading spaces (objdump prints a 64-bit
# kernel address with none, an object file's short one with some), and
# the check cannot pass by finding nothing: it needs at least
# AH_MIN_INSNS instructions in the storage functions and at least one in
# every one of them. AH_MIN_INSNS: S3 and S4 added 20,064 bytes of text
# (2,272 + 17,792; code and read-only data such as the log formats);
# about half of it is instructions, at 3 to 5 bytes each on x86-64, so
# roughly 2,000 to 3,300 are expected; 1,000 still fails a pattern that
# matches nothing or only a few lines, without depending on the
# compiler's exact choices. Then (3) proves the scan finds what it must.
AH_MIN_INSNS=1000
AH_WRITE_OPS='30|31|34|35|36|39|3a|3b|3c|3d|3e|45|57|61|64|92|93|c5|ca|cc|ce|e7|e8|ea|f1|f4|f9'
# ah_table <object or ELF>: sets AH_TBYTES to the bytes of its ahci_opcodes
# as hex ("ec25") and AH_TSIZE to its size from nm (both empty if it has
# no such symbol). It sets globals, so it is called directly, never inside
# $(...) or a pipeline (the S4 runs 41229884288 and 41229897526).
ah_table() {
    local addr size
    read -r addr size <<< "$(nm -S "$1" 2>/dev/null | awk '$4 == "ahci_opcodes" { print $1, $2; exit }')"
    AH_TSIZE=${size:-}
    AH_TBYTES=""
    [ -n "${addr:-}" ] || return 0
    AH_TBYTES=$(objdump -s -j .rodata --start-address=0x$addr --stop-address=$(printf '0x%x' $((16#$addr + 16#${size:-0}))) "$1" 2>/dev/null |
        awk '$1 ~ /^[0-9a-f]+$/ && NF >= 2 { print $2; exit }')
}
# ah_scan <objdump -d output> <file for the flagged lines>: prints "<storage
# functions> <their instructions> <those with none>"; every immediate byte
# store of a write-type opcode in a storage function goes to the file.
ah_scan() {
    : > "$2"
    awk -v badf="$2" -v ops="$AH_WRITE_OPS" '
        /^[0-9a-f]+ <[^>]*>:$/ { if (fn && !cnt) empty++; fn = ($2 ~ /^<(ahci|blk|pci)_/); fns += fn; cnt = 0; next }
        fn && /^[ \t]*[0-9a-f]+:[ \t]/ {
            insns++; cnt++
            if ($0 ~ ("movb[ \t]+[$]0x(" ops "),")) print > badf
        }
        END { if (fn && !cnt) empty++; print fns + 0, insns + 0, empty + 0 }' "$1"
}
AH_ELF=$OUT/madire.elf
ah_table "$AH_ELF"
AH_BYTES=$AH_TBYTES; AH_SIZE=$AH_TSIZE
AH_DIS=$OUT/ahci-objdump.txt
objdump -d --no-show-raw-insn "$AH_ELF" > "$AH_DIS" 2>/dev/null
read -r AH_FNS AH_INSNS AH_EMPTY <<< "$(ah_scan "$AH_DIS" "$OUT/ahci-objdump-flagged.txt")"
AH_BAD=$(cat "$OUT/ahci-objdump-flagged.txt")
echo "      madire.elf: ahci_opcodes ${AH_SIZE:-?} bytes: ${AH_BYTES:-?}; $AH_FNS storage functions, $AH_INSNS instructions examined (minimum $AH_MIN_INSNS), $AH_EMPTY with none; write-type opcode stores: $(grep -c . <<< "$AH_BAD")"
[ -n "$AH_BAD" ] && sed 's/^/      store: /' <<< "$AH_BAD"
# 1.4 stage 10 (changed): the table as linked is the 4 bytes EC 25 35 EA
# (it was EC 25), still the only source of an opcode: the scan below is
# unchanged, so no storage function stores a write-type opcode (0x35 and
# 0xEA included) as an immediate byte; WRITE DMA EXT and FLUSH CACHE EXT
# can only come from the table, through the guarded builder (1).
if [ "${AH_SIZE:-}" = 0000000000000004 ] && [ "$AH_BYTES" = ec2535ea ] && [ "$AH_FNS" -ge 10 ] &&
   [ "$AH_INSNS" -ge "$AH_MIN_INSNS" ] && [ "$AH_EMPTY" -eq 0 ] && [ -z "$AH_BAD" ]; then
    pass "[1.4] AHCI guarded writes (madire.elf): ahci_opcodes is 4 bytes, EC 25 35 EA, as linked; no ahci_, blk_ or pci_ function ($AH_FNS of them, $AH_INSNS instructions examined, at least $AH_MIN_INSNS needed and one in each) stores a write-type ATA opcode as an immediate byte, so a write or a flush can only come from the table through the guarded builder"
else
    fail "[1.4] AHCI guarded writes (madire.elf): ahci_opcodes is not the 4 bytes EC 25 35 EA (${AH_SIZE:-no symbol}: ${AH_BYTES:-none}), too few storage functions or instructions were examined ($AH_FNS functions, $AH_INSNS instructions, minimum $AH_MIN_INSNS, $AH_EMPTY with none), or a storage function stores a write-type opcode"
fi
# (3) The scan's self-test: tests/ahci_opcode_sample.c, compiled here with
# the kernel's code-generation flags (test only, never linked), stores
# WRITE DMA EXT (0x35) and FLUSH CACHE EXT (0xEA) as the driver stores an
# opcode, and READ DMA EXT (0x25) in a third function; its opcode table is
# EC 35. The same ah_table and ah_scan must flag exactly the two stores,
# one in each function, not the read, and read the table as ec35.
AH_SAMPLE=$OUT/ahci-sample.o
rm -f "$AH_SAMPLE"
gcc -std=gnu11 -m64 -ffreestanding -fno-pic -fno-pie -mno-red-zone -mgeneral-regs-only -mcmodel=kernel \
    -fno-stack-protector -fno-asynchronous-unwind-tables -O2 -Wall -Wextra -Werror \
    -c "$(dirname "$0")/ahci_opcode_sample.c" -o "$AH_SAMPLE" 2>&1 | sed 's/^/      gcc: /'
AH_SDIS=$OUT/ahci-sample-objdump.txt
objdump -d --no-show-raw-insn "$AH_SAMPLE" > "$AH_SDIS" 2>/dev/null
ah_table "$AH_SAMPLE"
AH_SBYTES=$AH_TBYTES
read -r AH_SFNS AH_SINSNS AH_SEMPTY <<< "$(ah_scan "$AH_SDIS" "$OUT/ahci-sample-flagged.txt")"
AH_SBAD=$(cat "$OUT/ahci-sample-flagged.txt")
AH_SFLAG=$(awk '/^[0-9a-f]+ <[^>]*>:$/ { fn = $2 } /movb[ \t]+[$]0x(35|ea),/ { print fn, ($0 ~ /[$]0x35,/ ? "0x35" : "0xea") }' "$AH_SDIS" | tr '\n' ' ')
echo "      sample: table ${AH_SBYTES:-?}; $AH_SFNS functions, $AH_SINSNS instructions; flagged: $(grep -c . <<< "$AH_SBAD") ($AH_SFLAG)"
sed 's/^/      flagged: /' <<< "$AH_SBAD"
if [ -s "$AH_SAMPLE" ] && [ "$AH_SBYTES" = ec35 ] && [ "$AH_SFNS" -eq 3 ] && [ "$AH_SEMPTY" -eq 0 ] &&
   [ "$(grep -c . <<< "$AH_SBAD")" -eq 2 ] && grep -qE '[$]0x35,' <<< "$AH_SBAD" && grep -qE '[$]0xea,' <<< "$AH_SBAD" &&
   [ "$AH_SFLAG" = "<ahci_sample_write>: 0x35 <ahci_sample_flush>: 0xea " ] && ! grep -qE '[$]0x25,' <<< "$AH_SBAD"; then
    pass "[1.4] AHCI read-only check, self-test: in a test-only sample compiled with the kernel's flags, the same scan flags the WRITE DMA EXT (0x35) and FLUSH CACHE EXT (0xEA) stores, one in each function, not the READ DMA EXT (0x25) one, and reads its opcode table as EC 35"
else
    fail "[1.4] AHCI read-only check, self-test: the scan did not flag exactly the sample's 0x35 and 0xEA stores, or did not read its table as EC 35 (table ${AH_SBYTES:-none}; $AH_SFNS functions; flagged: $AH_SFLAG)"
fi

# (4) The test=diskread boot.
AH_W=$OUT/ahci-read
rm -rf "$AH_W" && mkdir -p "$AH_W"
AH_IMG=$AH_W/disk.img
AH_LEN=$((64 * 1024 * 1024 + 3 * 512))
AH_N=$((AH_LEN / 512))
disk_image pattern "$AH_IMG" "$AH_LEN"
AH_SHA0=$(sha256sum < "$AH_IMG" | cut -d' ' -f1)
AH_LOG=$OUT/serial-diskread.log
boot_until "$AH_LOG" 120 "$PCI_READY" -m 128M -kernel "$KERNEL" -append "test=diskread" \
    -drive file="$AH_IMG",if=none,id=drtest-disk,format=raw -device ich9-ahci,id=drtest-ahci,addr=0x5 \
    -device ide-hd,drive=drtest-disk,bus=drtest-ahci.0 \
    -drive if=none,id=drtest-cd,media=cdrom -device ide-cd,drive=drtest-cd,bus=drtest-ahci.1
AH_SHA1=$(sha256sum < "$AH_IMG" | cut -d' ' -f1)
grep -E '^(PCI|ahci|blk|part|guard|Storage|diskread): ' "$AH_LOG" | sed 's/^/      /'
# The configuration write and AHCI mode.
AH_CMDV=$(sed -nE 's/^ahci: PCI 00:05\.0 command register 0x[0-9a-f]{4} -> 0x([0-9a-f]{4}) \(memory space on, bus master on, INTx off; read back as written\); the only configuration write$/\1/p' "$AH_LOG")
AH_GHC=$(sed -nE 's/^ahci: AHCI version 0x[0-9a-f]{8}, CAP 0x[0-9a-f]{8}, CAP2 0x[0-9a-f]{8}, 6 ports implemented \(PI 0x0000003f\); BIOS hand-off: (not offered|done); GHC 0x[0-9a-f]{8} -> 0x([0-9a-f]{8}) \(AHCI mode on, interrupts off\); polling$/\2/p' "$AH_LOG")
if [ -n "$AH_CMDV" ] && [ $((16#$AH_CMDV & 0x406)) -eq $((0x406)) ] && [ "$(grep -c 'configuration write' "$AH_LOG")" -eq 1 ] &&
   [ -n "$AH_GHC" ] && [ $((16#$AH_GHC & 0x80000002)) -eq $((0x80000000)) ] &&
   grep -qxF "ahci: controller at 00:05.0 (8086:2922, BAR5 $(sed -nE 's/^ahci: controller at 00:05\.0 \(8086:2922, BAR5 (0x[0-9a-f]{8})\)$/\1/p' "$AH_LOG"))" "$AH_LOG"; then
    pass "[1.4] AHCI: one configuration write, the controller's command register (0x$AH_CMDV: memory space, bus master, INTx off), read back as written; AHCI mode on and interrupts off (GHC 0x$AH_GHC); 6 ports; polling"
else
    fail "[1.4] AHCI: the command register's write line, or the controller's AHCI mode line, is missing or wrong (command ${AH_CMDV:-?}, GHC ${AH_GHC:-?})"
fi
# IDENTIFY against the image; the Storage: line. 1.4 stage 5 (changed): the
# Storage: line also names the partition table and the guard's result; the
# pattern disk has no 0x55AA signature in LBA 0, so "no partition table,
# writes would be REFUSED: no partition table" (the same disk, model and
# size as before; the table and the guard are checked in stage 5's boots).
# 1.4 stage 10 (changed): the IDENTIFY line ends "used (written only as
# the write guard allows)" where it said "used read only" (logged before
# the guard decides; the same disk, model and size).
AH_STORAGE="Storage: AHCI 00:05.0, port 0 disk QEMU HARDDISK 64 MiB, no partition table, writes would be REFUSED: no partition table; read only, not mounted (Live mode)"
if grep -qxF "ahci: port 0: signature 0x00000101 (ATA disk); IDENTIFY DEVICE: model 'QEMU HARDDISK', $AH_N sectors of 512 bytes (64 MiB), LBA48 yes, DMA yes; used (written only as the write guard allows)" "$AH_LOG" &&
   grep -qxF "$AH_STORAGE" "$AH_LOG" &&
   [ "$(grep -c '^Storage: ' "$AH_LOG")" -eq 1 ]; then
    pass "[1.4] AHCI: IDENTIFY DEVICE on port 0 gives QEMU's model 'QEMU HARDDISK' and the image's $AH_N sectors of 512 bytes (LBA48, DMA); '$AH_STORAGE'"
else
    fail "[1.4] AHCI: the IDENTIFY line or the Storage: line differs from the image's model and size ($AH_N sectors)"
fi
# The CD on port 1: ignored, never sent a command; the other ports empty.
if grep -qxF "ahci: port 1: signature 0xeb140101: ATAPI device, ignored (no command sent)" "$AH_LOG" &&
   grep -qxF "ahci: 4 of 6 implemented ports empty (no device)" "$AH_LOG" &&
   [ "$(grep -cE '^ahci: port [2-9]' "$AH_LOG")" -eq 0 ]; then
    pass "[1.4] AHCI: the ATAPI CD drive on port 1 is found by its signature (0xEB140101) and ignored, never sent a command; ports 2-5 empty"
else
    fail "[1.4] AHCI: the ATAPI drive on port 1 was not found and ignored, or the other ports are not reported empty"
fi
# The CRC32s, computed here from the image.
AH_RUNS="0:1 $((AH_N - 1)):1"
while read -r l c; do AH_RUNS="$AH_RUNS $l:$c"; done < <(sed -nE 's/^diskread: read [0-9]+: LBA ([0-9]+), ([0-9]+) sectors: .*/\1 \2/p' "$AH_LOG")
AH_HOST=$(disk_image sectors "$AH_IMG" $AH_RUNS)
sed 's/^/      host: /' <<< "$AH_HOST"
AH_C0=$(sed -n '1s/.* crc=\(0x[0-9a-f]*\)$/\1/p' <<< "$AH_HOST"); AH_C1=$(sed -n '2s/.* crc=\(0x[0-9a-f]*\)$/\1/p' <<< "$AH_HOST")
if [ -n "$AH_C0" ] && [ -n "$AH_C1" ] &&
   grep -qE "^blk: read check: LBA 0 CRC32 $AH_C0, LBA $((AH_N - 1)) CRC32 $AH_C1 \(2 commands, [0-9]+ ticks\)$" "$AH_LOG"; then
    pass "[1.4] block cache: the attach's read check reads the disk's first and last sector with the image's CRC32s ($AH_C0, $AH_C1), one command each"
else
    fail "[1.4] block cache: the attach's read check is missing or its CRC32s differ from the image's (${AH_C0:-?}, ${AH_C1:-?})"
fi
# Every test=diskread read: the planned sectors; each read inside the disk
# with the image's CRC32, each beyond the end EIO.
AH_WANT="0 1|$((AH_N / 2)) 1|$((AH_N - 1)) 1|8 128|1001 300|4096 8192|0 1|$((AH_N - 11)) 11|$((AH_N - 3)) 3|8 128|$AH_N 1|$((AH_N - 2)) 4|281474976710655 1"
AH_GOT=$(sed -nE 's/^diskread: read [0-9]+: LBA ([0-9]+), ([0-9]+) sectors: .*/\1 \2/p' "$AH_LOG" | paste -sd'|')
AH_BADCRC=0
AH_I=0
while read -r line; do
    AH_I=$((AH_I + 1))
    h=$(sed -n "$((AH_I + 2))p" <<< "$AH_HOST")
    case "$h" in
    *beyond) grep -qE ': EIO \(.*\), as expected$' <<< "$line" || AH_BADCRC=$((AH_BADCRC + 1)) ;;
    *crc=*) grep -qF "CRC32 ${h##*crc=} (" <<< "$line" && ! grep -q 'NOT EXPECTED' <<< "$line" || AH_BADCRC=$((AH_BADCRC + 1)) ;;
    *) AH_BADCRC=$((AH_BADCRC + 1)) ;;
    esac
done < <(grep -E '^diskread: read [0-9]+: ' "$AH_LOG")
if [ "$AH_GOT" = "$AH_WANT" ] && [ "$AH_I" -eq 13 ] && [ "$AH_BADCRC" -eq 0 ] &&
   grep -qxF "diskread: DONE (13 reads; 3 beyond the end ended with EIO, as expected)" "$AH_LOG"; then
    pass "[1.4] test=diskread: the first, middle and last sector, 16 pages in one command, an unaligned run, 4 MiB (twice the cache), the first sector again, the partly filled last page and the 16 pages again all read with the image's CRC32s; the 3 reads beyond the end end with EIO, no panic"
else
    fail "[1.4] test=diskread: the reads ($AH_GOT) are not the planned ones, $AH_BADCRC of $AH_I differ from the image or from the expected EIO, or no DONE line"
fi
# The image unchanged by the boot.
echo "      disk.img SHA-256 before $AH_SHA0, after $AH_SHA1"
if [ -n "$AH_SHA0" ] && [ "$AH_SHA0" = "$AH_SHA1" ] && [ "$(stat -c%s "$AH_IMG")" -eq "$AH_LEN" ]; then
    pass "[1.4] AHCI read only: the test disk's SHA-256 is the same before and after the test=diskread boot ($AH_SHA0)"
else
    fail "[1.4] AHCI read only: the test disk's SHA-256 changed during the test=diskread boot ($AH_SHA0 -> ${AH_SHA1:-?})"
fi
# Slot 508 against the model: the registers uncached; after the start and
# after the reads.
AH_PT=$(python3 "$(dirname "$0")/pagetables.py" "$AH_LOG" --label diskread)
grep -E '^PAGETABLES_(RESULT|STORAGE)|storage' <<< "$AH_PT" | sed 's/^/      /'
if grep -qE '^PAGETABLES_RESULT .* MATCH$' <<< "$AH_PT" &&
   grep -qE '^PAGETABLES_STORAGE label=diskread at=start registers=1 dma=2 cache=2 computed=([0-9]+/[0-9]+) logged=\1 MATCH$' <<< "$AH_PT" &&
   grep -qE '^PAGETABLES_STORAGE label=diskread at=diskread registers=1 dma=2 cache=([0-9]+) computed=([0-9]+/[0-9]+) logged=\2 MATCH$' <<< "$AH_PT" &&
   grep -qE '^diskread: cache: 512 of 512 pages made, [0-9]+ pages from the cache, [0-9]+ read, [0-9]+ commands, [1-9][0-9]* replaced, 3 reads refused \(beyond the end\), 0 errors$' "$AH_LOG"; then
    pass "[1.4] slot 508: tests/pagetables.py models the registers (uncached: PCD and PWT in their entry), 2 DMA pages and the cache's pages (2 after the read check, 512 after the reads, pages replaced least recently used first): tables and pages equal the kernel's walk"
else
    fail "[1.4] slot 508: the model differs from the kernel's walk, the registers are not mapped uncached, or the cache did not fill and replace as expected: $(grep -E '^PAGETABLES_(RESULT|STORAGE)' <<< "$AH_PT" | tr '\n' ' ')"
fi
# The lines' place and the boot's health.
if pcitest_order "$AH_LOG" && grep -qE '^diskread: speed \(report only\): ' "$AH_LOG" &&
   grep -qE '^diskread: longest turn \(report only\): ' "$AH_LOG" && ! grep -qE '^ahci: port [0-9]+: .* failed: ' "$AH_LOG"; then
    pass "[1.4] test=diskread boot: the driver's lines between the PCI: and Storage: lines, no disk error, the speed and turn lines, System ready; no PANIC or FAILED line"
else
    fail "[1.4] test=diskread boot: the lines are out of place, a disk error was logged, a figure line is missing, or the boot did not reach System ready cleanly (see serial-diskread.log)"
fi
echo "      [1.4] $(grep -E '^diskread: (speed|longest turn)' "$AH_LOG" | tr '\n' ' ')"
echo "      [1.4] AHCI reads: $(( $(date +%s) - AH_T0 )) s (report only)"

# ---- 1.4 stage 5: partition tables and the write guard ----

section "[1.4] Partition tables and the write guard: read only (stage 5)"
# docs/plans/1.4.md, S5 (D5, D10, C4, C6, C9). Eight -kernel boots at
# -m 128M to System ready, no test= option (every boot with a disk reads
# its partition table and decides the guard at attach), each with
# ich9-ahci at 00:05.0 and one raw disk on port 0. The disks: an 8 MiB
# ext2 filesystem made by mke2fs (tests/disk-lib.sh, label madire-test) in
# an MBR disk and in a GPT disk, both with the Madire test marker
# (tests/disk_image.py), so writes would be ALLOWED; copies of the MBR
# disk with one thing changed (disk_image.py variant): no marker, a bad
# marker CRC32, a wrong label, the marker's start not the partition's;
# the marked MBR disk with a model that is not a virtual disk's (ide-hd
# model=); and an empty disk of zero bytes (no partition table: the path
# of MadireOS-Test's resting "empty" disk). Each boot: (1) its part:,
# guard: and Storage: lines exactly as expected from the image
# (disk_image.py verify gives the partition, the GPT fields and the
# marker's CRC32s), no more part: or guard: lines, no disk error; the
# guard's line names the first failed condition; (2) the image's SHA-256
# the same before and after the boot. Then one check of the eight boots'
# line order and health. Stage 5 has no write command (the read-only
# checks above still hold); the hashes show no boot changed its disk.
GD_T0=$(date +%s)
GD_W=$OUT/guard-disks
rm -rf "$GD_W" && mkdir -p "$GD_W"
declare -A GD_SHA0=() GD_SHA1=()
GD_OK=1
disk_mkfs "$GD_W/fs.img" 8192 1024 128 256 "" > "$GD_W/mkfs.txt" 2>&1 || GD_OK=0
disk_image mbr "$GD_W/fs.img" "$GD_W/mbr.img" || GD_OK=0
disk_image gpt "$GD_W/fs.img" "$GD_W/gpt.img" || GD_OK=0
for v in nomarker badcrc label=other-disk start; do
    disk_image variant "$GD_W/mbr.img" "$v" "$GD_W/mbr-${v%%=*}.img" > "$GD_W/variant-${v%%=*}.txt" || GD_OK=0
    sed 's/^/      /' "$GD_W/variant-${v%%=*}.txt"
done
truncate -s 16M "$GD_W/empty.img" || GD_OK=0
GD_VM=$(disk_image verify "$GD_W/mbr.img")
GD_VG=$(disk_image verify "$GD_W/gpt.img")
GD_VC=$(disk_image verify "$GD_W/mbr-badcrc.img")
sed 's/^/      mbr: /' <<< "$GD_VM"
sed 's/^/      gpt: /' <<< "$GD_VG"
read -r GD_S GD_N <<< "$(sed -nE 's/^TABLE mbr start=([0-9]+) sectors=([0-9]+) .*/\1 \2/p' <<< "$GD_VM")"
read -r GD_GS GD_GN GD_FU GD_LU <<< "$(sed -nE 's/^TABLE gpt start=([0-9]+) sectors=([0-9]+) .* first_usable=([0-9]+) last_usable=([0-9]+)$/\1 \2 \3 \4/p' <<< "$GD_VG")"
read -r GD_HC GD_AC <<< "$(sed -nE 's/^GPT header_crc=(0x[0-9a-f]{8}) .* entries_crc=(0x[0-9a-f]{8}) .*/\1 \2/p' <<< "$GD_VG")"
GD_ALT=$(sed -nE 's/^GPT backup at=([0-9]+) .*/\1/p' <<< "$GD_VG")
read -r GD_CS GD_CO <<< "$(sed -nE 's/^MARKER .* crc=(0x[0-9a-f]{8}) own=(0x[0-9a-f]{8}) .*/\1 \2/p' <<< "$GD_VC")"
[ -n "${GD_S:-}" ] && [ -n "${GD_GS:-}" ] && [ -n "${GD_HC:-}" ] && [ -n "$GD_ALT" ] && [ -n "${GD_CS:-}" ] || GD_OK=0
GD_MIB_M=$(( $(stat -c%s "$GD_W/mbr.img") / 512 >> 11 ))
GD_MIB_G=$(( $(stat -c%s "$GD_W/gpt.img") / 512 >> 11 ))
echo "      images: ok=$GD_OK; MBR partition $GD_S+$GD_N (${GD_MIB_M} MiB disk); GPT partition $GD_GS+$GD_GN, usable $GD_FU-$GD_LU, CRC32s $GD_HC $GD_AC, backup at $GD_ALT (${GD_MIB_G} MiB disk); bad marker CRC32 $GD_CS (computed $GD_CO)"

# gd_boot <name> <image> [model]: one boot into serial-guard-<name>.log, the
# image's SHA-256 before and after.
# 1.4 stage 10 (changed helper): the boot runs on a copy, <name>-after.img
# (the hashes are the copy's, before and after: the image booted, as
# before), so the images stay pristine for the later sections: the marked
# disks are now written by their read-write mount (save 0).
gd_boot() {
    local name=$1 img=$2 model=${3:-} log=$OUT/serial-guard-$1.log run=$GD_W/$1-after.img
    cp "$img" "$run"
    GD_SHA0[$name]=$(sha256sum < "$run" | cut -d' ' -f1)
    # 1.4 stage 12 (changed, the owner's decision): test=nohome, so the
    # read-write mounts make no home folders (D18: none on a test= boot);
    # the diffs below stay save 0's alone.
    boot_until "$log" 60 "$PCI_READY" -m 128M -kernel "$KERNEL" -append "test=nohome" \
        -drive file="$run",if=none,id=gdtest-disk,format=raw -device ich9-ahci,id=gdtest-ahci,addr=0x5 \
        -device "ide-hd,drive=gdtest-disk,bus=gdtest-ahci.0${model:+,model=$model}"
    GD_SHA1[$name]=$(sha256sum < "$run" | cut -d' ' -f1)
    grep -E '^(part|guard|Storage): ' "$log" | sed "s/^/      $name: /"
}
# gd_lines <name> <expected lines>: each expected line exactly once; as many
# part: and guard: lines as expected, one Storage: line, no disk error.
gd_lines() {
    local log=$OUT/serial-guard-$1.log l np ng
    [ "$GD_OK" -eq 1 ] && [ -f "$log" ] || return 1
    while IFS= read -r l; do
        [ "$(grep -cxF -- "$l" "$log")" -eq 1 ] || { echo "      $1: missing or repeated: $l"; return 1; }
    done <<< "$2"
    np=$(grep -c '^part: ' <<< "$2"); ng=$(grep -c '^guard: ' <<< "$2")
    [ "$(grep -c '^part: ' "$log")" -eq "$np" ] && [ "$(grep -c '^guard: ' "$log")" -eq "$ng" ] &&
        [ "$(grep -c '^Storage: ' "$log")" -eq 1 ] && ! grep -qE '^ahci: port [0-9]+: .* failed: ' "$log"
}
# gd_hash <name> <what>: the counted hash check of one boot.
gd_hash() {
    echo "      $1: SHA-256 before ${GD_SHA0[$1]:-?}, after ${GD_SHA1[$1]:-?}"
    if [ -n "${GD_SHA0[$1]:-}" ] && [ "${GD_SHA0[$1]}" = "${GD_SHA1[$1]:-}" ]; then
        pass "[1.4] write guard, $2: the disk image's SHA-256 is the same before and after the boot (${GD_SHA0[$1]})"
    else
        fail "[1.4] write guard, $2: the disk image's SHA-256 changed during the boot (${GD_SHA0[$1]:-?} -> ${GD_SHA1[$1]:-?})"
    fi
}
# 1.4 stage 10 (changed): the decision lines end with what the decision now
# does ("the AHCI command builder checks this decision before every write
# and flush" / "refuses every write and flush to this disk") where they
# said "no write command exists in stage 5"; the decisions are the same.
GD_DEC_ALLOW="guard: port 0: decided once, kept by the AHCI driver: writes would be ALLOWED on partition 1 (LBA %s, %s sectors); the AHCI command builder checks this decision before every write and flush"
GD_DEC_REFUSE="guard: port 0: decided once, kept by the AHCI driver: writes would be REFUSED; the AHCI command builder refuses every write and flush to this disk"
GD_PART_MBR="part: port 0: MBR (disk signature 0x4d414452), 1 partition
part: port 0: partition 1: MBR type 0x83 (Linux), LBA $GD_S, $GD_N sectors"
GD_ST_MBR="Storage: AHCI 00:05.0, port 0 disk QEMU HARDDISK $GD_MIB_M MiB, MBR, 1 partition, writes would be"
GD_TAIL="; read only, not mounted (Live mode)"
# 1.4 stage 8 (changed): a disk the guard allows has its ext2 volume
# mounted read-only on /home (D16), so the two marked disks' Storage:
# lines end so (the refused disks' lines are unchanged, GD_TAIL).
# 1.4 stage 10 (changed): mounted read-write, "Disk mode" (D16).
GD_TAIL_MOUNTED="; ext2 'madire-test' mounted read-write on /home (Disk mode)"
# gd_bounded <name> <what> <image before> <partition's first sector>
# <bytes allowed after the superblock>: 1.4 stage 10 (changed, the owner's
# bounded diff, where stage 5 checked the SHA-256 unchanged): a read-write
# mount writes save 0 (the "not clean" state) at once, so the image may
# differ from the one before the boot ONLY inside the partition's
# superblock (partition bytes 1024-2047), and with <bytes> > 0 in the
# <bytes> after it (the group descriptors a D12 recount rewrites); the
# partition table and the marker's sector are byte-identical; and e2fsck
# -fn on the partition after the boot reports nothing.
# The owner's FIELD-LEVEL addition (approved with the S10 changed checks):
# inside the superblock, only the bytes of the fields save 0 writes may
# differ: the state (58-59), the last mount time (44-47), the last write
# time (48-51) and the mount count (52-53; allowed, not written by Madire);
# the free block and inode counts (12-19) are written too (the superblock
# is written whole), so they must keep their values except after D12's
# recount ('dirty': <bytes> > 0); in the descriptor block only each
# 32-byte descriptor's free blocks, free inodes and folders (its bytes
# 12-17), which the recount fixes. dumpe2fs -h before and after must also
# differ only in those fields (Filesystem state, Mount count, Last mount
# time, Last write time; for 'dirty' also Free blocks and Free inodes).
gd_bounded() {
    local name=$1 what=$2 before=$3 start=$4 extra=$5 img=$GD_W/$1-after.img lo hi offs out bad fsck rc fields fbad allow
    lo=$((start * 512 + 1024)); hi=$((lo + 1024 + extra))
    offs=$(cmp -l "$before" "$img" 2>/dev/null | awk '{ print $1 - 1 }')
    bad=$(awk -v lo="$lo" -v hi="$hi" -v ex="$extra" '{ r = $1 - lo }
            r < 0 || $1 >= hi { print; next }
            r < 1024 { if ((r >= 44 && r <= 53) || r == 58 || r == 59 || (ex > 0 && r >= 12 && r <= 19)) next; print; next }
            { d = (r - 1024) % 32; if (d >= 12 && d <= 17) next; print }' <<< "$offs" | grep -c .)
    disk_image extract "$before" "$GD_W/$1-before-part.img" > /dev/null 2>&1
    disk_image extract "$img" "$GD_W/$1-after-part.img" > /dev/null 2>&1
    fields=$(diff <(LC_ALL=C "$(disk_tool dumpe2fs)" -h "$GD_W/$1-before-part.img" 2>/dev/null) \
                  <(LC_ALL=C "$(disk_tool dumpe2fs)" -h "$GD_W/$1-after-part.img" 2>/dev/null) |
             sed -nE 's/^[<>] ([^:]+):.*/\1/p' | LC_ALL=C sort -u | tr '\n' ',' | sed 's/,$//')
    allow='^(Filesystem state|Mount count|Last mount time|Last write time)$'
    [ "$extra" -gt 0 ] && allow='^(Filesystem state|Mount count|Last mount time|Last write time|Free blocks|Free inodes)$'
    fbad=$(tr ',' '\n' <<< "$fields" | grep . | grep -vE "$allow" | tr '\n' ',' | sed 's/,$//')
    out=$(LC_ALL=C E2FSCK_CONFIG=/dev/null "$(disk_tool e2fsck)" -fn "$GD_W/$1-after-part.img" 2>&1); rc=$?
    fsck=$(grep -vE '^(e2fsck [0-9]+\.[0-9]+.*|Pass [1-5]: .*|madire-test: [0-9]+/[0-9]+ files \(.*\), [0-9]+/[0-9]+ blocks|)$' <<< "$out")
    echo "      $name: $(grep -c . <<< "$offs") bytes changed, $bad outside the allowed fields; dumpe2fs -h fields changed: ${fields:-none}${fbad:+ (NOT allowed: $fbad)}; e2fsck -fn exit $rc${fsck:+: $(tr '\n' '|' <<< "$fsck")}"
    if [ -s "$img" ] && [ "$bad" -eq 0 ] && [ -z "$fbad" ] && [ -n "$fields" ] &&
       [ "$(stat -c%s "$img")" -eq "$(stat -c%s "$before")" ] && [ "$rc" -eq 0 ] && [ -z "$fsck" ]; then
        pass "[1.4] $what: the read-write mount changed only the superblock's state, mount and write times (save 0)$([ "$extra" -gt 0 ] && echo ", its free counts and the descriptors' per-group counts (the D12 recount)"), byte for byte and field by field (dumpe2fs -h: $fields); the partition table and the marker's sector are byte-identical; e2fsck -fn reports nothing"
    else
        fail "[1.4] $what: the image changed outside the allowed fields ($bad bytes; dumpe2fs -h fields: ${fields:-none}; not allowed: ${fbad:-none}), or e2fsck -fn reported something (exit $rc)"
    fi
}

# (1) The marked MBR disk: ALLOWED.
gd_boot mbr "$GD_W/mbr.img"
if gd_lines mbr "$GD_PART_MBR
guard: port 0 partition 1: writes would be ALLOWED (virtual disk 'QEMU HARDDISK'; test marker for LBA $GD_S, $GD_N sectors; ext2 label 'madire-test')
$(printf "$GD_DEC_ALLOW" "$GD_S" "$GD_N")
$GD_ST_MBR ALLOWED on partition 1$GD_TAIL_MOUNTED"; then
    pass "[1.4] write guard, the marked MBR disk: the MBR's one Linux partition (LBA $GD_S, $GD_N sectors) read as the image holds it; all three conditions hold (QEMU HARDDISK, the marker matching the partition, label madire-test): writes would be ALLOWED on partition 1, decided once and kept by the AHCI driver; the Storage: line says so"
else
    fail "[1.4] write guard, the marked MBR disk: the part:, guard: or Storage: lines differ from the image's (see serial-guard-mbr.log)"
fi
gd_bounded mbr "write guard, the marked MBR disk" "$GD_W/mbr.img" "$GD_S" 0
# (2) The marked GPT disk: ALLOWED; the backup header checked and reported.
gd_boot gpt "$GD_W/gpt.img"
if gd_lines gpt "part: port 0: GPT (protective MBR 0xEE): revision 1.0, header 92 bytes, 128 entries of 128 bytes at LBA 2, usable LBA $GD_FU-$GD_LU, header and entry-array CRC32 valid ($GD_HC, $GD_AC), 1 partition
part: port 0: partition 1: GPT Linux filesystem data, LBA $GD_GS, $GD_GN sectors, name 'madire-test'
part: port 0: GPT backup header at LBA $GD_ALT: valid (report only)
guard: port 0 partition 1: writes would be ALLOWED (virtual disk 'QEMU HARDDISK'; test marker for LBA $GD_GS, $GD_GN sectors; ext2 label 'madire-test')
$(printf "$GD_DEC_ALLOW" "$GD_GS" "$GD_GN")
Storage: AHCI 00:05.0, port 0 disk QEMU HARDDISK $GD_MIB_G MiB, GPT, 1 partition, writes would be ALLOWED on partition 1$GD_TAIL_MOUNTED"; then
    pass "[1.4] write guard, the marked GPT disk: the protective MBR, the GPT header and its entry array with their CRC32s as tests/disk_image.py wrote them ($GD_HC, $GD_AC), the Linux filesystem data partition (LBA $GD_GS, $GD_GN sectors, name madire-test) and the backup header at LBA $GD_ALT valid (report only); writes would be ALLOWED on partition 1"
else
    fail "[1.4] write guard, the marked GPT disk: the part:, guard: or Storage: lines differ from the image's (see serial-guard-gpt.log)"
fi
gd_bounded gpt "write guard, the marked GPT disk" "$GD_W/gpt.img" "$GD_GS" 0
# (3) No marker: REFUSED at (b).
gd_boot nomarker "$GD_W/mbr-nomarker.img"
if gd_lines nomarker "$GD_PART_MBR
guard: port 0 partition 1: writes would be REFUSED: (b) no Madire test marker (MADIRE-TEST-DISK) in the partition's first sector
$GD_DEC_REFUSE
$GD_ST_MBR REFUSED: (b) no valid test marker$GD_TAIL"; then
    pass "[1.4] write guard, an unmarked ext2 disk: writes would be REFUSED at condition (b), no Madire test marker in the partition's first sector"
else
    fail "[1.4] write guard, an unmarked ext2 disk: not refused at (b) as expected (see serial-guard-nomarker.log)"
fi
gd_hash nomarker "the unmarked disk"
# (4) A bad marker CRC32: REFUSED at (b), both values named.
gd_boot badcrc "$GD_W/mbr-badcrc.img"
if gd_lines badcrc "$GD_PART_MBR
guard: port 0 partition 1: writes would be REFUSED: (b) test marker CRC32 $GD_CS does not match its record (computed $GD_CO)
$GD_DEC_REFUSE
$GD_ST_MBR REFUSED: (b) no valid test marker$GD_TAIL"; then
    pass "[1.4] write guard, a marker with a bad CRC32: writes would be REFUSED at condition (b), the stored CRC32 $GD_CS and the computed $GD_CO (as tests/disk_image.py's own and zlib's) named"
else
    fail "[1.4] write guard, a marker with a bad CRC32: not refused at (b) with the image's values (see serial-guard-badcrc.log)"
fi
gd_hash badcrc "the bad marker CRC32"
# (5) A wrong label: REFUSED at (c).
gd_boot label "$GD_W/mbr-label.img"
if gd_lines label "$GD_PART_MBR
guard: port 0 partition 1: writes would be REFUSED: (c) ext2 label 'other-disk', not 'madire-test'
$GD_DEC_REFUSE
$GD_ST_MBR REFUSED: (c) not labelled madire-test$GD_TAIL"; then
    pass "[1.4] write guard, a valid marker and the ext2 label 'other-disk': writes would be REFUSED at condition (c)"
else
    fail "[1.4] write guard, a wrong ext2 label: not refused at (c) as expected (see serial-guard-label.log)"
fi
gd_hash label "the wrong label"
# (6) A model that is not a virtual disk's (the marked disk): REFUSED at (a).
gd_boot model "$GD_W/mbr.img" MADIRE-REAL-DISK-TEST
if gd_lines model "ahci: port 0: signature 0x00000101 (ATA disk); IDENTIFY DEVICE: model 'MADIRE-REAL-DISK-TEST', $((GD_S + GD_N)) sectors of 512 bytes ($GD_MIB_M MiB), LBA48 yes, DMA yes; used (written only as the write guard allows)
$GD_PART_MBR
guard: port 0: writes would be REFUSED: (a) model 'MADIRE-REAL-DISK-TEST' is not QEMU HARDDISK or VBOX HARDDISK
$GD_DEC_REFUSE
Storage: AHCI 00:05.0, port 0 disk MADIRE-REAL-DISK-TEST $GD_MIB_M MiB, MBR, 1 partition, writes would be REFUSED: (a) not a virtual disk$GD_TAIL"; then
    pass "[1.4] write guard, the marked disk with the model 'MADIRE-REAL-DISK-TEST' (ide-hd model=): writes would be REFUSED at condition (a), before its partitions are examined, though its marker and label are valid"
else
    fail "[1.4] write guard, a model that is not a virtual disk's: not refused at (a) as expected (see serial-guard-model.log)"
fi
gd_hash model "the non-virtual model"
# (7) The marker's start not the partition's (its CRC32 valid): REFUSED at (b).
gd_boot start "$GD_W/mbr-start.img"
if gd_lines start "$GD_PART_MBR
guard: port 0 partition 1: writes would be REFUSED: (b) test marker start LBA $((GD_S + 1)), the partition starts at LBA $GD_S
$GD_DEC_REFUSE
$GD_ST_MBR REFUSED: (b) no valid test marker$GD_TAIL"; then
    pass "[1.4] write guard, a marker whose start LBA ($((GD_S + 1))) is not the partition's ($GD_S), its CRC32 valid: writes would be REFUSED at condition (b)"
else
    fail "[1.4] write guard, the marker's start not the partition's: not refused at (b) as expected (see serial-guard-start.log)"
fi
gd_hash start "the marker's start mismatch"
# (8) The empty disk: no partition table, REFUSED.
gd_boot empty "$GD_W/empty.img"
if gd_lines empty "part: port 0: no partition table (LBA 0 has no 0x55AA signature)
guard: port 0: writes would be REFUSED: no partition table
$GD_DEC_REFUSE
Storage: AHCI 00:05.0, port 0 disk QEMU HARDDISK 16 MiB, no partition table, writes would be REFUSED: no partition table$GD_TAIL"; then
    pass "[1.4] write guard, an empty disk of zero bytes (as MadireOS-Test's resting disk): no partition table, writes would be REFUSED, Live mode"
else
    fail "[1.4] write guard, the empty disk: the lines differ from 'no partition table' and REFUSED (see serial-guard-empty.log)"
fi
gd_hash empty "the empty disk"
# The eight boots' line order and health.
GD_BAD=""
for n in mbr gpt nomarker badcrc label model start empty; do
    pcitest_order "$OUT/serial-guard-$n.log" || GD_BAD="$GD_BAD $n"
done
if [ -z "$GD_BAD" ]; then
    pass "[1.4] write guard boots: on all eight the part: and guard: lines come with the driver's own between the PCI: and Storage: lines (Storage: right before 'Key events:'), System ready; no PANIC or FAILED line"
else
    fail "[1.4] write guard boots: lines out of place, no System ready, or a PANIC or FAILED line in:$GD_BAD"
fi
echo "      [1.4] partition and guard boots: $(( $(date +%s) - GD_T0 )) s (report only)"

# ---- 1.4 stage 6: the ext2 read core ----

section "[1.4] The ext2 read core: tests/ext2dump.c against debugfs, the feature policy, corrupted images (stage 6)"
# docs/plans/1.4.md, S6 (D11, D12, C7; risks 25, 31, 37). The core
# (kernel/fs/ext2_core.c) is pure and not linked into the kernel. (1)
# Compiled with the kernel's flags it needs no symbol (no libgcc or libc
# helper) and uses no SSE/x87 register, and madire.elf holds none of its
# symbols. (2-8) ext2 images made by mke2fs (tests/disk-lib.sh,
# tests/mke2fs.conf) from the fixed sample trees of tests/ext2_view.py: 1, 2
# and 4 KiB blocks, 128- and 256-byte inodes, revision 1 and revision 0, a
# sparse file above 2 GiB, a folder indexed by e2fsck -D (dir_index), files
# with an attribute block (debugfs ea_set; ext_attr); resize_inode is in
# every revision-1 image. For each, tests/ext2dump.c (built from the core)
# must list exactly what debugfs reads (tests/ext2_view.py view): every path
# with its inode number, mode, size, link count, flags, attribute block and
# the SHA-256 of its contents (of a link's target), and the same superblock
# figures. (9-11) The feature policy (D11) on copies of the 1 KiB image with
# one superblock field changed: an unknown INCOMPAT flag is not mounted, an
# unknown RO_COMPAT flag is read only (and still read exactly), the clean
# and error states are reported (D12 decides at the mount, stage 8). (12)
# ext2dump built again with -fsanitize=address,undefined over the fixed set
# of corrupted copies of a small image (tests/ext2_view.py corrupt: seeded
# bit flips and chosen bad values in the superblock, the group descriptors,
# the inode table, folder blocks and indirect blocks): no sanitizer report,
# no crash, no hang (10 s each), no read outside the image; only the exit
# codes 0, 1 or 2. Nothing boots; everything stays under $OUT/ext2-core.
E2_T0=$(date +%s)
E2_ROOT=$(dirname "$0")/..
E2_W=$OUT/ext2-core
rm -rf "$E2_W" && mkdir -p "$E2_W"
E2_DUMP=$OUT/ext2dump
E2_PY=(python3 -I -B "$(dirname "$0")/ext2_view.py")

# (1) The core with the kernel's flags, not linked.
E2_OBJ=$OUT/kernel/fs/ext2_core.o
E2_UNDEF=$(nm -u "$E2_OBJ" 2>/dev/null | awk '{ print $2 }' | tr '\n' ' ' | sed 's/ *$//')
E2_SSE=$(objdump -d "$E2_OBJ" 2>/dev/null | grep -cE '%(x|y|z)mm[0-9]|%st(\(|$)|%mm[0-9]')
# 1.4 stage 8 (changed): the core is linked now (ext2 mounted read-only at
# /home), so madire.elf must hold its public functions (ext2_mount,
# ext2_read, ext2_dir_next, ext2_inode_get), where stage 6 checked it
# held none; the object's own checks are unchanged.
E2_INELF=$(nm "$OUT/madire.elf" 2>/dev/null | grep -cE ' ext2_')
E2_LINKED=$(nm "$OUT/madire.elf" 2>/dev/null | grep -cE ' T ext2_(mount|read|dir_next|inode_get)$')
echo "      ext2_core.o: text $(size "$E2_OBJ" 2>/dev/null | awk 'NR == 2 { print $1 }') bytes (report only); undefined: ${E2_UNDEF:-none}; SSE/x87: $E2_SSE; ext2_ symbols in madire.elf: $E2_INELF (the four public read calls: $E2_LINKED)"
if [ -s "$E2_OBJ" ] && [ -z "$E2_UNDEF" ] && [ "$E2_SSE" -eq 0 ] && [ "$E2_LINKED" -eq 4 ] && [ -x "$E2_DUMP" ]; then
    pass "[1.4] ext2 read core: kernel/fs/ext2_core.c compiled with the kernel's flags needs no symbol (no libgcc or libc helper) and uses no SSE/x87 register; madire.elf holds its four public read calls (linked, stage 8); tests/ext2dump.c is built from it"
else
    fail "[1.4] ext2 read core: the object is missing, needs symbols (${E2_UNDEF:-none}), uses SSE/x87 ($E2_SSE), madire.elf lacks the core's read calls ($E2_LINKED of 4), or no ext2dump"
fi
# 1.4 stage 8 (added, the owner's read-only proof): madire.elf held no
# ext2 write-type symbol but the adapter's refusing ops. 1.4 stage 10
# (changed, the owner's GUARDED-WRITE proof, now that ext2 writes): in
# madire.elf, every reference (call or jump) to each function on the way
# to a disk write comes only from the function one level up, so the ONLY
# path from the filesystem to the disk is ext2_d_write / ext2_d_flush (the
# block layer's write-back calls, ext2_disk.c) -> blk_write / blk_flush
# (blk.c) -> ahci_disk_write / ahci_disk_flush (ahci.c) -> ahci_write /
# ahci_flush (ahci_core.c, into the guarded builder, source check above);
# besides it only test=diskwrite's refused-disk attempt (ahci_write_attempt,
# from disktest.c's dt_refused or the function it was inlined into) may
# reach the driver's calls, and it must be refused (its boots below). Each
# function on the path must be in madire.elf.
E2_WDIS=$OUT/ext2-write-path.txt
E2_WALL=$OUT/ext2-write-path-objdump.txt
objdump -d --no-show-raw-insn "$OUT/madire.elf" 2>/dev/null | tee "$E2_WALL" |
    awk '/^[0-9a-f]+ <[^>]*>:$/ { fn = substr($2, 2, length($2) - 3); sub(/\.[a-z]+\.[0-9]+$/, "", fn); next }
         /(call|jmp)[a-z]*[ \t]+[0-9a-f]+ <[^>+]+>/ { t = $0; sub(/^.*</, "", t); sub(/>.*$/, "", t);
             sub(/\.[a-z]+\.[0-9]+$/, "", t); print t "\t" fn }' | LC_ALL=C sort -u > "$E2_WDIS"
# e2_refs <target>: the functions that call or jump to it (other than itself).
e2_refs() { awk -F'\t' -v t="$1" '$1 == t && $2 != t { print $2 }' "$E2_WDIS" | tr '\n' ' ' | sed 's/ *$//'; }
E2_WBAD=""
for e2_pair in "ahci_write:ahci_disk_write ahci_write_attempt" "ahci_flush:ahci_disk_flush ahci_write_attempt" \
               "ahci_disk_write:blk_write ahci_write_attempt" "ahci_disk_flush:blk_flush ahci_write_attempt" \
               "blk_write:ext2_d_write" "blk_flush:ext2_d_flush" "ahci_write_attempt:dt_refused disktest_write"; do
    e2_t=${e2_pair%%:*}; e2_ok=" ${e2_pair#*:} "
    e2_got=$(e2_refs "$e2_t")
    echo "      references to $e2_t: ${e2_got:-none} (allowed: ${e2_pair#*:})"
    [ -n "$e2_got" ] || E2_WBAD="$E2_WBAD $e2_t:unreferenced"
    for e2_f in $e2_got; do
        case "$e2_ok" in *" $e2_f "*) ;; *) E2_WBAD="$E2_WBAD $e2_t<-$e2_f" ;; esac
    done
done
E2_WMISS=""
for e2_f in ext2_d_write ext2_d_flush blk_write blk_flush ahci_disk_write ahci_disk_flush ahci_write ahci_flush; do
    nm "$OUT/madire.elf" 2>/dev/null | awk '{ print $NF }' | grep -qE "^$e2_f(\.[a-z]+\.[0-9]+)?\$" || E2_WMISS="$E2_WMISS $e2_f"
done
# The owner's addition (indirect calls): no function on the write path has
# its address taken anywhere in madire.elf, in code (any instruction but a
# call or a jump that names it, or holds its address as an immediate) or in
# data (its 8-byte address stored in a non-code section, tests/elf_refs.py),
# with ONE exception, required and safe: ext2_d_write and ext2_d_flush are
# stored once each in ext2_d_sv_ops, the block layer's write-back calls
# (const, in .rodata), which ext2_disk.c hands only to blk_sv_init; the
# write-back part calls them only from its save (blk_sv_save_step) and the
# write core's error-state write, which are the level above in the path.
E2_WFNS="ahci_write ahci_flush ahci_disk_write ahci_disk_flush blk_write blk_flush ext2_d_write ext2_d_flush ahci_write_attempt"
E2_ACODE=""
for e2_f in $E2_WFNS; do
    e2_a=$(nm "$OUT/madire.elf" 2>/dev/null | awk -v f="$e2_f" '{ n = $NF; sub(/\.[a-z]+\.[0-9]+$/, "", n) }
           n == f && ($2 == "t" || $2 == "T") { a = $1; sub(/^0+/, "", a); print a; exit }')
    e2_r=$(grep -E "<$e2_f(\.[a-z]+\.[0-9]+)?>|\\\$0x0*${e2_a:-NONE}([^0-9a-f]|\$)" "$E2_WALL" |
           grep -vE '>:$' | grep -vE ':[[:space:]]+(call|j[a-z]+)[a-z]*[[:space:]]' | grep -c .)
    [ "$e2_r" -eq 0 ] || E2_ACODE="$E2_ACODE $e2_f:$e2_r"
done
E2_ADATA=$(python3 -I -B "$(dirname "$0")/elf_refs.py" "$OUT/madire.elf" $E2_WFNS 2>&1)
sed 's/^/      address stored: /' <<< "$E2_ADATA"
E2_ABAD=$(grep -E '^(DATA|MISSING|NOT-ELF64)' <<< "$E2_ADATA" |
          grep -vxE 'DATA ext2_d_(write|flush) \.rodata\+0x[0-9a-f]+ in ext2_d_sv_ops' | tr '\n' ';')
E2_AOK=$(grep -cxE 'DATA ext2_d_(write|flush) \.rodata\+0x[0-9a-f]+ in ext2_d_sv_ops' <<< "$E2_ADATA")
echo "      address taken in code: ${E2_ACODE:-none}; stored in data outside ext2_d_sv_ops: ${E2_ABAD:-none} (in ext2_d_sv_ops: $E2_AOK of 2)"
if [ -s "$OUT/madire.elf" ] && [ -s "$E2_WDIS" ] && [ -z "$E2_WBAD" ] && [ -z "$E2_WMISS" ] && [ -z "$E2_ACODE" ] &&
   [ -z "$E2_ABAD" ] && [ "$E2_AOK" -eq 2 ] && grep -qxF "FUNCS 9 of 9" <<< "$E2_ADATA"; then
    pass "[1.4] ext2 guarded writes (madire.elf): the only path from the filesystem to a disk write is ext2_d_write/ext2_d_flush -> blk_write/blk_flush -> ahci_disk_write/ahci_disk_flush -> ahci_write/ahci_flush (into the guarded builder); every call or jump to each comes only from the level above (and test=diskwrite's refused-disk attempt to the driver's calls); no address of a path function is taken in code, and in data only ext2_d_write's and ext2_d_flush's, once each, in the block layer's const ext2_d_sv_ops"
else
    fail "[1.4] ext2 guarded writes (madire.elf): a function on the write path is referenced from elsewhere or not referenced (${E2_WBAD:-none}), missing from madire.elf (${E2_WMISS:-none}), or its address is taken (code: ${E2_ACODE:-none}; data: ${E2_ABAD:-none}; in ext2_d_sv_ops $E2_AOK of 2)"
fi

# The images. e2_mkimg <name> <tree kind> <KiB> <block size> <inode size>
# <inodes> [more mke2fs options...]; its exit status in E2_MKRC.
declare -A E2_MKRC=()
e2_mkimg() {
    local name=$1 tree=$E2_W/tree-$2 rc=0
    [ -d "$tree" ] || "${E2_PY[@]}" tree "$tree" "$2" || rc=1
    [ "$rc" -eq 0 ] && { disk_mkfs "$E2_W/$name.img" "$3" "$4" "$5" "$6" "$tree" "${@:7}" > "$E2_W/mkfs-$name.txt" 2>&1 || rc=$?; }
    sed "s/^/      mke2fs ($name): /" "$E2_W/mkfs-$name.txt" 2>/dev/null
    E2_MKRC[$name]=$rc
}
e2_mkimg b1k full 16384 1024 128 512
e2_mkimg b2k full 81920 2048 256 512
e2_mkimg b4k full 163840 4096 256 512
e2_mkimg rev0 full 16384 1024 128 512 -r 0 -O none
e2_mkimg large large 32768 1024 128 64
e2_mkimg dirindex dirindex 8192 1024 128 512
e2_mkimg xattr xattr 8192 1024 128 64
e2_mkimg corpus corpus 2048 1024 128 128
# dir_index: e2fsck -fyD indexes the 400-entry folder (exit 0 or 1: changed).
E2_FSCK=$(disk_tool e2fsck)
LC_ALL=C E2FSCK_CONFIG=/dev/null "$E2_FSCK" -fyD "$E2_W/dirindex.img" > "$E2_W/fsck-dirindex.txt" 2>&1
E2_DRC=$?
[ "$E2_DRC" -le 1 ] || E2_MKRC[dirindex]=$E2_DRC
# ext_attr: two attributes on a file and one on a folder (debugfs -w, on
# this test image only); with 128-byte inodes they go into an attribute block.
printf '%s\n' "ea_set /attr/a.txt user.madire madire-os" "ea_set /attr/a.txt user.stage six" \
    "ea_set /attr/folder user.madire folder" > "$E2_W/xattr.cmd"
LC_ALL=C "$(disk_tool debugfs)" -w -f "$E2_W/xattr.cmd" "$E2_W/xattr.img" > "$E2_W/xattr-set.txt" 2>&1 || E2_MKRC[xattr]=1
echo "      images: $(for e2_n in b1k b2k b4k rev0 large dirindex xattr corpus; do printf '%s=%s ' "$e2_n" "${E2_MKRC[$e2_n]}"; done)(e2fsck -fyD exit $E2_DRC)"

# e2_compare <name>: ext2dump against debugfs's view of the same image: the
# same P lines (sorted), the same "EXT2 rev=" line, exit 0, read-write.
e2_compare() {
    local img=$E2_W/$1.img d v n rc
    "$E2_DUMP" "$img" > "$E2_W/$1.dump" 2> "$E2_W/$1.dump-err"
    rc=$?
    "${E2_PY[@]}" view "$img" > "$E2_W/$1.view" 2> "$E2_W/$1.view-err"
    "${E2_PY[@]}" blocks "$img" > "$E2_W/$1.blocks" 2>/dev/null
    d=$(grep '^P ' "$E2_W/$1.dump" | LC_ALL=C sort)
    v=$(grep '^P ' "$E2_W/$1.view" | LC_ALL=C sort)
    n=$(grep -c . <<< "$d")
    grep -E '^EXT2 ' "$E2_W/$1.dump" | sed "s/^/      $1: /"
    echo "      $1: ext2dump exit $rc, $n paths; debugfs $(grep -c . <<< "$v") paths; $(grep -E '^DONE' "$E2_W/$1.dump"); image SHA-256 $(sha256sum < "$img" | cut -c1-16)..."
    diff <(echo "$d") <(echo "$v") | head -n 10 | sed "s/^/      $1 differs: /"
    [ "${E2_MKRC[$1]}" = 0 ] && [ "$rc" -eq 0 ] && [ "$n" -gt 1 ] && [ "$d" = "$v" ] &&
        [ "$(grep -m1 '^EXT2 rev=' "$E2_W/$1.dump")" = "$(grep -m1 '^EXT2 rev=' "$E2_W/$1.view")" ] &&
        grep -qxF "EXT2 policy: read-write" "$E2_W/$1.dump"
}
# e2_rev1 <name>: revision 1 with resize_inode: reserved descriptor blocks
# (as debugfs reads them, in the compared line) and inode 7 read.
e2_rev1() {
    grep -qE '^EXT2 rev=1 .* reserved_gdt=[1-9][0-9]*$' "$E2_W/$1.dump" &&
        grep -qE '^EXT2 resize_inode: inode 7 mode 100600 ' "$E2_W/$1.dump"
}
# e2_line <name> <path> <regex>: the path's P line in the dump matches.
e2_line() { grep -E "^P $2 " "$E2_W/$1.dump" | grep -qE "$3"; }
e2_groups() { sed -nE 's/^EXT2 groups=([0-9]+) .*/\1/p' "$E2_W/$1.dump"; }

if e2_compare b1k && e2_rev1 b1k && [ "$(e2_groups b1k)" -ge 2 ] &&
   grep -qE '^BLOCKMAP /sparse-tind.bin [0-9]+ ind=[0-9]+ dind=[0-9]+ tind=1$' "$E2_W/b1k.blocks" &&
   grep -qE '^BLOCKMAP /big.bin [0-9]+ ind=[0-9]+ dind=1 tind=0$' "$E2_W/b1k.blocks" &&
   e2_line b1k /link-fast 'mode=120777 size=38 ' && e2_line b1k /link-slow 'mode=120777 size=87 ' &&
   e2_line b1k /hard-1.txt 'links=2 ' && e2_line b1k /pipe 'mode=010644 '; then
    pass "[1.4] ext2 read core, 1 KiB blocks, 128-byte inodes, revision 1, $(e2_groups b1k) groups: ext2dump lists exactly debugfs's paths with their inode numbers, modes, sizes, link counts, flags, attribute blocks and SHA-256s ($(grep -c '^P ' "$E2_W/b1k.dump") paths: files through the double indirect block, a sparse file through the triple indirect block, a 255-byte name, a fast and a slow link, a hard link, a FIFO, a multi-block folder), and debugfs's superblock figures (resize_inode's reserved blocks included)"
else
    fail "[1.4] ext2 read core, 1 KiB blocks: ext2dump's listing differs from debugfs's, or the image lacks its triple indirect file, links or groups (see $E2_W/b1k.*)"
fi
if e2_compare b2k && e2_rev1 b2k && [ "$(e2_groups b2k)" -ge 2 ] && grep -qE '^EXT2 rev=1 block=2048 inode=256 ' "$E2_W/b2k.dump" &&
   grep -qE '^BLOCKMAP /double.bin [0-9]+ ind=[0-9]+ dind=1 tind=0$' "$E2_W/b2k.blocks"; then
    pass "[1.4] ext2 read core, 2 KiB blocks, 256-byte inodes, revision 1, $(e2_groups b2k) groups: ext2dump lists exactly what debugfs reads ($(grep -c '^P ' "$E2_W/b2k.dump") paths, double.bin through the double indirect block)"
else
    fail "[1.4] ext2 read core, 2 KiB blocks: ext2dump's listing differs from debugfs's (see $E2_W/b2k.*)"
fi
if e2_compare b4k && e2_rev1 b4k && [ "$(e2_groups b4k)" -ge 2 ] && grep -qE '^EXT2 rev=1 block=4096 inode=256 ' "$E2_W/b4k.dump" &&
   grep -qE '^BLOCKMAP /double.bin [0-9]+ ind=[0-9]+ dind=1 tind=0$' "$E2_W/b4k.blocks"; then
    pass "[1.4] ext2 read core, 4 KiB blocks, 256-byte inodes, revision 1, $(e2_groups b4k) groups: ext2dump lists exactly what debugfs reads ($(grep -c '^P ' "$E2_W/b4k.dump") paths, double.bin through the double indirect block)"
else
    fail "[1.4] ext2 read core, 4 KiB blocks: ext2dump's listing differs from debugfs's (see $E2_W/b4k.*)"
fi
if e2_compare rev0 && grep -qE '^EXT2 rev=0 block=1024 inode=128 .* first_ino=11 reserved_gdt=0$' "$E2_W/rev0.dump" &&
   grep -qE '^EXT2 features compat=0x0 incompat=0x0 ro_compat=0x0;' "$E2_W/rev0.dump"; then
    pass "[1.4] ext2 read core, revision 0 (no features: 16-bit name lengths, no file types, first inode 11, 128-byte inodes): ext2dump lists exactly what debugfs reads ($(grep -c '^P ' "$E2_W/rev0.dump") paths)"
else
    fail "[1.4] ext2 read core, revision 0: ext2dump's listing differs from debugfs's, or the image is not revision 0 without features (see $E2_W/rev0.*)"
fi
if e2_compare large && e2_rev1 large && e2_line large /large.bin 'size=2147484672 ' &&
   grep -qE '^BLOCKMAP /large.bin [0-9]+ ind=[0-9]+ dind=[0-9]+ tind=1$' "$E2_W/large.blocks"; then
    pass "[1.4] ext2 read core, large_file: a sparse file of 2 GiB + 1 KiB (its last block through the triple indirect block) read with the same size and SHA-256 as debugfs's"
else
    fail "[1.4] ext2 read core, large_file: the file above 2 GiB differs from debugfs's reading (see $E2_W/large.*)"
fi
if e2_compare dirindex && e2_rev1 dirindex && e2_line dirindex /many 'flags=0x1000 ' &&
   [ "$(grep -c '^P /many/' "$E2_W/dirindex.dump")" -eq 400 ]; then
    pass "[1.4] ext2 read core, dir_index: a folder of 400 entries indexed by e2fsck -D (flag 0x1000) read in its on-disk order, its index blocks skipped: exactly debugfs's 400 entries"
else
    fail "[1.4] ext2 read core, dir_index: the indexed folder's entries differ from debugfs's, or it is not indexed (see $E2_W/dirindex.*)"
fi
if e2_compare xattr && e2_rev1 xattr && e2_line xattr /attr/a.txt 'acl=[1-9][0-9]* ' && e2_line xattr /attr/folder 'acl=[1-9][0-9]* ' &&
   e2_line xattr /attr/b.txt 'acl=0 '; then
    pass "[1.4] ext2 read core, ext_attr: a file and a folder with an attribute block (debugfs ea_set) read as debugfs reads them, their attribute blocks reported, the contents unchanged"
else
    fail "[1.4] ext2 read core, ext_attr: the files with attribute blocks differ from debugfs's reading, or no attribute block was made (see $E2_W/xattr.*)"
fi

# (9-11) The feature policy and the states: copies of b1k.img.
E2_B1K=$(grep '^P ' "$E2_W/b1k.dump" 2>/dev/null | LC_ALL=C sort)
declare -A E2_PRC=()
for e2_spec in "inc-meta $((1024 + 96)) 4 0x10 or" "inc-high $((1024 + 96)) 4 0x80000000 or" \
               "ro-huge $((1024 + 100)) 4 0x8 or" "ro-high $((1024 + 100)) 4 0x80000000 or" \
               "st-dirty $((1024 + 58)) 2 0x0" "st-error $((1024 + 58)) 2 0x3"; do
    read -r e2_x e2_off e2_len e2_val e2_how <<< "$e2_spec"
    # shellcheck disable=SC2086
    "${E2_PY[@]}" set "$E2_W/b1k.img" "$E2_W/$e2_x.img" "$e2_off" "$e2_len" "$e2_val" $e2_how | sed 's/^/      /'
    "$E2_DUMP" "$E2_W/$e2_x.img" > "$E2_W/$e2_x.dump" 2>&1
    E2_PRC[$e2_x]=$?
    grep -E '^EXT2 (mount|policy|state)' "$E2_W/$e2_x.dump" | sed "s/^/      $e2_x: /"
    echo "      $e2_x: exit ${E2_PRC[$e2_x]}, $(grep -c '^P ' "$E2_W/$e2_x.dump") paths"
done
e2_plines() { grep '^P ' "$E2_W/$1.dump" | LC_ALL=C sort; }
E2_INC_WHY="EXT2 mount: EINCOMPAT (features: an unknown INCOMPAT flag (not mounted))"
if [ "${E2_PRC[inc-meta]}" = 1 ] && [ "${E2_PRC[inc-high]}" = 1 ] &&
   grep -qxF "$E2_INC_WHY" "$E2_W/inc-meta.dump" && grep -qxF "$E2_INC_WHY" "$E2_W/inc-high.dump" &&
   grep -qxF "EXT2 policy: not mounted (unknown INCOMPAT 0x10)" "$E2_W/inc-meta.dump" &&
   grep -qxF "EXT2 policy: not mounted (unknown INCOMPAT 0x80000000)" "$E2_W/inc-high.dump" &&
   ! grep -q '^P ' "$E2_W/inc-meta.dump" "$E2_W/inc-high.dump"; then
    pass "[1.4] ext2 feature policy (D11): an unknown INCOMPAT flag (meta_bg 0x10, or 0x80000000) is not mounted: EINCOMPAT, nothing read"
else
    fail "[1.4] ext2 feature policy (D11): an image with an unknown INCOMPAT flag was not refused as expected (see $E2_W/inc-*.dump)"
fi
"${E2_PY[@]}" view "$E2_W/ro-huge.img" > "$E2_W/ro-huge.view" 2>/dev/null
if [ "${E2_PRC[ro-huge]}" = 0 ] && [ "${E2_PRC[ro-high]}" = 0 ] &&
   grep -qxF "EXT2 policy: read-only (unknown RO_COMPAT 0x8)" "$E2_W/ro-huge.dump" &&
   grep -qxF "EXT2 policy: read-only (unknown RO_COMPAT 0x80000000)" "$E2_W/ro-high.dump" &&
   [ -n "$E2_B1K" ] && [ "$(e2_plines ro-huge)" = "$E2_B1K" ] && [ "$(e2_plines ro-high)" = "$E2_B1K" ] &&
   [ "$(grep '^P ' "$E2_W/ro-huge.view" | LC_ALL=C sort)" = "$E2_B1K" ]; then
    pass "[1.4] ext2 feature policy (D11): an unknown RO_COMPAT flag (huge_file 0x8, or 0x80000000) is mounted read only, and every path still reads exactly as debugfs reads it"
else
    fail "[1.4] ext2 feature policy (D11): an image with an unknown RO_COMPAT flag was not read only, or not read exactly (see $E2_W/ro-*.dump)"
fi
if grep -qxF "EXT2 state: clean, error bit not set (0x1)" "$E2_W/b1k.dump" &&
   [ "${E2_PRC[st-dirty]}" = 0 ] && grep -qxF "EXT2 state: not clean, error bit not set (0x0)" "$E2_W/st-dirty.dump" &&
   [ "${E2_PRC[st-error]}" = 0 ] && grep -qxF "EXT2 state: clean, error bit set (0x3)" "$E2_W/st-error.dump" &&
   [ -n "$E2_B1K" ] && [ "$(e2_plines st-dirty)" = "$E2_B1K" ] && [ "$(e2_plines st-error)" = "$E2_B1K" ]; then
    pass "[1.4] ext2 states (D12, reported only in stage 6): clean (0x1), not clean (0x0) and the error bit (0x3) read and reported; every path read exactly in each (the mount's decisions come in stage 8)"
else
    fail "[1.4] ext2 states: the clean or error state was not reported as expected (see $E2_W/st-*.dump)"
fi

# (12) The sanitizers over the corrupted images.
E2_SAN=$E2_W/ext2dump-san
gcc -std=gnu11 -O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined -fno-sanitize-recover=all \
    -Wall -Wextra -Werror -I"$E2_ROOT" "$E2_ROOT/tests/ext2dump.c" "$E2_ROOT/kernel/fs/ext2_core.c" \
    -o "$E2_SAN" > "$E2_W/san-build.txt" 2>&1
E2_SANB=$?
head -n 20 "$E2_W/san-build.txt" | sed 's/^/      sanitizer build: /'
# Address-space randomisation off for these runs (setarch -R), where it is
# available: some kernels' random mmap bits break ASan's shadow memory.
E2_NOASLR=()
command -v setarch > /dev/null 2>&1 && setarch "$(uname -m)" -R true 2>/dev/null && E2_NOASLR=(setarch "$(uname -m)" -R)
# e2_san <image> <output>: one sanitized run (-l 65536: long files read at
# both ends only), 10 s at most; its exit status.
e2_san() {
    ASAN_OPTIONS=detect_leaks=1:exitcode=70 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1:exitcode=71 \
        timeout 10 "${E2_NOASLR[@]}" "$E2_SAN" -l 65536 "$1" > "$2" 2> "$2.err" < /dev/null
}
e2_san "$E2_W/corpus.img" "$E2_W/corpus.san"
E2_SANC=$?
"$E2_DUMP" -l 65536 "$E2_W/corpus.img" > "$E2_W/corpus.plain" 2>&1
E2_PLAINC=$?
echo "      sanitizer build exit $E2_SANB (${E2_NOASLR[*]:-ASLR on}); the healthy image: sanitized exit $E2_SANC, plain exit $E2_PLAINC, $(grep -c '^P ' "$E2_W/corpus.san") paths"
"${E2_PY[@]}" corrupt "$E2_W/corpus.img" "$E2_W/bad" > "$E2_W/bad.txt" 2> "$E2_W/bad-err.txt"
sed 's/^/      corrupt: /' "$E2_W/bad-err.txt" | head -n 5
E2_BN=0
E2_BBAD=0
declare -A E2_AREA=()
while read -r _ e2_f e2_area e2_what; do
    e2_san "$E2_W/bad/$e2_f" "$E2_W/bad/$e2_f.out"
    e2_rc=$?
    E2_BN=$((E2_BN + 1))
    e2_ok=1
    case $e2_rc in 0|1|2) ;; *) e2_ok=0 ;; esac
    grep -qE 'Sanitizer|runtime error' "$E2_W/bad/$e2_f.out.err" && e2_ok=0
    grep -qE '^DONE .* bad_reads=0$' "$E2_W/bad/$e2_f.out" || e2_ok=0
    [ "$e2_ok" -eq 1 ] || E2_BBAD=$((E2_BBAD + 1))
    E2_AREA[$e2_area]="${E2_AREA[$e2_area]:-}$e2_rc "
    e2_first=$(grep -m1 -E '^(E |EXT2 mount: E)' "$E2_W/bad/$e2_f.out")
    echo "      $e2_f ($e2_what): exit $e2_rc$([ "$e2_ok" -eq 1 ] || echo ' NOT ONLY AN ERROR CODE'): ${e2_first:-everything read}"
done < "$E2_W/bad.txt"
for e2_a in superblock groupdesc inodes folder indirect; do
    e2_s=${E2_AREA[$e2_a]:-}
    echo "      [1.4] corrupted $e2_a: $(wc -w <<< "$e2_s") images; exits (report only):$(tr ' ' '\n' <<< "$e2_s" | grep . | LC_ALL=C sort | uniq -c | awk '{ printf " %s x%s", $2, $1 }')"
done
if [ "$E2_SANB" -eq 0 ] && [ "$E2_SANC" -eq 0 ] && [ "$E2_PLAINC" -eq 0 ] &&
   ! grep -qE 'Sanitizer|runtime error' "$E2_W/corpus.san.err" &&
   [ "$(grep '^P ' "$E2_W/corpus.san")" = "$(grep '^P ' "$E2_W/corpus.plain")" ] &&
   [ "$E2_BN" -eq 104 ] && [ "$E2_BBAD" -eq 0 ]; then
    pass "[1.4] ext2 read core under -fsanitize=address,undefined: the healthy image read clean (as the plain build reads it), and all 104 corrupted images (bit flips and bad values in the superblock, group descriptors, inode table, folder blocks and indirect blocks) end with an error code (exit 0, 1 or 2): no sanitizer report, no crash, no hang, no read outside the image"
else
    fail "[1.4] ext2 read core under the sanitizers: build exit $E2_SANB, healthy image exit $E2_SANC, $E2_BBAD of $E2_BN corrupted images (104 expected) crashed, hung, read outside the image or raised a sanitizer report"
fi
echo "      [1.4] ext2 read core: $(( $(date +%s) - E2_T0 )) s (report only)"

# ---- 1.4 stage 8: ext2 mounted read-only at /home ----

section "[1.4] ext2 mounted read-only at /home: the boot behaviour, the read sessions against debugfs (stage 8)"
# docs/plans/1.4.md, S8 (C8, C9, D11, D12, D15, D16, D22; risks 26, 33,
# 34, 38, 39). The ext2 core is linked (kernel/fs/ext2_core.c), behind the
# VFS (kernel/fs/ext2_vfs.c) and mounted at boot on the RAM /home through
# the mount table (kernel/fs/ext2_disk.c), read only: every change is
# EROFS. (1) No kernel file calls the RAM-only wrappers fsys_cwd_path and
# fsys_dir_fill (kept for the existing unit tests). (2-11) The boot
# behaviour (D16), -kernel boots at -m 128M to System ready with one disk on
# ich9-ahci (as stage 5's): the boots with no disk, an unmarked disk and
# disks the guard refused are stage 3's and 5's (their logs read again);
# new boots on copies of stage 5's marked MBR disk with one superblock
# field changed: no ext2 magic (not ext2: refused by the guard), an unknown
# INCOMPAT flag (not mounted), an unknown RO_COMPAT flag, the error bit
# and not clean (mounted read-only, with their lines), an unknown COMPAT
# flag (mounted; reading allowed, logged); the marked clean disk is stage
# 5's. Each: its ext2: and mount: lines and its Storage: line exactly.
# (12-17) Each new boot's image SHA-256 unchanged; (18) the new boots' line
# order and health. (19-35) The read sessions (moved here from part 1,
# D22): one console session on a marked MBR disk with a 1 KiB-block and one
# with a 4 KiB-block ext2 volume made by mke2fs from the same tree (text
# files, a 14 KiB file through the single indirect block at 1 KiB, nested
# folders, an empty folder): pwd, ls -l (in on-disk order), cat, stat, du,
# cd across the mount, and the writes (touch, mkdir, rmdir, rm, the
# redirections, cp, mv), each refused with the read-only message and
# changing nothing; compared with debugfs on the partition extracted from
# the disk after the boot (tests/ext2_session.py); the image's SHA-256
# unchanged; the per-filesystem file-size limits logged at the mount.
E8_T0=$(date +%s)
E8_W=$OUT/ext2-mount
rm -rf "$E8_W" && mkdir -p "$E8_W"
declare -A E8_SHA0=() E8_SHA1=()
E8_OK=$GD_OK

# (1) The RAM-only wrappers: no caller in the kernel's sources (their own
# definitions and declarations in fsys_core.c and fsys_core.h aside).
E8_CALLS=$(grep -rnE '\bfsys_(cwd_path|dir_fill)[[:space:]]*\(' "$E2_ROOT/kernel" --include='*.c' --include='*.h' |
           grep -vE '/kernel/fs/fsys_core\.(c|h):')
E8_DEFS=$(grep -cE '^(int64_t fsys_cwd_path|uint32_t fsys_dir_fill)\(' "$E2_ROOT/kernel/fs/fsys_core.c")
echo "      wrapper calls in kernel/: $(grep -c . <<< "$E8_CALLS"); definitions in fsys_core.c: $E8_DEFS"
[ -n "$E8_CALLS" ] && sed 's/^/      call: /' <<< "$E8_CALLS"
if [ -z "$E8_CALLS" ] && [ "$E8_DEFS" -eq 2 ] &&
   grep -q "RAM-only form for the existing tests; not re-entrant; never used by" "$E2_ROOT/kernel/fs/fsys_core.c"; then
    pass "[1.4] the RAM-only wrappers fsys_cwd_path and fsys_dir_fill (for the existing unit tests; not re-entrant): no kernel file calls them, and their comment says so"
else
    fail "[1.4] the RAM-only wrappers: a kernel file calls fsys_cwd_path or fsys_dir_fill, or their definitions or comment are missing"
fi
# Report only: the RAM node record's size (its compile-time check stays
# "at most 128"), and the vnode's, as the kernel's compiler lays them out.
printf '%s\n' '#include <stdio.h>' '#include "kernel/fs/ramfs_core.h"' \
    'int main(void) { printf("SIZEOF struct ramfs_node=%zu struct vnode=%zu\n", sizeof(struct ramfs_node), sizeof(struct vnode)); return 0; }' \
    > "$E8_W/sizeof.c"
gcc -std=gnu11 -m64 -I"$E2_ROOT" "$E8_W/sizeof.c" -o "$E8_W/sizeof" > "$E8_W/sizeof.txt" 2>&1 && "$E8_W/sizeof" >> "$E8_W/sizeof.txt"
sed 's/^/      [1.4] (report only) /' "$E8_W/sizeof.txt"

# e8_boot <name> <image>: one boot into serial-ext2-<name>.log, the image's
# SHA-256 before and after (as gd_boot).
e8_boot() {
    local name=$1 img=$2 log=$OUT/serial-ext2-$1.log
    E8_SHA0[$name]=$(sha256sum < "$img" | cut -d' ' -f1)
    # 1.4 stage 12 (changed, the owner's decision): test=nohome, as gd_boot.
    boot_until "$log" 60 "$PCI_READY" -m 128M -kernel "$KERNEL" -append "test=nohome" \
        -drive file="$img",if=none,id=e8test-disk,format=raw -device ich9-ahci,id=e8test-ahci,addr=0x5 \
        -device ide-hd,drive=e8test-disk,bus=e8test-ahci.0
    E8_SHA1[$name]=$(sha256sum < "$img" | cut -d' ' -f1)
    grep -E '^(guard: port 0 partition|ext2|mount|Storage): ' "$log" | sed "s/^/      $name: /"
}
# e8_hash <name> <what>: the counted hash check of one boot.
e8_hash() {
    echo "      $1: SHA-256 before ${E8_SHA0[$1]:-?}, after ${E8_SHA1[$1]:-?}"
    if [ -n "${E8_SHA0[$1]:-}" ] && [ "${E8_SHA0[$1]}" = "${E8_SHA1[$1]:-}" ]; then
        pass "[1.4] ext2 at /home, $2: the disk image's SHA-256 is the same before and after the boot (${E8_SHA0[$1]})"
    else
        fail "[1.4] ext2 at /home, $2: the disk image's SHA-256 changed during the boot (${E8_SHA0[$1]:-?} -> ${E8_SHA1[$1]:-?})"
    fi
}
# e8_lines <log> <expected lines>: each exactly once; the ext2: and mount:
# lines exactly those expected (none when none is), one Storage: line, no
# disk error.
e8_lines() {
    local log=$1 l ne nm
    [ "$E8_OK" -eq 1 ] && [ -f "$log" ] || return 1
    while IFS= read -r l; do
        [ -z "$l" ] && continue
        [ "$(grep -cxF -- "$l" "$log")" -eq 1 ] || { echo "      $(basename "$log"): missing or repeated: $l"; return 1; }
    done <<< "$2"
    ne=$(grep -c '^ext2: ' <<< "$2"); nm=$(grep -c '^mount: ' <<< "$2")
    [ "$(grep -c '^ext2: ' "$log")" -eq "$ne" ] && [ "$(grep -c '^mount: ' "$log")" -eq "$nm" ] &&
        [ "$(grep -c '^Storage: ' "$log")" -eq 1 ] && ! grep -qE '^ahci: port [0-9]+: .* failed: ' "$log"
}
# The superblock of stage 5's marked MBR disk (8 MiB, 1 KiB blocks, its
# partition at LBA $GD_S): its fields' bytes in the disk image.
E8_SB=$((GD_S * 512 + 1024))
E8_ST="Storage: AHCI 00:05.0, port 0 disk QEMU HARDDISK $GD_MIB_M MiB, MBR, 1 partition, writes would be"
E8_RO="; ext2 'madire-test' mounted read-only on /home (Disk mode, read-only)"
E8_FS_RE="^ext2: port 0 partition 1: revision 1, 1024-byte blocks, [0-9]+ blocks \([0-9]+ free\), [0-9]+ inodes \([0-9]+ free\), [0-9]+ groups, 128-byte inodes, label 'madire-test'; features COMPAT 0x"
# e8_fsline <log> <state text>: the volume's line, with its state.
e8_fsline() { grep -E "$E8_FS_RE" "$1" | grep -qE "; state $2\$"; }
# e8_mount <reason>: the mount line up to its read counts; e8_mounted <log>
# <reason>: that line exactly once, with no failed read.
e8_mount() { echo "mount: ext2 'madire-test' (port 0, partition 1) on /home as filesystem 1, read-only ($1); the RAM folder /home is under it, / and /tmp stay in memory; "; }
e8_mounted() {
    local want
    want=$(e8_mount "$2")
    [ "$(awk -v n="${#want}" -v w="$want" 'substr($0, 1, n) == w && substr($0, n + 1) ~ /^[0-9]+ disk reads, 0 failed$/' "$1" | grep -c .)" -eq 1 ]
}
for e8_spec in "notext2 $((E8_SB + 56)) 2 0x0" "incompat $((E8_SB + 96)) 4 0x10 or" "rocompat $((E8_SB + 100)) 4 0x8 or" \
               "error $((E8_SB + 58)) 2 0x3" "dirty $((E8_SB + 58)) 2 0x0" "compat $((E8_SB + 92)) 4 0x1000 or"; do
    read -r e8_x e8_off e8_len e8_val e8_how <<< "$e8_spec"
    # shellcheck disable=SC2086
    "${E2_PY[@]}" set "$GD_W/mbr.img" "$E8_W/$e8_x.img" "$e8_off" "$e8_len" "$e8_val" $e8_how | sed 's/^/      /' || E8_OK=0
    [ -s "$E8_W/$e8_x.img" ] || E8_OK=0
done
cp "$E8_W/dirty.img" "$E8_W/dirty-before.img"     # 1.4 stage 10: for its bounded diff
for e8_x in notext2 incompat rocompat error dirty compat; do
    e8_boot "$e8_x" "$E8_W/$e8_x.img"
done
E8_L=$OUT/serial-ext2

# (2) No disk: Live mode exactly as before (stage 3's controller boots and
# stage 4's pattern disk): no ext2: or mount: line.
if [ -f "$PCI_B" ] && [ -f "$PCI_C" ] && [ -f "$AH_LOG" ] &&
   [ "$(cat "$PCI_A" "$PCI_B" "$PCI_C" "$AH_LOG" | grep -cE '^(ext2|mount): ')" -eq 0 ] &&
   grep -qxF "Storage: AHCI 00:05.0, no disk (Live mode)" "$PCI_B" && grep -qxF "$AH_STORAGE" "$AH_LOG"; then
    pass "[1.4] ext2 at /home, no controller, no disk or no partition table: Live mode exactly as before (the Storage: lines unchanged, no ext2: or mount: line)"
else
    fail "[1.4] ext2 at /home, no disk: an ext2: or mount: line, or a changed Storage: line, on a boot without a usable disk"
fi
# (3) An unmarked ext2 disk (stage 5's): not mounted, Live mode.
if e8_lines "$OUT/serial-guard-nomarker.log" "$GD_ST_MBR REFUSED: (b) no valid test marker$GD_TAIL"; then
    pass "[1.4] ext2 at /home, an unmarked ext2 disk: the guard refuses it, nothing is mounted (no ext2: or mount: line), Live mode"
else
    fail "[1.4] ext2 at /home, an unmarked disk: an ext2: or mount: line, or not Live mode (see serial-guard-nomarker.log)"
fi
# (4) Disks the guard refused otherwise (stage 5's): a wrong label, a
# model that is not a virtual disk's, a bad marker CRC32, the marker's
# start, the empty disk: none mounted.
E8_BAD=""
for n in label model badcrc start empty; do
    e8_lines "$OUT/serial-guard-$n.log" "" && grep -qE '^Storage: .*; read only, not mounted \(Live mode\)$' "$OUT/serial-guard-$n.log" ||
        E8_BAD="$E8_BAD $n"
done
if [ -z "$E8_BAD" ]; then
    pass "[1.4] ext2 at /home, every disk the guard refused (a wrong label, a non-virtual model, a bad marker CRC32, the marker's start, the empty disk): nothing mounted, Live mode"
else
    fail "[1.4] ext2 at /home, a disk the guard refused was mounted or not in Live mode:$E8_BAD"
fi
# (5) Not ext2 (the marked partition without the ext2 magic): the guard's
# condition (c) refuses it; Live mode.
if e8_lines "$E8_L-notext2.log" "guard: port 0 partition 1: writes would be REFUSED: (c) no ext2 superblock at partition byte 1024 (magic 0x0000, not 0xef53)
$E8_ST REFUSED: (c) not ext2 (no superblock)$GD_TAIL"; then
    pass "[1.4] ext2 at /home, a marked partition that is not ext2 (no magic): refused at the guard's condition (c), 'not ext2 (no superblock)', nothing mounted, Live mode"
else
    fail "[1.4] ext2 at /home, not ext2: not refused as expected (see serial-ext2-notext2.log)"
fi
# (6) An unknown INCOMPAT flag (meta_bg 0x10): allowed by the guard, not
# mounted (D11), one ext2: line with the reason; Live mode.
if e8_lines "$E8_L-incompat.log" "ext2: port 0 partition 1: not mounted: EINCOMPAT: features: an unknown INCOMPAT flag (not mounted) (INCOMPAT 0x12, unknown 0x10)
$E8_ST ALLOWED on partition 1; read only, not mounted: unknown INCOMPAT flag (Live mode)"; then
    pass "[1.4] ext2 at /home, an unknown INCOMPAT flag (0x10): not mounted (D11), the ext2: line names the flag, the Storage: line the reason; Live mode"
else
    fail "[1.4] ext2 at /home, an unknown INCOMPAT flag: not refused as expected (see serial-ext2-incompat.log)"
fi
# (7-9) An unknown RO_COMPAT flag, the error bit, not clean: mounted
# read-only, each with its line (D11, C7, D12).
# e8_ro <name> <its own ext2: line> <state regex> <the mount line's reason>:
# the volume's line and that line (2 ext2: lines), the mount's three
# lines, the Storage: line, no disk error.
e8_ro() {
    local log=$E8_L-$1.log
    [ "$E8_OK" -eq 1 ] && [ -f "$log" ] &&
        [ "$(grep -c '^ext2: ' "$log")" -eq 2 ] && [ "$(grep -c '^mount: ' "$log")" -eq 3 ] &&
        [ "$(grep -cxF -- "$2" "$log")" -eq 1 ] && e8_fsline "$log" "$3" && e8_mounted "$log" "$4" &&
        [ "$(grep -cxF -- "$E8_ST ALLOWED on partition 1$E8_RO" "$log")" -eq 1 ] &&
        [ "$(grep -c '^Storage: ' "$log")" -eq 1 ] && ! grep -qE '^ahci: port [0-9]+: .* failed: ' "$log"
}
if e8_ro rocompat "ext2: unknown RO_COMPAT flags 0x8: mounted read-only (D11)" "0x1 \(clean, error bit not set\)" "an unknown RO_COMPAT flag"; then
    pass "[1.4] ext2 at /home, an unknown RO_COMPAT flag (huge_file 0x8): mounted read-only on /home with its line (D11); 'Disk mode, read-only'"
else
    fail "[1.4] ext2 at /home, an unknown RO_COMPAT flag: not mounted read-only with its line (see serial-ext2-rocompat.log)"
fi
if e8_ro error "ext2: the error bit is set (state 0x3): mounted read-only (C7)" "0x3 \(clean, error bit set\)" "the error bit is set"; then
    pass "[1.4] ext2 at /home, the error bit set (state 0x3): mounted read-only on /home with its line (C7); 'Disk mode, read-only'"
else
    fail "[1.4] ext2 at /home, the error bit: not mounted read-only with its line (see serial-ext2-error.log)"
fi
# 1.4 stage 10 (changed): not clean (no error bit) is now mounted
# READ-WRITE after D12's recount, with the warning line naming the recount
# and its results, the read-write mount line (save 0) and the fourth mount:
# line (the store of changes); 'Disk mode'. (Stage 8 checked the read-only
# mount that stage had, with the warning that the recount would come.)
# e8_rw <name> <state regex>: the volume's line, the mount's four lines
# (read-write, save 0 with at least one write and one flush, the lock hold,
# the limits, the store: automatic save off, as on every test= boot since
# 1.4 stage 12's test=nohome), 'Disk mode', no disk error.
E8_RWTAIL="; ext2 'madire-test' mounted read-write on /home (Disk mode)"
e8_rw() {
    local log=$1
    [ "$E8_OK" -eq 1 ] && [ -f "$log" ] && [ "$(grep -c '^mount: ' "$log")" -eq 4 ] && e8_fsline "$log" "$2" &&
        [ "$(grep -cE "^mount: ext2 'madire-test' \(port 0, partition 1\) on /home as filesystem 1, read-write; the RAM folder /home is under it, / and /tmp stay in memory; [0-9]+ disk reads, 0 failed; save 0 \(the volume marked not clean on the disk\): [1-9][0-9]* writes, [1-9][0-9]* flushes$" "$log")" -eq 1 ] &&
        [ "$(grep -cE '^mount: changes are kept in memory \(([0-9]+) KiB, \1 chunks of 1 KiB\) until a save writes them, at most 8 disk commands per hold of the VFS lock; automatic save every 5 s: off \(a test= boot\)$' "$log")" -eq 1 ] &&
        [ "$(grep '^Storage: ' "$log" | grep -cF ", writes would be ALLOWED on partition 1$E8_RWTAIL")" -eq 1 ] &&
        [ "$(grep -c '^Storage: ' "$log")" -eq 1 ] && ! grep -qE '^ahci: port [0-9]+: .* failed: ' "$log"
}
if [ "$(grep -c '^ext2: ' "$E8_L-dirty.log")" -eq 2 ] && e8_rw "$E8_L-dirty.log" "0x0 \(not clean, error bit not set\)" &&
   [ "$(grep -cE '^ext2: warning: the volume was not cleanly unmounted \(state 0x0\); the free blocks, free inodes and folders of every group recounted from the bitmaps and inode tables \(D12\): [0-9]+ blocks free, [0-9]+ inodes free; mounted read-write$' "$E8_L-dirty.log")" -eq 1 ]; then
    pass "[1.4] ext2 at /home, not cleanly unmounted (state 0x0): the free blocks, inodes and folders recounted from the bitmaps (D12), the warning line naming the recount; mounted read-write (save 0), 'Disk mode'"
else
    fail "[1.4] ext2 at /home, not clean: not recounted and mounted read-write with the D12 warning (see serial-ext2-dirty.log)"
fi
# (10) An unknown COMPAT flag (0x1000). 1.4 stage 10 (changed): reading
# allowed, writing not (the owner's rule for writes, which stage 8 named as
# coming): mounted READ-ONLY with its line; still read exactly as before.
if e8_ro compat "ext2: unknown COMPAT flags 0x1000: mounted read-only (reading allowed; writing needs every COMPAT flag known)" "0x1 \(clean, error bit not set\)" "an unknown COMPAT flag"; then
    pass "[1.4] ext2 at /home, an unknown COMPAT flag (0x1000): mounted read-only (reading allowed, writing needs every COMPAT flag known), its line logged; 'Disk mode, read-only'"
else
    fail "[1.4] ext2 at /home, an unknown COMPAT flag: not mounted read-only with its line (see serial-ext2-compat.log)"
fi
# (11) The marked, supported, clean disk (stage 5's MBR and GPT boots).
# 1.4 stage 10 (changed): mounted READ-WRITE on /home (save 0), no warning
# line, the fourth mount: line; 'Disk mode' (stage 8: read-only, 'stage 8
# has no write path').
E8_BAD=""
for n in mbr gpt; do
    log=$OUT/serial-guard-$n.log
    [ "$(grep -c '^ext2: ' "$log")" -eq 1 ] && e8_rw "$log" "0x1 \(clean, error bit not set\)" &&
        grep -qxF "mount: file-size limits from each filesystem's ops table: / 2097152 bytes, /home 4294967295 bytes" "$log" &&
        grep -qE '^mount: the mount held the VFS lock [0-9]+ us \([0-9]+ ticks\) \(report only; TSC [0-9]+ cycles per tick\)$' "$log" ||
        E8_BAD="$E8_BAD $n"
done
if [ -z "$E8_BAD" ]; then
    pass "[1.4] ext2 at /home, the marked clean disk (MBR and GPT): its volume's line, mounted read-write on /home as filesystem 1 with no warning (save 0 written: at least one write and one flush), the store of changes and the automatic save named, 'Disk mode'"
else
    fail "[1.4] ext2 at /home, the marked clean disk: not mounted as expected:$E8_BAD"
fi
# (12-17) The new boots' images unchanged.
e8_hash notext2 "the partition without the ext2 magic"
e8_hash incompat "the unknown INCOMPAT flag"
e8_hash rocompat "the unknown RO_COMPAT flag"
e8_hash error "the error bit"
# 1.4 stage 10 (changed): the not-clean volume is mounted read-write, so its
# check is the owner's bounded diff (gd_bounded, stage 5's section): only
# the superblock and the group descriptors' block after it (the D12
# recount) may change; e2fsck -fn reports nothing.
cp "$E8_W/dirty.img" "$GD_W/e8dirty-after.img"
gd_bounded e8dirty "ext2 at /home, the volume not cleanly unmounted" "$E8_W/dirty-before.img" "$GD_S" 1024
e8_hash compat "the unknown COMPAT flag"
# (18) Their line order and health.
E8_BAD=""
for n in notext2 incompat rocompat error dirty compat; do
    pcitest_order "$E8_L-$n.log" || E8_BAD="$E8_BAD $n"
done
if [ -z "$E8_BAD" ]; then
    pass "[1.4] ext2 at /home, the six boot-behaviour boots: the ext2: and mount: lines with the driver's own between the PCI: and Storage: lines (Storage: right before 'Key events:'), System ready; no PANIC or FAILED line"
else
    fail "[1.4] ext2 at /home, boot-behaviour boots: lines out of place, no System ready, or a PANIC or FAILED line in:$E8_BAD"
fi
echo "      [1.4] boot-behaviour boots: $(( $(date +%s) - E8_T0 )) s (report only)"

# (19-35) The read sessions. The tree: text files (every line shorter than
# 79 columns, so cat prints the file exactly), a 14 KiB file (through the
# single indirect block with 1 KiB blocks), nested folders, an empty one.
E8_T1=$(date +%s)
E8_TREE=$E8_W/tree
mkdir -p "$E8_TREE/docs/nested" "$E8_TREE/empty"
for i in $(seq 1 40); do printf 'note %02d: the ext2 volume at /home, read only in stage 8\n' "$i"; done > "$E8_TREE/notes.txt"
for i in $(seq 1 280); do printf 'big %04d: abcdefghijklmnopqrstuvwxyz0123456789ABCD\n' "$i"; done > "$E8_TREE/big.txt"
printf 'first file in docs\nsecond line\n' > "$E8_TREE/docs/a.txt"
printf 'b\n' > "$E8_TREE/docs/b.txt"
printf 'nested c.txt\nline 2\nline 3\n' > "$E8_TREE/docs/nested/c.txt"
printf 'last\n' > "$E8_TREE/z-last.txt"
printf 'middle\n' > "$E8_TREE/m-middle.txt"
E8_KEYS="x{wait:1}pwd{ret}ls -l{ret}ls -l docs{ret}ls -l /home/docs/nested{ret}"
E8_KEYS+="cat notes.txt{ret}{wait:2}cat docs/a.txt{ret}{wait:1}cat big.txt{ret}{wait:4}"
E8_KEYS+="stat notes.txt{ret}stat big.txt{ret}stat docs{ret}stat /home{ret}du{ret}du docs{ret}"
E8_KEYS+="cd docs{ret}pwd {ret}cd ..{ret}pwd  {ret}cd ..{ret}pwd   {ret}cd{ret}pwd    {ret}"
E8_KEYS+="touch new.txt{ret}touch notes.txt{ret}mkdir newdir{ret}mkdir docs{ret}rmdir empty{ret}"
E8_KEYS+="rm -y notes.txt{ret}echo hi > out.txt{ret}echo hi >> notes.txt{ret}{wait:1}"
E8_KEYS+="cp -y notes.txt copy.txt{ret}{wait:1}mv -y notes.txt n2.txt{ret}{wait:1}ls -l .{ret}"
E8_KEYS+="echo end-of-ext2-session{ret}"
E8_DEBUGFS=$(disk_tool debugfs)
# e8_session <name> <block size> <inode size> <KiB>: the volume, the marked
# disk, the boot with the session, then the comparison.
e8_session() {
    local name=$1 log=$OUT/serial-ext2-$1.log img=$E8_W/$1-disk.img
    disk_mkfs "$E8_W/$1-fs.img" "$4" "$2" "$3" 256 "$E8_TREE" > "$E8_W/$1-mkfs.txt" 2>&1 &&
        disk_image mbr "$E8_W/$1-fs.img" "$img" || E8_OK=0
    sed "s/^/      mke2fs ($1): /" "$E8_W/$1-mkfs.txt"
    E8_SHA0[$1]=$(sha256sum < "$img" | cut -d' ' -f1)
    T0S=$(date +%s%N)
    # 1.4 stage 10 (changed, the owner's decision): the read sessions boot
    # with test=diskro, which mounts the marked disk read-only as stage 8
    # did (a clean marked disk is read-write since stage 10), so every check
    # below keeps its meaning (writes refused, the image unchanged).
    boot_text "$log" 150 "System ready." "$E8_KEYS" 0.05 $'\nend-of-ext2-session\n' -m 128M -kernel "$KERNEL" \
        -append "test=diskro" \
        -drive file="$img",if=none,id=e8s-disk,format=raw -device ich9-ahci,id=e8s-ahci,addr=0x5 \
        -device ide-hd,drive=e8s-disk,bus=e8s-ahci.0
    boot_seconds "$log" "$T0S"
    E8_SHA1[$1]=$(sha256sum < "$img" | cut -d' ' -f1)
    disk_image extract "$img" "$E8_W/$1-part.img" > /dev/null || E8_OK=0
    grep -E '^(ext2|mount|Storage|console: cat: from the disk): ' "$log" | sed "s/^/      $1: /"
    DEBUGFS=$E8_DEBUGFS python3 -I -B "$(dirname "$0")/ext2_session.py" "$log" "$E8_W/$1-part.img" \
        > "$E8_W/$1-session.txt" 2>&1
    sed "s/^/      $1: /" "$E8_W/$1-session.txt"
}
# e8_results <name> <what>: the session's counted checks.
e8_results() {
    local name=$1 log=$OUT/serial-ext2-$1.log res=$E8_W/$1-session.txt c
    if [ "$E8_OK" -eq 1 ] && grep -qE '^Storage: AHCI 00:05\.0, port 0 disk QEMU HARDDISK [0-9]+ MiB, MBR, 1 partition, ' "$log" &&
       [ "$(grep '^Storage: ' "$log" | grep -cF ", writes would be ALLOWED on partition 1$E8_RO")" -eq 1 ] &&
       e8_mounted "$log" "test=diskro" && grep -qxF "$PCI_READY" "$log" && ! grep -qE 'PANIC|FAILED' "$log" &&
       ! grep -qE '^ahci: port [0-9]+: .* failed: ' "$log" && grep -qE '^ext2: port 0 partition 1: revision 1, '"$3"'-byte blocks' "$log" &&
       [ "$(grep -cE '^console: cat: from the disk \(report only\): longest VFS lock hold [0-9]+ us ' "$log")" -eq 3 ]; then
        pass "[1.4] ext2 read session, $2: the marked disk mounted read-only on /home at boot by test=diskro ('Disk mode, read-only'), System ready, the session typed, no disk error, no PANIC or FAILED line"
    else
        fail "[1.4] ext2 read session, $2: not mounted as expected, or the boot or the session failed (see serial-ext2-$1.log)"
    fi
    for c in ls cat stat du cd writes; do
        if grep -qxF "CHECK $c OK" "$res"; then
            case $c in
            ls) pass "[1.4] ext2 read session, $2: ls -l of /home, /home/docs and /home/docs/nested lists debugfs's entries in their on-disk order, with their types and sizes" ;;
            cat) pass "[1.4] ext2 read session, $2: cat of three files (one through the single indirect block at 1 KiB) prints them exactly: the SHA-256s of debugfs's cat, the byte counts on the console's cat lines" ;;
            stat) pass "[1.4] ext2 read session, $2: stat of two files, a folder and /home: the size, pages (i_blocks), node (inode number) and links as debugfs reads them" ;;
            du) pass "[1.4] ext2 read session, $2: du of /home and /home/docs: the KiB of the files' blocks (i_blocks / 2), the files and folders debugfs's tree gives (stage 12: KiB on the disk, was pages)" ;;
            cd) pass "[1.4] ext2 read session, $2: the shell starts in the mounted /home; cd docs, cd .., cd .. (to /, the RAM root) and cd alone (the mounted /home again), as pwd shows" ;;
            writes) pass "[1.4] ext2 read session, $2: touch (new and existing), mkdir, rmdir, rm, '>' and '>>', cp and mv are each refused with 'the filesystem is read-only' (mkdir of an existing folder: already exists), and the listing afterwards is the same" ;;
            esac
        else
            fail "[1.4] ext2 read session, $2: $c differs from debugfs or from the expected text: $(grep "^CHECK $c " "$res" | cut -c1-300)"
        fi
    done
    echo "      $1: SHA-256 before ${E8_SHA0[$1]:-?}, after ${E8_SHA1[$1]:-?}"
    if [ -n "${E8_SHA0[$1]:-}" ] && [ "${E8_SHA0[$1]}" = "${E8_SHA1[$1]:-}" ]; then
        pass "[1.4] ext2 read session, $2: the disk image's SHA-256 is the same before and after the boot and the session (${E8_SHA0[$1]})"
    else
        fail "[1.4] ext2 read session, $2: the disk image's SHA-256 changed (${E8_SHA0[$1]:-?} -> ${E8_SHA1[$1]:-?})"
    fi
}
e8_session s1k 1024 128 8192
e8_results s1k "1 KiB blocks" 1024
# The per-filesystem file-size limit (from each filesystem's ops table):
# the RAM filesystem's 2 MiB, the ext2 volume's 4 GiB - 1 (its block map
# reaches further at 1 KiB blocks: capped, C7).
if grep -qxF "mount: file-size limits from each filesystem's ops table: / 2097152 bytes, /home 4294967295 bytes" "$OUT/serial-ext2-s1k.log"; then
    pass "[1.4] file-size limits per filesystem (from each ops table): / (RAM) 2097152 bytes, /home (ext2, 1 KiB blocks) 4294967295 bytes"
else
    fail "[1.4] file-size limits per filesystem: the mount: line does not give / 2097152 and /home 4294967295 bytes (see serial-ext2-s1k.log)"
fi
e8_session s4k 4096 256 16384
e8_results s4k "4 KiB blocks" 4096
echo "      [1.4] read sessions: $(( $(date +%s) - E8_T1 )) s (report only)"
echo "      [1.4] $(grep -hE '^console: cat: from the disk' "$OUT/serial-ext2-s1k.log" "$OUT/serial-ext2-s4k.log" | sed 's/^console: cat: from the disk (report only): //' | tr '\n' ';')"
echo "      [1.4] ext2 at /home: $(( $(date +%s) - E8_T0 )) s (report only)"

# ---- 1.4 stage 9: the ext2 write core and the host cut sweep ----

section "[1.4] The ext2 write core and the save order: host sequences, the cut sweep, e2fsck and debugfs (stage 9)"
# docs/plans/1.4.md, S9 (C5 with the owner's R1-R4, section D's allowed
# list A1-A13; risks 25, 37, 38, 42). The write core (kernel/fs/ext2_write.c)
# and the block layer's save order (kernel/storage/blk_core_save.c) are pure
# and NOT linked into the kernel. (1) Compiled with the kernel's flags they
# need no symbol but the read core's and each other's, use no SSE/x87
# register, and madire.elf holds none of their symbols; tests/ext2write.c is
# built from them (its SHA-256 self-test passes). (2) mke2fs (tests/
# mke2fs.conf, stage 9 fix) no longer warns that the madire type is not
# defined, and this section's images have 256-byte inodes but the revision-0
# one (risk 42). (3-7) The host sequence "all" (every operation: folders,
# files through the direct, single, double and, at 1 KiB, triple indirect
# blocks, overwrites, truncates, renames across folders and replacing,
# folder growth, refusals) on mke2fs images: 1 KiB and 4 KiB blocks (the
# fixed "full" tree of tests/ext2_view.py), dir_index (the indexed folder
# changed: its index flag cleared), ext_attr (a file and a folder with an
# attribute block removed: both blocks freed) and revision 0; after the full
# save and the unmount, e2fsck -fn reports NOTHING and debugfs's view equals
# the tool's own expected tree (paths, modes, sizes, link counts, SHA-256s).
# (8) D12: a volume not cleanly unmounted with a wrong free count is
# recounted at the mount and checks clean. (9) The policy: an unknown COMPAT
# or RO_COMPAT flag or the error bit: read-only, nothing written. (10-15)
# THE HOST CUT SWEEP (section D (a)): six fixed sequences (the owner's case;
# a hard-linked file's names removed; folders: mkdir, a file and a folder
# moved to other folders, a file replaced, rmdir; folders removed (the end of
# save E and save E+1 of R3); indirect blocks copied and freed by writes and
# truncates; a folder grown past its blocks), each cut
# after write N for EVERY N: e2fsck -fn may report only the allowed list
# (A8-A11 only for inodes the tool reports changed in the cut save, or the
# save before where the list says so), every file saved before the cut reads
# back exactly (debugfs), and in the owner's case A is exact or gone, gone
# once save 2 has finished, B never holds a block of A, and after saves 3-4
# only leaks and counts. Stage 11 (changed, the owner's approval): every
# saved path the cut save renames must read back exactly at its old path or
# its new path (the stage 9 checker accepted "at neither path"); A9 no
# longer accepts a folder moved in the cut save (R5 keeps it under at least
# one name); A14 (a folder under two names) only for a folder moved to
# another folder in the cut save. (16) The sweep again with the tool built with
# -fsanitize=address,undefined: no report, byte-identical images. Nothing
# boots; everything stays under $OUT/ext2-write.
E9_T0=$(date +%s)
E9_ROOT=$(dirname "$0")/..
E9_W=$OUT/ext2-write
rm -rf "${E9_W:?}" && mkdir -p "$E9_W"
E9_TOOL=$OUT/ext2write
E9_PY=(python3 -I -B "$(dirname "$0")/ext2_cut.py")
E9_VIEW=(python3 -I -B "$(dirname "$0")/ext2_view.py")

# (1) The objects with the kernel's flags, not linked.
E9_OBJW=$OUT/kernel/fs/ext2_write.o
E9_OBJS=$OUT/kernel/storage/blk_core_save.o
E9_DEF=$(nm --defined-only "$OUT/kernel/fs/ext2_core.o" "$E9_OBJW" "$E9_OBJS" 2>/dev/null | awk 'NF == 3 { print $3 }' | LC_ALL=C sort -u)
E9_UNDEF=$(nm -u "$E9_OBJW" "$E9_OBJS" 2>/dev/null | awk 'NF == 2 { print $2 }' | LC_ALL=C sort -u | grep -vxFf <(echo "$E9_DEF") | tr '\n' ' ' | sed 's/ *$//')
E9_SSE=$(objdump -d "$E9_OBJW" "$E9_OBJS" 2>/dev/null | grep -cE '%(x|y|z)mm[0-9]|%st(\(|$)|%mm[0-9]')
E9_MINE=$(nm --defined-only "$E9_OBJW" "$E9_OBJS" 2>/dev/null | awk 'NF == 3 { print $3 }' | LC_ALL=C sort -u)
E9_ELFSYMS=$(nm "$OUT/madire.elf" 2>/dev/null | awk '{ print $NF }')
E9_INELF=$(grep -xFf <(echo "$E9_MINE") <<< "$E9_ELFSYMS" | tr '\n' ' ' | sed 's/ *$//')
E9_PREFIX=$(grep -cE '^(ext2_w_|ext2_write_|blk_sv_)' <<< "$E9_ELFSYMS")
E9_SELF=$("$E9_TOOL" -t 2>&1)
echo "      ext2_write.o text $(size "$E9_OBJW" 2>/dev/null | awk 'NR == 2 { print $1 }') bytes, blk_core_save.o text $(size "$E9_OBJS" 2>/dev/null | awk 'NR == 2 { print $1 }') bytes (report only); $(grep -c . <<< "$E9_MINE") symbols; undefined outside the cores: ${E9_UNDEF:-none}; SSE/x87: $E9_SSE; in madire.elf: ${E9_INELF:-none} (prefixed: $E9_PREFIX); $E9_SELF"
# 1.4 stage 10 (changed): the two files are linked into the kernel now, so
# madire.elf must hold their public calls the kernel uses (ext2_write_mount,
# ext2_write_save_step, ext2_write_create, ext2_write_pwrite, blk_sv_init,
# blk_sv_put, blk_sv_save_step), where stage 9 checked it held none; the
# objects' own checks (no other symbol, no SSE/x87, the tool) are unchanged.
E9_LINKED=$(grep -cxE 'ext2_write_(mount|save_step|create|pwrite)|blk_sv_(init|put|save_step)' <<< "$E9_ELFSYMS")
if [ -s "$E9_OBJW" ] && [ -s "$E9_OBJS" ] && [ -z "$E9_UNDEF" ] && [ "$E9_SSE" -eq 0 ] && [ "$E9_LINKED" -eq 7 ] &&
   [ "$(grep -c . <<< "$E9_MINE")" -ge 40 ] && [ "$E9_SELF" = "SHA256_SELFTEST ok" ]; then
    pass "[1.4] ext2 write core: kernel/fs/ext2_write.c and kernel/storage/blk_core_save.c compiled with the kernel's flags need no symbol but the read core's and each other's, use no SSE/x87 register, and madire.elf holds their seven public calls the kernel uses (linked, stage 10); tests/ext2write.c is built from them (SHA-256 self-test ok)"
else
    fail "[1.4] ext2 write core: an object is missing, needs other symbols (${E9_UNDEF:-none}), uses SSE/x87 ($E9_SSE), madire.elf lacks their public calls ($E9_LINKED of 7; in madire.elf: ${E9_INELF:-none}), or the tool's self-test failed ($E9_SELF)"
fi
echo "      mke2fs fix (report only): the stage 2 image ext2-1k-128 is ${DISK_SA:-?} (stage 8's run 42226698091: 20516434eba0496fedb74c0980b03ec64b55b3d9aa13d77c185fcc5287e0a349)"

# The images. e9_mkimg <name> <tree: a tests/ext2_view.py kind, "hard",
# or "-" for none> <KiB> <block size> <inode size> <inodes> [more mke2fs
# options...]; a failure leaves no image.
e9_mkimg() {
    local name=$1 kind=$2 tree="" rc=0
    if [ "$kind" = hard ]; then
        tree=$E9_W/tree-hard
        if [ ! -d "$tree" ]; then
            mkdir -p "$tree"
            printf 'one inode, two names (stage 9)\n' > "$tree/h1"
            ln "$tree/h1" "$tree/h2"
            for i in $(seq 1 30); do printf 'kept line %02d\n' "$i"; done > "$tree/keep.txt"
            chmod 0644 "$tree/h1" "$tree/keep.txt"
            touch -h -a -m -d "@$DISK_TIME" "$tree" "$tree/h1" "$tree/h2" "$tree/keep.txt"
        fi
    elif [ "$kind" != - ]; then
        tree=$E9_W/tree-$kind
        [ -d "$tree" ] || "${E9_VIEW[@]}" tree "$tree" "$kind" || rc=1
    fi
    [ "$rc" -eq 0 ] && { disk_mkfs "$E9_W/$name.img" "$3" "$4" "$5" "$6" "$tree" "${@:7}" > "$E9_W/mkfs-$name.txt" 2>&1 || rc=$?; }
    sed "s/^/      mke2fs ($name): /" "$E9_W/mkfs-$name.txt" 2>/dev/null
    [ "$rc" -eq 0 ] || rm -f "${E9_W:?}/${name:?}.img"
}
e9_mkimg b1k full 16384 1024 256 512
e9_mkimg b4k full 163840 4096 256 512
e9_mkimg rev0 full 16384 1024 128 512 -r 0 -O none
e9_mkimg dirindex dirindex 8192 1024 256 512
e9_mkimg xattr xattr 8192 1024 256 64
e9_mkimg small - 4096 1024 256 256
e9_mkimg hard hard 4096 1024 256 256
# dir_index: e2fsck -fyD indexes the 400-entry folder (as in stage 6).
LC_ALL=C E2FSCK_CONFIG=/dev/null "$(disk_tool e2fsck)" -fyD "$E9_W/dirindex.img" > "$E9_W/fsck-dirindex.txt" 2>&1
E9_DRC=$?
# ext_attr: attributes too long for a 256-byte inode's spare room, so each
# goes into an attribute block (debugfs -w, on this test image only).
E9_LONG=$(printf 'madire-stage-nine-%.0s' $(seq 1 16))
printf '%s\n' "ea_set /attr/a.txt user.madire $E9_LONG" "ea_set /attr/folder user.madire folder-$E9_LONG" \
    "ea_set /attr/b.txt user.madire b-$E9_LONG" > "$E9_W/xattr.cmd"
LC_ALL=C "$(disk_tool debugfs)" -w -f "$E9_W/xattr.cmd" "$E9_W/xattr.img" > "$E9_W/xattr-set.txt" 2>&1
E9_ACL_A=$(disk_debugfs "$E9_W/xattr.img" "stat /attr/a.txt" | sed -nE 's/.*File ACL: ([0-9]+).*/\1/p' | head -n 1)
E9_ACL_F=$(disk_debugfs "$E9_W/xattr.img" "stat /attr/folder" | sed -nE 's/.*File ACL: ([0-9]+).*/\1/p' | head -n 1)
E9_FLAGS0=$(disk_debugfs "$E9_W/dirindex.img" "stat /many" | sed -nE 's/.*Flags: (0x[0-9a-f]+).*/\1/p' | head -n 1)

# (2) The mke2fs fix and the inode sizes.
E9_WARN=$(cat "$E9_W"/mkfs-*.txt 2>/dev/null | grep -c 'is not defined in mke2fs.conf')
E9_ISZ=""
for e9_n in b1k b4k rev0 dirindex xattr small hard; do
    e9_st=$(disk_debugfs "$E9_W/$e9_n.img" stats 2>/dev/null)
    e9_sz=$(sed -nE 's/^Inode size:[[:space:]]*([0-9]+).*/\1/p' <<< "$e9_st" | head -n 1)
    # Revision 0 has no inode-size field (fixed at 128) and debugfs prints
    # none; a missing line on any other revision still fails.
    if [ -z "$e9_sz" ] && grep -qE '^Filesystem revision #:[[:space:]]*0( |$)' <<< "$e9_st"; then e9_sz=128; fi
    E9_ISZ="$E9_ISZ $e9_n=$e9_sz"
done
echo "      images:$E9_ISZ (inode sizes); 'not defined' warnings: $E9_WARN; e2fsck -fyD exit $E9_DRC; attribute blocks a.txt=${E9_ACL_A:-?} folder=${E9_ACL_F:-?}; /many flags ${E9_FLAGS0:-?}"
if [ "$E9_WARN" -eq 0 ] && [ "$E9_ISZ" = " b1k=256 b4k=256 rev0=128 dirindex=256 xattr=256 small=256 hard=256" ]; then
    pass "[1.4] mke2fs (tests/mke2fs.conf, stage 9 fix): no 'fs_type madire is not defined' warning for any image, and 256-byte inodes in every image of this section but the revision-0 one (128)"
else
    fail "[1.4] mke2fs: $E9_WARN 'not defined' warnings, or inode sizes differ:$E9_ISZ"
fi

# Diagnostic (report only, decides no check): the stage 2 image
# ext2-1k-128 made with stage 8's tests/mke2fs.conf and with this stage's,
# the same command line (stage 2's tree, times and options): dumpe2fs -h of
# both, their differences and the first differing bytes. Stage 8's file is
# rebuilt from this one (its stage 9 comment and madire setting left out,
# inode_size 128) and must have stage 8's SHA-256 (575c9805...).
E9_D=$E9_W/conf-diag
mkdir -p "$E9_D"
awk '/^# 1\.4 stage 9:/ { skip = 1 } /^\[fs_types\]/ { skip = 0 } skip { next } /^\t\tinode_ratio = 4096$/ { next } { sub(/^\tinode_size = 256$/, "\tinode_size = 128"); print }' \
    "$DISK_CONF" > "$E9_D/mke2fs-s8.conf"
echo "      conf diagnostic: stage 8 conf rebuilt, SHA-256 $(sha256sum < "$E9_D/mke2fs-s8.conf" | cut -c1-16)... (stage 8's: 575c98052557...)"
E9_CONF_SAVE=$DISK_CONF
for e9_c in s8 s9; do
    if [ "$e9_c" = s8 ]; then DISK_CONF=$E9_D/mke2fs-s8.conf; else DISK_CONF=$E9_CONF_SAVE; fi
    touch -h -a -m -d "@$DISK_TIME" "${DISK_TOUCH[@]}"
    disk_mkfs "$E9_D/$e9_c.img" 8192 1024 128 256 "$DISK_W/src" > "$E9_D/mkfs-$e9_c.txt" 2>&1
    echo "      conf diagnostic: $e9_c conf: mke2fs exit $?, image $(sha256sum < "$E9_D/$e9_c.img" | cut -d' ' -f1)"
    sed "s/^/      conf diagnostic: mke2fs ($e9_c): /" "$E9_D/mkfs-$e9_c.txt"
    LC_ALL=C "$(disk_tool dumpe2fs)" -h "$E9_D/$e9_c.img" > "$E9_D/dumpe2fs-$e9_c.txt" 2>&1
done
DISK_CONF=$E9_CONF_SAVE
diff "$E9_D/dumpe2fs-s8.txt" "$E9_D/dumpe2fs-s9.txt" | sed 's/^/      conf diagnostic: dumpe2fs -h diff: /'
echo "      conf diagnostic: $(cmp -l "$E9_D/s8.img" "$E9_D/s9.img" 2>/dev/null | wc -l) bytes differ; the first (offset, octal s8, octal s9):"
cmp -l "$E9_D/s8.img" "$E9_D/s9.img" 2>/dev/null | head -n 24 | sed 's/^/      conf diagnostic:   /'

# (3-7) The host sequence "all" on each image.
declare -A E9_FRC=()
for e9_n in b1k b4k dirindex xattr rev0; do
    "${E9_PY[@]}" final "$E9_TOOL" "$E9_W/$e9_n.img" all "$E9_W/run-$e9_n" > "$E9_W/final-$e9_n.txt" 2>&1
    E9_FRC[$e9_n]=$?
    head -n 12 "$E9_W/final-$e9_n.txt" | sed "s/^/      $e9_n: /"
done
e9_final_ok() { [ "${E9_FRC[$1]:-1}" -eq 0 ] && grep -qE "^FINAL all: ok;" "$E9_W/final-$1.txt"; }
e9_paths() { sed -nE 's/^FINAL .* view ([0-9]+) paths.*/\1/p' "$E9_W/final-$1.txt"; }
if e9_final_ok b1k; then
    pass "[1.4] ext2 write core, host sequence on 1 KiB blocks (256-byte inodes, the full tree): every operation through the triple indirect block, a full save and the unmount; e2fsck -fn reports nothing and debugfs's view equals the expected tree ($(e9_paths b1k) paths: names, modes, sizes, link counts, SHA-256s)"
else
    fail "[1.4] ext2 write core, host sequence on 1 KiB blocks: e2fsck -fn reported something, or debugfs's view differs (see $E9_W/final-b1k.txt)"
fi
if e9_final_ok b4k; then
    pass "[1.4] ext2 write core, host sequence on 4 KiB blocks (256-byte inodes, the full tree): e2fsck -fn reports nothing and debugfs's view equals the expected tree ($(e9_paths b4k) paths)"
else
    fail "[1.4] ext2 write core, host sequence on 4 KiB blocks: e2fsck -fn reported something, or debugfs's view differs (see $E9_W/final-b4k.txt)"
fi
E9_FLAGS1=$(disk_debugfs "$E9_W/run-dirindex/uncut.img" "stat /many" | sed -nE 's/.*Flags: (0x[0-9a-f]+).*/\1/p' | head -n 1)
echo "      dirindex: /many flags before ${E9_FLAGS0:-?}, after ${E9_FLAGS1:-?}"
if e9_final_ok dirindex && [ "$E9_DRC" -le 1 ] && [ -n "$E9_FLAGS0" ] && [ $(( E9_FLAGS0 & 0x1000 )) -ne 0 ] &&
   [ -n "$E9_FLAGS1" ] && [ $(( E9_FLAGS1 & 0x1000 )) -eq 0 ]; then
    pass "[1.4] ext2 write core, dir_index: the folder indexed by e2fsck -D (flag 0x1000) changed (an entry added, one removed, one renamed): its index flag cleared, e2fsck -fn reports nothing, debugfs's view equals the expected tree"
else
    fail "[1.4] ext2 write core, dir_index: the indexed folder was not indexed before, kept its flag, or the check failed (flags ${E9_FLAGS0:-?} -> ${E9_FLAGS1:-?}; see $E9_W/final-dirindex.txt)"
fi
E9_TA=$(disk_debugfs "$E9_W/run-xattr/uncut.img" "testb ${E9_ACL_A:-0}")
E9_TF=$(disk_debugfs "$E9_W/run-xattr/uncut.img" "testb ${E9_ACL_F:-0}")
echo "      xattr: block ${E9_ACL_A:-?}: $E9_TA; block ${E9_ACL_F:-?}: $E9_TF"
if e9_final_ok xattr && [ "${E9_ACL_A:-0}" -gt 0 ] && [ "${E9_ACL_F:-0}" -gt 0 ] && grep -q 'not in use' <<< "$E9_TA" &&
   grep -q 'not in use' <<< "$E9_TF"; then
    pass "[1.4] ext2 write core, ext_attr: a file and a folder with an attribute block removed (unlink, rmdir) and a third file renamed: both attribute blocks free after the full save, e2fsck -fn reports nothing, debugfs's view equals the expected tree"
else
    fail "[1.4] ext2 write core, ext_attr: no attribute blocks were made, one is still in use, or the check failed (see $E9_W/final-xattr.txt)"
fi
if e9_final_ok rev0; then
    pass "[1.4] ext2 write core, revision 0 (no features: 16-bit name lengths, no file types, 128-byte inodes): e2fsck -fn reports nothing and debugfs's view equals the expected tree ($(e9_paths rev0) paths)"
else
    fail "[1.4] ext2 write core, revision 0: e2fsck -fn reported something, or debugfs's view differs (see $E9_W/final-rev0.txt)"
fi

# (8) D12: not cleanly unmounted, a wrong free count.
"${E9_VIEW[@]}" set "$E9_W/small.img" "$E9_W/dirty-1.img" $((1024 + 58)) 2 0x0 | sed 's/^/      /'
"${E9_VIEW[@]}" set "$E9_W/dirty-1.img" "$E9_W/dirty.img" $((1024 + 12)) 4 0x10 | sed 's/^/      /'
"${E9_PY[@]}" final "$E9_TOOL" "$E9_W/dirty.img" recount "$E9_W/run-dirty" > "$E9_W/final-dirty.txt" 2>&1
E9_RRC=$?
head -n 6 "$E9_W/final-dirty.txt" | sed 's/^/      recount: /'
if [ "$E9_RRC" -eq 0 ] && grep -qx 'MOUNT rw=1 why=- recounted=1' "$E9_W/run-dirty/uncut.log"; then
    pass "[1.4] ext2 write core, D12: a volume not cleanly unmounted (state 0x0) with a wrong free-block count mounted read-write after the recount from the bitmaps; after a write, the full save and the unmount e2fsck -fn reports nothing"
else
    fail "[1.4] ext2 write core, D12: no recount at the mount, or the volume does not check clean afterwards (see $E9_W/final-dirty.txt)"
fi

# (9) The policy: read-only, nothing written.
E9_ROK=1
for e9_spec in "ro-compat $((1024 + 92)) 4 0x1000 or" "ro-rocompat $((1024 + 100)) 4 0x8 or" "ro-error $((1024 + 58)) 2 0x3"; do
    read -r e9_x e9_off e9_len e9_val e9_how <<< "$e9_spec"
    # shellcheck disable=SC2086
    "${E9_VIEW[@]}" set "$E9_W/small.img" "$E9_W/$e9_x.img" "$e9_off" "$e9_len" "$e9_val" ${e9_how:-} > /dev/null
    "${E9_PY[@]}" readonly "$E9_TOOL" "$E9_W/$e9_x.img" "$E9_W/run-$e9_x" > "$E9_W/$e9_x.txt" 2>&1
    sed 's/^/      /' "$E9_W/$e9_x.txt"
    grep -q '^READONLY .*: ok;' "$E9_W/$e9_x.txt" || E9_ROK=0
done
if [ "$E9_ROK" -eq 1 ] && grep -q 'why=an unknown COMPAT flag' "$E9_W/ro-compat.txt" &&
   grep -q 'why=an unknown RO_COMPAT flag' "$E9_W/ro-rocompat.txt" && grep -q 'why=the error bit is set' "$E9_W/ro-error.txt"; then
    pass "[1.4] ext2 write core, the policy for writes (D11, D9): an unknown COMPAT flag (0x1000), an unknown RO_COMPAT flag (0x8) or the error bit: mounted read-only, a create refused (EROFS), no write, the image unchanged"
else
    fail "[1.4] ext2 write core, the policy for writes: a volume was not read-only, or something was written (see $E9_W/ro-*.txt)"
fi

# (10-16) The cut sweep, then the sanitizers.
E9_SAN=$E9_W/ext2write-san
gcc -std=gnu11 -O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined -fno-sanitize-recover=all \
    -Wall -Wextra -Werror -I"$E9_ROOT" "$E9_ROOT/tests/ext2write.c" "$E9_ROOT/kernel/fs/ext2_core.c" \
    "$E9_ROOT/kernel/fs/ext2_write.c" "$E9_ROOT/kernel/storage/blk_core_save.c" -o "$E9_SAN" > "$E9_W/san-build.txt" 2>&1
E9_SANB=$?
head -n 20 "$E9_W/san-build.txt" | sed 's/^/      sanitizer build: /'
E9_SANRUN=$E9_W/ext2write-san.sh
if command -v setarch > /dev/null 2>&1 && setarch "$(uname -m)" -R true 2>/dev/null; then
    printf '#!/bin/sh\nexec setarch %s -R %s "$@"\n' "$(uname -m)" "$E9_SAN" > "$E9_SANRUN"
else
    printf '#!/bin/sh\nexec %s "$@"\n' "$E9_SAN" > "$E9_SANRUN"
fi
chmod +x "$E9_SANRUN"
E9_SANARG=()
[ "$E9_SANB" -eq 0 ] && E9_SANARG=(--san "$E9_SANRUN")
declare -A E9_SRC=()
for e9_s in owner:small hardlink:hard dirs:small rmdir:small indirect:small grow:small; do
    e9_seq=${e9_s%%:*}
    "${E9_PY[@]}" sweep "$E9_TOOL" "$E9_W/${e9_s#*:}.img" "$e9_seq" "$E9_W/sweep-$e9_seq" "${E9_SANARG[@]}" \
        > "$E9_W/sweep-$e9_seq.txt" 2>&1
    E9_SRC[$e9_seq]=$?
    grep -vE '^  e2fsck: (e2fsck [0-9]|Pass [1-5]:|madire-test: [0-9]|$)|^(ACCEPT|FIRST) ' "$E9_W/sweep-$e9_seq.txt" | head -n 40 | sed "s/^/      $e9_seq: /"
    grep -E '^(ACCEPT|FIRST) ' "$E9_W/sweep-$e9_seq.txt" | sed 's/^/      /'
done
e9_sweep_ok() { [ "${E9_SRC[$1]:-1}" -eq 0 ] && grep -qE "^SWEEP $1: ok;" "$E9_W/sweep-$1.txt"; }
e9_cuts() { sed -nE "s/^SWEEP $1: .*cuts N=0\.\.[0-9]+ \(([0-9]+)\).*/\1/p" "$E9_W/sweep-$1.txt"; }
if e9_sweep_ok owner; then
    pass "[1.4] host cut sweep, the owner's case (save 1 writes A; save 2 deletes A and creates B with no save between; save 3 deletes A's inode; save 4 clears the bitmaps), cut after every write ($(e9_cuts owner) cuts): e2fsck -fn reports only the allowed list (A1-A13 in their contexts; after saves 3-4 only leaks and counts), every saved file reads back exactly, A is exact or gone and gone once save 2 has finished, B never holds a block of A"
else
    fail "[1.4] host cut sweep, the owner's case: a cut left something not on the allowed list, a saved file damaged, or A or B wrong (see $E9_W/sweep-owner.txt)"
fi
if e9_sweep_ok hardlink; then
    pass "[1.4] host cut sweep, a hard-linked file ($(e9_cuts hardlink) cuts): one name removed (its count lowered one save later, R3), then the other (the inode deleted): only the allowed list, every saved file exact"
else
    fail "[1.4] host cut sweep, a hard-linked file: something not on the allowed list, or a saved file damaged (see $E9_W/sweep-hardlink.txt)"
fi
if e9_sweep_ok dirs; then
    pass "[1.4] host cut sweep, folders ($(e9_cuts dirs) cuts): mkdir, a file in a fresh folder (R4), a file replaced by rename, a file and a folder moved to other folders (\"..\", both parents; R5), rmdir: only the allowed list, every saved file exact, every renamed saved path exactly at its old or its new path"
else
    fail "[1.4] host cut sweep, folders: something not on the allowed list, or a saved file damaged (see $E9_W/sweep-dirs.txt)"
fi
if e9_sweep_ok rmdir; then
    pass "[1.4] host cut sweep, folders removed ($(e9_cuts rmdir) cuts): a top-level and a nested folder removed (save E the names and the parents' counts, save E+1 the folders' inodes, save E+2 the bits; R3), cuts at the end of save E and inside save E+1 included: only the allowed list (A9 and A10 for the removed folders and their parents only in their R3 windows), every saved file exact"
else
    fail "[1.4] host cut sweep, folders removed: something not on the allowed list, or a saved file damaged (see $E9_W/sweep-rmdir.txt)"
fi
if e9_sweep_ok indirect; then
    pass "[1.4] host cut sweep, indirect blocks ($(e9_cuts indirect) cuts): writes and truncates through the single and double indirect blocks, each changed indirect block copied (R2): only the allowed list (never a wrong i_size or i_blocks), every saved file exact"
else
    fail "[1.4] host cut sweep, indirect blocks: something not on the allowed list, or a saved file damaged (see $E9_W/sweep-indirect.txt)"
fi
if e9_sweep_ok grow; then
    pass "[1.4] host cut sweep, a folder grown past its blocks ($(e9_cuts grow) cuts): 45 names (the empty block added, then a save, R4), 15 removed, one renamed (R5): only the allowed list, every saved file exact, the renamed file exactly at its old or its new path"
else
    fail "[1.4] host cut sweep, a growing folder: something not on the allowed list, or a saved file damaged (see $E9_W/sweep-grow.txt)"
fi
E9_SANOK=$(cat "$E9_W"/sweep-*.txt 2>/dev/null | grep -cE '^SANITIZE [a-z]+: ok;')
if [ "$E9_SANB" -eq 0 ] && [ "$E9_SANOK" -eq 6 ]; then
    pass "[1.4] host cut sweep under -fsanitize=address,undefined: all six sequences at every cut and uncut, no sanitizer report, every image byte-identical to the plain build's"
else
    fail "[1.4] host cut sweep under the sanitizers: build exit $E9_SANB, $E9_SANOK of 6 sequences clean (see $E9_W/sweep-*.txt)"
fi
echo "      [1.4] ext2 write core and cut sweep: $(( $(date +%s) - E9_T0 )) s (report only)"

# ---- 1.4 stage 10: disk writes behind the guard, Disk mode read-write ----

section "[1.4] Disk writes behind the guard: the guard's proof, test=diskwrite, the write session and persistence, blkdebug (stage 10)"
# docs/plans/1.4.md, S10 (C4, C5 with R1-R4, D5, D8, D9, D12, D16, D19;
# risks 24, 25, 26, 31, 32, 43). The write core and the save order are
# linked; the AHCI driver's one command builder sends WRITE DMA EXT and
# FLUSH CACHE EXT only as the guard allows (the source and ELF checks of
# stages 4 and 8, changed). -kernel boots at -m 128M with one disk on
# ich9-ahci (as stage 5's).
# (1-6) The guard's proof (D5): each disk the guard REFUSED (stage 5's
# unmarked, bad-CRC, wrong-label, start-mismatch and empty disks, and the
# marked disk with a non-virtual model) booted with test=diskwrite, which
# tries 3 writes and 3 flushes through the driver's own calls: all 6
# refused by the command builder, none sent, the image's SHA-256 the same
# before and after.
# (7-9) test=diskwrite on a marked read-write disk (16 MiB, 1 KiB blocks):
# 1 MiB written through the VFS, saved, read back; rewritten, truncated,
# saved, read back; removed and saved (DONE); then the node test in the
# same boot (1100 files in one folder, more than the 1024-node table: all
# listed with eviction; every node held: getdents64's records call answers
# ENOMEM; all removed and saved); after the boot e2fsck -fn reports nothing
# and debugfs finds neither left. The write speed, FLUSH's cost, the
# longest VFS lock hold of a write call and of a save piece and the longest
# turn are report only (the owner's STOP rule: a hold during disk writes
# above 2 ticks is reported before anything else).
# (10-12) The write session (boot A, no test= option: the automatic save
# on): a console session on a marked disk holding stage 8's tree: mkdir,
# '>' and '>>', cp, cat '>>' (a 28 KiB file through the single indirect
# block), mv in a folder and between folders, rm, rmdir, sync (twice: the
# second has nothing to save), then a write and a removal after the last
# sync, then reboot (it saves first: "Saving files... done."). After it
# e2fsck -fn reports nothing and debugfs's tree equals the expected tree
# (tests/ext2_tree.py; names, types, sizes, SHA-256s).
# (13-15) Persistence (boot B, the SAME image): the volume is clean (the
# save before Restart marked it), mounted read-write; ls -l and cat read
# everything back exactly; after the boot e2fsck -fn reports nothing and
# the tree is still the expected one.
# (16-18) test=blkdebug (the driver's own error path, injected): the first
# disk read of a file's data fails once ("cat: 'big.txt': I/O error.",
# error 1 of 3, the port running again), the same cat then prints the file;
# every disk write fails: sync fails (error 2), the error state's write
# fails too (error 3: the disk marked failed), /home is read-only from then
# on; no panic, the session ends.
E10_T0=$(date +%s)
E10_W=$OUT/disk-write
rm -rf "$E10_W" && mkdir -p "$E10_W"
E10_OK=$GD_OK
E10_FSCK=$(disk_tool e2fsck)
E10_TREE_PY=(python3 -I -B "$(dirname "$0")/ext2_tree.py")
# e10_fsck <disk image> <tag>: its partition extracted; true when e2fsck -fn
# exits 0 and prints only its frame lines (nothing to report).
e10_fsck() {
    local part=$E10_W/$2-part.img out rc extra
    disk_image extract "$1" "$part" > /dev/null 2>&1 || { echo "      e2fsck ($2): no partition"; return 1; }
    out=$(LC_ALL=C E2FSCK_CONFIG=/dev/null "$E10_FSCK" -fn "$part" 2>&1); rc=$?
    extra=$(grep -vE '^(e2fsck [0-9]+\.[0-9]+.*|Pass [1-5]: .*|madire-test: [0-9]+/[0-9]+ files \(.*\), [0-9]+/[0-9]+ blocks|)$' <<< "$out")
    sed "s/^/      e2fsck ($2, exit $rc): /" <<< "$out"
    [ "$rc" -eq 0 ] && [ -z "$extra" ]
}
# e10_health <log>: System ready (or the session's own end), no PANIC or
# FAILED line.
e10_health() { [ -s "$1" ] && grep -q '^System ready\.' "$1" && ! grep -qE 'PANIC|FAILED' "$1"; }

# (1-6) The guard's proof.
for e10_spec in "nomarker:the unmarked disk" "badcrc:the bad marker CRC32" "label:the wrong label" \
                "model:the non-virtual model" "start:the marker's start mismatch" "empty:the empty disk"; do
    e10_n=${e10_spec%%:*}; e10_what=${e10_spec#*:}
    case $e10_n in
    empty) e10_src=$GD_W/empty.img ;;
    model) e10_src=$GD_W/mbr.img ;;
    *) e10_src=$GD_W/mbr-$e10_n.img ;;
    esac
    e10_img=$E10_W/guard-$e10_n.img
    cp "$e10_src" "$e10_img" 2>/dev/null || E10_OK=0
    e10_s0=$(sha256sum < "$e10_img" | cut -d' ' -f1)
    e10_log=$OUT/serial-diskwrite-$e10_n.log
    e10_model=""
    [ "$e10_n" = model ] && e10_model=",model=MADIRE-REAL-DISK-TEST"
    boot_until "$e10_log" 60 "$PCI_READY" -m 128M -kernel "$KERNEL" -append "test=diskwrite" \
        -drive file="$e10_img",if=none,id=dwtest-disk,format=raw -device ich9-ahci,id=dwtest-ahci,addr=0x5 \
        -device "ide-hd,drive=dwtest-disk,bus=dwtest-ahci.0$e10_model"
    e10_s1=$(sha256sum < "$e10_img" | cut -d' ' -f1)
    grep -E '^(Storage|diskwrite): ' "$e10_log" | sed "s/^/      $e10_n: /"
    echo "      $e10_n: SHA-256 before $e10_s0, after $e10_s1"
    if [ "$E10_OK" -eq 1 ] && [ -n "$e10_s0" ] && [ "$e10_s0" = "$e10_s1" ] &&
       grep -qxF "diskwrite: the disk on port 0: the write guard REFUSED it; 3 WRITE DMA EXT (LBA 0, 2050, 4096) and 3 FLUSH CACHE EXT tried through the driver's calls: 3 and 3 refused; 0 writes and 0 flushes sent, 6 refused by the command builder" "$e10_log" &&
       grep -qxF "diskwrite: DONE (every write and flush refused by the guard, nothing sent to the disk)" "$e10_log" &&
       grep -qE '^Storage: .*, writes would be REFUSED: .*; read only, not mounted \(Live mode\)$' "$e10_log" &&
       [ "$(grep -cE '^(ext2|mount): ' "$e10_log")" -eq 0 ] && e10_health "$e10_log"; then
        pass "[1.4] write guard proof (D5), $e10_what: test=diskwrite tried 3 WRITE DMA EXT and 3 FLUSH CACHE EXT through the driver's calls; the command builder refused all 6 and sent none; nothing mounted; the image's SHA-256 is the same before and after ($e10_s0)"
    else
        fail "[1.4] write guard proof (D5), $e10_what: a write or a flush was not refused, the image changed ($e10_s0 -> ${e10_s1:-?}), or the boot was not healthy (see serial-diskwrite-$e10_n.log)"
    fi
done

# (7-9) test=diskwrite on a marked read-write disk, and the node test.
E10_DW=$E10_W/dw-disk.img
disk_mkfs "$E10_W/dw-fs.img" 16384 1024 256 2048 "" > "$E10_W/dw-mkfs.txt" 2>&1 &&
    disk_image mbr "$E10_W/dw-fs.img" "$E10_DW" || E10_OK=0
E10_DWLOG=$OUT/serial-diskwrite-rw.log
boot_until "$E10_DWLOG" 240 "$PCI_READY" -m 128M -kernel "$KERNEL" -append "test=diskwrite" \
    -drive file="$E10_DW",if=none,id=dwrw-disk,format=raw -device ich9-ahci,id=dwrw-ahci,addr=0x5 \
    -device ide-hd,drive=dwrw-disk,bus=dwrw-ahci.0
grep -E '^(mount|diskwrite|disknodes|ext2): ' "$E10_DWLOG" | sed 's/^/      diskwrite: /'
E10_CRC=$(sed -nE 's/^diskwrite: read back after the sync: CRC32 (0x[0-9a-f]{8}) \(expected (0x[0-9a-f]{8})\); after the rewrite, the truncate to 512 KiB and a sync: CRC32 (0x[0-9a-f]{8}) \(expected (0x[0-9a-f]{8})\)$/\1 \2 \3 \4/p' "$E10_DWLOG")
read -r e10_c1 e10_w1 e10_c2 e10_w2 <<< "${E10_CRC:-x y z w}"
if [ "$E10_OK" -eq 1 ] && [ "$e10_c1" = "$e10_w1" ] && [ "$e10_c2" = "$e10_w2" ] &&
   grep -qxF "diskwrite: DONE (1 MiB written, saved, read back; rewritten, truncated, saved, read back; removed and saved)" "$E10_DWLOG" &&
   grep -qE '^diskwrite: disk writes: [1-9][0-9]* commands, ' "$E10_DWLOG" &&
   grep -qE '^diskwrite: the longest VFS lock hold: a write call [0-9]+ us \([0-9]+ ticks\), a save piece [0-9]+ us \([0-9]+ ticks\); ' "$E10_DWLOG" &&
   grep -qE "^mount: ext2 'madire-test' \(port 0, partition 1\) on /home as filesystem 1, read-write; " "$E10_DWLOG" &&
   grep -qE '^mount: changes are kept in memory .*; automatic save every 5 s: off \(a test= boot\)$' "$E10_DWLOG" &&
   e10_health "$E10_DWLOG"; then
    pass "[1.4] test=diskwrite on a read-write disk: 1 MiB written through the VFS, saved, read back (CRC32 $e10_c1); 64 KiB rewritten, truncated to 512 KiB, saved, read back (CRC32 $e10_c2); removed and saved; the automatic save off on this test= boot; no PANIC or FAILED line"
else
    fail "[1.4] test=diskwrite on a read-write disk: not DONE, a read-back CRC32 differs ($E10_CRC), a figure line is missing or the boot was not healthy (see serial-diskwrite-rw.log)"
fi
if grep -qE '^disknodes: 1100 files made in /home/nodes \([0-9]+ saves; [0-9]+ an operation needed\); listed 1100 entries \(end: the end\); the node table: ([0-9]|[1-9][0-9]{1,2}|10[01][0-9]|102[0-4]) nodes now \(at most 1024\), [1-9][0-9]* evicted$' "$E10_DWLOG" &&
   grep -qE '^disknodes: the table full: [0-9]+ nodes held, the listing ended with ENOMEM; getdents64.s records call from there answered -12 \(ENOMEM\)$' "$E10_DWLOG" &&
   grep -qxF "disknodes: the holds given back: 1100 entries listed again (end: the end)" "$E10_DWLOG" &&
   grep -qxF "disknodes: 1100 files and the folder removed (rmdir ok); saved: yes" "$E10_DWLOG" &&
   grep -qxF "disknodes: DONE (more files than the node table holds, listed with eviction; ENOMEM when every node is in use)" "$E10_DWLOG"; then
    pass "[1.4] node eviction (risk 43): 1100 files in one folder of /home, more than the 1024-node table, all listed with nodes evicted (the table at most 1024); with every node held, getdents64's records call answers ENOMEM; the holds given back, all 1100 listed again; all removed and saved"
else
    fail "[1.4] node eviction: the 1100 files were not all listed with eviction, the full table did not answer ENOMEM, or the files were not removed (see the disknodes: lines in serial-diskwrite-rw.log)"
fi
E10_DWLS=$(disk_image extract "$E10_DW" "$E10_W/dw-part.img" > /dev/null 2>&1 &&
           LC_ALL=C "$(disk_tool debugfs)" -R "ls -l /" "$E10_W/dw-part.img" 2>/dev/null | grep -cE ' (diskwrite\.bin|nodes)$')
if e10_fsck "$E10_DW" diskwrite && [ "${E10_DWLS:-1}" -eq 0 ]; then
    pass "[1.4] test=diskwrite on a read-write disk: after the boot e2fsck -fn reports nothing on the partition, and debugfs finds neither /diskwrite.bin nor /nodes (every block and inode given back through R3)"
else
    fail "[1.4] test=diskwrite on a read-write disk: e2fsck -fn reported something after the boot, or something was left (${E10_DWLS:-?} names; see the e2fsck lines above)"
fi
echo "      [1.4] (report only) $(grep -E '^diskwrite: (wrote|disk writes|the longest)' "$E10_DWLOG" | sed 's/^diskwrite: //' | tr '\n' ';')"

# (10-12) The write session (boot A) and (13-15) persistence (boot B).
E10_SS=$E10_W/session-disk.img
disk_mkfs "$E10_W/session-fs.img" 16384 1024 256 1024 "$E8_TREE" > "$E10_W/session-mkfs.txt" 2>&1 &&
    disk_image mbr "$E10_W/session-fs.img" "$E10_SS" || E10_OK=0
E10_EXP=$E10_W/expected
rm -rf "$E10_EXP" && cp -a "$E8_TREE" "$E10_EXP" && (
    cd "$E10_EXP" && mkdir -p work/sub &&
    printf 'first line\nsecond line\n' > work/one.txt && cat big.txt big.txt > work/big2.txt &&
    printf 'inner\n' > work/sub/inner.txt && mv notes.txt work/notes-moved.txt && mv z-last.txt last.txt &&
    rm m-middle.txt && rmdir empty && printf 'after sync\n' > work/late.txt && rm docs/b.txt &&
    mkdir -p user/Desktop user/Documents user/Downloads user/Pictures user/Music user/Videos user/Trash
) || E10_OK=0
# 1.4 stage 12 (changed, the owner's decision): boot A, a normal boot, makes
# the home folders at the mount (D18; in the expected tree above) and the
# shell starts in /home/user, so both sessions begin with "cd /home" (the
# commands after it as before); boot B finds the folders and makes none.
E10_KA="x{wait:1}cd /home{ret}mkdir work{ret}echo first line > work/one.txt{ret}{wait:1}echo second line >> work/one.txt{ret}{wait:1}"
E10_KA+="cp -y big.txt work/big2.txt{ret}{wait:2}cat big.txt >> work/big2.txt{ret}{wait:3}mkdir work/sub{ret}"
E10_KA+="echo inner > work/sub/inner.txt{ret}{wait:1}mv -y notes.txt work/notes-moved.txt{ret}{wait:1}"
E10_KA+="mv -y z-last.txt last.txt{ret}{wait:1}rm -y m-middle.txt{ret}{wait:1}rmdir empty{ret}mkdir gone{ret}rmdir gone{ret}"
E10_KA+="sync{ret}{wait:2}sync{ret}{wait:1}echo after sync > work/late.txt{ret}{wait:1}rm -y docs/b.txt{ret}{wait:1}"
E10_KA+="reboot{ret}{wait:1}yes{ret}"
E10_LA=$OUT/serial-diskwrite-session.log
T0S=$(date +%s%N)
boot_text "$E10_LA" 180 "System ready." "$E10_KA" 0.05 $'\nRestarting...\n' -m 128M -kernel "$KERNEL" \
    -drive file="$E10_SS",if=none,id=e10a-disk,format=raw -device ich9-ahci,id=e10a-ahci,addr=0x5 \
    -device ide-hd,drive=e10a-disk,bus=e10a-ahci.0
boot_seconds "$E10_LA" "$T0S"
grep -E '^(mount|ext2|sync|Saving files|Restarting)|^(console: restart)' "$E10_LA" | sed 's/^/      session: /'
# The two syncs: the first saves what the automatic save has not saved yet
# (it may have saved everything: "nothing to save" then), the second has
# nothing left to save.
E10_SYNC1=$(grep -cE '^sync: [1-9][0-9]* blocks? saved\.$' "$E10_LA")
E10_SYNC0=$(grep -cxF "sync: nothing to save." "$E10_LA")
if [ "$E10_OK" -eq 1 ] && e10_health "$E10_LA" &&
   grep -qE "^mount: ext2 'madire-test' \(port 0, partition 1\) on /home as filesystem 1, read-write; " "$E10_LA" &&
   grep -qE '^mount: changes are kept in memory .*; automatic save every 5 s: on$' "$E10_LA" &&
   [ $((E10_SYNC1 + E10_SYNC0)) -eq 2 ] && [ "$E10_SYNC0" -ge 1 ] &&
   grep -qE "^madire> sync$" "$E10_LA" &&
   grep -qxF "Saving files... done." "$E10_LA" && grep -qxF "Restarting..." "$E10_LA" &&
   grep -qE '^ext2: save before restart: done in [0-9]+ ms \([0-9]+ saves, [0-9]+ writes; 0 open files with no name left forgotten first\); the volume marked clean$' "$E10_LA" &&
   ! grep -qE ': (the filesystem is read-only|I/O error|no room|already exists|no such file or folder|no room left on the disk|the file is at the disk.s size limit)\.$' "$E10_LA" &&
   ! grep -qE '^ahci: port [0-9]+: .* failed: ' "$E10_LA"; then
    pass "[1.4] write session on a marked disk (read-write, the automatic save on): mkdir, '>' and '>>', cp, cat '>>' through the single indirect block, mv in a folder and between folders, rm, rmdir; sync ($E10_SYNC1 with blocks to save), a second sync had nothing to save; reboot saved first ('Saving files... done.', the volume marked clean) and restarted; no error line"
else
    fail "[1.4] write session on a marked disk: not mounted read-write, a command failed, a sync line is missing, or reboot did not save first (see serial-diskwrite-session.log)"
fi
cp "$E10_SS" "$E10_W/session-after-a.img"
if e10_fsck "$E10_SS" session-a; then
    pass "[1.4] write session: after boot A (the last changes saved by reboot only) e2fsck -fn reports nothing on the partition"
else
    fail "[1.4] write session: e2fsck -fn reported something after boot A (see the e2fsck lines above)"
fi
"${E10_TREE_PY[@]}" compare "$E10_W/session-a-part.img" "$E10_EXP" > "$E10_W/tree-a.txt" 2>&1
grep -vE '^  want ' "$E10_W/tree-a.txt" | sed 's/^/      tree (A): /'
if [ "$E10_OK" -eq 1 ] && grep -qE '^TREE ok [0-9]+ paths$' "$E10_W/tree-a.txt"; then
    pass "[1.4] write session: debugfs's tree after boot A equals the expected tree ($(sed -nE 's/^TREE ok ([0-9]+) paths$/\1/p' "$E10_W/tree-a.txt") paths: every name, type, size and SHA-256; the changes after the last sync included, saved by reboot)"
else
    fail "[1.4] write session: debugfs's tree after boot A differs from the expected tree (see $E10_W/tree-a.txt)"
fi
E10_KB="x{wait:1}cd /home{ret}ls -l{ret}ls -l work{ret}ls -l work/sub{ret}ls -l docs{ret}cat work/one.txt{ret}{wait:1}"
E10_KB+="cat work/sub/inner.txt{ret}{wait:1}cat work/notes-moved.txt{ret}{wait:2}cat work/late.txt{ret}{wait:1}"
E10_KB+="cat last.txt{ret}{wait:1}cat work/big2.txt{ret}{wait:6}echo end-of-persistence{ret}"
E10_LB=$OUT/serial-diskwrite-persist.log
T0S=$(date +%s%N)
boot_text "$E10_LB" 180 "System ready." "$E10_KB" 0.05 $'\nend-of-persistence\n' -m 128M -kernel "$KERNEL" \
    -drive file="$E10_SS",if=none,id=e10b-disk,format=raw -device ich9-ahci,id=e10b-ahci,addr=0x5 \
    -device ide-hd,drive=e10b-disk,bus=e10b-ahci.0
boot_seconds "$E10_LB" "$T0S"
grep -E '^(ext2|mount): ' "$E10_LB" | sed 's/^/      persist: /'
if [ "$E10_OK" -eq 1 ] && e10_health "$E10_LB" &&
   grep -qE "^ext2: port 0 partition 1: revision 1, 1024-byte blocks, [0-9]+ blocks \([0-9]+ free\), [0-9]+ inodes \([0-9]+ free\), [0-9]+ groups, 256-byte inodes, label 'madire-test'; features .*; state 0x1 \(clean, error bit not set\)$" "$E10_LB" &&
   [ "$(grep -c '^ext2: ' "$E10_LB")" -eq 1 ] &&
   grep -qE "^mount: ext2 'madire-test' \(port 0, partition 1\) on /home as filesystem 1, read-write; " "$E10_LB" &&
   ! grep -qE '^ahci: port [0-9]+: .* failed: ' "$E10_LB"; then
    pass "[1.4] persistence: the second boot on the same image finds the volume clean (state 0x1: the save before Restart marked it), no recount and no warning, and mounts it read-write"
else
    fail "[1.4] persistence: the second boot did not find the volume clean or did not mount it read-write (see serial-diskwrite-persist.log)"
fi
"${E10_TREE_PY[@]}" session "$E10_LB" "$E10_EXP" > "$E10_W/session-b.txt" 2>&1
sed 's/^/      persist: /' "$E10_W/session-b.txt"
if [ "$E10_OK" -eq 1 ] && grep -qxF "SESSION ok 10 commands" "$E10_W/session-b.txt"; then
    pass "[1.4] persistence: in the second boot ls -l of /home, work, work/sub and docs lists the expected entries, and cat prints six files exactly (the 28 KiB one through the single indirect block, and the one written after the last sync): everything reads back"
else
    fail "[1.4] persistence: the second boot did not read everything back exactly (see $E10_W/session-b.txt)"
fi
if e10_fsck "$E10_SS" session-b && "${E10_TREE_PY[@]}" compare "$E10_W/session-b-part.img" "$E10_EXP" > "$E10_W/tree-b.txt" 2>&1 &&
   grep -qE '^TREE ok [0-9]+ paths$' "$E10_W/tree-b.txt"; then
    pass "[1.4] persistence: after the second boot e2fsck -fn reports nothing and debugfs's tree is still the expected one"
else
    fail "[1.4] persistence: after the second boot e2fsck -fn reported something or the tree changed (see $E10_W/tree-b.txt)"
fi

# The owner's rule (stage 10, option 2) had exempted the boot-time
# read-write mount from the 2-tick rule. 1.5 stage 4 (docs/plans/1.5.md,
# C1, H2 item 3, a CHANGED CHECK): the exemption is gone; no write or FLUSH
# command is sent while the VFS lock is held, the boot mount included (save
# 0 runs under the disk lock only). On the four read-write mount boots
# (diskwrite, the write session's boot A, stage 8's not-clean volume,
# persistence): the mount's "blk:" line says 0 such commands since boot,
# and, as before, the mount's hold line once and before 'System ready.',
# and no other thread asked for the VFS lock or waited during the mount.
E10_MH=""
E10_MBAD=""
for e10_ml in "$E10_DWLOG" "$E8_L-dirty.log" "$E10_LA" "$E10_LB"; do
    e10_order=$(awk '/^mount: the mount held the VFS lock / && !h { h = NR } /^System ready\./ && !r { r = NR }
                     END { if (h && r && h < r) print "ok" }' "$e10_ml" 2>/dev/null)
    e10_t=$(sed -nE 's/^mount: the mount held the VFS lock ([0-9]+) us \(([0-9]+) ticks\).*/\1 us, \2 ticks/p' "$e10_ml" 2>/dev/null |
            head -n 1)
    E10_MH="$E10_MH ${e10_ml##*/} ${e10_t:-no hold line};"
    if [ "$e10_order" != ok ] || [ "$(grep -c '^mount: the mount held the VFS lock ' "$e10_ml" 2>/dev/null)" != 1 ] ||
       [ "$(grep -cxF 'blk: the mount: save 0 under the disk lock only; 0 write or FLUSH commands sent while the VFS lock was held (since boot); 0 other threads asked for the VFS lock during its hold, 0 wait steps (report only)' \
            "$e10_ml" 2>/dev/null)" != 1 ]; then
        E10_MBAD="$E10_MBAD ${e10_ml##*/}"
    fi
done
echo "      [1.4] the read-write mounts' holds of the VFS lock (report only):$E10_MH"
if [ -z "$E10_MBAD" ]; then
    pass "[1.4] boot-time read-write mount (no exemption since 1.5 stage 4): on the four read-write mount boots (diskwrite, the write session's boot A, the not-clean volume, persistence) no write or FLUSH command was sent while the VFS lock was held, the boot mount included (save 0 under the disk lock only; 0 since boot at the mount's line); the mount's hold of the VFS lock is logged once, before 'System ready.', and no other thread asked for the lock or waited during it (0 tickets, 0 wait steps)"
else
    fail "[1.4] boot-time read-write mount: a write or FLUSH sent under the VFS lock, the mount's lines missing, repeated or after 'System ready.', or another thread asked for or waited on the VFS lock during the mount:$E10_MBAD"
fi

# (16-18) test=blkdebug.
E10_BD=$E10_W/blkdebug-disk.img
disk_mkfs "$E10_W/blkdebug-fs.img" 8192 1024 128 256 "$E8_TREE" > "$E10_W/blkdebug-mkfs.txt" 2>&1 &&
    disk_image mbr "$E10_W/blkdebug-fs.img" "$E10_BD" || E10_OK=0
E10_KD="x{wait:1}cat big.txt{ret}{wait:3}cat big.txt{ret}{wait:4}echo hi > new.txt{ret}{wait:1}sync{ret}{wait:2}"
E10_KD+="echo x > n2.txt{ret}{wait:1}cat docs/a.txt{ret}{wait:1}echo end-of-blkdebug{ret}"
E10_LD=$OUT/serial-blkdebug.log
T0S=$(date +%s%N)
# Stage 10 diagnostics (decide no check): the blkdebug boot runs as
# boot_text runs it (the same keys, gap, done text, time limit, -no-reboot
# and the same "[0.5] key injection" failure), and QEMU's stderr and exit
# status are kept, so QEMU ending by itself is seen (S10: QEMU 8.2.2
# segfaulted when the old injection stopped a port during a write, runs
# 42323216993, 42323230806 and 42330910354). The stderr file goes to
# $QEMU_WATCH_DIR (uploaded with the run).
E10_QERR=$QEMU_WATCH_DIR/blkdebug-qemu-stderr.txt
rm -f "$E10_LD" "$OUT/qmp.sock" "$E10_QERR"
timeout 150 qemu-system-x86_64 -display none -no-reboot -serial file:"$E10_LD" \
    -qmp unix:"$OUT/qmp.sock",server=on,wait=off -m 128M -kernel "$KERNEL" \
    -append "test=blkdebug" \
    -drive file="$E10_BD",if=none,id=e10d-disk,format=raw -device ich9-ahci,id=e10d-ahci,addr=0x5 \
    -device ide-hd,drive=e10d-disk,bus=e10d-ahci.0 > /dev/null 2> "$E10_QERR" &
E10_QPID=$!
E10_KOUT=$(python3 "$QMPKEYS" "$OUT/qmp.sock" "$E10_LD" "System ready." --text "$E10_KD" --gap 0.05 \
           --done $'\nend-of-blkdebug\n' 2>&1)
echo "$E10_KOUT"
case "$E10_KOUT" in *ERROR*) fail "[0.5] key injection into $E10_LD" ;; esac
E10_QALIVE=no
kill -0 "$E10_QPID" 2>/dev/null && E10_QALIVE=yes
sleep 2
kill "$E10_QPID" 2>/dev/null
wait "$E10_QPID" 2>/dev/null
E10_QEXIT=$?
[ -f "$E10_LD" ] || : > "$E10_LD"
boot_seconds "$E10_LD" "$T0S"
echo "      [1.4] blkdebug diagnostics (report only): QEMU running when the keys ended: $E10_QALIVE; exit status $E10_QEXIT (143: stopped by the harness, 124: the time limit, 134: abort)"
echo "      [1.4] blkdebug diagnostics: QEMU stderr ($(cat "$E10_QERR" 2>/dev/null | wc -l) lines):"
sed 's/^/      blkdebug stderr: /' "$E10_QERR" 2>/dev/null | head -n 40
echo "      [1.4] blkdebug diagnostics: the serial log's last lines:"
tail -n 5 "$E10_LD" | sed 's/^/      blkdebug tail: /'
grep -E '^(ahci: port 0: (READ|WRITE)|blk: test=|ext2: sync|sync: |cat: |Output to )' "$E10_LD" | sed 's/^/      blkdebug: /'
E10_BIG=$(stat -c%s "$E8_TREE/big.txt" 2>/dev/null)
E10_E1=$(grep -cE '^ahci: port 0: READ DMA EXT LBA [0-9]+, [0-9]+ sectors failed: the device reported an error \(PxIS 0x[0-9a-f]{8}, PxTFD 0x[0-9a-f]{8}, PxSERR 0x[0-9a-f]{8}, PxCI 0x[0-9a-f]{8}\); (no port reset|port reset \(COMRESET\) once); port running again; error 1 of 3, then EIO$' "$E10_LD")
if [ "$E10_OK" -eq 1 ] && [ "$E10_E1" -eq 1 ] && grep -qxF "cat: 'big.txt': I/O error." "$E10_LD" &&
   grep -qxF "blk: test=blkdebug: armed: the first disk read during a file read fails once, and every disk write fails" "$E10_LD" &&
   [ "$(grep -cE "^console: cat: $E10_BIG bytes in " "$E10_LD")" -eq 1 ] && ! grep -q 'PANIC' "$E10_LD"; then
    pass "[1.4] test=blkdebug: an injected read error during a file read: logged once (error 1 of 3, the port running again), EIO, cat prints \"cat: 'big.txt': I/O error.\"; the same cat then prints all $E10_BIG bytes; no panic, no retry loop"
else
    fail "[1.4] test=blkdebug: the injected read error was not logged once, cat did not print its I/O error, or the second cat did not print the file (see serial-blkdebug.log)"
fi
if grep -qE '^ahci: port 0: WRITE DMA EXT LBA [0-9]+, [0-9]+ sectors failed: .*; port running again; error 2 of 3, then EIO$' "$E10_LD" &&
   grep -qE '^ahci: port 0: WRITE DMA EXT LBA [0-9]+, [0-9]+ sectors failed: .*; disk marked failed; error 3 of 3, then EIO$' "$E10_LD" &&
   grep -qxF "sync: the disk could not be written; /home is read-only now." "$E10_LD" &&
   grep -qE '^ext2: sync: not saved \(a disk write failed\); ' "$E10_LD" &&
   grep -qxF "Output to 'n2.txt': the filesystem is read-only." "$E10_LD"; then
    pass "[1.4] test=blkdebug: every disk write fails: sync's first write (error 2) and the error state's write (error 3: the disk marked failed) logged once each; sync says the disk could not be written; /home is read-only from then on (a new file: 'the filesystem is read-only')"
else
    fail "[1.4] test=blkdebug: the write errors, the failed disk, sync's line or the read-only filesystem are not as expected (see serial-blkdebug.log)"
fi
E10_THIRD=$(grep -E "^cat: 'docs/a\.txt': " "$E10_LD" | head -n 1)
if grep -q 'end-of-blkdebug' "$E10_LD" && ! grep -qE 'PANIC|FAILED' "$E10_LD" && grep -q '^System ready\.' "$E10_LD" &&
   [ "$(grep -cE '^ahci: port 0: .* failed: ' "$E10_LD")" -eq 3 ]; then
    pass "[1.4] test=blkdebug: after the disk failed the system goes on (the session ends normally): no panic, no PANIC or FAILED line, exactly three disk errors logged (a failed disk sends no more commands); cat of another file (report only): ${E10_THIRD:-printed from the cache}"
else
    fail "[1.4] test=blkdebug: the session did not end normally after the disk failed, a PANIC or FAILED line, or more than three disk errors (see serial-blkdebug.log)"
fi
echo "      [1.4] disk writes: $(( $(date +%s) - E10_T0 )) s (report only)"

# ---- 1.4 stage 11: power cuts in QEMU, the unordered sweep, test=autosave ----

section "[1.4] Power cuts: the unordered host sweep, test=diskcut at N1 and N2 with the remount, test=autosave (stage 11)"
# docs/plans/1.4.md, S11 (C5 with R5, section D with A14 and the narrowed
# A9, G4; risks 32, 39). (1-6) The unordered host sweep: a real disk may
# store the writes between two FLUSHes in any order, so for each of stage
# 9's six sequences, a cut inside every flush interval whose writes cover
# two or more filesystem blocks, with a subset of them on the disk (every
# proper non-empty subset up to 8 blocks, a fixed sample above):
# e2fsck -fn only the allowed list in its contexts, every saved file
# exact, every renamed saved path exact at its old or new path, the
# owner's case rules. A sector torn inside a block stays outside the model.
# (7) test=diskcut, uncut: the owner's case through the VFS (save 1 writes
# A; A removed and B made with no save between; saves 2 to 4); the kernel's
# writes equal the host model's one by one (tests/diskcut-lib.sh). (8-11)
# The two sharded cut points (section D): N1, the last file-data write of
# save 2 (B's data on the disk, none of its pointers, A not yet removed),
# and N2, the last write before save 2's first folder write (B's inode
# written, the folder block that removes A and adds B not yet): each a cut
# boot judged on the image (e2fsck -fn's allowed list in the model's
# contexts, every saved file exact, A exact, B never holding A's blocks)
# and a remount boot on the same image (the D12 recount, read-write, A and
# B read back as debugfs reads them). The other cut points run in the
# stress part. (12-14) test=autosave: a file written at the console, the
# automatic save's line awaited, then QEMU killed (no sync, no reboot): the
# image holds the file exactly, e2fsck -fn reports only leaks and counts,
# and the remount boot reads it back. The longest VFS lock hold of a save
# after System ready is reported (the owner's 2-tick rule).
E11_T0=$(date +%s)
. "$(dirname "$0")/diskcut-lib.sh"
E11_W=$OUT/power-cut
rm -rf "${E11_W:?}" && mkdir -p "$E11_W"

# (1-6) The unordered host sweep.
declare -A E11_URC=()
for e11_s in owner:small hardlink:hard dirs:small rmdir:small indirect:small grow:small; do
    e11_seq=${e11_s%%:*}
    "${E9_PY[@]}" usweep "$E9_TOOL" "$E9_W/${e11_s#*:}.img" "$e11_seq" "$E11_W/usweep-$e11_seq" \
        > "$E11_W/usweep-$e11_seq.txt" 2>&1
    E11_URC[$e11_seq]=$?
    grep -E '^(UNORDERED|  cut )' "$E11_W/usweep-$e11_seq.txt" | head -n 30 | sed "s/^/      $e11_seq: /"
    grep -E '^(UACCEPT|UFIRST) ' "$E11_W/usweep-$e11_seq.txt" | sed 's/^/      /'
done
e11_uok() { [ "${E11_URC[$1]:-1}" -eq 0 ] && grep -qE "^UNORDERED $1: ok;" "$E11_W/usweep-$1.txt"; }
e11_ucuts() { sed -nE "s/^UNORDERED $1: .*; ([0-9]+) cuts, bad .*/\1/p" "$E11_W/usweep-$1.txt"; }
e11_uinfo() { sed -nE "s/^UNORDERED $1: ok; ([0-9]+ flush intervals, [0-9]+ with 2 or more units \([0-9]+ with every subset, [0-9]+ sampled\)).*/\1/p" "$E11_W/usweep-$1.txt"; }
for e11_spec in "owner|the owner's case" "hardlink|a hard-linked file" \
                "dirs|folders (a file and a folder moved to other folders: R5)" "rmdir|folders removed" \
                "indirect|indirect blocks" "grow|a folder grown past its blocks (a rename across its blocks: R5)"; do
    e11_seq=${e11_spec%%|*}
    e11_what=${e11_spec#*|}
    if e11_uok "$e11_seq"; then
        pass "[1.4] unordered host sweep, $e11_what ($(e11_ucuts "$e11_seq") cuts; $(e11_uinfo "$e11_seq")): any subset of a flush interval's blocks on the disk at the cut; e2fsck -fn reports only the allowed list in its contexts, every saved file reads back exactly, every renamed saved path exactly at its old or its new path"
    else
        fail "[1.4] unordered host sweep, $e11_what: a cut left something not on the allowed list, or a saved file damaged or at neither of its paths (see $E11_W/usweep-$e11_seq.txt)"
    fi
done

# (7) The uncut run and the model.
dc_setup "$E11_W/owner" "$E9_TOOL" static
echo "      N1=$DC_N1 N2=$DC_N2; the model's writes before its unmount: $DC_TOTAL"
grep -E '^QWRITE ' "$DC_W/model.txt" | sed 's/^/      model: /'
if [ "$DC_SETUP_OK" -eq 1 ]; then
    pass "[1.4] test=diskcut, uncut: the owner's case through the VFS (save 1 writes A; A removed and B made with no save between; saves 2 to 4) ends with 'diskcut: DONE', and the kernel's $DC_TOTAL disk writes equal the host model's one by one (number, class, partition offset, length; R5's classes); no PANIC or FAILED line"
else
    fail "[1.4] test=diskcut, uncut: the model failed, the boot did not end with DONE, or the kernel's writes differ from the model's (see $DC_W/model.txt, $DC_W/compare.txt, ${DC_UNCUT##*/})"
fi

# (8-11) The two sharded cut points.
for e11_p in "N1|${DC_N1:-0}|the last file-data write of save 2: B's data on the disk, none of B's pointers, A not yet removed" \
             "N2|${DC_N2:-0}|the last write before save 2's first folder write: B's inode written, the folder block not yet"; do
    IFS='|' read -r e11_name e11_n e11_what <<< "$e11_p"
    DC_CUT_OK=0
    DC_RE_OK=0
    [ "$DC_SETUP_OK" -eq 1 ] && dc_point "$e11_n"
    if [ "$DC_CUT_OK" -eq 1 ]; then
        pass "[1.4] power cut $e11_name (test=diskcut=$e11_n; $e11_what): the boot halted after exactly $e11_n writes; e2fsck -fn reports only the allowed list in its contexts, every saved file reads back exactly, A is exact and B holds none of A's blocks"
    else
        fail "[1.4] power cut $e11_name (test=diskcut=$e11_n): the boot did not halt after exactly $e11_n writes, or the image failed section D's checks (see $DC_W/qcut-$e11_n.txt)"
    fi
    if [ "$DC_RE_OK" -eq 1 ]; then
        pass "[1.4] power cut $e11_name, the remount boot on the same image: the D12 recount (not cleanly unmounted), mounted read-write, and Madire reads A and B exactly as debugfs does ($(sed -nE 's/^QVIEW ok; //p' "$DC_W/qview-$e11_n.txt"))"
    else
        fail "[1.4] power cut $e11_name, the remount boot: no recount, not read-write, or Madire's reading differs from debugfs's (see $DC_W/qview-$e11_n.txt)"
    fi
done

# (12-14) test=autosave.
E11_AS=$E11_W/autosave
mkdir -p "$E11_AS"
E11_ASOK=1
disk_mkfs "$E11_AS/fs.img" 4096 1024 256 256 "" > "$E11_AS/mkfs.txt" 2>&1 &&
    disk_image mbr "$E11_AS/fs.img" "$E11_AS/disk.img" > /dev/null 2>&1 || E11_ASOK=0
E11_ASTEXT="autosave-test-line"
E11_ASLOG=$OUT/serial-autosave.log
boot_text "$E11_ASLOG" 120 "System ready." "x{wait:1}echo $E11_ASTEXT > auto.txt{ret}" 0.05 \
    $'\next2: automatic save (test=autosave): ' -m 128M -kernel "$KERNEL" -append "test=autosave" \
    -drive file="$E11_AS/disk.img",if=none,id=e11as-disk,format=raw -device ich9-ahci,id=e11as-ahci,addr=0x5 \
    -device ide-hd,drive=e11as-disk,bus=e11as-ahci.0
grep -E '^(mount: changes|ext2: automatic save|madire> echo|madire> sync|Saving files)' "$E11_ASLOG" | sed 's/^/      autosave: /'
E11_HOLD=$(sed -nE 's/^ext2: automatic save \(test=autosave\): .*; the longest VFS lock hold in a save so far ([0-9]+ us \([0-9]+ ticks\)).*/\1/p' "$E11_ASLOG" | tail -n 1)
echo "      [1.4] the longest VFS lock hold of a save after System ready (the automatic save; report only, the owner's 2-tick rule): ${E11_HOLD:-none}"
if [ "$E11_ASOK" -eq 1 ] && dc_health "$E11_ASLOG" &&
   grep -qE '^mount: changes are kept in memory .*; automatic save every 5 s: on \(test=autosave\)$' "$E11_ASLOG" &&
   grep -qxF "madire> echo $E11_ASTEXT > auto.txt" "$E11_ASLOG" &&
   grep -qE '^ext2: automatic save \(test=autosave\): saved; [1-9][0-9]* saves, [1-9][0-9]* writes, [1-9][0-9]* flushes; ' "$E11_ASLOG" &&
   ! grep -qE '^(madire> sync|Saving files)' "$E11_ASLOG"; then
    pass "[1.4] test=autosave: the automatic save on in this test boot; a file written at the console was saved by the automatic save within 5 s (its line logged), with no sync and no reboot; then QEMU was killed (the power cut)"
else
    fail "[1.4] test=autosave: the automatic save was not on, its line never came, or a sync or reboot ran (see serial-autosave.log)"
fi
disk_image extract "$E11_AS/disk.img" "$E11_AS/part.img" > /dev/null 2>&1
"${DC_PY[@]}" fsckstrict "$E11_AS/part.img" > "$E11_AS/fsck.txt" 2>&1
E11_FRC=$?
sed 's/^/      autosave: /' "$E11_AS/fsck.txt"
E11_GOT=$(disk_debugfs "$E11_AS/part.img" "cat /auto.txt" | sha256sum | cut -d' ' -f1)
E11_EXP=$(printf '%s\n' "$E11_ASTEXT" | sha256sum | cut -d' ' -f1)
echo "      autosave: /auto.txt on the image: SHA-256 $E11_GOT (expected $E11_EXP)"
if [ "$E11_ASOK" -eq 1 ] && [ "$E11_FRC" -eq 0 ] && [ "$E11_GOT" = "$E11_EXP" ]; then
    pass "[1.4] test=autosave: after the cut the image holds /auto.txt exactly ('$E11_ASTEXT' and a newline), and e2fsck -fn reports only leaks and counts"
else
    fail "[1.4] test=autosave: after the cut /auto.txt is not exact, or e2fsck -fn reported more than leaks and counts (see $E11_AS/fsck.txt)"
fi
E11_ASR=$OUT/serial-autosave-remount.log
dc_boot "$E11_ASR" 90 "$DC_READY" "test=diskcheck" "$E11_AS/disk.img"
disk_image extract "$E11_AS/disk.img" "$E11_AS/after.img" > /dev/null 2>&1
"${DC_PY[@]}" qview "$E11_AS/after.img" "$E11_ASR" auto.txt > "$E11_AS/qview.txt" 2>&1
E11_VRC=$?
grep -E '^(ext2: warning|diskcheck): ' "$E11_ASR" | sed 's/^/      autosave remount: /'
sed 's/^/      autosave remount: /' "$E11_AS/qview.txt"
E11_WANT=$(printf '%s\n' "$E11_ASTEXT" | python3 -I -B -c 'import sys, zlib; b = sys.stdin.buffer.read(); print("%d bytes CRC32 0x%08x" % (len(b), zlib.crc32(b)))')
if [ "$E11_ASOK" -eq 1 ] && [ "$E11_VRC" -eq 0 ] && grep -qF "auto.txt: Madire $E11_WANT, debugfs $E11_WANT" "$E11_AS/qview.txt" &&
   grep -qE "$DC_RECOUNT" "$E11_ASR" && grep -qE "$DC_RW" "$E11_ASR" && dc_health "$E11_ASR"; then
    pass "[1.4] test=autosave: the next boot (the D12 recount, read-write) reads /auto.txt back exactly ($E11_WANT)"
else
    fail "[1.4] test=autosave: the next boot did not recount, mount read-write or read /auto.txt back exactly (want $E11_WANT; see $E11_AS/qview.txt)"
fi
echo "      [1.4] power cuts: $(( $(date +%s) - E11_T0 )) s (report only)"

# ---- 1.4 stage 12: Disk mode's About and Restart scenes ----

section "[1.4] Disk mode's About window and Restart dialog against tests/desktop_ref.py, five modes (stage 12)"
# docs/plans/1.4.md, C10: the toolkit built into the host tool
# (build/desktopdump) draws stage 4a's two About windows and stage 5's
# Restart dialog with Disk mode's texts, read-write ("Disk (home is
# saved)", "Files in /home are saved first.") and read-only ("Disk (home
# read-only)", "Disk is read-only; /home is unchanged."): scenes
# windows_disk, windows_diskro, dialog_disk and dialog_diskro; each equals
# tests/desktop_ref.py's rendering pixel for pixel. Five modes, both pixel
# formats. Live mode's scenes (stages 4a and 5) are unchanged.
desktop12_static() {   # desktop12_static <W> <H> <bytes per pixel> <scene>
    local a=$OUT/desktop-c-$1x$2-$4.ppm b=$OUT/desktop-py-$1x$2-$4.ppm t0 n
    rm -f "$a" "$b"
    "$OUT/desktopdump" "$1" "$2" "$3" "$4" "$a" 2>&1 | sed 's/^/      /'
    t0=$(date +%s)
    python3 "$DREF" "$1" "$2" "$4" "$b" 2>&1 | sed 's/^/      /'
    n=$(python3 - "$a" "$b" <<'EOF' 2>&1
import sys
a, b = open(sys.argv[1], "rb").read(), open(sys.argv[2], "rb").read()
print("same" if a == b and len(a) > 100 else
      "%d bytes differ" % sum(x != y for x, y in zip(a, b)) if len(a) == len(b) else "sizes differ")
EOF
)
    echo "      $1x$2x$(($3 * 8)), scene $4: $n (the reference in $(( $(date +%s) - t0 )) s)"
    if [ "$n" = same ]; then
        pass "[1.4] the Disk-mode scene $4 at $1x$2x$(($3 * 8)) (ui_about.c, ui_dialog.c) equals tests/desktop_ref.py's rendering pixel for pixel"
    else
        fail "[1.4] the Disk-mode scene $4 at $1x$2x$(($3 * 8)): $n"
    fi
}
for mode in "800 600 4" "1024 768 4" "1280 800 3" "1920 1080 4" "2560 1440 4"; do
    for scene in windows_disk windows_diskro dialog_disk dialog_diskro; do
        # shellcheck disable=SC2086
        desktop12_static $mode $scene
    done
done

# ---- 1.5 stage 6: Shut down ----

section "[1.5] Shut down: poweroff -y in Live mode and in Disk mode (QEMU's guest-shutdown event), the fallback screen with -machine acpi=off (stage 6)"
# docs/plans/1.5.md, C3, C6b, D, G (S6), risks 47-49 and 64. Each boot types
# "poweroff -y" at the console (tests/qmp_keys.py on one QMP monitor);
# tests/qmp_power.py watches QEMU's events on another (power_boot,
# qemu-lib.sh): QEMU's PIIX4 power management sends SHUTDOWN with "reason":
# "guest-shutdown" when the guest writes SLP_TYP and SLP_EN (ACPI S5), then
# QEMU exits. (1-2) Live mode (-kernel, no disk): the console's line,
# "Power: shutting down (Live mode)", one ACPI: line (the tables, _S5's two
# values, PM1a_CNT, SCI_EN), no save and no fallback screen; the event
# within 5 s of the Power: line, QEMU's own exit. (3-5) Disk mode (a marked
# read-write test disk, no test= option: the home folders made, the
# automatic save on): a file written, then poweroff -y: the save before
# power-off ("done ...; the volume marked clean"), "Saving files... done.",
# the ACPI: line, the event within 10 s (the save's limit, D9); the image
# afterwards: e2fsck -fn reports nothing, the volume clean, the file there.
# (6-7) -machine acpi=off (QEMU builds no ACPI tables): "ACPI: no ACPI tables
# found (...)", the fallback screen's two lines on serial and in the cell
# grid (VGA text, read through QMP, rows 1 and 3), and QEMU still running
# 2 s later (halted with interrupts off; stopped within the stall watch's
# 6 s).
PW_T0=$(date +%s)
PW_W=$OUT/poweroff
rm -rf "${PW_W:?}" && mkdir -p "$PW_W/tree"
PW_OK=1
PW_ACPI='^ACPI: RSDP at 0x[0-9a-f]+ \(revision [0-9]+\), (RSDT|XSDT) at 0x[0-9a-f]+ \([0-9]+ entries\), FADT at 0x[0-9a-f]+ \([0-9]+ bytes\), DSDT at 0x[0-9a-f]+ \([0-9]+ bytes\); _S5 SLP_TYPa [0-7], SLP_TYPb [0-7]; PM1a_CNT 0x[0-9a-f]+, PM1b_CNT (0x[0-9a-f]+|none); SCI_EN (set|set after ACPI_ENABLE 0x[0-9a-f]+ to SMI_CMD 0x[0-9a-f]+)$'
PW_SAFE="It is now safe to turn off the computer."
pw_lines() { grep -E '^(console: power-off|Power: |ACPI: |ext2: save before power-off|Saving files)|safe to turn off|could not power off' "$1" | sed "s/^/      $2: /"; }

# (1-2) Live mode.
PW_LL=$OUT/serial-poweroff-live.log
power_boot "$PW_LL" 90 "x{wait:1}poweroff -y{ret}" "Power: shutting down (Live mode)" -m 128M -kernel "$KERNEL"
pw_lines "$PW_LL" live
if RE="$PW_ACPI" awk '
        $0 == "console: power-off confirmed (-y)" && !c { c = NR }
        $0 == "Power: shutting down (Live mode)" && c && !p { p = NR }
        $0 ~ ENVIRON["RE"] { a++; if (p && !an) an = NR }
        END { exit !(c && p && an && c < p && p < an && a == 1) }' "$PW_LL" &&
   ! grep -qE '^ext2: save before power-off|^Saving files|safe to turn off|could not power off' "$PW_LL"; then
    pass "[1.5] poweroff -y in Live mode: 'console: power-off confirmed (-y)', then 'Power: shutting down (Live mode)', then one ACPI: line (RSDP, RSDT or XSDT, FADT, DSDT, _S5's SLP_TYPa and SLP_TYPb, PM1a_CNT, SCI_EN); no save and no fallback screen"
else
    fail "[1.5] poweroff -y in Live mode: the console's, Power: and ACPI: lines not in order and once, or a save or the fallback screen (see ${PW_LL##*/})"
fi
if [ "$POWER_EVENT" = SHUTDOWN:guest-shutdown ] && [[ "$POWER_MS" =~ ^[0-9]+$ ]] && [ "$POWER_MS" -le 5000 ] &&
   [ "$POWER_RC" -eq 0 ] && [ "$(grep -c '^boot32: start$' "$PW_LL")" -eq 1 ] && ! grep -qE 'PANIC|FAILED|^Restart: ' "$PW_LL"; then
    pass "[1.5] poweroff -y in Live mode: QEMU sent SHUTDOWN (reason guest-shutdown) $POWER_MS ms after 'Power: shutting down (Live mode)' (limit 5 s) and exited by itself; booted once; no PANIC, FAILED or Restart: line"
else
    fail "[1.5] poweroff -y in Live mode: no guest-shutdown event within 5 s, or QEMU did not exit by itself (${POWER_OUT:-no POWER line}; status $POWER_RC)"
fi

# (3-5) Disk mode.
printf 'kept on the disk\n' > "$PW_W/tree/note.txt"
disk_mkfs "$PW_W/fs.img" 16384 1024 256 2048 "$PW_W/tree" > "$PW_W/mkfs.txt" 2>&1 &&
    disk_image mbr "$PW_W/fs.img" "$PW_W/disk.img" > /dev/null 2>&1 || PW_OK=0
PW_DL=$OUT/serial-poweroff-disk.log
if [ "$PW_OK" -eq 1 ]; then
    power_boot "$PW_DL" 120 "x{wait:1}echo power-file > /home/user/power.txt{ret}{wait:1}poweroff -y{ret}" \
        "Power: shutting down (Disk mode)" -m 128M -kernel "$KERNEL" \
        -drive file="$PW_W/disk.img",if=none,id=pw-disk,format=raw -device ich9-ahci,id=pw-ahci,addr=0x5 \
        -device ide-hd,drive=pw-disk,bus=pw-ahci.0
else : > "$PW_DL"; POWER_EVENT=none; POWER_MS=none; POWER_RC=1; fi
pw_lines "$PW_DL" disk
if [ "$PW_OK" -eq 1 ] && RE="$PW_ACPI" awk '
        $0 == "console: power-off confirmed (-y)" && !c { c = NR }
        $0 == "Power: shutting down (Disk mode)" && c && !p { p = NR }
        /^ext2: save before power-off: done in [0-9]+ ms \([0-9]+ saves, [0-9]+ writes; 0 open files with no name left forgotten first\); the volume marked clean$/ { s++; if (p && !sn) sn = NR }
        $0 == "Saving files... done." && sn && !d { d = NR }
        $0 ~ ENVIRON["RE"] { a++; if (d && !an) an = NR }
        END { exit !(c && p && sn && d && an && c < p && p < sn && sn < d && d < an && s == 1 && a == 1) }' "$PW_DL" &&
   ! grep -qE 'safe to turn off|could not power off|powering off anyway' "$PW_DL"; then
    pass "[1.5] poweroff -y in Disk mode: the console's line, 'Power: shutting down (Disk mode)', the save before power-off ('done in <n> ms ...; the volume marked clean'), 'Saving files... done.', then one ACPI: line; no fallback screen"
else
    fail "[1.5] poweroff -y in Disk mode: the lines not in order and once, the save not done, or the fallback screen (see ${PW_DL##*/})"
fi
if [ "$POWER_EVENT" = SHUTDOWN:guest-shutdown ] && [[ "$POWER_MS" =~ ^[0-9]+$ ]] && [ "$POWER_MS" -le 10000 ] &&
   [ "$POWER_RC" -eq 0 ] && [ "$(grep -c '^boot32: start$' "$PW_DL")" -eq 1 ] && ! grep -qE 'PANIC|FAILED|^Restart: ' "$PW_DL"; then
    pass "[1.5] poweroff -y in Disk mode: QEMU sent SHUTDOWN (reason guest-shutdown) $POWER_MS ms after 'Power: shutting down (Disk mode)' (the save included; limit 10 s) and exited by itself; booted once; no PANIC, FAILED or Restart: line"
else
    fail "[1.5] poweroff -y in Disk mode: no guest-shutdown event within 10 s, or QEMU did not exit by itself (${POWER_OUT:-no POWER line}; status $POWER_RC)"
fi
disk_image extract "$PW_W/disk.img" "$PW_W/part.img" > /dev/null 2>&1
PW_STATE=$(disk_debugfs "$PW_W/part.img" stats | sed -nE 's/^Filesystem state:[[:space:]]*(.*)$/\1/p')
PW_FSCK=$(disk_fsck "$PW_W/part.img"); PW_FRC=$?
PW_FX=$(grep -vE '^(e2fsck [0-9]+\.[0-9]+.*|Pass [1-5]: .*|madire-test: [0-9]+/[0-9]+ files \(.*\), [0-9]+/[0-9]+ blocks|)$' <<< "$PW_FSCK")
PW_FILE=$(disk_debugfs "$PW_W/part.img" "cat /user/power.txt" | sha256sum | cut -d' ' -f1)
echo "      disk after the power-off: state ${PW_STATE:-?}; e2fsck -fn exit $PW_FRC${PW_FX:+: $PW_FX}"
if [ "$PW_OK" -eq 1 ] && [ "$PW_STATE" = clean ] && [ "$PW_FRC" -eq 0 ] && [ -z "$PW_FX" ] &&
   [ "$PW_FILE" = "$(printf 'power-file\n' | sha256sum | cut -d' ' -f1)" ]; then
    pass "[1.5] poweroff -y in Disk mode: after the power-off the volume is marked clean, e2fsck -fn reports nothing, and /user/power.txt (written just before) is on the disk"
else
    fail "[1.5] poweroff -y in Disk mode: the volume not clean (${PW_STATE:-?}), e2fsck -fn exit $PW_FRC${PW_FX:+ ($PW_FX)}, or /user/power.txt not saved"
fi

# (6-7) -machine acpi=off: the fallback screen.
PW_AL=$OUT/serial-poweroff-acpioff.log
PW_SOCK=$OUT/qmp-poweroff.sock
rm -f "$PW_AL" "$PW_SOCK"
timeout 90 qemu-system-x86_64 -display none -no-reboot -serial file:"$PW_AL" \
    -qmp unix:"$PW_SOCK",server=on,wait=off -machine acpi=off -m 128M -kernel "$KERNEL" >/dev/null 2>&1 &
PW_QPID=$!
python3 "$QMPKEYS" "$PW_SOCK" "$PW_AL" "System ready." --text "x{wait:1}poweroff -y{ret}" --gap 0.05 2>&1 |
    sed 's/^/      /'
PW_WHY='(no ACPI tables found|an ACPI table failed its checks|no _S5 sleep state|ACPI mode could not be enabled|the computer did not power off)'
python3 "$QMPSCREEN" "$PW_SOCK" "$OUT/screen-poweroff-acpioff.bin" --log "$PW_AL" \
    --wait '^Madire OS could not power off by itself' 1 --row 1 "^It is now safe to turn off the computer\.$" \
    --row 3 "^Madire OS could not power off by itself \\($PW_WHY\\)\\.$" > "$OUT/screen-poweroff-acpioff.txt" 2>&1
PW_SCREEN_RC=$?
sleep 2
if kill -0 "$PW_QPID" 2>/dev/null; then PW_ALIVE=yes; else PW_ALIVE=no; fi
kill "$PW_QPID" 2>/dev/null
wait "$PW_QPID" 2>/dev/null
[ -f "$PW_AL" ] || : > "$PW_AL"
pw_lines "$PW_AL" acpi=off
grep -E '^(row 0[0-4]|CHECK)' "$OUT/screen-poweroff-acpioff.txt" | sed 's/^/      acpi=off screen: /'
echo "      acpi=off: QEMU still running 2 s after the fallback screen: $PW_ALIVE"
PW_REASON=$(sed -nE "s/^Madire OS could not power off by itself \\($PW_WHY\\)\\.$/\\1/p" "$PW_AL" | head -n 1)
PW_ROW3=$(sed -nE 's/^row 03\|(.*)\|$/\1/p' "$OUT/screen-poweroff-acpioff.txt")
echo "      acpi=off: the reason '${PW_REASON:-none}' (expected with QEMU's -machine acpi=off: no ACPI tables found)"
if SAFE="$PW_SAFE" WHY="$PW_REASON" awk '
        $0 == "Power: shutting down (Live mode)" && !p { p = NR }
        /^ACPI: / { a++; if (p && !an) an = NR }
        $0 == ENVIRON["SAFE"] && an && !s { s = NR }
        $0 == "Madire OS could not power off by itself (" ENVIRON["WHY"] ")." && s && !r { r = NR }
        END { exit !(p && an && s && r && a == 1) }' "$PW_AL" && [ -n "$PW_REASON" ] && ! grep -qE 'PANIC|FAILED' "$PW_AL"; then
    pass "[1.5] -machine acpi=off: 'Power: shutting down (Live mode)', one ACPI: line, then the fallback screen's two lines on serial: '$PW_SAFE' and 'Madire OS could not power off by itself ($PW_REASON).' (one of C6b's reasons); no PANIC or FAILED line"
else
    fail "[1.5] -machine acpi=off: the ACPI: line or the fallback screen's lines missing or out of order on serial (see ${PW_AL##*/})"
fi
if [ "$PW_SCREEN_RC" -eq 0 ] && [ "$PW_ROW3" = "Madire OS could not power off by itself ($PW_REASON)." ] && [ "$PW_ALIVE" = yes ]; then
    pass "[1.5] -machine acpi=off: the fallback screen's two lines in the cell grid (rows 1 and 3, read through QMP; the reason as on serial), and QEMU still running 2 s later (halted, not powered off)"
else
    fail "[1.5] -machine acpi=off: the fallback screen not in the cell grid (screen-poweroff-acpioff.txt) or QEMU not running (still running: $PW_ALIVE)"
fi
echo "      [1.5] Shut down: $(( $(date +%s) - PW_T0 )) s (report only)"
