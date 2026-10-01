/*
 * Memory subsystem set-up (see memory.h).
 *
 * Order matters and is what makes "reserved wins" hold:
 *   1. validate the memory map before reading any entry
 *   2. build the reservation list (kernel, boot data, low 1 MiB)
 *   3. check the kernel really sits in usable RAM
 *   4. allocator: add type-1 regions, then remove every other map region,
 *      then remove the reservations, then finish
 *
 * Diagnostics go to the serial log in a fixed line format that
 * tests/memory_check.py parses to recompute the figures independently.
 */
#include "kernel/kprintf.h"
#include "kernel/memory/memory.h"
#include "kernel/memory/mmap.h"
#include "kernel/memory/pmm.h"
#include "kernel/memory/vm_layout.h"
#include "kernel/panic.h"

#define ADDR_LIMIT 0x100000000ull

/* Linker script and boot.asm symbols: only their addresses are meaningful.
 * They are higher-half virtual addresses (0.8); the memory manager works
 * with the PHYSICAL addresses of the kernel image, below 4 GiB. */
extern char __kernel_start[], __kernel_end[];
extern char __text_start[], __text_end[], __rodata_start[], __rodata_end[];
extern char __data_start[], __data_end[], __bss_start[], __bss_end[];
extern char stack_bottom[], stack_top[];

#define SYM(s) ((uint32_t)kernel_phys((uint64_t)(uintptr_t)(s)))

static struct phys_range reservations[RESERVE_MAX_RANGES];
static unsigned reservation_count;
static uint32_t map_addr, map_length;
static const void *map_view;            /* where the map is read (mmap.h) */

static uint32_t hi32(uint64_t v) { return (uint32_t)(v >> 32); }
static uint32_t lo32(uint64_t v) { return (uint32_t)v; }

uint32_t memory_bytes_to_mb(uint64_t bytes)
{
    return (uint32_t)((bytes + (1u << 19)) >> 20);
}

static void log_map_and_totals(struct memory_stats *st)
{
    struct mmap_iter it;
    struct mem_region r;

    klogf("Memory map: %u bytes at 0x%08x\n", map_length, map_addr);
    mmap_iter_init(&it, map_view, map_length);
    while (mmap_next(&it, &r)) {
        uint64_t end = r.base + r.length;
        klogf("mmap: base=0x%08x%08x end=0x%08x%08x type=%u %s\n",
              hi32(r.base), lo32(r.base), hi32(end), lo32(end), r.raw_type, mem_kind_str(r.kind));
        if (!mem_kind_is_ram(r.kind))
            continue;
        st->ram_bytes += r.length;
        if (end > ADDR_LIMIT)
            st->above_4g_bytes += end - (r.base > ADDR_LIMIT ? r.base : ADDR_LIMIT);
    }
}

static void log_kernel_layout(void)
{
    klogf("Kernel: text 0x%08x-0x%08x rodata 0x%08x-0x%08x data 0x%08x-0x%08x "
          "bss 0x%08x-0x%08x stack 0x%08x-0x%08x end 0x%08x\n",
          SYM(__text_start), SYM(__text_end), SYM(__rodata_start), SYM(__rodata_end),
          SYM(__data_start), SYM(__data_end), SYM(__bss_start), SYM(__bss_end),
          SYM(stack_bottom), SYM(stack_top), SYM(__kernel_end));
}

static bool kernel_in_usable_ram(void)
{
    struct mmap_iter it;
    struct mem_region r;
    mmap_iter_init(&it, map_view, map_length);
    while (mmap_next(&it, &r))
        if (r.kind == MEM_USABLE && r.base <= SYM(__kernel_start)
            && SYM(__kernel_end) <= r.base + r.length)
            return true;
    return false;
}

static void check(enum pmm_result result, const char *step)
{
    if (result != PMM_OK)
        panicf("Memory: allocator set-up failed while %s: %s", step, pmm_result_str(result));
}

void memory_init(const struct multiboot_info *mbi, struct memory_stats *st)
{
    *st = (struct memory_stats){ 0 };

    if (!mbi)
        panic("Memory: no Multiboot information from the bootloader.");
    if (!(mbi->flags & MULTIBOOT_INFO_MEM_MAP))
        panic("Memory: the bootloader provided no memory map (Multiboot flag 6).");

    uint32_t bad_offset;
    /* The map is read through the physical view (it is boot data, so it
     * stays in the view once the kernel's own tables are loaded). */
    const void *view = phys_view(mbi->mmap_addr);
    enum mmap_status ms = mmap_validate(mbi->mmap_addr, mbi->mmap_length, view, &bad_offset);
    if (ms != MMAP_OK)
        panicf("Memory: invalid memory map: %s (entry at byte %u).", mmap_status_str(ms), bad_offset);
    map_addr = mbi->mmap_addr;
    map_length = mbi->mmap_length;
    map_view = view;

    log_map_and_totals(st);
    log_kernel_layout();

    struct kernel_layout layout = {
        .kernel_start = SYM(__kernel_start), .kernel_end = SYM(__kernel_end),
        .bss_start = SYM(__bss_start), .bss_end = SYM(__bss_end),
        .stack_bottom = SYM(stack_bottom), .stack_top = SYM(stack_top),
    };
    enum reserve_status rs = reserve_build(mbi, &layout, reservations, RESERVE_MAX_RANGES,
                                           &reservation_count);
    if (rs != RESERVE_OK)
        panicf("Memory: cannot reserve kernel and boot data: %s.", reserve_status_str(rs));

    if (!kernel_in_usable_ram())
        panicf("Memory: kernel 0x%08x-0x%08x is not inside usable RAM.",
               SYM(__kernel_start), SYM(__kernel_end));

    /* Allocator: usable RAM in, every other map region out, then boot data. */
    struct mmap_iter it;
    struct mem_region r;
    pmm_begin();
    mmap_iter_init(&it, map_view, map_length);
    while (mmap_next(&it, &r))
        if (r.kind == MEM_USABLE)
            check(pmm_add_usable(r.base, r.base + r.length), "adding usable RAM");
    mmap_iter_init(&it, map_view, map_length);
    while (mmap_next(&it, &r))
        if (r.kind != MEM_USABLE)
            check(pmm_reserve(r.base, r.base + r.length), "excluding non-usable regions");

    st->usable_pages = pmm_count_allocatable();
    st->usable_bytes = (uint64_t)st->usable_pages << PAGE_SHIFT;

    for (unsigned i = 0; i < reservation_count; i++) {
        klogf("reserve: start=0x%08x end=0x%08x%08x %s\n", lo32(reservations[i].start),
              hi32(reservations[i].end), lo32(reservations[i].end), reservations[i].what);
        check(pmm_reserve(reservations[i].start, reservations[i].end), "reserving boot data");
    }
    pmm_finish();

    st->free_pages = pmm_free_pages();
    st->reserved_pages = st->usable_pages - st->free_pages;
    st->reservations = reservation_count;

    klogf("memory: ram_bytes=0x%08x%08x usable_bytes=0x%08x%08x above4g_bytes=0x%08x%08x "
          "usable_pages=%u free_pages=%u reserved_pages=%u\n",
          hi32(st->ram_bytes), lo32(st->ram_bytes), hi32(st->usable_bytes), lo32(st->usable_bytes),
          hi32(st->above_4g_bytes), lo32(st->above_4g_bytes),
          st->usable_pages, st->free_pages, st->reserved_pages);
    klogf("Memory: %u MB detected, %u MB usable below 4 GiB, %u MB above 4 GiB not used yet\n",
          memory_bytes_to_mb(st->ram_bytes),
          memory_bytes_to_mb(st->usable_bytes), memory_bytes_to_mb(st->above_4g_bytes));

    if (st->free_pages == 0)
        panic("Memory: no free pages left after reserving the kernel and boot data.");
}

uint32_t memory_alloc_page(void)
{
    return pmm_alloc_page();
}

void memory_free_page(uint32_t addr)
{
    switch (pmm_free_page(addr)) {
    case PMM_OK:
        return;
    case PMM_ERR_UNALIGNED:
        panicf("Attempt to free unaligned address 0x%08x", addr);
    case PMM_ERR_RESERVED:
        panicf("Attempt to free reserved page 0x%08x", addr);
    case PMM_ERR_DOUBLE_FREE:
        panicf("Double free of physical page 0x%08x", addr);
    default:
        panicf("Page 0x%08x freed before the allocator was ready", addr);
    }
}

bool memory_page_is_safe(uint32_t addr)
{
    uint64_t start = addr, end = (uint64_t)addr + PAGE_SIZE;
    bool in_usable = false;
    struct mmap_iter it;
    struct mem_region r;

    if (addr == 0 || (addr & (PAGE_SIZE - 1)))
        return false;

    mmap_iter_init(&it, map_view, map_length);
    while (mmap_next(&it, &r)) {
        uint64_t r_end = r.base + r.length;
        if (r.kind == MEM_USABLE) {
            if (r.base <= start && end <= r_end)
                in_usable = true;
        } else if (start < r_end && end > r.base) {
            return false;                       /* touches a non-usable region */
        }
    }
    if (!in_usable)
        return false;

    for (unsigned i = 0; i < reservation_count; i++)
        if (start < reservations[i].end && end > reservations[i].start)
            return false;
    return true;
}

const struct phys_range *memory_reservations(unsigned *count)
{
    *count = reservation_count;
    return reservations;
}
