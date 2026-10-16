/* fpu (0.10): x87, MMX and SSE instructions. CR0.EM is set (no FPU state
 * is saved between processes yet), so each must raise an exception in
 * ring 3 (#NM for x87, #UD for MMX and SSE) and kill this process.
 * The only program allowed to contain these registers (see
 * scripts/user-images.sh).
 *   0 fld1 (x87)   1 pxor %mm0, %mm0 (MMX)   2 xorps (SSE)
 * (0.10 stage 5a: arg 1 was emms, which QEMU 8.2.2 (TCG) does not fault
 * on with CR0.EM set; real CPUs do. pxor faults on both, as the CPU
 * manuals specify.) */
#include "user/ulib.h"

int main(int argc, char **argv)
{
    long arg = u_arg(argc, argv);
    switch (arg) {
    case 0: __asm__ volatile ("fld1"); break;
    case 1: __asm__ volatile ("pxor %mm0, %mm0"); break;
    case 2: __asm__ volatile ("xorps %xmm0, %xmm0"); break;
    default: return 2;
    }
    u_put_num("fpu: INSTRUCTION RAN IN RING 3: ", arg);
    return 1;
}
