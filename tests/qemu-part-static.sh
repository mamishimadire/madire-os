# Madire OS QEMU tests, part static (0.12, stage 0c): the checks that boot
# nothing: Multiboot header, [0.8] reproducible build (needs make repro),
# [0.10] version read once. Sourced by tests/qemu-tests.sh (a full run, or
# --shard static). Apart from this header, every line was moved unchanged
# from the single script. 0.13 stage 3a: the Multiboot header check uses
# scripts/check-multiboot.py, the default-entry title comes from limine.conf,
# and the [0.13] checks of the Limine ISO are added at the end. 1.0 stage 0a:
# the [1.0] check of Madire's licence files at the ISO root.

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
# with a single "/") is the default one.
TITLE=$(grep -m1 -E '^/[^/]' "$OUT/limine.conf")
if [ "$TITLE" = "/Madire OS v$VERSION" ]; then
    pass "[0.10] boot menu: the default entry is titled 'Madire OS v$VERSION'"
else
    fail "[0.10] default boot entry: '$TITLE', expected '/Madire OS v$VERSION'"
fi

# ---- 0.13: the Limine ISO (checks that boot nothing) ----

echo "== [0.13] scripts/check-multiboot.py: the real kernel, and each refusal"
MBOUT=$(python3 "$MBCHECK" "$KERNEL")
if [ $? -eq 0 ] && grep -qE '^MULTIBOOT OK: header at offset [0-9]+, flags 0x00010003, a\.out kludge: load 0x00100000-0x[0-9a-f]{8}, bss end 0x[0-9a-f]{8}, entry 0x[0-9a-f]{8}$' <<< "$MBOUT"; then
    pass "[0.13] check-multiboot.py accepts the real kernel (flags 0x00010003, a.out kludge, loaded at 1 MiB)"
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

echo "== [0.13] limine.conf: every program module names itself"
# Limine passes only module_string to the kernel, so every program module of
# the default entry needs "module_string: <its file name without .elf>".
MSBAD=$(awk '/^\// { n++ } n == 1 && /^ *module_path: / { if (p != "") print "no string for " p; p = $2; sub(/.*\//, "", p); sub(/\.elf$/, "", p); next }
             n == 1 && /^ *module_string: / { if (p == "" || $2 != p || NF != 2) print "string \"" $2 "\" for \"" p "\""; p = ""; c++ }
             END { if (p != "") print "no string for " p; if (c == 0) print "no modules" }' "$OUT/limine.conf")
if [ -z "$MSBAD" ]; then pass "[0.13] limine.conf: every program module's module_string is its file name ($(grep -c '^ *module_string: ' "$OUT/limine.conf") modules)"
else fail "[0.13] limine.conf: ${MSBAD//$'\n'/; }"; fi

echo "== [0.13] madire.kernel against v0.12's (printed for the 0.13 report; decides no check)"
# Every "0.13" in the kernel image put back to "0.12" (same length): if the
# result's SHA-256 is v0.12's (60012175..., runs 38181484366 and 38190277693),
# the version string is the only difference.
python3 - "$KERNEL" <<'PY'
import hashlib, sys
data = open(sys.argv[1], "rb").read()
new, old = b"0.13", b"0.12"
offsets = []
i = data.find(new)
while i >= 0:
    offsets.append(i)
    i = data.find(new, i + 1)
back = data.replace(new, old)
print("KERNEL_VS_V012: %d bytes, \"0.13\" at %d offset(s): %s" % (len(data), len(offsets), ", ".join("0x%x" % o for o in offsets)))
for o in offsets:
    a = max(0, o - 12)
    print("KERNEL_VS_V012:   0x%x: %r" % (o, data[a:o + 8]))
print("KERNEL_VS_V012: with \"0.12\" put back: sha256 %s" % hashlib.sha256(back).hexdigest())
print("KERNEL_VS_V012: v0.12's madire.kernel:   sha256 60012175f86c02ed5361f58b6098a64a5497849abd63243f29b860e856d69b8f")
print("KERNEL_VS_V012: %s" % ("ONLY THE VERSION STRING DIFFERS" if hashlib.sha256(back).hexdigest()
      == "60012175f86c02ed5361f58b6098a64a5497849abd63243f29b860e856d69b8f" else "OTHER DIFFERENCES"))
PY
