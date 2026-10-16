#ifndef MADIRE_MEMORY_RESERVE_H
#define MADIRE_MEMORY_RESERVE_H

/*
 * Builds the list of physical memory that must never be allocated: the low
 * 1 MiB, the kernel image, and every Multiboot structure the kernel reads
 * (info block, memory map, command line, modules and their names).
 *
 * Pure module: no hardware access, printing or panics, so it is unit-tested
 * on the host (tests/unit/test_reserve.c) with fake Multiboot data.
 */

#include <stdbool.h>
#include <stdint.h>
#include "kernel/multiboot.h"

/* 0.3 safety decision: nothing below 1 MiB is ever allocated. It holds the
 * real-mode IVT and BIOS data area, page 0, the EBDA, VGA memory
 * (0xA0000-0xBFFFF) and the BIOS ROM, and usually the boot loader's structures. */
#define RESERVE_LOW_MEMORY_END  0x100000u

#define RESERVE_STRING_MAX      4096u   /* longest command line / module name */
#define RESERVE_MAX_RANGES      32u

struct phys_range {
    uint64_t start;         /* inclusive */
    uint64_t end;           /* exclusive */
    const char *what;
};

/* Kernel extent from the linker script and boot.asm. */
struct kernel_layout {
    uint32_t kernel_start, kernel_end;
    uint32_t bss_start, bss_end;
    uint32_t stack_bottom, stack_top;
};

enum reserve_status {
    RESERVE_OK,
    RESERVE_ERR_FULL,           /* more ranges than the table holds */
    RESERVE_ERR_KERNEL,         /* kernel range empty or not page-aligned */
    RESERVE_ERR_STACK,          /* boot stack outside .bss, or .bss outside kernel */
    RESERVE_ERR_BAD_POINTER,    /* null/wrapping Multiboot pointer or range */
    RESERVE_ERR_BAD_STRING,     /* string not terminated within RESERVE_STRING_MAX */
};

enum reserve_status reserve_build(const struct multiboot_info *mbi,
                                  const struct kernel_layout *kernel,
                                  struct phys_range *out, unsigned max,
                                  unsigned *count);

/* True if addr lies inside any of the ranges. */
bool reserve_contains(const struct phys_range *ranges, unsigned count, uint64_t addr);

const char *reserve_status_str(enum reserve_status status);

#endif
