/* Unit tests for the 0.12 stage 2 shell additions of kernel/shell/shell.c:
 * set, unset, export, env, sleep, presskey, time and "kill -y". They exist
 * only when the context has a session; without it every output stays as in
 * stage 1 (test_shell_012.c pins that). */
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
static struct shell_session ss;
static struct history hist;
static int starts, kill_asks;
static int32_t asked_pid;

/* 1.0 stage 7: the environment "run" hands over, "NAME=value" lines. */
static char started_env[512];

static int32_t fake_start(void *c, int argc, char **argv, uint32_t envc,
                          const char *const *env_names, const char *const *env_values,
                          bool background)
{
    (void)c; (void)argc; (void)argv; (void)background;
    starts++;
    started_env[0] = '\0';
    for (uint32_t i = 0; i < envc; i++) {
        strncat(started_env, env_names[i], sizeof(started_env) - strlen(started_env) - 1);
        strncat(started_env, "=", sizeof(started_env) - strlen(started_env) - 1);
        strncat(started_env, env_values[i], sizeof(started_env) - strlen(started_env) - 1);
        strncat(started_env, "\n", sizeof(started_env) - strlen(started_env) - 1);
    }
    return 7;
}

static void fake_ask_kill(void *c, int32_t pid)
{
    (void)c;
    kill_asks++;
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
    static char line[512];
    strncpy(line, cmd, sizeof(line) - 1);
    line[sizeof(line) - 1] = '\0';
    out[0] = '\0';
    errors = 0;
    starts = 0;
    kill_asks = 0;
    asked_pid = 0;
    return shell_execute(line, &ctx, &sink);
}

static const struct shell_thread one_thread[] = { { 0, "running", 5, 1480, 16384, "kernel" } };
static const struct shell_process two_processes[] = {
    { 5, "running", 1, "spin" },
    { 9, "ended", 2, "hello" },
};

/* The full shell, as the console builds it: threads, extras, procs, state
 * and the session. */
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
    ctx.timer_ok = true;
    ctx.timer_hz = 100;
    ctx.threads = one_thread;
    ctx.thread_count = 1;
    extras.hostname = "madire";
    extras.history = &hist;
    ctx.extras = &extras;
    procs.rows = two_processes;
    procs.count = 2;
    procs.start = fake_start;
    procs.ask_kill = fake_ask_kill;
    ctx.procs = &procs;
    shell_state_init(&st);
    ctx.state = &st;
    memset(&ss, 0x5A, sizeof(ss));          /* shell_session_init must set everything */
    shell_session_init(&ss);
    ctx.session = &ss;
}

static bool ends_with(const char *s, const char *tail)
{
    size_t n = strlen(s), k = strlen(tail);
    return n >= k && strcmp(s + n - k, tail) == 0;
}

static const char *const new_rows =
    "  set      Show or set variables: set [NAME[=value]]\n"
    "  unset    Remove a variable: unset <NAME>\n"
    "  export   Mark a variable as exported: export <NAME>\n"
    "  env      Show the exported variables\n"
    "  sleep    Wait: sleep <seconds> (1-3600, Ctrl+C stops)\n"
    "  presskey Wait for a key press (Ctrl+C cancels)\n"
    "  time     Time a command: time <command>\n";

static const char *const new_names[] = { "set", "unset", "export", "env", "sleep", "presskey", "time" };

static void test_without_session_nothing_changes(void)
{
    static char before[8192];
    reset();
    ctx.session = NULL;
    CHECK_EQ(run("help"), SHELL_OK);
    strcpy(before, out);
    CHECK(ends_with(out, "  unalias  Remove a short name: unalias <name>\n"
                         "Sections: shell system programs (help <section>)\n"));
    for (unsigned i = 0; i < 7; i++) {
        char want[96];
        CHECK_EQ(run(new_names[i]), SHELL_UNKNOWN);
        snprintf(want, sizeof(want), "Unknown command: %s. Type 'help' for a list of commands.\n",
                 new_names[i]);
        CHECK(strncmp(out, want, strlen(want)) == 0);
    }
    /* kill as in 0.11: no -y. */
    CHECK_EQ(run("kill -y 5"), SHELL_BAD_ARGS);
    CHECK(strcmp(out, "Usage: kill <pid>\n") == 0);
    CHECK_EQ(kill_asks, 0);
    CHECK_EQ(run("kill --help"), SHELL_OK);
    CHECK(strcmp(out, "kill: End a process: kill <pid> (asks first)\nUsage: kill <pid>\n") == 0);
    /* The suggestions of stage 1 (no new names among the candidates). */
    CHECK_EQ(run("hlep"), SHELL_UNKNOWN);
    CHECK(strcmp(out, "Unknown command: hlep. Type 'help' for a list of commands.\n"
                      "Did you mean: help?\n") == 0);

    /* With the session, "help" is the stage 1 list with the seven new rows
     * after "unalias", then the sections line: no existing row moves. */
    reset();
    CHECK_EQ(run("help"), SHELL_OK);
    size_t keep = strlen(before) - strlen("Sections: shell system programs (help <section>)\n");
    CHECK(strncmp(out, before, keep) == 0);
    char want[2048];
    snprintf(want, sizeof(want), "%sSections: shell system programs (help <section>)\n", new_rows);
    CHECK(strcmp(out + keep, want) == 0);
    CHECK(strstr(out, "  kill     End a process: kill <pid> (asks first)\n") != NULL);
}

static void test_help(void)
{
    reset();
    char want[2048];
    snprintf(want, sizeof(want),
             "shell: The shell's own commands\n"
             "  help     Show this list\n"
             "  clear    Clear the screen\n"
             "  echo     Print text: echo <text>\n"
             "  history  Show previous commands (Up/Down recall)\n"
             "  prompt   Show or change the prompt: prompt [text]\n"
             "  alias    Show or make short names: alias [name [command]]\n"
             "  unalias  Remove a short name: unalias <name>\n%s", new_rows);
    CHECK_EQ(run("help shell"), SHELL_OK);
    CHECK(strcmp(out, want) == 0);
    CHECK_EQ(run("help programs"), SHELL_OK);
    CHECK(strcmp(out, "programs: Programs and processes\n"
                      "  ps       Show threads and processes\n"
                      "  run      Run a program: run [-b] <name> [args]\n"
                      "  kill     End a process: kill <pid> (asks first)\n") == 0);
    static const char *const topics[][2] = {
        { "set",      "set: Show or set variables: set [NAME[=value]]\n"
                      "Usage: set [NAME[=value]] (names are case-sensitive)\n" },
        { "unset",    "unset: Remove a variable: unset <NAME>\nUsage: unset <NAME>\n" },
        { "export",   "export: Mark a variable as exported: export <NAME>\nUsage: export <NAME>\n" },
        { "env",      "env: Show the exported variables\nUsage: env\n" },
        { "sleep",    "sleep: Wait: sleep <seconds> (1-3600, Ctrl+C stops)\nUsage: sleep <seconds>\n" },
        { "presskey", "presskey: Wait for a key press (Ctrl+C cancels)\nUsage: presskey\n" },
        { "time",     "time: Time a command: time <command>\nUsage: time <command...>\n" },
        { "kill",     "kill: End a process: kill <pid> (asks first)\nUsage: kill [-y] <pid>\n" },
    };
    for (unsigned i = 0; i < sizeof(topics) / sizeof(topics[0]); i++) {
        char cmd[32];
        snprintf(cmd, sizeof(cmd), "help %s", topics[i][0]);
        CHECK_EQ(run(cmd), SHELL_OK);
        CHECK(strcmp(out, topics[i][1]) == 0);
        snprintf(cmd, sizeof(cmd), "%s --help", topics[i][0]);
        CHECK_EQ(run(cmd), SHELL_OK);
        CHECK(strcmp(out, topics[i][1]) == 0);
        CHECK_EQ(ss.sleep_seconds, 0);              /* help never starts a wait */
        CHECK_EQ(ss.timed, 0);
    }
}

/* The names in the "  name     text" rows of `text`. */
static unsigned row_names(const char *text, char names[40][16])
{
    unsigned n = 0;
    for (const char *p = text; *p && n < 40; ) {
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

/* (B) Every command "help" lists is in exactly one section; the 7 new ones
 * are in the shell section. */
static void test_section_completeness(void)
{
    static const char *const sections[] = { "shell", "system", "programs" };
    char all[40][16], in[3][40][16];
    unsigned n_all, n_in[3];
    reset();
    run("help");
    n_all = row_names(out, all);
    CHECK_EQ(n_all, 26);
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
    for (unsigned i = 0; i < 7; i++) {
        unsigned found = 0;
        for (unsigned j = 0; j < n_in[0]; j++)
            found += strcmp(new_names[i], in[0][j]) == 0;
        CHECK_EQ(found, 1);
    }
}

static void test_did_you_mean(void)
{
    reset();
    CHECK_EQ(run("hlep"), SHELL_UNKNOWN);
    CHECK(strcmp(out, "Unknown command: hlep. Type 'help' for a list of commands.\n"
                      "Did you mean: help, sleep?\n") == 0);
    CHECK_EQ(run("help shel"), SHELL_UNKNOWN);
    CHECK(strcmp(out, "No help for 'shel': unknown command.\nDid you mean: shell, help, set?\n") == 0);
    CHECK_EQ(run("slep 1"), SHELL_UNKNOWN);
    CHECK(strcmp(out, "Unknown command: slep. Type 'help' for a list of commands.\n"
                      "Did you mean: sleep, set?\n") == 0);
    CHECK_EQ(ss.sleep_seconds, 0);                  /* never run */
    CHECK_EQ(run("exprot A"), SHELL_UNKNOWN);
    CHECK(strcmp(out, "Unknown command: exprot. Type 'help' for a list of commands.\n"
                      "Did you mean: export?\n") == 0);
    CHECK_EQ(run("presskye"), SHELL_UNKNOWN);
    CHECK(strcmp(out, "Unknown command: presskye. Type 'help' for a list of commands.\n"
                      "Did you mean: presskey?\n") == 0);
    CHECK_EQ(run("timee echo"), SHELL_UNKNOWN);
    CHECK(strcmp(out, "Unknown command: timee. Type 'help' for a list of commands.\n"
                      "Did you mean: time?\n") == 0);
    CHECK_EQ(ss.timed, 0);
    /* Unchanged by the new names. */
    CHECK_EQ(run("ru"), SHELL_UNKNOWN);
    CHECK(strcmp(out, "Unknown command: ru. Type 'help' for a list of commands.\n"
                      "Did you mean: run, cpu, ps?\n") == 0);
    CHECK_EQ(run("kil 5"), SHELL_UNKNOWN);
    CHECK(strcmp(out, "Unknown command: kil. Type 'help' for a list of commands.\n"
                      "Did you mean: kill?\n") == 0);
    CHECK_EQ(kill_asks, 0);
}

static void test_alias_and_new_builtins(void)
{
    reset();
    for (unsigned i = 0; i < 7; i++) {
        char cmd[64], want[96];
        snprintf(cmd, sizeof(cmd), "alias %s echo x", new_names[i]);
        CHECK_EQ(run(cmd), SHELL_BAD_ARGS);
        snprintf(want, sizeof(want), "alias: '%s' is a built-in command.\n", new_names[i]);
        CHECK(strcmp(out, want) == 0);
    }
    /* An alias may start with a new built-in, and runs like a typed line. */
    CHECK_EQ(run("alias zz sleep 2"), SHELL_OK);
    CHECK_EQ(run("zz"), SHELL_SLEEP);
    CHECK_EQ(ss.sleep_seconds, 2);
    CHECK_EQ(run("alias sv set V=1"), SHELL_OK);
    CHECK_EQ(run("sv"), SHELL_OK);
    CHECK_EQ(run("set V"), SHELL_OK);
    CHECK(strcmp(out, "V=1\n") == 0);
    /* Without a session the new names are not built-ins (stage 1's rules). */
    ctx.session = NULL;
    CHECK_EQ(run("alias qq sleep 2"), SHELL_BAD_ARGS);
    CHECK(strcmp(out, "alias: 'sleep' is not a built-in command.\n") == 0);
}

static void test_set(void)
{
    reset();
    CHECK_EQ(run("set"), SHELL_OK);
    CHECK(strcmp(out, "No variables.\n") == 0);
    CHECK_EQ(errors, 0);
    CHECK_EQ(run("set A=1"), SHELL_OK);
    CHECK(strcmp(out, "") == 0);                    /* nothing printed */
    CHECK_EQ(run("set A"), SHELL_OK);
    CHECK(strcmp(out, "A=1\n") == 0);
    CHECK_EQ(run("set B=b=c"), SHELL_OK);           /* the first '=' ends the name */
    CHECK_EQ(run("set B"), SHELL_OK);
    CHECK(strcmp(out, "B=b=c\n") == 0);
    CHECK_EQ(run("set E="), SHELL_OK);              /* an empty value */
    CHECK_EQ(run("set E"), SHELL_OK);
    CHECK(strcmp(out, "E=\n") == 0);
    CHECK_EQ(run("set W=x   y \t z"), SHELL_OK);    /* words, single spaces */
    CHECK_EQ(run("set W"), SHELL_OK);
    CHECK(strcmp(out, "W=x y z\n") == 0);
    CHECK_EQ(run("set S= after"), SHELL_OK);        /* the words after an empty start */
    CHECK_EQ(run("set S"), SHELL_OK);
    CHECK(strcmp(out, "S=after\n") == 0);
    CHECK_EQ(run("set A=2"), SHELL_OK);             /* changed */
    CHECK_EQ(run("set A"), SHELL_OK);
    CHECK(strcmp(out, "A=2\n") == 0);
    /* Case-sensitive names, listed in byte order (A-Z, then _, then a-z). */
    CHECK_EQ(run("set a=small"), SHELL_OK);
    CHECK_EQ(run("set _u=under"), SHELL_OK);
    CHECK_EQ(run("set Z9=z"), SHELL_OK);
    CHECK_EQ(run("set"), SHELL_OK);
    CHECK(strcmp(out, "A=2\nB=b=c\nE=\nS=after\nW=x y z\nZ9=z\n_u=under\na=small\n") == 0);
    CHECK_EQ(ss.var_count, 8);
    CHECK_EQ(run("set a"), SHELL_OK);
    CHECK(strcmp(out, "a=small\n") == 0);

    /* Show one that is not set; a second word without '=' is a usage error. */
    CHECK_EQ(run("set nosuch"), SHELL_BAD_ARGS);
    CHECK(strcmp(out, "set: nosuch is not set\n") == 0);
    CHECK_EQ(errors, 1);
    CHECK_EQ(run("set A b"), SHELL_BAD_ARGS);
    CHECK(strcmp(out, "Usage: set [NAME[=value]] (names are case-sensitive)\n") == 0);
    CHECK_EQ(run("set abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz"), SHELL_BAD_ARGS);
    CHECK(strcmp(out, "set: abcdefghijklmnopqrstuvwxyzabcdefghijk... is not set\n") == 0);
}

static void test_set_names(void)
{
    reset();
    static const char *const good[] = { "_", "a", "Z", "_9", "abcdefghijklmnop", "A_b_C_1" };
    for (unsigned i = 0; i < sizeof(good) / sizeof(good[0]); i++) {
        char cmd[64];
        snprintf(cmd, sizeof(cmd), "set %s=v", good[i]);
        CHECK_EQ(run(cmd), SHELL_OK);
        CHECK_EQ(errors, 0);
    }
    CHECK_EQ(ss.var_count, 6);
    static const char *const bad[][2] = {
        { "set abcdefghijklmnopq=v", "set: invalid name: abcdefghijklmnopq\n" },   /* 17 */
        { "set 1abc=v",              "set: invalid name: 1abc\n" },
        { "set a-b=v",               "set: invalid name: a-b\n" },
        { "set a.b=v",               "set: invalid name: a.b\n" },
        { "set =v",                  "set: invalid name: (empty)\n" },
        { "set \xc3\xa9=v",          "set: invalid name: \xc3\xa9\n" },
        { "set abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz=v",
          "set: invalid name: abcdefghijklmnopqrstuvwxyzabcdefghijk...\n" },
    };
    for (unsigned i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        CHECK_EQ(run(bad[i][0]), SHELL_BAD_ARGS);
        CHECK(strcmp(out, bad[i][1]) == 0);
        CHECK_EQ(errors, 1);
    }
    CHECK_EQ(ss.var_count, 6);                      /* nothing added */
}

static void test_set_bounds(void)
{
    char cmd[256], v63[64], v64[65];
    reset();
    memset(v63, 'v', 63);
    v63[63] = '\0';
    memset(v64, 'v', 64);
    v64[64] = '\0';
    snprintf(cmd, sizeof(cmd), "set V=%s", v63);
    CHECK_EQ(run(cmd), SHELL_OK);
    CHECK_EQ(run("set V"), SHELL_OK);
    CHECK_EQ(strlen(out), 2 + 63 + 1);
    snprintf(cmd, sizeof(cmd), "set V=%s", v64);
    CHECK_EQ(run(cmd), SHELL_BAD_ARGS);
    CHECK(strcmp(out, "set: value longer than 63 characters\n") == 0);
    CHECK_EQ(run("set V"), SHELL_OK);               /* unchanged */
    CHECK_EQ(strlen(out), 2 + 63 + 1);
    /* The spaces between the words count: 20 + 1 + 20 + 1 + 21 = 63, then 64. */
    CHECK_EQ(run("set W=aaaaaaaaaaaaaaaaaaaa bbbbbbbbbbbbbbbbbbbb ccccccccccccccccccccc"), SHELL_OK);
    CHECK_EQ(run("set W=aaaaaaaaaaaaaaaaaaaa bbbbbbbbbbbbbbbbbbbb cccccccccccccccccccccc"), SHELL_BAD_ARGS);
    CHECK(strcmp(out, "set: value longer than 63 characters\n") == 0);
    CHECK_EQ(run("set N="), SHELL_OK);
    snprintf(cmd, sizeof(cmd), "set N=%s", v64);
    CHECK_EQ(run(cmd), SHELL_BAD_ARGS);             /* a new name: not added either */
    CHECK_EQ(run("set N"), SHELL_OK);
    CHECK(strcmp(out, "N=\n") == 0);

    /* 32 variables, then no room; changing one still works. */
    reset();
    for (unsigned i = 0; i < 32; i++) {
        snprintf(cmd, sizeof(cmd), "set V%02u=%u", i, i);
        CHECK_EQ(run(cmd), SHELL_OK);
    }
    CHECK_EQ(ss.var_count, 32);
    CHECK_EQ(run("set V32=32"), SHELL_BAD_ARGS);
    CHECK(strcmp(out, "set: no room (32 variables)\n") == 0);
    CHECK_EQ(ss.var_count, 32);
    CHECK_EQ(run("set V05=five"), SHELL_OK);
    CHECK_EQ(run("set V05"), SHELL_OK);
    CHECK(strcmp(out, "V05=five\n") == 0);
    CHECK_EQ(run("unset V00"), SHELL_OK);
    CHECK_EQ(run("set V32=32"), SHELL_OK);
    CHECK_EQ(run("set"), SHELL_OK);
    CHECK(strncmp(out, "V01=1\n", 6) == 0);
    CHECK(ends_with(out, "V31=31\nV32=32\n"));
}

static void test_unset_export_env(void)
{
    reset();
    CHECK_EQ(run("env"), SHELL_OK);
    CHECK(strcmp(out, "No exported variables.\n") == 0);
    CHECK_EQ(errors, 0);
    run("set b=2");
    run("set A=1");
    run("set c=3");
    CHECK_EQ(run("export c"), SHELL_OK);
    CHECK(strcmp(out, "") == 0);
    CHECK_EQ(run("export A"), SHELL_OK);
    CHECK_EQ(run("env"), SHELL_OK);                 /* exported only, sorted */
    CHECK(strcmp(out, "A=1\nc=3\n") == 0);
    CHECK_EQ(run("set"), SHELL_OK);                 /* "set" lists all */
    CHECK(strcmp(out, "A=1\nb=2\nc=3\n") == 0);
    CHECK_EQ(run("set c=three"), SHELL_OK);         /* a new value keeps the mark */
    CHECK_EQ(run("env"), SHELL_OK);
    CHECK(strcmp(out, "A=1\nc=three\n") == 0);
    CHECK_EQ(run("export A"), SHELL_OK);            /* twice: still once */
    CHECK_EQ(run("env"), SHELL_OK);
    CHECK(strcmp(out, "A=1\nc=three\n") == 0);

    /* unset removes the variable and its mark. */
    CHECK_EQ(run("unset c"), SHELL_OK);
    CHECK(strcmp(out, "") == 0);
    CHECK_EQ(run("set c"), SHELL_BAD_ARGS);
    CHECK(strcmp(out, "set: c is not set\n") == 0);
    CHECK_EQ(run("set c=again"), SHELL_OK);
    CHECK_EQ(run("env"), SHELL_OK);
    CHECK(strcmp(out, "A=1\n") == 0);
    CHECK_EQ(run("set"), SHELL_OK);
    CHECK(strcmp(out, "A=1\nb=2\nc=again\n") == 0);

    /* Errors. */
    CHECK_EQ(run("unset nosuch"), SHELL_BAD_ARGS);
    CHECK(strcmp(out, "unset: nosuch is not set\n") == 0);
    CHECK_EQ(run("unset C"), SHELL_BAD_ARGS);       /* case-sensitive */
    CHECK(strcmp(out, "unset: C is not set\n") == 0);
    CHECK_EQ(run("unset"), SHELL_BAD_ARGS);
    CHECK(strcmp(out, "Usage: unset <NAME>\n") == 0);
    CHECK_EQ(run("unset A b"), SHELL_BAD_ARGS);
    CHECK(strcmp(out, "Usage: unset <NAME>\n") == 0);
    CHECK_EQ(run("export nosuch"), SHELL_BAD_ARGS);
    CHECK(strcmp(out, "export: nosuch is not set\n") == 0);
    CHECK_EQ(run("export B=2"), SHELL_BAD_ARGS);
    CHECK(strcmp(out, "export: NAME=value is not supported; use set NAME=value, then export NAME.\n") == 0);
    CHECK_EQ(run("export A=9"), SHELL_BAD_ARGS);    /* refused even for a set name */
    CHECK_EQ(run("set A"), SHELL_OK);
    CHECK(strcmp(out, "A=1\n") == 0);
    CHECK_EQ(run("export"), SHELL_BAD_ARGS);
    CHECK(strcmp(out, "Usage: export <NAME>\n") == 0);
    CHECK_EQ(run("export A b"), SHELL_BAD_ARGS);
    CHECK(strcmp(out, "Usage: export <NAME>\n") == 0);
    CHECK_EQ(run("env x"), SHELL_BAD_ARGS);
    CHECK(strcmp(out, "Usage: env\n") == 0);
    CHECK_EQ(run("env"), SHELL_OK);
    CHECK(strcmp(out, "A=1\n") == 0);
}

/* 1.0 stage 7: "run" hands the exported variables (only those, in "env"
 * order, with their current values) to the program as its environment. */
static void test_run_env(void)
{
    reset();
    CHECK_EQ(run("run hello"), SHELL_RUN);
    CHECK_EQ(starts, 1);
    CHECK(strcmp(started_env, "") == 0);            /* nothing exported */
    run("set B=2");
    run("set A=1 x");
    run("set C=3");
    run("export B");
    CHECK_EQ(run("run hello"), SHELL_RUN);
    CHECK(strcmp(started_env, "B=2\n") == 0);       /* A and C are not exported */
    run("export A");
    run("set B=");
    CHECK_EQ(run("run -b hello 1"), SHELL_OK);
    CHECK(strcmp(started_env, "A=1 x\nB=\n") == 0);
    run("unset A");
    CHECK_EQ(run("run hello"), SHELL_RUN);
    CHECK(strcmp(started_env, "B=\n") == 0);
}

static void test_sleep(void)
{
    reset();
    CHECK_EQ(run("sleep 1"), SHELL_SLEEP);
    CHECK(strcmp(out, "") == 0);
    CHECK_EQ(ss.sleep_seconds, 1);
    CHECK_EQ(run("sleep 3600"), SHELL_SLEEP);
    CHECK_EQ(ss.sleep_seconds, 3600);
    CHECK_EQ(run("sleep 0042"), SHELL_SLEEP);
    CHECK_EQ(ss.sleep_seconds, 42);
    CHECK_EQ(run("echo x"), SHELL_OK);              /* every line starts with nothing pending */
    CHECK_EQ(ss.sleep_seconds, 0);
    static const char *const bad[] = { "sleep 0", "sleep 3601", "sleep 99999999999", "sleep x",
                                       "sleep -1", "sleep 1.5", "sleep 1s", "sleep +1" };
    for (unsigned i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        CHECK_EQ(run(bad[i]), SHELL_BAD_ARGS);
        CHECK(strcmp(out, "sleep: give whole seconds from 1 to 3600\n") == 0);
        CHECK_EQ(errors, 1);
        CHECK_EQ(ss.sleep_seconds, 0);
    }
    CHECK_EQ(run("sleep"), SHELL_BAD_ARGS);
    CHECK(strcmp(out, "Usage: sleep <seconds>\n") == 0);
    CHECK_EQ(run("sleep 1 2"), SHELL_BAD_ARGS);
    CHECK(strcmp(out, "Usage: sleep <seconds>\n") == 0);
    ctx.timer_ok = false;
    CHECK_EQ(run("sleep 1"), SHELL_BAD_ARGS);
    CHECK(strcmp(out, "sleep: unavailable (timer not running)\n") == 0);
    CHECK_EQ(ss.sleep_seconds, 0);
}

static void test_presskey(void)
{
    reset();
    CHECK_EQ(run("presskey"), SHELL_PRESSKEY);
    CHECK(strcmp(out, "Press any key to continue...\n") == 0);
    CHECK_EQ(errors, 0);
    CHECK_EQ(run("presskey x"), SHELL_BAD_ARGS);
    CHECK(strcmp(out, "Usage: presskey\n") == 0);
    CHECK(strcmp(SHELL_SLEEP_CANCELLED, "sleep: cancelled.") == 0);
    CHECK(strcmp(SHELL_PRESSKEY_CANCELLED, "presskey: cancelled.") == 0);
}

static void test_time(void)
{
    reset();
    CHECK_EQ(run("time echo hi there"), SHELL_OK);
    CHECK(strcmp(out, "hi there\n") == 0);
    CHECK_EQ(ss.timed, 1);
    CHECK_EQ(run("echo x"), SHELL_OK);              /* reset by the next line */
    CHECK_EQ(ss.timed, 0);
    CHECK_EQ(run("time sleep 2"), SHELL_SLEEP);
    CHECK_EQ(ss.timed, 1);
    CHECK_EQ(ss.sleep_seconds, 2);
    CHECK_EQ(run("time presskey"), SHELL_PRESSKEY);
    CHECK_EQ(ss.timed, 1);
    CHECK_EQ(run("time run hello"), SHELL_RUN);
    CHECK_EQ(starts, 1);
    CHECK_EQ(ss.timed, 1);
    CHECK_EQ(run("time kill -y 5"), SHELL_KILL_NOW);
    CHECK_EQ(asked_pid, 5);
    CHECK_EQ(ss.timed, 1);
    CHECK_EQ(run("time kill 5"), SHELL_KILL);       /* still asks */
    CHECK_EQ(ss.timed, 1);
    CHECK_EQ(run("time help programs"), SHELL_OK);  /* help <section> */
    CHECK(strncmp(out, "programs: Programs and processes\n", 33) == 0);
    CHECK_EQ(run("time mem --help"), SHELL_OK);
    CHECK(strcmp(out, "mem: Show memory information\nUsage: mem\n") == 0);
    CHECK_EQ(ss.timed, 1);
    CHECK_EQ(run("time echo 1 2 3 4 5 6"), SHELL_OK);   /* 8 words */
    CHECK(strcmp(out, "1 2 3 4 5 6\n") == 0);
    CHECK_EQ(run("time version x"), SHELL_BAD_ARGS);    /* the command's own usage */
    CHECK(strcmp(out, "Usage: version\n") == 0);
    CHECK_EQ(ss.timed, 1);

    /* Refused: nothing runs, nothing timed. */
    CHECK_EQ(run("time"), SHELL_BAD_ARGS);
    CHECK(strcmp(out, "Usage: time <command...>\n") == 0);
    CHECK_EQ(ss.timed, 0);
    CHECK_EQ(run("time time echo x"), SHELL_BAD_ARGS);
    CHECK(strcmp(out, "time: cannot time time\n") == 0);
    CHECK_EQ(ss.timed, 0);
    CHECK_EQ(run("time time"), SHELL_BAD_ARGS);
    CHECK(strcmp(out, "time: cannot time time\n") == 0);
    CHECK_EQ(run("time nosuch"), SHELL_BAD_ARGS);
    CHECK(strcmp(out, "time: 'nosuch' is not a built-in command.\n") == 0);
    CHECK_EQ(ss.timed, 0);
    CHECK_EQ(run("alias ll echo hi"), SHELL_OK);
    CHECK_EQ(run("time ll"), SHELL_BAD_ARGS);       /* an alias is not expanded */
    CHECK(strcmp(out, "time: 'll' is an alias; time runs built-in commands only.\n") == 0);
    CHECK_EQ(ss.timed, 0);
    CHECK_EQ(run("alias tt time echo t"), SHELL_OK);    /* time at the start of an alias */
    CHECK_EQ(run("tt"), SHELL_OK);
    CHECK(strcmp(out, "t\n") == 0);
    CHECK_EQ(ss.timed, 1);
    CHECK_EQ(run("alias t2 time"), SHELL_OK);
    CHECK_EQ(run("t2 tt"), SHELL_BAD_ARGS);         /* no time inside time through aliases */
    CHECK(strcmp(out, "time: 'tt' is an alias; time runs built-in commands only.\n") == 0);
    CHECK_EQ(run("time --help"), SHELL_OK);         /* time's own help */
    CHECK(strcmp(out, "time: Time a command: time <command>\nUsage: time <command...>\n") == 0);
    CHECK_EQ(ss.timed, 0);
}

static void test_time_line(void)
{
    char t[SHELL_TIME_LINE_MAX];
    shell_time_line(t, 0, 100);
    CHECK(strcmp(t, "time: 0 ticks (0.00 s)") == 0);
    shell_time_line(t, 1, 100);
    CHECK(strcmp(t, "time: 1 tick (0.01 s)") == 0);
    shell_time_line(t, 100, 100);
    CHECK(strcmp(t, "time: 100 ticks (1.00 s)") == 0);
    shell_time_line(t, 250, 100);
    CHECK(strcmp(t, "time: 250 ticks (2.50 s)") == 0);
    shell_time_line(t, 360099, 100);
    CHECK(strcmp(t, "time: 360099 ticks (3600.99 s)") == 0);
    shell_time_line(t, 1234, 1000);
    CHECK(strcmp(t, "time: 1234 ticks (1.23 s)") == 0);
    shell_time_line(t, 7, 0);
    CHECK(strcmp(t, "time: 7 ticks") == 0);
    shell_time_line(t, 0xFFFFFFFFull, 100);
    CHECK(strcmp(t, "time: 4294967295 ticks (42949672.95 s)") == 0);
    shell_time_line(t, 0x100000000ull, 100);
    CHECK(strcmp(t, "time: more than 4294967295 ticks") == 0);
}

static void test_kill_y(void)
{
    reset();
    CHECK_EQ(run("kill -y 5"), SHELL_KILL_NOW);
    CHECK(strcmp(out, "") == 0);                    /* no question */
    CHECK_EQ(kill_asks, 1);
    CHECK_EQ(asked_pid, 5);
    CHECK_EQ(run("kill 5"), SHELL_KILL);            /* plain kill still asks */
    CHECK(strcmp(out, "End process 5 'spin'? Type 'yes' to confirm:\n") == 0);
    CHECK_EQ(kill_asks, 1);
    /* The same refusals as kill, and nothing handed over. */
    static const char *const refused[][2] = {
        { "kill -y 0",    "kill: PID 0 is the kernel, not a process.\n" },
        { "kill -y 6",    "kill: no process with PID 6.\n" },
        { "kill -y 9",    "kill: process 9 'hello' has already ended.\n" },
        { "kill -y x",    "kill: 'x' is not a process ID.\n" },
        { "kill -y -y",   "kill: '-y' is not a process ID.\n" },
        { "kill -y 2147483648", "kill: '2147483648' is not a process ID.\n" },
        { "kill -y",      "Usage: kill [-y] <pid>\n" },
        { "kill",         "Usage: kill [-y] <pid>\n" },
        { "kill 5 -y",    "Usage: kill [-y] <pid>\n" },
        { "kill 4 5",     "Usage: kill [-y] <pid>\n" },
        { "kill -y 5 6",  "Usage: kill [-y] <pid>\n" },
        { "kill -Y 5",    "Usage: kill [-y] <pid>\n" },
    };
    for (unsigned i = 0; i < sizeof(refused) / sizeof(refused[0]); i++) {
        CHECK_EQ(run(refused[i][0]), SHELL_BAD_ARGS);
        CHECK(strcmp(out, refused[i][1]) == 0);
        if (strcmp(out, refused[i][1]) != 0)
            printf("    '%s' gave '%s'\n", refused[i][0], out);
        CHECK_EQ(kill_asks, 0);
        CHECK_EQ(errors, 1);
    }
    CHECK_EQ(run("alias ky kill -y"), SHELL_OK);    /* through an alias */
    CHECK_EQ(run("ky 5"), SHELL_KILL_NOW);
    CHECK_EQ(asked_pid, 5);
}

static void test_every_line_fits(void)
{
    char cmd[160], v50[51];
    reset();
    memset(v50, 'v', 50);
    v50[50] = '\0';
    /* The longest row a console line can make: "set " + 16 + "=" + 50 = 71. */
    snprintf(cmd, sizeof(cmd), "set abcdefghijklmnop=%s", v50);
    CHECK_EQ(run(cmd), SHELL_OK);
    run("export abcdefghijklmnop");
    static const char *const cmds[] = {
        "help", "help shell", "set", "env", "set abcdefghijklmnop",
        "set abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefgh=1",
        "unset abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghij",
        "export abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefgh",
        "export A=1", "sleep 99999", "presskey", "time abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefg",
        "time time", "kill -y abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcd", "hlep", "help shel",
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
    printf("shell variables, sleep, presskey, time and kill -y (0.12 stage 2) unit tests\n");
    RUN(test_without_session_nothing_changes);
    RUN(test_help);
    RUN(test_section_completeness);
    RUN(test_did_you_mean);
    RUN(test_alias_and_new_builtins);
    RUN(test_set);
    RUN(test_set_names);
    RUN(test_set_bounds);
    RUN(test_unset_export_env);
    RUN(test_run_env);
    RUN(test_sleep);
    RUN(test_presskey);
    RUN(test_time);
    RUN(test_time_line);
    RUN(test_kill_y);
    RUN(test_every_line_fits);
    return unit_report("shell_session012");
}
