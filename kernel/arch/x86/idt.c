/*
 * Interrupt Descriptor Table.
 *
 * Vectors 0-31 are CPU exceptions, 32-47 are the remapped PIC IRQs. Every one
 * of those points at a stub in isr.asm. Vectors 48-255 are left not-present:
 * reaching one raises a General Protection Fault, which is reported.
 */
#include <stdint.h>
#include "kernel/arch/x86/gdt.h"
#include "kernel/arch/x86/idt.h"

struct idt_entry {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t  zero;
    uint8_t  type_attr;
    uint16_t offset_high;
} __attribute__((packed));

struct idt_ptr {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

#define IDT_INTERRUPT_GATE 0x8E   /* present, ring 0, 32-bit interrupt gate */

static struct idt_entry idt[IDT_ENTRIES];
static struct idt_ptr idtr;

extern const uint32_t isr_stub_table[IDT_HANDLED_VECTORS];

static void idt_set_gate(int vector, uint32_t handler)
{
    idt[vector].offset_low  = handler & 0xFFFF;
    idt[vector].selector    = GDT_KERNEL_CODE;
    idt[vector].zero        = 0;
    idt[vector].type_attr   = IDT_INTERRUPT_GATE;
    idt[vector].offset_high = (handler >> 16) & 0xFFFF;
}

bool idt_init(void)
{
    for (int i = 0; i < IDT_HANDLED_VECTORS; i++)
        idt_set_gate(i, isr_stub_table[i]);

    idtr.limit = sizeof(idt) - 1;
    idtr.base  = (uint32_t)(uintptr_t)&idt;
    __asm__ volatile ("lidt %0" : : "m"(idtr));

    struct idt_ptr loaded;
    __asm__ volatile ("sidt %0" : "=m"(loaded));
    return loaded.base == idtr.base && loaded.limit == idtr.limit;
}
