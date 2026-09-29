#ifndef MADIRE_ARCH_X86_INTERRUPTS_H
#define MADIRE_ARCH_X86_INTERRUPTS_H

#include <stdint.h>

/* Stack layout built by isr.asm (x86-64), lowest address first. */
struct interrupt_frame {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;     /* pushed by isr_common */
    uint64_t rdi, rsi, rbp, rbx, rdx, rcx, rax;
    uint64_t vector, error_code;                        /* pushed by the stub */
    uint64_t rip, cs, rflags, rsp, ss;                  /* pushed by the CPU */
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
