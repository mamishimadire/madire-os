/* Unit tests for 1.5 stage 6 of kernel/shell/shell.c: "poweroff"
 * (docs/plans/1.5.md, C3 and C6b; MOCS v0.13). With the context's power
 * flag, "poweroff" prints "Power off now? Type 'yes' to confirm:" and
 * answers SHELL_POWEROFF (the console then powers off only on exactly
 * "yes", shell_reboot_confirmed's rule); "poweroff -y" asks nothing and
 * answers SHELL_POWEROFF_NOW; any other argument is a usage error; "help
 * poweroff" and "poweroff --help" show its purpose and usage; "help" lists
 * it in the section "system" after "reboot" (43 commands with display,
 * sync and disk), still on one screen; an alias may not take its name.
 * Without the flag "poweroff" is an unknown command and "help" prints
 * exactly what it did before. */
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
static struct shell_display disp;
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

static void reset(bool power)
{
    memset(&mem, 0, sizeof(mem));
    memset(&cpu, 0, sizeof(cpu));
    memset(&ctx, 0, sizeof(ctx));
    memset(&extras, 0, sizeof(extras));
    memset(&files, 0, sizeof(files));
    memset(&storage, 0, sizeof(storage));
    memset(&disp, 0, sizeof(disp));
    history_init(&hist);
    ctx.version = "1.5";
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
    ctx.display = &disp;
    ctx.power = power;
}

static void test_not_offered(void)
{
    reset(false);
    CHECK_EQ(run("poweroff"), SHELL_UNKNOWN);
    CHECK(strstr(out, "Unknown command: poweroff.") != NULL);
    CHECK(strstr(out, "Power off now?") == NULL);
    CHECK_EQ(run("poweroff -y"), SHELL_UNKNOWN);
}

static void test_question_and_y(void)
{
    reset(true);
    CHECK_EQ(run("poweroff"), SHELL_POWEROFF);
    CHECK(strcmp(out, "Power off now? Type 'yes' to confirm:\n") == 0);
    CHECK_EQ(errors, 0);
    CHECK_EQ(run("poweroff -y"), SHELL_POWEROFF_NOW);
    CHECK(strcmp(out, "") == 0);
    CHECK_EQ(run("poweroff -x"), SHELL_BAD_ARGS);
    CHECK(strcmp(out, "Usage: poweroff [-y]\n") == 0);
    CHECK_EQ(errors, 1);
    CHECK_EQ(run("poweroff -y now"), SHELL_BAD_ARGS);  /* one argument at most */
    CHECK(strstr(out, "Power off now?") == NULL);
    CHECK(strcmp(SHELL_POWEROFF_CANCELLED, "Power off cancelled.") == 0);
}

/* The answer: exactly "yes" (spaces and tabs around it allowed), as reboot's. */
static void test_answer_rule(void)
{
    CHECK(shell_reboot_confirmed("yes"));
    CHECK(shell_reboot_confirmed("  yes\t"));
    CHECK(!shell_reboot_confirmed(""));
    CHECK(!shell_reboot_confirmed("y"));
    CHECK(!shell_reboot_confirmed("YES"));
    CHECK(!shell_reboot_confirmed("yes please"));
    CHECK(!shell_reboot_confirmed("no"));
}

static void test_help_topic(void)
{
    reset(true);
    CHECK_EQ(run("help poweroff"), SHELL_OK);
    CHECK(strcmp(out, "poweroff: Power off the computer (asks first)\nUsage: poweroff [-y]\n") == 0);
    CHECK_EQ(run("poweroff --help"), SHELL_OK);
    CHECK(strcmp(out, "poweroff: Power off the computer (asks first)\nUsage: poweroff [-y]\n") == 0);
    reset(false);
    CHECK_EQ(run("help poweroff"), SHELL_UNKNOWN);   /* as "help nosuch" since 0.9.1 */
    CHECK(strncmp(out, "No help for 'poweroff': unknown command.\n", 41) == 0);
}

/* The help lists: "poweroff" after "reboot" in "system", once; "help"
 * still fits one screen; without the flag both lists are unchanged. */
static void test_help_lists(void)
{
    static char without[8192], with[8192], sys_without[2048], sys_with[2048];
    reset(false);
    CHECK_EQ(run("help"), SHELL_OK);
    strcpy(without, out);
    CHECK_EQ(run("help system"), SHELL_OK);
    strcpy(sys_without, out);
    CHECK(strstr(without, "poweroff") == NULL && strstr(sys_without, "poweroff") == NULL);
    CHECK(strstr(without, "  version  uptime  mem  cpu  uname  hostname  date  dmesg  reboot  display\n") != NULL);
    reset(true);
    CHECK_EQ(run("help"), SHELL_OK);
    strcpy(with, out);
    CHECK(strstr(with, "  version  uptime  mem  cpu  uname  hostname  date  dmesg  reboot  poweroff\n"
                       "  display\n") != NULL);
    CHECK_EQ(run("help system"), SHELL_OK);
    strcpy(sys_with, out);
    CHECK(strstr(sys_with, "  reboot   Restart the computer (asks first)\n"
                           "  poweroff Power off the computer (asks first)\n"
                           "  display  Show the screen's mode and scale\n") != NULL);
    /* One screen: at most 23 lines, none above 79 columns; poweroff once. */
    int lines = 0, col = 0, widest = 0, seen = 0;
    for (const char *p = with; *p; p++) {
        if (*p == '\n') {
            lines++;
            col = 0;
        } else if (++col > widest) {
            widest = col;
        }
    }
    for (const char *p = with; (p = strstr(p, "poweroff")) != NULL; p++)
        seen++;
    CHECK(lines <= 23);
    CHECK(widest <= 79);
    CHECK_EQ(seen, 1);
    /* "help" with the flag differs from "help" without it only by the
     * system section's two lines. */
    char *a = strstr(without, "  version  uptime"), *b = strstr(with, "  version  uptime");
    CHECK(a != NULL && b != NULL);
    if (a && b) {
        CHECK_EQ((size_t)(a - without), (size_t)(b - with));
        CHECK(strncmp(without, with, (size_t)(a - without)) == 0);
        const char *ra = strstr(a, "programs:"), *rb = strstr(b, "programs:");
        CHECK(ra != NULL && rb != NULL && strcmp(ra, rb) == 0);
    }
}

static void test_alias_and_suggestion(void)
{
    reset(true);
    CHECK_EQ(run("alias poweroff echo x"), SHELL_BAD_ARGS);
    CHECK(strcmp(out, "alias: 'poweroff' is a built-in command.\n") == 0);
    CHECK_EQ(errors, 1);
    CHECK_EQ(run("alias po poweroff"), SHELL_OK);       /* an alias may run it */
    CHECK_EQ(run("po"), SHELL_POWEROFF);
    CHECK(strcmp(out, "Power off now? Type 'yes' to confirm:\n") == 0);
    CHECK_EQ(run("poweof"), SHELL_UNKNOWN);
    CHECK(strstr(out, "Did you mean: poweroff?") != NULL);
}

int main(void)
{
    printf("shell_power15 unit tests\n");
    RUN(test_not_offered);
    RUN(test_question_and_y);
    RUN(test_answer_rule);
    RUN(test_help_topic);
    RUN(test_help_lists);
    RUN(test_alias_and_suggestion);
    return unit_report("shell_power15");
}
