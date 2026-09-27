/*
 * Multiboot memory-map validation and iteration (see mmap.h).
 *
 * Entry layout: { u32 size; u64 base; u64 length; u32 type }. `size` does
 * not count itself, so the next entry starts at entry + size + 4. The buffer
 * address is a physical address; with paging off it is also a pointer.
 */
#include "kernel/memory/mmap.h"
#include "kernel/multiboot.h"

#define ADDR_LIMIT 0x100000000ull   /* 32-bit physical address space */

static const struct multiboot_mmap_entry *entry_at(uint64_t pos)
{
    return (const struct multiboot_mmap_entry *)(uintptr_t)pos;
}

enum mmap_status mmap_validate(uint32_t addr, uint32_t length, uint32_t *bad_offset)
{
    if (bad_offset)
        *bad_offset = 0;
    if (length == 0)
        return MMAP_ERR_EMPTY;
    if (addr == 0 || (uint64_t)addr + length > ADDR_LIMIT)
        return MMAP_ERR_BAD_BUFFER;

    uint64_t pos = addr, end = (uint64_t)addr + length;
    unsigned entries = 0;

    while (pos < end) {
        if (bad_offset)
            *bad_offset = (uint32_t)(pos - addr);
        if (end - pos < sizeof(uint32_t))
            return MMAP_ERR_TRUNCATED;

        const struct multiboot_mmap_entry *e = entry_at(pos);
        uint32_t size = e->size;
        if (size < MULTIBOOT_MMAP_ENTRY_MIN_SIZE)
            return MMAP_ERR_ENTRY_SIZE;
        if ((uint64_t)size + sizeof(uint32_t) > end - pos)
            return MMAP_ERR_TRUNCATED;

        /* Rejects both a real overflow and an end of exactly 2^64. */
        if (e->length > UINT64_MAX - e->base)
            return MMAP_ERR_OVERFLOW;
        if (e->length != 0)
            entries++;

        pos += (uint64_t)size + sizeof(uint32_t);
    }

    if (bad_offset)
        *bad_offset = 0;
    return entries ? MMAP_OK : MMAP_ERR_EMPTY;
}

void mmap_iter_init(struct mmap_iter *it, uint32_t addr, uint32_t length)
{
    it->pos = addr;
    it->end = (uint64_t)addr + length;
}

bool mmap_next(struct mmap_iter *it, struct mem_region *out)
{
    while (it->pos < it->end) {
        const struct multiboot_mmap_entry *e = entry_at(it->pos);
        it->pos += (uint64_t)e->size + sizeof(uint32_t);
        if (e->length == 0)
            continue;
        out->base = e->base;
        out->length = e->length;
        out->raw_type = e->type;
        out->kind = mem_kind_from_type(e->type);
        return true;
    }
    return false;
}

enum mem_kind mem_kind_from_type(uint32_t type)
{
    switch (type) {
    case MULTIBOOT_MEMORY_AVAILABLE:        return MEM_USABLE;
    case MULTIBOOT_MEMORY_ACPI_RECLAIMABLE: return MEM_ACPI_RECLAIMABLE;
    case MULTIBOOT_MEMORY_NVS:              return MEM_ACPI_NVS;
    case MULTIBOOT_MEMORY_BADRAM:           return MEM_BAD;
    default:                                return MEM_RESERVED;
    }
}

bool mem_kind_is_ram(enum mem_kind kind)
{
    return kind != MEM_RESERVED;
}

const char *mmap_status_str(enum mmap_status status)
{
    switch (status) {
    case MMAP_OK:             return "ok";
    case MMAP_ERR_EMPTY:      return "memory map is empty";
    case MMAP_ERR_BAD_BUFFER: return "memory map buffer address is invalid";
    case MMAP_ERR_ENTRY_SIZE: return "memory map entry size is too small";
    case MMAP_ERR_TRUNCATED:  return "memory map entry is truncated";
    case MMAP_ERR_OVERFLOW:   return "memory map entry overflows 64 bits";
    }
    return "unknown memory map error";
}

const char *mem_kind_str(enum mem_kind kind)
{
    switch (kind) {
    case MEM_USABLE:           return "usable";
    case MEM_RESERVED:         return "reserved";
    case MEM_ACPI_RECLAIMABLE: return "ACPI reclaimable";
    case MEM_ACPI_NVS:         return "ACPI NVS";
    case MEM_BAD:              return "bad RAM";
    }
    return "unknown";
}
