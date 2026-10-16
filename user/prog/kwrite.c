/* kwrite (0.10): writes a kernel address; the page fault must kill this
 * process, and the kernel must be unchanged. Returning means the write
 * happened. arg: as for kread. */
#include "user/ulib.h"

static const uint64_t targets[] = { K_TEXT, K_PML4, K_HEAP, K_PHYS_VGA };

int main(int argc, char **argv)
{
    long arg = u_arg(argc, argv);
    uint64_t a = targets[(uint64_t)arg < 4 ? arg : 0];
    /* 1.0 stage 7: the store in assembly, so the bytes at the fault are
     * fixed (48 89 08, mov %rcx,(%rax); test=userkmem checks them) whatever
     * the compiler does around it. */
    __asm__ volatile ("mov %%rcx, (%%rax)" : : "a"(a), "c"(0x4D414449524521ull) /* "MADIRE!" */
                      : "memory");
    u_puts("kwrite: KERNEL MEMORY WAS WRITABLE\n");
    return 1;
}
