/* Unit tests for 1.4 stage 10 of kernel/shell/shell.c: "sync" (docs/plans/
 * 1.4.md, C12, D19). With the context's disk part, "sync" calls its sync
 * once and prints one line for each outcome ("N blocks saved", "1 block
 * saved", "nothing to save", Live mode, read-only, a failed write as an
 * error); it takes no argument; "help sync" and "sync --help" show its
 * purpose. It is not in the help lists until stage 12 (the owner's
 * decision): "help" and "help files" print exactly the same with and
 * without the disk part. Without it "sync" is an unknown command, as
 * before. */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "kernel/shell/shell.h"
#include "tests/unit/unit.h"

static char out[8192];
static int errors;
static struct memory_stats mem;
static struct cpu_info cpu;
static struct shell_context ctx;
static struct shell_extras extras;
static struct shell_state st;
static struct shell_session ss;
static struct shell_files files;
static struct shell_storage storage;
static struct shell_disk disk;
static struct history hist;

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
    static char line[512];
    strncpy(line, cmd, sizeof(line) - 1);
    line[sizeof(line) - 1] = '\0';
    out[0] = '\0';
    errors = 0;
    return shell_execute(line, &ctx, &sink);
}

static bool fake_removed(void *c) { (void)c; return false; }

static enum shell_sync_end next_end;
static uint64_t next_blocks;
static unsigned sync_calls;

static enum shell_sync_end fake_sync(void *c, uint64_t *blocks)
{
    (void)c;
    sync_calls++;
    *blocks = next_blocks;
    return next_end;
}

static void reset(bool with_disk)
{
    memset(&mem, 0, sizeof(mem));
    memset(&cpu, 0, sizeof(cpu));
    memset(&ctx, 0, sizeof(ctx));
    memset(&extras, 0, sizeof(extras));
    memset(&files, 0, sizeof(files));
    memset(&storage, 0, sizeof(storage));
    history_init(&hist);
    ctx.version = "1.4";
    ctx.mem = &mem;
    ctx.cpu = &cpu;
    ctx.page_size = 4096;
    extras.hostname = "madire";
    extras.history = &hist;
    ctx.extras = &extras;
    shell_state_init(&st);
    ctx.state = &st;
    shell_session_init(&ss);
    ctx.session = &ss;
    files.cwd_removed = fake_removed;
    ctx.files = &files;
    ctx.storage = &storage;
    disk.sync = fake_sync;
    disk.ctx = NULL;
    ctx.disk = with_disk ? &disk : NULL;
    sync_calls = 0;
}

static void expect(enum shell_sync_end end, uint64_t blocks, const char *line, int errs)
{
    next_end = end;
    next_blocks = blocks;
    unsigned calls = sync_calls;
    CHECK_EQ(run("sync"), SHELL_OK);
    CHECK_EQ(sync_calls, calls + 1);
    CHECK(strcmp(out, line) == 0);
    CHECK_EQ(errors, errs);
}

static void test_lines(void)
{
    reset(true);
    expect(SHELL_SYNC_SAVED, 5, "sync: 5 blocks saved.\n", 0);
    expect(SHELL_SYNC_SAVED, 1, "sync: 1 block saved.\n", 0);
    expect(SHELL_SYNC_SAVED, 0, "sync: nothing to save.\n", 0);
    expect(SHELL_SYNC_NO_DISK, 0, "sync: no disk in use (Live mode).\n", 0);
    expect(SHELL_SYNC_READONLY, 0, "sync: /home is read-only; nothing to save.\n", 0);
    expect(SHELL_SYNC_FAILED, 3, "sync: the disk could not be written; /home is read-only now.\n", 1);
    unsigned calls = sync_calls;
    CHECK_EQ(run("sync now"), SHELL_BAD_ARGS);          /* no argument */
    CHECK_EQ(sync_calls, calls);
}

static void test_help(void)
{
    static char without[8192], with[8192];
    /* "help" and "help files": the same text with and without "sync". */
    reset(false);
    CHECK_EQ(run("help"), SHELL_OK);
    snprintf(without, sizeof(without), "%s", out);
    reset(true);
    CHECK_EQ(run("help"), SHELL_OK);
    snprintf(with, sizeof(with), "%s", out);
    CHECK(strcmp(with, without) == 0);
    CHECK(strstr(with, "sync") == NULL);
    reset(false);
    CHECK_EQ(run("help files"), SHELL_OK);
    snprintf(without, sizeof(without), "%s", out);
    reset(true);
    CHECK_EQ(run("help files"), SHELL_OK);
    CHECK(strcmp(out, without) == 0);
    CHECK(strstr(out, "sync") == NULL);
    /* Its own help. */
    CHECK_EQ(run("help sync"), SHELL_OK);
    CHECK(strstr(out, "Save changed files to the disk") != NULL);
    CHECK_EQ(run("sync --help"), SHELL_OK);
    CHECK(strstr(out, "Save changed files to the disk") != NULL);
    CHECK_EQ(sync_calls, 0);                            /* help does not save */
    /* Without the disk part: unknown, as before. */
    reset(false);
    CHECK_EQ(run("sync"), SHELL_UNKNOWN);
    CHECK_EQ(sync_calls, 0);
}

int main(void)
{
    RUN(test_lines);
    RUN(test_help);
    return unit_report("shell_sync");
}
