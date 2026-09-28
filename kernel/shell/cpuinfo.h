#ifndef MADIRE_SHELL_CPUINFO_H
#define MADIRE_SHELL_CPUINFO_H

/*
 * Decoding of raw CPUID register values into readable processor
 * information. Pure module: the `cpuid` instruction itself lives in
 * kernel/arch/x86/cpuid.c; this file only interprets register values, so
 * it is unit-tested on the host with synthetic values
 * (tests/unit/test_cpuinfo.c).
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Register order in every array: eax, ebx, ecx, edx. */
struct cpuid_raw {
    bool available;         /* CPUID instruction exists (EFLAGS.ID toggles) */
    uint32_t max_basic;     /* leaf 0 eax */
    uint32_t max_ext;       /* leaf 0x80000000 eax (0 if not read) */
    uint32_t leaf0[4];
    bool has_leaf1;
    uint32_t leaf1[4];
    bool has_brand;         /* leaves 0x80000002-0x80000004 read */
    uint32_t brand[12];
    bool has_hv;            /* leaf 0x40000000 read (hypervisor bit set) */
    uint32_t hv[4];
};

struct cpu_info {
    bool available;
    char vendor[13];
    bool has_signature;             /* family / model / stepping / features valid */
    uint32_t family, model, stepping;
    uint32_t edx_features, ecx_features;
    char brand[49];                 /* "" if not reported */
    char hypervisor[13];            /* "" if not reported */
};

void cpuinfo_decode(const struct cpuid_raw *raw, struct cpu_info *out);

/* Space-separated names of the notable feature flags, e.g.
 * "fpu tsc pae apic sse sse2 hypervisor"; "none" if none are set.
 * Always NUL-terminated (cut off if size is too small). Returns length. */
size_t cpuinfo_features(const struct cpu_info *c, char *buf, size_t size);

#endif
