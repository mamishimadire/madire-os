#ifndef MADIRE_ARCH_X86_CPUID_H
#define MADIRE_ARCH_X86_CPUID_H

/*
 * Safe access to the CPUID instruction and CR0. Normal context only.
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

#define CR0_PE 0x00000001u      /* protected mode */
#define CR0_PG 0x80000000u      /* paging */

#endif
