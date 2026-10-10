/* Unit tests for the 0.11 program modules (kernel/proc/progmod_core.c):
 * names from module command lines, the table (duplicates, at most 8), and
 * the safe form of a command line for log lines. */
#include <stdint.h>
#include <string.h>
#include "kernel/proc/progmod_core.h"
#include "tests/unit/unit.h"

static char name[PROGMOD_NAME_MAX];

static int named(const char *cmdline, const char *want)
{
    return progmod_name(cmdline, name) == PROGMOD_OK && strcmp(name, want) == 0;
}

static int refused(const char *cmdline)
{
    return progmod_name(cmdline, name) == PROGMOD_ERR_NAME && name[0] == '\0';
}

static void test_names_from_paths(void)
{
    CHECK(named("/boot/prog/hello.elf", "hello"));          /* GRUB: the path */
    CHECK(named("build/user/hello.elf", "hello"));          /* QEMU -initrd */
    CHECK(named("hello.elf", "hello"));
    CHECK(named("hello", "hello"));                          /* no ".elf" to strip */
    CHECK(named("/a/b/c/get_pid2.elf", "get_pid2"));
    CHECK(named("  /boot/x.elf  ", "x"));                    /* spaces around */
    CHECK(named("/boot/abcdefghijklmno.elf", "abcdefghijklmno"));   /* 15: the most */
    CHECK(refused("/boot/abcdefghijklmnop.elf"));            /* 16 */
    CHECK(refused("/boot/.elf"));                            /* nothing left */
    CHECK(refused("/boot/"));
    CHECK(refused("/boot/Hello.elf"));                       /* capitals */
    CHECK(refused("/boot/he-llo.elf"));
    CHECK(refused("/boot/hello.ELF"));                       /* ".ELF" is not stripped */
    CHECK(named("/boot/a.elf.elf", "a.elf") == 0);           /* '.' is not a name character */
    CHECK(refused("/boot/a.elf.elf"));
}

static void test_names_from_second_word(void)
{
    CHECK(named("/boot/prog/hello.elf hello", "hello"));
    CHECK(named("build/user/spin.elf myspin", "myspin"));    /* the second word wins */
    CHECK(named("x.bin \tprog_1", "prog_1"));
    CHECK(refused("/boot/hello.elf Hello"));
    CHECK(refused("/boot/hello.elf a-b"));
    CHECK(refused("/boot/hello.elf abcdefghijklmnop"));
    CHECK(refused("/boot/hello.elf one two"));               /* three words */
    CHECK(refused("/boot/hello.elf one two three"));
}

static void test_no_name(void)
{
    CHECK(refused(NULL));
    CHECK(refused(""));
    CHECK(refused("   "));
    CHECK(refused("\t"));
    /* A command line with no end within 4096 characters is refused. */
    static char longline[5000];
    memset(longline, 'a', sizeof(longline));
    CHECK(refused(longline));
    longline[4000] = '\0';
    CHECK(refused(longline));                                /* one word, too long a name */
}

static struct elf_plan plan(uint64_t entry)
{
    struct elf_plan p;
    memset(&p, 0, sizeof(p));
    p.entry = entry;
    p.count = 1;
    p.pages = 1;
    return p;
}

static void test_table(void)
{
    struct progmod_table t;
    progmod_init(&t);
    CHECK_EQ(t.count, 0);
    CHECK(progmod_find(&t, "hello") == NULL);
    struct elf_plan p = plan(0x400000);
    CHECK_EQ(progmod_add(&t, "hello", 1, 0x200000, 5176, &p), PROGMOD_OK);
    const struct progmod *m = progmod_find(&t, "hello");
    CHECK(m != NULL);
    CHECK(m && strcmp(m->name, "hello") == 0 && m->module == 1 && m->phys == 0x200000 &&
          m->size == 5176 && m->plan.entry == 0x400000);
    CHECK_EQ(progmod_add(&t, "hello", 2, 0x300000, 1, &p), PROGMOD_ERR_DUPLICATE);
    CHECK_EQ(t.count, 1);
    CHECK(progmod_find(&t, "hell") == NULL);                  /* whole names only */
    CHECK(progmod_find(&t, "hello2") == NULL);
    CHECK(progmod_find(&t, NULL) == NULL);
    CHECK_EQ(progmod_add(&t, "Bad", 3, 0, 1, &p), PROGMOD_ERR_NAME);
    CHECK_EQ(progmod_add(&t, NULL, 3, 0, 1, &p), PROGMOD_ERR_NAME);
    CHECK_EQ(progmod_add(&t, "", 3, 0, 1, &p), PROGMOD_ERR_NAME);
    /* Fill to 8, then the 9th is refused; a duplicate is still a duplicate. */
    static const char *names[] = { "a", "b", "c", "d", "e", "f", "g" };
    for (uint32_t i = 0; i < 7; i++)
        CHECK_EQ(progmod_add(&t, names[i], 2 + i, 0, 1, &p), PROGMOD_OK);
    CHECK_EQ(t.count, PROGMOD_MAX);
    CHECK_EQ(progmod_add(&t, "ninth", 9, 0, 1, &p), PROGMOD_ERR_FULL);
    CHECK_EQ(progmod_add(&t, "a", 9, 0, 1, &p), PROGMOD_ERR_DUPLICATE);
    CHECK_EQ(t.count, PROGMOD_MAX);
    CHECK(progmod_find(&t, "g") != NULL && progmod_find(&t, "g")->module == 8);
    progmod_init(&t);
    CHECK_EQ(t.count, 0);
}

static void test_show(void)
{
    char out[PROGMOD_SHOW_BUF];
    progmod_show("/boot/x.elf Bad-Name", out);
    CHECK(strcmp(out, "/boot/x.elf Bad-Name") == 0);
    progmod_show("a\tb\x1b[2Jc\x7f", out);                   /* control bytes and DEL */
    CHECK(strcmp(out, "a?b?[2Jc?") == 0);
    progmod_show(NULL, out);
    CHECK(strcmp(out, "") == 0);
    static char longline[100];
    memset(longline, 'x', 99);
    longline[99] = '\0';
    progmod_show(longline, out);
    CHECK_EQ(strlen(out), PROGMOD_SHOW_MAX + 3);             /* 40 characters and "..." */
    CHECK(strcmp(out + PROGMOD_SHOW_MAX, "...") == 0);
    longline[PROGMOD_SHOW_MAX] = '\0';                       /* exactly 40: no "..." */
    progmod_show(longline, out);
    CHECK_EQ(strlen(out), PROGMOD_SHOW_MAX);
    char high[3] = { (char)0xC3, (char)0xA9, 0 };            /* UTF-8 bytes */
    progmod_show(high, out);
    CHECK(strcmp(out, "??") == 0);
}

static void test_result_strings(void)
{
    CHECK(strcmp(progmod_result_str(PROGMOD_OK), "OK") == 0);
    CHECK(strcmp(progmod_result_str(PROGMOD_ERR_NAME), "bad name") == 0);
    CHECK(strcmp(progmod_result_str(PROGMOD_ERR_DUPLICATE), "duplicate name") == 0);
    CHECK(strcmp(progmod_result_str(PROGMOD_ERR_FULL), "more than 8 program modules") == 0);
    CHECK(strcmp(progmod_result_str((enum progmod_result)99), "unknown result") == 0);
}

int main(void)
{
    RUN(test_names_from_paths);
    RUN(test_names_from_second_word);
    RUN(test_no_name);
    RUN(test_table);
    RUN(test_show);
    RUN(test_result_strings);
    return unit_report("progmod");
}
