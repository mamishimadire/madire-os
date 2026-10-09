/* Unit tests for kernel/arch/x86/gdt_desc.h (0.10 GDT layout, decoded
 * independently of the constants' comments). */
#include <stdint.h>
#include "kernel/arch/x86/gdt.h"
#include "kernel/arch/x86/gdt_desc.h"
#include "tests/unit/unit.h"

static void test_selectors(void)
{
    CHECK_EQ(GDT_KERNEL_CODE, 0x08);
    CHECK_EQ(GDT_KERNEL_DATA, 0x10);
    CHECK_EQ(GDT_TSS, 0x18);                        /* 16 bytes: entries 3 and 4 */
    CHECK_EQ(GDT_USER32_PLACEHOLDER, 0x28);         /* entry 5 */
    CHECK_EQ(GDT_USER_DATA, 0x30);
    CHECK_EQ(GDT_USER_CODE, 0x38);
    CHECK_EQ(GDT_USER_DATA_SEL, 0x33);
    CHECK_EQ(GDT_USER_CODE_SEL, 0x3B);
    CHECK_EQ(GDT_ENTRIES, 8);
    CHECK_EQ(GDT_LIMIT, 0x3F);
    CHECK(0x40 > GDT_LIMIT);                        /* gpflimit's selector is past the end */
}

static void test_kernel_descriptors(void)
{
    CHECK(desc_present(DESC_KERNEL_CODE) && desc_code(DESC_KERNEL_CODE));
    CHECK_EQ(desc_dpl(DESC_KERNEL_CODE), 0);
    CHECK(desc_long(DESC_KERNEL_CODE) && !desc_db(DESC_KERNEL_CODE));
    CHECK(desc_present(DESC_KERNEL_DATA) && desc_data(DESC_KERNEL_DATA) && desc_rw(DESC_KERNEL_DATA));
    CHECK_EQ(desc_dpl(DESC_KERNEL_DATA), 0);
    /* The 0.7 values, unchanged. */
    CHECK_EQ(DESC_KERNEL_CODE, 0x00AF9A000000FFFFull);
    CHECK_EQ(DESC_KERNEL_DATA, 0x00CF92000000FFFFull);
}

static void test_user_descriptors(void)
{
    CHECK(desc_present(DESC_USER_CODE) && desc_code(DESC_USER_CODE) && desc_rw(DESC_USER_CODE));
    CHECK_EQ(desc_dpl(DESC_USER_CODE), 3);
    CHECK(desc_long(DESC_USER_CODE) && !desc_db(DESC_USER_CODE));   /* 64-bit, not 32 */
    CHECK(desc_present(DESC_USER_DATA) && desc_data(DESC_USER_DATA) && desc_rw(DESC_USER_DATA));
    CHECK_EQ(desc_dpl(DESC_USER_DATA), 3);
}

static void test_placeholder_raises_gp(void)
{
    /* Entry 5 is all zero: not present, a system type (S=0). A MOV into DS
     * of selector 0x28 passes the limit check and fails the type check:
     * #GP(0x28), the error code test=gpf expects. */
    CHECK_EQ(DESC_USER32_NONE, 0);
    CHECK(!desc_present(DESC_USER32_NONE));
    CHECK(!desc_s(DESC_USER32_NONE));
    CHECK(!desc_loads_into_ds(DESC_USER32_NONE));
    CHECK(0x28 + 7 <= GDT_LIMIT);                   /* inside the table: not the limit check */
    /* The real segments would load (so 0x28 must stay zero). */
    CHECK(desc_loads_into_ds(DESC_KERNEL_DATA));
    CHECK(desc_loads_into_ds(DESC_USER_DATA));
    CHECK(desc_loads_into_ds(DESC_USER_CODE));      /* readable code loads into DS */
}

static void test_star(void)
{
    uint64_t star = gdt_star();
    uint16_t syscall_cs = (uint16_t)(star >> 32);
    uint16_t sysret_base = (uint16_t)(star >> 48);
    CHECK_EQ(syscall_cs, GDT_KERNEL_CODE);
    CHECK_EQ(syscall_cs + 8, GDT_KERNEL_DATA);                      /* SYSCALL's SS */
    CHECK_EQ((sysret_base + 16) | 3, GDT_USER_CODE_SEL);            /* 64-bit SYSRET's CS */
    CHECK_EQ((sysret_base + 8) | 3, GDT_USER_DATA_SEL);             /* SYSRET's SS */
    CHECK_EQ(sysret_base & ~3u, GDT_USER32_PLACEHOLDER);            /* 32-bit SYSRET: the zero entry */
    CHECK_EQ(star & 0xFFFFFFFFull, 0);                              /* no 32-bit SYSCALL target */
    CHECK_EQ(star, 0x002B000800000000ull);
}

int main(void)
{
    RUN(test_selectors);
    RUN(test_kernel_descriptors);
    RUN(test_user_descriptors);
    RUN(test_placeholder_raises_gp);
    RUN(test_star);
    return unit_report("gdt010");
}
