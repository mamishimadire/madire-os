#ifndef MADIRE_BOOTMOD_H
#define MADIRE_BOOTMOD_H

/*
 * Boot-module integrity check (0.7). A test module starts with a header
 * that describes its own contents:
 *
 *   offset 0   8 bytes  magic "MADIRETM"
 *   offset 8   4 bytes  payload length in bytes (little endian)
 *   offset 12  4 bytes  FNV-1a (32-bit) of the payload
 *   offset 16  payload
 *
 * The kernel checks every Multiboot module after boot and logs whether a
 * test module is intact; this proves the loader did not place a module
 * where the kernel (e.g. its .bss) or its boot tables overwrite it. Modules
 * without the header are reported, not judged.
 *
 * Pure module: no hardware access (tests/unit/test_bootmod.c).
 */

#include <stdint.h>

#define BOOTMOD_MAGIC       "MADIRETM"
#define BOOTMOD_HEADER_SIZE 16u

enum bootmod_status {
    BOOTMOD_INTACT,             /* header valid, checksum matches */
    BOOTMOD_CORRUPT,            /* header valid, checksum differs */
    BOOTMOD_TRUNCATED,          /* header valid, module shorter than it says */
    BOOTMOD_NO_HEADER,          /* not a test module */
};

struct bootmod_result {
    enum bootmod_status status;
    uint32_t length;            /* payload length from the header */
    uint32_t expected;          /* checksum from the header */
    uint32_t actual;            /* checksum computed over the payload */
};

uint32_t bootmod_fnv1a(const uint8_t *data, uint32_t length);

/* data/size: the whole module as loaded. */
enum bootmod_status bootmod_check(const uint8_t *data, uint32_t size, struct bootmod_result *out);

const char *bootmod_status_str(enum bootmod_status status);

#endif
