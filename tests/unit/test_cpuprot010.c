/* Unit tests for the 0.10 SMEP / SMAP decision from raw CPUID values
 * (cpuid_leaf7_has, kernel/arch/x86/cpuid.h). */
#include <stdint.h>
#include "kernel/arch/x86/cpuid.h"
#include "tests/unit/unit.h"

static void test_bits(void)
{
    CHECK_EQ(CPUID_7_EBX_SMEP, 1u << 7);
    CHECK_EQ(CPUID_7_EBX_SMAP, 1u << 20);
    CHECK_EQ(CR4_SMEP, 1u << 20);
    CHECK_EQ(CR4_SMAP, 1u << 21);
    CHECK_EQ(CR0_EM, 1u << 2);
}

static void test_leaf7(void)
{
    uint32_t both = CPUID_7_EBX_SMEP | CPUID_7_EBX_SMAP;
    CHECK(cpuid_leaf7_has(7, both, CPUID_7_EBX_SMEP));
    CHECK(cpuid_leaf7_has(0x16, both, CPUID_7_EBX_SMAP));
    CHECK(!cpuid_leaf7_has(6, both, CPUID_7_EBX_SMEP));        /* leaf 7 not listed */
    CHECK(!cpuid_leaf7_has(0, both, CPUID_7_EBX_SMAP));
    CHECK(!cpuid_leaf7_has(7, CPUID_7_EBX_SMEP, CPUID_7_EBX_SMAP));
    CHECK(cpuid_leaf7_has(7, CPUID_7_EBX_SMEP, CPUID_7_EBX_SMEP));
    CHECK(!cpuid_leaf7_has(7, 0, CPUID_7_EBX_SMEP));
    CHECK(!cpuid_leaf7_has(7, ~both, CPUID_7_EBX_SMEP));        /* every other bit set */
    CHECK(!cpuid_leaf7_has(7, ~both, CPUID_7_EBX_SMAP));
}

int main(void)
{
    RUN(test_bits);
    RUN(test_leaf7);
    return unit_report("cpuprot010");
}
