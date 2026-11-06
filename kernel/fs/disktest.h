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
 * Both run on thread 0 at boot (diskwrite's work on its own thread), and
 * take the VFS lock for each call, as the system calls do.
 */

void disktest_write(void);
void disktest_nodes(void);

#endif
