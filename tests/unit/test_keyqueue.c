/* Unit tests for kernel/input/keyqueue.c (key-event queue). */
#include <stdint.h>
#include "kernel/input/keyqueue.h"
#include "tests/unit/unit.h"

static struct keyqueue q;

static struct key_event make(uint32_t n)
{
    return (struct key_event){ KEY_CHAR, (char)('a' + n % 26), KEY_PRESSED, (uint8_t)n };
}

static void test_empty_pop(void)
{
    struct key_event e;
    keyqueue_init(&q);
    CHECK(!keyqueue_pop(&q, &e));
    CHECK_EQ(keyqueue_count(&q), 0);
    CHECK_EQ(keyqueue_dropped(&q), 0);
}

static void test_fifo_order(void)
{
    struct key_event e;
    keyqueue_init(&q);
    for (uint32_t i = 0; i < 10; i++) {
        e = make(i);
        CHECK(keyqueue_push(&q, &e));
    }
    CHECK_EQ(keyqueue_count(&q), 10);
    for (uint32_t i = 0; i < 10; i++) {
        CHECK(keyqueue_pop(&q, &e));
        CHECK_EQ(e.scancode, i);
        CHECK_EQ(e.ascii, 'a' + i % 26);
    }
    CHECK(!keyqueue_pop(&q, &e));
}

static void test_fill_to_capacity(void)
{
    struct key_event e;
    keyqueue_init(&q);
    for (uint32_t i = 0; i < KEYQUEUE_SIZE; i++) {
        e = make(i);
        CHECK(keyqueue_push(&q, &e));
    }
    CHECK_EQ(keyqueue_count(&q), KEYQUEUE_SIZE);
    CHECK_EQ(keyqueue_dropped(&q), 0);
}

static void test_overflow_drops_newest(void)
{
    struct key_event e;
    keyqueue_init(&q);
    for (uint32_t i = 0; i < 100; i++) {
        e = make(i);
        CHECK_EQ(keyqueue_push(&q, &e), i < KEYQUEUE_SIZE);
    }
    CHECK_EQ(keyqueue_dropped(&q), 100 - KEYQUEUE_SIZE);
    CHECK_EQ(keyqueue_count(&q), KEYQUEUE_SIZE);
    /* The kept events are the first 64, in order. */
    for (uint32_t i = 0; i < KEYQUEUE_SIZE; i++) {
        CHECK(keyqueue_pop(&q, &e));
        CHECK_EQ(e.scancode, i);
    }
    CHECK(!keyqueue_pop(&q, &e));
}

static void test_space_frees_after_pop(void)
{
    struct key_event e;
    keyqueue_init(&q);
    for (uint32_t i = 0; i < KEYQUEUE_SIZE; i++) {
        e = make(i);
        keyqueue_push(&q, &e);
    }
    e = make(200);
    CHECK(!keyqueue_push(&q, &e));
    CHECK(keyqueue_pop(&q, &e));
    e = make(201);
    CHECK(keyqueue_push(&q, &e));                   /* one slot free again */
    CHECK_EQ(keyqueue_dropped(&q), 1);
}

static void test_wraparound_many_cycles(void)
{
    struct key_event e;
    keyqueue_init(&q);
    uint32_t next_in = 0, next_out = 0;
    /* Uneven push/pop batches force the indices around the ring many times. */
    for (uint32_t round = 0; round < 1000; round++) {
        uint32_t pushes = 1 + round % 37, pops = 1 + round % 29;
        for (uint32_t i = 0; i < pushes; i++) {
            e = make(next_in);
            if (keyqueue_push(&q, &e))
                next_in++;
        }
        for (uint32_t i = 0; i < pops && keyqueue_pop(&q, &e); i++) {
            CHECK_EQ(e.scancode, (uint8_t)next_out);
            next_out++;
        }
        CHECK(keyqueue_count(&q) <= KEYQUEUE_SIZE);
    }
    while (keyqueue_pop(&q, &e)) {
        CHECK_EQ(e.scancode, (uint8_t)next_out);
        next_out++;
    }
    CHECK_EQ(next_in, next_out);
}

static void test_counter_wrap(void)
{
    /* Free-running counters close to 2^32 still give correct counts. */
    struct key_event e;
    keyqueue_init(&q);
    q.head = q.tail = 0xFFFFFFF0u;
    for (uint32_t i = 0; i < 40; i++) {
        e = make(i);
        CHECK(keyqueue_push(&q, &e));
    }
    CHECK_EQ(keyqueue_count(&q), 40);
    for (uint32_t i = 0; i < 40; i++) {
        CHECK(keyqueue_pop(&q, &e));
        CHECK_EQ(e.scancode, i);
    }
    CHECK_EQ(keyqueue_count(&q), 0);
}

static void test_reinit_clears_everything(void)
{
    struct key_event e = make(1);
    keyqueue_init(&q);
    for (uint32_t i = 0; i < KEYQUEUE_SIZE + 5; i++)
        keyqueue_push(&q, &e);
    keyqueue_init(&q);
    CHECK_EQ(keyqueue_count(&q), 0);
    CHECK_EQ(keyqueue_dropped(&q), 0);
    CHECK(!keyqueue_pop(&q, &e));
}

int main(void)
{
    printf("keyqueue unit tests\n");
    RUN(test_empty_pop);
    RUN(test_fifo_order);
    RUN(test_fill_to_capacity);
    RUN(test_overflow_drops_newest);
    RUN(test_space_frees_after_pop);
    RUN(test_wraparound_many_cycles);
    RUN(test_counter_wrap);
    RUN(test_reinit_clears_everything);
    return unit_report("keyqueue");
}
