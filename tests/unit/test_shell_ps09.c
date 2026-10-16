/* Unit tests for the 0.9 "ps" command of kernel/shell/shell.c. The 0.5
 * output (help included) is pinned by test_shell.c; without the thread
 * fields it is unchanged and "ps" does not exist. */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "kernel/shell/shell.h"
#include "tests/unit/unit.h"

static char out[4096];
static int errors;
static struct memory_stats mem;
static struct cpu_info cpu;
static struct shell_context ctx;

static void capture(void *c, const char *text, enum shell_text kind)
{
    (void)c;
    if (kind == SHELL_ERROR)
        errors++;
    strncat(out, text, sizeof(out) - strlen(out) - 1);
}

static const struct shell_output sink = { capture, NULL };

static enum shell_result run(const char *cmd)
{
    static char line[80];
    strncpy(line, cmd, sizeof(line) - 1);
    line[sizeof(line) - 1] = '\0';
    out[0] = '\0';
    errors = 0;
    return shell_execute(line, &ctx, &sink);
}

static void reset(void)
{
    memset(&mem, 0, sizeof(mem));
    memset(&cpu, 0, sizeof(cpu));
    memset(&ctx, 0, sizeof(ctx));
    ctx.version = "0.9";
    ctx.mem = &mem;
    ctx.cpu = &cpu;
    ctx.page_size = 4096;
}

/* 1.0 stage 10 (D2): "help" lists the names, several per line. */
static const char *const help_07 =
    "Available commands:\n"
    "  help  clear  version  uptime  mem  cpu  echo\n";

static void test_without_threads_nothing_changes(void)
{
    reset();
    CHECK_EQ(run("help"), SHELL_OK);
    CHECK(strcmp(out, help_07) == 0);
    CHECK_EQ(run("ps"), SHELL_UNKNOWN);
    CHECK(strcmp(out, "Unknown command: ps. Type 'help' for a list of commands.\n") == 0);
    CHECK_EQ(errors, 1);
}

static void test_help_lists_ps_last(void)
{
    static const struct shell_thread one[] = { { 0, "running", 5, 1480, 16384, "kernel" } };
    reset();
    ctx.threads = one;
    ctx.thread_count = 1;
    CHECK_EQ(run("help"), SHELL_OK);
    char want[1024];
    snprintf(want, sizeof(want), "Available commands:\n  help  clear  version  uptime  mem  cpu  echo  ps\n");
    CHECK(strcmp(out, want) == 0);
    CHECK_EQ(errors, 0);
}

static void test_ps_exact(void)
{
    static const struct shell_thread rows[] = {
        { 0, "running", 12345, 1480, 16384, "kernel" },
        { 1, "ready", 100, 320, 16384, "spin0" },
        { 2, "blocked", 0, 176, 16384, "sleeper" },
    };
    reset();
    ctx.threads = rows;
    ctx.thread_count = 3;
    CHECK_EQ(run("ps"), SHELL_OK);
    const char *want =
        "TID  STATE      TICKS    STACK PEAK    NAME\n"
        "  0  running    12345     1480/16384   kernel\n"
        "  1  ready        100      320/16384   spin0\n"
        "  2  blocked        0      176/16384   sleeper\n"
        "3 threads\n";
    CHECK(strcmp(out, want) == 0);
    if (strcmp(out, want) != 0)
        printf("%s", out);
    CHECK_EQ(errors, 0);
}

static void test_ps_edges(void)
{
    static const struct shell_thread rows[] = {
        { 63, "zombie", 0xFFFFFFFFull, 16384, 16384, "fifteen-chars-x" },
        { 7, "new", 0x100000000ull, 0, 16384, "" },
    };
    reset();
    ctx.threads = rows;
    ctx.thread_count = 1;
    CHECK_EQ(run("ps"), SHELL_OK);
    /* A 10-digit tick count fills its column; one space before the peak. */
    CHECK(strstr(out, " 63  zombie   4294967295 16384/16384   fifteen-chars-x\n") != NULL);
    CHECK(strstr(out, "\n1 thread\n") != NULL);                  /* singular */
    ctx.thread_count = 2;
    CHECK_EQ(run("ps"), SHELL_OK);
    CHECK(strstr(out, "  7  new        large        0/16384   \n") != NULL);   /* > 32 bits */
    ctx.thread_count = 0;                                       /* header and count */
    CHECK_EQ(run("ps"), SHELL_OK);
    CHECK(strcmp(out, "TID  STATE      TICKS    STACK PEAK    NAME\n0 threads\n") == 0);
    CHECK_EQ(run("ps x"), SHELL_BAD_ARGS);
    CHECK(strcmp(out, "Usage: ps\n") == 0);
    CHECK_EQ(errors, 1);
}

static void test_every_line_fits(void)
{
    static const struct shell_thread rows[] = {
        { 63, "running", 0xFFFFFFFFull, 16384, 16384, "fifteen-chars-x" },
    };
    reset();
    ctx.threads = rows;
    ctx.thread_count = 1;
    CHECK_EQ(run("ps"), SHELL_OK);
    unsigned longest = 0, n = 0;
    for (const char *p = out; *p; p++) {
        if (*p == '\n') {
            if (n > longest)
                longest = n;
            n = 0;
        } else {
            n++;
        }
    }
    CHECK(longest <= 79);
}

int main(void)
{
    printf("shell ps (0.9) unit tests\n");
    RUN(test_without_threads_nothing_changes);
    RUN(test_help_lists_ps_last);
    RUN(test_ps_exact);
    RUN(test_ps_edges);
    RUN(test_every_line_fits);
    return unit_report("shell_ps09");
}
