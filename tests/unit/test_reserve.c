/* Unit tests for kernel/memory/reserve.c, plus reserve + pmm together. */
#include <stdint.h>
#include <string.h>
#include "kernel/memory/pmm.h"
#include "kernel/memory/reserve.h"
#include "tests/unit/unit.h"

static struct multiboot_info mbi;
static uint8_t mmap_buf[48];
static char cmdline[] = "/boot/madire.kernel test=none";
static struct multiboot_module mods[3];
static char mod_name[] = "initrd";
static uint8_t mod_data[3 * PAGE_SIZE] __attribute__((aligned(4096)));

static struct phys_range ranges[RESERVE_MAX_RANGES];
static unsigned count;

static const struct kernel_layout kernel = {
    .kernel_start = 0x100000, .kernel_end = 0x149000,
    .bss_start = 0x104000, .bss_end = 0x148bcc,
    .stack_bottom = 0x104000, .stack_top = 0x108000,
};

static uint32_t addr_of(const void *p)
{
    return (uint32_t)(uintptr_t)p;
}

static void mbi_reset(void)
{
    memset(&mbi, 0, sizeof(mbi));
    mbi.flags = MULTIBOOT_INFO_MEM_MAP | MULTIBOOT_INFO_CMDLINE;
    mbi.mmap_addr = addr_of(mmap_buf);
    mbi.mmap_length = sizeof(mmap_buf);
    mbi.cmdline = addr_of(cmdline);
}

static const struct phys_range *find(const char *what)
{
    for (unsigned i = 0; i < count; i++)
        if (strcmp(ranges[i].what, what) == 0)
            return &ranges[i];
    return NULL;
}

static void test_basic_reservations(void)
{
    mbi_reset();
    CHECK_EQ(reserve_build(&mbi, &kernel, ranges, RESERVE_MAX_RANGES, &count), RESERVE_OK);
    CHECK_EQ(count, 5);

    const struct phys_range *r = find("low memory (BIOS data, VGA, ROM)");
    CHECK(r && r->start == 0 && r->end == 0x100000);
    r = find("kernel image (text, data, bss, stack)");
    CHECK(r && r->start == 0x100000 && r->end == 0x149000);
    r = find("multiboot info");
    CHECK(r && r->start == addr_of(&mbi) && r->end == addr_of(&mbi) + sizeof(mbi));
    r = find("multiboot memory map");
    CHECK(r && r->start == addr_of(mmap_buf) && r->end == addr_of(mmap_buf) + sizeof(mmap_buf));
    r = find("kernel command line");
    CHECK(r && r->start == addr_of(cmdline) && r->end == addr_of(cmdline) + sizeof(cmdline));

    CHECK(reserve_contains(ranges, count, 0xb8000));       /* VGA text buffer */
    CHECK(reserve_contains(ranges, count, 0x0));
    CHECK(reserve_contains(ranges, count, 0x148fff));
    CHECK(!reserve_contains(ranges, count, 0x149000));      /* first page after the kernel */
    CHECK(!reserve_contains(ranges, count, 0x100000000ull));
}

static void test_optional_fields_only_when_flagged(void)
{
    mbi_reset();
    mbi.flags = 0;                               /* no map, no cmdline, no modules */
    CHECK_EQ(reserve_build(&mbi, &kernel, ranges, RESERVE_MAX_RANGES, &count), RESERVE_OK);
    CHECK_EQ(count, 3);
    CHECK(find("kernel command line") == NULL);
}

static void test_modules_reserved(void)
{
    mbi_reset();
    mods[0] = (struct multiboot_module){ addr_of(mod_data), addr_of(mod_data) + 5000, addr_of(mod_name), 0 };
    mods[1] = (struct multiboot_module){ addr_of(mod_data) + 8192, addr_of(mod_data) + 8192, 0, 0 };  /* empty */
    mbi.flags |= MULTIBOOT_INFO_MODS;
    mbi.mods_count = 2;
    mbi.mods_addr = addr_of(mods);
    CHECK_EQ(reserve_build(&mbi, &kernel, ranges, RESERVE_MAX_RANGES, &count), RESERVE_OK);
    CHECK_EQ(count, 8);   /* 5 + table + module 0 + its name; empty module 1 adds nothing */

    const struct phys_range *r = find("boot module table");
    CHECK(r && r->start == addr_of(mods) && r->end == addr_of(mods) + 2 * sizeof(struct multiboot_module));
    r = find("boot module");
    CHECK(r && r->start == addr_of(mod_data) && r->end == addr_of(mod_data) + 5000);
    CHECK(find("boot module name") != NULL);
}

static void test_bad_module_range(void)
{
    mbi_reset();
    mods[0] = (struct multiboot_module){ 0x300000, 0x200000, 0, 0 };   /* end < start */
    mbi.flags |= MULTIBOOT_INFO_MODS;
    mbi.mods_count = 1;
    mbi.mods_addr = addr_of(mods);
    CHECK_EQ(reserve_build(&mbi, &kernel, ranges, RESERVE_MAX_RANGES, &count), RESERVE_ERR_BAD_POINTER);
}

static void test_too_many_ranges(void)
{
    mbi_reset();
    CHECK_EQ(reserve_build(&mbi, &kernel, ranges, 4, &count), RESERVE_ERR_FULL);
}

static void test_bad_pointers(void)
{
    mbi_reset();
    mbi.mmap_addr = 0;
    CHECK_EQ(reserve_build(&mbi, &kernel, ranges, RESERVE_MAX_RANGES, &count), RESERVE_ERR_BAD_POINTER);

    mbi_reset();
    mbi.mmap_addr = 0xfffff000u;
    mbi.mmap_length = 0x2000;                    /* wraps past 4 GiB */
    CHECK_EQ(reserve_build(&mbi, &kernel, ranges, RESERVE_MAX_RANGES, &count), RESERVE_ERR_BAD_POINTER);

    mbi_reset();
    mbi.cmdline = 0;
    CHECK_EQ(reserve_build(&mbi, &kernel, ranges, RESERVE_MAX_RANGES, &count), RESERVE_ERR_BAD_POINTER);

    CHECK_EQ(reserve_build(NULL, &kernel, ranges, RESERVE_MAX_RANGES, &count), RESERVE_ERR_BAD_POINTER);
}

static void test_unterminated_string(void)
{
    static char long_line[RESERVE_STRING_MAX + 16];
    memset(long_line, 'x', sizeof(long_line));   /* no terminator within the limit */
    mbi_reset();
    mbi.cmdline = addr_of(long_line);
    CHECK_EQ(reserve_build(&mbi, &kernel, ranges, RESERVE_MAX_RANGES, &count), RESERVE_ERR_BAD_STRING);
}

static void test_kernel_layout_checks(void)
{
    struct kernel_layout k = kernel;
    mbi_reset();

    k.kernel_end = 0x148800;                     /* not page-aligned */
    CHECK_EQ(reserve_build(&mbi, &k, ranges, RESERVE_MAX_RANGES, &count), RESERVE_ERR_KERNEL);

    k = kernel;
    k.kernel_end = k.kernel_start;               /* empty */
    CHECK_EQ(reserve_build(&mbi, &k, ranges, RESERVE_MAX_RANGES, &count), RESERVE_ERR_KERNEL);

    k = kernel;
    k.stack_top = 0x149000 + 0x1000;             /* stack past .bss */
    CHECK_EQ(reserve_build(&mbi, &k, ranges, RESERVE_MAX_RANGES, &count), RESERVE_ERR_STACK);

    k = kernel;
    k.bss_end = 0x14a000;                        /* .bss past kernel end */
    CHECK_EQ(reserve_build(&mbi, &k, ranges, RESERVE_MAX_RANGES, &count), RESERVE_ERR_STACK);
}

/* End to end: a QEMU-shaped map, reservations applied, exhaust the
 * allocator and prove no reserved byte was ever handed out. */
static void test_allocator_never_returns_reserved(void)
{
    mbi_reset();
    mods[0] = (struct multiboot_module){ 0x400000, 0x403800, 0, 0 };   /* 3.5 pages */
    mbi.flags |= MULTIBOOT_INFO_MODS;
    mbi.mods_count = 1;
    mbi.mods_addr = addr_of(mods);
    CHECK_EQ(reserve_build(&mbi, &kernel, ranges, RESERVE_MAX_RANGES, &count), RESERVE_OK);

    pmm_begin();
    pmm_add_usable(0x0, 0x9fc00);
    pmm_add_usable(0x100000, 0x2000000);         /* 32 MiB machine */
    for (unsigned i = 0; i < count; i++)
        pmm_reserve(ranges[i].start, ranges[i].end);
    pmm_finish();

    uint32_t n = 0, p;
    while ((p = pmm_alloc_page()) != 0) {
        /* No byte of the page may be reserved: check both ends. */
        CHECK(!reserve_contains(ranges, count, p));
        CHECK(!reserve_contains(ranges, count, (uint64_t)p + PAGE_SIZE - 1));
        n++;
    }
    /* 0x2000000 - 0x149000 bytes above the kernel, minus the module's 4 pages
     * (0x400000-0x404000). The host test's own mbi/mmap/cmdline live in the
     * test process, far above 32 MiB, so they remove nothing here. */
    CHECK_EQ(n, (0x2000000 - 0x149000) / PAGE_SIZE - 4);
}

int main(void)
{
    printf("reserve unit tests\n");
    RUN(test_basic_reservations);
    RUN(test_optional_fields_only_when_flagged);
    RUN(test_modules_reserved);
    RUN(test_bad_module_range);
    RUN(test_too_many_ranges);
    RUN(test_bad_pointers);
    RUN(test_unterminated_string);
    RUN(test_kernel_layout_checks);
    RUN(test_allocator_never_returns_reserved);
    return unit_report("reserve");
}
