#ifndef MADIRE_MULTIBOOT_H
#define MADIRE_MULTIBOOT_H

/*
 * Multiboot 1 (specification 0.6.96) structures, as handed over by a Multiboot 1 loader (GRUB 2 until 0.12, Limine since 0.13)
 * and by QEMU's -kernel loader. Multiboot 2 is not
 * supported. All addresses are 32-bit physical addresses; memory-map entries
 * carry 64-bit base/length because firmware may report RAM above 4 GiB.
 */

#include <stddef.h>
#include <stdint.h>

#define MULTIBOOT_BOOTLOADER_MAGIC 0x2BADB002

/* multiboot_info.flags: which fields the bootloader filled in. */
#define MULTIBOOT_INFO_MEMORY   (1u << 0)   /* mem_lower, mem_upper */
#define MULTIBOOT_INFO_CMDLINE  (1u << 2)   /* cmdline */
#define MULTIBOOT_INFO_MODS     (1u << 3)   /* mods_count, mods_addr */
#define MULTIBOOT_INFO_MEM_MAP  (1u << 6)   /* mmap_length, mmap_addr */

struct multiboot_info {
    uint32_t flags;
    uint32_t mem_lower;
    uint32_t mem_upper;
    uint32_t boot_device;
    uint32_t cmdline;
    uint32_t mods_count;
    uint32_t mods_addr;
    uint32_t syms[4];
    uint32_t mmap_length;
    uint32_t mmap_addr;
    uint32_t drives_length;
    uint32_t drives_addr;
    uint32_t config_table;
    uint32_t boot_loader_name;
    uint32_t apm_table;
    uint32_t vbe_control_info;
    uint32_t vbe_mode_info;
    uint16_t vbe_mode;
    uint16_t vbe_interface_seg;
    uint16_t vbe_interface_off;
    uint16_t vbe_interface_len;
    uint64_t framebuffer_addr;
    uint32_t framebuffer_pitch;
    uint32_t framebuffer_width;
    uint32_t framebuffer_height;
    uint8_t  framebuffer_bpp;
    uint8_t  framebuffer_type;
    uint8_t  color_info[6];
} __attribute__((packed));

/* One memory-map entry. `size` does not count itself: the next entry starts
 * at (address of this entry) + size + 4. size may be larger than 20. */
struct multiboot_mmap_entry {
    uint32_t size;
    uint64_t base;
    uint64_t length;
    uint32_t type;
} __attribute__((packed));

#define MULTIBOOT_MMAP_ENTRY_MIN_SIZE 20u   /* base + length + type */

/* Memory-map entry types. */
#define MULTIBOOT_MEMORY_AVAILABLE         1
#define MULTIBOOT_MEMORY_RESERVED          2
#define MULTIBOOT_MEMORY_ACPI_RECLAIMABLE  3
#define MULTIBOOT_MEMORY_NVS               4
#define MULTIBOOT_MEMORY_BADRAM            5

struct multiboot_module {
    uint32_t mod_start;
    uint32_t mod_end;       /* exclusive */
    uint32_t string;        /* module command line, 0 if none */
    uint32_t reserved;
} __attribute__((packed));

/* Offsets fixed by the specification. */
_Static_assert(offsetof(struct multiboot_info, cmdline) == 16, "multiboot cmdline offset");
_Static_assert(offsetof(struct multiboot_info, mods_addr) == 24, "multiboot mods_addr offset");
_Static_assert(offsetof(struct multiboot_info, mmap_length) == 44, "multiboot mmap_length offset");
_Static_assert(offsetof(struct multiboot_info, mmap_addr) == 48, "multiboot mmap_addr offset");
_Static_assert(offsetof(struct multiboot_info, framebuffer_addr) == 88, "multiboot framebuffer offset");
_Static_assert(sizeof(struct multiboot_info) == 116, "multiboot_info size");
_Static_assert(sizeof(struct multiboot_mmap_entry) == 24, "multiboot mmap entry size");
_Static_assert(sizeof(struct multiboot_module) == 16, "multiboot module size");

#endif
