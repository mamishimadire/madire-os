/*
 * Interrupt Descriptor Table (x86-64: 16-byte gates).
 *
 * Vectors 0-31 are CPU exceptions, 32-47 are the remapped PIC IRQs. Every one
 * of those points at a stub in isr.asm. Vectors 48-255 are left not-present:
 * reaching one raises a General Protection Fault, which is reported.
 *
 * Every gate is an interrupt gate (IF cleared on entry) of privilege 0, so
 * a program in ring 3 cannot raise any vector with `int n` (that is #GP).
 * The double fault (vector 8) runs on its own stack, IST1 in the TSS
 * (gdt.c), so it can be reported even when the kernel stack itself is what
 * failed. 0.10: NMI (2) and machine check (18) run on IST2, because they
 * can arrive while RSP still holds a program's stack (the first
 * instructions of the system-call entry).
 */
#include <stdint.h>
#include "kernel/arch/x86/gdt.h"
#include "kernel/arch/x86/idt.h"

struct idt_entry {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t  ist;               /* bits 0-2: IST index, 0 = current stack */
    uint8_t  type_attr;
    uint16_t offset_mid;
    uint32_t offset_high;
    uint32_t reserved;
} __attribute__((packed));

struct idt_ptr {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

#define IDT_INTERRUPT_GATE 0x8E   /* present, ring 0, 64-bit interrupt gate */
#define VECTOR_NMI          2
#define VECTOR_DOUBLE_FAULT 8
#define VECTOR_MACHINE_CHECK 18

static struct idt_entry idt[IDT_ENTRIES] __attribute__((aligned(16)));
static struct idt_ptr idtr;

extern const uint64_t isr_stub_table[IDT_HANDLED_VECTORS];

static void idt_set_gate(int vector, uint64_t handler, uint8_t ist)
{
    idt[vector].offset_low  = handler & 0xFFFF;
    idt[vector].selector    = GDT_KERNEL_CODE;
    idt[vector].ist         = ist;
    idt[vector].type_attr   = IDT_INTERRUPT_GATE;
    idt[vector].offset_mid  = (handler >> 16) & 0xFFFF;
    idt[vector].offset_high = (uint32_t)(handler >> 32);
    idt[vector].reserved    = 0;
}

bool idt_init(void)
{
    for (int i = 0; i < IDT_HANDLED_VECTORS; i++)
        idt_set_gate(i, isr_stub_table[i],
                     i == VECTOR_DOUBLE_FAULT ? 1
                     : (i == VECTOR_NMI || i == VECTOR_MACHINE_CHECK) ? 2 : 0);

    idtr.limit = sizeof(idt) - 1;
    idtr.base  = (uint64_t)(uintptr_t)&idt;
    __asm__ volatile ("lidt %0" : : "m"(idtr));

    struct idt_ptr loaded;
    __asm__ volatile ("sidt %0" : "=m"(loaded));
    return loaded.base == idtr.base && loaded.limit == idtr.limit;
}

int idt_gate_ist(int vector)
{
    struct idt_ptr loaded;
    __asm__ volatile ("sidt %0" : "=m"(loaded));
    if (vector < 0 || (uint32_t)(vector + 1) * sizeof(struct idt_entry) - 1 > loaded.limit)
        return -1;
    const struct idt_entry *e = (const struct idt_entry *)(uintptr_t)loaded.base + vector;
    return e->ist & 7;
}
