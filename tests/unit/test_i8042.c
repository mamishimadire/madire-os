/* Unit tests for drivers/ps2/i8042.c (polled controller access, 1.2). */
#include <stdbool.h>
#include <stdint.h>
#include "drivers/ps2/i8042.h"
#include "tests/unit/unit.h"

/*
 * A fake controller and clock. Status read r (counted from 1):
 *   input full while r <= ibf_reads;
 *   output full while bytes are queued and at least obf_gap status reads
 *   were made since the last data read (the current one included);
 *   aux_bit added to every status.
 * The clock reads t0 + reads / reads_per_tick (frozen if reads_per_tick
 * is 0); it is usable if clock_ok, until clock_lost_after reads (0: never).
 */
static uint32_t ibf_reads, obf_gap, reads_per_tick, clock_lost_after;
static bool clock_ok;
static uint8_t aux_bit;
static uint64_t t0;
static uint8_t queue[128];
static uint32_t qhead, qlen;

static uint32_t status_reads, data_reads, since_data, tick_calls;
static uint32_t commands, data_writes;
static uint8_t last_command, last_data;

static void reset(void)
{
    ibf_reads = 0; obf_gap = 1; reads_per_tick = 0; clock_lost_after = 0;
    clock_ok = false; aux_bit = 0; t0 = 0; qhead = 0; qlen = 0;
    status_reads = 0; data_reads = 0; since_data = 0; tick_calls = 0;
    commands = 0; data_writes = 0; last_command = 0; last_data = 0;
}

static void push(uint8_t b)
{
    queue[(qhead + qlen++) % sizeof(queue)] = b;
}

static uint8_t fake_status(void)
{
    status_reads++;
    since_data++;
    uint8_t s = aux_bit;
    if (status_reads <= ibf_reads)
        s |= I8042_STATUS_INPUT_FULL;
    if (qlen > 0 && since_data >= obf_gap)
        s |= I8042_STATUS_OUTPUT_FULL;
    return s;
}

static uint8_t fake_data(void)
{
    data_reads++;
    since_data = 0;
    if (qlen == 0)
        return 0xEE;
    uint8_t b = queue[qhead];
    qhead = (qhead + 1) % sizeof(queue);
    qlen--;
    return b;
}

static void fake_command(uint8_t c) { commands++; last_command = c; }
static void fake_write(uint8_t b)   { data_writes++; last_data = b; }

static bool fake_ticks(uint64_t *now)
{
    tick_calls++;
    if (!clock_ok || (clock_lost_after && status_reads >= clock_lost_after))
        return false;
    *now = t0 + (reads_per_tick ? status_reads / reads_per_tick : 0);
    return true;
}

static const struct i8042_bus bus = { fake_status, fake_data, fake_command, fake_write, fake_ticks };

static void test_constants(void)
{
    CHECK_EQ(I8042_DATA, 0x60);
    CHECK_EQ(I8042_STATUS, 0x64);
    CHECK_EQ(I8042_COMMAND, 0x64);
    CHECK_EQ(I8042_STATUS_OUTPUT_FULL, 0x01);
    CHECK_EQ(I8042_STATUS_INPUT_FULL, 0x02);
    CHECK_EQ(I8042_STATUS_AUX_DATA, 0x20);
    CHECK_EQ(I8042_WAIT_TICKS, 10);
    CHECK_EQ(I8042_SPIN_LIMIT, 1000000);
    CHECK_EQ(I8042_FLUSH_LIMIT, 64);
}

static void test_begin(void)
{
    struct i8042_wait w;
    reset();
    clock_ok = true; t0 = 500;
    i8042_wait_begin(&bus, &w);
    CHECK(w.timed);
    CHECK_EQ(w.deadline, 510);
    CHECK_EQ(w.spins, 0);

    reset();
    i8042_wait_begin(&bus, &w);
    CHECK(!w.timed);
    CHECK_EQ(w.spins, 0);
}

static void test_command_at_once(void)
{
    reset();
    clock_ok = true; t0 = 100; reads_per_tick = 1;
    CHECK(i8042_send_command(&bus, 0x20));
    CHECK_EQ(status_reads, 1);
    CHECK_EQ(commands, 1);
    CHECK_EQ(last_command, 0x20);
    CHECK_EQ(data_writes, 0);
}

static void test_command_after_busy(void)
{
    reset();
    clock_ok = true; t0 = 100; reads_per_tick = 1; ibf_reads = 5;
    CHECK(i8042_send_command(&bus, 0xAE));
    CHECK_EQ(status_reads, 6);
    CHECK_EQ(commands, 1);
    CHECK_EQ(last_command, 0xAE);
}

static void test_data_write(void)
{
    reset();
    ibf_reads = 3;
    CHECK(i8042_send_data(&bus, 0xED));
    CHECK_EQ(status_reads, 4);
    CHECK_EQ(data_writes, 1);
    CHECK_EQ(last_data, 0xED);
    CHECK_EQ(commands, 0);
}

/* With the clock: the wait ends at the read where the tick count reaches
 * the deadline (begin + 10 ticks), and nothing is written. */
static void test_timed_timeout(void)
{
    reset();
    clock_ok = true; t0 = 100; reads_per_tick = 4; ibf_reads = 0xFFFFFFFFu;
    CHECK(!i8042_send_command(&bus, 0x60));
    CHECK_EQ(status_reads, 40);
    CHECK_EQ(commands, 0);

    reset();
    clock_ok = true; t0 = 100; reads_per_tick = 4; ibf_reads = 0xFFFFFFFFu;
    CHECK(!i8042_send_data(&bus, 0x01));
    CHECK_EQ(status_reads, 40);
    CHECK_EQ(data_writes, 0);

    /* Ready on the last read before the deadline: taken. */
    reset();
    clock_ok = true; t0 = 100; reads_per_tick = 4; ibf_reads = 39;
    CHECK(i8042_send_data(&bus, 0x02));
    CHECK_EQ(status_reads, 40);
    CHECK_EQ(data_writes, 1);
}

/* Without the clock: exactly I8042_SPIN_LIMIT status reads, the clock asked
 * only once (at the start); a byte ready on the last read is still taken. */
static void test_untimed_spin_limit(void)
{
    reset();
    ibf_reads = 0xFFFFFFFFu;
    CHECK(!i8042_send_command(&bus, 0x20));
    CHECK_EQ(status_reads, I8042_SPIN_LIMIT);
    CHECK_EQ(tick_calls, 1);
    CHECK_EQ(commands, 0);

    reset();
    ibf_reads = I8042_SPIN_LIMIT - 1;
    CHECK(i8042_send_command(&bus, 0x20));
    CHECK_EQ(status_reads, I8042_SPIN_LIMIT);
    CHECK_EQ(commands, 1);
}

/* A timer that stops (frozen count), or a clock that becomes unusable
 * during the wait: the spin limit still ends it. */
static void test_clock_stops(void)
{
    reset();
    clock_ok = true; t0 = 100; reads_per_tick = 0; ibf_reads = 0xFFFFFFFFu;
    CHECK(!i8042_send_command(&bus, 0x20));
    CHECK_EQ(status_reads, I8042_SPIN_LIMIT);

    reset();
    clock_ok = true; t0 = 100; reads_per_tick = 1000; clock_lost_after = 10;
    ibf_reads = 0xFFFFFFFFu;
    CHECK(!i8042_send_command(&bus, 0x20));
    CHECK_EQ(status_reads, I8042_SPIN_LIMIT);
}

static void test_read(void)
{
    uint8_t b = 0x55;
    reset();
    clock_ok = true; t0 = 7; reads_per_tick = 1; obf_gap = 4;
    push(0xAB);
    CHECK(i8042_read_data(&bus, &b));
    CHECK_EQ(b, 0xAB);
    CHECK_EQ(status_reads, 4);
    CHECK_EQ(data_reads, 1);

    /* Nothing arrives: no data read, *out unchanged. */
    reset();
    clock_ok = true; t0 = 7; reads_per_tick = 1;
    b = 0x55;
    CHECK(!i8042_read_data(&bus, &b));
    CHECK_EQ(b, 0x55);
    CHECK_EQ(data_reads, 0);
    CHECK_EQ(status_reads, 10);

    /* The aux bit does not change readiness (the caller decides). */
    reset();
    aux_bit = I8042_STATUS_AUX_DATA;
    push(0x08);
    CHECK(i8042_read_data(&bus, &b));
    CHECK_EQ(b, 0x08);
}

/* One wait for several bytes: a byte every 4 reads, a tick every read.
 * Within one wait (deadline 110) two bytes come in time, the third would
 * come at read 12 and is not waited for; three separate waits take all. */
static void test_shared_wait(void)
{
    struct i8042_wait w;
    uint8_t b = 0;
    reset();
    clock_ok = true; t0 = 100; reads_per_tick = 1; obf_gap = 4;
    push(1); push(2); push(3);
    i8042_wait_begin(&bus, &w);
    CHECK(i8042_read_data_within(&bus, &w, &b));
    CHECK_EQ(b, 1);
    CHECK(i8042_read_data_within(&bus, &w, &b));
    CHECK_EQ(b, 2);
    CHECK(!i8042_read_data_within(&bus, &w, &b));
    CHECK_EQ(b, 2);
    CHECK_EQ(status_reads, 10);
    CHECK_EQ(data_reads, 2);

    reset();
    clock_ok = true; t0 = 100; reads_per_tick = 1; obf_gap = 4;
    push(1); push(2); push(3);
    for (unsigned i = 1; i <= 3; i++) {
        CHECK(i8042_read_data(&bus, &b));
        CHECK_EQ(b, i);
    }
    CHECK_EQ(data_reads, 3);
}

/* flush: no waiting; at most 64 bytes; stops when nothing is waiting. */
static void test_flush(void)
{
    reset();
    for (unsigned i = 0; i < 100; i++)
        push((uint8_t)i);
    i8042_flush(&bus);
    CHECK_EQ(data_reads, I8042_FLUSH_LIMIT);
    CHECK_EQ(status_reads, I8042_FLUSH_LIMIT);
    CHECK_EQ(qlen, 100 - I8042_FLUSH_LIMIT);

    reset();
    push(1); push(2); push(3);
    i8042_flush(&bus);
    CHECK_EQ(data_reads, 3);
    CHECK_EQ(status_reads, 4);
    CHECK_EQ(qlen, 0);

    reset();
    obf_gap = 2;                        /* a byte not yet there: not waited for */
    push(9);
    i8042_flush(&bus);
    CHECK_EQ(data_reads, 0);
    CHECK_EQ(status_reads, 1);
    CHECK_EQ(tick_calls, 0);
}

/* Port 2 not owned (the state at start, and until a mouse driver sets
 * it): a port 2 byte is read and discarded, as before 1.2. */
static void test_irq1_aux_not_owned(void)
{
    CHECK(!i8042_aux_owned());
    CHECK_EQ(i8042_irq1_action(0x00), I8042_IRQ1_NONE);
    CHECK_EQ(i8042_irq1_action(I8042_STATUS_AUX_DATA), I8042_IRQ1_NONE);
    CHECK_EQ(i8042_irq1_action(I8042_STATUS_OUTPUT_FULL), I8042_IRQ1_KEYBOARD);
    CHECK_EQ(i8042_irq1_action(0x15), I8042_IRQ1_KEYBOARD);     /* other bits do not matter */
    CHECK_EQ(i8042_irq1_action(0x21), I8042_IRQ1_DISCARD);
    CHECK_EQ(i8042_irq1_action(0xFF), I8042_IRQ1_DISCARD);
}

/* Port 2 owned: the byte is left unread for IRQ12; port 1 bytes and an
 * empty buffer unchanged; clearing the flag restores the discard. */
static void test_irq1_aux_owned(void)
{
    i8042_set_aux_owned(true);
    CHECK(i8042_aux_owned());
    CHECK_EQ(i8042_irq1_action(0x00), I8042_IRQ1_NONE);
    CHECK_EQ(i8042_irq1_action(I8042_STATUS_AUX_DATA), I8042_IRQ1_NONE);
    CHECK_EQ(i8042_irq1_action(I8042_STATUS_OUTPUT_FULL), I8042_IRQ1_KEYBOARD);
    CHECK_EQ(i8042_irq1_action(0x15), I8042_IRQ1_KEYBOARD);
    CHECK_EQ(i8042_irq1_action(0x21), I8042_IRQ1_LEAVE);
    CHECK_EQ(i8042_irq1_action(0xFF), I8042_IRQ1_LEAVE);
    i8042_set_aux_owned(false);
    CHECK(!i8042_aux_owned());
    CHECK_EQ(i8042_irq1_action(0x21), I8042_IRQ1_DISCARD);
}

int main(void)
{
    RUN(test_irq1_aux_not_owned);       /* first: the state at start */
    RUN(test_irq1_aux_owned);
    RUN(test_constants);
    RUN(test_begin);
    RUN(test_command_at_once);
    RUN(test_command_after_busy);
    RUN(test_data_write);
    RUN(test_timed_timeout);
    RUN(test_untimed_spin_limit);
    RUN(test_clock_stops);
    RUN(test_read);
    RUN(test_shared_wait);
    RUN(test_flush);
    return unit_report("i8042");
}
