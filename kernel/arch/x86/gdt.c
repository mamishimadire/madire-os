/*
 * Global Descriptor Table.
 *
 * GRUB leaves its own GDT loaded, and the Multiboot spec says the kernel must
 * not rely on it. Madire OS uses a flat model: code and data segments both
 * cover all 4 GiB, so segmentation does no translation and memory protection
 * will come from paging (milestone 0.5). User-mode segments and the TSS are
 * added with user mode.
 */
#include <stdint.h>
#include "kernel/arch/x86/gdt.h"

struct gdt_entry {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_mid;
    uint8_t  access;
    uint8_t  granularity;     /* high nibble: flags, low nibble: limit 19:16 */
    uint8_t  base_high;
} __attribute__((packed));

struct gdt_ptr {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

#define GDT_ACCESS_KERNEL_CODE 0x9A   /* present, ring 0, code, readable */
#define GDT_ACCESS_KERNEL_DATA 0x92   /* present, ring 0, data, writable */
#define GDT_FLAGS_4K_32BIT     0xC0   /* 4 KiB granularity, 32-bit */

static struct gdt_entry gdt[3];
static struct gdt_ptr gdtr;

extern void gdt_flush(const struct gdt_ptr *gdtr);

static void gdt_set(int i, uint32_t base, uint32_t limit, uint8_t access, uint8_t flags)
{
    gdt[i].limit_low   = limit & 0xFFFF;
    gdt[i].base_low    = base & 0xFFFF;
    gdt[i].base_mid    = (base >> 16) & 0xFF;
    gdt[i].access      = access;
    gdt[i].granularity = ((limit >> 16) & 0x0F) | (flags & 0xF0);
    gdt[i].base_high   = (base >> 24) & 0xFF;
}

bool gdt_init(void)
{
    gdt_set(0, 0, 0, 0, 0);                                            /* null */
    gdt_set(1, 0, 0xFFFFF, GDT_ACCESS_KERNEL_CODE, GDT_FLAGS_4K_32BIT);
    gdt_set(2, 0, 0xFFFFF, GDT_ACCESS_KERNEL_DATA, GDT_FLAGS_4K_32BIT);

    gdtr.limit = sizeof(gdt) - 1;
    gdtr.base  = (uint32_t)(uintptr_t)&gdt;
    gdt_flush(&gdtr);

    /* Read back what the CPU is actually using. */
    struct gdt_ptr loaded;
    uint16_t cs, ds, ss;
    __asm__ volatile ("sgdt %0" : "=m"(loaded));
    __asm__ volatile ("mov %%cs, %0" : "=r"(cs));
    __asm__ volatile ("mov %%ds, %0" : "=r"(ds));
    __asm__ volatile ("mov %%ss, %0" : "=r"(ss));

    return loaded.base == gdtr.base && loaded.limit == gdtr.limit
        && cs == GDT_KERNEL_CODE && ds == GDT_KERNEL_DATA && ss == GDT_KERNEL_DATA;
}
