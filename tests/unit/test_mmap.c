/* Unit tests for kernel/memory/mmap.c (Multiboot memory-map parsing). */
#include <stdint.h>
#include <string.h>
#include "kernel/memory/mmap.h"
#include "kernel/multiboot.h"
#include "tests/unit/unit.h"

static uint8_t buf[4096];
static uint32_t len;

static void map_reset(void)
{
    memset(buf, 0, sizeof(buf));
    len = 0;
}

/* Appends one entry with the given `size` field (normally 20). */
static void map_add_sized(uint32_t size, uint64_t base, uint64_t length, uint32_t type)
{
    uint8_t *p = buf + len;
    memcpy(p, &size, 4);
    memcpy(p + 4, &base, 8);
    memcpy(p + 12, &length, 8);
    memcpy(p + 20, &type, 4);
    len += size + 4;
}

static void map_add(uint64_t base, uint64_t length, uint32_t type)
{
    map_add_sized(20, base, length, type);
}

static uint32_t buf_addr(void)
{
    return (uint32_t)(uintptr_t)buf;
}

static unsigned count_regions(void)
{
    struct mmap_iter it;
    struct mem_region r;
    unsigned n = 0;
    mmap_iter_init(&it, buf_addr(), len);
    while (mmap_next(&it, &r))
        n++;
    return n;
}

/* The map QEMU 8.2.2 reports for -m 128M (inspected with GRUB lsmmap). */
static void test_qemu_map(void)
{
    map_reset();
    map_add(0x0, 0x9fc00, 1);
    map_add(0x9fc00, 0x400, 2);
    map_add(0xf0000, 0x10000, 2);
    map_add(0x100000, 0x7ee0000, 1);
    map_add(0x7fe0000, 0x20000, 2);
    map_add(0xfffc0000, 0x40000, 2);
    CHECK_EQ(mmap_validate(buf_addr(), len, NULL), MMAP_OK);
    CHECK_EQ(count_regions(), 6);

    struct mmap_iter it;
    struct mem_region r;
    mmap_iter_init(&it, buf_addr(), len);
    CHECK(mmap_next(&it, &r));
    CHECK_EQ(r.base, 0);
    CHECK_EQ(r.length, 0x9fc00);
    CHECK_EQ(r.kind, MEM_USABLE);
    CHECK(mmap_next(&it, &r));
    CHECK_EQ(r.kind, MEM_RESERVED);
}

/* The map VirtualBox 7.2 reports for MadireOS-Test (4096 MB). */
static void test_virtualbox_map_types(void)
{
    map_reset();
    map_add(0x0, 0x9fc00, 1);
    map_add(0x9fc00, 0x400, 2);
    map_add(0xf0000, 0x10000, 2);
    map_add(0x100000, 0xdfef0000, 1);
    map_add(0xdfff0000, 0x10000, 3);
    map_add(0xfec00000, 0x1000, 2);
    map_add(0xfee00000, 0x1000, 2);
    map_add(0xfffc0000, 0x40000, 2);
    map_add(0x100000000ull, 0x20000000, 1);
    CHECK_EQ(mmap_validate(buf_addr(), len, NULL), MMAP_OK);
    CHECK_EQ(count_regions(), 9);

    struct mmap_iter it;
    struct mem_region r;
    enum mem_kind kinds[9];
    unsigned n = 0;
    mmap_iter_init(&it, buf_addr(), len);
    while (mmap_next(&it, &r) && n < 9)
        kinds[n++] = r.kind;
    CHECK_EQ(kinds[4], MEM_ACPI_RECLAIMABLE);
    CHECK_EQ(kinds[8], MEM_USABLE);          /* above 4 GiB, still type 1 */
}

static void test_type_classification(void)
{
    CHECK_EQ(mem_kind_from_type(1), MEM_USABLE);
    CHECK_EQ(mem_kind_from_type(2), MEM_RESERVED);
    CHECK_EQ(mem_kind_from_type(3), MEM_ACPI_RECLAIMABLE);
    CHECK_EQ(mem_kind_from_type(4), MEM_ACPI_NVS);
    CHECK_EQ(mem_kind_from_type(5), MEM_BAD);
    CHECK_EQ(mem_kind_from_type(0), MEM_RESERVED);
    CHECK_EQ(mem_kind_from_type(12), MEM_RESERVED);     /* unknown -> reserved */
    CHECK(!mem_kind_is_ram(MEM_RESERVED));
    CHECK(mem_kind_is_ram(MEM_USABLE));
    CHECK(mem_kind_is_ram(MEM_ACPI_RECLAIMABLE));
    CHECK(mem_kind_is_ram(MEM_BAD));
}

static void test_larger_entry_size_skipped_correctly(void)
{
    map_reset();
    map_add_sized(24, 0x0, 0x1000, 1);       /* 4 extra bytes after type */
    map_add(0x100000, 0x100000, 1);
    CHECK_EQ(mmap_validate(buf_addr(), len, NULL), MMAP_OK);
    CHECK_EQ(count_regions(), 2);
}

static void test_entry_size_too_small(void)
{
    uint32_t off = 99;
    map_reset();
    map_add(0x0, 0x1000, 1);
    map_add_sized(16, 0x100000, 0x1000, 1);
    CHECK_EQ(mmap_validate(buf_addr(), len, &off), MMAP_ERR_ENTRY_SIZE);
    CHECK_EQ(off, 24);
}

static void test_truncated_entry(void)
{
    map_reset();
    map_add(0x0, 0x1000, 1);
    map_add(0x100000, 0x1000, 1);
    CHECK_EQ(mmap_validate(buf_addr(), len - 1, NULL), MMAP_ERR_TRUNCATED);
    CHECK_EQ(mmap_validate(buf_addr(), 24 + 2, NULL), MMAP_ERR_TRUNCATED);  /* partial size field */
}

static void test_overflowing_entry(void)
{
    map_reset();
    map_add(0xfffffffffffff000ull, 0x2000, 1);
    CHECK_EQ(mmap_validate(buf_addr(), len, NULL), MMAP_ERR_OVERFLOW);

    map_reset();
    map_add(0xfffffffffffff000ull, 0x1000, 1);   /* end would be exactly 2^64 */
    CHECK_EQ(mmap_validate(buf_addr(), len, NULL), MMAP_ERR_OVERFLOW);

    map_reset();
    map_add(0xffffffffffffe000ull, 0x1000, 1);   /* ends at 2^64 - 4096: fine */
    CHECK_EQ(mmap_validate(buf_addr(), len, NULL), MMAP_OK);
}

static void test_empty_maps(void)
{
    map_reset();
    CHECK_EQ(mmap_validate(buf_addr(), 0, NULL), MMAP_ERR_EMPTY);

    map_reset();
    map_add(0x100000, 0, 1);                     /* only a zero-length entry */
    CHECK_EQ(mmap_validate(buf_addr(), len, NULL), MMAP_ERR_EMPTY);
}

static void test_zero_length_entries_skipped(void)
{
    map_reset();
    map_add(0x0, 0x1000, 1);
    map_add(0x5000, 0, 1);
    map_add(0x100000, 0x1000, 2);
    CHECK_EQ(mmap_validate(buf_addr(), len, NULL), MMAP_OK);
    CHECK_EQ(count_regions(), 2);
}

static void test_bad_buffer(void)
{
    CHECK_EQ(mmap_validate(0, 24, NULL), MMAP_ERR_BAD_BUFFER);
    CHECK_EQ(mmap_validate(0xfffffff0u, 0x20, NULL), MMAP_ERR_BAD_BUFFER);  /* wraps 4 GiB */
}

static void test_unsorted_and_overlapping_are_reported_as_is(void)
{
    /* The parser reports entries verbatim; the allocator applies
     * "reserved wins" (tested in test_pmm.c). */
    map_reset();
    map_add(0x200000, 0x100000, 1);
    map_add(0x0, 0x9f000, 1);
    map_add(0x250000, 0x1000, 2);                /* inside the first region */
    CHECK_EQ(mmap_validate(buf_addr(), len, NULL), MMAP_OK);
    CHECK_EQ(count_regions(), 3);
}

static void test_many_entries(void)
{
    map_reset();
    for (unsigned i = 0; i < 150; i++)
        map_add((uint64_t)i * 0x10000, 0x8000, (i % 2) ? 2 : 1);
    CHECK_EQ(mmap_validate(buf_addr(), len, NULL), MMAP_OK);
    CHECK_EQ(count_regions(), 150);              /* no silent entry limit */
}

int main(void)
{
    printf("mmap unit tests\n");
    RUN(test_qemu_map);
    RUN(test_virtualbox_map_types);
    RUN(test_type_classification);
    RUN(test_larger_entry_size_skipped_correctly);
    RUN(test_entry_size_too_small);
    RUN(test_truncated_entry);
    RUN(test_overflowing_entry);
    RUN(test_empty_maps);
    RUN(test_zero_length_entries_skipped);
    RUN(test_bad_buffer);
    RUN(test_unsorted_and_overlapping_are_reported_as_is);
    RUN(test_many_entries);
    return unit_report("mmap");
}
