/* nullptr (0.10): uses address 0, which is never mapped.
 *   arg 0: read   1: write   2: call (execute) */
#include "user/ulib.h"

int main(long arg)
{
    if (arg == 1)
        *(volatile uint64_t *)0 = 1;
    else if (arg == 2)
        ((void (*)(void))0)();
    else
        (void)*(volatile uint64_t *)0;
    u_puts("nullptr: ADDRESS 0 WAS USABLE\n");
    return 1;
}
