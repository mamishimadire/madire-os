/*
 * Reserved-memory list (see reserve.h). Multiboot addresses are physical;
 * with paging off they are also valid pointers, which is how strings and
 * the module table are read here.
 */
#include "kernel/memory/reserve.h"

#define ADDR_LIMIT 0x100000000ull
#define PAGE_MASK  0xFFFu

struct builder {
    struct phys_range *out;
    unsigned max;
    unsigned count;
};

static enum reserve_status add(struct builder *b, uint64_t start, uint64_t end, const char *what)
{
    if (b->count >= b->max)
        return RESERVE_ERR_FULL;
    b->out[b->count].start = start;
    b->out[b->count].end = end;
    b->out[b->count].what = what;
    b->count++;
    return RESERVE_OK;
}

/* Reserves [addr, addr + length) given as a 32-bit pointer and a length. */
static enum reserve_status add_block(struct builder *b, uint32_t addr, uint64_t length,
                                     const char *what)
{
    if (addr == 0 || (uint64_t)addr + length > ADDR_LIMIT)
        return RESERVE_ERR_BAD_POINTER;
    return add(b, addr, (uint64_t)addr + length, what);
}

/* Reserves a NUL-terminated string, including its terminator. */
static enum reserve_status add_string(struct builder *b, uint32_t addr, const char *what)
{
    if (addr == 0)
        return RESERVE_ERR_BAD_POINTER;
    const char *s = (const char *)(uintptr_t)addr;
    uint32_t len = 0;
    while (len < RESERVE_STRING_MAX && (uint64_t)addr + len < ADDR_LIMIT && s[len])
        len++;
    if (len == RESERVE_STRING_MAX || (uint64_t)addr + len >= ADDR_LIMIT)
        return RESERVE_ERR_BAD_STRING;
    return add_block(b, addr, (uint64_t)len + 1, what);
}

enum reserve_status reserve_build(const struct multiboot_info *mbi,
                                  const struct kernel_layout *k,
                                  struct phys_range *out, unsigned max,
                                  unsigned *count)
{
    struct builder b = { out, max, 0 };
    enum reserve_status s;
    *count = 0;

    if (!mbi)
        return RESERVE_ERR_BAD_POINTER;

    if (k->kernel_start >= k->kernel_end
        || (k->kernel_start & PAGE_MASK) || (k->kernel_end & PAGE_MASK))
        return RESERVE_ERR_KERNEL;
    if (k->bss_start < k->kernel_start || k->bss_end > k->kernel_end
        || k->bss_start > k->bss_end
        || k->stack_bottom < k->bss_start || k->stack_top > k->bss_end
        || k->stack_bottom >= k->stack_top)
        return RESERVE_ERR_STACK;

    if ((s = add(&b, 0, RESERVE_LOW_MEMORY_END, "low memory (BIOS data, VGA, ROM)")))
        return s;
    if ((s = add(&b, k->kernel_start, k->kernel_end, "kernel image (text, data, bss, stack)")))
        return s;
    if ((s = add_block(&b, (uint32_t)(uintptr_t)mbi, sizeof(*mbi), "multiboot info")))
        return s;

    if (mbi->flags & MULTIBOOT_INFO_MEM_MAP)
        if ((s = add_block(&b, mbi->mmap_addr, mbi->mmap_length, "multiboot memory map")))
            return s;

    if (mbi->flags & MULTIBOOT_INFO_CMDLINE)
        if ((s = add_string(&b, mbi->cmdline, "kernel command line")))
            return s;

    if ((mbi->flags & MULTIBOOT_INFO_MODS) && mbi->mods_count) {
        uint64_t table_len = (uint64_t)mbi->mods_count * sizeof(struct multiboot_module);
        if ((s = add_block(&b, mbi->mods_addr, table_len, "boot module table")))
            return s;

        const struct multiboot_module *mods =
            (const struct multiboot_module *)(uintptr_t)mbi->mods_addr;
        for (uint32_t i = 0; i < mbi->mods_count; i++) {
            if (mods[i].mod_end < mods[i].mod_start)
                return RESERVE_ERR_BAD_POINTER;
            if (mods[i].mod_end > mods[i].mod_start)
                if ((s = add(&b, mods[i].mod_start, mods[i].mod_end, "boot module")))
                    return s;
            if (mods[i].string)
                if ((s = add_string(&b, mods[i].string, "boot module name")))
                    return s;
        }
    }

    *count = b.count;
    return RESERVE_OK;
}

bool reserve_contains(const struct phys_range *ranges, unsigned count, uint64_t addr)
{
    for (unsigned i = 0; i < count; i++)
        if (addr >= ranges[i].start && addr < ranges[i].end)
            return true;
    return false;
}

const char *reserve_status_str(enum reserve_status status)
{
    switch (status) {
    case RESERVE_OK:              return "ok";
    case RESERVE_ERR_FULL:        return "too many reserved ranges";
    case RESERVE_ERR_KERNEL:      return "kernel range is empty or not page-aligned";
    case RESERVE_ERR_STACK:       return "boot stack is outside .bss, or .bss is outside the kernel";
    case RESERVE_ERR_BAD_POINTER: return "invalid multiboot pointer or range";
    case RESERVE_ERR_BAD_STRING:  return "multiboot string is not terminated";
    }
    return "unknown reservation error";
}
