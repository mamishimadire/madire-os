#!/usr/bin/env bash
# 1.3 stage 9: the module ISO for the VirtualBox acceptance's module group
# (docs/testing.md, Test I), packaged on Kali around the kernel of the ISO
# under test, as part 1 makes its own: the test module, its limine.conf (the
# module entry with "textmode: yes") and limine_iso are taken unchanged from
# tests/qemu-part-1.sh and tests/qemu-lib.sh. Nothing is built: the kernel,
# limine-bios.sys and the wallpaper are the ISO's own files, the kernel
# checked against its expected SHA-256; limine-bios-cd.bin is the Limine
# package's (the ISO's copy carries a boot info table), checked equal to the
# ISO's outside that table. Run by scripts/kali-module-iso.ps1 (an owner-run
# step, AGENTS.md section 6); every result line starts with MODISO_.
#
# Usage: kali-module-iso.sh <work dir> <ISO SHA-256> <kernel SHA-256>
# <work dir> holds madire-os.iso (the ISO under test), qemu-part-1.sh and
# qemu-lib.sh; the module ISO is written to <work dir>/madire-os-module.iso.
set -euo pipefail
export PYTHONDONTWRITEBYTECODE=1
W=$1
ISO_SHA=$2
KERNEL_SHA=$3
LIMINE_DIR=/usr/local/share/limine
LIMINE=limine
cd "$W"
rm -rf out madire-os-module.iso
mkdir out
OUT=$W/out
echo "MODISO_TOOLS limine $($LIMINE --version 2>&1 | head -n 1) | package $(cat "$LIMINE_DIR/VERSION" 2>/dev/null || echo unknown) | $(xorriso -version 2>&1 | head -n 1) | $(python3 --version)"

got=$(sha256sum madire-os.iso | cut -d' ' -f1)
[ "$got" = "$ISO_SHA" ] || { echo "MODISO_ISO_SHA256_MISMATCH $got (expected $ISO_SHA)"; exit 2; }
echo "MODISO_ISO_SHA256=$got"

# The ISO's own files.
xorriso -osirrox on -indev madire-os.iso -extract / "$OUT/iso" >/dev/null 2>&1
chmod -R u+w "$OUT/iso"
KERNEL=$OUT/iso/boot/madire.kernel
got=$(sha256sum "$KERNEL" | cut -d' ' -f1)
[ "$got" = "$KERNEL_SHA" ] || { echo "MODISO_KERNEL_SHA256_MISMATCH $got (expected $KERNEL_SHA)"; exit 3; }
echo "MODISO_KERNEL_SHA256=$got (the ISO's /boot/madire.kernel)"

# Limine: the package on Kali must be the ISO's release.
cmp -s "$LIMINE_DIR/limine-bios.sys" "$OUT/iso/boot/limine/limine-bios.sys" ||
    { echo "MODISO_LIMINE_BIOS_SYS_DIFFERS (Kali's package is not the ISO's Limine)"; exit 4; }
n=$( { cmp -l "$LIMINE_DIR/limine-bios-cd.bin" "$OUT/iso/boot/limine/limine-bios-cd.bin" || true; } |
     awk '$1 < 9 || $1 > 64' | wc -l)
[ "$n" = 0 ] || { echo "MODISO_LIMINE_CD_BIN_DIFFERS $n bytes outside the boot info table"; exit 4; }
echo "MODISO_LIMINE=Kali's package equals the ISO's (limine-bios.sys identical; limine-bios-cd.bin identical outside its boot info table)"

# Part 1's pieces, unchanged: limine_tree and limine_iso (qemu-lib.sh),
# make_test_module and the module entry's limine.conf (qemu-part-1.sh).
# limine_tree reads $OUT/isodir, the build's ISO tree: here made of the
# ISO's files and the package's limine-bios-cd.bin (as the build has it).
eval "$(sed -n '/^limine_iso() {/,/^}/p' qemu-lib.sh)"
eval "$(sed -n '/^limine_tree() {/,/^}/p' qemu-lib.sh)"
eval "$(sed -n '/^make_test_module() {/,/^}/p' qemu-part-1.sh)"
declare -F limine_iso limine_tree make_test_module >/dev/null || { echo "MODISO_PART1_FUNCTIONS_MISSING"; exit 5; }
mkdir -p "$OUT/isodir/boot/limine"
cp "$OUT/iso/boot/limine/limine-bios.sys" "$OUT/iso/boot/limine/wallpaper.png" "$OUT/isodir/boot/limine/"
cp "$LIMINE_DIR/limine-bios-cd.bin" "$OUT/isodir/boot/limine/"

limine_tree "$OUT/isomod"
make_test_module "$OUT/test-module.bin" 14000
cp "$OUT/test-module.bin" "$OUT/isomod/boot/test-module.bin"
awk '/^cat > "\$OUT\/isomod\/boot\/limine\/limine\.conf" <</ { on = 1; next }
     on && /^CFG$/ { exit }
     on { print }' qemu-part-1.sh > "$OUT/isomod/boot/limine/limine.conf"
CONF=$OUT/isomod/boot/limine/limine.conf
for want in '^timeout: 0$' '^    protocol: multiboot1$' '^    textmode: yes$' '^    path: boot\(\):/boot/madire\.kernel$' \
            '^    module_path: boot\(\):/boot/test-module\.bin$' '^    module_string: test-module$'; do
    grep -Eq "$want" "$CONF" || { echo "MODISO_CONF_LINE_MISSING $want"; exit 6; }
done
echo "---- MODISO limine.conf (from qemu-part-1.sh)"
cat "$CONF"
echo "----"
limine_iso "$OUT/isomod" "$W/madire-os-module.iso" || { echo "MODISO_LIMINE_ISO_FAILED"; exit 7; }

# The module ISO read back: every file of the tree in it, each identical
# (limine-bios-cd.bin outside its boot info table), the kernel the expected one.
xorriso -osirrox on -indev "$W/madire-os-module.iso" -extract / "$OUT/back" >/dev/null 2>&1
chmod -R u+w "$OUT/back"
(cd "$OUT/isomod" && find . -type f | LC_ALL=C sort) > "$OUT/tree.list"
(cd "$OUT/back" && find . -type f | LC_ALL=C sort) > "$OUT/back.list"
missing=$(LC_ALL=C comm -23 "$OUT/tree.list" "$OUT/back.list" | tr '\n' ' ')
[ -z "$missing" ] || { echo "MODISO_FILES_MISSING $missing"; exit 8; }
echo "MODISO_EXTRA_FILES=$(LC_ALL=C comm -13 "$OUT/tree.list" "$OUT/back.list" | tr '\n' ' ') (report only)"
bad=0
while read -r f; do
    case "$f" in
    ./boot/limine/limine-bios-cd.bin)
        n=$( { cmp -l "$OUT/isomod/$f" "$OUT/back/$f" || true; } | awk '$1 < 9 || $1 > 64' | wc -l)
        [ "$n" = 0 ] || { echo "MODISO_DIFFERS $f ($n bytes outside the boot info table)"; bad=1; } ;;
    *) cmp -s "$OUT/isomod/$f" "$OUT/back/$f" || { echo "MODISO_DIFFERS $f"; bad=1; } ;;
    esac
done < "$OUT/tree.list"
[ "$bad" = 0 ] || exit 9
echo "MODISO_FILES=$(grep -c '' "$OUT/tree.list") files of the tree, each identical in the module ISO"
got=$(sha256sum "$OUT/back/boot/madire.kernel" | cut -d' ' -f1)
[ "$got" = "$KERNEL_SHA" ] || { echo "MODISO_MODULE_ISO_KERNEL_MISMATCH $got"; exit 10; }
echo "MODISO_MODULE_ISO_KERNEL_SHA256=$got (byte-identical to the ISO under test's)"
size=$(stat -c %s "$OUT/back/boot/test-module.bin")
[ "$size" = 14016 ] || { echo "MODISO_TEST_MODULE_SIZE $size (expected 14016)"; exit 11; }
echo "MODISO_TEST_MODULE=14016 bytes"
echo "MODISO_MODULE_ISO_SHA256=$(sha256sum "$W/madire-os-module.iso" | cut -d' ' -f1)"
echo "MODISO_DONE"
