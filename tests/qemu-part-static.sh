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
# madire.elf) and its test=diskread boot.

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
# with Cancel on hover, Cancel pressed, Restart pressed. menu_tiles and
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
    local a=$OUT/desktop-c-$1x$2-$4.ppm b=$OUT/desktop-py-$1x$2-$4.ppm t0 n
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
        pass "[1.3] the menu scene $4 at $1x$2x$(($3 * 8)) (ui_menu.c, ui_dialog.c, desktop_core.c) equals tests/desktop_ref.py's rendering pixel for pixel"
    else
        fail "[1.3] the menu scene $4 at $1x$2x$(($3 * 8)): $n"
    fi
}
for mode in "800 600 4" "1024 768 4" "1280 800 3" "1920 1080 4" "2560 1440 4"; do
    for scene in mbutton_hover mbutton_pressed menu_mouse menu_key menu_hover menu_desktop dialog dialog_key \
                 dialog_tab dialog_pressed_cancel dialog_pressed_restart; do
        # shellcheck disable=SC2086
        desktop5_static $mode $scene
    done
    # shellcheck disable=SC2086
    desktop5_static $mode menu_tiles menu_key
    # shellcheck disable=SC2086
    desktop5_static $mode dialog_tiles dialog_tab
done
CONTRAST5=$(python3 "$DREF" --contrast "$PAIRS5" 2>&1)
printf '%s\n' "$CONTRAST5" | sed 's/^/      /'
if [ "$(printf '%s\n' "$CONTRAST5" | tail -n 1)" = "CONTRAST result: PASS" ] &&
   printf '%s\n' "$CONTRAST5" | grep -qE '^CONTRAST tables: .*; [1-9][0-9]* distinct pairs from 11 scenes \(dialog dialog_key dialog_pressed_cancel dialog_pressed_restart dialog_tab mbutton_hover mbutton_pressed menu_desktop menu_hover menu_key menu_mouse\)$' &&
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
# The sample files, with fixed modes (ctime cannot be set: both images are
# made from this one folder).
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
# 128-byte inodes, 256 inodes; made twice.
DISK_MK=""
for x in a b; do
    touch -h -a -m -d "@$DISK_TIME" "${DISK_TOUCH[@]}"
    disk_mkfs "$DISK_W/$x.img" 8192 1024 128 256 "$DISK_W/src" > "$DISK_W/mkfs-$x.txt" 2>&1
    DISK_MK="$DISK_MK $?"
    sed "s/^/      mke2fs ($x): /" "$DISK_W/mkfs-$x.txt"
done
DISK_SA=$(sha256sum < "$DISK_W/a.img" | cut -d' ' -f1)
DISK_SB=$(sha256sum < "$DISK_W/b.img" | cut -d' ' -f1)
echo "      mke2fs exit:$DISK_MK; a.img $DISK_SA; b.img $DISK_SB ($(stat -c%s "$DISK_W/a.img" 2>/dev/null) bytes)"
echo "DISK_IMAGE_SHA256 ext2-1k-128 $DISK_SA (report only: the same tools give the same image in every run)"
if [ "$DISK_MK" = " 0 0" ] && [ -s "$DISK_W/a.img" ] && [ "$DISK_SA" = "$DISK_SB" ]; then
    pass "[1.4] disk tools: mke2fs with tests/mke2fs.conf, the fixed UUID, hash seed and time makes the same ext2 image twice (SHA-256 identical)"
else
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
PCI_LINE_RE='^PCI: ([0-9]+) buses, ([0-9]+) devices, ([0-9]+) functions, ([0-9]+) PCI-to-PCI bridges \(([0-9]+) secondary buses refused\); storage: ([0-9]+) AHCI, ([0-9]+) other; configuration read only$'
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
    local q b d f r qa q5 qx qo
    PK=$(sed -nE "s/$PCI_LINE_RE/\1 \2 \3 \4 \5 \6 \7/p" "$1")
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
# none (the two lines next to each other, as in stage 3).
pcitest_order() {
    awk '$0 == "Filesystem self-test: PASS" { f = NR } /^PCI: / { p = NR } /^Storage: / { s = NR }
         /^Key events: / && !k { k = NR }
         p && !s && NR > p && !/^PCI: / && !/^(ahci|blk): / { other++ }
         /^(ahci|blk): / && (!p || s) { other++ }
         END { exit !(f && p == f + 1 && s > p && k == s + 1 && !other) }' "$1" &&
        grep -qxF "$PCI_READY" "$1" && ! grep -qE 'PANIC|FAILED' "$1"
}
if pcitest_order "$PCI_A" && pcitest_order "$PCI_B" && pcitest_order "$PCI_C" &&
   [ "$(grep -cE '^(ahci|blk): ' "$PCI_A")" -eq 0 ]; then
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
# (1) The source: the opcode table holds 0xEC and 0x25 only; the FIS's
# command byte is set in one place, from it; a command header's first
# word is set in one place, without the write bit; only ahci_core.c
# issues commands (PxCI); no name of a write or flush command anywhere.
AH_TABLE=$(awk '/^static const uint8_t ahci_opcodes\[AHCI_OPS\] __attribute__\(\(used\)\) = \{$/ { on = 1; next }
                on && /^\};$/ { exit } on { print $1 }' "$AH_SRC/storage/ahci_core.c" | tr '\n' ' ')
AH_OPS=$(awk '/^enum ahci_op \{$/ { on = 1; next } on && /^\};$/ { exit } on { print $1 }' "$AH_SRC/storage/ahci_core.h" | tr '\n' ' ')
AH_FIS=$(grep -c 'fis\[2\]' "$AH_SRC/storage/ahci_core.c")
AH_HDR=$(grep -cE 'hdr\[0\] *=' "$AH_SRC/storage/ahci_core.c")
AH_CI=$(grep -rlE 'AHCI_PX_CI\b' "$AH_SRC" | grep -v 'storage/ahci_core\.[ch]$' | tr '\n' ' ')
AH_NAMES=$(grep -rniE 'write_dma|flush_cache|op_write|op_flush|0x35u|0xeau' "$AH_SRC/storage" | tr '\n' ' ')
echo "      opcode table: $AH_TABLE; enum ahci_op: $AH_OPS; fis[2] lines: $AH_FIS; hdr[0] lines: $AH_HDR; other files using PxCI: ${AH_CI:-none}"
if [ "$AH_TABLE" = "0xECu, 0x25u, " ] && [ "$AH_OPS" = "AHCI_OP_IDENTIFY, AHCI_OP_READ, AHCI_OPS, " ] &&
   [ "$AH_FIS" -eq 1 ] && grep -qxF '    fis[2] = code;' "$AH_SRC/storage/ahci_core.c" &&
   [ "$(grep -cF 'ahci_opcode(op)' "$AH_SRC/storage/ahci_core.c")" -eq 1 ] &&
   grep -qxF '    uint8_t code = ahci_opcode(op);' "$AH_SRC/storage/ahci_core.c" &&
   [ "$AH_HDR" -eq 1 ] && grep -qF 'hdr[0] = AHCI_CFL_H2D | (n << 16);' "$AH_SRC/storage/ahci_core.c" &&
   [ -z "$AH_CI" ] && [ -z "$AH_NAMES" ]; then
    pass "[1.4] AHCI read only (source): the opcode table holds IDENTIFY DEVICE (0xEC) and READ DMA EXT (0x25) only, the FIS's command byte is set once and from it, the command header's first word once and without the write bit, only ahci_core.c issues commands, and no write or flush command is named"
else
    fail "[1.4] AHCI read only (source): the opcode table, the FIS's command byte, the header's write bit or the command issue is not as stage 4 allows (table: $AH_TABLE; ops: $AH_OPS; fis[2]: $AH_FIS; hdr[0]: $AH_HDR; PxCI elsewhere: ${AH_CI:-none}; names: ${AH_NAMES:-none})"
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
# ah_table <object or ELF>: the bytes of its ahci_opcodes as hex ("ec25"),
# its size from nm on AH_TSIZE; nothing if it has no such symbol.
ah_table() {
    local addr size
    read -r addr size <<< "$(nm -S "$1" 2>/dev/null | awk '$4 == "ahci_opcodes" { print $1, $2; exit }')"
    AH_TSIZE=${size:-}
    [ -n "${addr:-}" ] || return 0
    objdump -s -j .rodata --start-address=0x$addr --stop-address=$(printf '0x%x' $((16#$addr + 16#${size:-0}))) "$1" 2>/dev/null |
        awk '$1 ~ /^[0-9a-f]+$/ && NF >= 2 { print $2; exit }'
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
AH_BYTES=$(ah_table "$AH_ELF"); AH_SIZE=$AH_TSIZE
AH_DIS=$OUT/ahci-objdump.txt
objdump -d --no-show-raw-insn "$AH_ELF" > "$AH_DIS" 2>/dev/null
read -r AH_FNS AH_INSNS AH_EMPTY <<< "$(ah_scan "$AH_DIS" "$OUT/ahci-objdump-flagged.txt")"
AH_BAD=$(cat "$OUT/ahci-objdump-flagged.txt")
echo "      madire.elf: ahci_opcodes ${AH_SIZE:-?} bytes: ${AH_BYTES:-?}; $AH_FNS storage functions, $AH_INSNS instructions examined (minimum $AH_MIN_INSNS), $AH_EMPTY with none; write-type opcode stores: $(grep -c . <<< "$AH_BAD")"
[ -n "$AH_BAD" ] && sed 's/^/      store: /' <<< "$AH_BAD"
if [ "${AH_SIZE:-}" = 0000000000000002 ] && [ "$AH_BYTES" = ec25 ] && [ "$AH_FNS" -ge 10 ] &&
   [ "$AH_INSNS" -ge "$AH_MIN_INSNS" ] && [ "$AH_EMPTY" -eq 0 ] && [ -z "$AH_BAD" ]; then
    pass "[1.4] AHCI read only (madire.elf): ahci_opcodes is 2 bytes, EC 25, as linked; no ahci_, blk_ or pci_ function ($AH_FNS of them, $AH_INSNS instructions examined, at least $AH_MIN_INSNS needed and one in each) stores a write-type ATA opcode as an immediate byte"
else
    fail "[1.4] AHCI read only (madire.elf): ahci_opcodes is not the 2 bytes EC 25 (${AH_SIZE:-no symbol}: ${AH_BYTES:-none}), too few storage functions or instructions were examined ($AH_FNS functions, $AH_INSNS instructions, minimum $AH_MIN_INSNS, $AH_EMPTY with none), or a storage function stores a write-type opcode"
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
AH_SBYTES=$(ah_table "$AH_SAMPLE")
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
grep -E '^(PCI|ahci|blk|Storage|diskread): ' "$AH_LOG" | sed 's/^/      /'
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
# IDENTIFY against the image; the Storage: line.
if grep -qxF "ahci: port 0: signature 0x00000101 (ATA disk); IDENTIFY DEVICE: model 'QEMU HARDDISK', $AH_N sectors of 512 bytes (64 MiB), LBA48 yes, DMA yes; used read only" "$AH_LOG" &&
   grep -qxF "Storage: AHCI 00:05.0, port 0 disk QEMU HARDDISK 64 MiB, read only, not mounted (Live mode)" "$AH_LOG" &&
   [ "$(grep -c '^Storage: ' "$AH_LOG")" -eq 1 ]; then
    pass "[1.4] AHCI: IDENTIFY DEVICE on port 0 gives QEMU's model 'QEMU HARDDISK' and the image's $AH_N sectors of 512 bytes (LBA48, DMA); 'Storage: AHCI 00:05.0, port 0 disk QEMU HARDDISK 64 MiB, read only, not mounted (Live mode)'"
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
