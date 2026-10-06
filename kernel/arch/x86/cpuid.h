#ifndef MADIRE_ARCH_X86_CPUID_H
#define MADIRE_ARCH_X86_CPUID_H

/*
 * Safe access to the CPUID instruction, CR0/CR4 and EFER. Normal context only.
 *
 * CPUID is never assumed: cpuid_supported() checks that EFLAGS bit 21 (ID)
 * can be changed, which only CPUs with CPUID allow. Executing `cpuid` on a
 * CPU without it would raise an invalid-opcode exception.
 */

#include <stdbool.h>
#include <stdint.h>
#include "kernel/shell/cpuinfo.h"

bool cpuid_supported(void);

/* Fills raw: availability first, then leaf 0, leaf 1 (if max basic >= 1),
 * the brand leaves (if max extended >= 0x80000004) and the hypervisor leaf
 * (if leaf 1 reports a hypervisor). Never runs a leaf the CPU doesn't list. */
void cpuid_read_all(struct cpuid_raw *raw);

uint32_t cpu_read_cr0(void);
uint64_t cpu_read_cr4(void);
/* The EFER MSR. It exists on every CPU that can run this (64-bit) kernel. */
uint64_t cpu_read_efer(void);

#define CR0_PE 0x00000001u      /* protected mode */
#define CR0_EM 0x00000004u      /* 0.10: x87/MMX/SSE instructions fault (#NM / #UD) */
#define CR0_WP 0x00010000u      /* write protect (supervisor writes obey read-only) */
#define CR0_PG 0x80000000u      /* paging */
#define CR4_PAE 0x00000020u     /* physical address extension (required by long mode) */
#define CR4_SMEP (1u << 20)     /* 0.10: the kernel cannot execute user pages */
#define CR4_SMAP (1u << 21)     /* 0.10: the kernel cannot touch user pages unless AC=1 */

#define MSR_EFER  0xC0000080u
#define EFER_LME  (1u << 8)     /* long mode enable */
#define EFER_LMA  (1u << 10)    /* long mode active (set by the CPU) */
#define EFER_NXE  (1u << 11)    /* no-execute enable */

#define CPUID_EXT_MAX        0x80000000u
#define CPUID_EXT_FEATURES   0x80000001u
#define CPUID_EXT_EDX_NX     (1u << 20)     /* 0x80000001 EDX: no-execute (XD) */

/*
 * NX decision from raw CPUID values (pure; tests/unit/test_nx.c): the CPU
 * supports no-execute pages only if it lists the extended-features leaf and
 * that leaf's EDX has bit 20. ext_max is CPUID 0x80000000 EAX; a value
 * outside the extended range means the CPU has no extended leaves at all.
 */
static inline bool cpuid_nx_supported(uint32_t ext_max, uint32_t ext_features_edx)
{
    if (ext_max < CPUID_EXT_FEATURES || ext_max > CPUID_EXT_MAX + 0xFFFFu)
        return false;
    return (ext_features_edx & CPUID_EXT_EDX_NX) != 0;
}

#define CPUID_STRUCT_FEATURES 7u               /* leaf 7, subleaf 0 */
#define CPUID_7_EBX_SMEP      (1u << 7)
#define CPUID_7_EBX_SMAP      (1u << 20)

/*
 * 0.10: SMEP / SMAP from raw CPUID values (pure; tests/unit/test_cpuprot010.c):
 * the CPU must list leaf 7 (max_basic = CPUID 0 EAX) and leaf 7's EBX must
 * have the bit.
 */
static inline bool cpuid_leaf7_has(uint32_t max_basic, uint32_t leaf7_ebx, uint32_t bit)
{
    return max_basic >= CPUID_STRUCT_FEATURES && (leaf7_ebx & bit) != 0;
}

/* CPUID 0 EAX and leaf 7's EBX (0 if the CPU lists no leaf 7). */
void cpu_leaf7(uint32_t *max_basic, uint32_t *leaf7_ebx);

void cpu_write_cr0(uint32_t cr0);
void cpu_write_cr4(uint64_t cr4);

/* CPUID says the CPU supports no-execute pages (never runs a leaf the CPU
 * doesn't list). */
bool cpu_nx_supported(void);

/* Sets EFER.NXE (read-modify-write, no other bit touched) and reads EFER
 * back. Only call when cpu_nx_supported(). true = NXE is now set. */
bool cpu_enable_nx(void);

#endif
