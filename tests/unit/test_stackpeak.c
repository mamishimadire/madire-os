/* Unit tests for kernel/stackpeak.c (stack high-water mark). */
#include <stdint.h>
#include <string.h>
#include "kernel/stackpeak.h"
#include "tests/unit/unit.h"

#define STACK_SIZE 16384u

static uint8_t stack[STACK_SIZE];

/* Simulates a stack that was used `depth` bytes down from its top. */
static void use_top(size_t depth, uint8_t value)
{
    memset(stack + STACK_SIZE - depth, value, depth);
}

static void test_untouched_stack(void)
{
    stack_paint(stack, STACK_SIZE);
    CHECK_EQ(stack_peak(stack, STACK_SIZE), 0);
    for (size_t i = 0; i < STACK_SIZE; i++)
        if (stack[i] != STACK_PAINT_BYTE) {
            CHECK(0 && "paint missed a byte");
            break;
        }
}

static void test_partly_used(void)
{
    stack_paint(stack, STACK_SIZE);
    use_top(100, 0x00);
    CHECK_EQ(stack_peak(stack, STACK_SIZE), 100);
    use_top(5000, 0x11);                        /* deeper later: the peak grows */
    CHECK_EQ(stack_peak(stack, STACK_SIZE), 5000);
    use_top(40, 0x22);                          /* shallow use afterwards: peak kept */
    CHECK_EQ(stack_peak(stack, STACK_SIZE), 5000);
}

static void test_fully_used(void)
{
    stack_paint(stack, STACK_SIZE);
    use_top(STACK_SIZE, 0x00);
    CHECK_EQ(stack_peak(stack, STACK_SIZE), STACK_SIZE);
    stack[0] = 0x01;                            /* only the very bottom byte */
    memset(stack + 1, STACK_PAINT_BYTE, STACK_SIZE - 1);
    CHECK_EQ(stack_peak(stack, STACK_SIZE), STACK_SIZE);
}

static void test_pattern_byte_at_the_deepest_point(void)
{
    /* The deepest written bytes equal the pattern: the peak can only come
     * out lower (by those bytes), never higher. */
    stack_paint(stack, STACK_SIZE);
    use_top(1000, 0x33);
    stack[STACK_SIZE - 1000] = STACK_PAINT_BYTE;
    stack[STACK_SIZE - 999] = STACK_PAINT_BYTE;
    CHECK_EQ(stack_peak(stack, STACK_SIZE), 998);
}

static void test_edges(void)
{
    CHECK_EQ(stack_peak(stack, 0), 0);
    stack_paint(stack, 1);
    CHECK_EQ(stack_peak(stack, 1), 0);
    stack[0] = 0;
    CHECK_EQ(stack_peak(stack, 1), 1);
    stack_paint(stack + 3, 13);                 /* unaligned start and length */
    CHECK_EQ(stack_peak(stack + 3, 13), 0);
    stack[3 + 12] = 0;
    CHECK_EQ(stack_peak(stack + 3, 13), 1);
    stack[2] = 0x77;                            /* outside the range: ignored */
    stack[16] = 0x77;
    CHECK_EQ(stack_peak(stack + 3, 13), 1);
}

int main(void)
{
    printf("stackpeak unit tests\n");
    RUN(test_untouched_stack);
    RUN(test_partly_used);
    RUN(test_fully_used);
    RUN(test_pattern_byte_at_the_deepest_point);
    RUN(test_edges);
    return unit_report("stackpeak");
}
