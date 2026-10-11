#ifndef MADIRE_KERNEL_H
#define MADIRE_KERNEL_H

#include <stdint.h>

#define MADIRE_OS_NAME    "Madire OS"
#define MADIRE_OS_VERSION "0.12"
#define MADIRE_HOSTNAME   "madire"  /* 0.9.1: fixed until settings can be stored */

void kernel_main(uint32_t magic, uint32_t mb_info);

/* Kernel stack high-water mark (0.8): the stack's size, and the most bytes
 * it has held so far (see kernel/stackpeak.h). */
uint32_t kernel_stack_size(void);
uint32_t kernel_stack_peak(void);

#endif
