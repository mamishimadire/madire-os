/* getpid (0.10): prints its process ID and exits with it (& 0xFF), so the
 * kernel can compare both with the ID it gave the process. */
#include "user/ulib.h"

int main(long arg)
{
    (void)arg;
    long a = sys_getpid(), b = sys_getpid();
    if (a <= 0 || a != b)
        return 255;
    u_put_num("getpid: ", a);
    return (int)(a & 0xFF);
}
