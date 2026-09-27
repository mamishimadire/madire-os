#ifndef MADIRE_ARCH_X86_INTERRUPTS_H
#define MADIRE_ARCH_X86_INTERRUPTS_H

#include <stdint.h>

/* Stack layout built by isr.asm, lowest address first. */
struct interrupt_frame {
    uint32_t gs, fs, es, ds;                           /* pushed by isr_common */
    uint32_t edi, esi, ebp, esp_pusha, ebx, edx, ecx, eax;  /* pusha */
    uint32_t vector, error_code;                       /* pushed by the stub */
    uint32_t eip, cs, eflags;                          /* pushed by the CPU */
} __attribute__((packed));

#define EXCEPTION_BREAKPOINT 3
#define IRQ_BASE_VECTOR      32

void interrupt_dispatch(struct interrupt_frame *frame);

/* Number of breakpoint (int3) exceptions handled; used by the boot self-test. */
uint32_t interrupts_breakpoint_count(void);

static inline void interrupts_enable(void)
{
    __asm__ volatile ("sti");
}

#endif
