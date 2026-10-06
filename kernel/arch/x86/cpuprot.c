/* CPU protections for user mode (see cpuprot.h). */
#include "kernel/arch/x86/cpuid.h"
#include "kernel/arch/x86/cpuprot.h"

volatile uint8_t cpuprot_smap_active;
volatile uint64_t cpuprot_clac_count;

static struct cpuprot_status status;

void cpuprot_init(struct cpuprot_status *out)
{
    uint32_t max_basic, ebx;
    cpu_leaf7(&max_basic, &ebx);
    status.smep_supported = cpuid_leaf7_has(max_basic, ebx, CPUID_7_EBX_SMEP);
    status.smap_supported = cpuid_leaf7_has(max_basic, ebx, CPUID_7_EBX_SMAP);

    uint64_t cr4 = cpu_read_cr4();
    if (status.smep_supported)
        cr4 |= CR4_SMEP;
    if (status.smap_supported)
        cr4 |= CR4_SMAP;
    if (status.smep_supported || status.smap_supported)
        cpu_write_cr4(cr4);
    cr4 = cpu_read_cr4();
    status.smep_on = (cr4 & CR4_SMEP) != 0;
    status.smap_on = (cr4 & CR4_SMAP) != 0;
    cpuprot_smap_active = status.smap_on;

    cpu_write_cr0(cpu_read_cr0() | CR0_EM);
    status.em_on = (cpu_read_cr0() & CR0_EM) != 0;
    if (out)
        *out = status;
}

const struct cpuprot_status *cpuprot_status(void)
{
    return &status;
}
