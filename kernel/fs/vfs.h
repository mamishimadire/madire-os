#ifndef MADIRE_FS_VFS_H
#define MADIRE_FS_VFS_H

/*
 * The kernel's filesystem (1.0): the live RAM filesystem mounted at "/",
 * holding /home and /tmp at boot (Live mode: nothing is read from or
 * written to any disk). kernel/fs/vfs.c; the logic is in the pure
 * path_core.c, vfs_core.c and ramfs_core.c.
 */

#include <stdbool.h>
#include "kernel/fs/vfs_core.h"

/* Builds the filesystem and runs its boot self-test (create, look up,
 * paths, limits, errors, rename, depth, then everything removed with no
 * node or heap byte left). Needs the kernel heap. false: the self-test
 * failed (details on the serial log). */
bool vfs_boot(void);

/* The mounted filesystem (after vfs_boot). */
struct vfs *vfs_root(void);

#endif
