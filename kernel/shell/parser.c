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
