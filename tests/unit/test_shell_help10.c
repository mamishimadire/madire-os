/* Unit tests for 1.0 stage 10 of kernel/shell/shell.c: "help" on one
 * screen (D2). With state (the console) "help" lists each section's title
 * and its names, then how to see the purposes and the scrollback keys;
 * without state, one block of names. Lines of names fill while the next
 * name fits in 79 columns. The purposes stay in "help <section>" and
 * "help <command>", unchanged. */
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
static struct shell_files files;
static struct shell_storage storage;
static struct history hist;
static char file[8192];
static size_t file_len;

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

static int32_t fake_start(void *c, int argc, char **argv, uint32_t envc,
                          const char *const *env_names, const char *const *env_values,
                          bool background)
{
    (void)c; (void)argc; (void)argv; (void)envc; (void)env_names; (void)env_values;
    (void)background;
    return 7;
}

static void fake_ask_kill(void *c, int32_t pid)
{
    (void)c; (void)pid;
}

static bool fake_removed(void *c) { (void)c; return false; }

static int fake_out_open(void *c, const char *path, bool append)
{
    (void)c; (void)path; (void)append;
    file_len = 0;
    return 0;
}

static int64_t fake_out_write(void *c, const uint8_t *buf, uint32_t len)
{
    (void)c;
    if (file_len + len > sizeof(file))
        len = (uint32_t)(sizeof(file) - file_len);
    memcpy(file + file_len, buf, len);
    file_len += len;
    return len;
}

static void fake_out_close(void *c) { (void)c; }
static bool fake_out_is(void *c, const char *path) { (void)c; (void)path; return false; }

static const struct shell_thread one_thread[] = { { 0, "running", 5, 1480, 16384, "kernel" } };
static const struct shell_process one_process[] = { { 5, "running", 1, "spin" } };

/* The context's optional parts, as the earlier milestones added them. */
enum {
    F_THREADS = 1, F_EXTRAS = 2, F_PROCS = 4, F_STATE = 8, F_SESSION = 16, F_FILES = 32,
    F_STORAGE = 64, F_ALL = 127,
};

static void reset(unsigned f)
{
    memset(&mem, 0, sizeof(mem));
    memset(&cpu, 0, sizeof(cpu));
    memset(&ctx, 0, sizeof(ctx));
    memset(&extras, 0, sizeof(extras));
    memset(&procs, 0, sizeof(procs));
    memset(&files, 0, sizeof(files));
    memset(&storage, 0, sizeof(storage));
    history_init(&hist);
    ctx.version = "1.0";
    ctx.mem = &mem;
    ctx.cpu = &cpu;
    ctx.page_size = 4096;
    if (f & F_THREADS) {
        ctx.threads = one_thread;
        ctx.thread_count = 1;
    }
    if (f & F_EXTRAS) {
        extras.hostname = "madire";
        extras.history = &hist;
        ctx.extras = &extras;
    }
    if (f & F_PROCS) {
        procs.rows = one_process;
        procs.count = 1;
        procs.start = fake_start;
        procs.ask_kill = fake_ask_kill;
        ctx.procs = &procs;
    }
    if (f & F_STATE) {
        shell_state_init(&st);
        ctx.state = &st;
    }
    if (f & F_SESSION) {
        shell_session_init(&ss);
        ctx.session = &ss;
    }
    if (f & F_FILES) {
        files.cwd_removed = fake_removed;
        ctx.files = &files;
    }
    if (f & F_STORAGE) {
        storage.out_open = fake_out_open;
        storage.out_write = fake_out_write;
        storage.out_close = fake_out_close;
        storage.out_is = fake_out_is;
        ctx.storage = &storage;
    }
}

/* Every command, in "help" order: the context part it needs, its section
 * (s shell, y system, p programs, f files) and its purpose. */
struct entry {
    const char *name;
    unsigned need;
    char section;
    const char *purpose;
};

static const struct entry entries[] = {
    { "help",     0,         's', "Show this list" },
    { "clear",    0,         's', "Clear the screen" },
    { "version",  0,         'y', "Show the Madire OS version" },
    { "uptime",   0,         'y', "Show time since boot" },
    { "mem",      0,         'y', "Show memory information" },
    { "cpu",      0,         'y', "Show processor information" },
    { "echo",     0,         's', "Print text: echo <text>" },
    { "ps",       F_THREADS, 'p', "Show threads and processes" },
    { "history",  F_EXTRAS,  's', "Show previous commands (Up/Down recall)" },
    { "uname",    F_EXTRAS,  'y', "Show the system name: uname [-a]" },
    { "hostname", F_EXTRAS,  'y', "Show the computer's name" },
    { "date",     F_EXTRAS,  'y', "Show the date and time (RTC)" },
    { "dmesg",    F_EXTRAS,  'y', "Show kernel messages: dmesg [lines]" },
    { "reboot",   F_EXTRAS,  'y', "Restart the computer (asks first)" },
    { "run",      F_PROCS,   'p', "Run a program: run [-b] <name> [args]" },
    { "kill",     F_PROCS,   'p', "End a process: kill <pid> (asks first)" },
    { "prompt",   F_STATE,   's', "Show or change the prompt: prompt [text]" },
    { "alias",    F_STATE,   's', "Show or make short names: alias [name [command]]" },
    { "unalias",  F_STATE,   's', "Remove a short name: unalias <name>" },
    { "set",      F_SESSION, 's', "Show or set variables: set [NAME[=value]]" },
    { "unset",    F_SESSION, 's', "Remove a variable: unset <NAME>" },
    { "export",   F_SESSION, 's', "Mark a variable as exported: export <NAME>" },
    { "env",      F_SESSION, 's', "Show the exported variables" },
    { "sleep",    F_SESSION, 's', "Wait: sleep <seconds> (1-3600, Ctrl+C stops)" },
    { "presskey", F_SESSION, 's', "Wait for a key press (Ctrl+C cancels)" },
    { "time",     F_SESSION, 's', "Time a command: time <command>" },
    { "pwd",      F_FILES,   'f', "Show the current folder" },
    { "cd",       F_FILES,   'f', "Change the current folder: cd [path]" },
    { "ls",       F_FILES,   'f', "List a folder: ls [-l] [path]" },
    { "mkdir",    F_FILES,   'f', "Create a folder: mkdir <path>" },
    { "rmdir",    F_FILES,   'f', "Remove an empty folder: rmdir <path>" },
    { "touch",    F_FILES,   'f', "Create an empty file, or update its time" },
    { "stat",     F_FILES,   'f', "Show a file's or folder's details: stat <path>" },
    { "cat",      F_FILES,   'f', "Show a file's contents: cat <path>" },
    { "rm",       F_FILES | F_STORAGE, 'f', "Remove a file: rm [-y] <path> (asks first)" },
    { "cp",       F_FILES | F_STORAGE, 'f', "Copy a file: cp [-y] <from> <to>" },
    { "mv",       F_FILES | F_STORAGE, 'f', "Move or rename: mv [-y] <from> <to>" },
    { "df",       F_FILES | F_STORAGE, 'f', "Show the filesystem's nodes and pages in use" },
    { "du",       F_FILES | F_STORAGE, 'f', "Show the pages under a folder: du [path]" },
};

#define ENTRIES (sizeof(entries) / sizeof(entries[0]))

static const char sections[] = "sypf";
static const char *const titles[] = {
    "shell: The shell's own commands", "system: Information about the system",
    "programs: Programs and processes", "files: Files and folders",
};

static bool offered(const struct entry *e, unsigned f)
{
    return (e->need & f) == e->need;
}

/* The expected output, built from the table: the layout rule of D2. */
static void names_into(char *buf, size_t size, unsigned f, char section)
{
    char line[96] = "";
    for (unsigned i = 0; i < ENTRIES; i++) {
        if (!offered(&entries[i], f) || (section && entries[i].section != section))
            continue;
        if (line[0] && strlen(line) + 2 + strlen(entries[i].name) > 79) {
            strncat(buf, line, size - strlen(buf) - 1);
            strncat(buf, "\n", size - strlen(buf) - 1);
            line[0] = '\0';
        }
        strcat(line, "  ");
        strcat(line, entries[i].name);
    }
    if (line[0]) {
        strncat(buf, line, size - strlen(buf) - 1);
        strncat(buf, "\n", size - strlen(buf) - 1);
    }
}

static void expected(char *buf, size_t size, unsigned f)
{
    snprintf(buf, size, "Available commands:\n");
    if (!(f & F_STATE)) {
        names_into(buf, size, f, 0);
        if (f & F_EXTRAS)
            strncat(buf, "Purposes and usage: help <command>.\n", size - strlen(buf) - 1);
        return;
    }
    for (unsigned s = 0; s < 4; s++) {
        if (sections[s] == 'f' && !(f & F_FILES))
            continue;
        strncat(buf, titles[s], size - strlen(buf) - 1);
        strncat(buf, "\n", size - strlen(buf) - 1);
        names_into(buf, size, f, sections[s]);
    }
    strncat(buf, "Purposes and usage: help <command>; one section: help <section>.\n"
                 "Scroll back: Shift+PgUp and Shift+PgDn.\n", size - strlen(buf) - 1);
}

/* ---- the exact output in each milestone's context ---- */

static void test_exact_outputs(void)
{
    static const struct { unsigned f; const char *text; } cases[] = {
        { 0, "Available commands:\n"
             "  help  clear  version  uptime  mem  cpu  echo\n" },
        { F_THREADS, "Available commands:\n"
             "  help  clear  version  uptime  mem  cpu  echo  ps\n" },
        { F_EXTRAS, "Available commands:\n"
             "  help  clear  version  uptime  mem  cpu  echo  history  uname  hostname  date\n"
             "  dmesg  reboot\n"
             "Purposes and usage: help <command>.\n" },
        { F_THREADS | F_EXTRAS, "Available commands:\n"
             "  help  clear  version  uptime  mem  cpu  echo  ps  history  uname  hostname\n"
             "  date  dmesg  reboot\n"
             "Purposes and usage: help <command>.\n" },
        { F_THREADS | F_EXTRAS | F_PROCS, "Available commands:\n"
             "  help  clear  version  uptime  mem  cpu  echo  ps  history  uname  hostname\n"
             "  date  dmesg  reboot  run  kill\n"
             "Purposes and usage: help <command>.\n" },
        { F_THREADS | F_EXTRAS | F_PROCS | F_STATE, "Available commands:\n"
             "shell: The shell's own commands\n"
             "  help  clear  echo  history  prompt  alias  unalias\n"
             "system: Information about the system\n"
             "  version  uptime  mem  cpu  uname  hostname  date  dmesg  reboot\n"
             "programs: Programs and processes\n"
             "  ps  run  kill\n"
             "Purposes and usage: help <command>; one section: help <section>.\n"
             "Scroll back: Shift+PgUp and Shift+PgDn.\n" },
        { F_THREADS | F_EXTRAS | F_PROCS | F_STATE | F_SESSION, "Available commands:\n"
             "shell: The shell's own commands\n"
             "  help  clear  echo  history  prompt  alias  unalias  set  unset  export  env\n"
             "  sleep  presskey  time\n"
             "system: Information about the system\n"
             "  version  uptime  mem  cpu  uname  hostname  date  dmesg  reboot\n"
             "programs: Programs and processes\n"
             "  ps  run  kill\n"
             "Purposes and usage: help <command>; one section: help <section>.\n"
             "Scroll back: Shift+PgUp and Shift+PgDn.\n" },
        { F_ALL & ~F_STORAGE, "Available commands:\n"
             "shell: The shell's own commands\n"
             "  help  clear  echo  history  prompt  alias  unalias  set  unset  export  env\n"
             "  sleep  presskey  time\n"
             "system: Information about the system\n"
             "  version  uptime  mem  cpu  uname  hostname  date  dmesg  reboot\n"
             "programs: Programs and processes\n"
             "  ps  run  kill\n"
             "files: Files and folders\n"
             "  pwd  cd  ls  mkdir  rmdir  touch  stat  cat\n"
             "Purposes and usage: help <command>; one section: help <section>.\n"
             "Scroll back: Shift+PgUp and Shift+PgDn.\n" },
        { F_ALL, "Available commands:\n"
             "shell: The shell's own commands\n"
             "  help  clear  echo  history  prompt  alias  unalias  set  unset  export  env\n"
             "  sleep  presskey  time\n"
             "system: Information about the system\n"
             "  version  uptime  mem  cpu  uname  hostname  date  dmesg  reboot\n"
             "programs: Programs and processes\n"
             "  ps  run  kill\n"
             "files: Files and folders\n"
             "  pwd  cd  ls  mkdir  rmdir  touch  stat  cat  rm  cp  mv  df  du\n"
             "Purposes and usage: help <command>; one section: help <section>.\n"
             "Scroll back: Shift+PgUp and Shift+PgDn.\n" },
    };
    for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        reset(cases[i].f);
        CHECK_EQ(run("help"), SHELL_OK);
        CHECK(strcmp(out, cases[i].text) == 0);
        if (strcmp(out, cases[i].text) != 0)
            printf("    context %u:\n%s", cases[i].f, out);
        CHECK_EQ(errors, 0);
    }
    /* The console's: 12 lines, 502 bytes. */
    CHECK_EQ(strlen(cases[8].text), 502);
}

/* ---- every context: the rule, the width, one screen, each name once ---- */

static unsigned count_lines(const char *s)
{
    unsigned n = 0;
    for (; *s; s++)
        n += *s == '\n';
    return n;
}

static void test_every_context(void)
{
    static char want[4096];
    unsigned bad_text = 0, bad_width = 0, bad_rows = 0, bad_once = 0, bad_fill = 0;
    for (unsigned f = 0; f <= F_ALL; f++) {
        reset(f);
        if (run("help") != SHELL_OK || errors)
            bad_text++;
        expected(want, sizeof(want), f);
        if (strcmp(out, want) != 0) {
            bad_text++;
            printf("    context %u:\n%s", f, out);
        }
        if (count_lines(out) > 23)          /* with the typed line: 24 rows */
            bad_rows++;
        /* Widths, and each line of names full: the next line's first name
         * would not have fitted after it. */
        const char *prev = NULL;
        size_t prev_len = 0;
        for (const char *p = out; *p; ) {
            const char *end = strchr(p, '\n');
            size_t len = end ? (size_t)(end - p) : strlen(p);
            if (len > 79)
                bad_width++;
            if (p[0] == ' ' && p[1] == ' ' && prev) {
                size_t first = strcspn(p + 2, " \n");
                if (prev_len + 2 + first <= 79)
                    bad_fill++;
            }
            if (p[0] == ' ' && p[1] == ' ') {
                prev = p;
                prev_len = len;
            } else {
                prev = NULL;
            }
            p = end ? end + 1 : p + len;
        }
        /* Each offered command exactly once, as a whole word on a line of
         * names; nothing else. */
        unsigned words = 0;
        for (const char *p = out; *p; ) {
            const char *end = strchr(p, '\n');
            size_t len = end ? (size_t)(end - p) : strlen(p);
            if (p[0] == ' ' && p[1] == ' ')
                for (const char *q = p; q < p + len; ) {
                    while (q < p + len && *q == ' ')
                        q++;
                    if (q < p + len)
                        words++;
                    while (q < p + len && *q != ' ')
                        q++;
                }
            p = end ? end + 1 : p + len;
        }
        unsigned n_offered = 0;
        for (unsigned i = 0; i < ENTRIES; i++) {
            if (!offered(&entries[i], f))
                continue;
            n_offered++;
            char word[24];
            unsigned found = 0;
            snprintf(word, sizeof(word), " %s ", entries[i].name);
            for (const char *p = out; *p; ) {
                const char *end = strchr(p, '\n');
                size_t len = end ? (size_t)(end - p) : strlen(p);
                if (p[0] == ' ' && p[1] == ' ') {
                    char buf[96];
                    snprintf(buf, sizeof(buf), "%.*s ", (int)len, p);
                    for (const char *q = buf; (q = strstr(q, word)) != NULL; q++)
                        found++;
                }
                p = end ? end + 1 : p + len;
            }
            if (found != 1)
                bad_once++;
        }
        if (words != n_offered)
            bad_once++;
    }
    CHECK_EQ(bad_text, 0);
    CHECK_EQ(bad_width, 0);
    CHECK_EQ(bad_rows, 0);
    CHECK_EQ(bad_once, 0);
    CHECK_EQ(bad_fill, 0);
}

/* ---- the sections: titles and names as "help <section>" lists them ---- */

static void test_sections_agree(void)
{
    static const unsigned contexts[] = {
        F_THREADS | F_EXTRAS | F_PROCS | F_STATE, F_ALL & ~F_FILES & ~F_STORAGE,
        F_ALL & ~F_STORAGE, F_ALL,
    };
    static const char *const names[] = { "shell", "system", "programs", "files" };
    for (unsigned c = 0; c < sizeof(contexts) / sizeof(contexts[0]); c++) {
        static char help[4096], from_sections[4096], one[4096];
        reset(contexts[c]);
        CHECK_EQ(run("help"), SHELL_OK);
        strcpy(help, out);
        /* Rebuild help's sections from "help <section>": its title, then
         * its rows' names filled by the same rule. */
        snprintf(from_sections, sizeof(from_sections), "Available commands:\n");
        for (unsigned s = 0; s < 4; s++) {
            char cmd[32];
            if (s == 3 && !(contexts[c] & F_FILES))
                continue;
            snprintf(cmd, sizeof(cmd), "help %s", names[s]);
            CHECK_EQ(run(cmd), SHELL_OK);
            strcpy(one, out);
            const char *nl = strchr(one, '\n');
            strncat(from_sections, one, (size_t)(nl - one) + 1);
            char line[96] = "";
            for (const char *p = nl + 1; *p; ) {
                const char *end = strchr(p, '\n');
                char name[16];
                size_t k = strcspn(p + 2, " \n");
                snprintf(name, sizeof(name), "%.*s", (int)k, p + 2);
                if (line[0] && strlen(line) + 2 + strlen(name) > 79) {
                    strcat(from_sections, line);
                    strcat(from_sections, "\n");
                    line[0] = '\0';
                }
                strcat(line, "  ");
                strcat(line, name);
                p = end ? end + 1 : p + strlen(p);
            }
            strcat(from_sections, line);
            strcat(from_sections, "\n");
        }
        strcat(from_sections, "Purposes and usage: help <command>; one section: help <section>.\n"
                              "Scroll back: Shift+PgUp and Shift+PgDn.\n");
        CHECK(strcmp(help, from_sections) == 0);
    }
}

/* ---- the purposes: "help <command>", word for word as help listed them ---- */

static void test_purposes(void)
{
    reset(F_ALL);
    for (unsigned i = 0; i < ENTRIES; i++) {
        char cmd[32], want[128];
        snprintf(cmd, sizeof(cmd), "help %s", entries[i].name);
        CHECK_EQ(run(cmd), SHELL_OK);
        snprintf(want, sizeof(want), "%s: %s\nUsage: ", entries[i].name, entries[i].purpose);
        CHECK(strncmp(out, want, strlen(want)) == 0);
        if (strncmp(out, want, strlen(want)) != 0)
            printf("    help %s: %s", entries[i].name, out);
    }
    /* Without processes "ps" shows threads only (0.9). */
    reset(F_THREADS | F_EXTRAS);
    CHECK_EQ(run("help ps"), SHELL_OK);
    CHECK(strcmp(out, "ps: Show threads\nUsage: ps\n") == 0);
}

/* ---- unchanged: help's own help, its usage, a section ---- */

static void test_unchanged(void)
{
    reset(F_ALL);
    CHECK_EQ(run("help --help"), SHELL_OK);
    CHECK(strcmp(out, "help: Show this list\nUsage: help [section | command]\n") == 0);
    CHECK_EQ(run("help x y"), SHELL_BAD_ARGS);
    CHECK(strcmp(out, "Usage: help [section | command]\n") == 0);
    CHECK_EQ(run("help files"), SHELL_OK);
    CHECK(strcmp(out, "files: Files and folders\n"
                      "  pwd      Show the current folder\n"
                      "  cd       Change the current folder: cd [path]\n"
                      "  ls       List a folder: ls [-l] [path]\n"
                      "  mkdir    Create a folder: mkdir <path>\n"
                      "  rmdir    Remove an empty folder: rmdir <path>\n"
                      "  touch    Create an empty file, or update its time\n"
                      "  stat     Show a file's or folder's details: stat <path>\n"
                      "  cat      Show a file's contents: cat <path>\n"
                      "  rm       Remove a file: rm [-y] <path> (asks first)\n"
                      "  cp       Copy a file: cp [-y] <from> <to>\n"
                      "  mv       Move or rename: mv [-y] <from> <to>\n"
                      "  df       Show the filesystem's nodes and pages in use\n"
                      "  du       Show the pages under a folder: du [path]\n") == 0);
    reset(F_EXTRAS);                        /* 0.9.1, no state: a command only */
    CHECK_EQ(run("help x y"), SHELL_BAD_ARGS);
    CHECK(strcmp(out, "Usage: help [command]\n") == 0);
    reset(0);                               /* 0.5: no argument */
    CHECK_EQ(run("help x"), SHELL_BAD_ARGS);
    CHECK(strcmp(out, "Usage: help\n") == 0);
}

/* ---- "help > file": the same bytes as on the screen ---- */

static void test_redirect(void)
{
    static char screen[4096];
    reset(F_ALL);
    CHECK_EQ(run("help"), SHELL_OK);
    strcpy(screen, out);
    CHECK_EQ(run("help > h.txt"), SHELL_OK);
    CHECK(out[0] == '\0');
    CHECK(file_len == strlen(screen) && memcmp(file, screen, file_len) == 0);
    CHECK_EQ(file_len, 502);
}

int main(void)
{
    printf("shell one-screen help (1.0 stage 10) unit tests\n");
    RUN(test_exact_outputs);
    RUN(test_every_context);
    RUN(test_sections_agree);
    RUN(test_purposes);
    RUN(test_unchanged);
    RUN(test_redirect);
    return unit_report("shell_help10");
}
