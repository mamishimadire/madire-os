/* Unit tests for the 0.9.1 commands of kernel/shell/shell.c: history,
 * uname, hostname, date, dmesg, reboot, "help <command>" and
 * "<command> --help". They exist only with shell_context.extras; without
 * it every output is unchanged (test_shell.c and test_shell_ps09.c pin it). */
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
static struct shell_extras ex;
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
    static char line[80];
    strncpy(line, cmd, sizeof(line) - 1);
    line[sizeof(line) - 1] = '\0';
    out[0] = '\0';
    errors = 0;
    return shell_execute(line, &ctx, &sink);
}

static const char *log_text =
    "boot64: running at the higher half, RIP=0xffffffff80101000\n"
    "Scheduler self-test: PASS (9 checks, 0 mismatches)\n"
    "console: started\n"
    "madire> dmesg 2\n";

static void reset(bool with_extras)
{
    memset(&mem, 0, sizeof(mem));
    memset(&cpu, 0, sizeof(cpu));
    memset(&ctx, 0, sizeof(ctx));
    memset(&ex, 0, sizeof(ex));
    ctx.version = "0.9.1";
    ctx.mem = &mem;
    ctx.cpu = &cpu;
    ctx.efer = 1u << 10;                    /* long mode */
    history_init(&hist);
    ex.hostname = "madire";
    ex.rtc_ok = true;
    strcpy(ex.date_text, "2026-10-06 14:03:27");
    ex.log = log_text;
    ex.log_len = (uint32_t)strlen(log_text);
    ex.log_wrapped = false;
    ex.history = &hist;
    if (with_extras)
        ctx.extras = &ex;
}

static const char *help_07 =
    "Available commands:\n"
    "  help     Show this list\n"
    "  clear    Clear the screen\n"
    "  version  Show the Madire OS version\n"
    "  uptime   Show time since boot\n"
    "  mem      Show memory information\n"
    "  cpu      Show processor information\n"
    "  echo     Print text: echo <text>\n";

static const char *help_extra =
    "  history  Show previous commands (Up/Down recall)\n"
    "  uname    Show the system name: uname [-a]\n"
    "  hostname Show the computer's name\n"
    "  date     Show the date and time (RTC)\n"
    "  dmesg    Show kernel messages: dmesg [lines]\n"
    "  reboot   Restart the computer (asks first)\n";

static void test_without_extras_nothing_changes(void)
{
    const char *names[] = { "history", "uname", "hostname", "date", "dmesg", "reboot" };
    reset(false);
    CHECK_EQ(run("help"), SHELL_OK);
    CHECK(strcmp(out, help_07) == 0);
    CHECK_EQ(run("help x"), SHELL_BAD_ARGS);
    CHECK(strcmp(out, "Usage: help\n") == 0);
    CHECK_EQ(run("mem --help"), SHELL_BAD_ARGS);     /* still a usage error */
    CHECK(strcmp(out, "Usage: mem\n") == 0);
    CHECK_EQ(run("echo --help"), SHELL_OK);          /* echo prints its words */
    CHECK(strcmp(out, "--help\n") == 0);
    for (unsigned i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
        char want[96];
        CHECK_EQ(run(names[i]), SHELL_UNKNOWN);
        snprintf(want, sizeof(want), "Unknown command: %s. Type 'help' for a list of commands.\n",
                 names[i]);
        CHECK(strcmp(out, want) == 0);
    }
}

static void test_help_lists_extra_commands(void)
{
    char want[2048];
    reset(true);
    CHECK_EQ(run("help"), SHELL_OK);
    snprintf(want, sizeof(want), "%s%s", help_07, help_extra);
    CHECK(strcmp(out, want) == 0);
    /* With threads too, "ps" comes before the 0.9.1 commands. */
    static const struct shell_thread t0 = { 0, "running", 5, 1496, 16384, "kernel" };
    ctx.threads = &t0;
    ctx.thread_count = 1;
    CHECK_EQ(run("help"), SHELL_OK);
    snprintf(want, sizeof(want), "%s  ps       Show threads\n%s", help_07, help_extra);
    CHECK(strcmp(out, want) == 0);
}

static void test_help_topic(void)
{
    reset(true);
    CHECK_EQ(run("help mem"), SHELL_OK);
    CHECK(strcmp(out, "mem: Show memory information\nUsage: mem\n") == 0);
    CHECK_EQ(run("help echo"), SHELL_OK);
    CHECK(strcmp(out, "echo: Print text: echo <text>\nUsage: echo <text>\n") == 0);
    CHECK_EQ(run("help help"), SHELL_OK);
    CHECK(strcmp(out, "help: Show this list\nUsage: help [command]\n") == 0);
    CHECK_EQ(run("help dmesg"), SHELL_OK);
    CHECK(strcmp(out, "dmesg: Show kernel messages: dmesg [lines]\nUsage: dmesg [lines]\n") == 0);
    CHECK_EQ(run("help nosuch"), SHELL_UNKNOWN);
    CHECK(strcmp(out, "No help for 'nosuch': unknown command.\n") == 0);
    CHECK_EQ(errors, 1);
    CHECK_EQ(run("help ps"), SHELL_UNKNOWN);        /* no threads in this context */
    CHECK_EQ(run("help mem cpu"), SHELL_BAD_ARGS);
    CHECK(strcmp(out, "Usage: help [command]\n") == 0);
    /* A long unknown name is shortened to 37 characters and "...", so the
     * line fits; exactly 40 is shown whole. */
    char cmd[80], want[120];
    strcpy(cmd, "help ");
    memset(cmd + 5, 'a', 70);
    cmd[75] = '\0';
    CHECK_EQ(run(cmd), SHELL_UNKNOWN);
    strcpy(want, "No help for '");
    memset(want + 13, 'a', 37);
    strcpy(want + 50, "...': unknown command.\n");
    CHECK(strcmp(out, want) == 0);
    cmd[45] = '\0';                         /* a 40-character name */
    CHECK_EQ(run(cmd), SHELL_UNKNOWN);
    memset(want + 13, 'a', 40);
    strcpy(want + 53, "': unknown command.\n");
    CHECK(strcmp(out, want) == 0);
}

static void test_dash_dash_help_everywhere(void)
{
    const char *names[] = { "help", "clear", "version", "uptime", "mem", "cpu", "echo",
                            "history", "uname", "hostname", "date", "dmesg", "reboot" };
    char want[256], cmd[32], topic[32];
    reset(true);
    for (unsigned i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
        snprintf(topic, sizeof(topic), "help %s", names[i]);
        CHECK_EQ(run(topic), SHELL_OK);
        strcpy(want, out);
        snprintf(cmd, sizeof(cmd), "%s --help", names[i]);
        CHECK_EQ(run(cmd), SHELL_OK);       /* never runs the command itself */
        CHECK(strcmp(out, want) == 0);
    }
    CHECK_EQ(run("reboot --help"), SHELL_OK);       /* not SHELL_REBOOT */
    CHECK(strcmp(out, "reboot: Restart the computer (asks first)\nUsage: reboot\n") == 0);
    CHECK_EQ(run("clear --help"), SHELL_OK);        /* not SHELL_CLEAR */
    CHECK_EQ(run("echo a --help"), SHELL_OK);       /* only "<command> --help" alone */
    CHECK(strcmp(out, "a --help\n") == 0);
}

static void test_uname_and_hostname(void)
{
    reset(true);
    CHECK_EQ(run("uname"), SHELL_OK);
    CHECK(strcmp(out, "Madire OS\n") == 0);
    CHECK_EQ(run("uname -a"), SHELL_OK);
    CHECK(strcmp(out, "Madire OS madire 0.9.1 x86_64\n") == 0);
    ctx.efer = 0;                           /* not long mode: "x86" */
    CHECK_EQ(run("uname -a"), SHELL_OK);
    CHECK(strcmp(out, "Madire OS madire 0.9.1 x86\n") == 0);
    CHECK_EQ(run("uname -x"), SHELL_BAD_ARGS);
    CHECK(strcmp(out, "Usage: uname [-a]\n") == 0);
    CHECK_EQ(run("uname -a -a"), SHELL_BAD_ARGS);
    CHECK(strcmp(out, "Usage: uname [-a]\n") == 0);
    CHECK_EQ(run("hostname"), SHELL_OK);
    CHECK(strcmp(out, "madire\n") == 0);
    CHECK_EQ(run("hostname other"), SHELL_BAD_ARGS);
    CHECK(strcmp(out, "Usage: hostname\n") == 0);
}

static void test_date(void)
{
    reset(true);
    CHECK_EQ(run("date"), SHELL_OK);
    CHECK(strcmp(out, "2026-10-06 14:03:27 (RTC; time zone not set)\n") == 0);
    CHECK_EQ(errors, 0);
    ex.rtc_ok = false;
    CHECK_EQ(run("date"), SHELL_OK);
    CHECK(strcmp(out, "Date: unavailable (RTC not responding)\n") == 0);
    CHECK_EQ(errors, 1);
    CHECK_EQ(run("date now"), SHELL_BAD_ARGS);
    CHECK(strcmp(out, "Usage: date\n") == 0);
}

static void test_dmesg(void)
{
    reset(true);
    CHECK_EQ(run("dmesg 2"), SHELL_OK);
    CHECK(strcmp(out, "console: started\nmadire> dmesg 2\n") == 0);
    CHECK_EQ(run("dmesg"), SHELL_OK);       /* default 20: all 4 lines */
    CHECK(strcmp(out, log_text) == 0);
    CHECK_EQ(run("dmesg 9999"), SHELL_OK);
    CHECK(strcmp(out, log_text) == 0);
    const char *bad[] = { "dmesg 0", "dmesg 10000", "dmesg -1", "dmesg x", "dmesg 5x",
                          "dmesg 99999999999", "dmesg 1 2" };
    for (unsigned i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        CHECK_EQ(run(bad[i]), SHELL_BAD_ARGS);
        CHECK(strcmp(out, "Usage: dmesg [lines]\n") == 0);
    }
    /* Empty log; a wrapped log drops its cut first line. */
    ex.log_len = 0;
    CHECK_EQ(run("dmesg"), SHELL_OK);
    CHECK(strcmp(out, "(the kernel log is empty)\n") == 0);
    ex.log = "t of a cut line\nwhole line\n";
    ex.log_len = (uint32_t)strlen(ex.log);
    ex.log_wrapped = true;
    CHECK_EQ(run("dmesg"), SHELL_OK);
    CHECK(strcmp(out, "whole line\n") == 0);
    ex.log = NULL;
    CHECK_EQ(run("dmesg"), SHELL_OK);
    CHECK(strcmp(out, "(the kernel log is empty)\n") == 0);
}

static void test_dmesg_long_lines_fit(void)
{
    static char log[400];
    reset(true);
    memset(log, 'L', 200);
    log[200] = '\n';
    memcpy(log + 201, "short\tline\n", 11);
    log[212] = '\0';
    ex.log = log;
    ex.log_len = (uint32_t)strlen(log);
    CHECK_EQ(run("dmesg"), SHELL_OK);
    char want[200];
    memset(want, 'L', 76);
    strcpy(want + 76, "...\nshort line\n");
    CHECK(strcmp(out, want) == 0);
}

static void test_history_command(void)
{
    char line[16];
    reset(true);
    CHECK_EQ(run("history"), SHELL_OK);
    CHECK(strcmp(out, "") == 0);            /* nothing yet: no output */
    history_add(&hist, "help");
    history_add(&hist, "mem");
    history_add(&hist, "history");          /* the console adds it before running it */
    CHECK_EQ(run("history"), SHELL_OK);
    CHECK(strcmp(out, "    1  help\n    2  mem\n    3  history\n") == 0);
    for (int i = 4; i <= 20; i++) {
        snprintf(line, sizeof(line), "echo %d", i);
        history_add(&hist, line);
    }
    CHECK_EQ(run("history"), SHELL_OK);
    CHECK(strncmp(out, "    5  echo 5\n", 14) == 0);       /* 16 kept: 5..20 */
    CHECK(strstr(out, "   20  echo 20\n") != NULL);
    CHECK(strstr(out, "    4  ") == NULL);
    CHECK_EQ(run("history 3"), SHELL_BAD_ARGS);
    CHECK(strcmp(out, "Usage: history\n") == 0);
}

static void test_reboot(void)
{
    reset(true);
    CHECK_EQ(run("reboot"), SHELL_REBOOT);
    CHECK(strcmp(out, SHELL_REBOOT_QUESTION "\n") == 0);
    CHECK(strcmp(SHELL_REBOOT_QUESTION, "Restart Madire OS now? Type 'yes' to confirm:") == 0);
    CHECK(strcmp(SHELL_REBOOT_STARTING, "Restarting...") == 0);
    CHECK(strcmp(SHELL_REBOOT_CANCELLED, "Restart cancelled.") == 0);
    CHECK_EQ(run("reboot now"), SHELL_BAD_ARGS);
    CHECK(strcmp(out, "Usage: reboot\n") == 0);
    CHECK(shell_reboot_confirmed("yes"));
    CHECK(shell_reboot_confirmed("  yes\t"));
    CHECK(!shell_reboot_confirmed(""));
    CHECK(!shell_reboot_confirmed("no"));
    CHECK(!shell_reboot_confirmed("y"));
    CHECK(!shell_reboot_confirmed("YES"));
    CHECK(!shell_reboot_confirmed("yes please"));
    CHECK(!shell_reboot_confirmed("yess"));
    CHECK(!shell_reboot_confirmed("ye"));
}

static void test_every_line_fits(void)
{
    static char log[2000];
    char longline[80];
    reset(true);
    memset(log, 'x', 1500);                 /* one very long log line */
    log[1500] = '\n';
    log[1501] = '\0';
    ex.log = log;
    ex.log_len = 1501;
    memset(longline, 'a', 71);              /* a history line at the limit */
    longline[71] = '\0';
    for (int i = 0; i < 99990; i++)         /* numbers up to 5 digits */
        hist.count++;
    history_add(&hist, longline);
    static char longhelp[80];
    strcpy(longhelp, "help ");
    memset(longhelp + 5, 'z', 71);
    longhelp[76] = '\0';
    const char *cmds[] = { "help", "help reboot", "help history", "uname -a", "hostname",
                           "date", "dmesg", "history", "reboot", "dmesg --help", longhelp };
    unsigned longest = 0;
    for (unsigned i = 0; i < sizeof(cmds) / sizeof(cmds[0]); i++) {
        run(cmds[i]);
        unsigned n = 0;
        for (const char *p = out; *p; p++) {
            if (*p == '\n') {
                if (n > longest)
                    longest = n;
                n = 0;
            } else {
                n++;
            }
        }
    }
    CHECK(longest <= 79);
    CHECK(longest >= 78);                   /* the history line: 5 + 2 + 71 */
}

int main(void)
{
    printf("shell 0.9.1 commands unit tests\n");
    RUN(test_without_extras_nothing_changes);
    RUN(test_help_lists_extra_commands);
    RUN(test_help_topic);
    RUN(test_dash_dash_help_everywhere);
    RUN(test_uname_and_hostname);
    RUN(test_date);
    RUN(test_dmesg);
    RUN(test_dmesg_long_lines_fit);
    RUN(test_history_command);
    RUN(test_reboot);
    RUN(test_every_line_fits);
    return unit_report("shell_091");
}
