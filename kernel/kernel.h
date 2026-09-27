#ifndef MADIRE_KERNEL_H
#define MADIRE_KERNEL_H

#include <stdint.h>

#define MADIRE_OS_NAME    "Madire OS"
#define MADIRE_OS_VERSION "0.2"

#define MULTIBOOT_BOOTLOADER_MAGIC 0x2BADB002

/* Start of the Multiboot information structure (only the fields used). */
struct multiboot_info {
    uint32_t flags;
    uint32_t mem_lower;
    uint32_t mem_upper;
    uint32_t boot_device;
    uint32_t cmdline;
} __attribute__((packed));

#define MULTIBOOT_INFO_CMDLINE (1u << 2)

void kernel_main(uint32_t magic, uint32_t mb_info);

#endif
