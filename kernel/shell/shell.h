#ifndef MADIRE_SHELL_SHELL_H
#define MADIRE_SHELL_SHELL_H

/*
 * Command shell core: parses a line and runs one of the built-in commands
 * (help, clear, version, uptime, mem, cpu, echo; ps when the context has
 * threads, 0.9; history, uname, hostname, date, dmesg, reboot, "help
 * <command>" and "<command> --help" when it has extras, 0.9.1).
 *
 * Pure module: every value it prints comes from a read-only shell_context
 * filled in by the caller, and all output goes through a shell_output
 * sink, so every command's exact output is unit-tested on the host
 * (tests/unit/test_shell.c). It never touches the screen or hardware.
 */

#include <stdbool.h>
#include <stdint.h>
#include "kernel/memory/heap.h"
#include "kernel/memory/memory.h"
#include "kernel/memory/paging.h"
#include "kernel/console/history.h"
#include "kernel/shell/cpuinfo.h"

/* 0.9.1: what the extra commands read (see shell_context.extras). */
struct shell_extras {
    const char *hostname;                   /* "hostname", "uname -a" */
    bool rtc_ok;                            /* "date": the RTC was read and valid */
    char date_text[20];                     /* "YYYY-MM-DD HH:MM:SS" (rtc_format) */
    const char *log;                        /* "dmesg": a snapshot of the kernel */
    uint32_t log_len;                       /*   log, oldest first (logbuf_copy) */
    bool log_wrapped;                       /*   older text was overwritten */
    const struct history *history;          /* "history" */
};

/* 0.9: one row of "ps" (a kernel thread). */
struct shell_thread {
    int32_t tid;
    const char *state;                      /* "running", "ready", "blocked", ... */
    uint64_t ticks;                         /* timer ticks it has run */
    uint32_t stack_peak;                    /* bytes, high-water mark */
    uint32_t stack_size;
    const char *name;
};

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
    uint64_t efer;                          /* 0.7: EFER MSR (0 = not read); LMA
                                             * picks the 64-bit wording of "cpu"
                                             * and "version" */
    /* 0.6, optional: when set, "mem" adds a paging and a kernel-heap
     * section; NULL leaves the 0.5 output exactly as it was. */
    const struct paging_stats *paging;
    const struct heap_stats *heap;          /* arena_bytes == 0: heap disabled */
    /* 0.8, optional: when stack_size is set, "mem" adds the kernel stack's
     * high-water mark (bytes used at the deepest point so far). */
    uint32_t stack_size;
    uint32_t stack_peak;
    /* 0.9, optional: when `threads` is set, "help" also lists "ps", which
     * shows these threads. NULL leaves every command's output (and "help")
     * exactly as before, and "ps" is then an unknown command. */
    const struct shell_thread *threads;
    uint32_t thread_count;
    /* 0.9.1, optional: when set, the commands history, uname, hostname,
     * date, dmesg and reboot exist and "help" lists them after the others;
     * "help <command>" and "<command> --help" show one command's purpose
     * and usage. NULL leaves every output exactly as before ("help x" is a
     * usage error, the new names are unknown commands). */
    const struct shell_extras *extras;
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
    SHELL_REBOOT,       /* "reboot": the question was printed; the console
                         * restarts only if the next line is "yes" (0.9.1) */
};

/* 0.9.1: the reboot confirmation (the console prints these). */
#define SHELL_REBOOT_QUESTION   "Restart Madire OS now? Type 'yes' to confirm:"
#define SHELL_REBOOT_STARTING   "Restarting..."
#define SHELL_REBOOT_CANCELLED  "Restart cancelled."

/* 0.9.1: true if `answer` confirms the reboot: exactly "yes", ignoring
 * spaces and tabs around it. */
bool shell_reboot_confirmed(const char *answer);

/* Runs one command line. The line is modified (split in place). */
enum shell_result shell_execute(char *line, const struct shell_context *c,
                                const struct shell_output *o);

#endif
