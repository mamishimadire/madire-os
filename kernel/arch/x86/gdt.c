/*
 * Global Descriptor Table and Task State Segment (x86-64).
 *
 * GRUB's GDT (and the boot stub's temporary one) are replaced. In long mode
 * segmentation does no translation: code and data descriptors only select
 * 64-bit mode and ring 0. The TSS is used only for its Interrupt Stack
 * Table: IST1 is a separate stack for double faults, so a double fault
 * caused by a kernel stack overflow can still be reported. User-mode
 * segments come with user mode.
 */
#include <stdint.h>
#include "kernel/arch/x86/gdt.h"

struct tss64 {
    uint32_t reserved0;
    uint64_t rsp[3];            /* stacks for privilege changes (unused: ring 0 only) */
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

#define DESC_KERNEL_CODE 0x00AF9A000000FFFFull  /* present, ring 0, code, L=1 */
#define DESC_KERNEL_DATA 0x00CF92000000FFFFull  /* present, ring 0, data, writable */
#define TSS_TYPE_AVAIL   0x89u                  /* present, 64-bit TSS (available) */

static uint64_t gdt[5] __attribute__((aligned(16)));
static struct gdt_ptr gdtr;
static struct tss64 tss __attribute__((aligned(16)));

extern char ist1_stack_top[];
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
    tss.iomap_base = sizeof(tss);

    gdt[0] = 0;
    gdt[1] = DESC_KERNEL_CODE;
    gdt[2] = DESC_KERNEL_DATA;
    set_tss_descriptor(3, (uint64_t)(uintptr_t)&tss, sizeof(tss) - 1);

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

    return loaded.base == gdtr.base && loaded.limit == gdtr.limit
        && cs == GDT_KERNEL_CODE && ds == GDT_KERNEL_DATA && ss == GDT_KERNEL_DATA
        && tr == GDT_TSS && code_64;
}
