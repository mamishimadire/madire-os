/* spin (0.10): never ends by itself; preemption must still give every
 * other thread its turns, and a kill must end it.
 *   arg 0: a pure loop   1: getpid in a loop (system calls only)
 *   arg 2: sched_yield in a loop   3: 10 ms sleeps in a loop */
#include "user/ulib.h"

int main(long arg)
{
    volatile uint64_t n = 0;
    struct timespec t = { 0, 10000000 };
    for (;;) {
        n++;
        if (arg == 1)
            sys_getpid();
        else if (arg == 2)
            sys_sched_yield();
        else if (arg == 3)
            sys_nanosleep(&t, 0);
    }
}
