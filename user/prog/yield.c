/* yield (0.10): 100 sched_yield calls, each must return 0; exits with the
 * number that did not. */
#include "user/ulib.h"

int main(void)
{
    int bad = 0;
    for (int i = 0; i < 100; i++)
        if (sys_sched_yield() != 0)
            bad++;
    u_put_num("yield: calls returning non-zero: ", bad);
    return bad;
}
