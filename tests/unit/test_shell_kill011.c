/* Unit tests for the 0.11 stage 4b "kill" command of kernel/shell/shell.c:
 * it asks first (the console ends the process only on "yes"), and refuses
 * PID 0, unknown and ended processes and bad arguments. */
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
static struct shell_extras extras;
static struct shell_procs procs;
static int asked;
static int32_t asked_pid;

static void ask_kill(void *c, int32_t pid)
{
    (void)c;
    asked++;
    asked_pid = pid;
}

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
    static char line[96];
    strncpy(line, cmd, sizeof(line) - 1);
    line[sizeof(line) - 1] = '\0';
    out[0] = '\0';
    errors = 0;
    asked = 0;
    asked_pid = -1;
    return shell_execute(line, &ctx, &sink);
}

static const struct shell_thread one_thread[] = { { 0, "running", 5, 1480, 16384, "kernel" } };
static const struct shell_process rows[] = {
    { 4, "running", 5, "spin" },
    { 9, "ended", 7, "hello" },
    { 2147483647, "running", 63, "fifteen-chars-x" },
};

static void reset(void)
{
    memset(&mem, 0, sizeof(mem));
    memset(&cpu, 0, sizeof(cpu));
    memset(&ctx, 0, sizeof(ctx));
    memset(&extras, 0, sizeof(extras));
    memset(&procs, 0, sizeof(procs));
    ctx.version = "0.11";
    ctx.mem = &mem;
    ctx.cpu = &cpu;
    ctx.page_size = 4096;
    ctx.threads = one_thread;
    ctx.thread_count = 1;
    ctx.extras = &extras;
    procs.rows = rows;
    procs.count = 3;
    procs.ask_kill = ask_kill;
    ctx.procs = &procs;
}

static void test_without_procs_kill_is_unknown(void)
{
    reset();
    ctx.procs = NULL;
    CHECK_EQ(run("kill 4"), SHELL_UNKNOWN);
    CHECK(strcmp(out, "Unknown command: kill. Type 'help' for a list of commands.\n") == 0);
    CHECK_EQ(asked, 0);
}

static void test_asks_first(void)
{
    reset();
    CHECK_EQ(run("kill 4"), SHELL_KILL);
    CHECK(strcmp(out, "End process 4 'spin'? Type 'yes' to confirm:\n") == 0);
    CHECK_EQ(errors, 0);
    CHECK_EQ(asked, 1);
    CHECK_EQ(asked_pid, 4);
    CHECK_EQ(run("kill 2147483647"), SHELL_KILL);
    CHECK(strcmp(out, "End process 2147483647 'fifteen-chars-x'? Type 'yes' to confirm:\n") == 0);
    CHECK(strlen(out) <= 80);
    CHECK_EQ(asked_pid, 2147483647);
}

static void test_refused(void)
{
    reset();
    CHECK_EQ(run("kill 0"), SHELL_BAD_ARGS);
    CHECK(strcmp(out, "kill: PID 0 is the kernel, not a process.\n") == 0);
    CHECK_EQ(run("kill 5"), SHELL_BAD_ARGS);
    CHECK(strcmp(out, "kill: no process with PID 5.\n") == 0);
    CHECK_EQ(run("kill 9"), SHELL_BAD_ARGS);
    CHECK(strcmp(out, "kill: process 9 'hello' has already ended.\n") == 0);
    CHECK_EQ(errors, 1);
    static const char *const bad[] = { "kill x", "kill -1", "kill 1x", "kill 2147483648",
                                       "kill 99999999999" };
    for (unsigned i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        CHECK_EQ(run(bad[i]), SHELL_BAD_ARGS);
        CHECK(strstr(out, "' is not a process ID.\n") != NULL);
        CHECK_EQ(asked, 0);
    }
    CHECK_EQ(run("kill"), SHELL_BAD_ARGS);
    CHECK(strcmp(out, "Usage: kill <pid>\n") == 0);
    CHECK_EQ(run("kill 4 5"), SHELL_BAD_ARGS);
    CHECK(strcmp(out, "Usage: kill <pid>\n") == 0);
    CHECK_EQ(asked, 0);
}

static void test_help(void)
{
    reset();
    CHECK_EQ(run("help kill"), SHELL_OK);
    CHECK(strcmp(out, "kill: End a process: kill <pid> (asks first)\nUsage: kill <pid>\n") == 0);
    CHECK_EQ(run("kill --help"), SHELL_OK);
    CHECK(strcmp(out, "kill: End a process: kill <pid> (asks first)\nUsage: kill <pid>\n") == 0);
    CHECK_EQ(asked, 0);
}

int main(void)
{
    printf("shell kill (0.11) unit tests\n");
    RUN(test_without_procs_kill_is_unknown);
    RUN(test_asks_first);
    RUN(test_refused);
    RUN(test_help);
    return unit_report("shell_kill011");
}
