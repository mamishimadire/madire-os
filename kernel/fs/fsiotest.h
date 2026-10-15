#ifndef MADIRE_FS_FSIOTEST_H
#define MADIRE_FS_FSIOTEST_H

/*
 * The file system call diagnostics (1.0, stage 5), QEMU boot options only
 * (plan B7, D7; added to B7's list like stage 4's test=fdsys).
 *
 * test=fileio: the programs fileio and fileerr: open and its flags, lseek,
 * the stat calls, ftruncate, openat and newfstatat, the console
 * descriptors, EMFILE, the errno matrix (nothing changed by it), ENFILE
 * (the kernel fills the open-file table) and ENOSPC (the kernel fills the
 * nodes); nothing left behind.
 *
 * test=bigio: the program bigio: the largest single call a program can
 * hold (61,440 bytes), 1 MiB and 2 MiB files in 32 KiB calls, EFBIG, the
 * short count at 2 MiB, the budget, buffers that turn unmapped part-way,
 * two writers appending through separate O_APPEND open files (the node
 * write mark forced), the VFS-lock holds (a 2 MiB truncate among them).
 *
 * test=filecontend: the busy mark with 4 sharers, 3 forced to queue; 4
 * workers and thread 0 contending for the VFS lock with 2 ring-3 spinners
 * running: the holds, the hand-off gaps, the waits, the turns.
 */

#include <stdbool.h>

bool fsiotest_fileio(void);
bool fsiotest_bigio(void);
bool fsiotest_contend(void);

#endif
