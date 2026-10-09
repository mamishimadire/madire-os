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

/* 0.10, test=clac: count the interrupts whose handler runs with RFLAGS.AC
 * set (with_ac) or clear, between start and stop. */
void interrupts_ac_probe_start(void);
void interrupts_ac_probe_stop(uint32_t *with_ac, uint32_t *without_ac);

/* 0.10, test=userspin: while armed, every timer interrupt taken in ring 3
 * (CS RPL 3) is recorded: its frame, where the frame lies against the
 * TSS's RSP0 and the running thread's stack top, and whether the
 * preemption point switched away from it. */
struct user_irq_probe {
    uint32_t irqs;                  /* timer interrupts from ring 3 */
    uint32_t cs_bad, ss_bad;        /* CS != 0x3B, SS != 0x33 */
    uint32_t if_clear, iopl_set;    /* in the interrupted RFLAGS */
    uint32_t stack_bad;             /* frame not at RSP0 - its size, or RSP0 not the thread's top */
    uint32_t other_thread;          /* in a thread other than the first one seen */
    uint64_t rip_min, rip_max, rsp_min, rsp_max;
    uint32_t preempted;             /* of which the preemption point switched away */
    uint32_t after_preempt;         /* taken in ring 3 after the first such switch */
    int32_t tid;                    /* the thread of the first one */
    struct interrupt_frame first;   /* a copy of the first frame */
    uint64_t first_at, first_rsp0, first_top;   /* its address, RSP0, the thread's stack top */
};
void interrupts_user_probe_start(void);
void interrupts_user_probe_stop(struct user_irq_probe *out);

static inline void interrupts_enable(void)
{
    __asm__ volatile ("sti");
}

#endif
