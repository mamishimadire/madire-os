/* Unit tests for the 0.7 no-execute (NX) logic: the CPUID capability
 * decision (kernel/arch/x86/cpuid.h), the NX page-table flag (pte.h) and
 * the page-fault error-code decoding (kernel/arch/x86/pagefault.h). */
#include <stdint.h>
#include <string.h>
#include "kernel/arch/x86/cpuid.h"
#include "kernel/arch/x86/pagefault.h"
#include "kernel/memory/pte.h"
#include "tests/unit/unit.h"

static void test_capability_detection(void)
{
    /* QEMU qemu64 and the MadireOS-Test VM (stage-1 probe values). */
    CHECK(cpuid_nx_supported(0x8000000Au, 0x2193FBFDu));
    CHECK(cpuid_nx_supported(0x80000008u, 0x28100000u));
    /* -cpu qemu64,-nx: same leaf, bit 20 clear. */
    CHECK(!cpuid_nx_supported(0x8000000Au, 0x2183FBFDu));
    /* Only bit 20 matters. */
    CHECK(cpuid_nx_supported(0x80000001u, 1u << 20));
    CHECK(!cpuid_nx_supported(0x80000001u, ~(1u << 20)));
    /* The extended-features leaf must be listed. */
    CHECK(!cpuid_nx_supported(0x80000000u, 1u << 20));        /* max leaf too low */
    CHECK(!cpuid_nx_supported(0x00000004u, 1u << 20));        /* no extended leaves (486) */
    CHECK(!cpuid_nx_supported(0x0000000Du, 0xFFFFFFFFu));     /* basic-leaf data echoed back */
    CHECK(!cpuid_nx_supported(0xFFFFFFFFu, 1u << 20));        /* garbage above the range */
    CHECK(cpuid_nx_supported(0x8000FFFFu, 1u << 20));         /* top of the range */
}

static void test_nx_flag_construction(void)
{
    CHECK_EQ(pte_nx_flag(true), PTE_NX);
    CHECK_EQ(pte_nx_flag(false), 0);
    CHECK_EQ(PTE_NX, 1ull << 63);                              /* the architectural bit */
    /* Set: every other part of the entry unchanged. */
    uint64_t e = pte_make(0x0000000123456000ull, PTE_PRESENT | PTE_WRITE | pte_nx_flag(true));
    CHECK(pte_nx(e) && pte_present(e) && pte_writable(e));
    CHECK_EQ(pte_frame(e), 0x0000000123456000ull);
    CHECK(!(e & PTE_USER));
    /* Clear. */
    e = pte_make(0x0000000123456000ull, PTE_PRESENT | pte_nx_flag(false));
    CHECK(!pte_nx(e) && pte_present(e) && !pte_writable(e));
    CHECK_EQ(e, 0x0000000123456001ull);
    /* Accessed/dirty bits set by the CPU don't disturb NX or the frame. */
    e = pte_make(0x0000000000200000ull, PTE_PRESENT | PTE_WRITE | PTE_NX) | PTE_ACCESSED | PTE_DIRTY;
    CHECK(pte_nx(e));
    CHECK_EQ(pte_frame(e), 0x0000000000200000ull);
    /* NX never leaks into the frame address (bits 12-51). */
    CHECK_EQ(PTE_ADDR_MASK & PTE_NX, 0);
    CHECK_EQ(pte_frame(PTE_NX | PTE_ADDR_MASK), PTE_ADDR_MASK);
}

static void test_page_fault_decoding(void)
{
    /* pfnull / pfunmapped: not present, read, kernel mode. */
    CHECK(strcmp(pf_cause_str(0x00), "not present") == 0);
    CHECK(strcmp(pf_access_str(0x00), "read") == 0);
    CHECK(strcmp(pf_mode_str(0x00), "kernel mode") == 0);
    CHECK(strcmp(pf_fetch_str(0x00), "") == 0);
    CHECK(!pf_nx_violation(0x00));
    /* pfreadonly: protection violation, write. */
    CHECK(strcmp(pf_cause_str(0x03), "protection violation") == 0);
    CHECK(strcmp(pf_access_str(0x03), "write") == 0);
    CHECK(!pf_nx_violation(0x03));
    CHECK(strcmp(pf_fetch_str(0x03), "") == 0);
    /* pfnx: present + instruction fetch = NX violation. */
    CHECK(pf_nx_violation(0x11));
    CHECK(strcmp(pf_cause_str(0x11), "protection violation") == 0);
    CHECK(strcmp(pf_access_str(0x11), "read") == 0);
    CHECK(strcmp(pf_fetch_str(0x11), ", instruction fetch (NX violation)") == 0);
    /* Fetch from a page that is not present: not an NX violation. */
    CHECK(!pf_nx_violation(0x10));
    CHECK(strcmp(pf_fetch_str(0x10), ", instruction fetch") == 0);
    /* User mode and reserved bits. */
    CHECK(strcmp(pf_mode_str(0x04), "user mode") == 0);
    CHECK(strcmp(pf_reserved_str(0x08), ", reserved bit set") == 0);
    CHECK(strcmp(pf_reserved_str(0x00), "") == 0);
    CHECK(pf_nx_violation(0x1F));                              /* every bit set */
}

int main(void)
{
    printf("nx unit tests\n");
    RUN(test_capability_detection);
    RUN(test_nx_flag_construction);
    RUN(test_page_fault_decoding);
    return unit_report("nx");
}
