#ifndef MADIRE_FS_FSTEST_H
#define MADIRE_FS_FSTEST_H

/*
 * The filesystem's storage diagnostics (1.0, stage 2), QEMU boot options
 * only (docs/plans/1.0.md, B7, D7). Both run on the live filesystem after
 * its boot self-test and leave it as they found it: the boot nodes only,
 * no data page, and the page allocator's free pages back to the start
 * minus what the kernel heap grew by (the heap never shrinks).
 *
 * test=fslimits: each B5 limit hit once, with its errno and short count:
 * 1024 nodes, 2 MiB per file, a hole, the page allocator refusing a page
 * under the budget, the budget itself, the heap exhausted (ENOMEM).
 *
 * test=fsfill: files filled to the budget (QEMU -m 32M: 3/4 of the free
 * pages; -m 64M: the 32 MiB cap), ENOSPC, every byte read back, all freed.
 *
 * Each logs a bounded number of lines (one per step, and at most 8 lines
 * about mismatches), never a line per node or per page.
 */

#include <stdbool.h>

bool fstest_limits(void);
bool fstest_fill(void);

#endif
