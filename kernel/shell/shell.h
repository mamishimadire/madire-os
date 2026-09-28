#ifndef MADIRE_SHELL_SHELL_H
#define MADIRE_SHELL_SHELL_H

/*
 * Command shell core: parses a line and runs one of the built-in commands
 * (help, clear, version, uptime, mem, cpu, echo).
 *
 * Pure module: every value it prints comes from a read-only shell_context
 * filled in by the caller, and all output goes through a shell_output
 * sink, so every command's exact output is unit-tested on the host
 * (tests/unit/test_shell.c). It never touches the screen or hardware.
 */

#include <stdbool.h>
#include <stdint.h>
#include "kernel/memory/memory.h"
#include "kernel/shell/cpuinfo.h"

struct shell_context {
    const char *version;                    /* e.g. "0.5" */
    bool timer_ok;                          /* timer verified at boot */
    uint32_t timer_hz;
    uint32_t uptime_seconds;
    uint64_t ticks;
    const struct memory_stats *mem;         /* figures from boot (0.3) */
    uint32_t free_pages_now;
    uint32_t page_size;
    const struct cpu_info *cpu;
    uint32_t cr0;
};

enum shell_text {
    SHELL_TEXT,
    SHELL_ERROR,
};

struct shell_output {
    /* Called once per output line; text ends with '\n'. */
    void (*write)(void *ctx, const char *text, enum shell_text kind);
    void *ctx;
};

enum shell_result {
    SHELL_OK,
    SHELL_EMPTY,        /* blank line: nothing to do */
    SHELL_UNKNOWN,      /* no such command */
    SHELL_BAD_ARGS,     /* wrong number of arguments: usage printed */
    SHELL_TOO_MANY,     /* more than 8 words */
    SHELL_CLEAR,        /* "clear": the console must clear the screen */
};

/* Runs one command line. The line is modified (split in place). */
enum shell_result shell_execute(char *line, const struct shell_context *c,
                                const struct shell_output *o);

#endif
