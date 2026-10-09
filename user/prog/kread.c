/* kread (0.10): reads a kernel address; the page fault must kill this
 * process, nothing else. Returning at all means the read succeeded.
 *   arg 0: kernel image   1: the PML4 (recursive slot)   2: kernel heap
 *   arg 3: VGA text buffer in the physical view */
#include "user/ulib.h"

static const uint64_t targets[] = { K_TEXT, K_PML4, K_HEAP, K_PHYS_VGA };

int main(long arg)
{
    uint64_t a = targets[(uint64_t)arg < 4 ? arg : 0];
    volatile uint64_t v = *(volatile const uint64_t *)a;
    (void)v;
    u_puts("kread: KERNEL MEMORY WAS READABLE\n");
    return 1;
}
