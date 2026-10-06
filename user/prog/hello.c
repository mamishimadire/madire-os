/* hello (0.10): one line through write, then exit status 0. */
#include "user/ulib.h"

int main(long arg)
{
    (void)arg;
    return u_puts("Hello from user mode\n") == 21 ? 0 : 1;
}
