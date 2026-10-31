# Madire OS test disks (1.4 stage 2; docs/plans/1.4.md, D13 option A): shell
# helpers that make ext2 test images with mke2fs, check them with e2fsck -fn
# and read them with debugfs. e2fsprogs (and sfdisk, for a report-only
# cross-check) are unmodified outside programs installed on the test machine
# by its own packages; nothing of theirs is copied into the repository or the
# ISO (docs/references.md, "Test tools"). The partition table and the Madire
# test marker come from tests/disk_image.py. tests/qemu-tests.sh sources this
# file after tests/qemu-lib.sh; every name here starts with disk_ or DISK_.
#
# An image is the same byte for byte every time it is made from the same
# files: mke2fs reads only tests/mke2fs.conf (MKE2FS_CONFIG), every option is
# on its command line, the UUID (-U) and the directory hash seed
# (-E hash_seed=) are fixed, and its clock is fixed (E2FSPROGS_FAKE_TIME).

DISK_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
DISK_CONF=$DISK_DIR/mke2fs.conf
DISK_IMAGE_PY=$DISK_DIR/disk_image.py
DISK_LABEL=madire-test
DISK_UUID=4d616469-7265-4f53-8000-6d6b65326673      # fixed: "Madire OS mke2fs"
DISK_HASH_SEED=4d616469-7265-4f53-8000-686173687331  # fixed
DISK_TIME=1791763200       # 2026-10-12 00:00:00 UTC, the ISO's SOURCE_DATE_EPOCH
DISK_E2FS_MIN=1.43         # mke2fs -d and -E root_owner

# disk_tool <name>: the program's path (PATH, then /usr/sbin and /sbin, where
# e2fsprogs and sfdisk live), or nothing.
disk_tool() { (PATH=$PATH:/usr/sbin:/sbin; command -v "$1" 2>/dev/null); }

# disk_tool_version <name>: the first "N.N[.N]" in the program's version
# output (mke2fs -V, e2fsck -V and debugfs -V print to stderr; sfdisk
# --version to stdout), or nothing.
disk_tool_version() {
    local p
    p=$(disk_tool "$1") || return 0
    [ -n "$p" ] || return 0
    case "$1" in
    sfdisk) LC_ALL=C "$p" --version 2>&1 ;;
    *) LC_ALL=C "$p" -V 2>&1 ;;
    esac | grep -oE '[0-9]+\.[0-9]+(\.[0-9]+)?' | head -n 1
}

# disk_version_ge <a> <b>: true if version a >= version b (numeric fields).
disk_version_ge() {
    awk -v a="$1" -v b="$2" 'BEGIN { n = split(a, x, "."); m = split(b, y, ".")
        for (i = 1; i <= (n > m ? n : m); i++) { if (x[i] + 0 > y[i] + 0) exit 0; if (x[i] + 0 < y[i] + 0) exit 1 }
        exit 0 }'
}

# disk_probe: the report-only probe lines (they decide no check). E2FSPROGS:
# each program's path and version, and whether all three are there and at
# least DISK_E2FS_MIN; SFDISK: present (path and version) or absent.
disk_probe() {
    local t p v line="E2FSPROGS" verdict="ok"
    for t in mke2fs e2fsck debugfs; do
        p=$(disk_tool "$t"); v=$(disk_tool_version "$t")
        if [ -z "$p" ]; then line="$line $t=MISSING"; verdict="MISSING"; continue; fi
        line="$line $t=$p ${v:-unknown}"
        if [ "$verdict" = ok ] && { [ -z "$v" ] || ! disk_version_ge "$v" "$DISK_E2FS_MIN"; }; then verdict="TOO OLD"; fi
    done
    echo "$line (minimum $DISK_E2FS_MIN: $verdict)"
    p=$(disk_tool sfdisk)
    if [ -n "$p" ]; then echo "SFDISK present $p $(disk_tool_version sfdisk)"
    else echo "SFDISK absent"; fi
}

# disk_mkfs <image> <size in KiB> <block size> <inode size> <inodes> <source
# folder or ""> [more mke2fs options...]: a new ext2 filesystem image, labelled
# madire-test, revision 1, no reserved blocks, the features of
# tests/mke2fs.conf (more options may change them, e.g. -O ^dir_index or
# -r 0), filled from the source folder (mke2fs -d) if one is given.
disk_mkfs() {
    local img=$1 kib=$2 bs=$3 is=$4 ni=$5 src=$6 mk
    shift 6
    mk=$(disk_tool mke2fs)
    [ -n "$mk" ] || { echo "disk_mkfs: no mke2fs"; return 1; }
    rm -f "$img"
    truncate -s "${kib}K" "$img" || return 1
    LC_ALL=C MKE2FS_CONFIG=$DISK_CONF E2FSPROGS_FAKE_TIME=$DISK_TIME \
        "$mk" -q -F -t ext2 -T madire -b "$bs" -I "$is" -N "$ni" -m 0 -r 1 -L "$DISK_LABEL" \
        -U "$DISK_UUID" -o Linux -e continue \
        -E "hash_seed=$DISK_HASH_SEED,root_owner=0:0,lazy_itable_init=0,nodiscard" \
        ${src:+-d "$src"} "$@" "$img" "${kib}k"
}

# disk_fsck <filesystem image>: e2fsck -fn (forced, read only: it changes
# nothing and answers "no" to every question); its output on stdout, its
# exit status returned (0: nothing to change).
disk_fsck() {
    local ck
    ck=$(disk_tool e2fsck)
    [ -n "$ck" ] || { echo "disk_fsck: no e2fsck"; return 8; }
    LC_ALL=C E2FSCK_CONFIG=/dev/null "$ck" -fn "$1" 2>&1
}

# disk_debugfs <filesystem image> <request>: one debugfs request, read only
# (no -w); its banner on stderr is dropped.
disk_debugfs() {
    local dbg
    dbg=$(disk_tool debugfs)
    [ -n "$dbg" ] || { echo "disk_debugfs: no debugfs"; return 1; }
    LC_ALL=C "$dbg" -R "$2" "$1" 2>/dev/null < /dev/null
}

# disk_listing <filesystem image> <folder>...: the files and folders in the
# given folders as debugfs reads them (ls -p; "." and ".." left out): one line
# "DIR <path>" per folder and "FILE <path> <size> <sha256 of debugfs cat>"
# per regular file, anything else "OTHER <path> <mode>"; LC_ALL=C sorted.
disk_listing() {
    local img=$1 d
    shift
    for d in "$@"; do
        disk_debugfs "$img" "ls -p $d" | awk -F/ -v d="$d" '
            NF >= 7 && $6 != "." && $6 != ".." {
                p = (d == "/" ? "" : d) "/" $6; m = $3
                if (m ~ /^0?4/) print "DIR " p
                else if (m ~ /^10/) print "FILE " p " " $7
                else print "OTHER " p " " m
            }'
    done | while read -r kind path size; do
        if [ "$kind" = FILE ]; then
            echo "FILE $path $size $(disk_debugfs "$img" "cat $path" | sha256sum | cut -d' ' -f1)"
        else echo "$kind $path${size:+ $size}"; fi
    done | LC_ALL=C sort
}

# disk_image <tests/disk_image.py arguments...>: the partition-table and
# marker helper, writing no bytecode.
disk_image() { PYTHONDONTWRITEBYTECODE=1 python3 "$DISK_IMAGE_PY" "$@"; }

# disk_sfdisk_dump <disk image>: sfdisk's reading of the partition table
# (report only), or nothing if sfdisk is absent.
disk_sfdisk_dump() {
    local sf
    sf=$(disk_tool sfdisk)
    [ -n "$sf" ] || return 0
    LC_ALL=C "$sf" --dump "$1" 2>&1
}
