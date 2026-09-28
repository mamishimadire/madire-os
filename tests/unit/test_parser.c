/* Unit tests for kernel/shell/parser.c (command-line splitter). */
#include <string.h>
#include "kernel/shell/parser.h"
#include "tests/unit/unit.h"

static char line[128];
static char *argv[SHELL_MAX_ARGS];
static int argc;

static enum parse_result parse(const char *text)
{
    strcpy(line, text);
    for (int i = 0; i < SHELL_MAX_ARGS; i++)
        argv[i] = NULL;
    return shell_parse(line, &argc, argv);
}

static void test_single_word(void)
{
    CHECK_EQ(parse("help"), PARSE_OK);
    CHECK_EQ(argc, 1);
    CHECK(strcmp(argv[0], "help") == 0);
}

static void test_multiple_spaces(void)
{
    CHECK_EQ(parse("echo   Hello    World"), PARSE_OK);
    CHECK_EQ(argc, 3);
    CHECK(strcmp(argv[0], "echo") == 0);
    CHECK(strcmp(argv[1], "Hello") == 0);
    CHECK(strcmp(argv[2], "World") == 0);
}

static void test_leading_and_trailing_whitespace(void)
{
    CHECK_EQ(parse("   version   "), PARSE_OK);
    CHECK_EQ(argc, 1);
    CHECK(strcmp(argv[0], "version") == 0);
}

static void test_tabs(void)
{
    CHECK_EQ(parse("\techo\ta\t\t b \t"), PARSE_OK);
    CHECK_EQ(argc, 3);
    CHECK(strcmp(argv[1], "a") == 0);
    CHECK(strcmp(argv[2], "b") == 0);
}

static void test_empty_and_blank(void)
{
    CHECK_EQ(parse(""), PARSE_EMPTY);
    CHECK_EQ(argc, 0);
    CHECK_EQ(parse("     "), PARSE_EMPTY);
    CHECK_EQ(parse("\t \t"), PARSE_EMPTY);
    CHECK_EQ(argc, 0);
}

static void test_maximum_words(void)
{
    CHECK_EQ(parse("echo 1 2 3 4 5 6 7"), PARSE_OK);      /* exactly 8 */
    CHECK_EQ(argc, 8);
    CHECK(strcmp(argv[7], "7") == 0);
    CHECK_EQ(parse("echo 1 2 3 4 5 6 7 8"), PARSE_TOO_MANY);
    CHECK_EQ(argc, 8);
    CHECK_EQ(parse("echo 1 2 3 4 5 6 7   "), PARSE_OK);   /* trailing space is not a word */
}

static void test_long_word(void)
{
    char word[72];
    memset(word, 'x', 71);
    word[71] = '\0';
    CHECK_EQ(parse(word), PARSE_OK);
    CHECK_EQ(argc, 1);
    CHECK_EQ(strlen(argv[0]), 71);
}

static void test_case_preserved(void)
{
    CHECK_EQ(parse("HELP Me"), PARSE_OK);
    CHECK(strcmp(argv[0], "HELP") == 0);              /* matching is the shell's job */
    CHECK(strcmp(argv[1], "Me") == 0);
}

static void test_stays_inside_the_line(void)
{
    /* Bytes after the terminator are never read or written. */
    memset(line, '#', sizeof(line));
    memcpy(line, "a b", 4);
    CHECK_EQ(shell_parse(line, &argc, argv), PARSE_OK);
    CHECK_EQ(argc, 2);
    CHECK_EQ(line[4], '#');
    CHECK_EQ(line[127], '#');
}

int main(void)
{
    printf("parser unit tests\n");
    RUN(test_single_word);
    RUN(test_multiple_spaces);
    RUN(test_leading_and_trailing_whitespace);
    RUN(test_tabs);
    RUN(test_empty_and_blank);
    RUN(test_maximum_words);
    RUN(test_long_word);
    RUN(test_case_preserved);
    RUN(test_stays_inside_the_line);
    return unit_report("parser");
}
