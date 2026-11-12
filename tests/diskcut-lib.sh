# Madire OS: the QEMU power-cut boots (1.4 stage 11; docs/plans/1.4.md,
# section D (b), G4, G5). Sourced by tests/qemu-part-static.sh (the uncut
# run, the cut points N1 and N2, test=autosave) and tests/qemu-part-stress.sh
# (every other cut point, the churn), after tests/qemu-lib.sh and
# tests/disk-lib.sh. Every name here starts with dc_ or DC_.
#
# The owner's case (kernel/fs/disktest.c, test=diskcut) runs on a marked
# 4 MiB disk with 1 KiB blocks (the host sweep's "small" image). The host
# model (tests/ext2write.c's "owner" sequence on a copy of the same
# partition, tests/ext2_cut.py qmodel) gives every write's save and class,
# N1 and N2, and the contexts of section D's allowed list; the uncut QEMU
# run must send the model's writes one by one (qcompare), so the model's
# contexts hold for the kernel's image. A cut boot (test=diskcut=N) halts
# after write N; QEMU is stopped, the partition extracted and judged
# (qjudge: e2fsck -fn's allowed list in its contexts, every saved file
# exact, the owner's case rules); then the remount boot (test=diskcheck) on
# the same image: the D12 recount, read-write, and Madire reads A and B
# exactly as debugfs does (qview).

DC_PY=(python3 -I -B "$DISK_DIR/ext2_cut.py")
DC_READY="System ready. Press any key to open the console."
DC_RECOUNT='^ext2: warning: the volume was not cleanly unmounted \(state 0x0\); the free blocks, free inodes and folders of every group recounted from the bitmaps and inode tables \(D12\): [0-9]+ blocks free, [0-9]+ inodes free; mounted read-write$'
DC_RW="^mount: ext2 'madire-test' \\(port 0, partition 1\\) on /home as filesystem 1, read-write; "

# dc_boot <log> <seconds> <line> <boot options> <disk image>: a -kernel boot
# (-m 128M) with the disk on ich9-ahci, stopped once <line> is in the log.
dc_boot() {
    boot_until "$1" "$2" "$3" -m 128M -kernel "$KERNEL" -append "$4" \
        -drive file="$5",if=none,id=dc-disk,format=raw -device ich9-ahci,id=dc-ahci,addr=0x5 \
        -device ide-hd,drive=dc-disk,bus=dc-ahci.0
}

# dc_health <log>: System ready, no PANIC or FAILED line (case-sensitive).
dc_health() { [ -s "$1" ] && grep -q '^System ready\.' "$1" && ! grep -qE 'PANIC|FAILED' "$1"; }

# dc_setup <work dir> <ext2write> <log tag>: the disk, its partition, the
# model and the uncut boot. Sets DC_W, DC_DISK, DC_N1, DC_N2, DC_TOTAL (the
# model's writes before the unmount), DC_UNCUT (its serial log) and
# DC_SETUP_OK (1: the disk made, the model ok, the uncut boot DONE and
# healthy, its writes equal to the model's).
dc_setup() {
    DC_W=$1
    DC_TOOL=$2
    DC_TAG=$3
    DC_SETUP_OK=1
    rm -rf "${DC_W:?}" && mkdir -p "$DC_W"
    disk_mkfs "$DC_W/fs.img" 4096 1024 256 256 "" > "$DC_W/mkfs.txt" 2>&1 &&
        disk_image mbr "$DC_W/fs.img" "$DC_W/disk.img" > "$DC_W/mbr.txt" 2>&1 &&
        disk_image extract "$DC_W/disk.img" "$DC_W/part.img" > /dev/null 2>&1 || DC_SETUP_OK=0
    DC_DISK=$DC_W/disk.img
    "${DC_PY[@]}" qmodel "$DC_TOOL" "$DC_W/part.img" owner "$DC_W/model" > "$DC_W/model.txt" 2>&1 || DC_SETUP_OK=0
    grep -E '^(QMODEL|FINAL) ' "$DC_W/model.txt" | sed 's/^/      model: /'
    DC_N1=$(sed -nE 's/^QMODEL owner: ok; .* N1=([0-9]+) .*/\1/p' "$DC_W/model.txt")
    DC_N2=$(sed -nE 's/^QMODEL owner: ok; .* N2=([0-9]+) .*/\1/p' "$DC_W/model.txt")
    DC_TOTAL=$(grep -c '^QWRITE ' "$DC_W/model.txt")
    cp "$DC_DISK" "$DC_W/uncut-disk.img" || DC_SETUP_OK=0
    DC_UNCUT=$OUT/serial-diskcut-$DC_TAG-uncut.log
    dc_boot "$DC_UNCUT" 120 "$DC_READY" "test=diskcut" "$DC_W/uncut-disk.img"
    grep -E '^(diskcut|ext2: sync): ' "$DC_UNCUT" | sed 's/^/      uncut: /'
    "${DC_PY[@]}" qcompare "$DC_W/model" "$DC_UNCUT" > "$DC_W/compare.txt" 2>&1 || DC_SETUP_OK=0
    sed 's/^/      uncut: /' "$DC_W/compare.txt"
    grep -qE '^diskcut: DONE \(uncut: [0-9]+ disk writes and [0-9]+ flushes since the mount; ' "$DC_UNCUT" || DC_SETUP_OK=0
    dc_health "$DC_UNCUT" || DC_SETUP_OK=0
    [ -n "$DC_N1" ] && [ -n "$DC_N2" ] && [ "$DC_TOTAL" -gt 0 ] || DC_SETUP_OK=0
}

# dc_point <N>: one cut point on a fresh copy of the disk. Sets DC_CUT_OK
# (the boot halted with exactly N writes logged and none after the cut line;
# qjudge ok; no PANIC or FAILED line) and DC_RE_OK (the remount boot on the
# same image: read-write, after the D12 recount unless nothing was written
# (N = 0: the volume still clean); Madire's reading of A and B equals
# debugfs's; healthy). DC_WHERE: the cut's save and class (report).
dc_point() {
    local n=$1 img=$DC_W/cut-$1-disk.img log=$OUT/serial-diskcut-$DC_TAG-$1.log
    local rlog=$OUT/serial-diskcut-$DC_TAG-$1-remount.log writes after jrc vrc
    local line="blk: test=diskcut=$n: the power cut after write $n: no more disk commands; halted"
    cp "$DC_DISK" "$img"
    dc_boot "$log" 90 "$line" "test=diskcut=$n" "$img"
    writes=$(grep -cE '^blk: write [0-9]+ class [0-9]+: ' "$log")
    after=$(L="$line" awk '$0 == ENVIRON["L"] { f = 1; next } f && /^blk: write / { c++ } END { print c + 0 }' "$log")
    disk_image extract "$img" "$DC_W/cut-$n-part.img" > /dev/null 2>&1
    "${DC_PY[@]}" qjudge "$DC_W/model" owner "$DC_W/cut-$n-part.img" "$n" > "$DC_W/qcut-$n.txt" 2>&1
    jrc=$?
    DC_WHERE=$(sed -nE "s/^QWRITE $((n + 1)) save ([0-9]+) class ([0-9]+) .*/save \\1, class \\2/p" "$DC_W/model.txt")
    sed 's/^/      /' "$DC_W/qcut-$n.txt"
    DC_CUT_OK=0
    if grep -qxF "$line" "$log" && [ "$writes" -eq "$n" ] && [ "$after" -eq 0 ] && [ "$jrc" -eq 0 ] &&
       grep -qE "^QCUT owner N=$n: ok;" "$DC_W/qcut-$n.txt" && ! grep -qE 'PANIC|FAILED' "$log"; then
        DC_CUT_OK=1
    fi
    dc_boot "$rlog" 90 "$DC_READY" "test=diskcheck" "$img"
    disk_image extract "$img" "$DC_W/cut-$n-after.img" > /dev/null 2>&1
    "${DC_PY[@]}" qview "$DC_W/cut-$n-after.img" "$rlog" A B > "$DC_W/qview-$n.txt" 2>&1
    vrc=$?
    grep -E '^(ext2: warning|diskcheck): ' "$rlog" | sed "s/^/      remount N=$n: /"
    sed 's/^/      /' "$DC_W/qview-$n.txt"
    DC_RE_OK=0
    if [ "$vrc" -eq 0 ] && grep -qE "^QVIEW ok;" "$DC_W/qview-$n.txt" && grep -qE "$DC_RW" "$rlog" && dc_health "$rlog" &&
       { { [ "$n" -gt 0 ] && grep -qE "$DC_RECOUNT" "$rlog"; } ||
         { [ "$n" -eq 0 ] && ! grep -q '^ext2: warning: ' "$rlog"; }; }; then
        DC_RE_OK=1
    fi
    echo "      cut N=$n (${DC_WHERE:-?}): $writes writes logged, $after after the cut line; judged: $(head -n 1 "$DC_W/qcut-$n.txt" | cut -c1-160); remount: $DC_RE_OK"
}
