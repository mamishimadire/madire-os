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
#   kali-disk.sh check <work dir> seed [image]
#   kali-disk.sh check <work dir> session <serial log> <UTC offset s> [image]
#       A VirtualBox disk (<work dir>/disk.vdi): qemu-img reads it (info),
#       with seed and an image given it must hold exactly that image and
#       zeros after it (qemu-img compare); converted to raw (sparse), its
#       first 65 MiB (the image's area) kept and the rest proven zero
#       (qemu-img compare), the partition table and marker read back, with
#       session and an image given its sectors 0-2047 must equal the image's
#       (cmp), the partition extracted, e2fsck -fn reports nothing, the
#       volume is clean, and debugfs's tree equals the expected tree
#       (scripts/vbox-storage.py seed for the pristine "marked" copy,
#       expected for a disk the storage group used).
#       session only (docs/testing.md, step 4): VirtualBox's clock holds the
#       host's local time, which Madire stores as UTC, so the superblock's
#       last mount and write times can be ahead of Kali's UTC clock. e2fsck
#       may then add its two "in the future ... by less than a day" notices
#       and nothing else (exit 0); both times must lie inside the storage
#       group's session (the boots' clock lines and saves before Restart in
#       its serial log, <serial log>) and be ahead of Kali's UTC clock by at
#       most the host's UTC offset (<UTC offset s>, from Windows) and a
#       margin; e2fsck.conf(5)'s paragraphs on accept_time_fudge and
#       broken_system_clock are logged from Kali's manual page as evidence,
#       report only (KDISK_MAN_MISSING if not found; e2fsck still runs with
#       E2FSCK_CONFIG=/dev/null).
#
# <work dir> holds this script, vbox-storage.py, disk-lib.sh, disk_image.py,
# mke2fs.conf, ext2_tree.py and ext2_view.py (and disk.vdi, the serial log
# and the image for check). Nothing is written outside <work dir>.
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
# session only: e2fsck's two notices for superblock times ahead of its clock
# by less than a day (verbatim, each with its tab-indented second line).
NOTE_TAIL=$'\t(by less than a day, probably due to the hardware clock being incorrectly set)'
NOTE_MOUNT="Superblock last mount time is in the future."$'\n'"$NOTE_TAIL"
NOTE_WRITE="Superblock last write time is in the future."$'\n'"$NOTE_TAIL"
FSCK_NOTICES=""    # set by kdisk_fsck: the notices it accepted (mount, write)
TIME_MARGIN=30     # s, each side of the session's window (the uptime lines come from the idle loop)
AHEAD_MARGIN=300   # s, on top of the host's UTC offset (Kali's clock against the host's)

for t in mke2fs e2fsck debugfs python3; do
    [ -n "$(disk_tool "$t")" ] || { echo "KDISK_TOOL_MISSING $t"; exit 2; }
done
echo "KDISK_E2FSPROGS mke2fs $(disk_tool_version mke2fs), e2fsck $(disk_tool_version e2fsck), debugfs $(disk_tool_version debugfs) ($(dpkg-query -W -f='${Package} ${Version}' e2fsprogs 2>/dev/null || echo 'no dpkg record'))"
echo "KDISK_PROBE $(disk_probe | tr '\n' ' ')"
echo "KDISK_PYTHON $(python3 --version 2>&1)"

# kdisk_fsck <fs image> <tag> [notices]: e2fsck -fn; true when it exits 0
# and prints only its frame lines (nothing to report), or with "notices"
# (check session only) its frame lines and exactly NOTE_MOUNT, NOTE_WRITE or
# both in that order (each once; kdisk_times must then explain them). Its
# output is printed.
kdisk_fsck() {
    local out rc=0 extra
    out=$(disk_fsck "$1") || rc=$?
    sed "s/^/KDISK_E2FSCK_LINE ($2): /" <<< "$out"
    extra=$(grep -vE "$FRAME" <<< "$out" || true)
    echo "KDISK_E2FSCK ($2): exit $rc; $(grep -c . <<< "$extra" || true) lines other than the frame"
    [ "$rc" -eq 0 ] || return 1
    [ -n "$extra" ] || return 0
    [ "${3:-}" = notices ] || return 1
    case $extra in
    "$NOTE_MOUNT") FSCK_NOTICES=mount ;;
    "$NOTE_WRITE") FSCK_NOTICES=write ;;
    "$NOTE_MOUNT"$'\n'"$NOTE_WRITE") FSCK_NOTICES="mount write" ;;
    *) echo "KDISK_E2FSCK_NOTICE ($2): NOT accepted: lines other than the two \"in the future (by less than a day ...)\" notices"
       return 1 ;;
    esac
    echo "KDISK_E2FSCK_NOTICE ($2): accepted: the \"in the future (by less than a day ...)\" notice for the superblock's last: $FSCK_NOTICES (explained below by KDISK_TIME)"
}

# kdisk_times <fs image> <serial log> <UTC offset s> <Kali UTC before e2fsck>:
# the superblock's last mount and write times (s_mtime, offset 44, and
# s_wtime, offset 48: the raw 32-bit values, equal to debugfs's stats read
# with TZ=UTC0) must each lie inside the storage group's session (from the
# first boot's clock base to the last boot's base plus its last "uptime: N s"
# line before its save before Restart plus 5 s, widened by TIME_MARGIN), be
# ahead of Kali's UTC clock by at most the host's UTC offset plus
# AHEAD_MARGIN, and, if e2fsck gave its notice for that time, be ahead of it.
# The serial log must have a save before Restart after every boot's clock line.
kdisk_times() {
    local img=$1 log=$2 off=$3 now=$4 good=1 line base first="" lastbase="" up=0 hi=""
    local boots=0 saves=0 saved=0 lo dbg name pos raw txt t ahead inwin note verdict
    local rtc_re='^Filesystem: clock: the RTC read .* at boot \(epoch ([0-9]+), uptime ([0-9]+) s\)'
    local up_re='^uptime: ([0-9]+) s$'
    while IFS= read -r line || [ -n "$line" ]; do
        line=${line%$'\r'}
        if [[ $line =~ $rtc_re ]]; then
            base=$(( BASH_REMATCH[1] - BASH_REMATCH[2] ))
            [ -n "$first" ] || first=$base
            lastbase=$base; up=0; saved=0; boots=$(( boots + 1 ))
        elif [[ $line =~ $up_re ]]; then
            up=${BASH_REMATCH[1]}
        elif [[ $line == "ext2: save before restart: done "* ]] && [ -n "$lastbase" ]; then
            saves=$(( saves + 1 )); saved=1; hi=$(( lastbase + up + 5 ))
        fi
    done < "$log"
    if [ "$boots" -eq 0 ] || [ "$saved" -ne 1 ] || [ "$saves" -ne "$boots" ]; then
        echo "KDISK_TIME window: NOT FOUND in the serial log ($boots boots' clock lines, $saves saves before Restart; the last boot saved: $([ "$saved" -eq 1 ] && echo yes || echo no))"
        return 1
    fi
    lo=$(( first - TIME_MARGIN )); hi=$(( hi + TIME_MARGIN ))
    echo "KDISK_TIME window: $boots boots, each ended by its save before Restart; the first boot's clock base $first ($(date -u -d "@$first" '+%F %T') as UTC), the last boot's $lastbase ($(date -u -d "@$lastbase" '+%F %T')) plus its last uptime line before the save ($up s) plus 5 s; with $TIME_MARGIN s each side: $lo .. $hi"
    echo "KDISK_TIME Kali UTC before e2fsck: $now ($(date -u -d "@$now" '+%F %T')); host UTC offset $off s ($(printf '%s%02d:%02d' "$([ "$off" -lt 0 ] && echo - || echo +)" $(( ${off#-} / 3600 )) $(( ${off#-} % 3600 / 60 )))); a time may be ahead of Kali's clock by at most $(( off + AHEAD_MARGIN )) s (the offset plus $AHEAD_MARGIN s)"
    dbg=$(export TZ=UTC0; disk_debugfs "$img" stats)
    for name in mount write; do
        pos=$([ "$name" = mount ] && echo 44 || echo 48)
        raw=$(od -An -t u4 --endian=little -j $(( 1024 + pos )) -N 4 "$img" | tr -d ' ')
        txt=$(sed -nE "s/^Last $name time:[[:space:]]*(.*)\$/\\1/p" <<< "$dbg")
        t=$(date -u -d "$txt" +%s 2>/dev/null || echo none)
        verdict=ok
        [ -n "$raw" ] && [ "$t" = "$raw" ] || verdict=BAD
        if [ "$verdict" = ok ]; then
            ahead=$(( raw - now ))
            inwin=$([ "$raw" -ge "$lo" ] && [ "$raw" -le "$hi" ] && echo yes || echo no)
            note=$([[ " $FSCK_NOTICES " == *" $name "* ]] && echo yes || echo no)
            [ "$inwin" = yes ] && [ "$ahead" -le $(( off + AHEAD_MARGIN )) ] || verdict=BAD
            [ "$note" = no ] || [ "$ahead" -gt 0 ] || verdict=BAD
            echo "KDISK_TIME s_${name:0:1}time (last $name): $raw ($(date -u -d "@$raw" '+%F %T') as UTC; debugfs: $txt); in the window: $inwin; ahead of Kali's clock by $ahead s; e2fsck notice: $note: $verdict"
        else
            echo "KDISK_TIME s_${name:0:1}time (last $name): raw ${raw:-unread}, debugfs '$txt' (as UTC: $t): they differ: BAD"
        fi
        [ "$verdict" = ok ] || good=0
    done
    echo "KDISK_TIME verdict: $([ "$good" -eq 1 ] && echo ok || echo BAD)"
    [ "$good" -eq 1 ]
}

# kdisk_man: e2fsck.conf(5)'s paragraphs on accept_time_fudge and
# broken_system_clock as Kali's installed manual page gives them, logged as
# evidence for the accepted notices (report only: the caller ignores its
# status). An option's name is a line of its own (any indent, words joined
# by "_"); its paragraph runs to the next such line or an unindented line.
# If either is not found: KDISK_MAN_MISSING with man's exit status, man -w,
# whether the page file exists, and the first 5 lines man printed.
kdisk_man() {
    local raw txt rc=0 where wrc=0 page=/usr/share/man/man5/e2fsck.conf.5.gz
    raw=$(LC_ALL=C MANWIDTH=80 man -P cat e2fsck.conf 2>&1) || rc=$?
    raw=$(sed 's/.\x08//g' <<< "$raw")
    txt=$(awk '/^[^ ]/ { p = 0 } /^ +[a-z]+(_[a-z0-9]+)+ *$/ { p = ($1 == "accept_time_fudge" || $1 == "broken_system_clock") } p' <<< "$raw" || true)
    if grep -qE '^ +accept_time_fudge *$' <<< "$txt" && grep -qE '^ +broken_system_clock *$' <<< "$txt"; then
        sed 's/^/KDISK_MAN e2fsck.conf(5): /' <<< "$txt"
        return 0
    fi
    where=$(LC_ALL=C man -w e2fsck.conf 2>&1) || wrc=$?
    echo "KDISK_MAN_MISSING e2fsck.conf(5): the accept_time_fudge and broken_system_clock paragraphs were not found (report only)"
    echo "KDISK_MAN_MISSING man: $(command -v man || echo 'not installed'); man -P cat e2fsck.conf exit $rc; man -w e2fsck.conf exit $wrc: $(tr '\n' ' ' <<< "$where")"
    echo "KDISK_MAN_MISSING $page: $([ -e "$page" ] && echo "present ($(stat -c %s "$page") bytes)" || echo absent)"
    head -n 5 <<< "$raw" | sed 's/^/KDISK_MAN_MISSING man printed: /'
    return 1
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
    SERIAL=""
    OFFSET=""
    case $EXPECT in
    seed) IMAGE=${4:-} ;;
    session)
        SERIAL=${4:-}
        OFFSET=${5:-}
        IMAGE=${6:-}
        [ -n "$SERIAL" ] && [ -f "$SERIAL" ] || { echo "KDISK_BAD_ARGS session needs <serial log> <UTC offset s> [image]: no serial log '$SERIAL'"; exit 2; }
        [[ $OFFSET =~ ^-?[0-9]+$ ]] || { echo "KDISK_BAD_ARGS session needs <serial log> <UTC offset s> [image]: UTC offset '$OFFSET'"; exit 2; }
        ;;
    *) echo "KDISK_BAD_EXPECT $EXPECT"; exit 2 ;;
    esac
    [ -n "$(command -v qemu-img)" ] || { echo "KDISK_TOOL_MISSING qemu-img"; exit 2; }
    echo "KDISK_QEMU_IMG $(qemu-img --version | head -n 1)"
    echo "KDISK_VDI_SHA256=$(sha256sum disk.vdi | cut -d' ' -f1)"
    [ -z "$SERIAL" ] || echo "KDISK_SERIAL_SHA256=$(sha256sum "$SERIAL" | cut -d' ' -f1)"
    qemu-img info -f vdi disk.vdi | sed 's/^/KDISK_INFO: /'
    if [ -n "$IMAGE" ]; then
        echo "KDISK_IMAGE_SHA256=$(sha256sum "$IMAGE" | cut -d' ' -f1)"
    fi
    if [ "$EXPECT" = seed ] && [ -n "$IMAGE" ]; then
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
    ok=1
    if [ "$EXPECT" = session ] && [ -n "$IMAGE" ]; then
        # Sectors 0-2047 (the MBR, the marker's sector and the gap before
        # the partition): Madire writes none of them.
        if LC_ALL=C cmp -n $(( 2048 * 512 )) head.raw "$IMAGE" > cmp-head.txt 2>&1; then
            echo "KDISK_HEAD identical (sectors 0-2047, $(( 2048 * 512 )) bytes, equal to the image's)"
        else
            at=$(sed -nE 's/.* differ: (byte|char) ([0-9]+),.*/\2/p' cmp-head.txt)
            echo "KDISK_HEAD differs (sectors 0-2047; the first differing offset: $([ -n "$at" ] && echo $(( at - 1 )) || echo unknown)): $(tr '\n' ' ' < cmp-head.txt)"
            ok=0
        fi
    fi
    rm -f head.raw
    if [ "$EXPECT" = session ]; then
        kdisk_man || true                           # report only
        now=$(date -u +%s)
        kdisk_fsck part.img "$EXPECT" notices || ok=0
        kdisk_times part.img "$SERIAL" "$OFFSET" "$now" || ok=0
    else
        kdisk_fsck part.img "$EXPECT" || ok=0
    fi
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
