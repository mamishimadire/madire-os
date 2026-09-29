/* Unit tests for the formatter in kernel/kprintf.c (ksnprintf), including
 * the 0.7 64-bit formats. The file is included directly, with the two
 * output drivers it calls replaced by stubs. */
#include <stdint.h>
#include <string.h>
#include "tests/unit/unit.h"

void vga_putc(char c) { (void)c; }
void serial_putc(char c) { (void)c; }

#include "kernel/kprintf.c"

static char buf[128];

static int same(const char *expected)
{
    if (strcmp(buf, expected) == 0)
        return 1;
    printf("    got \"%s\", expected \"%s\"\n", buf, expected);
    return 0;
}

static void test_existing_formats_unchanged(void)
{
    ksnprintf(buf, sizeof(buf), "%s %c %d %u %x %%", "ab", 'z', -42, 4294967295u, 0xBEEFu);
    CHECK(same("ab z -42 4294967295 beef %"));
    ksnprintf(buf, sizeof(buf), "[%08x] [%5u]", 0x1234u, 7u);
    CHECK(same("[00001234] [    7]"));
    const char *volatile none = 0;                  /* hidden from format checking */
    ksnprintf(buf, sizeof(buf), "%s", none);
    CHECK(same("(null)"));
}

static void test_long_long(void)
{
    ksnprintf(buf, sizeof(buf), "%llx", 0xFFFFFE8000000010ull);
    CHECK(same("fffffe8000000010"));
    ksnprintf(buf, sizeof(buf), "%016llx", 0x1000ull);
    CHECK(same("0000000000001000"));
    ksnprintf(buf, sizeof(buf), "%llu", 18446744073709551615ull);
    CHECK(same("18446744073709551615"));
    ksnprintf(buf, sizeof(buf), "%llu %llu", 4294967296ull, 0ull);
    CHECK(same("4294967296 0"));
    ksnprintf(buf, sizeof(buf), "%lld %lld", -9223372036854775807ll - 1, 123ll);
    CHECK(same("-9223372036854775808 123"));
}

static void test_long(void)
{
    ksnprintf(buf, sizeof(buf), "%lx %lu %ld", 0xABCDEFul, 99ul, -5l);
    CHECK(same("abcdef 99 -5"));
    ksnprintf(buf, sizeof(buf), "%lu", (unsigned long)-1);
    CHECK(same(sizeof(long) == 8 ? "18446744073709551615" : "4294967295"));
}

static void test_pointer_width(void)
{
    ksnprintf(buf, sizeof(buf), "%p", (void *)(uintptr_t)0x1234);
    CHECK(same(sizeof(void *) == 8 ? "0x0000000000001234" : "0x00001234"));
}

static void test_every_power_of_ten(void)
{
    uint64_t v = 1;
    char expect[64];                                /* "%llu %llx" needs up to 37 */
    for (int i = 0; i < 20; i++) {
        ksnprintf(buf, sizeof(buf), "%llu", (unsigned long long)v);
        snprintf(expect, sizeof(expect), "%llu", (unsigned long long)v);
        CHECK(same(expect));
        ksnprintf(buf, sizeof(buf), "%llu", (unsigned long long)(v - 1));
        snprintf(expect, sizeof(expect), "%llu", (unsigned long long)(v - 1));
        CHECK(same(expect));
        v *= 10;
    }
    uint64_t x = 0x9E3779B97F4A7C15ull;
    for (int i = 0; i < 1000; i++) {                /* pseudo-random 64-bit values */
        x ^= x << 13; x ^= x >> 7; x ^= x << 17;
        ksnprintf(buf, sizeof(buf), "%llu %llx", (unsigned long long)x, (unsigned long long)x);
        snprintf(expect, sizeof(expect) , "%llu %llx", (unsigned long long)x, (unsigned long long)x);
        if (strcmp(buf, expect) != 0) {
            CHECK(same(expect));
            return;
        }
    }
    CHECK(1);
}

static void test_truncation(void)
{
    char small[6];
    size_t n = ksnprintf(small, sizeof(small), "%llx", 0x123456789ull);
    CHECK_EQ(n, 5);
    CHECK(strcmp(small, "12345") == 0);
}

int main(void)
{
    printf("kprintf unit tests\n");
    RUN(test_existing_formats_unchanged);
    RUN(test_long_long);
    RUN(test_long);
    RUN(test_pointer_width);
    RUN(test_every_power_of_ten);
    RUN(test_truncation);
    return unit_report("kprintf");
}
