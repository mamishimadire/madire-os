#ifndef MADIRE_PROC_USERTEST_H
#define MADIRE_PROC_USERTEST_H

/*
 * User-mode diagnostics (0.10). Stage 2 (CPU set-up, before any program
 * runs):
 *   test=rsp0  threads yield and are preempted; after every switch each one
 *              checks that the TSS's RSP0 is its own kernel stack top;
 *   test=clac  with RFLAGS.AC set, interrupts are taken: with SMAP on, the
 *              handler must see AC=0 (isr.asm's clac) and the clac count
 *              must grow; with SMAP off, clac must never run.
 * Each returns true on PASS and logs the details.
 */

#include <stdbool.h>

bool usertest_rsp0(void);
bool usertest_clac(void);

#endif
