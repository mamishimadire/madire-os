/* Unit tests for the 1.0 stage 9 commands of kernel/shell/shell.c: rm,
 * cp, mv, df, du, their questions (shell_answer), the copy's end
 * (shell_copy_end) and output redirection ("> file", ">> file"). They
 * exist only when the context has storage (and files); without it every
 * output stays as stage 8 left it (test_shell_fs10.c pins that). The
 * filesystem is a fake: a few paths with their type and node number, and
 * an output file that takes at most `room` bytes. */
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "kernel/fs/fs_errno.h"
#include "kernel/shell/shell.h"
#include "tests/unit/unit.h"

static char out[32768];
static int errors, notes, lines_written, longest;
static struct memory_stats mem;
static struct cpu_info cpu;
static struct shell_context ctx;
static struct shell_extras extras;
static struct shell_procs procs;
static struct shell_state st;
static struct shell_session ss;
static struct history hist;

/* ---- the fake filesystem ---- */

struct fake_node {
    const char *path;
    uint32_t type;
    uint64_t ino;
};
static const struct fake_node nodes[] = {
    { "a.txt", SHELL_FT_FILE, 10 }, { "b.txt", SHELL_FT_FILE, 11 },
    { "/home/a.txt", SHELL_FT_FILE, 10 }, { "docs", SHELL_FT_DIR, 12 },
    { "/", SHELL_FT_DIR, 1 }, { "e1", SHELL_FT_DIR, 13 }, { "e2", SHELL_FT_DIR, 14 },
};
static bool removed;

static int fake_stat(void *c, const char *p, struct shell_finfo *o)
{
    (void)c;
    for (unsigned i = 0; i < sizeof(nodes) / sizeof(nodes[0]); i++) {
        if (strcmp(nodes[i].path, p) == 0) {
            memset(o, 0, sizeof(*o));
            o->type = nodes[i].type;
            o->ino = nodes[i].ino;
            return 0;
        }
    }
    return -FS_ENOENT;
}

static int64_t fake_cwd_path(void *c, char *buf, uint32_t size)
{
    (void)c; (void)size;
    strcpy(buf, "/home");
    return 6;
}
static bool fake_removed(void *c) { (void)c; return removed; }
static int fake_path(void *c, const char *p) { (void)c; (void)p; return 0; }
static int fake_list_open(void *c, const char *p) { (void)c; (void)p; return 0; }
static int fake_list_next(void *c, uint64_t *cur, struct shell_finfo *rows, uint32_t max)
{
    (void)c; (void)cur; (void)rows; (void)max;
    return 0;
}
static void fake_list_close(void *c) { (void)c; }
static int cat_answer;
static int fake_cat_open(void *c, const char *p) { (void)c; (void)p; return cat_answer; }

static const struct shell_files files = {
    fake_cwd_path, fake_removed, fake_path, fake_stat, fake_path, fake_path, fake_path,
    fake_list_open, fake_list_next, fake_list_close, fake_cat_open, NULL,
};

/* ---- the fake storage ---- */

static char calls[1024];                /* "unlink(a.txt) " ... in order */
static int answer;                      /* what unlink, rename, copy_open, du_open return */
static int out_answer;                  /* what out_open returns */
static uint8_t file[16384];             /* the output file's bytes */
static uint32_t file_len, room, writes, largest;
static int full_err;                    /* out_write's error once room is used up */
static bool out_append, out_open_now;
static uint32_t du_steps;
static bool du_fail;                    /* du_next: the folder was removed after 3 batches */
static bool du_file;                    /* du_open was given a file */

static void call(const char *name, const char *a, const char *b)
{
    char one[600];
    if (b)
        snprintf(one, sizeof(one), "%s(%s,%s) ", name, a, b);
    else
        snprintf(one, sizeof(one), "%s(%s) ", name, a);
    strncat(calls, one, sizeof(calls) - strlen(calls) - 1);
}

static int st_unlink(void *c, const char *p) { (void)c; call("unlink", p, NULL); return answer; }
static int st_rename(void *c, const char *a, const char *b)
{
    (void)c;
    call("rename", a, b);
    return answer;
}
static int st_copy_open(void *c, const char *a, const char *b, uint64_t *size)
{
    (void)c;
    call("copy_open", a, b);
    *size = 12300;
    return answer;
}
static void st_df(void *c, struct shell_dfinfo *d)
{
    (void)c;
    d->nodes = 7;
    d->max_nodes = 1024;
    d->pages = 522;
    d->budget = 5432;
}
static int st_du_open(void *c, const char *p, struct shell_du *t)
{
    (void)c;
    call("du_open", p, NULL);
    du_steps = 0;
    du_file = answer == 0 && strcmp(p, "a.txt") == 0;
    if (du_file) {                      /* a file is counted at once (nothing held) */
        t->files = 1;
        t->pages = 1;
    }
    return answer;
}
static int st_du_next(void *c, struct shell_du *t)
{
    (void)c;
    if (answer < 0 || du_file)
        return 0;
    if (du_steps++ < 3) {               /* three batches: 2 files and a folder each */
        t->files += 2;
        t->folders += 1;
        t->pages += 172;
        return 1;
    }
    return du_fail ? -FS_ENOENT : 0;
}
static void st_du_close(void *c) { (void)c; call("du_close", "", NULL); }
static int st_out_open(void *c, const char *p, bool append)
{
    (void)c;
    call(append ? "out_append" : "out_open", p, NULL);
    if (out_answer < 0)
        return out_answer;
    out_open_now = true;
    out_append = append;
    file_len = 0;
    writes = largest = 0;
    return 0;
}
static int64_t st_out_write(void *c, const uint8_t *buf, uint32_t len)
{
    (void)c;
    CHECK(out_open_now);
    CHECK(len <= 4096);                 /* one chunk per hold */
    writes++;
    if (len > largest)
        largest = len;
    uint32_t left = room - file_len;
    if (left == 0)
        return full_err;
    uint32_t n = len < left ? len : left;
    memcpy(file + file_len, buf, n);
    file_len += n;
    return n;
}
static void st_out_close(void *c)
{
    (void)c;
    call("out_close", "", NULL);
    out_open_now = false;
}
static bool st_out_is(void *c, const char *p) { (void)c; return strcmp(p, "a.txt") == 0; }

static const struct shell_storage storage = {
    st_unlink, st_rename, st_copy_open, st_df, st_du_open, st_du_next, st_du_close,
    st_out_open, st_out_write, st_out_close, st_out_is, NULL,
};

/* ---- the rest of the full shell ---- */

static int32_t fake_start(void *c, int argc, char **argv, uint32_t envc,
                          const char *const *env_names, const char *const *env_values,
                          bool background)
{
    (void)c; (void)argc; (void)argv; (void)envc; (void)env_names; (void)env_values;
    (void)background;
    call("start", "", NULL);
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
    if (kind == SHELL_NOTE)
        notes++;
    lines_written++;
    int w = (int)strlen(text) - 1;      /* without its newline */
    if (w > longest)
        longest = w;
    strncat(out, text, sizeof(out) - strlen(out) - 1);
}

static const struct shell_output sink = { capture, NULL };

static const struct shell_thread one_thread[] = { { 0, "running", 5, 1480, 16384, "kernel" } };
static char log_text[9000];

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
    ctx.storage = &storage;
    removed = false;
    answer = out_answer = cat_answer = 0;
    room = sizeof(file);
    full_err = -FS_ENOSPC;
    file_len = 0;
    out_open_now = false;
    calls[0] = '\0';
    longest = 0;
}

static void clear_out(void)
{
    out[0] = '\0';
    errors = notes = lines_written = 0;
}

static enum shell_result run(const char *cmd)
{
    static char line[512];
    strncpy(line, cmd, sizeof(line) - 1);
    line[sizeof(line) - 1] = '\0';
    clear_out();
    calls[0] = '\0';
    return shell_execute(line, &ctx, &sink);
}

static enum shell_result reply(const char *text)
{
    clear_out();
    return shell_answer(text, &ctx, &sink);
}

static bool ends_with(const char *s, const char *tail)
{
    size_t n = strlen(s), k = strlen(tail);
    return n >= k && strcmp(s + n - k, tail) == 0;
}

static bool file_is(const char *text)
{
    return file_len == strlen(text) && memcmp(file, text, file_len) == 0;
}

static const char *const rows9 =
    "  rm       Remove a file: rm [-y] <path> (asks first)\n"
    "  cp       Copy a file: cp [-y] <from> <to>\n"
    "  mv       Move or rename: mv [-y] <from> <to>\n"
    "  df       Show the filesystem's nodes and pages in use\n"
    "  du       Show the pages under a folder: du [path]\n";

/* ---- without storage: nothing changes ---- */

static void test_without_storage_nothing_changes(void)
{
    static const char *const names[] = { "rm", "cp", "mv", "df", "du" };
    reset();
    ctx.storage = NULL;
    CHECK_EQ(run("help"), SHELL_OK);
    CHECK(ends_with(out, "  cat      Show a file's contents: cat <path>\n"
                         "Sections: shell system programs files (help <section>)\n"));
    CHECK_EQ(lines_written, 1 + 34 + 1);
    for (unsigned i = 0; i < 5; i++) {
        char line[16], want[96];
        snprintf(line, sizeof(line), "%s x", names[i]);
        CHECK_EQ(run(line), SHELL_UNKNOWN);
        snprintf(want, sizeof(want), "Unknown command: %s. Type 'help' for a list of commands.\n",
                 names[i]);
        CHECK(strncmp(out, want, strlen(want)) == 0);
    }
    CHECK_EQ(run("echo hi > f"), SHELL_OK);             /* '>' an ordinary word */
    CHECK(strcmp(out, "hi > f\n") == 0);
    CHECK(calls[0] == '\0');
    CHECK_EQ(run("echo a >>> b"), SHELL_OK);
    CHECK(strcmp(out, "a >>> b\n") == 0);
    CHECK_EQ(run("ru"), SHELL_UNKNOWN);
    CHECK(ends_with(out, "Did you mean: run, cd, cpu?\n"));
    CHECK_EQ(run("alias rm echo x"), SHELL_OK);         /* not a built-in name without storage */
}

/* ---- help and suggestions ---- */

static void test_help_and_suggestions(void)
{
    char want[2048];
    reset();
    CHECK_EQ(run("help"), SHELL_OK);
    snprintf(want, sizeof(want), "  cat      Show a file's contents: cat <path>\n%s"
             "Sections: shell system programs files (help <section>)\n", rows9);
    CHECK(ends_with(out, want));
    CHECK_EQ(lines_written, 1 + 39 + 1);
    CHECK_EQ(run("help files"), SHELL_OK);
    CHECK(ends_with(out, rows9));
    CHECK_EQ(lines_written, 1 + 13);
    CHECK_EQ(run("help rm"), SHELL_OK);
    CHECK(strcmp(out, "rm: Remove a file: rm [-y] <path> (asks first)\nUsage: rm [-y] <path>\n") == 0);
    CHECK_EQ(run("cp --help"), SHELL_OK);
    CHECK(strcmp(out, "cp: Copy a file: cp [-y] <from> <to>\nUsage: cp [-y] <from> <to>\n") == 0);
    CHECK_EQ(run("help mv"), SHELL_OK);
    CHECK(strcmp(out, "mv: Move or rename: mv [-y] <from> <to>\nUsage: mv [-y] <from> <to>\n") == 0);
    CHECK_EQ(run("df --help"), SHELL_OK);
    CHECK(strcmp(out, "df: Show the filesystem's nodes and pages in use\nUsage: df\n") == 0);
    CHECK_EQ(run("help du"), SHELL_OK);
    CHECK(strcmp(out, "du: Show the pages under a folder: du [path]\nUsage: du [path]\n") == 0);
    CHECK_EQ(run("ru"), SHELL_UNKNOWN);
    CHECK(ends_with(out, "Did you mean: du, rm, run?\n"));
    CHECK_EQ(notes, 1);                                 /* the suggestion is a note */
    CHECK_EQ(errors, 1);
    CHECK_EQ(run("x"), SHELL_UNKNOWN);
    CHECK(ends_with(out, "Did you mean: cd, cp, df?\n"));
    CHECK_EQ(run("kil 5"), SHELL_UNKNOWN);
    CHECK(ends_with(out, "Did you mean: kill?\n"));
    CHECK_EQ(run("alias rm echo x"), SHELL_BAD_ARGS);
    CHECK(strcmp(out, "alias: 'rm' is a built-in command.\n") == 0);
    CHECK_EQ(run("alias d1 du"), SHELL_OK);
}

/* ---- redirection ---- */

static void test_redirect_write_and_append(void)
{
    reset();
    CHECK_EQ(run("echo hello > a.txt"), SHELL_OK);
    CHECK(out[0] == '\0');                              /* nothing on the screen */
    CHECK(file_is("hello\n"));
    CHECK(strcmp(calls, "out_open(a.txt) out_close() ") == 0);
    CHECK(!out_open_now && !shell_redirect_active());
    CHECK_EQ(run("echo world >> b.txt"), SHELL_OK);
    CHECK(out_append && file_is("world\n"));
    CHECK(strcmp(calls, "out_append(b.txt) out_close() ") == 0);
    CHECK_EQ(run("echo a > f b"), SHELL_OK);            /* anywhere on the line */
    CHECK(file_is("a b\n"));
    CHECK_EQ(run("> f echo x"), SHELL_OK);
    CHECK(file_is("x\n"));
    CHECK_EQ(run("echo hi>b.txt"), SHELL_OK);           /* D11 (a): text */
    CHECK(strcmp(out, "hi>b.txt\n") == 0 && calls[0] == '\0');
    CHECK_EQ(run("echo 1 2 3 4 5 > f8"), SHELL_OK);
    CHECK(file_is("1 2 3 4 5\n"));
    CHECK_EQ(run("echo 1 2 3 4 5 6 > f9"), SHELL_TOO_MANY);
    CHECK(strcmp(out, "Too many arguments (max 8).\n") == 0 && calls[0] == '\0');
}

static void test_redirect_syntax(void)
{
    reset();
    CHECK_EQ(run("echo a >"), SHELL_BAD_ARGS);
    CHECK(strcmp(out, "Redirection: '>' needs a file name after it.\n") == 0);
    CHECK_EQ(run("echo a >>"), SHELL_BAD_ARGS);
    CHECK(strcmp(out, "Redirection: '>>' needs a file name after it.\n") == 0);
    CHECK_EQ(run("echo a > b > c"), SHELL_BAD_ARGS);
    CHECK(strcmp(out, "Redirection: only one '>' or '>>' per line.\n") == 0);
    CHECK_EQ(run("echo a >>> c"), SHELL_BAD_ARGS);
    CHECK(strcmp(out, "Redirection: '>>>' is not an operator (use > or >>).\n") == 0);
    CHECK_EQ(run("> c"), SHELL_BAD_ARGS);
    CHECK(strcmp(out, "Redirection: no command before '>'.\n") == 0);
    CHECK_EQ(run(">> c"), SHELL_BAD_ARGS);
    CHECK(strcmp(out, "Redirection: no command before '>>'.\n") == 0);
    CHECK_EQ(errors, 1);
    CHECK(calls[0] == '\0');                            /* nothing opened by any of them */
}

static void test_redirect_refused_and_unknown(void)
{
    static const char *const lines[] = {
        "clear > c", "sleep 1 > c", "run hello > c", "rm a.txt > c", "cp a.txt x > c",
        "mv a.txt x > c", "kill 3 > c", "kill -y 3 > c", "reboot > c", "presskey > c",
        "time sleep 1 > c",
    };
    static const char *const names[] = {
        "clear", "sleep", "run", "rm", "cp", "mv", "kill", "kill", "reboot", "presskey", "sleep",
    };
    reset();
    for (unsigned i = 0; i < sizeof(lines) / sizeof(lines[0]); i++) {
        char want[96];
        CHECK_EQ(run(lines[i]), SHELL_BAD_ARGS);
        snprintf(want, sizeof(want), "%s: its output cannot go to a file.\n", names[i]);
        CHECK(strcmp(out, want) == 0);
        CHECK(calls[0] == '\0');                        /* no file, no program, no question */
    }
    CHECK_EQ(run("nosuch > n.txt"), SHELL_UNKNOWN);
    CHECK(strncmp(out, "Unknown command: nosuch. ", 25) == 0 && calls[0] == '\0');
    CHECK_EQ(run("help nosuch > f"), SHELL_UNKNOWN);    /* the error and the note on the screen */
    CHECK(strcmp(out, "No help for 'nosuch': unknown command.\nDid you mean: touch?\n") == 0);
    CHECK(file_len == 0 && strcmp(calls, "out_open(f) out_close() ") == 0);
}

static void test_redirect_errors_stay_on_screen(void)
{
    reset();
    CHECK_EQ(run("ls /nosuch > e.txt"), SHELL_BAD_ARGS);
    CHECK(strcmp(out, "ls: '/nosuch': no such file or folder.\n") == 0);
    CHECK(file_len == 0 && strcmp(calls, "out_open(e.txt) out_close() ") == 0);
    out_answer = -FS_EISDIR;
    CHECK_EQ(run("echo x > docs"), SHELL_BAD_ARGS);
    CHECK(strcmp(out, "Output to 'docs': is a folder.\n") == 0);
    out_answer = -FS_ENOENT;
    CHECK_EQ(run("echo x > /nosuch/f"), SHELL_BAD_ARGS);
    CHECK(strcmp(out, "Output to '/nosuch/f': no such file or folder.\n") == 0);
    removed = true;                                     /* stage 8's hint for a relative path */
    CHECK_EQ(run("echo x > f"), SHELL_BAD_ARGS);
    CHECK(ends_with(out, "(The current folder was removed: use an absolute path, for example cd /.)\n"));
    out_answer = -FS_ENOSPC;
    removed = false;
    CHECK_EQ(run("echo x > new"), SHELL_BAD_ARGS);
    CHECK(strcmp(out, "Output to 'new': no room (1024 at most).\n") == 0);
}

static void test_redirect_chunks(void)
{
    reset();
    /* dmesg: about 8,800 bytes of log, so the file takes 3 writes of at most 4096. */
    size_t at = 0;
    for (unsigned i = 0; i < 160; i++)
        at += (size_t)snprintf(log_text + at, sizeof(log_text) - at,
                               "log line %03u: the kernel said something long enough\n", i);
    extras.log = log_text;
    extras.log_len = (uint32_t)strlen(log_text);
    CHECK_EQ(run("dmesg 9999"), SHELL_OK);
    static char screen[16384];
    strcpy(screen, out);
    CHECK_EQ(run("dmesg 9999 > /tmp/log.txt"), SHELL_OK);
    CHECK(out[0] == '\0');
    CHECK(file_len == strlen(screen) && memcmp(file, screen, file_len) == 0);
    CHECK(file_len > 8192 && writes == (file_len + 4095) / 4096 && largest == 4096);
    CHECK_EQ(run("help > h.txt"), SHELL_OK);
    CHECK(file_len > 1000 && memcmp(file, "Available commands:\n", 20) == 0);
    CHECK_EQ(run("time echo x > t"), SHELL_OK);         /* the command's output; time's line later */
    CHECK(file_is("x\n") && ss.timed);
}

static void test_redirect_limits(void)
{
    reset();
    room = 10;
    CHECK_EQ(run("echo 0123456789abc > f"), SHELL_OK);
    CHECK(file_is("0123456789"));
    CHECK(strcmp(out, "Output to 'f': no file space left (the budget is full).\n"
                      "Output to 'f': only the first 10 bytes were written.\n") == 0);
    CHECK_EQ(errors, 2);
    full_err = -FS_EFBIG;
    room = 0;
    CHECK_EQ(run("echo x >> edge.dat"), SHELL_OK);
    CHECK(strcmp(out, "Output to 'edge.dat': the file is at its size limit (2 MiB).\n"
                      "Output to 'edge.dat': only the first 0 bytes were written.\n") == 0);
}

static void test_cat_into_a_file(void)
{
    static const uint8_t raw[5] = { 'a', 0, 1, '\n', 0xFF };
    reset();
    CHECK_EQ(run("cat a.txt >> a.txt"), SHELL_BAD_ARGS);
    CHECK(strcmp(out, "cat: 'a.txt': is also the output file.\n") == 0);
    CHECK(!shell_redirect_active() && strcmp(calls, "out_append(a.txt) out_close() ") == 0);
    CHECK_EQ(run("cat b.txt > c.txt"), SHELL_CAT);      /* the console writes it */
    CHECK(shell_redirect_active() && out_open_now);
    CHECK(shell_redirect_raw(raw, 5));
    CHECK(file_len == 5 && memcmp(file, raw, 5) == 0);  /* the bytes as they are */
    shell_redirect_end();
    CHECK(!shell_redirect_active() && !out_open_now);
    room = 7;
    CHECK_EQ(run("cat b.txt >> c.txt"), SHELL_CAT);
    CHECK(shell_redirect_raw(raw, 5));
    CHECK(!shell_redirect_raw(raw, 5));                 /* stopped at the limit: 2 of 5 */
    clear_out();
    shell_redirect_end();
    CHECK(strcmp(out, "Output to 'c.txt': no file space left (the budget is full).\n"
                      "Output to 'c.txt': only the first 7 bytes were written.\n") == 0);
    cat_answer = -FS_EISDIR;
    CHECK_EQ(run("cat docs > c.txt"), SHELL_BAD_ARGS);
    CHECK(!shell_redirect_active() && !out_open_now);
}

/* ---- rm ---- */

static void test_rm(void)
{
    reset();
    CHECK_EQ(run("rm a.txt"), SHELL_ASK);
    CHECK(strcmp(out, "Remove file 'a.txt'? Type 'yes' to confirm:\n") == 0 && calls[0] == '\0');
    CHECK_EQ(reply("no"), SHELL_OK);
    CHECK(strcmp(out, "Remove cancelled.\n") == 0 && calls[0] == '\0');
    CHECK_EQ(run("rm a.txt"), SHELL_ASK);
    CHECK_EQ(reply("yes"), SHELL_OK);
    CHECK(out[0] == '\0' && strcmp(calls, "unlink(a.txt) ") == 0);
    CHECK_EQ(run("rm -y b.txt"), SHELL_OK);
    CHECK(out[0] == '\0' && strcmp(calls, "unlink(b.txt) ") == 0);
    CHECK_EQ(run("rm docs"), SHELL_BAD_ARGS);
    CHECK(strcmp(out, "rm: 'docs': is a folder (rmdir removes folders).\n") == 0);
    CHECK_EQ(run("rm /"), SHELL_BAD_ARGS);
    CHECK(strcmp(out, "rm: '/': is a folder (rmdir removes folders).\n") == 0);
    CHECK_EQ(run("rm nosuch"), SHELL_BAD_ARGS);
    CHECK(strcmp(out, "rm: 'nosuch': no such file or folder.\n") == 0);
    CHECK_EQ(run("rm"), SHELL_BAD_ARGS);
    CHECK(strcmp(out, "Usage: rm [-y] <path>\n") == 0);
    CHECK_EQ(run("rm -x"), SHELL_BAD_ARGS);
    CHECK(strcmp(out, "Usage: rm [-y] <path>\n") == 0);
    CHECK_EQ(run("rm -y"), SHELL_BAD_ARGS);
    CHECK_EQ(run("rm a b"), SHELL_BAD_ARGS);
    CHECK(strcmp(out, "Usage: rm [-y] <path>\n") == 0);
    CHECK_EQ(run("rm a.txt"), SHELL_ASK);               /* removed meanwhile */
    answer = -FS_ENOENT;
    CHECK_EQ(reply(" yes "), SHELL_BAD_ARGS);
    CHECK(strcmp(out, "rm: 'a.txt': no such file or folder.\n") == 0);
    answer = 0;
    CHECK_EQ(run("rm a.txt"), SHELL_ASK);
    CHECK_EQ(reply("yess"), SHELL_OK);                  /* only exactly "yes" */
    CHECK(strcmp(out, "Remove cancelled.\n") == 0);
}

/* ---- cp ---- */

static void test_cp(void)
{
    reset();
    CHECK_EQ(run("cp a.txt new.txt"), SHELL_COPY);
    CHECK(out[0] == '\0' && strcmp(calls, "copy_open(a.txt,new.txt) ") == 0);
    CHECK_EQ(run("cp a.txt b.txt"), SHELL_ASK);
    CHECK(strcmp(out, "Replace file 'b.txt'? Type 'yes' to confirm:\n") == 0 && calls[0] == '\0');
    CHECK_EQ(reply("no"), SHELL_OK);
    CHECK(strcmp(out, "Copy cancelled.\n") == 0);
    CHECK_EQ(run("cp a.txt b.txt"), SHELL_ASK);
    calls[0] = '\0';
    CHECK_EQ(reply("yes"), SHELL_COPY);
    CHECK(strcmp(calls, "copy_open(a.txt,b.txt) ") == 0);
    CHECK_EQ(run("cp -y a.txt b.txt"), SHELL_COPY);
    CHECK(out[0] == '\0');
    CHECK_EQ(run("cp docs x"), SHELL_BAD_ARGS);
    CHECK(strcmp(out, "cp: 'docs': is a folder.\n") == 0);
    CHECK_EQ(run("cp a.txt docs"), SHELL_BAD_ARGS);
    CHECK(strcmp(out, "cp: 'docs': is a folder.\n") == 0);
    CHECK_EQ(run("cp nosuch x"), SHELL_BAD_ARGS);
    CHECK(strcmp(out, "cp: 'nosuch': no such file or folder.\n") == 0);
    CHECK_EQ(run("cp a.txt /home/a.txt"), SHELL_BAD_ARGS);
    CHECK(strcmp(out, "cp: 'a.txt' and '/home/a.txt' are the same file.\n") == 0);
    CHECK_EQ(run("cp a.txt"), SHELL_BAD_ARGS);
    CHECK(strcmp(out, "Usage: cp [-y] <from> <to>\n") == 0);
    CHECK_EQ(run("cp -x a b"), SHELL_BAD_ARGS);
    CHECK_EQ(run("cp a -b"), SHELL_BAD_ARGS);
    CHECK(strcmp(out, "Usage: cp [-y] <from> <to>\n") == 0);
    answer = -FS_ENOSPC;                                /* no node left for the target */
    CHECK_EQ(run("cp a.txt new.txt"), SHELL_BAD_ARGS);
    CHECK(strcmp(out, "cp: 'new.txt': no room (1024 at most).\n") == 0);
    answer = -FS_ENOENT;
    CHECK_EQ(run("cp a.txt /nosuch/x"), SHELL_BAD_ARGS);
    CHECK(strcmp(out, "cp: '/nosuch/x': no such file or folder.\n") == 0);
}

static void test_copy_end(void)
{
    reset();
    clear_out();
    shell_copy_end("big2.dat", 2097152, 2097152, 0, false, &sink);
    CHECK(out[0] == '\0');                              /* whole: nothing said */
    shell_copy_end("big3.dat", 28672, 2097152, 0, true, &sink);
    CHECK(strcmp(out, "cp: cancelled.\ncp: 'big3.dat' holds the first 28672 of 2097152 bytes.\n") == 0);
    CHECK_EQ(errors, 0);
    clear_out();
    shell_copy_end("full.dat", 8192, 2097152, -FS_ENOSPC, false, &sink);
    CHECK(strcmp(out, "cp: 'full.dat': no file space left (the budget is full).\n"
                      "cp: 'full.dat' holds the first 8192 of 2097152 bytes.\n") == 0);
    CHECK_EQ(errors, 2);
}

/* ---- mv ---- */

static void test_mv(void)
{
    reset();
    CHECK_EQ(run("mv a.txt new.txt"), SHELL_OK);
    CHECK(out[0] == '\0' && strcmp(calls, "rename(a.txt,new.txt) ") == 0);
    CHECK_EQ(run("mv a.txt b.txt"), SHELL_ASK);         /* a file over a file */
    CHECK(strcmp(out, "Replace file 'b.txt'? Type 'yes' to confirm:\n") == 0);
    CHECK_EQ(reply("no"), SHELL_OK);
    CHECK(strcmp(out, "Move cancelled.\n") == 0);
    CHECK_EQ(run("mv a.txt b.txt"), SHELL_ASK);
    calls[0] = '\0';
    CHECK_EQ(reply("yes"), SHELL_OK);
    CHECK(strcmp(calls, "rename(a.txt,b.txt) ") == 0);
    CHECK_EQ(run("mv -y a.txt b.txt"), SHELL_OK);
    CHECK(strcmp(calls, "rename(a.txt,b.txt) ") == 0);
    CHECK_EQ(run("mv e1 e2"), SHELL_OK);                /* a folder over a folder: no question */
    CHECK_EQ(run("mv a.txt /home/a.txt"), SHELL_OK);    /* the same node: no question */
    answer = -FS_EINVAL;
    CHECK_EQ(run("mv docs docs/sub"), SHELL_BAD_ARGS);
    CHECK(strcmp(out, "mv: 'docs' -> 'docs/sub': a folder cannot move into itself.\n") == 0);
    answer = -FS_EBUSY;
    CHECK_EQ(run("mv / x"), SHELL_BAD_ARGS);
    CHECK(strcmp(out, "mv: '/' -> 'x': '/', '.' and '..' cannot move.\n") == 0);
    answer = -FS_EISDIR;
    CHECK_EQ(run("mv a.txt docs"), SHELL_BAD_ARGS);
    CHECK(strcmp(out, "mv: 'a.txt' -> 'docs': is a folder.\n") == 0);
    answer = -FS_ENOENT;
    CHECK_EQ(run("mv nosuch x"), SHELL_BAD_ARGS);
    CHECK(strcmp(out, "mv: 'nosuch' -> 'x': no such file or folder.\n") == 0);
    answer = -FS_ENOTEMPTY;
    CHECK_EQ(run("mv e1 e2"), SHELL_BAD_ARGS);
    CHECK(strcmp(out, "mv: 'e1' -> 'e2': the folder is not empty.\n") == 0);
    CHECK_EQ(run("mv a"), SHELL_BAD_ARGS);
    CHECK(strcmp(out, "Usage: mv [-y] <from> <to>\n") == 0);
}

/* ---- df and du ---- */

static void test_df_du(void)
{
    reset();
    CHECK_EQ(run("df"), SHELL_OK);
    CHECK(strcmp(out, "Filesystem: live (RAM), mounted on / (files in memory only)\n"
                      "Nodes:      7 used of 1024 (1017 free)\n"
                      "Pages:      522 used of 5432 (4910 free), 4 KiB each\n"
                      "Space:      2088 KiB used of 21728 KiB (19640 KiB free)\n") == 0);
    CHECK_EQ(run("df x"), SHELL_BAD_ARGS);
    CHECK(strcmp(out, "Usage: df\n") == 0);
    CHECK_EQ(run("du a.txt"), SHELL_OK);
    CHECK(strcmp(out, "1 page (4 KiB) in 1 file and 0 folders\n") == 0);
    CHECK_EQ(run("du /home"), SHELL_OK);
    CHECK(strcmp(out, "516 pages (2064 KiB) in 6 files and 3 folders\n") == 0);
    CHECK(strcmp(calls, "du_open(/home) du_close() ") == 0);
    CHECK_EQ(run("du"), SHELL_OK);
    CHECK(strcmp(calls, "du_open(.) du_close() ") == 0);
    answer = -FS_ENOENT;
    CHECK_EQ(run("du /nosuch"), SHELL_BAD_ARGS);
    CHECK(strcmp(out, "du: '/nosuch': no such file or folder.\n") == 0);
    CHECK(strcmp(calls, "du_open(/nosuch) ") == 0);     /* nothing held, nothing to give back */
    CHECK_EQ(run("du a b"), SHELL_BAD_ARGS);
    CHECK(strcmp(out, "Usage: du [path]\n") == 0);
    CHECK_EQ(run("du -s"), SHELL_BAD_ARGS);
    CHECK_EQ(run("df > d.txt"), SHELL_OK);              /* df's own lines, in the file */
    CHECK(file_len > 100 && memcmp(file, "Filesystem: live (RAM)", 22) == 0);
}

/* A folder removed while du walks it: the error, and the walk given back. */
static void test_du_removed(void)
{
    reset();
    du_fail = true;
    CHECK_EQ(run("du /home"), SHELL_BAD_ARGS);
    CHECK(strcmp(out, "du: '/home': no such file or folder.\n") == 0);
    CHECK(strcmp(calls, "du_open(/home) du_close() ") == 0);
    du_fail = false;
}

/* ---- every line fits ---- */

static void test_line_widths(void)
{
    char p[80];
    memset(p, 'p', 70);
    p[70] = '\0';
    char cmd[256];
    reset();
    longest = 0;
    snprintf(cmd, sizeof(cmd), "rm %s", p);
    run(cmd);
    reply("no");
    snprintf(cmd, sizeof(cmd), "cp %s b.txt", p);
    run(cmd);
    snprintf(cmd, sizeof(cmd), "mv b.txt %s", p);
    answer = -FS_EINVAL;
    run(cmd);
    snprintf(cmd, sizeof(cmd), "mv %s %s", p, p);
    run(cmd);
    answer = 0;
    room = 0;
    snprintf(cmd, sizeof(cmd), "echo x > %s", p);
    run(cmd);
    out_answer = -FS_EISDIR;
    run(cmd);
    snprintf(cmd, sizeof(cmd), "cat a.txt >> %s", p);
    out_answer = 0;
    run(cmd);
    shell_redirect_end();
    clear_out();
    shell_copy_end(p, 2097152, 2097152, -FS_ENOSPC, false, &sink);
    run("echo a >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>> b");
    CHECK(longest > 60 && longest <= 79);
}

int main(void)
{
    printf("shell_fs10b unit tests\n");
    RUN(test_without_storage_nothing_changes);
    RUN(test_help_and_suggestions);
    RUN(test_redirect_write_and_append);
    RUN(test_redirect_syntax);
    RUN(test_redirect_refused_and_unknown);
    RUN(test_redirect_errors_stay_on_screen);
    RUN(test_redirect_chunks);
    RUN(test_redirect_limits);
    RUN(test_cat_into_a_file);
    RUN(test_rm);
    RUN(test_cp);
    RUN(test_copy_end);
    RUN(test_mv);
    RUN(test_df_du);
    RUN(test_du_removed);
    RUN(test_line_widths);
    return unit_report("shell_fs10b");
}
