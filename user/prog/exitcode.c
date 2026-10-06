/* exitcode (0.10): ends with a known status.
 *   arg 0: exit(42)           arg 1: exit_group(43)
 *   arg 2: exit(256 + 7): the waiter sees 7 (status & 0xFF)
 *   arg 3: return 44 from main (crt0's exit) */
#include "user/ulib.h"

int main(long arg)
{
    if (arg == 1)
        sys_exit_group(43);
    if (arg == 2)
        sys_exit(256 + 7);
    if (arg == 3)
        return 44;
    sys_exit(42);
}
