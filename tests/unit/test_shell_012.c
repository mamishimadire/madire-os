/* Unit tests for the 0.12 shell additions of kernel/shell/shell.c: help
 * sections, "Did you mean", prompt, alias and unalias. They exist only when
 * the context has state; without it every output stays as before
 * (test_shell.c, test_shell_091.c, test_shell_ps09.c and
 * test_shell_run011.c pin it). */
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
static struct shell_procs procs;
static struct shell_state st;
static struct history hist;
static int starts, kill_asks;

static int32_t fake_start(void *c, int argc, char **argv, uint32_t envc,
                          const char *const *env_names, const char *const *env_values,
                          bool background)
{
    (void)c; (void)argc; (void)argv; (void)envc; (void)env_names; (void)env_values;
    (void)background;
    starts++;
    return 7;
}

static void fake_ask_kill(void *c, int32_t pid)
{
    (void)c; (void)pid;
    kill_asks++;
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
    static char line[512];
    strncpy(line, cmd, sizeof(line) - 1);
    line[sizeof(line) - 1] = '\0';
    out[0] = '\0';
    errors = 0;
    starts = 0;
    kill_asks = 0;
    return shell_execute(line, &ctx, &sink);
}

static const struct shell_thread one_thread[] = { { 0, "running", 5, 1480, 16384, "kernel" } };
static const struct shell_process one_process[] = { { 5, "running", 1, "spin" } };

/* The full shell: threads, extras, procs and state, as the console builds it. */
static void reset(void)
{
    memset(&mem, 0, sizeof(mem));
    memset(&cpu, 0, sizeof(cpu));
    memset(&ctx, 0, sizeof(ctx));
    memset(&extras, 0, sizeof(extras));
    memset(&procs, 0, sizeof(procs));
    history_init(&hist);
    ctx.version = "0.12";
    ctx.mem = &mem;
    ctx.cpu = &cpu;
    ctx.page_size = 4096;
    ctx.threads = one_thread;
    ctx.thread_count = 1;
    extras.hostname = "madire";
    extras.history = &hist;
    ctx.extras = &extras;
    procs.rows = one_process;
    procs.count = 1;
    procs.start = fake_start;
    procs.ask_kill = fake_ask_kill;
    ctx.procs = &procs;
    shell_state_init(&st);
    ctx.state = &st;
}

/* 1.0 stage 10 (D2): "help" on one screen: each section's names. */
static const char *const help_012 =
    "Available commands:\n"
    "shell: The shell's own commands\n"
    "  help  clear  echo  history  prompt  alias  unalias\n"
    "system: Information about the system\n"
    "  version  uptime  mem  cpu  uname  hostname  date  dmesg  reboot\n"
    "programs: Programs and processes\n"
    "  ps  run  kill\n"
    "Purposes and usage: help <command>; one section: help <section>.\n"
    "Scroll back: Shift+PgUp and Shift+PgDn.\n";

static const char *const help_tail =
    "programs: Programs and processes\n"
    "  ps  run  kill\n"
    "Purposes and usage: help <command>; one section: help <section>.\n"
    "Scroll back: Shift+PgUp and Shift+PgDn.\n";

static bool ends_with(const char *s, const char *tail)
{
    size_t n = strlen(s), k = strlen(tail);
    return n >= k && strcmp(s + n - k, tail) == 0;
}

static void test_without_state_nothing_changes(void)
{
    static char before[8192];
    reset();
    ctx.state = NULL;
    CHECK_EQ(run("help"), SHELL_OK);
    strcpy(before, out);
    CHECK(strstr(out, "Sections") == NULL);
    CHECK(strstr(out, "prompt") == NULL);
    static const char *const gone[] = { "prompt", "alias", "unalias" };
    for (unsigned i = 0; i < 3; i++) {
        char want[96];
        CHECK_EQ(run(gone[i]), SHELL_UNKNOWN);
        snprintf(want, sizeof(want), "Unknown command: %s. Type 'help' for a list of commands.\n",
                 gone[i]);
        CHECK(strcmp(out, want) == 0);
    }
    /* No suggestions without state, even for a near name. */
    CHECK_EQ(run("hlep"), SHELL_UNKNOWN);
    CHECK(strcmp(out, "Unknown command: hlep. Type 'help' for a list of commands.\n") == 0);
    CHECK_EQ(run("help shell"), SHELL_UNKNOWN);
    CHECK(strcmp(out, "No help for 'shell': unknown command.\n") == 0);
    CHECK_EQ(run("help shel"), SHELL_UNKNOWN);
    CHECK(strcmp(out, "No help for 'shel': unknown command.\n") == 0);
    CHECK_EQ(run("help x y"), SHELL_BAD_ARGS);
    CHECK(strcmp(out, "Usage: help [command]\n") == 0);

    /* With state, "help" lists the names by section, the three new ones in
     * "shell" (1.0 stage 10, D2: was the same list with them appended). */
    reset();
    CHECK_EQ(run("help"), SHELL_OK);
    CHECK(strncmp(out, "Available commands:\n", 20) == 0);
    CHECK(strcmp(out, help_012) == 0);
    CHECK(ends_with(out, help_tail));
    CHECK_EQ(run("help x y"), SHELL_BAD_ARGS);
    CHECK(strcmp(out, "Usage: help [section | command]\n") == 0);
    CHECK_EQ(run("help --help"), SHELL_OK);
    CHECK(strcmp(out, "help: Show this list\nUsage: help [section | command]\n") == 0);
}

static void test_sections(void)
{
    reset();
    CHECK_EQ(run("help shell"), SHELL_OK);
    CHECK(strcmp(out, "shell: The shell's own commands\n"
                      "  help     Show this list\n"
                      "  clear    Clear the screen\n"
                      "  echo     Print text: echo <text>\n"
                      "  history  Show previous commands (Up/Down recall)\n"
                      "  prompt   Show or change the prompt: prompt [text]\n"
                      "  alias    Show or make short names: alias [name [command]]\n"
                      "  unalias  Remove a short name: unalias <name>\n") == 0);
    CHECK_EQ(run("help system"), SHELL_OK);
    CHECK(strcmp(out, "system: Information about the system\n"
                      "  version  Show the Madire OS version\n"
                      "  uptime   Show time since boot\n"
                      "  mem      Show memory information\n"
                      "  cpu      Show processor information\n"
                      "  uname    Show the system name: uname [-a]\n"
                      "  hostname Show the computer's name\n"
                      "  date     Show the date and time (RTC)\n"
                      "  dmesg    Show kernel messages: dmesg [lines]\n"
                      "  reboot   Restart the computer (asks first)\n") == 0);
    CHECK_EQ(run("help programs"), SHELL_OK);
    CHECK(strcmp(out, "programs: Programs and processes\n"
                      "  ps       Show threads and processes\n"
                      "  run      Run a program: run [-b] <name> [args]\n"
                      "  kill     End a process: kill <pid> (asks first)\n") == 0);
    CHECK_EQ(errors, 0);
    /* help <command> is unchanged; the new commands have help too. */
    CHECK_EQ(run("help mem"), SHELL_OK);
    CHECK(strcmp(out, "mem: Show memory information\nUsage: mem\n") == 0);
    CHECK_EQ(run("help prompt"), SHELL_OK);
    CHECK(strcmp(out, "prompt: Show or change the prompt: prompt [text]\n"
                      "Usage: prompt [text | --reset]\n") == 0);
    CHECK_EQ(run("alias --help"), SHELL_OK);
    CHECK(strcmp(out, "alias: Show or make short names: alias [name [command]]\n"
                      "Usage: alias [name [command...]]\n") == 0);
    CHECK_EQ(run("unalias --help"), SHELL_OK);
    CHECK(strcmp(out, "unalias: Remove a short name: unalias <name>\nUsage: unalias <name>\n") == 0);
}

/* The names in the "  name     text" rows of `text`. */
static unsigned row_names(const char *text, char names[32][16])
{
    unsigned n = 0;
    for (const char *p = text; *p && n < 32; ) {
        const char *end = strchr(p, '\n');
        if (p[0] == ' ' && p[1] == ' ') {
            unsigned k = 0;
            for (const char *q = p + 2; *q != ' ' && k < 15; q++)
                names[n][k++] = *q;
            names[n][k] = '\0';
            n++;
        }
        p = end ? end + 1 : p + strlen(p);
    }
    return n;
}

/* 1.0 stage 10: every name on "help"'s indented lines (several per line). */
static unsigned help_names(const char *text, char names[32][16])
{
    unsigned n = 0;
    for (const char *p = text; *p && n < 32; ) {
        const char *end = strchr(p, '\n');
        if (p[0] == ' ' && p[1] == ' ') {
            for (const char *q = p; *q && *q != '\n' && n < 32; ) {
                while (*q == ' ')
                    q++;
                if (!*q || *q == '\n')
                    break;
                unsigned k = 0;
                for (; *q && *q != ' ' && *q != '\n'; q++)
                    if (k < 15)
                        names[n][k++] = *q;
                names[n][k] = '\0';
                n++;
            }
        }
        p = end ? end + 1 : p + strlen(p);
    }
    return n;
}

/* (B) Every command "help" lists is in exactly one section, every section
 * row is a listed command, and no section name is a command name. */
static void test_section_completeness(void)
{
    static const char *const sections[] = { "shell", "system", "programs" };
    char all[32][16], in[3][32][16];
    unsigned n_all, n_in[3];
    reset();
    run("help");
    n_all = help_names(out, all);
    CHECK_EQ(n_all, 19);
    for (unsigned s = 0; s < 3; s++) {
        char cmd[32];
        snprintf(cmd, sizeof(cmd), "help %s", sections[s]);
        CHECK_EQ(run(cmd), SHELL_OK);
        n_in[s] = row_names(out, in[s]);
    }
    CHECK_EQ(n_in[0] + n_in[1] + n_in[2], n_all);
    for (unsigned i = 0; i < n_all; i++) {
        unsigned found = 0;
        for (unsigned s = 0; s < 3; s++)
            for (unsigned j = 0; j < n_in[s]; j++)
                found += strcmp(all[i], in[s][j]) == 0;
        CHECK_EQ(found, 1);
        if (found != 1)
            printf("    command '%s' is in %u sections\n", all[i], found);
    }
    for (unsigned s = 0; s < 3; s++) {
        char want[96];
        for (unsigned i = 0; i < n_all; i++)
            CHECK(strcmp(all[i], sections[s]) != 0);
        CHECK_EQ(run(sections[s]), SHELL_UNKNOWN);
        snprintf(want, sizeof(want), "Unknown command: %s. Type 'help' for a list of commands.\n",
                 sections[s]);
        CHECK(strncmp(out, want, strlen(want)) == 0);
        snprintf(want, sizeof(want), "%s --help", sections[s]);
        CHECK_EQ(run(want), SHELL_UNKNOWN);
    }
}

static void test_did_you_mean(void)
{
    reset();
    CHECK_EQ(run("hlep"), SHELL_UNKNOWN);
    CHECK(strcmp(out, "Unknown command: hlep. Type 'help' for a list of commands.\n"
                      "Did you mean: help?\n") == 0);
    CHECK_EQ(errors, 1);                    /* the suggestion is not an error line */
    /* Nearest first, then alphabetically: run (1), cpu and ps (2). */
    CHECK_EQ(run("ru"), SHELL_UNKNOWN);
    CHECK(strcmp(out, "Unknown command: ru. Type 'help' for a list of commands.\n"
                      "Did you mean: run, cpu, ps?\n") == 0);
    CHECK_EQ(starts, 0);
    /* Case-sensitive: one changed letter is distance 1, four are none. */
    CHECK_EQ(run("Echo x"), SHELL_UNKNOWN);
    CHECK(strcmp(out, "Unknown command: Echo. Type 'help' for a list of commands.\n"
                      "Did you mean: echo?\n") == 0);
    CHECK_EQ(run("ECHO x"), SHELL_UNKNOWN);
    CHECK(strcmp(out, "Unknown command: ECHO. Type 'help' for a list of commands.\n") == 0);
    /* Distance 2 is suggested, 3 is not. */
    CHECK_EQ(run("clearxx"), SHELL_UNKNOWN);
    CHECK(strcmp(out, "Unknown command: clearxx. Type 'help' for a list of commands.\n"
                      "Did you mean: clear?\n") == 0);
    CHECK_EQ(run("clearxxx"), SHELL_UNKNOWN);
    CHECK(strcmp(out, "Unknown command: clearxxx. Type 'help' for a list of commands.\n") == 0);
    CHECK_EQ(run("foo"), SHELL_UNKNOWN);
    CHECK(strcmp(out, "Unknown command: foo. Type 'help' for a list of commands.\n") == 0);
    /* Never run: "kil 5" only suggests kill. */
    CHECK_EQ(run("kil 5"), SHELL_UNKNOWN);
    CHECK(strcmp(out, "Unknown command: kil. Type 'help' for a list of commands.\n"
                      "Did you mean: kill?\n") == 0);
    CHECK_EQ(kill_asks, 0);
    CHECK_EQ(run("rnu hello"), SHELL_UNKNOWN);
    CHECK_EQ(starts, 0);
    /* Aliases are candidates too; at most 3 names (aa1-aa3 at 1, ps at 2 dropped). */
    run("alias aa4 echo 4");
    run("alias aa2 echo 2");
    run("alias aa3 echo 3");
    run("alias aa1 echo 1");
    CHECK_EQ(run("aa"), SHELL_UNKNOWN);
    CHECK(strcmp(out, "Unknown command: aa. Type 'help' for a list of commands.\n"
                      "Did you mean: aa1, aa2, aa3?\n") == 0);
    /* A long name is compared without trouble and finds nothing. */
    CHECK_EQ(run("abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz"), SHELL_UNKNOWN);
    CHECK(strstr(out, "Did you mean") == NULL);

    /* help <name>: sections and commands (not aliases); today's line first. */
    CHECK_EQ(run("help shel"), SHELL_UNKNOWN);
    CHECK(strcmp(out, "No help for 'shel': unknown command.\nDid you mean: shell, help?\n") == 0);
    CHECK_EQ(run("help syste"), SHELL_UNKNOWN);
    CHECK(strcmp(out, "No help for 'syste': unknown command.\nDid you mean: system?\n") == 0);
    CHECK_EQ(run("help memm"), SHELL_UNKNOWN);
    CHECK(strcmp(out, "No help for 'memm': unknown command.\nDid you mean: mem?\n") == 0);
    CHECK_EQ(run("help nosuch"), SHELL_UNKNOWN);
    CHECK(strcmp(out, "No help for 'nosuch': unknown command.\n") == 0);
    CHECK_EQ(run("help aa"), SHELL_UNKNOWN);
    CHECK(strcmp(out, "No help for 'aa': unknown command.\nDid you mean: ps?\n") == 0);
}

static void test_prompt(void)
{
    reset();
    CHECK(strcmp(st.prompt, "madire> ") == 0);
    CHECK_EQ(run("prompt"), SHELL_OK);
    CHECK(strcmp(out, "Prompt: 'madire> ' (the default)\n") == 0);
    CHECK_EQ(run("prompt abc>"), SHELL_OK);
    CHECK(strcmp(out, "Prompt set to 'abc>    '.\n") == 0);
    CHECK(strcmp(st.prompt, "abc>    ") == 0);
    CHECK_EQ(run("prompt"), SHELL_OK);
    CHECK(strcmp(out, "Prompt: 'abc>    '\n") == 0);
    CHECK_EQ(run("prompt 12345678"), SHELL_OK);             /* 8: the most */
    CHECK(strcmp(st.prompt, "12345678") == 0);
    CHECK_EQ(run("prompt #"), SHELL_OK);                    /* 1: the least */
    CHECK(strcmp(st.prompt, "#       ") == 0);
    CHECK_EQ(run("prompt !~"), SHELL_OK);                   /* the ends of printable ASCII */
    CHECK(strcmp(st.prompt, "!~      ") == 0);

    /* Refused: the prompt stays as it was. */
    CHECK_EQ(run("prompt 123456789"), SHELL_BAD_ARGS);
    CHECK(strcmp(out, "prompt: at most 8 characters (this text has 9).\n") == 0);
    CHECK_EQ(run("prompt -x"), SHELL_BAD_ARGS);
    CHECK(strcmp(out, "prompt: the text may not start with '-' (to reset: prompt --reset).\n") == 0);
    CHECK_EQ(run("prompt -"), SHELL_BAD_ARGS);
    CHECK_EQ(run("prompt a b"), SHELL_BAD_ARGS);
    CHECK(strcmp(out, "Usage: prompt [text | --reset]\n") == 0);
    CHECK_EQ(run("prompt a\x01"), SHELL_BAD_ARGS);
    CHECK(strcmp(out, "prompt: only printable characters, no spaces.\n") == 0);
    CHECK_EQ(run("prompt a\x7f"), SHELL_BAD_ARGS);
    CHECK_EQ(run("prompt \xc3\xa9"), SHELL_BAD_ARGS);
    CHECK_EQ(errors, 1);
    CHECK(strcmp(st.prompt, "!~      ") == 0);
    CHECK_EQ(run("prompt --help"), SHELL_OK);               /* help, not a prompt */
    CHECK(strcmp(st.prompt, "!~      ") == 0);

    CHECK_EQ(run("prompt --reset"), SHELL_OK);
    CHECK(strcmp(out, "Prompt reset to 'madire> '.\n") == 0);
    CHECK(strcmp(st.prompt, "madire> ") == 0);
    CHECK_EQ(run("prompt"), SHELL_OK);
    CHECK(strcmp(out, "Prompt: 'madire> ' (the default)\n") == 0);
}

static void test_alias(void)
{
    reset();
    CHECK_EQ(run("alias"), SHELL_OK);
    CHECK(strcmp(out, "0 aliases (at most 16)\n") == 0);
    CHECK_EQ(run("alias ll echo   hi   there"), SHELL_OK);
    CHECK(strcmp(out, "Alias 'll' added.\n") == 0);
    CHECK_EQ(run("alias ll"), SHELL_OK);
    CHECK(strcmp(out, "ll              echo hi there\n") == 0);
    CHECK_EQ(run("ll"), SHELL_OK);
    CHECK(strcmp(out, "hi there\n") == 0);
    CHECK_EQ(run("  ll   more  "), SHELL_OK);              /* the first word only */
    CHECK(strcmp(out, "hi there more\n") == 0);
    CHECK_EQ(run("echo ll"), SHELL_OK);                     /* not as an argument */
    CHECK(strcmp(out, "ll\n") == 0);
    CHECK_EQ(run("alias ll echo replaced"), SHELL_OK);
    CHECK(strcmp(out, "Alias 'll' replaced.\n") == 0);
    CHECK_EQ(run("ll"), SHELL_OK);
    CHECK(strcmp(out, "replaced\n") == 0);
    CHECK_EQ(run("alias b2 help shell"), SHELL_OK);
    CHECK_EQ(run("b2"), SHELL_OK);                          /* runs "help shell" */
    CHECK(strncmp(out, "shell: The shell's own commands\n", 32) == 0);
    CHECK_EQ(run("alias"), SHELL_OK);                       /* by name */
    CHECK(strcmp(out, "b2              help shell\n"
                      "ll              echo replaced\n"
                      "2 aliases (at most 16)\n") == 0);
    CHECK_EQ(run("alias a_9 version"), SHELL_OK);
    CHECK_EQ(run("alias"), SHELL_OK);
    CHECK(strncmp(out, "a_9             version\n", 24) == 0);
    CHECK(strstr(out, "3 aliases (at most 16)\n") != NULL);

    /* unalias */
    CHECK_EQ(run("unalias b2"), SHELL_OK);
    CHECK(strcmp(out, "Alias 'b2' removed.\n") == 0);
    CHECK_EQ(run("b2"), SHELL_UNKNOWN);
    CHECK_EQ(run("unalias b2"), SHELL_BAD_ARGS);
    CHECK(strcmp(out, "unalias: no alias named 'b2'.\n") == 0);
    CHECK_EQ(run("unalias"), SHELL_BAD_ARGS);
    CHECK(strcmp(out, "Usage: unalias <name>\n") == 0);
    CHECK_EQ(run("unalias a b"), SHELL_BAD_ARGS);
    CHECK(strcmp(out, "Usage: unalias <name>\n") == 0);
    CHECK_EQ(run("alias zz"), SHELL_BAD_ARGS);
    CHECK(strcmp(out, "alias: no alias named 'zz'.\n") == 0);
    CHECK_EQ(run("alias"), SHELL_OK);
    CHECK(strcmp(out, "a_9             version\nll              echo replaced\n"
                      "2 aliases (at most 16)\n") == 0);
}

static void test_alias_refused(void)
{
    reset();
    /* A built-in's name, whatever the context offers. */
    static const char *const builtins[] = {
        "help", "clear", "version", "uptime", "mem", "cpu", "echo", "ps", "history", "uname",
        "hostname", "date", "dmesg", "reboot", "run", "kill", "prompt", "alias", "unalias",
    };
    for (unsigned i = 0; i < sizeof(builtins) / sizeof(builtins[0]); i++) {
        char cmd[64], want[96];
        snprintf(cmd, sizeof(cmd), "alias %s echo x", builtins[i]);
        CHECK_EQ(run(cmd), SHELL_BAD_ARGS);
        snprintf(want, sizeof(want), "alias: '%s' is a built-in command.\n", builtins[i]);
        CHECK(strcmp(out, want) == 0);
    }
    ctx.procs = NULL;                       /* run and kill not offered: still refused */
    CHECK_EQ(run("alias run echo x"), SHELL_BAD_ARGS);
    CHECK(strcmp(out, "alias: 'run' is a built-in command.\n") == 0);
    reset();

    /* (C) The command must start with a built-in: no alias to an alias. */
    CHECK_EQ(run("alias ll echo hi"), SHELL_OK);
    CHECK_EQ(run("alias zz ll"), SHELL_BAD_ARGS);
    CHECK(strcmp(out, "alias: 'll' is not a built-in command.\n") == 0);
    CHECK_EQ(run("alias zz nosuch x"), SHELL_BAD_ARGS);
    CHECK(strcmp(out, "alias: 'nosuch' is not a built-in command.\n") == 0);
    CHECK_EQ(run("alias ll ll"), SHELL_BAD_ARGS);           /* nor to itself */
    CHECK_EQ(run("ll"), SHELL_OK);
    CHECK(strcmp(out, "hi\n") == 0);

    /* Names: 1-15 of a-z, 0-9 and _. */
    CHECK_EQ(run("alias abcdefghijklmno echo 15"), SHELL_OK);
    CHECK_EQ(run("abcdefghijklmno"), SHELL_OK);
    CHECK(strcmp(out, "15\n") == 0);
    CHECK_EQ(run("alias abcdefghijklmnop echo 16"), SHELL_BAD_ARGS);
    CHECK(strcmp(out, "alias: a name has at most 15 characters (this one has 16).\n") == 0);
    static const char *const bad[] = { "LL", "l-l", "l.l", "lL", "l/" };
    for (unsigned i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        char cmd[64];
        snprintf(cmd, sizeof(cmd), "alias %s echo x", bad[i]);
        CHECK_EQ(run(cmd), SHELL_BAD_ARGS);
        CHECK(strcmp(out, "alias: a name uses only a-z, 0-9 and _.\n") == 0);
    }

    /* The command: at most 63 characters ("echo " + 58 = 63). */
    char cmd[160], x58[59], x59[60];
    memset(x58, 'x', 58);
    x58[58] = '\0';
    memset(x59, 'x', 59);
    x59[59] = '\0';
    snprintf(cmd, sizeof(cmd), "alias v echo %s", x58);
    CHECK_EQ(run(cmd), SHELL_OK);
    CHECK_EQ(run("alias v"), SHELL_OK);
    CHECK_EQ(strlen(out), 16 + 63 + 1);                     /* 79 columns */
    snprintf(cmd, sizeof(cmd), "alias w echo %s", x59);
    CHECK_EQ(run(cmd), SHELL_BAD_ARGS);
    CHECK(strcmp(out, "alias: the command has at most 63 characters (this one has 64).\n") == 0);
    CHECK_EQ(run("alias w"), SHELL_BAD_ARGS);               /* not added */
    char x20[21];
    memset(x20, 'y', 20);
    x20[20] = '\0';
    snprintf(cmd, sizeof(cmd), "alias w echo %s %s %s", x20, x20, x20);   /* 5+20*3+2 = 67 */
    CHECK_EQ(run(cmd), SHELL_BAD_ARGS);
    CHECK(strcmp(out, "alias: the command has at most 63 characters (this one has 67).\n") == 0);
    CHECK_EQ(run("alias v"), SHELL_OK);                     /* v unchanged by the refusals */
    CHECK(strncmp(out, "v               echo xxxx", 25) == 0);
}

static void test_alias_table_full(void)
{
    char cmd[64];
    reset();
    for (unsigned i = 0; i < 16; i++) {
        snprintf(cmd, sizeof(cmd), "alias a%02u echo %u", i, i);
        CHECK_EQ(run(cmd), SHELL_OK);
    }
    CHECK_EQ(st.alias_count, 16);
    CHECK_EQ(run("alias a16 echo 16"), SHELL_BAD_ARGS);
    CHECK(strcmp(out, "alias: the table is full (16 aliases); remove one with unalias.\n") == 0);
    CHECK_EQ(st.alias_count, 16);
    CHECK_EQ(run("alias a05 echo five"), SHELL_OK);         /* replacing still works */
    CHECK(strcmp(out, "Alias 'a05' replaced.\n") == 0);
    CHECK_EQ(run("a05"), SHELL_OK);
    CHECK(strcmp(out, "five\n") == 0);
    CHECK_EQ(run("alias"), SHELL_OK);
    CHECK(strncmp(out, "a00             echo 0\n", 22) == 0);
    CHECK(ends_with(out, "a15             echo 15\n16 aliases (at most 16)\n"));
    CHECK_EQ(run("unalias a00"), SHELL_OK);
    CHECK_EQ(run("alias a16 echo 16"), SHELL_OK);
    CHECK_EQ(run("a16"), SHELL_OK);
    CHECK(strcmp(out, "16\n") == 0);
}

static void test_alias_expansion(void)
{
    reset();
    /* One level, re-split under the 8-word limit. */
    CHECK_EQ(run("alias e5 echo 1 2 3 4 5"), SHELL_OK);     /* 6 words */
    CHECK_EQ(run("e5 6 7"), SHELL_OK);                      /* 8 words */
    CHECK(strcmp(out, "1 2 3 4 5 6 7\n") == 0);
    CHECK_EQ(run("e5 6 7 8"), SHELL_TOO_MANY);              /* 9 words */
    CHECK(strcmp(out, "Too many arguments (max 8).\n") == 0);
    /* The expanded line runs like a typed one: --help, usage errors. */
    CHECK_EQ(run("alias hv version"), SHELL_OK);
    CHECK_EQ(run("hv --help"), SHELL_OK);
    CHECK(strcmp(out, "version: Show the Madire OS version\nUsage: version\n") == 0);
    CHECK_EQ(run("hv x"), SHELL_BAD_ARGS);
    CHECK(strcmp(out, "Usage: version\n") == 0);
    CHECK_EQ(run("alias kk kill"), SHELL_OK);
    CHECK_EQ(run("kk 5"), SHELL_KILL);                      /* still asks first */
    CHECK_EQ(kill_asks, 1);
    /* An alias's value may be "alias ..." (a built-in), still one level. */
    CHECK_EQ(run("alias mk alias"), SHELL_OK);
    CHECK_EQ(run("mk"), SHELL_OK);
    CHECK(strstr(out, "aliases (at most 16)\n") != NULL);
    /* Too long to hold: refused, nothing cut (a console line never gets here). */
    char x58[59], arg30[31], cmd[400];
    memset(x58, 'x', 58);
    x58[58] = '\0';
    memset(arg30, 'z', 30);
    arg30[30] = '\0';
    snprintf(cmd, sizeof(cmd), "alias big echo %s", x58);
    CHECK_EQ(run(cmd), SHELL_OK);
    snprintf(cmd, sizeof(cmd), "big %s %s %s %s %s", arg30, arg30, arg30, arg30, arg30);
    CHECK_EQ(run(cmd), SHELL_OK);                           /* 63 + 5 x 31 = 218 */
    CHECK(strncmp(out, x58, 58) == 0 && strncmp(out + 58, " zzz", 4) == 0);
    snprintf(cmd, sizeof(cmd), "big %s %s %s %s %s %s", arg30, arg30, arg30, arg30, arg30, arg30);
    CHECK_EQ(run(cmd), SHELL_TOO_MANY);                     /* 249: more than it holds */
    CHECK(strcmp(out, "The alias makes the line too long.\n") == 0);
}

static void test_every_line_fits(void)
{
    char cmd[160], x58[59];
    reset();
    memset(x58, 'x', 58);
    x58[58] = '\0';
    snprintf(cmd, sizeof(cmd), "alias abcdefghijklmno echo %s", x58);
    run(cmd);
    static const char *const cmds[] = {
        "help", "help shell", "help system", "help programs", "help shel", "alias", "prompt",
        "prompt 123456789012345678901234567890", "alias abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrst echo",
        "alias a abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefgh",
        "unalias abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefgh", "ru",
    };
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
        if (longest > 79)
            printf("    '%s': a line of %u characters\n", cmds[c], longest);
    }
}

int main(void)
{
    printf("shell sections, suggestions, prompt and aliases (0.12) unit tests\n");
    RUN(test_without_state_nothing_changes);
    RUN(test_sections);
    RUN(test_section_completeness);
    RUN(test_did_you_mean);
    RUN(test_prompt);
    RUN(test_alias);
    RUN(test_alias_refused);
    RUN(test_alias_table_full);
    RUN(test_alias_expansion);
    RUN(test_every_line_fits);
    return unit_report("shell_012");
}
