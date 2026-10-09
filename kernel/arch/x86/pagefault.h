#ifndef MADIRE_ARCH_X86_PAGEFAULT_H
#define MADIRE_ARCH_X86_PAGEFAULT_H

/*
 * Page-fault (#PF, exception 14) error-code decoding (pure, header only;
 * tests/unit/test_nx.c). The faulting address itself is in CR2.
 *
 *   bit 0  0 = page not present, 1 = protection violation
 *   bit 1  0 = read, 1 = write
 *   bit 2  0 = kernel mode, 1 = user mode
 *   bit 3  a reserved bit was set in a paging entry
 *   bit 4  instruction fetch
 *
 * A protection violation on an instruction fetch is an NX violation: the
 * page is present, but its entry marks it no-execute (only possible while
 * EFER.NXE is set).
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "kernel/arch/x86/cpuid.h"

#define PF_PRESENT  0x01u
#define PF_WRITE    0x02u
#define PF_USER     0x04u
#define PF_RESERVED 0x08u
#define PF_FETCH    0x10u

static inline bool pf_nx_violation(uint32_t error_code)
{
    return (error_code & PF_PRESENT) && (error_code & PF_FETCH);
}

static inline const char *pf_cause_str(uint32_t error_code)
{
    return (error_code & PF_PRESENT) ? "protection violation" : "not present";
}

static inline const char *pf_access_str(uint32_t error_code)
{
    return (error_code & PF_WRITE) ? "write" : "read";
}

static inline const char *pf_mode_str(uint32_t error_code)
{
    return (error_code & PF_USER) ? "user mode" : "kernel mode";
}

/* The optional parts after the mode, each starting with ", ". */
static inline const char *pf_reserved_str(uint32_t error_code)
{
    return (error_code & PF_RESERVED) ? ", reserved bit set" : "";
}

static inline const char *pf_fetch_str(uint32_t error_code)
{
    if (!(error_code & PF_FETCH))
        return "";
    return pf_nx_violation(error_code) ? ", instruction fetch (NX violation)"
                                       : ", instruction fetch";
}

/*
 * 0.10: SMEP and SMAP (tests/unit/test_pfcause010.c). The error code alone
 * cannot name them: a kernel instruction fetch from a user page under SMEP
 * reads like an NX violation (present + fetch), and a kernel read or write
 * of a user page under SMAP like any protection violation. The kernel's
 * own tables map nothing in the lower half, so a present lower-half page
 * is a process's user page (uvm_core.c): a kernel-mode protection
 * violation there is SMEP for a fetch (when CR4.SMEP is set) and SMAP for
 * a read or write (when CR4.SMAP is set and RFLAGS.AC was clear). NULL:
 * neither; the fault is decoded as before.
 */
#define PF_USER_LIMIT 0x0000800000000000ull /* first address above the lower half */
#define PF_RFLAGS_AC  0x40000ull

static inline const char *pf_smx_str(uint32_t error_code, uint64_t cr2, uint64_t cr4,
                                     uint64_t rflags)
{
    if (!(error_code & PF_PRESENT) || (error_code & (PF_USER | PF_RESERVED)) ||
        cr2 >= PF_USER_LIMIT)
        return NULL;
    if (error_code & PF_FETCH)
        return (cr4 & CR4_SMEP) ? "SMEP violation (the kernel executed a user page)" : NULL;
    if (!(cr4 & CR4_SMAP) || (rflags & PF_RFLAGS_AC))
        return NULL;
    return (error_code & PF_WRITE) ? "SMAP violation (the kernel wrote a user page, AC=0)"
                                   : "SMAP violation (the kernel read a user page, AC=0)";
}

#endif
