/* isolate (0.10): two of these run at once (arg 1 and arg 2) at the same
 * virtual addresses. Each must find the initial value in its data, write
 * its own pattern into its data and stack, let the other run (sleeps and
 * yields), and find its own pattern still there. Exits with 0 if its
 * memory was its own throughout. */
#include "user/ulib.h"

static volatile uint64_t shared_va[64] = { 0x1122334455667788ull };

int main(int argc, char **argv)
{
    long arg = u_arg(argc, argv);
    if (arg != 1 && arg != 2)
        return 2;
    uint64_t mine = 0xA5A5000000000000ull | (uint64_t)arg;
    volatile uint64_t on_stack[64];
    int bad = 0;
    if (shared_va[0] != 0x1122334455667788ull)       /* the other's writes are not visible */
        bad++;
    struct timespec t = { 0, 20000000 };
    for (int round = 0; round < 10; round++) {
        for (int i = 0; i < 64; i++) {
            shared_va[i] = mine + (uint64_t)i;
            on_stack[i] = mine ^ (uint64_t)i;
        }
        sys_nanosleep(&t, 0);
        sys_sched_yield();
        for (int i = 0; i < 64; i++)
            if (shared_va[i] != mine + (uint64_t)i || on_stack[i] != (mine ^ (uint64_t)i))
                bad++;
    }
    u_put_num("isolate: foreign values seen: ", bad);
    return bad > 255 ? 255 : bad;
}
