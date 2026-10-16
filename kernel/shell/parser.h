#ifndef MADIRE_SHELL_PARSER_H
#define MADIRE_SHELL_PARSER_H

/*
 * Command-line splitter. Pure module, unit-tested on the host
 * (tests/unit/test_parser.c).
 *
 * Spaces and tabs separate words; runs of them count as one separator;
 * leading and trailing ones are ignored. The line is split in place
 * (separators are overwritten with NUL), so argv points into it.
 *
 * 1.0 stage 9 (plan D9, D11 (a)): shell_parse_line also finds an output
 * redirection. ">" and ">>" are operators only as whole words
 * ("echo hi > /tmp/a"); inside a word they are text ("hi>/tmp/a" is one
 * word: a documented deviation from Unix shells, to revisit with quoting).
 * The operator and its file are taken out of argv wherever they stand;
 * they count against the 8 words. shell_parse finds none: there a ">"
 * word is an ordinary word.
 */

#define SHELL_MAX_ARGS 8

enum parse_result {
    PARSE_OK,
    PARSE_EMPTY,        /* nothing but whitespace */
    PARSE_TOO_MANY,     /* more than SHELL_MAX_ARGS words */
    /* 1.0 stage 9, shell_parse_line only (the first problem from the left): */
    PARSE_NO_TARGET,    /* ">" or ">>" with no word after it */
    PARSE_TWO_REDIRECTS,/* a second ">" or ">>" (also as the file) */
    PARSE_BAD_OPERATOR, /* a word of three or more '>' (">>>") */
    PARSE_NO_COMMAND,   /* nothing but the redirection and its file */
};

enum shell_redirect_kind {
    REDIRECT_NONE,
    REDIRECT_WRITE,     /* ">": the file is created, or emptied first */
    REDIRECT_APPEND,    /* ">>": the output goes after its end */
};

struct shell_redirect {
    enum shell_redirect_kind kind;
    char *target;       /* the file's word (in the line); for PARSE_BAD_OPERATOR
                         * the operator word; otherwise 0 */
};

enum parse_result shell_parse(char *line, int *argc, char *argv[SHELL_MAX_ARGS]);

/* shell_parse, then the redirection (r->kind REDIRECT_NONE if none). On
 * PARSE_OK argv holds the command's words only. */
enum parse_result shell_parse_line(char *line, int *argc, char *argv[SHELL_MAX_ARGS],
                                   struct shell_redirect *r);

#endif
