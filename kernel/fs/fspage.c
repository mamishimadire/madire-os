/*
 * The RAM filesystem's page source in the kernel (1.0, stage 2): see
 * fspage.h. Frames below 4 GiB (the page allocator's range).
 */
#include "kernel/fs/fspage.h"
#include "kernel/memory/memory.h"
#include "kernel/memory/paging.h"

static uint64_t fp_alloc(void)
{
    return memory_alloc_page();
}

static void fp_free(uint64_t frame)
{
    memory_free_page((uint32_t)frame);
}

static volatile void *fp_map(uint64_t frame)
{
    return vmm_window_map(frame);
}

static void fp_unmap(void)
{
    vmm_window_unmap();
}

const struct ramfs_pages fspage_source = {
    .alloc = fp_alloc,
    .free = fp_free,
    .map = fp_map,
    .unmap = fp_unmap,
};
