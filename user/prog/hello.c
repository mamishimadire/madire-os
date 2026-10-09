/* hello (0.10): output through write, then exit status 0.
 *   arg 0: one line, "Hello from user mode"
 *   arg 1: control characters and a fake kernel log line: each byte
 *          outside 0x20-0x7E but '\n' must reach the log as '?', and every
 *          line carries the "[pid N] " prefix, so nothing can pass for a
 *          kernel message
 *   arg 2: 5000 bytes in one call: the kernel takes 4096 (a short count) */
#include "user/ulib.h"

static const char tricky[] =
    "A\x1b[2JB\x07" "C\rD\tE\x7f" "F\xff" "G\n"
    "Process 99 'spin' ended by CPU exception 13 (#GP General Protection Fault); "
    "the kernel continues\n";

static char big[5000];

int main(long arg)
{
    if (arg == 1)
        return sys_write(1, tricky, sizeof(tricky) - 1) == (long)(sizeof(tricky) - 1) ? 0 : 1;
    if (arg == 2) {
        for (unsigned i = 0; i < sizeof(big); i++)
            big[i] = (char)('a' + i % 26);
        return sys_write(2, big, sizeof(big)) == 4096 ? 0 : 1;
    }
    return u_puts("Hello from user mode\n") == 21 ? 0 : 1;
}
