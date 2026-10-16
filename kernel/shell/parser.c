/* Command-line splitter (see parser.h). */
#include "kernel/shell/parser.h"

static int is_space(char c)
{
    return c == ' ' || c == '\t';
}

enum parse_result shell_parse(char *line, int *argc, char *argv[SHELL_MAX_ARGS])
{
    int n = 0;
    char *p = line;

    *argc = 0;
    for (;;) {
        while (is_space(*p))
            *p++ = '\0';
        if (*p == '\0')
            break;
        if (n == SHELL_MAX_ARGS) {
            *argc = n;
            return PARSE_TOO_MANY;
        }
        argv[n++] = p;
        while (*p && !is_space(*p))
            p++;
    }
    *argc = n;
    return n ? PARSE_OK : PARSE_EMPTY;
}

/* 1.0 stage 9: how many '>' a word is made of (0 if any other byte). */
static int arrows(const char *w)
{
    int n = 0;
    for (; *w; w++) {
        if (*w != '>')
            return 0;
        n++;
    }
    return n;
}

enum parse_result shell_parse_line(char *line, int *argc, char *argv[SHELL_MAX_ARGS],
                                   struct shell_redirect *r)
{
    r->kind = REDIRECT_NONE;
    r->target = 0;
    enum parse_result res = shell_parse(line, argc, argv);
    if (res != PARSE_OK)
        return res;
    int at = -1;
    for (int i = 0; i < *argc; i++) {
        int n = arrows(argv[i]);
        if (n > 2) {
            r->target = argv[i];
            return PARSE_BAD_OPERATOR;
        }
        if (n == 0)
            continue;
        if (at >= 0)
            return PARSE_TWO_REDIRECTS;
        at = i;
    }
    if (at < 0)
        return PARSE_OK;
    if (at + 1 == *argc)
        return PARSE_NO_TARGET;
    r->kind = arrows(argv[at]) == 1 ? REDIRECT_WRITE : REDIRECT_APPEND;
    r->target = argv[at + 1];
    for (int i = at; i + 2 < *argc; i++)
        argv[i] = argv[i + 2];
    *argc -= 2;
    return *argc ? PARSE_OK : PARSE_NO_COMMAND;
}
