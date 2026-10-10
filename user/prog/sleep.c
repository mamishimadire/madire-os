/* sleep (0.10).
 *   arg 0: nanosleep for 100 ms; exits with its result (0). The kernel
 *          measures the ticks the process slept.
 *   arg 1: argument errors; exits with the number of wrong results.
 *   arg 2: nanosleep for 10 s (0.11: a kill or Ctrl+C wakes it at once). */
#include "user/ulib.h"

int main(long arg)
{
    if (arg == 2) {
        struct timespec t = { 10, 0 };
        return (int)sys_nanosleep(&t, 0);
    }
    if (arg == 0) {
        struct timespec t = { 0, 100000000 };
        return (int)sys_nanosleep(&t, 0);
    }
    int bad = 0;
    struct timespec t = { 0, 1000000000 };              /* tv_nsec too large */
    bad += u_expect("sleep: nsec 1e9: ", sys_nanosleep(&t, 0), -EINVAL);
    t.tv_nsec = -1;
    bad += u_expect("sleep: nsec -1: ", sys_nanosleep(&t, 0), -EINVAL);
    t.tv_sec = -1;
    t.tv_nsec = 0;
    bad += u_expect("sleep: sec -1: ", sys_nanosleep(&t, 0), -EINVAL);
    bad += u_expect("sleep: req NULL: ", sys_nanosleep(0, 0), -EFAULT);
    bad += u_expect("sleep: req in the kernel: ",
                    sys_nanosleep((const struct timespec *)K_TEXT, 0), -EFAULT);
    bad += u_expect("sleep: req across the top: ",
                    sys_nanosleep((const struct timespec *)(USER_TOP - 8), 0), -EFAULT);
    t.tv_sec = 0;
    t.tv_nsec = 0;                                      /* 0: a yield */
    bad += u_expect("sleep: zero: ", sys_nanosleep(&t, 0), 0);
    /* rem is never written (nothing interrupts a sleep), so it is not checked. */
    bad += u_expect("sleep: rem in the kernel: ",
                    sys_nanosleep(&t, (struct timespec *)K_TEXT), 0);
    u_put_num("sleep: wrong results: ", bad);
    return bad;
}
