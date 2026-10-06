#ifndef MADIRE_ARCH_X86_GDT_DESC_H
#define MADIRE_ARCH_X86_GDT_DESC_H

/*
 * GDT descriptor values and decoding (0.10), pure: tests/unit/test_gdt010.c.
 *
 * Layout (selector = index * 8):
 *   0x00  null
 *   0x08  kernel code, 64-bit (L=1)            GDT_KERNEL_CODE
 *   0x10  kernel data                          GDT_KERNEL_DATA
 *   0x18  TSS, 16 bytes (entries 3 and 4)      GDT_TSS
 *   0x28  all zero: a placeholder, never loaded. SYSRET's selector
 *         arithmetic starts here (STAR[63:48]); a 32-bit SYSRET would load
 *         it, but Madire OS has no 32-bit user code. Loading it into a data
 *         segment register raises #GP(0x28): the `gpf` diagnostic relies on
 *         that (it is not a data or readable code segment).
 *   0x30  user data, ring 3                    used as 0x33 (RPL 3)
 *   0x38  user code, 64-bit, ring 3            used as 0x3B (RPL 3)
 *
 * SYSCALL loads CS = STAR[47:32] (0x08) and SS = that + 8 (0x10). A 64-bit
 * SYSRET would load CS = STAR[63:48] + 16 and SS = STAR[63:48] + 8, both
 * with RPL 3: 0x3B and 0x33, the selectors IRETQ uses for user mode.
 */

#include <stdbool.h>
#include <stdint.h>
#include "kernel/arch/x86/gdt.h"

#define GDT_ENTRIES             8u
#define GDT_USER32_PLACEHOLDER  0x28
#define GDT_USER_DATA           0x30
#define GDT_USER_CODE           0x38
#define GDT_RPL3                3u
#define GDT_USER_DATA_SEL       (GDT_USER_DATA | GDT_RPL3)     /* 0x33 */
#define GDT_USER_CODE_SEL       (GDT_USER_CODE | GDT_RPL3)     /* 0x3B */
#define GDT_LIMIT               (GDT_ENTRIES * 8u - 1)         /* 0x3F */

#define DESC_KERNEL_CODE 0x00AF9A000000FFFFull  /* present, ring 0, code (readable), L=1 */
#define DESC_KERNEL_DATA 0x00CF92000000FFFFull  /* present, ring 0, data, writable */
#define DESC_USER32_NONE 0x0000000000000000ull  /* the placeholder at 0x28 */
#define DESC_USER_DATA   0x00CFF2000000FFFFull  /* present, ring 3, data, writable */
#define DESC_USER_CODE   0x00AFFA000000FFFFull  /* present, ring 3, code (readable), L=1 */

static inline bool desc_present(uint64_t d)  { return (d >> 47) & 1; }
static inline unsigned desc_dpl(uint64_t d)  { return (unsigned)(d >> 45) & 3; }
static inline bool desc_s(uint64_t d)        { return (d >> 44) & 1; }   /* code/data, not system */
static inline bool desc_code(uint64_t d)     { return desc_s(d) && ((d >> 43) & 1); }
static inline bool desc_data(uint64_t d)     { return desc_s(d) && !((d >> 43) & 1); }
static inline bool desc_rw(uint64_t d)       { return (d >> 41) & 1; }   /* code: readable; data: writable */
static inline bool desc_long(uint64_t d)     { return (d >> 53) & 1; }
static inline bool desc_db(uint64_t d)       { return (d >> 54) & 1; }

/* True if a MOV into DS/ES/FS/GS of this descriptor (with a selector inside
 * the GDT's limit, CPL 0, RPL 0) is accepted: a data segment, or a readable
 * code segment, that is present. Anything else raises #GP(selector) (or
 * #NP for a not-present code/data segment). */
static inline bool desc_loads_into_ds(uint64_t d)
{
    return desc_present(d) && (desc_data(d) || (desc_code(d) && desc_rw(d)));
}

/* The STAR MSR (0xC0000081): SYSCALL from 0x08, SYSRET base 0x28 | 3. */
static inline uint64_t gdt_star(void)
{
    return ((uint64_t)(GDT_USER32_PLACEHOLDER | GDT_RPL3) << 48) | ((uint64_t)GDT_KERNEL_CODE << 32);
}

#endif
