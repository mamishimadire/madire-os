#ifndef MADIRE_FS_DISKTEST_H
#define MADIRE_FS_DISKTEST_H

/*
 * The disk write tests (1.4 stage 10; docs/plans/1.4.md, C4, C5, D2, D5,
 * D8, risks 24, 26, 43), test= options on -kernel boots only (D21):
 *
 * test=diskwrite (disktest_write)
 *   A disk the write guard REFUSED: three WRITE DMA EXT (LBA 0, 2050 and
 *   4096) and three FLUSH CACHE EXT are tried through the driver's own
 *   calls; each must be refused by the command builder, nothing sent (the
 *   driver's counts: 0 writes, 0 flushes, 6 refused); the test compares the
 *   image's SHA-256 before and after. A read-write volume: a thread writes
 *   1 MiB to /home/diskwrite.bin through the VFS, saves (sync), reads it
 *   back (CRC32), rewrites 64 KiB in the middle and truncates it to 512
 *   KiB, saves and reads back again, removes it and saves; then the
 *   figures (report only): the write speed into memory and onto the disk,
 *   FLUSH's cost, the longest VFS lock hold of a write call and of a save
 *   piece, and the longest turn. "diskwrite: DONE" or "diskwrite: FAILED";
 *   after DONE, the node test below in the same boot.
 *
 * test=disknodes (disktest_nodes; also run by test=diskwrite)
 *   On a read-write volume: 1100 files in /home/nodes (more than the
 *   1024-node table), all listed (nodes evicted on the way, the table at
 *   most 1024); then every listed node held until the table is full, and
 *   getdents64's records call (fd_dir_records) must answer ENOMEM; the
 *   holds given back, the folder lists all 1100 again; then everything
 *   removed and saved. "disknodes: DONE" or "disknodes: FAILED".
 *
 * Stage 11 (section D (b), G5):
 *
 * test=diskcut[=N] (disktest_cut)
 *   The owner's case through the VFS on a read-write volume: /home/A (3000
 *   bytes of tests/ext2write.c's pattern, seed 1) made, closed, sync (save
 *   1); A removed and /home/B (seed 2) made with no save between, closed,
 *   sync (saves 2 to 4, R3). The disk writes are logged one by one
 *   (ext2_disk_cut); with N the power is cut after write N (the boot
 *   halts there), otherwise "diskcut: DONE". Each file is closed before it
 *   is removed (note D: unlink-while-open is outside the cut sequences).
 *
 * test=diskcheck (disktest_check)
 *   The remount boot after a cut: /home/A, /home/B and /home/auto.txt
 *   looked up; each one's size and CRC32 (read through the VFS), or
 *   "absent"; then "diskcheck: DONE". The mount itself does the D12
 *   recount of a volume not cleanly unmounted.
 *
 * test=diskchurn (disktest_churn, the stress part)
 *   1000 cycles: a file made in /home, 1000 to 4072 bytes written, closed,
 *   removed; a sync every 50 and one at the end; the free block and inode
 *   counts must come back to their start. "diskchurn: DONE" or "FAILED".
 *
 * 1.5 stage 4 (docs/plans/1.5.md, C1):
 *
 * test=diskcontend (disktest_contend)
 *   On a read-write volume: the program bigio (writer 1) appends 8 writes
 *   of 32 KiB to /home/contend.bin through an O_APPEND open file the kernel
 *   gives it as fd 3, while a kernel thread saves (a sync) whenever
 *   something is dirty; both must finish (the program's exit status 0, no
 *   save failed, at least one save ended while the program ran); then a
 *   last sync, and the file must hold exactly the program's 256 KiB; no
 *   write or FLUSH command sent under the VFS lock since boot. The test
 *   checks the image afterwards (e2fsck -fn, debugfs). "diskcontend: DONE"
 *   or "diskcontend: FAILED".
 *
 * All run on thread 0 at boot (diskwrite's work on its own thread), and
 * take the VFS lock for each call, as the system calls do (in Disk mode
 * the disk lock first, since 1.5 stage 4).
 */

void disktest_write(void);
void disktest_nodes(void);
void disktest_cut(void);
void disktest_check(void);
void disktest_churn(void);
void disktest_contend(void);

#endif
