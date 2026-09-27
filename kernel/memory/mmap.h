#ifndef MADIRE_MEMORY_MMAP_H
#define MADIRE_MEMORY_MMAP_H

/*
 * Multiboot memory-map validation and iteration.
 *
 * Pure module: no hardware access, printing or panics, so it is unit-tested
 * on the host (tests/unit/test_mmap.c). The map is read in place; it is never
 * copied, so there is no entry limit that could silently drop entries.
 */

#include <stdbool.h>
#include <stdint.h>

enum mem_kind {
    MEM_USABLE,             /* type 1: available RAM */
    MEM_RESERVED,           /* type 2 and any unknown type */
    MEM_ACPI_RECLAIMABLE,   /* type 3: RAM holding ACPI tables; not reclaimed in 0.3 */
    MEM_ACPI_NVS,           /* type 4: RAM the firmware keeps */
    MEM_BAD,                /* type 5: defective RAM */
};

struct mem_region {
    uint64_t base;
    uint64_t length;        /* > 0; base + length does not overflow */
    uint32_t raw_type;
    enum mem_kind kind;
};

enum mmap_status {
    MMAP_OK,
    MMAP_ERR_EMPTY,         /* no bytes, or no non-empty entries */
    MMAP_ERR_BAD_BUFFER,    /* null address, or buffer wraps past 4 GiB */
    MMAP_ERR_ENTRY_SIZE,    /* entry size field < 20 */
    MMAP_ERR_TRUNCATED,     /* entry extends past the end of the buffer */
    MMAP_ERR_OVERFLOW,      /* base + length overflows 64 bits */
};

struct mmap_iter {
    uint64_t pos;
    uint64_t end;
};

/* Checks the whole buffer before any entry is used. On error, *bad_offset
 * (if not NULL) is the byte offset of the offending entry. */
enum mmap_status mmap_validate(uint32_t addr, uint32_t length, uint32_t *bad_offset);

/* Iterates a buffer that passed mmap_validate(). Zero-length entries are
 * skipped. Returns false after the last entry. */
void mmap_iter_init(struct mmap_iter *it, uint32_t addr, uint32_t length);
bool mmap_next(struct mmap_iter *it, struct mem_region *out);

enum mem_kind mem_kind_from_type(uint32_t type);
bool mem_kind_is_ram(enum mem_kind kind);   /* usable, ACPI, NVS or bad RAM */

const char *mmap_status_str(enum mmap_status status);
const char *mem_kind_str(enum mem_kind kind);

#endif
