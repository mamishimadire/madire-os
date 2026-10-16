/* Unit tests for the 1.0 stage 8 file commands of kernel/shell/shell.c:
 * pwd, cd, ls, mkdir, rmdir, touch, stat and cat, the help section
 * "files", the Live line of "version", cat's text (shell_cat_*) and the
 * dates (shell_date). They exist only when the context has files; without
 * them every output stays as before (the other test_shell_* suites pin
 * it). The filesystem is a fake here: each call records its path and
 * answers what the test set. */
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "kernel/fs/fs_errno.h"
#include "kernel/shell/shell.h"
#include "tests/unit/unit.h"

static char out[16384];
static int errors, lines_written;
static struct memory_stats mem;
static struct cpu_info cpu;
static struct shell_context ctx;
static struct shell_extras extras;
static struct shell_procs procs;
static struct shell_state st;
static struct shell_session ss;
static struct history hist;

/* ---- the fake filesystem ---- */

static char last_path[512];
static const char *last_call;
static int answer;                      /* what the next call returns */
static bool removed;                    /* cwd_removed */
static const char *cwd_text = "/home";
static int64_t cwd_answer;              /* 0: the text's length + 1 */
static struct shell_finfo stat_info;
#define FAKE_ENTRIES 40
static struct shell_finfo entries[FAKE_ENTRIES];
static uint32_t entry_count;
static int list_fail_at = -1;           /* list_next fails from this entry on */
static int list_calls, list_opens, list_closes, cat_opens;

static void record(const char *call, const char *path)
{
    last_call = call;
    strncpy(last_path, path, sizeof(last_path) - 1);
    last_path[sizeof(last_path) - 1] = '\0';
}

static int64_t fake_cwd_path(void *c, char *buf, uint32_t size)
{
    (void)c;
    if (cwd_answer)
        return cwd_answer;
    size_t n = strlen(cwd_text);
    if (n + 1 > size)
        return -FS_ENAMETOOLONG;
    memcpy(buf, cwd_text, n + 1);
    return (int64_t)n + 1;
}

static bool fake_removed(void *c)
{
    (void)c;
    return removed;
}

static int fake_chdir(void *c, const char *p) { (void)c; record("chdir", p); return answer; }
static int fake_mkdir(void *c, const char *p) { (void)c; record("mkdir", p); return answer; }
static int fake_rmdir(void *c, const char *p) { (void)c; record("rmdir", p); return answer; }
static int fake_touch(void *c, const char *p) { (void)c; record("touch", p); return answer; }

static int fake_stat(void *c, const char *p, struct shell_finfo *o)
{
    (void)c;
    record("stat", p);
    if (answer < 0)
        return answer;
    *o = stat_info;
    return 0;
}

static int fake_list_open(void *c, const char *p)
{
    (void)c;
    record("list_open", p);
    list_opens++;
    return 0;
}

static int fake_list_next(void *c, uint64_t *cursor, struct shell_finfo *rows, uint32_t max)
{
    (void)c;
    list_calls++;
    uint32_t n = 0;
    while (*cursor < entry_count && n < max) {
        if (list_fail_at >= 0 && *cursor >= (uint64_t)list_fail_at)
            return n ? (int)n : -FS_ENOENT;
        rows[n++] = entries[*cursor];
        (*cursor)++;
    }
    return (int)n;
}

static void fake_list_close(void *c)
{
    (void)c;
    list_closes++;
}

static int fake_cat_open(void *c, const char *p)
{
    (void)c;
    record("cat_open", p);
    cat_opens++;
    return answer;
}

static const struct shell_files files = {
    fake_cwd_path, fake_removed, fake_chdir, fake_stat, fake_mkdir, fake_rmdir, fake_touch,
    fake_list_open, fake_list_next, fake_list_close, fake_cat_open, NULL,
};

/* ---- the rest of the full shell ---- */

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

static void capture(void *c, const char *text, enum shell_text kind)
{
    (void)c;
    if (kind == SHELL_ERROR)
        errors++;
    lines_written++;
    strncat(out, text, sizeof(out) - strlen(out) - 1);
}

static const struct shell_output sink = { capture, NULL };

static const struct shell_thread one_thread[] = { { 0, "running", 5, 1480, 16384, "kernel" } };

static void reset(void)
{
    memset(&mem, 0, sizeof(mem));
    memset(&cpu, 0, sizeof(cpu));
    memset(&ctx, 0, sizeof(ctx));
    memset(&extras, 0, sizeof(extras));
    memset(&procs, 0, sizeof(procs));
    history_init(&hist);
    ctx.version = "1.0";
    ctx.efer = 1u << 10;
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
    procs.start = fake_start;
    procs.ask_kill = fake_ask_kill;
    ctx.procs = &procs;
    shell_state_init(&st);
    ctx.state = &st;
    shell_session_init(&ss);
    ctx.session = &ss;
    ctx.files = &files;
    answer = 0;
    removed = false;
    cwd_text = "/home";
    cwd_answer = 0;
    memset(&stat_info, 0, sizeof(stat_info));
    entry_count = 0;
    list_fail_at = -1;
    list_calls = list_opens = list_closes = cat_opens = 0;
    last_call = "";
    last_path[0] = '\0';
}

static enum shell_result run(const char *cmd)
{
    static char line[512];
    strncpy(line, cmd, sizeof(line) - 1);
    line[sizeof(line) - 1] = '\0';
    out[0] = '\0';
    errors = 0;
    lines_written = 0;
    return shell_execute(line, &ctx, &sink);
}

static bool ends_with(const char *s, const char *tail)
{
    size_t n = strlen(s), k = strlen(tail);
    return n >= k && strcmp(s + n - k, tail) == 0;
}

static const char *const names[] = { "pwd", "cd", "ls", "mkdir", "rmdir", "touch", "stat", "cat" };

static const char *const rows =
    "  pwd      Show the current folder\n"
    "  cd       Change the current folder: cd [path]\n"
    "  ls       List a folder: ls [-l] [path]\n"
    "  mkdir    Create a folder: mkdir <path>\n"
    "  rmdir    Remove an empty folder: rmdir <path>\n"
    "  touch    Create an empty file, or update its time\n"
    "  stat     Show a file's or folder's details: stat <path>\n"
    "  cat      Show a file's contents: cat <path>\n";

/* 1.0 stage 10 (D2): "help" on one screen, without files. */
static const char *const help_session =
    "Available commands:\n"
    "shell: The shell's own commands\n"
    "  help  clear  echo  history  prompt  alias  unalias  set  unset  export  env\n"
    "  sleep  presskey  time\n"
    "system: Information about the system\n"
    "  version  uptime  mem  cpu  uname  hostname  date  dmesg  reboot\n"
    "programs: Programs and processes\n"
    "  ps  run  kill\n"
    "Purposes and usage: help <command>; one section: help <section>.\n"
    "Scroll back: Shift+PgUp and Shift+PgDn.\n";

/* ---- without files: nothing changes ---- */

static void test_without_files_nothing_changes(void)
{
    reset();
    ctx.files = NULL;
    CHECK_EQ(run("help"), SHELL_OK);
    CHECK(strcmp(out, help_session) == 0);  /* 1.0 stage 10: no section "files" */
    CHECK(strstr(out, "pwd") == NULL);
    for (unsigned i = 0; i < 8; i++) {
        char want[96];
        CHECK_EQ(run(names[i]), SHELL_UNKNOWN);
        snprintf(want, sizeof(want), "Unknown command: %s. Type 'help' for a list of commands.\n",
                 names[i]);
        CHECK(strncmp(out, want, strlen(want)) == 0);
    }
    CHECK_EQ(run("version"), SHELL_OK);
    CHECK(strcmp(out, "Madire OS v1.0 (64-bit x86-64, Multiboot 1)\n") == 0);
    CHECK_EQ(run("help files"), SHELL_UNKNOWN);
    CHECK(strncmp(out, "No help for 'files': unknown command.\n", 38) == 0);
    CHECK_EQ(run("ru"), SHELL_UNKNOWN);
    CHECK(ends_with(out, "Did you mean: run, cpu, ps?\n"));
    CHECK_EQ(run("alias ls echo x"), SHELL_OK);         /* not a built-in name without files */
    CHECK(strcmp(out, "Alias 'ls' added.\n") == 0);
}

/* ---- help, version, suggestions ---- */

static void test_help_and_version(void)
{
    char want[2048];
    reset();
    CHECK_EQ(run("help"), SHELL_OK);
    CHECK(ends_with(out, "files: Files and folders\n"
                         "  pwd  cd  ls  mkdir  rmdir  touch  stat  cat\n"
                         "Purposes and usage: help <command>; one section: help <section>.\n"
                         "Scroll back: Shift+PgUp and Shift+PgDn.\n"));
    CHECK_EQ(lines_written, 12);            /* 1.0 stage 10: one screen (was 1 + 34 + 1) */
    CHECK_EQ(run("help files"), SHELL_OK);
    snprintf(want, sizeof(want), "files: Files and folders\n%s", rows);
    CHECK(strcmp(out, want) == 0);
    CHECK_EQ(run("help shell"), SHELL_OK);
    CHECK(strstr(out, "pwd") == NULL && strstr(out, "  cd ") == NULL);
    CHECK_EQ(run("version"), SHELL_OK);
    CHECK(strcmp(out, "Madire OS v1.0 (64-bit x86-64, Multiboot 1)\n"
                      "Live mode (files in memory only)\n") == 0);
    CHECK_EQ(run("help cat"), SHELL_OK);
    CHECK(strcmp(out, "cat: Show a file's contents: cat <path>\nUsage: cat <path>\n") == 0);
    CHECK_EQ(run("ls --help"), SHELL_OK);
    CHECK(strcmp(out, "ls: List a folder: ls [-l] [path]\nUsage: ls [-l] [path]\n") == 0);
    CHECK_EQ(run("cd --help"), SHELL_OK);
    CHECK(strcmp(out, "cd: Change the current folder: cd [path]\nUsage: cd [path]\n") == 0);
}

static void test_did_you_mean(void)
{
    reset();
    CHECK_EQ(run("ru"), SHELL_UNKNOWN);
    CHECK(ends_with(out, "Did you mean: run, cd, cpu?\n"));
    CHECK_EQ(run("alias ll echo x"), SHELL_OK);
    CHECK_EQ(run("lll"), SHELL_UNKNOWN);
    CHECK(ends_with(out, "Did you mean: ll, kill, ls?\n"));
    CHECK_EQ(run("help fils"), SHELL_UNKNOWN);
    CHECK(ends_with(out, "Did you mean: files, kill, ls?\n"));
    CHECK_EQ(run("hlep"), SHELL_UNKNOWN);
    CHECK(ends_with(out, "Did you mean: help, sleep?\n"));
    CHECK_EQ(run("help shel"), SHELL_UNKNOWN);
    CHECK(ends_with(out, "Did you mean: shell, help, set?\n"));
    reset();                                /* no alias: "ll" is within 2 of "kil" */
    CHECK_EQ(run("kil 5"), SHELL_UNKNOWN);
    CHECK(ends_with(out, "Did you mean: kill?\n"));
    CHECK_EQ(run("Echo x"), SHELL_UNKNOWN);
    CHECK(ends_with(out, "Did you mean: echo?\n"));
    CHECK_EQ(run("alias ls echo x"), SHELL_BAD_ARGS);
    CHECK(strcmp(out, "alias: 'ls' is a built-in command.\n") == 0);
    CHECK_EQ(run("alias l2 cat x"), SHELL_OK);          /* a file command may start an alias */
}

/* ---- pwd and cd ---- */

static void test_pwd(void)
{
    char want[512], longp[256];
    reset();
    CHECK_EQ(run("pwd"), SHELL_OK);
    CHECK(strcmp(out, "/home\n") == 0);
    cwd_text = "/";
    CHECK_EQ(run("pwd"), SHELL_OK);
    CHECK(strcmp(out, "/\n") == 0);
    memset(longp, 'a', 200);
    longp[0] = '/';
    longp[200] = '\0';
    cwd_text = longp;
    CHECK_EQ(run("pwd"), SHELL_OK);
    snprintf(want, sizeof(want), "%.79s\n%.79s\n%.42s\n", longp, longp + 79, longp + 158);
    CHECK(strcmp(out, want) == 0);
    cwd_text = "/tmp/a\tb\x01";
    CHECK_EQ(run("pwd"), SHELL_OK);
    CHECK(strcmp(out, "/tmp/a?b?\n") == 0);
    cwd_answer = -FS_ENOENT;
    CHECK_EQ(run("pwd"), SHELL_BAD_ARGS);
    CHECK(strcmp(out, "pwd: the current folder was removed (cd / to leave it).\n") == 0);
    CHECK_EQ(errors, 1);
    cwd_answer = -FS_ENAMETOOLONG;
    CHECK_EQ(run("pwd"), SHELL_BAD_ARGS);
    CHECK(strcmp(out, "pwd: the path is longer than 4095 bytes.\n") == 0);
    CHECK_EQ(run("pwd x"), SHELL_BAD_ARGS);
    CHECK(strcmp(out, "Usage: pwd\n") == 0);
}

static void test_cd(void)
{
    reset();
    CHECK_EQ(run("cd"), SHELL_OK);
    CHECK(strcmp(last_call, "chdir") == 0 && strcmp(last_path, "/home") == 0);
    CHECK(out[0] == '\0');
    CHECK_EQ(run("cd docs"), SHELL_OK);
    CHECK(strcmp(last_path, "docs") == 0 && out[0] == '\0');
    answer = -FS_ENOENT;
    CHECK_EQ(run("cd /nosuch"), SHELL_BAD_ARGS);
    CHECK(strcmp(out, "cd: '/nosuch': no such file or folder.\n") == 0);
    answer = -FS_ENOTDIR;
    CHECK_EQ(run("cd a.txt"), SHELL_BAD_ARGS);
    CHECK(strcmp(out, "cd: 'a.txt': not a folder.\n") == 0);
    removed = true;
    answer = -FS_ENOENT;
    CHECK_EQ(run("cd .."), SHELL_BAD_ARGS);
    CHECK(strcmp(out, "cd: '..': no such file or folder.\n"
                      "(The current folder was removed: use an absolute path, for example cd /.)\n")
          == 0);
    CHECK_EQ(errors, 2);
    CHECK_EQ(run("cd /nosuch"), SHELL_BAD_ARGS);         /* absolute: no hint */
    CHECK(strcmp(out, "cd: '/nosuch': no such file or folder.\n") == 0);
    answer = -FS_ENOTDIR;
    CHECK_EQ(run("cd x"), SHELL_BAD_ARGS);               /* not ENOENT: no hint */
    CHECK(strcmp(out, "cd: 'x': not a folder.\n") == 0);
    reset();
    last_call = "";
    CHECK_EQ(run("cd -x"), SHELL_BAD_ARGS);
    CHECK(strcmp(out, "Usage: cd [path]\n") == 0 && strcmp(last_call, "") == 0);
    CHECK_EQ(run("cd a b"), SHELL_BAD_ARGS);
    CHECK(strcmp(out, "Usage: cd [path]\n") == 0);
}

/* ---- mkdir, rmdir, touch ---- */

static void test_simple_commands(void)
{
    static const struct { int err; const char *text; } reasons[] = {
        { -FS_ENOENT, "no such file or folder" },
        { -FS_ENOTDIR, "not a folder" },
        { -FS_EISDIR, "is a folder" },
        { -FS_EEXIST, "already exists" },
        { -FS_ENOTEMPTY, "the folder is not empty" },
        { -FS_EBUSY, "the root folder cannot be removed" },
        { -FS_EINVAL, "the path ends in '.'" },
        { -FS_ENOSPC, "no room (1024 at most)" },
        { -FS_ENOMEM, "out of kernel memory" },
        { -FS_ENAMETOOLONG, "name too long" },
        { -FS_EFBIG, "error 27" },
    };
    static const char *const cmds[] = { "mkdir", "rmdir", "touch" };
    char line[64], want[160];
    for (unsigned c = 0; c < 3; c++) {
        reset();
        snprintf(line, sizeof(line), "%s /tmp/x", cmds[c]);
        CHECK_EQ(run(line), SHELL_OK);
        CHECK(out[0] == '\0' && strcmp(last_call, cmds[c]) == 0 &&
              strcmp(last_path, "/tmp/x") == 0);
        snprintf(line, sizeof(line), "%s", cmds[c]);
        snprintf(want, sizeof(want), "Usage: %s <path>\n", cmds[c]);
        CHECK_EQ(run(line), SHELL_BAD_ARGS);
        CHECK(strcmp(out, want) == 0);
        snprintf(line, sizeof(line), "%s -p x", cmds[c]);
        CHECK_EQ(run(line), SHELL_BAD_ARGS);
        CHECK(strcmp(out, want) == 0);
        snprintf(line, sizeof(line), "%s -p", cmds[c]);
        last_call = "";
        CHECK_EQ(run(line), SHELL_BAD_ARGS);
        CHECK(strcmp(out, want) == 0 && strcmp(last_call, "") == 0);
    }
    for (unsigned i = 0; i < sizeof(reasons) / sizeof(reasons[0]); i++) {
        reset();
        answer = reasons[i].err;
        CHECK_EQ(run("rmdir docs"), SHELL_BAD_ARGS);
        snprintf(want, sizeof(want), "rmdir: 'docs': %s.\n", reasons[i].text);
        CHECK(strcmp(out, want) == 0);
        CHECK_EQ(errors, 1);
    }
    /* A path over 30 characters is cut to 27 and "..."; the longest
     * error line fits 79 columns. */
    reset();
    answer = -FS_EBUSY;
    CHECK_EQ(run("rmdir /abcdefghijklmnopqrstuvwxyz0123456789"), SHELL_BAD_ARGS);
    CHECK(strcmp(out, "rmdir: '/abcdefghijklmnopqrstuvwxyz...': the root folder cannot be removed.\n")
          == 0);
    CHECK(strlen(out) - 1 <= 79);
    reset();
    answer = -FS_ENOENT;
    removed = true;
    CHECK_EQ(run("mkdir x"), SHELL_BAD_ARGS);
    CHECK(ends_with(out, "(The current folder was removed: use an absolute path, for example cd /.)\n"));
    CHECK_EQ(run("mkdir /x/y"), SHELL_BAD_ARGS);
    CHECK(strcmp(out, "mkdir: '/x/y': no such file or folder.\n") == 0);
}

/* ---- stat ---- */

static void test_stat(void)
{
    reset();
    stat_info.type = SHELL_FT_FILE;
    stat_info.size = 12300;
    stat_info.pages = 5;
    stat_info.nlink = 1;
    stat_info.mode = 0644;
    stat_info.ino = 7;
    stat_info.atime = 1791763200;
    stat_info.mtime = 951782400;
    stat_info.ctime = 1798761599;
    CHECK_EQ(run("stat docs/a.txt"), SHELL_OK);
    CHECK(strcmp(out, "File:   docs/a.txt\n"
                      "Type:   file\n"
                      "Size:   12300 bytes\n"
                      "Pages:  5 (4 KiB each, the index page included)\n"
                      "Node:   7\n"
                      "Links:  1\n"
                      "Mode:   0644\n"
                      "Access: 2026-10-12 00:00:00\n"
                      "Modify: 2000-02-29 00:00:00\n"
                      "Change: 2026-12-31 23:59:59\n") == 0);
    CHECK(strcmp(last_path, "docs/a.txt") == 0);
    stat_info.type = SHELL_FT_DIR;
    stat_info.size = 0;
    stat_info.pages = 0;
    stat_info.nlink = 3;
    stat_info.mode = 01777;
    stat_info.ino = 3;
    stat_info.atime = stat_info.mtime = stat_info.ctime = 0;
    CHECK_EQ(run("stat /tmp"), SHELL_OK);
    CHECK(strcmp(out, "File:   /tmp\n"
                      "Type:   folder\n"
                      "Size:   0 bytes\n"
                      "Pages:  0 (4 KiB each, the index page included)\n"
                      "Node:   3\n"
                      "Links:  3\n"
                      "Mode:   1777\n"
                      "Access: 1970-01-01 00:00:00\n"
                      "Modify: 1970-01-01 00:00:00\n"
                      "Change: 1970-01-01 00:00:00\n") == 0);
    stat_info.type = SHELL_FT_FILE;
    stat_info.size = 1;
    CHECK_EQ(run("stat one"), SHELL_OK);
    CHECK(strstr(out, "Size:   1 byte\n") != NULL);
    answer = -FS_ENOENT;
    CHECK_EQ(run("stat nosuch"), SHELL_BAD_ARGS);
    CHECK(strcmp(out, "stat: 'nosuch': no such file or folder.\n") == 0);
    CHECK_EQ(run("stat"), SHELL_BAD_ARGS);
    CHECK(strcmp(out, "Usage: stat <path>\n") == 0);
}

/* ---- ls ---- */

static void entry(uint32_t i, const char *name, uint32_t type, uint64_t size, int64_t mtime)
{
    memset(&entries[i], 0, sizeof(entries[i]));
    entries[i].type = type;
    entries[i].size = size;
    entries[i].mtime = mtime;
    strncpy(entries[i].name, name, SHELL_NAME_MAX);
}

static void test_ls(void)
{
    char want[4096], name[300];
    reset();
    stat_info.type = SHELL_FT_DIR;
    entry(0, "home", SHELL_FT_DIR, 0, 1791763200);
    entry(1, "tmp", SHELL_FT_DIR, 0, 1791763260);
    entry(2, "data.txt", SHELL_FT_FILE, 12300, 1798761599);
    entry_count = 3;
    CHECK_EQ(run("ls"), SHELL_OK);
    CHECK(strcmp(out, "home/\ntmp/\ndata.txt\n") == 0);
    CHECK(strcmp(last_path, ".") == 0);
    CHECK(list_opens == 1 && list_closes == 1);
    CHECK_EQ(run("ls -l /"), SHELL_OK);
    CHECK(strcmp(out, "d       0 2026-10-12 00:00 home/\n"
                      "d       0 2026-10-12 00:01 tmp/\n"
                      "-   12300 2026-12-31 23:59 data.txt\n") == 0);
    CHECK(strcmp(last_path, "/") == 0);
    CHECK_EQ(run("ls / -l"), SHELL_BAD_ARGS);           /* the option comes first */
    CHECK(strcmp(out, "Usage: ls [-l] [path]\n") == 0);
    CHECK_EQ(run("ls -x"), SHELL_BAD_ARGS);
    CHECK(strcmp(out, "Usage: ls [-l] [path]\n") == 0);
    CHECK_EQ(run("ls a b"), SHELL_BAD_ARGS);
    CHECK(strcmp(out, "Usage: ls [-l] [path]\n") == 0);
    CHECK_EQ(run("ls -l -l"), SHELL_BAD_ARGS);
    CHECK(strcmp(out, "Usage: ls [-l] [path]\n") == 0);
    CHECK_EQ(run("ls -l a b"), SHELL_BAD_ARGS);
    CHECK(strcmp(out, "Usage: ls [-l] [path]\n") == 0);

    /* An empty folder prints nothing; 40 entries come in batches of 16. */
    entry_count = 0;
    CHECK_EQ(run("ls /tmp"), SHELL_OK);
    CHECK(out[0] == '\0');
    for (uint32_t i = 0; i < 40; i++) {
        char n[16];
        snprintf(n, sizeof(n), "f%02u", i);
        entry(i, n, SHELL_FT_FILE, i, 0);
    }
    entry_count = 40;
    list_calls = list_opens = list_closes = 0;
    CHECK_EQ(run("ls"), SHELL_OK);
    CHECK_EQ(lines_written, 40);
    CHECK_EQ(list_calls, 4);                             /* 16 + 16 + 8, then 0 */
    CHECK(list_opens == 1 && list_closes == 1);
    CHECK(strncmp(out, "f00\nf01\n", 8) == 0 && ends_with(out, "f38\nf39\n"));

    /* The folder removed during the listing: what came before, then the error. */
    list_fail_at = 20;
    list_closes = 0;
    removed = true;
    CHECK_EQ(run("ls"), SHELL_BAD_ARGS);
    CHECK_EQ(lines_written, 20 + 2);
    CHECK(ends_with(out, "f19\nls: '.': no such file or folder.\n"
                         "(The current folder was removed: use an absolute path, for example cd /.)\n"));
    CHECK_EQ(list_closes, 1);

    /* A file: its own row, with the path as typed. */
    reset();
    stat_info.type = SHELL_FT_FILE;
    stat_info.size = 300;
    stat_info.mtime = 0;
    CHECK_EQ(run("ls docs/a.txt"), SHELL_OK);
    CHECK(strcmp(out, "docs/a.txt\n") == 0 && list_opens == 0);
    CHECK_EQ(run("ls -l docs/a.txt"), SHELL_OK);
    CHECK(strcmp(out, "-     300 1970-01-01 00:00 docs/a.txt\n") == 0);
    answer = -FS_ENOENT;
    CHECK_EQ(run("ls nosuch"), SHELL_BAD_ARGS);
    CHECK(strcmp(out, "ls: 'nosuch': no such file or folder.\n") == 0);

    /* Long names are cut so every row fits 79 columns; odd bytes as '?'. */
    reset();
    stat_info.type = SHELL_FT_DIR;
    memset(name, 'n', 255);
    name[255] = '\0';
    entry(0, name, SHELL_FT_DIR, 0, 0);
    entry(1, name, SHELL_FT_FILE, 0, 0);
    entry(2, "a\tb\x7f" "c\xff", SHELL_FT_FILE, 0, 0);
    entry_count = 3;
    CHECK_EQ(run("ls"), SHELL_OK);
    snprintf(want, sizeof(want), "%.75s.../\n%.76s...\na?b?c?\n", name, name);
    CHECK(strcmp(out, want) == 0);
    CHECK_EQ(run("ls -l"), SHELL_OK);
    snprintf(want, sizeof(want), "d       0 1970-01-01 00:00 %.48s.../\n"
             "-       0 1970-01-01 00:00 %.49s...\n"
             "-       0 1970-01-01 00:00 a?b?c?\n", name, name);
    CHECK(strcmp(out, want) == 0);
    for (const char *p = out; *p; ) {
        const char *e = strchr(p, '\n');
        CHECK(e - p <= 79);
        p = e + 1;
    }
}

/* ---- cat ---- */

static void test_cat_command(void)
{
    reset();
    CHECK_EQ(run("cat data.txt"), SHELL_CAT);
    CHECK(strcmp(last_call, "cat_open") == 0 && strcmp(last_path, "data.txt") == 0);
    CHECK(out[0] == '\0');
    answer = -FS_EISDIR;
    CHECK_EQ(run("cat docs"), SHELL_BAD_ARGS);
    CHECK(strcmp(out, "cat: 'docs': is a folder.\n") == 0);
    answer = 0;
    cat_opens = 0;
    CHECK_EQ(run("cat"), SHELL_BAD_ARGS);
    CHECK(strcmp(out, "Usage: cat <path>\n") == 0);
    CHECK_EQ(run("cat a b"), SHELL_BAD_ARGS);
    CHECK_EQ(run("cat -n x"), SHELL_BAD_ARGS);
    CHECK_EQ(cat_opens, 0);
    CHECK_EQ(run("time cat data.txt"), SHELL_CAT);       /* the time line follows the file */
    CHECK(ss.timed);
}

static void cat_text(struct shell_cat *s, const char *bytes, uint32_t len)
{
    shell_cat_put(s, (const uint8_t *)bytes, len, &sink);
}

static void test_cat_text(void)
{
    struct shell_cat s;
    char text[512], want[512];
    out[0] = '\0';
    shell_cat_init(&s);
    cat_text(&s, "abc\n\nd", 6);
    CHECK(strcmp(out, "abc\n\n") == 0);
    shell_cat_end(&s, &sink);
    CHECK(strcmp(out, "abc\n\nd\n") == 0);
    shell_cat_end(&s, &sink);                           /* nothing pending: nothing */
    CHECK(strcmp(out, "abc\n\nd\n") == 0);

    out[0] = '\0';
    shell_cat_init(&s);
    cat_text(&s, "A\x1b[2J\x07" "B\tC\rD\x7f" "E\xff\x00" "F\n", 17);
    CHECK(strcmp(out, "A?[2J?B?C?D?E??F\n") == 0);

    /* 79 characters and a newline: one line; 80: 79 and 1; 200: 79, 79, 42. */
    memset(text, 'x', sizeof(text));
    out[0] = '\0';
    shell_cat_init(&s);
    text[79] = '\n';
    cat_text(&s, text, 80);
    shell_cat_end(&s, &sink);
    snprintf(want, sizeof(want), "%.79s\n", text);
    CHECK(strcmp(out, want) == 0);
    memset(text, 'x', sizeof(text));
    out[0] = '\0';
    shell_cat_init(&s);
    cat_text(&s, text, 80);
    shell_cat_end(&s, &sink);
    snprintf(want, sizeof(want), "%.79s\nx\n", text);
    CHECK(strcmp(out, want) == 0);
    out[0] = '\0';
    lines_written = 0;
    shell_cat_init(&s);
    cat_text(&s, text, 200);
    shell_cat_end(&s, &sink);
    snprintf(want, sizeof(want), "%.79s\n%.79s\n%.42s\n", text, text, text);
    CHECK(strcmp(out, want) == 0);
    CHECK_EQ(lines_written, 3);

    /* A line split across chunks (the console's 4096-byte reads) joins. */
    out[0] = '\0';
    shell_cat_init(&s);
    cat_text(&s, "line 0", 6);
    cat_text(&s, "0001 ab", 7);
    cat_text(&s, "c\nnext\n", 7);
    CHECK(strcmp(out, "line 00001 abc\nnext\n") == 0);
    shell_cat_end(&s, &sink);
    CHECK(strcmp(out, "line 00001 abc\nnext\n") == 0);

    /* A 79-character line split at its end, then the newline alone. */
    out[0] = '\0';
    shell_cat_init(&s);
    cat_text(&s, text, 79);
    cat_text(&s, "\n", 1);
    snprintf(want, sizeof(want), "%.79s\n", text);
    CHECK(strcmp(out, want) == 0);
}

/* ---- dates ---- */

static void test_dates(void)
{
    static const struct { int64_t t; const char *text; } dates[] = {
        { 0, "1970-01-01 00:00:00" },
        { 86399, "1970-01-01 23:59:59" },
        { 31536000, "1971-01-01 00:00:00" },
        { 951782400, "2000-02-29 00:00:00" },
        { 951868799, "2000-02-29 23:59:59" },
        { 1791763200, "2026-10-12 00:00:00" },
        { 1798761599, "2026-12-31 23:59:59" },
        { 4102444800, "2100-01-01 00:00:00" },
        { -5, "1970-01-01 00:00:00" },
    };
    for (unsigned i = 0; i < sizeof(dates) / sizeof(dates[0]); i++) {
        char d[20];
        memset(d, 'Z', sizeof(d));
        shell_date(dates[i].t, d);
        CHECK(strcmp(d, dates[i].text) == 0);
        if (strcmp(d, dates[i].text) != 0)
            printf("    %lld: '%s', expected '%s'\n", (long long)dates[i].t, d, dates[i].text);
    }
}

/* Every line any file command prints fits 79 columns. */
static void test_every_line_fits(void)
{
    static const char *const cmds[] = {
        "help", "help files", "version", "pwd", "cd /abcdefghijklmnopqrstuvwxyz0123456789abc",
        "stat /abcdefghijklmnopqrstuvwxyz0123456789abcdefghijklmnopqrstuvwxyz01234567",
        "rmdir /abcdefghijklmnopqrstuvwxyz0123456789abcdefghijklmnopqrstuvwxyz01234567",
    };
    reset();
    stat_info.type = SHELL_FT_FILE;
    stat_info.ino = 18446744073709551615ull;
    stat_info.size = 2097152;
    for (unsigned c = 0; c < sizeof(cmds) / sizeof(cmds[0]); c++) {
        answer = c == 6 ? -FS_EBUSY : c == 4 ? -FS_ENOENT : 0;
        removed = c == 4;
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
    printf("shell file commands and the Live label (1.0 stage 8) unit tests\n");
    RUN(test_without_files_nothing_changes);
    RUN(test_help_and_version);
    RUN(test_did_you_mean);
    RUN(test_pwd);
    RUN(test_cd);
    RUN(test_simple_commands);
    RUN(test_stat);
    RUN(test_ls);
    RUN(test_cat_command);
    RUN(test_cat_text);
    RUN(test_dates);
    RUN(test_every_line_fits);
    return unit_report("shell_fs10");
}
