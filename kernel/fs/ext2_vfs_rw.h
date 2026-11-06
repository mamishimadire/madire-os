#ifndef MADIRE_FS_EXT2_VFS_RW_H
#define MADIRE_FS_EXT2_VFS_RW_H

/*
 * The writer of ext2_vfs.h over the write core (1.4 stage 10; docs/plans/
 * 1.4.md, C5, C8, D9), pure: tests/unit/test_ext2_vfs_rw.c. Each call of
 * struct ext2_vfs_writer becomes the write core's operation on the mounted
 * volume (ext2_write.h), its answer an errno of fs_errno.h, with Linux's
 * value where Linux has one: EXT2_EAGAIN is -FS_EAGAIN (internal: save,
 * then try again), a read or a number on the disk out of range is -FS_EIO.
 * keep_ino goes to the core's keep_ino around the call (an open file's
 * deletion waits for forget). writable: 0 while the volume is read-write
 * and no save has failed, else -FS_EROFS.
 *
 * Not locked: the caller holds the VFS lock. Written from the plan; no
 * reference project's code was consulted for it.
 */

#include "kernel/fs/ext2_vfs.h"
#include "kernel/fs/ext2_write.h"

/* wr's calls on volume w (both must stay valid while used). */
void ext2_vfs_rw_init(struct ext2_vfs_writer *wr, struct ext2_wfs *w);

/* The write core's error code as an errno of fs_errno.h (negative). */
int ext2_vfs_rw_errno(int ext2_err);

#endif
