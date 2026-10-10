/* Unit tests for the 0.11 "run" command, the processes in "ps" and the
 * end-of-program line (shell_end_line) of kernel/shell/shell.c. Without
 * the procs field every output stays as before (test_shell_ps09.c and
 * test_shell_091.c pin it). */
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

/* The fake start: records the call, returns `next_pid`. */
static int32_t next_pid;
static int starts;
static char started_name[64];
static uint64_t started_arg;
static bool started_bg;

static int32_t fake_start(void *c, const char *name, uint64_t arg, bool background)
{
    (void)c;
    starts++;
    strncpy(started_name, name, sizeof(started_name) - 1);
    started_arg = arg;
    started_bg = background;
    return strcmp(name, "nosuch") == 0 ? SHELL_RUN_NO_PROGRAM : next_pid;
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
    starts = 0;
    started_name[0] = '\0';
    started_arg = 0xDEAD;
    started_bg = false;
    return shell_execute(line, &ctx, &sink);
}

static const struct shell_thread one_thread[] = { { 0, "running", 5, 1480, 16384, "kernel" } };

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
    procs.start = fake_start;
    ctx.procs = &procs;
    next_pid = 7;
}

static void test_without_procs_nothing_changes(void)
{
    reset();
    ctx.procs = NULL;
    CHECK_EQ(run("run hello"), SHELL_UNKNOWN);
    CHECK(strcmp(out, "Unknown command: run. Type 'help' for a list of commands.\n") == 0);
    CHECK_EQ(starts, 0);
    CHECK_EQ(run("help"), SHELL_OK);
    CHECK(strstr(out, "  ps       Show threads\n") != NULL);
    CHECK(strstr(out, "run") == NULL);
    CHECK_EQ(run("ps"), SHELL_OK);
    CHECK(strcmp(out, "TID  STATE      TICKS    STACK PEAK    NAME\n"
                      "  0  running        5     1480/16384   kernel\n"
                      "1 thread\n") == 0);
}

static void test_help(void)
{
    reset();
    CHECK_EQ(run("help"), SHELL_OK);
    CHECK(strstr(out, "  echo     Print text: echo <text>\n"
                      "  ps       Show threads and processes\n"
                      "  history  Show previous commands (Up/Down recall)\n") != NULL);
    /* run follows reboot (stage 4b: then kill, the last) */
    const char *tail = "  reboot   Restart the computer (asks first)\n"
                       "  run      Run a program: run [-b] <name> [n]\n"
                       "  kill     End a process: kill <pid> (asks first)\n";
    CHECK(strlen(out) >= strlen(tail) && strcmp(out + strlen(out) - strlen(tail), tail) == 0);
    CHECK_EQ(run("help run"), SHELL_OK);
    CHECK(strcmp(out, "run: Run a program: run [-b] <name> [n]\n"
                      "Usage: run [-b] <program> [number]\n") == 0);
    CHECK_EQ(run("run --help"), SHELL_OK);
    CHECK(strcmp(out, "run: Run a program: run [-b] <name> [n]\n"
                      "Usage: run [-b] <program> [number]\n") == 0);
    CHECK_EQ(starts, 0);
    CHECK_EQ(run("ps --help"), SHELL_OK);
    CHECK(strcmp(out, "ps: Show threads and processes\nUsage: ps\n") == 0);
}

static void test_run_foreground(void)
{
    reset();
    CHECK_EQ(run("run hello"), SHELL_RUN);
    CHECK_EQ(starts, 1);
    CHECK(strcmp(started_name, "hello") == 0);
    CHECK_EQ(started_arg, 0);
    CHECK(!started_bg);
    CHECK(strcmp(out, "") == 0);                    /* nothing until it ends */
    CHECK_EQ(run("run exitcode 3"), SHELL_RUN);
    CHECK_EQ(started_arg, 3);
    CHECK_EQ(run("run exitcode 4294967295"), SHELL_RUN);
    CHECK_EQ(started_arg, 4294967295ull);
    CHECK_EQ(run("  run   spin   0  "), SHELL_RUN);
    CHECK_EQ(started_arg, 0);
}

static void test_run_background(void)
{
    reset();
    next_pid = 12;
    CHECK_EQ(run("run -b spin"), SHELL_OK);
    CHECK(started_bg);
    CHECK(strcmp(started_name, "spin") == 0);
    CHECK(strcmp(out, "[process 12 'spin' started in the background]\n") == 0);
    CHECK_EQ(errors, 0);
    CHECK_EQ(run("run -b spin 2"), SHELL_OK);
    CHECK_EQ(started_arg, 2);
}

static void test_run_refused(void)
{
    reset();
    CHECK_EQ(run("run nosuch"), SHELL_BAD_ARGS);
    CHECK(strcmp(out, "run: no program named 'nosuch'.\n") == 0);
    CHECK_EQ(errors, 1);
    next_pid = SHELL_RUN_FAILED;
    CHECK_EQ(run("run hello"), SHELL_BAD_ARGS);
    CHECK(strcmp(out, "run: could not start 'hello' (no free process slot).\n") == 0);
    next_pid = 7;
    static const char *const bad_args[] = {
        "run hello x", "run hello -1", "run hello 4294967296", "run hello 1x", "run hello 0x10",
        "run hello 99999999999999999999",
    };
    for (unsigned i = 0; i < sizeof(bad_args) / sizeof(bad_args[0]); i++) {
        CHECK_EQ(run(bad_args[i]), SHELL_BAD_ARGS);
        CHECK_EQ(starts, 0);
        CHECK_EQ(errors, 1);
        CHECK(strncmp(out, "run: '", 6) == 0);
        CHECK(strstr(out, "' is not a number from 0 to 4294967295.\n") != NULL);
    }
    static const char *const usage[] = {
        "run", "run -b", "run -x", "run hello 1 2", "run -b hello 1 2", "run -b -b hello",
    };
    for (unsigned i = 0; i < sizeof(usage) / sizeof(usage[0]); i++) {
        CHECK_EQ(run(usage[i]), SHELL_BAD_ARGS);
        CHECK_EQ(starts, 0);
        CHECK(strcmp(out, "Usage: run [-b] <program> [number]\n") == 0);
    }
    /* A long name is shortened in the message, so the line fits. */
    CHECK_EQ(run("run nosuch"), SHELL_BAD_ARGS);
    CHECK_EQ(run("run abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz 1x"), SHELL_BAD_ARGS);
    CHECK(strlen(out) <= 80);
}

static void test_ps_processes(void)
{
    static const struct shell_process rows[] = {
        { 4, "running", 5, "spin" },
        { 63, "ended", 63, "fifteen-chars-x" },
    };
    reset();
    procs.rows = rows;
    procs.count = 2;
    CHECK_EQ(run("ps"), SHELL_OK);
    const char *want =
        "TID  STATE      TICKS    STACK PEAK    NAME\n"
        "  0  running        5     1480/16384   kernel\n"
        "1 thread\n"
        "PID  STATE    TID  NAME\n"
        "  4  running    5  spin\n"
        " 63  ended     63  fifteen-chars-x\n"
        "2 processes\n";
    CHECK(strcmp(out, want) == 0);
    if (strcmp(out, want) != 0)
        printf("%s", out);
    procs.count = 1;
    CHECK_EQ(run("ps"), SHELL_OK);
    CHECK(strstr(out, "\n1 process\n") != NULL);
    procs.count = 0;
    CHECK_EQ(run("ps"), SHELL_OK);
    CHECK(strstr(out, "1 thread\nPID  STATE    TID  NAME\n0 processes\n") != NULL);
}

static void test_end_line(void)
{
    char line[SHELL_END_LINE_MAX];
    shell_end_line(line, 3, "hello", SHELL_END_EXIT, 0, NULL, 0);
    CHECK(strcmp(line, "[process 3 'hello' exited, status 0]") == 0);
    shell_end_line(line, 4, "exitcode", SHELL_END_EXIT, 42, NULL, 0);
    CHECK(strcmp(line, "[process 4 'exitcode' exited, status 42]") == 0);
    shell_end_line(line, 5, "spin", SHELL_END_KILLED, 0, NULL, 0);
    CHECK(strcmp(line, "[process 5 'spin' killed]") == 0);
    shell_end_line(line, 6, "nullptr", SHELL_END_FAULT, 14, "#PF", 0);
    CHECK(strcmp(line, "[process 6 'nullptr' ended by #PF at 0x0000000000000000]") == 0);
    shell_end_line(line, 7, "ud2", SHELL_END_FAULT, 6, "#UD", 0x401234);
    CHECK(strcmp(line, "[process 7 'ud2' ended by #UD at 0x0000000000401234]") == 0);
    shell_end_line(line, 8, "big", SHELL_END_FAULT, -1, NULL, 0);
    CHECK(strcmp(line, "[process 8 'big' could not be loaded (no memory)]") == 0);
    shell_end_line(line, 9, "bad", SHELL_END_LOAD_FAILED, 3, NULL, 0);
    CHECK(strcmp(line, "[process 9 'bad' load failed]") == 0);
    /* The widest: a 10-digit PID, a 15-character name, a fault address. */
    shell_end_line(line, 2147483647, "fifteen-chars-x", SHELL_END_FAULT, 14, "#PF",
                   0xFFFFFFFFFFFFFFFFull);
    CHECK(strcmp(line, "[process 2147483647 'fifteen-chars-x' ended by #PF at 0xffffffffffffffff]")
          == 0);
    CHECK(strlen(line) <= 79);
}

static void test_every_line_fits(void)
{
    static const struct shell_process rows[] = { { 2147483647, "running", 63, "fifteen-chars-x" } };
    reset();
    procs.rows = rows;
    procs.count = 1;
    static const char *const cmds[] = { "ps", "help", "help run", "run -b spin", "run nosuch",
                                        "run hello 1x" };
    for (unsigned c = 0; c < sizeof(cmds) / sizeof(cmds[0]); c++) {
        run(cmds[c]);
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
}

int main(void)
{
    printf("shell run and processes (0.11) unit tests\n");
    RUN(test_without_procs_nothing_changes);
    RUN(test_help);
    RUN(test_run_foreground);
    RUN(test_run_background);
    RUN(test_run_refused);
    RUN(test_ps_processes);
    RUN(test_end_line);
    RUN(test_every_line_fits);
    return unit_report("shell_run011");
}
