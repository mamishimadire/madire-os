/* int80 (0.10): software interrupts. Every gate is ring 0 only, so each
 * raises #GP in ring 3 and kills this process (int 0x80 is not a system
 * call in Madire OS: SYSCALL is).
 *   0 int 0x80   1 int3   2 int 0x20 (the timer's vector)   3 int 0x0e */
#include "user/ulib.h"

int main(int argc, char **argv)
{
    long arg = u_arg(argc, argv);
    switch (arg) {
    case 0: __asm__ volatile ("int $0x80"); break;
    case 1: __asm__ volatile ("int3"); break;
    case 2: __asm__ volatile ("int $0x20"); break;
    case 3: __asm__ volatile ("int $0x0e"); break;
    default: return 2;
    }
    u_put_num("int80: INTERRUPT RETURNED: ", arg);
    return 1;
}
