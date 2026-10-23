# Madire OS QEMU tests, part static (0.12, stage 0c): the checks that boot
# nothing: Multiboot header, [0.8] reproducible build (needs make repro),
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
# notices, the name) before the size report.

echo "== Multiboot header"
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
echo "== [0.8] Reproducible build (make repro)"
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

echo "== [0.10] Version read once by the test harness"
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

echo "== [0.13] scripts/check-multiboot.py: the real kernel, and each refusal"
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

echo "== [0.13] The ISO: Limine's files, the notices, no file of the previous boot loader"
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

echo "== [1.0] The ISO: Madire's licence files at its root"
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

echo "== [1.0] docs: every .md file valid UTF-8"
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

echo "== [0.13] limine.conf: every program module names itself"
# Limine passes only module_string to the kernel, so every program module of
# the default entry needs "module_string: <its file name without .elf>".
MSBAD=$(awk '/^\// { n++ } n == 1 && /^ *module_path: / { if (p != "") print "no string for " p; p = $2; sub(/.*\//, "", p); sub(/\.elf$/, "", p); next }
             n == 1 && /^ *module_string: / { if (p == "" || $2 != p || NF != 2) print "string \"" $2 "\" for \"" p "\""; p = ""; c++ }
             END { if (p != "") print "no string for " p; if (c == 0) print "no modules" }' "$OUT/limine.conf")
if [ -z "$MSBAD" ]; then pass "[0.13] limine.conf: every program module's module_string is its file name ($(grep -c '^ *module_string: ' "$OUT/limine.conf") modules)"
else fail "[0.13] limine.conf: ${MSBAD//$'\n'/; }"; fi

# ---- 1.1: the boot menu hidden behind a key, on Madire's wallpaper ----

echo "== [1.1] limine.conf: the global options (docs/plans/1.1.md, section E)"
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

echo "== [1.1] The ISO: the boot menu's wallpaper"
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

echo "== [1.1] make-wallpaper.py: the M of kernel/logo.c"
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

echo "== [1.2] The Multiboot header's video request (stage 5)"
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

echo "== [1.2] limine.conf: every entry boots in text mode (stage 5, decision 3)"
# Until the switch stage, every entry (a line starting "/" with a
# "protocol:" line) holds exactly one "    textmode: yes" and no other
# textmode line; no textmode line outside an entry.
TM=$(awk 'function done() { if (blk == "") return
                            if (proto) { ent++; if (tm != 1 || other) bad = bad blk " (" tm " textmode: yes, " other " other); " }
                            else if (tm || other) bad = bad blk " (a textmode line, no protocol); " }
          /^\// { done(); blk = $0; proto = 0; tm = 0; other = 0; next }
          /^[ \t]*textmode:/ { if (blk == "") bad = bad "a textmode line before the first entry; "
                               else if ($0 == "    textmode: yes") tm++; else other++; next }
          /^[ \t]*protocol:/ { proto = 1 }
          END { done(); printf "%d|%s\n", ent, bad }' "$OUT/limine.conf")
TMN=${TM%%|*}
TMBAD=${TM#*|}
echo "      $TMN entries"
if [ -z "$TMBAD" ] && [ "$TMN" -gt 0 ]; then
    pass "[1.2] limine.conf: every entry boots in text mode (one 'textmode: yes' in each of its $TMN entries)"
else
    fail "[1.2] limine.conf: text mode: ${TMBAD:-no entries}"
fi

# ---- 1.2 stage 6: the console font (decision 8) ----

echo "== [1.2] third_party/terminus: the pinned font files (stage 6)"
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

echo "== [1.2] scripts/make-font.py: the font C files regenerate byte-identically (stage 6)"
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

echo "== [1.2] The ISO: the font's notices; the font never named in the interface (stage 6)"
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

# ---- 1.2: the size report (decides no check) ----

echo "== Size report (1.2 stage 2; report only)"
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
