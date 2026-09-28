/* CPUID and CR0 access (see cpuid.h). */
#include "kernel/arch/x86/cpuid.h"

#define EFLAGS_ID        0x00200000u
#define LEAF_EXT_MAX     0x80000000u
#define LEAF_BRAND_FIRST 0x80000002u
#define LEAF_BRAND_LAST  0x80000004u
#define LEAF_HYPERVISOR  0x40000000u
#define ECX_HYPERVISOR   0x80000000u

bool cpuid_supported(void)
{
    uint32_t before, after;
    __asm__ volatile ("pushfl\n\t"
                      "popl %0\n\t"
                      "movl %0, %1\n\t"
                      "xorl %2, %1\n\t"
                      "pushl %1\n\t"
                      "popfl\n\t"
                      "pushfl\n\t"
                      "popl %1\n\t"
                      "pushl %0\n\t"             /* restore the original EFLAGS */
                      "popfl"
                      : "=&r"(before), "=&r"(after)
                      : "i"(EFLAGS_ID)
                      : "cc");
    return ((before ^ after) & EFLAGS_ID) != 0;
}

static void cpuid(uint32_t leaf, uint32_t r[4])
{
    __asm__ volatile ("cpuid"
                      : "=a"(r[0]), "=b"(r[1]), "=c"(r[2]), "=d"(r[3])
                      : "a"(leaf), "c"(0));
}

static void zero(uint32_t *words, int n)
{
    for (int i = 0; i < n; i++)
        words[i] = 0;
}

void cpuid_read_all(struct cpuid_raw *raw)
{
    raw->max_basic = raw->max_ext = 0;
    raw->has_leaf1 = raw->has_brand = raw->has_hv = false;
    zero(raw->leaf0, 4);
    zero(raw->leaf1, 4);
    zero(raw->brand, 12);
    zero(raw->hv, 4);

    raw->available = cpuid_supported();
    if (!raw->available)
        return;

    cpuid(0, raw->leaf0);
    raw->max_basic = raw->leaf0[0];

    if (raw->max_basic >= 1) {
        cpuid(1, raw->leaf1);
        raw->has_leaf1 = true;
    }

    uint32_t ext[4];
    cpuid(LEAF_EXT_MAX, ext);
    /* A CPU without extended leaves returns unrelated data here; only
     * trust a value in the extended range. */
    if (ext[0] >= LEAF_EXT_MAX && ext[0] <= LEAF_EXT_MAX + 0xFFFF)
        raw->max_ext = ext[0];
    if (raw->max_ext >= LEAF_BRAND_LAST) {
        for (uint32_t i = 0; i <= LEAF_BRAND_LAST - LEAF_BRAND_FIRST; i++)
            cpuid(LEAF_BRAND_FIRST + i, &raw->brand[4 * i]);
        raw->has_brand = true;
    }

    if (raw->has_leaf1 && (raw->leaf1[2] & ECX_HYPERVISOR)) {
        cpuid(LEAF_HYPERVISOR, raw->hv);
        raw->has_hv = true;
    }
}

uint32_t cpu_read_cr0(void)
{
    uint32_t cr0;
    __asm__ volatile ("mov %%cr0, %0" : "=r"(cr0));
    return cr0;
}
