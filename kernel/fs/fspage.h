#ifndef MADIRE_FS_FSPAGE_H
#define MADIRE_FS_FSPAGE_H

/*
 * The RAM filesystem's page source in the kernel (1.0, stage 2): file
 * pages are 4 KiB frames from the page allocator, never heap memory, and
 * are reached through the physical-access window, one at a time (there is
 * no direct map). The window keeps preemption off while a page is mapped,
 * so each mapping covers one page's copy only.
 */

#include "kernel/fs/ramfs_core.h"

extern const struct ramfs_pages fspage_source;

#endif
