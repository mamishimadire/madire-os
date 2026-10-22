/* Unit tests for 1.1 stage 8 of kernel/shell/shell.c: Tab completion of
 * command names (shell_complete, shell_complete_names). One match types
 * the rest of the name and a space; several type what they share; with
 * nothing to add, the list is the matches in byte order, two spaces apart,
 * a line filling while the next name fits in 79 columns. Nothing is typed
 * without a match or without room. The names are the commands the context
 * offers (no file commands without files); aliases are not completed. */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "kernel/shell/shell.h"
#include "tests/unit/unit.h"

static char out[4096];
static int lines, errors;

static void capture(void *c, const char *text, enum shell_text kind)
{
    (void)c;
    if (kind != SHELL_TEXT)
        errors++;
    lines++;
    strncat(out, text, sizeof(out) - strlen(out) - 1);
}

static const struct shell_output sink = { capture, NULL };

static void clear_out(void)
{
    out[0] = '\0';
    lines = errors = 0;
}

/* The context of the console: everything offered; files optional. Only
 * which parts are set matters to completion. */
static struct shell_thread threads[1];
static struct shell_extras extras;
static struct shell_procs procs;
static struct shell_state st;
static struct shell_session ss;
static struct shell_files files;
static struct shell_storage storage;
static struct shell_context ctx;

static void setup(bool with_files)
{
    memset(&ctx, 0, sizeof(ctx));
    shell_state_init(&st);
    shell_session_init(&ss);
    ctx.version = "1.1";
    ctx.threads = threads;
    ctx.thread_count = 1;
    ctx.extras = &extras;
    ctx.procs = &procs;
    ctx.state = &st;
    ctx.session = &ss;
    ctx.files = with_files ? &files : NULL;
    ctx.storage = with_files ? &storage : NULL;
}

static void test_unique(void)
{
    struct shell_completion c;
    setup(true);
    clear_out();
    shell_complete(&ctx, "his", 68, &c, &sink);
    CHECK_EQ(c.count, 1);
    CHECK(!c.no_room);
    CHECK(strcmp(c.add, "tory ") == 0);
    CHECK_EQ(lines, 0);                     /* one match: never listed */

    shell_complete(&ctx, "history", 64, &c, NULL);  /* already whole: the space */
    CHECK_EQ(c.count, 1);
    CHECK(strcmp(c.add, " ") == 0);

    shell_complete(&ctx, "ec", 69, &c, NULL);
    CHECK_EQ(c.count, 1);
    CHECK(strcmp(c.add, "ho ") == 0);
}

static void test_common_prefix(void)
{
    static const char *const names[] = { "zeta", "abcxy", "abcde", "abcdz" };
    struct shell_completion c;
    clear_out();
    shell_complete_names(names, 4, "a", 70, &c, &sink);
    CHECK_EQ(c.count, 3);
    CHECK(!c.no_room);
    CHECK(strcmp(c.add, "bc") == 0);
    CHECK_EQ(lines, 1);                     /* listed when asked */
    CHECK(strcmp(out, "abcde  abcdz  abcxy\n") == 0);

    shell_complete_names(names, 4, "abcd", 70, &c, NULL);
    CHECK_EQ(c.count, 2);
    CHECK(strcmp(c.add, "") == 0);          /* abcde, abcdz: nothing to add */
}

static void test_ambiguous(void)
{
    struct shell_completion c;
    setup(true);
    clear_out();
    shell_complete(&ctx, "un", 69, &c, &sink);
    CHECK_EQ(c.count, 3);
    CHECK(!c.no_room);
    CHECK(strcmp(c.add, "") == 0);
    CHECK(strcmp(out, "unalias  uname  unset\n") == 0);
    CHECK_EQ(lines, 1);
    CHECK_EQ(errors, 0);

    clear_out();
    shell_complete(&ctx, "rm", 69, &c, &sink);
    CHECK_EQ(c.count, 2);
    CHECK(strcmp(c.add, "") == 0);
    CHECK(strcmp(out, "rm  rmdir\n") == 0);

    clear_out();
    shell_complete(&ctx, "un", 69, &c, NULL);   /* no list sink: nothing written */
    CHECK_EQ(c.count, 3);
    CHECK_EQ(lines, 0);
}

static void test_aliases_not_completed(void)
{
    struct shell_completion c;
    setup(true);
    strcpy(st.aliases[0].name, "unx");
    strcpy(st.aliases[0].value, "uname -a");
    st.alias_count = 1;
    clear_out();
    shell_complete(&ctx, "un", 69, &c, &sink);
    CHECK_EQ(c.count, 3);
    CHECK(strcmp(out, "unalias  uname  unset\n") == 0);
    shell_complete(&ctx, "unx", 68, &c, NULL);
    CHECK_EQ(c.count, 0);
}

static void test_no_match(void)
{
    struct shell_completion c;
    setup(true);
    clear_out();
    shell_complete(&ctx, "zz", 69, &c, &sink);
    CHECK_EQ(c.count, 0);
    CHECK(!c.no_room);
    CHECK(strcmp(c.add, "") == 0);
    CHECK_EQ(lines, 0);
    shell_complete(&ctx, "echoo", 66, &c, NULL);    /* longer than the name */
    CHECK_EQ(c.count, 0);
}

/* The line holds 71 characters at most (LINE_MAX): with no room left, or
 * too little, nothing is typed. */
static void test_line_full(void)
{
    struct shell_completion c;
    setup(true);
    shell_complete(&ctx, "his", 0, &c, NULL);
    CHECK_EQ(c.count, 1);
    CHECK(c.no_room);
    CHECK(strcmp(c.add, "") == 0);
    shell_complete(&ctx, "his", 4, &c, NULL);       /* "tory " needs 5 */
    CHECK(c.no_room);
    CHECK(strcmp(c.add, "") == 0);
    shell_complete(&ctx, "his", 5, &c, NULL);       /* exactly fits */
    CHECK(!c.no_room);
    CHECK(strcmp(c.add, "tory ") == 0);

    static const char *const names[] = { "abcde", "abcdz" };
    shell_complete_names(names, 2, "a", 2, &c, NULL);   /* "bcd" needs 3 */
    CHECK_EQ(c.count, 2);
    CHECK(c.no_room);
    CHECK(strcmp(c.add, "") == 0);
}

/* Byte order (capitals first), two spaces apart, no indent; a line fills
 * while the next name fits in 79 columns: 9 + 5 x (2 + 12) = 79 exactly. */
static void test_listing(void)
{
    static const char *const names[] = {
        "b23456789011", "b23456789003", "b23456789007", "a23456789", "b23456789001",
        "b23456789005", "b23456789009", "b23456789002", "b23456789010", "b23456789004",
        "b23456789006", "b23456789008", "Zeta",
    };
    struct shell_completion c;
    clear_out();
    shell_complete_names(names, 13, "", 71, &c, &sink);
    CHECK_EQ(c.count, 13);
    CHECK(strcmp(c.add, "") == 0);
    CHECK_EQ(lines, 3);
    CHECK_EQ(errors, 0);
    CHECK(strcmp(out,
                 "Zeta  a23456789  b23456789001  b23456789002  b23456789003  b23456789004\n"
                 "b23456789005  b23456789006  b23456789007  b23456789008  b23456789009\n"
                 "b23456789010  b23456789011\n") == 0);

    /* Exactly 79 columns: the sixth name still fits. */
    clear_out();
    shell_complete_names(names, 12, "", 71, &c, &sink);
    CHECK_EQ(c.count, 12);
    CHECK_EQ(lines, 3);
    CHECK(strcmp(out,
                 "a23456789  b23456789001  b23456789002  b23456789003  b23456789004  b23456789005\n"
                 "b23456789006  b23456789007  b23456789008  b23456789009  b23456789010\n"
                 "b23456789011\n") == 0);
    const char *nl = strchr(out, '\n');
    CHECK_EQ(nl - out, 79);
}

static void test_without_files(void)
{
    struct shell_completion c;
    setup(false);
    clear_out();
    shell_complete(&ctx, "l", 70, &c, &sink);       /* no "ls" without a filesystem */
    CHECK_EQ(c.count, 0);
    shell_complete(&ctx, "c", 70, &c, &sink);       /* clear, cpu (no cd, cat, cp) */
    CHECK_EQ(c.count, 2);
    CHECK(strcmp(out, "clear  cpu\n") == 0);
    shell_complete(&ctx, "rm", 69, &c, NULL);
    CHECK_EQ(c.count, 0);

    setup(true);
    clear_out();
    shell_complete(&ctx, "l", 70, &c, NULL);
    CHECK_EQ(c.count, 1);
    CHECK(strcmp(c.add, "s ") == 0);
    shell_complete(&ctx, "c", 70, &c, &sink);
    CHECK_EQ(c.count, 5);
    CHECK(strcmp(out, "cat  cd  clear  cp  cpu\n") == 0);
}

int main(void)
{
    printf("test_complete11\n");
    RUN(test_unique);
    RUN(test_common_prefix);
    RUN(test_ambiguous);
    RUN(test_aliases_not_completed);
    RUN(test_no_match);
    RUN(test_line_full);
    RUN(test_listing);
    RUN(test_without_files);
    return unit_report("test_complete11");
}
