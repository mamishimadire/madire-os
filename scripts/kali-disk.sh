#!/usr/bin/env bash
# 1.4 stage 13: the Kali side of the VirtualBox storage steps
# (docs/plans/1.4.md, C13, S13/S14; docs/testing.md, "In 1.4 (stage 13)").
# Run by scripts/kali-disk.ps1 (an owner-run step, AGENTS.md section 6);
# every result line starts with KDISK_.
#
#   kali-disk.sh image <work dir>
#       The marked test image for MadireOS-Test's disk: the seed tree
#       (scripts/vbox-storage.py seed), an ext2 filesystem of 64 MiB made by
#       tests/disk-lib.sh's disk_mkfs (the pinned tests/mke2fs.conf; 1 KiB
#       blocks, 256-byte inodes, 4096 inodes, label madire-test), put by
#       tests/disk_image.py mbr into an MBR disk image (one Linux partition
#       at LBA 2048, the Madire test marker in its first sector):
#       <work dir>/madire-test-disk.img. Checked: e2fsck -fn reports
#       nothing, the marker and table read back, debugfs's tree equals the
#       seed tree.
#   kali-disk.sh check <work dir> <seed|session> [image]
#       A VirtualBox disk (<work dir>/disk.vdi): qemu-img reads it (info),
#       with an image given it must hold exactly that image and zeros after
#       it (qemu-img compare); converted to raw (sparse), its first 65 MiB
#       (the image's area) kept and the rest proven zero (qemu-img compare),
#       the partition table and marker read back, the partition extracted,
#       e2fsck -fn reports nothing, the volume is clean, and debugfs's tree
#       equals the expected tree (scripts/vbox-storage.py seed for the
#       pristine "marked" copy, expected for a disk the storage group used).
#
# <work dir> holds this script, vbox-storage.py, disk-lib.sh, disk_image.py,
# mke2fs.conf, ext2_tree.py and ext2_view.py (and disk.vdi, the image for
# check). Nothing is written outside <work dir>.
set -euo pipefail
export PYTHONDONTWRITEBYTECODE=1
STEP=$1
W=$2
cd "$W"
# shellcheck source=../tests/disk-lib.sh
. ./disk-lib.sh
FRAME='^(e2fsck [0-9]+\.[0-9]+.*|Pass [1-5]: .*|madire-test: [0-9]+/[0-9]+ files \(.*\), [0-9]+/[0-9]+ blocks|)$'
KIB=65536          # the filesystem: 64 MiB (131072 sectors), at LBA 2048 of the 30 GB disk
BS=1024
IS=256
NI=4096

for t in mke2fs e2fsck debugfs python3; do
    [ -n "$(disk_tool "$t")" ] || { echo "KDISK_TOOL_MISSING $t"; exit 2; }
done
echo "KDISK_E2FSPROGS mke2fs $(disk_tool_version mke2fs), e2fsck $(disk_tool_version e2fsck), debugfs $(disk_tool_version debugfs) ($(dpkg-query -W -f='${Package} ${Version}' e2fsprogs 2>/dev/null || echo 'no dpkg record'))"
echo "KDISK_PROBE $(disk_probe | tr '\n' ' ')"
echo "KDISK_PYTHON $(python3 --version 2>&1)"

# kdisk_fsck <fs image> <tag>: e2fsck -fn; true when it exits 0 and prints
# only its frame lines (nothing to report). Its output is printed.
kdisk_fsck() {
    local out rc=0 extra
    out=$(disk_fsck "$1") || rc=$?
    sed "s/^/KDISK_E2FSCK_LINE ($2): /" <<< "$out"
    extra=$(grep -vE "$FRAME" <<< "$out" || true)
    echo "KDISK_E2FSCK ($2): exit $rc; $(grep -c . <<< "$extra" || true) lines other than the frame"
    [ "$rc" -eq 0 ] && [ -z "$extra" ]
}

# kdisk_tree <fs image> <seed|expected>: debugfs's tree against the
# expected tree (tests/ext2_tree.py compare).
kdisk_tree() {
    rm -rf want
    python3 -I vbox-storage.py "$2" want > want-listing.txt
    sed "s/^/KDISK_WANT ($2): /" want-listing.txt
    python3 -I -B ext2_tree.py compare "$1" want > tree.txt 2>&1 || true
    { grep -vE '^  want ' tree.txt || true; } | sed 's/^/KDISK_TREE_LINE: /'
    grep -qE '^TREE ok [0-9]+ paths$' tree.txt
}

case $STEP in
image)
    rm -rf seed fs.img madire-test-disk.img
    python3 -I vbox-storage.py seed seed | sed 's/^/KDISK_SEED: /'
    # mke2fs -d copies the source's times: fixed first (tests/disk-lib.sh's clock).
    find seed -exec touch -h -a -m -d "@$DISK_TIME" {} +
    disk_mkfs fs.img "$KIB" "$BS" "$IS" "$NI" seed > mkfs.txt 2>&1 || { sed 's/^/KDISK_MKFS: /' mkfs.txt; echo "KDISK_MKFS_FAILED"; exit 3; }
    sed 's/^/KDISK_MKFS: /' mkfs.txt
    kdisk_fsck fs.img image || { echo "KDISK_E2FSCK_NOT_CLEAN"; exit 4; }
    { disk_debugfs fs.img stats | grep -E '^(Filesystem volume name|Filesystem revision|Filesystem features|Filesystem state|Block count|Block size|Inode count|Inode size|Blocks per group|Filesystem created)' || true; } |
        sed 's/^/KDISK_STATS: /'
    kdisk_tree fs.img seed || { echo "KDISK_TREE_DIFFERS"; exit 5; }
    { python3 -I -B ext2_view.py blocks fs.img | grep -E ' /seed/' || true; } | sed 's/^/KDISK_BLOCKMAP: /'
    disk_image mbr fs.img madire-test-disk.img | sed 's/^/KDISK_MBR: /'
    v=$(disk_image verify madire-test-disk.img) || { echo "KDISK_VERIFY_FAILED $v"; exit 6; }
    sed 's/^/KDISK_VERIFY: /' <<< "$v"
    n=$(( $(stat -c %s madire-test-disk.img) / 512 ))
    [ "$n" -eq $(( 2048 + KIB * 2 )) ] || { echo "KDISK_IMAGE_SIZE $n sectors (expected $(( 2048 + KIB * 2 )))"; exit 7; }
    echo "KDISK_IMAGE_SECTORS=$n (partition 1: LBA 2048, $(( KIB * 2 )) sectors)"
    echo "KDISK_FS_SHA256=$(sha256sum fs.img | cut -d' ' -f1)"
    echo "KDISK_IMAGE_SHA256=$(sha256sum madire-test-disk.img | cut -d' ' -f1)"
    echo "KDISK_DONE"
    ;;
check)
    EXPECT=$3
    IMAGE=${4:-}
    case $EXPECT in seed|session) ;; *) echo "KDISK_BAD_EXPECT $EXPECT"; exit 2 ;; esac
    [ -n "$(command -v qemu-img)" ] || { echo "KDISK_TOOL_MISSING qemu-img"; exit 2; }
    echo "KDISK_QEMU_IMG $(qemu-img --version | head -n 1)"
    echo "KDISK_VDI_SHA256=$(sha256sum disk.vdi | cut -d' ' -f1)"
    qemu-img info -f vdi disk.vdi | sed 's/^/KDISK_INFO: /'
    if [ -n "$IMAGE" ]; then
        echo "KDISK_IMAGE_SHA256=$(sha256sum "$IMAGE" | cut -d' ' -f1)"
        # The smaller image's bytes equal, and the larger's rest unallocated or zero.
        if qemu-img compare -f raw -F vdi "$IMAGE" disk.vdi > compare.txt 2>&1; then
            echo "KDISK_COMPARE=identical (the disk holds the image exactly, zeros after it): $(tr '\n' ' ' < compare.txt)"
        else
            echo "KDISK_COMPARE_DIFFERS $(tr '\n' ' ' < compare.txt)"; exit 8
        fi
    fi
    rm -f disk.raw head.raw part.img
    # The whole disk as raw (sparse), then its first 65 MiB: the image's
    # area (tests/disk_image.py reads a whole file into memory, so it is
    # given that part only); the rest of the 30 GB must read as zeros
    # (nothing written outside the image's area).
    qemu-img convert -f vdi -O raw disk.vdi disk.raw
    echo "KDISK_RAW $(stat -c %s disk.raw) bytes ($(du -k disk.raw | cut -f1) KiB allocated)"
    dd if=disk.raw of=head.raw bs=1M count=$(( (2048 + KIB * 2) / 2048 )) status=none
    rm -f disk.raw
    if qemu-img compare -f raw -F vdi head.raw disk.vdi > compare-head.txt 2>&1; then
        echo "KDISK_REST_ZERO=yes (after the first $(( (2048 + KIB * 2) / 2048 )) MiB the disk reads as zeros): $(tr '\n' ' ' < compare-head.txt)"
    else
        echo "KDISK_REST_ZERO_NO $(tr '\n' ' ' < compare-head.txt)"; exit 8
    fi
    v=$(disk_image verify head.raw) || { echo "KDISK_VERIFY_FAILED $v"; exit 6; }
    sed 's/^/KDISK_VERIFY: /' <<< "$v"
    disk_image extract head.raw part.img | sed 's/^/KDISK_EXTRACT: /' || true
    rm -f head.raw
    ok=1
    kdisk_fsck part.img "$EXPECT" || ok=0
    { disk_debugfs part.img stats | grep -E '^(Filesystem volume name|Filesystem state|Errors behavior|Mount count|Last mount time|Last write time|Free blocks|Free inodes|Block count|Inode count)' || true; } |
        sed 's/^/KDISK_STATS: /'
    state=$(disk_debugfs part.img stats | sed -nE 's/^Filesystem state:[[:space:]]*(.*)$/\1/p')
    echo "KDISK_STATE=${state:-unknown}"
    [ "$state" = clean ] || ok=0
    want=$([ "$EXPECT" = seed ] && echo seed || echo expected)
    kdisk_tree part.img "$want" || { echo "KDISK_TREE_DIFFERS"; ok=0; }
    { python3 -I -B ext2_view.py blocks part.img | grep -E ' /(seed|user)/' || true; } | sed 's/^/KDISK_BLOCKMAP: /'
    rm -f part.img
    [ "$ok" -eq 1 ] || { echo "KDISK_CHECK_FAILED"; exit 9; }
    echo "KDISK_DONE"
    ;;
*)
    echo "KDISK_BAD_STEP $STEP"; exit 2 ;;
esac
