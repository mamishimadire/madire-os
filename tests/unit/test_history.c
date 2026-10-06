/* Unit tests for kernel/console/history.c (command history, 0.9.1). */
#include <stdio.h>
#include <string.h>
#include "kernel/console/history.h"
#include "kernel/console/lineedit.h"
#include "tests/unit/unit.h"

static struct history h;

static int same(const char *a, const char *b)
{
    return a && b && strcmp(a, b) == 0;
}

static void test_empty(void)
{
    history_init(&h);
    CHECK_EQ(history_size(&h), 0);
    CHECK_EQ(history_first(&h), 0);
    CHECK(history_get(&h, 0) == NULL);
    CHECK(history_get(&h, 1) == NULL);
    CHECK(history_up(&h) == NULL);          /* nothing to recall */
    CHECK(history_down(&h) == NULL);
    CHECK_EQ(HISTORY_LINE_MAX, LINE_MAX);   /* same limit as the line editor */
}

static void test_add_and_number(void)
{
    history_init(&h);
    CHECK(history_add(&h, "help"));
    CHECK(history_add(&h, "mem"));
    CHECK(history_add(&h, "mem"));          /* repeats are kept */
    CHECK_EQ(history_size(&h), 3);
    CHECK_EQ(history_first(&h), 1);
    CHECK(same(history_get(&h, 1), "help"));
    CHECK(same(history_get(&h, 2), "mem"));
    CHECK(same(history_get(&h, 3), "mem"));
    CHECK(history_get(&h, 4) == NULL);
}

static void test_blank_lines_ignored(void)
{
    history_init(&h);
    CHECK(!history_add(&h, ""));
    CHECK(!history_add(&h, "   "));
    CHECK(!history_add(&h, " \t \t"));
    CHECK(!history_add(&h, NULL));
    CHECK_EQ(history_size(&h), 0);
    CHECK(history_add(&h, "  echo  a  "));  /* kept exactly as typed */
    CHECK(same(history_get(&h, 1), "  echo  a  "));
}

static void test_long_line_cut(void)
{
    char longline[100];
    memset(longline, 'a', sizeof(longline) - 1);
    longline[sizeof(longline) - 1] = '\0';
    history_init(&h);
    CHECK(history_add(&h, longline));
    CHECK_EQ(strlen(history_get(&h, 1)), HISTORY_LINE_MAX);
    longline[HISTORY_LINE_MAX] = '\0';      /* exactly the limit: unchanged */
    CHECK(history_add(&h, longline));
    CHECK(same(history_get(&h, 2), longline));
}

static void test_wrap_keeps_numbering(void)
{
    char buf[16];
    history_init(&h);
    for (int i = 1; i <= 40; i++) {
        snprintf(buf, sizeof(buf), "echo %d", i);
        CHECK(history_add(&h, buf));
    }
    CHECK_EQ(history_size(&h), HISTORY_MAX);
    CHECK_EQ(history_first(&h), 40 - HISTORY_MAX + 1);
    CHECK(history_get(&h, 40 - HISTORY_MAX) == NULL);      /* dropped */
    for (uint32_t n = history_first(&h); n <= 40; n++) {
        snprintf(buf, sizeof(buf), "echo %u", n);
        CHECK(same(history_get(&h, n), buf));
    }
    /* The 17th line after a fresh start drops exactly the first. */
    history_init(&h);
    for (int i = 1; i <= HISTORY_MAX + 1; i++) {
        snprintf(buf, sizeof(buf), "c%d", i);
        history_add(&h, buf);
    }
    CHECK(history_get(&h, 1) == NULL);
    CHECK(same(history_get(&h, 2), "c2"));
    CHECK(same(history_get(&h, HISTORY_MAX + 1), "c17"));
}

static void test_browse(void)
{
    history_init(&h);
    history_add(&h, "one");
    history_add(&h, "two");
    history_add(&h, "three");
    CHECK(history_down(&h) == NULL);        /* not browsing: Down does nothing */
    CHECK(same(history_up(&h), "three"));   /* Up starts at the newest */
    CHECK(same(history_up(&h), "two"));
    CHECK(same(history_up(&h), "one"));
    CHECK(history_up(&h) == NULL);          /* at the oldest: stays */
    CHECK(history_up(&h) == NULL);
    CHECK(same(history_down(&h), "two"));
    CHECK(same(history_down(&h), "three"));
    CHECK(same(history_down(&h), ""));      /* past the newest: empty line */
    CHECK(history_down(&h) == NULL);        /* browsing ended */
    CHECK(same(history_up(&h), "three"));   /* starts again at the newest */
}

static void test_add_or_end_stops_browsing(void)
{
    history_init(&h);
    history_add(&h, "a");
    history_add(&h, "b");
    CHECK(same(history_up(&h), "b"));
    CHECK(same(history_up(&h), "a"));
    CHECK(history_add(&h, "c"));
    CHECK(same(history_up(&h), "c"));       /* a fresh start at the newest */
    history_end_browse(&h);
    CHECK(history_down(&h) == NULL);
    CHECK(same(history_up(&h), "c"));
    CHECK(!history_add(&h, " "));           /* a blank submit also ends it */
    CHECK(history_down(&h) == NULL);
}

static void test_browse_after_wrap(void)
{
    char buf[16];
    history_init(&h);
    for (int i = 1; i <= 20; i++) {
        snprintf(buf, sizeof(buf), "l%d", i);
        history_add(&h, buf);
    }
    int ups = 0;
    const char *last = NULL, *p;
    while ((p = history_up(&h)) != NULL) {
        last = p;
        ups++;
    }
    CHECK_EQ(ups, HISTORY_MAX);             /* only the kept lines */
    CHECK(same(last, "l5"));                /* the oldest kept: 20 - 16 + 1 */
}

int main(void)
{
    printf("history unit tests\n");
    RUN(test_empty);
    RUN(test_add_and_number);
    RUN(test_blank_lines_ignored);
    RUN(test_long_line_cut);
    RUN(test_wrap_keeps_numbering);
    RUN(test_browse);
    RUN(test_add_or_end_stops_browsing);
    RUN(test_browse_after_wrap);
    return unit_report("history");
}
