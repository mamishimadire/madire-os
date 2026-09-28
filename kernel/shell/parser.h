#ifndef MADIRE_SHELL_PARSER_H
#define MADIRE_SHELL_PARSER_H

/*
 * Command-line splitter. Pure module, unit-tested on the host
 * (tests/unit/test_parser.c).
 *
 * Spaces and tabs separate words; runs of them count as one separator;
 * leading and trailing ones are ignored. The line is split in place
 * (separators are overwritten with NUL), so argv points into it.
 */

#define SHELL_MAX_ARGS 8

enum parse_result {
    PARSE_OK,
    PARSE_EMPTY,        /* nothing but whitespace */
    PARSE_TOO_MANY,     /* more than SHELL_MAX_ARGS words */
};

enum parse_result shell_parse(char *line, int *argc, char *argv[SHELL_MAX_ARGS]);

#endif
