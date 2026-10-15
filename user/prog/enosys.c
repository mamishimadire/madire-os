/* enosys (0.10): every number not implemented returns -ENOSYS (38): all of
 * 0-1023 except the ten (six until 1.0 stage 4 added read, close, dup and
 * dup2), values with high bits set, -1, the x32 range and the reserved
 * Madire range. Exits with the number of wrong results (at most 255). */
#include "user/ulib.h"

static int implemented(uint64_t nr)
{
    return nr == SYS_WRITE || nr == SYS_SCHED_YIELD || nr == SYS_NANOSLEEP ||
           nr == SYS_GETPID || nr == SYS_EXIT || nr == SYS_EXIT_GROUP ||
           nr == SYS_READ || nr == SYS_CLOSE || nr == SYS_DUP || nr == SYS_DUP2;
}

int main(long arg)
{
    (void)arg;
    int bad = 0, tried = 0;
    for (uint64_t nr = 0; nr < 1024; nr++) {
        if (implemented(nr))
            continue;
        tried++;
        if (syscall6(nr, 0, 0, 0, 0, 0, 0) != -ENOSYS)
            bad += u_expect("enosys: number ", (long)nr, -1);
    }
    static const uint64_t high[] = {
        0x40000000ull + SYS_WRITE, 0x40000000ull + SYS_EXIT, (1ull << 32) + SYS_WRITE,
        (1ull << 32) + SYS_EXIT, 0xFFFFFFFFull, UINT64_MAX, 0x8000000000000000ull + SYS_GETPID,
        0x10000000ull, 0x10000001ull, 0x1000FFFFull,
    };
    for (unsigned i = 0; i < sizeof(high) / sizeof(high[0]); i++) {
        tried++;
        if (syscall6(high[i], 1, 0, 0, 0, 0, 0) != -ENOSYS)
            bad += u_expect("enosys: high number index ", (long)i, -1);
    }
    u_put_num("enosys: numbers tried: ", tried);
    u_put_num("enosys: wrong results: ", bad);
    return bad > 255 ? 255 : bad;
}
