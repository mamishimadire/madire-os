/* Boot-module integrity check (see bootmod.h). */
#include "kernel/bootmod.h"

#define FNV_OFFSET 0x811C9DC5u
#define FNV_PRIME  0x01000193u

uint32_t bootmod_fnv1a(const uint8_t *data, uint32_t length)
{
    uint32_t h = FNV_OFFSET;
    for (uint32_t i = 0; i < length; i++) {
        h ^= data[i];
        h *= FNV_PRIME;
    }
    return h;
}

static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

enum bootmod_status bootmod_check(const uint8_t *data, uint32_t size, struct bootmod_result *out)
{
    static const char magic[] = BOOTMOD_MAGIC;
    out->length = out->expected = out->actual = 0;
    out->status = BOOTMOD_NO_HEADER;
    if (!data || size < BOOTMOD_HEADER_SIZE)
        return out->status;
    for (unsigned i = 0; i < 8; i++)
        if (data[i] != (uint8_t)magic[i])
            return out->status;

    out->length = le32(data + 8);
    out->expected = le32(data + 12);
    if (out->length > size - BOOTMOD_HEADER_SIZE) {
        out->status = BOOTMOD_TRUNCATED;
        return out->status;
    }
    out->actual = bootmod_fnv1a(data + BOOTMOD_HEADER_SIZE, out->length);
    out->status = out->actual == out->expected ? BOOTMOD_INTACT : BOOTMOD_CORRUPT;
    return out->status;
}

const char *bootmod_status_str(enum bootmod_status status)
{
    switch (status) {
    case BOOTMOD_INTACT:    return "test pattern intact";
    case BOOTMOD_CORRUPT:   return "test pattern CORRUPTED";
    case BOOTMOD_TRUNCATED: return "test module TRUNCATED";
    case BOOTMOD_NO_HEADER: return "no test header";
    }
    return "unknown";
}
