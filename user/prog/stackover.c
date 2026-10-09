/* stackover (0.10): unbounded recursion runs off the bottom of the 32 KiB
 * user stack into unmapped memory; the page fault must kill this process. */
#include "user/ulib.h"

static uint64_t deeper(uint64_t n)
{
    volatile uint64_t frame[64];                /* 512 bytes per call */
    if (n == UINT64_MAX)                        /* never: the stack ends long before */
        return 0;
    frame[0] = n;
    frame[63] = deeper(n + 1);
    return frame[0] + frame[63];
}

int main(long arg)
{
    (void)arg;
    deeper(0);
    u_puts("stackover: RECURSION ENDED\n");
    return 1;
}
