/* Unit tests for kernel/bootmod.c (boot-module integrity check). */
#include <stdint.h>
#include <string.h>
#include "kernel/bootmod.h"
#include "tests/unit/unit.h"

static uint8_t mod[16 + 4096];

/* Builds a test module with `length` payload bytes; returns its size. */
static uint32_t make(uint32_t length, uint32_t seed)
{
    memcpy(mod, "MADIRETM", 8);
    for (uint32_t i = 0; i < length; i++) {
        seed = seed * 1103515245u + 12345u;
        mod[16 + i] = (uint8_t)(seed >> 16);
    }
    uint32_t h = bootmod_fnv1a(mod + 16, length);
    for (int i = 0; i < 4; i++) {
        mod[8 + i] = (uint8_t)(length >> (8 * i));
        mod[12 + i] = (uint8_t)(h >> (8 * i));
    }
    return 16 + length;
}

static void test_fnv1a_known_values(void)
{
    /* Published FNV-1a 32-bit test vectors. */
    CHECK_EQ(bootmod_fnv1a((const uint8_t *)"", 0), 0x811C9DC5u);
    CHECK_EQ(bootmod_fnv1a((const uint8_t *)"a", 1), 0xE40C292Cu);
    CHECK_EQ(bootmod_fnv1a((const uint8_t *)"foobar", 6), 0xBF9CF968u);
}

static void test_intact(void)
{
    struct bootmod_result r;
    uint32_t size = make(4096, 7);
    CHECK_EQ(bootmod_check(mod, size, &r), BOOTMOD_INTACT);
    CHECK_EQ(r.length, 4096);
    CHECK_EQ(r.actual, r.expected);
    size = make(0, 7);                              /* empty payload */
    CHECK_EQ(bootmod_check(mod, size, &r), BOOTMOD_INTACT);
    size = make(100, 9);                            /* loader rounded the size up */
    CHECK_EQ(bootmod_check(mod, size + 3000, &r), BOOTMOD_INTACT);
}

static void test_every_byte_matters(void)
{
    struct bootmod_result r;
    uint32_t size = make(512, 3);
    for (uint32_t i = 16; i < size; i++) {
        mod[i] ^= 0x01;
        if (bootmod_check(mod, size, &r) != BOOTMOD_CORRUPT) {
            CHECK(0 && "a flipped payload bit went unnoticed");
            return;
        }
        mod[i] ^= 0x01;
    }
    CHECK_EQ(bootmod_check(mod, size, &r), BOOTMOD_INTACT);
    memset(mod + 16 + 100, 0, 64);                  /* a zeroed stretch (e.g. by .bss clearing) */
    CHECK_EQ(bootmod_check(mod, size, &r), BOOTMOD_CORRUPT);
    CHECK(r.actual != r.expected);
}

static void test_truncated_and_no_header(void)
{
    struct bootmod_result r;
    uint32_t size = make(1000, 5);
    CHECK_EQ(bootmod_check(mod, size - 1, &r), BOOTMOD_TRUNCATED);
    CHECK_EQ(bootmod_check(mod, 15, &r), BOOTMOD_NO_HEADER);          /* shorter than a header */
    CHECK_EQ(bootmod_check(0, 100, &r), BOOTMOD_NO_HEADER);
    mod[0] = 'X';
    CHECK_EQ(bootmod_check(mod, size, &r), BOOTMOD_NO_HEADER);
    mod[0] = 'M';
    mod[8] = 0xFF; mod[9] = 0xFF; mod[10] = 0xFF; mod[11] = 0xFF;     /* length 4 GiB - 1 */
    CHECK_EQ(bootmod_check(mod, size, &r), BOOTMOD_TRUNCATED);
}

static void test_status_strings(void)
{
    CHECK(strcmp(bootmod_status_str(BOOTMOD_INTACT), "test pattern intact") == 0);
    CHECK(strcmp(bootmod_status_str(BOOTMOD_CORRUPT), "test pattern CORRUPTED") == 0);
    CHECK(strcmp(bootmod_status_str(BOOTMOD_NO_HEADER), "no test header") == 0);
}

int main(void)
{
    printf("bootmod unit tests\n");
    RUN(test_fnv1a_known_values);
    RUN(test_intact);
    RUN(test_every_byte_matters);
    RUN(test_truncated_and_no_header);
    RUN(test_status_strings);
    return unit_report("bootmod");
}
