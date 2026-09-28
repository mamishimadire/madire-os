#ifndef MADIRE_ARCH_X86_IRQ_H
#define MADIRE_ARCH_X86_IRQ_H

/*
 * Hardware IRQ layer on top of the 8259 PIC.
 *
 * Handlers run in interrupt context with interrupts disabled. They must be
 * short and may only update state, counters and queues: no VGA, no serial,
 * no allocation, no waiting, no panics for device problems.
 *
 * Enabling a line is always:
 *   irq_register() -> set up the device -> clear pending data ->
 *   irq_enable_line()  (refuses if no handler; verifies the mask)
 */

#include <stdbool.h>
#include <stdint.h>

#define IRQ_LINES 16
#define IRQ_TIMER 0
#define IRQ_KEYBOARD 1

typedef void (*irq_handler_t)(void);

enum irq_result {
    IRQ_OK,
    IRQ_ERR_RANGE,      /* irq >= 16 */
    IRQ_ERR_NULL,       /* no handler given */
    IRQ_ERR_BUSY,       /* line already has a handler */
    IRQ_ERR_NO_HANDLER, /* enabling a line with no handler */
    IRQ_ERR_MASK,       /* mask did not read back as expected */
};

/* One handler per line, registered once, never removed (static lifetime). */
enum irq_result irq_register(uint8_t irq, irq_handler_t handler);

/* Unmasks a line whose handler is registered, then verifies the mask. */
enum irq_result irq_enable_line(uint8_t irq);

/* Called by interrupt_dispatch() for vectors 32-47 only:
 * spurious check -> count -> handler (or count as unexpected) -> EOI. */
void irq_dispatch(uint8_t irq);

uint32_t irq_count(uint8_t irq);        /* delivered, excluding spurious */
uint32_t irq_spurious_count(void);
uint32_t irq_unexpected_count(void);    /* delivered on a line with no handler */
uint8_t  irq_last_unexpected(void);     /* line of the most recent unexpected IRQ */

const char *irq_result_str(enum irq_result result);

#define EFLAGS_IF 0x200u

/* Disable interrupts, returning the previous EFLAGS; restore re-enables
 * them only if they were enabled before. For short critical sections. */
static inline uint32_t irq_save(void)
{
    uint32_t flags;
    __asm__ volatile ("pushf\n\tpop %0\n\tcli" : "=r"(flags) : : "memory");
    return flags;
}

static inline void irq_restore(uint32_t flags)
{
    if (flags & EFLAGS_IF)
        __asm__ volatile ("sti" : : : "memory");
}

static inline bool irq_enabled(void)
{
    uint32_t flags;
    __asm__ volatile ("pushf\n\tpop %0" : "=r"(flags));
    return flags & EFLAGS_IF;
}

#endif
