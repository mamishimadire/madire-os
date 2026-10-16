/* kwrite (0.10): writes a kernel address; the page fault must kill this
 * process, and the kernel must be unchanged. Returning means the write
 * happened. arg: as for kread. */
#include "user/ulib.h"

static const uint64_t targets[] = { K_TEXT, K_PML4, K_HEAP, K_PHYS_VGA };

int main(int argc, char **argv)
{
    long arg = u_arg(argc, argv);
    uint64_t a = targets[(uint64_t)arg < 4 ? arg : 0];
    *(volatile uint64_t *)a = 0x4D414449524521ull;     /* "MADIRE!" */
    u_puts("kwrite: KERNEL MEMORY WAS WRITABLE\n");
    return 1;
}
