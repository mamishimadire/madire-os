/* Unit tests for kernel/console/outq.c (foreground output queue, 0.11). */
#include <stdint.h>
#include <string.h>
#include "kernel/console/outq.h"
#include "tests/unit/unit.h"

static struct outq q;

static void fill(uint8_t *buf, size_t len, uint32_t offset)
{
    for (size_t i = 0; i < len; i++)
        buf[i] = (uint8_t)('a' + (offset + (uint32_t)i) % 26);
}

static void test_empty(void)
{
    uint8_t out[8];
    outq_init(&q);
    CHECK_EQ(outq_used(&q), 0);
    CHECK_EQ(outq_read(&q, out, sizeof(out)), 0);
    CHECK_EQ(outq_write(&q, out, 0), 0);
}

static void test_capacity_and_partial_write(void)
{
    static uint8_t input[OUTQ_CAPACITY + 1];
    static uint8_t output[OUTQ_CAPACITY];
    fill(input, sizeof(input), 0);
    outq_init(&q);
    CHECK_EQ(outq_write(&q, input, sizeof(input)), OUTQ_CAPACITY);
    CHECK_EQ(outq_used(&q), OUTQ_CAPACITY);
    CHECK_EQ(outq_write(&q, input + OUTQ_CAPACITY, 1), 0);
    CHECK_EQ(outq_read(&q, output, sizeof(output)), OUTQ_CAPACITY);
    CHECK(memcmp(output, input, OUTQ_CAPACITY) == 0);
    CHECK_EQ(outq_used(&q), 0);
}

static void test_wrap_and_fifo(void)
{
    static uint8_t first[7000];
    static uint8_t second[5000];
    static uint8_t output[5500];
    fill(first, sizeof(first), 0);
    fill(second, sizeof(second), 7000);
    outq_init(&q);
    CHECK_EQ(outq_write(&q, first, sizeof(first)), sizeof(first));
    CHECK_EQ(outq_read(&q, output, 6500), 6500);
    CHECK(memcmp(output, first, 6500) == 0);
    CHECK_EQ(outq_write(&q, second, sizeof(second)), sizeof(second));
    CHECK_EQ(outq_used(&q), sizeof(output));
    CHECK_EQ(outq_read(&q, output, sizeof(output)), sizeof(output));
    CHECK(memcmp(output, first + 6500, 500) == 0);
    CHECK(memcmp(output + 500, second, sizeof(second)) == 0);
    CHECK_EQ(outq_used(&q), 0);
}

int main(void)
{
    RUN(test_empty);
    RUN(test_capacity_and_partial_write);
    RUN(test_wrap_and_fifo);
    return unit_report("outq unit tests");
}
