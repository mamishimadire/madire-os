#ifndef MADIRE_FS_VFS_H
#define MADIRE_FS_VFS_H

/*
 * The kernel's filesystem (1.0): the live RAM filesystem mounted at "/",
 * holding /home and /tmp at boot (Live mode: nothing is read from or
 * written to any disk). kernel/fs/vfs.c; the logic is in the pure
 * path_core.c, vfs_core.c and ramfs_core.c.
 */

#include <stdbool.h>
#include <stdint.h>
#include "kernel/fs/vfs_core.h"

struct ramfs;

/* Builds the filesystem and runs its boot self-test (create, look up,
 * paths, limits, errors, rename, depth, file data (stage 2), then
 * everything removed with no node, heap byte or data page left). Fixes
 * the data budget from the free pages now. Needs the kernel heap. false:
 * the self-test failed (details on the serial log). */
bool vfs_boot(void);

/* The mounted filesystem (after vfs_boot). */
struct vfs *vfs_root(void);

/* Stage 2: the RAM filesystem under it (its data calls, counts and
 * budget), and the free pages at boot its budget was computed from. */
struct ramfs *vfs_ramfs(void);
uint32_t vfs_boot_free_pages(void);

/* Stage 3 (D10): the VFS lock (kernel/proc/klock.h). Every user of the
 * filesystem takes it around each bounded piece of work: one path lookup,
 * one chunk of at most 4096 bytes, one folder's entries (plan B2). */
struct klock *vfs_lock(void);

/* Stage 5: file times are seconds since 1970 from the RTC read at boot,
 * plus the uptime (no time zone). vfs_boot_epoch: that time at uptime 0
 * (0 if the RTC could not be read: times are then seconds of uptime);
 * vfs_now: the time now. */
int64_t vfs_boot_epoch(void);
int64_t vfs_now(void);

#endif
