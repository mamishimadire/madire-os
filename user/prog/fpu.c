/* fpu (0.10): x87, MMX and SSE instructions. CR0.EM is set (no FPU state
 * is saved between processes yet), so each must raise an exception in
 * ring 3 (#NM for x87, #UD for MMX and SSE) and kill this process.
 * The only program allowed to contain these registers (see
 * scripts/user-images.sh).
 *   0 fld1 (x87)   1 emms (MMX)   2 xorps (SSE) */
#include "user/ulib.h"

int main(long arg)
{
    switch (arg) {
    case 0: __asm__ volatile ("fld1"); break;
    case 1: __asm__ volatile ("emms"); break;
    case 2: __asm__ volatile ("xorps %xmm0, %xmm0"); break;
    default: return 2;
    }
    u_put_num("fpu: INSTRUCTION RAN IN RING 3: ", arg);
    return 1;
}
