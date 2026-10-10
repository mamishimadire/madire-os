/* Unit tests for the 0.11 longest-turn record (sched_longest_reset,
 * sched_longest_note; kernel/proc/sched_core.h). */
#include <stdint.h>
#include <string.h>
#include "kernel/proc/sched_core.h"
#include "tests/unit/unit.h"

static void test_reset(void)
{
    struct sched_longest l;
    memset(&l, 0x5A, sizeof(l));
    sched_longest_reset(&l);
    CHECK_EQ(l.ticks, 0);
    CHECK_EQ((uint32_t)l.tid, (uint32_t)-1);
    CHECK_EQ(l.pid, 0);
    CHECK(l.name[0] == '\0');
}

static void test_longer_replaces(void)
{
    struct sched_longest l;
    sched_longest_reset(&l);
    CHECK(sched_longest_note(&l, 5, 3, 12, false, "spin"));
    CHECK_EQ(l.ticks, 5);
    CHECK_EQ(l.tid, 3);
    CHECK_EQ(l.pid, 12);
    CHECK(strcmp(l.name, "spin") == 0);
    CHECK(sched_longest_note(&l, 10, 2, 0, false, "frames"));     /* longer: a kernel thread */
    CHECK_EQ(l.ticks, 10);
    CHECK_EQ(l.tid, 2);
    CHECK_EQ(l.pid, 0);
    CHECK(strcmp(l.name, "frames") == 0);
}

static void test_shorter_and_tie_keep_the_first(void)
{
    struct sched_longest l;
    sched_longest_reset(&l);
    CHECK(sched_longest_note(&l, 7, 4, 9, false, "first"));
    CHECK(!sched_longest_note(&l, 7, 5, 10, false, "tie"));        /* a tie keeps the first */
    CHECK(!sched_longest_note(&l, 6, 6, 11, false, "shorter"));
    CHECK(!sched_longest_note(&l, 0, 1, 1, false, "zero"));
    CHECK_EQ(l.ticks, 7);
    CHECK_EQ(l.tid, 4);
    CHECK_EQ(l.pid, 9);
    CHECK(strcmp(l.name, "first") == 0);
}

static void test_zero_is_never_a_turn(void)
{
    struct sched_longest l;
    sched_longest_reset(&l);
    CHECK(!sched_longest_note(&l, 0, 1, 0, false, "idle"));
    CHECK_EQ((uint32_t)l.tid, (uint32_t)-1);               /* still "no thread" */
    CHECK(sched_longest_note(&l, 1, 1, 0, false, "one"));
    CHECK_EQ(l.ticks, 1);
}

static void test_names_and_pids(void)
{
    struct sched_longest l;
    sched_longest_reset(&l);
    CHECK(sched_longest_note(&l, 1, 1, -5, false, NULL));  /* no name; pid < 0: no process */
    CHECK(l.name[0] == '\0');
    CHECK_EQ(l.pid, 0);
    CHECK(sched_longest_note(&l, 2, 63, 2147483647, false, "abcdefghijklmnopqrstuvwxyz"));
    CHECK_EQ(strlen(l.name), SCHED_LONGEST_NAME_MAX - 1);   /* cut to 15 characters */
    CHECK(strcmp(l.name, "abcdefghijklmno") == 0);
    CHECK_EQ(l.pid, 2147483647);
    CHECK_EQ(l.tid, 63);
    CHECK(sched_longest_note(&l, UINT64_MAX, 1, 1, false, "max"));  /* the largest count */
    CHECK_EQ(l.ticks, UINT64_MAX);
    CHECK(!sched_longest_note(&l, UINT64_MAX, 2, 2, false, "again"));
    CHECK(strcmp(l.name, "max") == 0);
    sched_longest_reset(&l);                                 /* and a reset clears it all */
    CHECK_EQ(l.ticks, 0);
    CHECK_EQ((uint32_t)l.tid, (uint32_t)-1);
    CHECK(l.name[0] == '\0');
}

static void test_own_space(void)
{
    struct sched_longest l;
    sched_longest_reset(&l);
    CHECK(!l.own_space);
    CHECK(sched_longest_note(&l, 3, 1, 0, true, "spin"));      /* ring 3, no process entry */
    CHECK(l.own_space);
    CHECK_EQ(l.pid, 0);
    CHECK(sched_longest_note(&l, 4, 2, 7, true, "hello"));     /* a process: pid kept as well */
    CHECK(l.own_space);
    CHECK_EQ(l.pid, 7);
    CHECK(sched_longest_note(&l, 5, 3, 0, false, "frames"));   /* a kernel thread clears it */
    CHECK(!l.own_space);
    CHECK(!sched_longest_note(&l, 5, 4, 9, true, "tie"));      /* a tie changes nothing */
    CHECK(!l.own_space);
    CHECK_EQ(l.tid, 3);
    sched_longest_reset(&l);
    CHECK(!l.own_space);
}

int main(void)
{
    RUN(test_reset);
    RUN(test_own_space);
    RUN(test_longer_replaces);
    RUN(test_shorter_and_tie_keep_the_first);
    RUN(test_zero_is_never_a_turn);
    RUN(test_names_and_pids);
    return unit_report("longest011");
}
