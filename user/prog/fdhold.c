/* fdhold (1.0 stage 4, test=fdkill): ends while holding descriptors, so
 * the kernel's teardown must close them.
 *   arg 0: fills its descriptor table with dups of fd 1 (to 16), then spins
 *          until it is killed
 *   arg 1: fills it, then reads address 0 (a page fault ends it)
 *   arg 2: fills it, then exits without closing any
 *   arg 3: one 48 KiB read from fd 3 (a file the kernel gave it); the kernel
 *          kills it during the read
 *   arg 4: spins (killed before its first entry to ring 3, it never runs)
 * Exit status 1: the table did not fill up to EMFILE. */
#include "user/ulib.h"

static uint8_t buf[48 * 1024];

/* dup until the table is full; -1 unless the last answer was EMFILE. */
static int fill(void)
{
    long r;
    int n = 0;
    while ((r = sys_dup(1)) >= 0)
        n++;
    return r == -EMFILE ? n : -1;
}

int main(int argc, char **argv)
{
    long arg = u_arg(argc, argv);
    volatile uint64_t n = 0;
    if (arg <= 2) {
        if (fill() < 0)
            return 1;
        if (arg == 1)
            (void)*(volatile uint64_t *)0;
        if (arg != 0)
            return 0;
    }
    if (arg == 3)
        return sys_read(3, buf, sizeof(buf)) == (long)sizeof(buf) ? 2 : 3;
    for (;;)
        n++;
}
