/* Stage 5 test-only program: fill the foreground output queue continuously. */
#include "user/ulib.h"

int main(long arg)
{
    (void)arg;
    static char block[4096];
    for (uint64_t i = 0; i < sizeof(block); i++)
        block[i] = 'F';
    u_puts("flood: ready\n");
    for (;;)
        sys_write(1, block, sizeof(block));
}
