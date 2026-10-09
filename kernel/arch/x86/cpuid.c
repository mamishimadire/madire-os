/* CPUID, control-register and EFER access (see cpuid.h). */
#include "kernel/arch/x86/cpuid.h"

#define EFLAGS_ID        0x00200000u
#define LEAF_EXT_MAX     0x80000000u
#define LEAF_BRAND_FIRST 0x80000002u
#define LEAF_BRAND_LAST  0x80000004u
#define LEAF_HYPERVISOR  0x40000000u
#define ECX_HYPERVISOR   0x80000000u

bool cpuid_supported(void)
{
    uint64_t before, after;
    __asm__ volatile ("pushfq\n\t"
                      "popq %0\n\t"
                      "movq %0, %1\n\t"
                      "xorq %2, %1\n\t"
                      "pushq %1\n\t"
                      "popfq\n\t"
                      "pushfq\n\t"
                      "popq %1\n\t"
                      "pushq %0\n\t"             /* restore the original RFLAGS */
                      "popfq"
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
    uint64_t cr0;                           /* bits 32-63 are reserved (zero) */
    __asm__ volatile ("mov %%cr0, %0" : "=r"(cr0));
    return (uint32_t)cr0;
}

uint64_t cpu_read_cr4(void)
{
    uint64_t cr4;
    __asm__ volatile ("mov %%cr4, %0" : "=r"(cr4));
    return cr4;
}

uint64_t cpu_read_efer(void)
{
    uint32_t lo, hi;
    __asm__ volatile ("rdmsr" : "=a"(lo), "=d"(hi) : "c"(MSR_EFER));
    return ((uint64_t)hi << 32) | lo;
}

bool cpu_nx_supported(void)
{
    uint32_t ext[4], features[4] = { 0, 0, 0, 0 };
    if (!cpuid_supported())
        return false;
    cpuid(CPUID_EXT_MAX, ext);
    if (ext[0] >= CPUID_EXT_FEATURES && ext[0] <= CPUID_EXT_MAX + 0xFFFFu)
        cpuid(CPUID_EXT_FEATURES, features);
    return cpuid_nx_supported(ext[0], features[3]);
}

bool cpu_enable_nx(void)
{
    uint64_t efer = cpu_read_efer() | EFER_NXE;
    __asm__ volatile ("wrmsr" : : "c"(MSR_EFER), "a"((uint32_t)efer),
                      "d"((uint32_t)(efer >> 32)) : "memory");
    return (cpu_read_efer() & EFER_NXE) != 0;
}

void cpu_leaf7(uint32_t *max_basic, uint32_t *leaf7_ebx)
{
    uint32_t r[4] = { 0, 0, 0, 0 };
    *max_basic = 0;
    *leaf7_ebx = 0;
    if (!cpuid_supported())
        return;
    cpuid(0, r);
    *max_basic = r[0];
    if (r[0] >= CPUID_STRUCT_FEATURES) {
        cpuid(CPUID_STRUCT_FEATURES, r);            /* subleaf 0 (ECX = 0) */
        *leaf7_ebx = r[1];
    }
}

void cpu_write_cr0(uint32_t cr0)
{
    __asm__ volatile ("mov %0, %%cr0" : : "r"((uint64_t)cr0) : "memory");
}

void cpu_write_cr4(uint64_t cr4)
{
    __asm__ volatile ("mov %0, %%cr4" : : "r"(cr4) : "memory");
}
