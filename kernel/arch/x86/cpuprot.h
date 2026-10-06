#ifndef MADIRE_ARCH_X86_CPUPROT_H
#define MADIRE_ARCH_X86_CPUPROT_H

/*
 * CPU protections for user mode (0.10), set once at boot, after paging:
 *   - SMEP (CR4 bit 20): the kernel cannot execute a user page;
 *   - SMAP (CR4 bit 21): the kernel cannot read or write a user page unless
 *     RFLAGS.AC is set (only copy_from_user / copy_to_user set it). While
 *     SMAP is on, every kernel entry (isr.asm, and the system-call entry)
 *     runs `clac` first, so a program's own AC flag never lifts SMAP in the
 *     kernel. Without SMAP no `clac` is executed: it would raise #UD;
 *   - CR0.EM: x87, MMX and SSE instructions fault (#NM / #UD). No FPU
 *     state is saved between processes yet, so a program using them is
 *     killed instead of seeing another program's registers. The kernel
 *     never uses them (-mgeneral-regs-only, checked by the build).
 * SMEP and SMAP are used when CPUID reports them; nothing else changes
 * without them.
 */

#include <stdbool.h>
#include <stdint.h>

struct cpuprot_status {
    bool smep_supported, smep_on;
    bool smap_supported, smap_on;
    bool em_on;                     /* CR0.EM read back as set */
};

void cpuprot_init(struct cpuprot_status *out);
const struct cpuprot_status *cpuprot_status(void);

/* Read by isr.asm at every interrupt and exception entry. */
extern volatile uint8_t cpuprot_smap_active;
/* Times isr.asm executed clac (0 while SMAP is off): the evidence that the
 * conditional clac runs exactly when it should. */
extern volatile uint64_t cpuprot_clac_count;

#endif
