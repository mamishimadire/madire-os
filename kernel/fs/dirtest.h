#ifndef MADIRE_FS_DIRTEST_H
#define MADIRE_FS_DIRTEST_H

/*
 * The folder call diagnostics (1.0, stage 6), QEMU boot options only
 * (plan B7, D7; test=dirops added to B7's list, S6-8).
 *
 * test=dirops: the program dirops: getcwd and chdir, relative paths and
 * AT_FDCWD, the errno matrices of mkdir, mkdirat, rmdir, unlink, unlinkat
 * and rename, getdents64 (listings, small buffers, entries removed and
 * added between calls, errors), name and path limits; the errno matrix
 * changed nothing; ENOSPC (the kernel fills the nodes); current folders
 * given back by teardowns after an exit, a fault and a kill; a folder of
 * 1000 entries listed; the VFS lock held in bounded pieces.
 *
 * test=fsunlink (plan B7): unlink, rename-over and rmdir of files and
 * folders that are open or a current folder: an open file's data stays
 * until its last close, then its pages are given back in steps of at most
 * 64 per hold (S6-6 (a)) on unlink, rename-over, a last close and a
 * teardown after a kill or a fault; a removed current folder or open
 * folder answers ENOENT.
 */

#include <stdbool.h>

bool dirtest_dirops(void);
bool dirtest_fsunlink(void);

#endif
