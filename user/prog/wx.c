/* wx (0.10): write or execute permission broken from inside a program.
 * Each must raise a page fault that kills this process.
 *   arg 0: writes into its own code (read + execute)
 *   arg 1: jumps into its own data (read + write, no-execute; needs NX) */
#include "user/ulib.h"

static volatile uint8_t code_in_data[16] = { 0xC3 };   /* ret */

int main(long arg)
{
    if (arg == 1)
        ((void (*)(void))(uintptr_t)code_in_data)();
    else
        *(volatile uint8_t *)(uintptr_t)main = 0x90;
    u_puts("wx: WRITE OR EXECUTE PERMISSION NOT ENFORCED\n");
    return 1;
}
