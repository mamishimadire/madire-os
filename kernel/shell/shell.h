#ifndef MADIRE_SHELL_SHELL_H
#define MADIRE_SHELL_SHELL_H

/*
 * Command shell core: parses a line and runs one of the built-in commands
 * (help, clear, version, uptime, mem, cpu, echo; ps when the context has
 * threads, 0.9; history, uname, hostname, date, dmesg, reboot, "help
 * <command>" and "<command> --help" when it has extras, 0.9.1; run, and
 * the processes in "ps", when it has procs, 0.11; prompt, alias, unalias,
 * help sections and "Did you mean" suggestions when it has state, 0.12;
 * set, unset, export, env, sleep, presskey, time and "kill -y" when it also
 * has a session, 0.12 stage 2; pwd, cd, ls, mkdir, rmdir, touch, stat and
 * cat, the help section "files" and the Live line of "version" when it has
 * files, 1.0 stage 8).
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

/* 0.11: one row of the process part of "ps". */
struct shell_process {
    int32_t pid;
    const char *state;                      /* "running" or "ended" */
    int32_t tid;
    const char *name;
};

/* 0.11: what "start" returns when it did not start a process. */
#define SHELL_RUN_NO_PROGRAM (-1)           /* no program module of that name */
#define SHELL_RUN_FAILED     (-2)           /* no free process slot or thread */
#define SHELL_RUN_TOO_BIG    (-3)           /* 1.0 stage 7: the start block would not fit (E2BIG) */

/* 0.11: user programs (see shell_context.procs). */
struct shell_procs {
    const struct shell_process *rows;       /* for "ps", after the threads */
    uint32_t count;
    /* Starts program module argv[0] with its arguments argv[0..argc-1]
     * and (1.0 stage 7) the environment env_names[i] '=' env_values[i];
     * returns the process ID (> 0) or one of SHELL_RUN_*. For a foreground
     * run the caller waits for the process after shell_execute returns
     * SHELL_RUN. */
    int32_t (*start)(void *ctx, int argc, char **argv, uint32_t envc,
                     const char *const *env_names, const char *const *env_values,
                     bool background);
    /* 0.11 stage 4b: "kill <pid>" asks first. The shell hands over the
     * process to end; the caller ends it only if the answer is "yes". */
    void (*ask_kill)(void *ctx, int32_t pid);
    void *ctx;
};

/* 0.12: the shell's own changeable state (see shell_context.state). */
#define SHELL_PROMPT_WIDTH      8           /* the prompt fills 8 columns */
#define SHELL_PROMPT_DEFAULT    "madire> "
#define SHELL_ALIAS_MAX         16          /* aliases at most */
#define SHELL_ALIAS_NAME_MAX    15          /* [a-z0-9_]{1,15} */
#define SHELL_ALIAS_VALUE_MAX   63          /* characters */

struct shell_alias {
    char name[SHELL_ALIAS_NAME_MAX + 1];
    char value[SHELL_ALIAS_VALUE_MAX + 1];
};

struct shell_state {
    char prompt[SHELL_PROMPT_WIDTH + 1];    /* always 8 characters, padded */
    struct shell_alias aliases[SHELL_ALIAS_MAX];    /* sorted by name */
    uint32_t alias_count;
};

/* The default prompt and no aliases. */
void shell_state_init(struct shell_state *s);

/* 0.12 stage 2: shell variables and the console's waits (see
 * shell_context.session). */
#define SHELL_VAR_MAX           32          /* variables at most */
#define SHELL_VAR_NAME_MAX      16          /* [A-Za-z_][A-Za-z0-9_]{0,15} */
#define SHELL_VAR_VALUE_MAX     63          /* characters */
#define SHELL_SLEEP_MAX         3600        /* seconds; "sleep" takes 1-3600 */

struct shell_var {
    char name[SHELL_VAR_NAME_MAX + 1];
    char value[SHELL_VAR_VALUE_MAX + 1];
    bool exported;                          /* "export": listed by "env" */
};

struct shell_session {
    struct shell_var vars[SHELL_VAR_MAX];   /* sorted by name, byte order */
    uint32_t var_count;
    /* Set by every shell_execute, for the console: */
    uint32_t sleep_seconds;                 /* SHELL_SLEEP: how long */
    bool timed;                             /* "time": the console prints the
                                             * time line (shell_time_line) once
                                             * the command is done, before the
                                             * next prompt */
};

/* No variables, nothing pending. */
void shell_session_init(struct shell_session *s);

/* 0.12 stage 2: "time: 250 ticks (2.50 s)" (hz 0: the ticks only). `out`
 * holds at least SHELL_TIME_LINE_MAX bytes; no newline. */
#define SHELL_TIME_LINE_MAX 64
void shell_time_line(char *out, uint64_t ticks, uint32_t hz);

/* 0.12 stage 2: what the console prints when a wait ends early. */
#define SHELL_PRESSKEY_TEXT         "Press any key to continue..."
#define SHELL_SLEEP_CANCELLED       "sleep: cancelled."
#define SHELL_PRESSKEY_CANCELLED    "presskey: cancelled."

/*
 * 1.0 stage 8: the file commands (see shell_context.files): pwd, cd, ls,
 * mkdir, rmdir, touch, stat and cat, in the help section "files". The
 * console reaches the filesystem through these calls, each one bounded
 * piece of work under the VFS lock (kernel/console/console.c); the shell
 * only words the results. Each returns 0 (or a count) or -FS_E...
 * (kernel/fs/fs_errno.h). Paths are the typed words: relative ones start
 * at the shell's own current folder.
 */
#define SHELL_FT_FILE   1
#define SHELL_FT_DIR    2
#define SHELL_NAME_MAX  255                 /* a name's bytes (NAME_MAX) */
#define SHELL_LS_BATCH  16                  /* entries per list_next call */
#define SHELL_PATH_MAX  4096                /* PATH_MAX, the NUL included */

struct shell_finfo {
    uint32_t type;                          /* SHELL_FT_* */
    uint32_t pages;                         /* files: the index page included */
    uint32_t nlink;                         /* 1 for a file, 2 + subfolders for a folder; 0 once removed */
    uint32_t mode;                          /* the permission bits stored */
    uint64_t size, ino;
    int64_t atime, mtime, ctime;            /* seconds since 1970 (no time zone) */
    char name[SHELL_NAME_MAX + 1];          /* list_next only: the entry's name */
};

struct shell_files {
    /* The current folder's path with its NUL into buf: its length with the
     * NUL, -FS_ENOENT (removed) or -FS_ENAMETOOLONG. */
    int64_t (*cwd_path)(void *ctx, char *buf, uint32_t size);
    bool (*cwd_removed)(void *ctx);         /* the current folder has no name left */
    int (*chdir)(void *ctx, const char *path);
    int (*stat)(void *ctx, const char *path, struct shell_finfo *out);
    int (*mkdir)(void *ctx, const char *path);
    int (*rmdir)(void *ctx, const char *path);
    int (*touch)(void *ctx, const char *path);  /* creates an empty file or sets its times */
    /* ls: list_open holds the folder at path (-FS_ENOTDIR for a file);
     * list_next copies up to max entries from *cursor on (creation order)
     * and moves *cursor past them: the count (0 at the end) or -FS_ENOENT
     * (the folder was removed); list_close gives the folder back. */
    int (*list_open)(void *ctx, const char *path);
    int (*list_next)(void *ctx, uint64_t *cursor, struct shell_finfo *rows, uint32_t max);
    void (*list_close)(void *ctx);
    /* cat: holds the file at path for the console's chunk loop (-FS_EISDIR
     * for a folder); the console reads and prints it after SHELL_CAT. */
    int (*cat_open)(void *ctx, const char *path);
    void *ctx;
};

/* "cat"'s text, one chunk at a time: bytes outside 0x20-0x7E but '\n' are
 * shown as '?', and lines are cut at 79 characters. shell_cat_end prints a
 * last line that had no newline. */
#define SHELL_CAT_WIDTH 79
struct shell_output;
struct shell_cat {
    char line[SHELL_CAT_WIDTH + 2];
    uint32_t n;
};
void shell_cat_init(struct shell_cat *s);
void shell_cat_put(struct shell_cat *s, const uint8_t *bytes, uint32_t len,
                   const struct shell_output *o);
void shell_cat_end(struct shell_cat *s, const struct shell_output *o);

#define SHELL_CAT_CANCELLED     "cat: cancelled."

/* "YYYY-MM-DD HH:MM:SS" (19 characters and a NUL) for seconds since 1970
 * (no time zone; a negative time is shown as 1970-01-01 00:00:00). */
void shell_date(int64_t seconds, char out[20]);

/* The Live label (D6): the console's lines, and version's second line. */
#define SHELL_LIVE_LINE1    "Live mode: files are kept in memory only."
#define SHELL_LIVE_LINE2    "Nothing is saved: all files are lost when the computer restarts."
#define SHELL_LIVE_VERSION  "Live mode (files in memory only)"

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
    /* 0.11, optional: when set, "run" and "kill" exist ("help" lists them
     * last), "ps"
     * adds the processes after the thread table (which stays exactly as
     * before) and its help says "Show threads and processes". NULL leaves
     * every output exactly as before. */
    const struct shell_procs *procs;
    /* 0.12, optional (the full shell): when set, "prompt", "alias" and
     * "unalias" exist ("help" lists them last, then the section names),
     * "help <section>" shows one section, an alias's name is replaced by
     * its value (the first word only, one level), and an unknown command or
     * help name adds "Did you mean: ...?". NULL leaves every output exactly
     * as before. The commands change this state; nothing else does. */
    struct shell_state *state;
    /* 0.12 stage 2, optional (the full shell, with state): when set, "set",
     * "unset", "export", "env", "sleep", "presskey" and "time" exist
     * ("help" lists them after "unalias", all in the shell section), and
     * "kill" also takes -y (no question; its usage becomes "kill [-y]
     * <pid>"). NULL leaves every output exactly as before. */
    struct shell_session *session;
    /* 1.0 stage 8, optional (the filesystem booted): when set, "pwd",
     * "cd", "ls", "mkdir", "rmdir", "touch", "stat" and "cat" exist ("help"
     * lists them after "time", in the section "files") and "version" adds
     * the Live mode line. NULL leaves every output exactly as before. */
    const struct shell_files *files;
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
    SHELL_RUN,          /* 0.11: "run" started a program in the foreground:
                         * no prompt until it ends (shell_end_line) */
    SHELL_KILL,         /* 0.11: "kill": the question was printed and the
                         * process handed to ask_kill; the console ends it
                         * only if the next line is "yes" */
    SHELL_KILL_NOW,     /* 0.12: "kill -y": the process was handed to ask_kill;
                         * the console ends it without asking */
    SHELL_SLEEP,        /* 0.12: "sleep": the console waits session->sleep_seconds
                         * (Ctrl+C cancels); no prompt until then */
    SHELL_PRESSKEY,     /* 0.12: "presskey": the text was printed; the console
                         * waits for one key press (Ctrl+C cancels) */
    SHELL_CAT,          /* 1.0 stage 8: "cat": the file is held (cat_open); the
                         * console prints it a chunk at a time (Ctrl+C
                         * cancels); no prompt until then */
};

/* 0.11: what the console prints after the kill question. */
#define SHELL_KILL_CANCELLED    "Kill cancelled."

/* 0.11: how a program run from the console ended (proc_core.h's
 * enum proc_end, as an int so this header stays independent of it). */
enum shell_end {
    SHELL_END_EXIT = 0,         /* code: the status */
    SHELL_END_FAULT = 1,        /* code: the vector, or < 0: it could not be loaded */
    SHELL_END_KILLED = 2,
    SHELL_END_LOAD_FAILED = 3,
};

/* 0.11: the line shown when a program ends, at most 79 characters:
 *   [process 3 'hello' exited, status 0]
 *   [process 4 'spin' killed]
 *   [process 5 'nullptr' ended by #PF at 0x0000000000000000]
 * `mnemonic` names the exception ("#PF"); `address` is CR2 for a page
 * fault, otherwise RIP. `out` holds at least SHELL_END_LINE_MAX bytes. */
#define SHELL_END_LINE_MAX 96
void shell_end_line(char *out, int32_t pid, const char *name, enum shell_end end, int32_t code,
                    const char *mnemonic, uint64_t address);

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
