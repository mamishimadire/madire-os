#ifndef MADIRE_KERNEL_H
#define MADIRE_KERNEL_H

#include <stdint.h>

#define MADIRE_OS_NAME    "Madire OS"
#define MADIRE_OS_VERSION "0.3"

void kernel_main(uint32_t magic, uint32_t mb_info);

#endif
