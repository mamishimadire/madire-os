/* kread (0.10): reads a kernel address; the page fault must kill this
 * process, nothing else. Returning at all means the read succeeded.
 *   arg 0: kernel image   1: the PML4 (recursive slot)   2: kernel heap
 *   arg 3: VGA text buffer in the physical view */
#include "user/ulib.h"

static const uint64_t targets[] = { K_TEXT, K_PML4, K_HEAP, K_PHYS_VGA };

int main(int argc, char **argv)
{
    long arg = u_arg(argc, argv);
    uint64_t a = targets[(uint64_t)arg < 4 ? arg : 0];
    /* 1.0 stage 7: the load in assembly, so the bytes at the fault are fixed
     * (48 8b 00, mov (%rax),%rax; test=userkmem checks them) whatever the
     * compiler does around it. */
    uint64_t v = a;
    __asm__ volatile ("mov (%%rax), %%rax" : "+a"(v) : : "memory");
    (void)v;
    u_puts("kread: KERNEL MEMORY WAS READABLE\n");
    return 1;
}
