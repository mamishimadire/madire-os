/* ud2 (0.10): an invalid opcode (#UD) must kill this process. */
#include "user/ulib.h"

int main(void)
{
    __asm__ volatile ("ud2");
    u_puts("ud2: CONTINUED AFTER UD2\n");
    return 1;
}
