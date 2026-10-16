/*
 * Global Descriptor Table and Task State Segment (x86-64).
 *
 * The boot loader's GDT (and the boot stub's temporary one) are replaced. In long mode
 * segmentation does no translation: code and data descriptors only select
 * 64-bit mode and the privilege level. Layout: gdt_desc.h (0.10: user data
 * and code segments, and a zero placeholder at 0x28).
 *
 * The TSS holds:
 *   - RSP0, the stack the CPU switches to when an interrupt or exception
 *     arrives in ring 3: the running thread's kernel stack top, set on
 *     every thread switch (gdt_set_rsp0, kernel/proc/thread.c);
 *   - IST1, a separate stack for double faults, so a double fault caused by
 *     a kernel stack overflow can still be reported;
 *   - IST2 (0.10), a separate stack for NMI and machine checks, which can
 *     arrive at any instruction, including the first ones of the system-call
 *     entry, which still run on the program's stack.
 */
#include <stddef.h>
#include <stdint.h>
#include "kernel/arch/x86/gdt.h"
#include "kernel/arch/x86/gdt_desc.h"

struct tss64 {
    uint32_t reserved0;
    uint64_t rsp[3];            /* rsp[0]: stack for entries from ring 3 */
    uint64_t reserved1;
    uint64_t ist[7];            /* IST1..IST7 */
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iomap_base;        /* = sizeof: no I/O permission bitmap */
} __attribute__((packed));

struct gdt_ptr {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

#define TSS_TYPE_AVAIL   0x89u                  /* present, 64-bit TSS (available) */
#define IST2_STACK_SIZE  8192u

static uint64_t gdt[GDT_ENTRIES] __attribute__((aligned(16)));
static struct gdt_ptr gdtr;
static struct tss64 tss __attribute__((aligned(16)));
/* NMI / machine-check stack. No guard page: those handlers only show the
 * crash screen. */
static uint8_t ist2_stack[IST2_STACK_SIZE] __attribute__((aligned(16)));

extern char ist1_stack_top[];
extern char stack_top[];                        /* boot/boot.asm: thread 0's stack */
extern void gdt_flush(const struct gdt_ptr *gdtr);

static void set_tss_descriptor(int i, uint64_t base, uint32_t limit)
{
    gdt[i] = (limit & 0xFFFFull)
           | ((base & 0xFFFFFFull) << 16)
           | ((uint64_t)TSS_TYPE_AVAIL << 40)
           | ((uint64_t)((limit >> 16) & 0xF) << 48)
           | (((base >> 24) & 0xFFull) << 56);
    gdt[i + 1] = base >> 32;
}

uint64_t gdt_descriptor(uint16_t selector)
{
    struct gdt_ptr loaded;
    __asm__ volatile ("sgdt %0" : "=m"(loaded));
    uint16_t offset = selector & ~7u;
    if ((uint32_t)offset + 7 > loaded.limit)
        return 0;
    return *(const uint64_t *)(uintptr_t)(loaded.base + offset);
}

bool gdt_init(void)
{
    tss.ist[0] = (uint64_t)(uintptr_t)ist1_stack_top;
    tss.ist[1] = (uint64_t)(uintptr_t)(ist2_stack + IST2_STACK_SIZE);
    tss.rsp[0] = (uint64_t)(uintptr_t)stack_top;
    tss.iomap_base = sizeof(tss);       /* no I/O bitmap: every port is closed to ring 3 */

    gdt[0] = 0;
    gdt[1] = DESC_KERNEL_CODE;
    gdt[2] = DESC_KERNEL_DATA;
    set_tss_descriptor(3, (uint64_t)(uintptr_t)&tss, sizeof(tss) - 1);
    gdt[5] = DESC_USER32_NONE;
    gdt[6] = DESC_USER_DATA;
    gdt[7] = DESC_USER_CODE;

    gdtr.limit = sizeof(gdt) - 1;
    gdtr.base  = (uint64_t)(uintptr_t)&gdt;
    gdt_flush(&gdtr);
    __asm__ volatile ("ltr %w0" : : "r"((uint16_t)GDT_TSS));

    /* Read back what the CPU is actually using. */
    struct gdt_ptr loaded;
    uint16_t cs, ds, ss, tr;
    __asm__ volatile ("sgdt %0" : "=m"(loaded));
    __asm__ volatile ("mov %%cs, %0" : "=r"(cs));
    __asm__ volatile ("mov %%ds, %0" : "=r"(ds));
    __asm__ volatile ("mov %%ss, %0" : "=r"(ss));
    __asm__ volatile ("str %0" : "=r"(tr));
    bool code_64 = (gdt_descriptor(cs) >> 53) & 1;      /* L bit */

    return loaded.base == gdtr.base && loaded.limit == gdtr.limit && loaded.limit == GDT_LIMIT
        && cs == GDT_KERNEL_CODE && ds == GDT_KERNEL_DATA && ss == GDT_KERNEL_DATA
        && tr == GDT_TSS && code_64;
}

void gdt_set_rsp0(uint64_t rsp0)
{
    tss.rsp[0] = rsp0;
}

/* 0.10: the SYSCALL entry (kernel/proc/syscall_entry.asm) loads its stack
 * from the TSS's RSP0 through this pointer: it has no free register. (The
 * TSS is packed, so the address is formed from the structure's own.) */
void *const gdt_rsp0_slot = (uint8_t *)&tss + offsetof(struct tss64, rsp);

uint64_t gdt_rsp0(void)
{
    return tss.rsp[0];
}

uint64_t gdt_ist(unsigned n)
{
    return (n >= 1 && n <= 7) ? tss.ist[n - 1] : 0;
}
