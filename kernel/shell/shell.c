/*
 * Command shell core (see shell.h).
 *
 * Output lines are built in a small fixed buffer with local helpers (the
 * kernel's ksnprintf is not usable here: this module must stay pure).
 * Only 32-bit division is used; the 64-bit tick count is never divided.
 */
#include "kernel/logbuf.h"
#include "kernel/memory/vm_layout.h"
#include "kernel/shell/parser.h"
#include "kernel/shell/shell.h"

#define LINE_CAP 160

struct line {
    char s[LINE_CAP];
    uint32_t n;
};

static void l_reset(struct line *l)
{
    l->n = 0;
    l->s[0] = '\0';
}

static void l_char(struct line *l, char c)
{
    if (l->n + 1 < LINE_CAP) {
        l->s[l->n++] = c;
        l->s[l->n] = '\0';
    }
}

static void l_str(struct line *l, const char *s)
{
    while (*s)
        l_char(l, *s++);
}

static void l_pad_to(struct line *l, uint32_t col)
{
    while (l->n < col)
        l_char(l, ' ');
}

static void l_uint(struct line *l, uint32_t v, uint32_t width, char pad)
{
    char digits[10];
    uint32_t n = 0;
    do {
        digits[n++] = (char)('0' + v % 10);
        v /= 10;
    } while (v);
    for (uint32_t i = n; i < width; i++)
        l_char(l, pad);
    while (n)
        l_char(l, digits[--n]);
}

static void emit(const struct shell_output *o, struct line *l, enum shell_text kind)
{
    l_char(l, '\n');
    o->write(o->ctx, l->s, kind);
    l_reset(l);
}

static void say(const struct shell_output *o, const char *text, enum shell_text kind)
{
    struct line l;
    l_reset(&l);
    l_str(&l, text);
    emit(o, &l, kind);
}

static int str_eq(const char *a, const char *b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

static uint32_t bytes_to_mb(uint64_t bytes)
{
    return (uint32_t)((bytes + (1u << 19)) >> 20);      /* shift, not division */
}

/* EFER.LMA (bit 10): long mode is active. 0 = EFER not read. Used by both
 * "cpu" and "version", so the two always agree. */
static bool long_mode_active(const struct shell_context *c)
{
    return (c->efer & (1u << 10)) != 0;
}

/* ---- Commands ---- */

struct command {
    const char *name;
    const char *usage;
    const char *help;
    int max_args;                           /* arguments after the name */
    enum shell_result (*run)(int argc, char **argv, const struct shell_context *c,
                             const struct shell_output *o);
    int section;                            /* 0.12: SECTION_* ("help <section>") */
};

/* 0.12: the help sections; every command is in exactly one. */
enum { SECTION_SHELL, SECTION_SYSTEM, SECTION_PROGRAMS, SECTION_COUNT };

static enum shell_result cmd_help(int, char **, const struct shell_context *, const struct shell_output *);

static enum shell_result cmd_clear(int argc, char **argv, const struct shell_context *c,
                                   const struct shell_output *o)
{
    (void)argc; (void)argv; (void)c; (void)o;
    return SHELL_CLEAR;
}

static enum shell_result cmd_version(int argc, char **argv, const struct shell_context *c,
                                     const struct shell_output *o)
{
    struct line l;
    (void)argc; (void)argv;
    l_reset(&l);
    l_str(&l, "Madire OS v");
    l_str(&l, c->version);
    l_str(&l, long_mode_active(c) ? " (64-bit x86-64, Multiboot 1)"
                                  : " (32-bit x86, Multiboot 1)");
    emit(o, &l, SHELL_TEXT);
    return SHELL_OK;
}

static enum shell_result cmd_uptime(int argc, char **argv, const struct shell_context *c,
                                    const struct shell_output *o)
{
    struct line l;
    (void)argc; (void)argv;
    if (!c->timer_ok) {
        say(o, "Uptime: unavailable (timer not running)", SHELL_ERROR);
        return SHELL_OK;
    }
    uint32_t s = c->uptime_seconds;
    l_reset(&l);
    l_str(&l, "Uptime: ");
    l_uint(&l, s / 3600, 2, '0');
    l_char(&l, ':');
    l_uint(&l, (s / 60) % 60, 2, '0');
    l_char(&l, ':');
    l_uint(&l, s % 60, 2, '0');
    if ((c->ticks >> 32) == 0) {            /* shown while it fits in 32 bits */
        l_str(&l, " (");
        l_uint(&l, (uint32_t)c->ticks, 0, ' ');
        l_str(&l, " ticks at ");
        l_uint(&l, c->timer_hz, 0, ' ');
        l_str(&l, " Hz)");
    }
    emit(o, &l, SHELL_TEXT);
    return SHELL_OK;
}

static void mem_row(const struct shell_output *o, const char *label, uint32_t value,
                    const char *unit)
{
    struct line l;
    l_reset(&l);
    l_str(&l, label);
    l_pad_to(&l, 23);
    l_uint(&l, value, 6, ' ');
    l_str(&l, unit);
    emit(o, &l, SHELL_TEXT);
}

static void text_row(const struct shell_output *o, const char *label, const char *text)
{
    struct line l;
    l_reset(&l);
    l_str(&l, label);
    l_pad_to(&l, 23);
    l_str(&l, text);
    emit(o, &l, SHELL_TEXT);
}

/* "Mapped pages" is the kernel's own mapping made at boot; heap pages are
 * shown in the heap section. */
static void mem_paging(const struct shell_output *o, const struct paging_stats *p)
{
    if (!p->enabled) {
        text_row(o, "Paging:", "disabled");
        return;
    }
    text_row(o, "Paging:", p->nx ? "enabled (4-level, 4 KiB, higher-half, NX on)"
                                 : "enabled (4-level, 4 KiB, higher-half, NX off)");
    mem_row(o, "Mapped pages:", p->mapped_pages, "");
    mem_row(o, "Page tables:", p->page_tables, "");
    text_row(o, "NX:", !p->nx_supported ? "not supported by this CPU"
                       : !p->nx_enabled ? "supported, not enabled"
                       : "supported, enabled");
}

static void mem_heap(const struct shell_output *o, const struct heap_stats *h)
{
    struct line l;
    if (h->arena_bytes == 0) {
        text_row(o, "Kernel heap:", "disabled");
        return;
    }
    l_reset(&l);
    l_str(&l, "Kernel heap:");
    l_pad_to(&l, 23);
    l_uint(&l, h->arena_bytes >> 10, 6, ' ');
    l_str(&l, " KiB mapped, max ");
    l_uint(&l, VM_HEAP_MAX >> 10, 0, ' ');
    l_str(&l, " KiB");
    emit(o, &l, SHELL_TEXT);

    l_str(&l, "Heap in use:");
    l_pad_to(&l, 23);
    l_uint(&l, h->used_bytes >> 10, 6, ' ');
    l_str(&l, " KiB in ");
    l_uint(&l, h->live_blocks, 0, ' ');
    l_str(&l, h->live_blocks == 1 ? " block" : " blocks");
    emit(o, &l, SHELL_TEXT);

    l_str(&l, "Heap free:");
    l_pad_to(&l, 23);
    l_uint(&l, h->free_bytes >> 10, 6, ' ');
    l_str(&l, " KiB, largest block ");
    l_uint(&l, h->largest_free >> 10, 0, ' ');
    l_str(&l, " KiB");
    emit(o, &l, SHELL_TEXT);
}

static enum shell_result cmd_mem(int argc, char **argv, const struct shell_context *c,
                                 const struct shell_output *o)
{
    const struct memory_stats *m = c->mem;
    (void)argc; (void)argv;
    mem_row(o, "Physical RAM detected:", bytes_to_mb(m->ram_bytes), " MB");
    mem_row(o, "Usable RAM (<4 GiB):", bytes_to_mb(m->usable_bytes), " MB");
    mem_row(o, "RAM above 4 GiB:", bytes_to_mb(m->above_4g_bytes), " MB");
    mem_row(o, "Page size:", c->page_size, " bytes");
    mem_row(o, "Usable pages:", m->usable_pages, "");
    mem_row(o, "Free pages (now):", c->free_pages_now, "");
    mem_row(o, "Reserved ranges:", m->reservations, "");
    if (c->paging)
        mem_paging(o, c->paging);
    if (c->heap)
        mem_heap(o, c->heap);
    if (c->stack_size) {
        struct line l;
        l_reset(&l);
        l_str(&l, "Kernel stack:");
        l_pad_to(&l, 23);
        l_uint(&l, c->stack_peak, 6, ' ');
        l_str(&l, " bytes at peak, of ");
        l_uint(&l, c->stack_size, 0, ' ');
        emit(o, &l, SHELL_TEXT);
    }
    return SHELL_OK;
}

static void cpu_row(const struct shell_output *o, const char *label, const char *value)
{
    struct line l;
    l_reset(&l);
    l_str(&l, label);
    l_pad_to(&l, 14);
    l_str(&l, value);
    emit(o, &l, SHELL_TEXT);
}

static enum shell_result cmd_cpu(int argc, char **argv, const struct shell_context *c,
                                 const struct shell_output *o)
{
    const struct cpu_info *cpu = c->cpu;
    struct line l;
    (void)argc; (void)argv;

    if (!cpu->available) {
        cpu_row(o, "CPUID:", "not supported (pre-586 processor)");
    } else {
        cpu_row(o, "CPUID:", "supported");
        cpu_row(o, "Vendor:", cpu->vendor);
        if (cpu->brand[0])
            cpu_row(o, "Brand:", cpu->brand);
        if (cpu->has_signature) {
            l_reset(&l);
            l_str(&l, "Family/model: ");
            l_uint(&l, cpu->family, 0, ' ');
            l_str(&l, " / ");
            l_uint(&l, cpu->model, 0, ' ');
            l_str(&l, ", stepping ");
            l_uint(&l, cpu->stepping, 0, ' ');
            emit(o, &l, SHELL_TEXT);
            char features[LINE_CAP - 16];
            cpuinfo_features(cpu, features, sizeof(features));
            cpu_row(o, "Features:", features);
        }
        if (cpu->hypervisor[0])
            cpu_row(o, "Hypervisor:", cpu->hypervisor);
    }

    l_reset(&l);
    l_str(&l, "Mode:");
    l_pad_to(&l, 14);
    l_str(&l, long_mode_active(c) ? "64-bit long mode"
              : (c->cr0 & 1u) ? "32-bit protected mode" : "real mode");
    l_str(&l, (c->cr0 & 0x80000000u) ? ", paging on" : ", paging off");
    l_str(&l, ", 1 CPU in use");
    emit(o, &l, SHELL_TEXT);
    return SHELL_OK;
}

static enum shell_result cmd_echo(int argc, char **argv, const struct shell_context *c,
                                  const struct shell_output *o)
{
    struct line l;
    (void)c;
    l_reset(&l);
    for (int i = 1; i < argc; i++) {
        if (i > 1)
            l_char(&l, ' ');
        l_str(&l, argv[i]);
    }
    emit(o, &l, SHELL_TEXT);
    return SHELL_OK;
}

/* 0.11: the processes, after the thread table: PID right-aligned in 3,
 * STATE from column 5, TID right-aligned in 3 from column 14, NAME from
 * column 19 (at most 34 characters); then a count line. */
static void ps_processes(const struct shell_output *o, const struct shell_procs *p)
{
    struct line l;
    l_reset(&l);
    l_str(&l, "PID");
    l_pad_to(&l, 5);
    l_str(&l, "STATE");
    l_pad_to(&l, 14);
    l_str(&l, "TID");
    l_pad_to(&l, 19);
    l_str(&l, "NAME");
    emit(o, &l, SHELL_TEXT);
    for (uint32_t i = 0; i < p->count; i++) {
        const struct shell_process *r = &p->rows[i];
        l_uint(&l, (uint32_t)r->pid, 3, ' ');
        l_pad_to(&l, 5);
        l_str(&l, r->state);
        l_pad_to(&l, 14);
        l_uint(&l, (uint32_t)r->tid, 3, ' ');
        l_pad_to(&l, 19);
        l_str(&l, r->name);
        emit(o, &l, SHELL_TEXT);
    }
    l_uint(&l, p->count, 0, ' ');
    l_str(&l, p->count == 1 ? " process" : " processes");
    emit(o, &l, SHELL_TEXT);
}

/* 0.9: "ps", only when the context has threads. Columns (at most 54
 * characters): TID right-aligned in 3, STATE from column 5, TICKS
 * right-aligned to column 25, STACK PEAK from column 25 ("used/size"),
 * NAME from column 39; then a count line. */
static enum shell_result cmd_ps(int argc, char **argv, const struct shell_context *c,
                                const struct shell_output *o)
{
    struct line l;
    (void)argc; (void)argv;
    l_reset(&l);
    l_str(&l, "TID");
    l_pad_to(&l, 5);
    l_str(&l, "STATE");
    l_pad_to(&l, 16);
    l_str(&l, "TICKS");
    l_pad_to(&l, 25);
    l_str(&l, "STACK PEAK");
    l_pad_to(&l, 39);
    l_str(&l, "NAME");
    emit(o, &l, SHELL_TEXT);
    for (uint32_t i = 0; i < c->thread_count; i++) {
        const struct shell_thread *t = &c->threads[i];
        l_reset(&l);
        l_uint(&l, (uint32_t)t->tid, 3, ' ');
        l_pad_to(&l, 5);
        l_str(&l, t->state);
        l_pad_to(&l, 14);
        /* 32-bit division only: the count is shown while it fits. */
        if (t->ticks <= 0xFFFFFFFFull)
            l_uint(&l, (uint32_t)t->ticks, 7, ' ');
        else
            l_str(&l, "  large");
        l_pad_to(&l, 25);
        l_uint(&l, t->stack_peak, 5, ' ');
        l_char(&l, '/');
        l_uint(&l, t->stack_size, 0, ' ');
        l_pad_to(&l, 39);
        l_str(&l, t->name);
        emit(o, &l, SHELL_TEXT);
    }
    l_reset(&l);
    l_uint(&l, c->thread_count, 0, ' ');
    l_str(&l, c->thread_count == 1 ? " thread" : " threads");
    emit(o, &l, SHELL_TEXT);
    if (c->procs)
        ps_processes(o, c->procs);
    return SHELL_OK;
}

/* ---- 0.9.1: the extra commands (only when the context has extras) ---- */

static enum shell_result usage_error(const struct shell_output *o, const char *usage)
{
    struct line l;
    l_reset(&l);
    l_str(&l, "Usage: ");
    l_str(&l, usage);
    emit(o, &l, SHELL_ERROR);
    return SHELL_BAD_ARGS;
}

/* "   12  mem": the number right-aligned in 5 columns, two spaces, the line
 * (at most 71 characters, so at most 78 columns). */
static enum shell_result cmd_history(int argc, char **argv, const struct shell_context *c,
                                     const struct shell_output *o)
{
    const struct history *h = c->extras->history;
    struct line l;
    (void)argc; (void)argv;
    if (!h || history_size(h) == 0)
        return SHELL_OK;
    for (uint32_t n = history_first(h); n <= h->count; n++) {
        l_reset(&l);
        l_uint(&l, n, 5, ' ');
        l_str(&l, "  ");
        l_str(&l, history_get(h, n));
        emit(o, &l, SHELL_TEXT);
    }
    return SHELL_OK;
}

/* "uname": the system's name; "uname -a": name, host, version, architecture. */
static enum shell_result cmd_uname(int argc, char **argv, const struct shell_context *c,
                                   const struct shell_output *o)
{
    struct line l;
    if (argc == 2 && !str_eq(argv[1], "-a"))
        return usage_error(o, "uname [-a]");
    l_reset(&l);
    l_str(&l, "Madire OS");
    if (argc == 2) {
        l_char(&l, ' ');
        l_str(&l, c->extras->hostname ? c->extras->hostname : "unknown");
        l_char(&l, ' ');
        l_str(&l, c->version);
        l_str(&l, long_mode_active(c) ? " x86_64" : " x86");
    }
    emit(o, &l, SHELL_TEXT);
    return SHELL_OK;
}

static enum shell_result cmd_hostname(int argc, char **argv, const struct shell_context *c,
                                      const struct shell_output *o)
{
    (void)argc; (void)argv;
    say(o, c->extras->hostname ? c->extras->hostname : "unknown", SHELL_TEXT);
    return SHELL_OK;
}

static enum shell_result cmd_date(int argc, char **argv, const struct shell_context *c,
                                  const struct shell_output *o)
{
    struct line l;
    (void)argc; (void)argv;
    if (!c->extras->rtc_ok) {
        say(o, "Date: unavailable (RTC not responding)", SHELL_ERROR);
        return SHELL_OK;
    }
    l_reset(&l);
    l_str(&l, c->extras->date_text);
    l_str(&l, " (RTC; time zone not set)");
    emit(o, &l, SHELL_TEXT);
    return SHELL_OK;
}

#define DMESG_DEFAULT_LINES 20u
#define DMESG_MAX_LINES     9999u

/* "dmesg [lines]": the last lines of the kernel log (default 20), each
 * shortened to 79 columns if needed. */
static enum shell_result cmd_dmesg(int argc, char **argv, const struct shell_context *c,
                                   const struct shell_output *o)
{
    const struct shell_extras *e = c->extras;
    uint32_t want = DMESG_DEFAULT_LINES;
    if (argc == 2) {
        const char *p = argv[1];
        want = 0;
        if (!*p)
            return usage_error(o, "dmesg [lines]");
        for (; *p; p++) {
            if (*p < '0' || *p > '9' || want > DMESG_MAX_LINES)
                return usage_error(o, "dmesg [lines]");
            want = want * 10 + (uint32_t)(*p - '0');
        }
        if (want == 0 || want > DMESG_MAX_LINES)
            return usage_error(o, "dmesg [lines]");
    }
    uint32_t lines = 0;
    uint32_t pos = e->log ? logbuf_tail(e->log, e->log_len, e->log_wrapped, want, &lines) : 0;
    if (lines == 0) {
        say(o, "(the kernel log is empty)", SHELL_TEXT);
        return SHELL_OK;
    }
    while (pos < e->log_len) {
        uint32_t end = pos;
        while (end < e->log_len && e->log[end] != '\n')
            end++;
        char fitted[LOGBUF_LINE_WIDTH + 1];
        logbuf_fit(e->log + pos, end - pos, fitted, LOGBUF_LINE_WIDTH);
        say(o, fitted, SHELL_TEXT);
        pos = end + 1;
    }
    return SHELL_OK;
}

/* "reboot": asks; the console does the rest (SHELL_REBOOT). */
static enum shell_result cmd_reboot(int argc, char **argv, const struct shell_context *c,
                                    const struct shell_output *o)
{
    (void)argc; (void)argv; (void)c;
    say(o, SHELL_REBOOT_QUESTION, SHELL_TEXT);
    return SHELL_REBOOT;
}

bool shell_reboot_confirmed(const char *answer)
{
    while (*answer == ' ' || *answer == '\t')
        answer++;
    const char *yes = "yes";
    while (*yes && *answer == *yes) {
        answer++;
        yes++;
    }
    if (*yes)
        return false;
    while (*answer == ' ' || *answer == '\t')
        answer++;
    return *answer == '\0';
}

/* ---- 0.11: run (only when the context has procs) ---- */

#define RUN_USAGE      "run [-b] <program> [number]"
#define KILL_Y_USAGE   "kill [-y] <pid>"        /* 0.12 stage 2, with a session */
#define RUN_NAME_SHOWN 40       /* longer names are shortened, so the line fits */

static void l_name(struct line *l, const char *name)
{
    uint32_t n = 0;
    while (name[n])
        n++;
    if (n <= RUN_NAME_SHOWN) {
        l_str(l, name);
        return;
    }
    for (uint32_t i = 0; i < RUN_NAME_SHOWN - 3; i++)
        l_char(l, name[i]);
    l_str(l, "...");
}

static void l_hex64(struct line *l, uint64_t v)
{
    l_str(l, "0x");
    for (int shift = 60; shift >= 0; shift -= 4)
        l_char(l, "0123456789abcdef"[(v >> shift) & 0xF]);
}

/* A decimal number from 0 to 4294967295, nothing else. */
static bool parse_arg(const char *s, uint64_t *out)
{
    uint64_t v = 0;
    if (!*s)
        return false;
    for (; *s; s++) {
        if (*s < '0' || *s > '9')
            return false;
        v = v * 10 + (uint64_t)(*s - '0');
        if (v > 0xFFFFFFFFull)
            return false;
    }
    *out = v;
    return true;
}

/* "run hello", "run exitcode 1", "run -b spin": a program module, in the
 * foreground (the console waits; its output goes to the serial log and
 * dmesg) or in the background. */
static enum shell_result cmd_run(int argc, char **argv, const struct shell_context *c,
                                 const struct shell_output *o)
{
    struct line l;
    int i = 1;
    bool background = argc > 1 && str_eq(argv[1], "-b");
    if (background)
        i++;
    if (i >= argc || argc - i > 2 || argv[i][0] == '-')
        return usage_error(o, RUN_USAGE);
    const char *name = argv[i];
    uint64_t arg = 0;
    if (argc - i == 2 && !parse_arg(argv[i + 1], &arg)) {
        l_reset(&l);
        l_str(&l, "run: '");
        l_name(&l, argv[i + 1]);
        l_str(&l, "' is not a number from 0 to 4294967295.");
        emit(o, &l, SHELL_ERROR);
        return SHELL_BAD_ARGS;
    }
    const struct shell_procs *p = c->procs;
    int32_t pid = p->start(p->ctx, name, arg, background);
    if (pid == SHELL_RUN_NO_PROGRAM || pid == SHELL_RUN_FAILED || pid <= 0) {
        l_reset(&l);
        l_str(&l, pid == SHELL_RUN_NO_PROGRAM ? "run: no program named '"
                                              : "run: could not start '");
        l_name(&l, name);
        l_str(&l, pid == SHELL_RUN_NO_PROGRAM ? "'." : "' (no free process slot).");
        emit(o, &l, SHELL_ERROR);
        return SHELL_BAD_ARGS;
    }
    if (!background)
        return SHELL_RUN;
    l_reset(&l);
    l_str(&l, "[process ");
    l_uint(&l, (uint32_t)pid, 0, ' ');
    l_str(&l, " '");
    l_str(&l, name);
    l_str(&l, "' started in the background]");
    emit(o, &l, SHELL_TEXT);
    return SHELL_OK;
}

/* "kill 7": asks first ("End process 7 'spin'? Type 'yes' to confirm:");
 * the console ends it on "yes". PID 0, unknown and ended processes are
 * refused here. 0.12 stage 2 (with a session): "kill -y 7" makes the same
 * refusals but does not ask; the console ends the process at once. */
static enum shell_result cmd_kill(int argc, char **argv, const struct shell_context *c,
                                  const struct shell_output *o)
{
    struct line l;
    uint64_t pid = 0;
    bool yes = false;
    if (c->session && argc >= 2 && str_eq(argv[1], "-y")) {
        yes = true;
        argv++;
        argc--;
    }
    if (argc != 2)
        return usage_error(o, c->session ? KILL_Y_USAGE : "kill <pid>");
    if (!parse_arg(argv[1], &pid) || pid > 0x7FFFFFFFull) {
        l_reset(&l);
        l_str(&l, "kill: '");
        l_name(&l, argv[1]);
        l_str(&l, "' is not a process ID.");
        emit(o, &l, SHELL_ERROR);
        return SHELL_BAD_ARGS;
    }
    l_reset(&l);
    if (pid == 0) {
        l_str(&l, "kill: PID 0 is the kernel, not a process.");
        emit(o, &l, SHELL_ERROR);
        return SHELL_BAD_ARGS;
    }
    const struct shell_procs *p = c->procs;
    const struct shell_process *r = 0;
    for (uint32_t i = 0; i < p->count && !r; i++)
        if ((uint64_t)p->rows[i].pid == pid)
            r = &p->rows[i];
    if (!r) {
        l_str(&l, "kill: no process with PID ");
        l_uint(&l, (uint32_t)pid, 0, ' ');
        l_char(&l, '.');
        emit(o, &l, SHELL_ERROR);
        return SHELL_BAD_ARGS;
    }
    if (str_eq(r->state, "ended")) {
        l_str(&l, "kill: process ");
        l_uint(&l, (uint32_t)pid, 0, ' ');
        l_str(&l, " '");
        l_str(&l, r->name);
        l_str(&l, "' has already ended.");
        emit(o, &l, SHELL_ERROR);
        return SHELL_BAD_ARGS;
    }
    if (yes) {
        p->ask_kill(p->ctx, (int32_t)pid);
        return SHELL_KILL_NOW;
    }
    l_str(&l, "End process ");
    l_uint(&l, (uint32_t)pid, 0, ' ');
    l_str(&l, " '");
    l_str(&l, r->name);
    l_str(&l, "'? Type 'yes' to confirm:");
    emit(o, &l, SHELL_TEXT);
    p->ask_kill(p->ctx, (int32_t)pid);
    return SHELL_KILL;
}

void shell_end_line(char *out, int32_t pid, const char *name, enum shell_end end, int32_t code,
                    const char *mnemonic, uint64_t address)
{
    struct line l;
    l_reset(&l);
    l_str(&l, "[process ");
    l_uint(&l, (uint32_t)pid, 0, ' ');
    l_str(&l, " '");
    l_name(&l, name);
    l_str(&l, "' ");
    switch (end) {
    case SHELL_END_EXIT:
        l_str(&l, "exited, status ");
        l_uint(&l, (uint32_t)code, 0, ' ');
        break;
    case SHELL_END_KILLED:
        l_str(&l, "killed");
        break;
    case SHELL_END_FAULT:
        if (code < 0) {
            l_str(&l, "could not be loaded (no memory)");
        } else {
            l_str(&l, "ended by ");
            l_str(&l, mnemonic ? mnemonic : "an exception");
            l_str(&l, " at ");
            l_hex64(&l, address);
        }
        break;
    case SHELL_END_LOAD_FAILED:
    default:
        l_str(&l, "load failed");
        break;
    }
    l_char(&l, ']');
    uint32_t k = 0;
    for (; k + 1 < SHELL_END_LINE_MAX && l.s[k]; k++)
        out[k] = l.s[k];
    out[k] = '\0';
}

static const struct command commands[] = {
    { "help",    "help",          "Show this list",             0, cmd_help, SECTION_SHELL },
    { "clear",   "clear",         "Clear the screen",           0, cmd_clear, SECTION_SHELL },
    { "version", "version",       "Show the Madire OS version", 0, cmd_version, SECTION_SYSTEM },
    { "uptime",  "uptime",        "Show time since boot",       0, cmd_uptime, SECTION_SYSTEM },
    { "mem",     "mem",           "Show memory information",    0, cmd_mem, SECTION_SYSTEM },
    { "cpu",     "cpu",           "Show processor information", 0, cmd_cpu, SECTION_SYSTEM },
    { "echo",    "echo <text>",   "Print text: echo <text>",    SHELL_MAX_ARGS - 1, cmd_echo, SECTION_SHELL },
};

#define COMMAND_COUNT (sizeof(commands) / sizeof(commands[0]))

/* 0.9: offered only when the context has threads (see shell.h). */
static const struct command ps_command =
    { "ps",      "ps",            "Show threads",               0, cmd_ps, SECTION_PROGRAMS };

/* 0.9.1: offered only when the context has extras (see shell.h). */
static const struct command extra_commands[] = {
    { "history",  "history",       "Show previous commands (Up/Down recall)", 0, cmd_history, SECTION_SHELL },
    { "uname",    "uname [-a]",    "Show the system name: uname [-a]",        1, cmd_uname, SECTION_SYSTEM },
    { "hostname", "hostname",      "Show the computer's name",                0, cmd_hostname, SECTION_SYSTEM },
    { "date",     "date",          "Show the date and time (RTC)",            0, cmd_date, SECTION_SYSTEM },
    { "dmesg",    "dmesg [lines]", "Show kernel messages: dmesg [lines]",     1, cmd_dmesg, SECTION_SYSTEM },
    { "reboot",   "reboot",        "Restart the computer (asks first)",       0, cmd_reboot, SECTION_SYSTEM },
};

#define EXTRA_COUNT (sizeof(extra_commands) / sizeof(extra_commands[0]))

/* 0.11: offered only when the context has procs (see shell.h); "ps" then
 * also shows the processes. */
static const struct command ps_procs_command =
    { "ps",      "ps",            "Show threads and processes", 0, cmd_ps, SECTION_PROGRAMS };
static const struct command run_command =
    { "run",     RUN_USAGE,       "Run a program: run [-b] <name> [n]", 3, cmd_run, SECTION_PROGRAMS };
static const struct command kill_command =
    { "kill",    "kill <pid>",    "End a process: kill <pid> (asks first)", 1, cmd_kill, SECTION_PROGRAMS };

/* 0.12: offered only when the context has state (see shell.h). */
static enum shell_result cmd_prompt(int, char **, const struct shell_context *, const struct shell_output *);
static enum shell_result cmd_alias(int, char **, const struct shell_context *, const struct shell_output *);
static enum shell_result cmd_unalias(int, char **, const struct shell_context *, const struct shell_output *);

static const struct command state_commands[] = {
    { "prompt",  "prompt [text | --reset]",   "Show or change the prompt: prompt [text]",
      1, cmd_prompt, SECTION_SHELL },
    { "alias",   "alias [name [command...]]", "Show or make short names: alias [name [command]]",
      SHELL_MAX_ARGS - 1, cmd_alias, SECTION_SHELL },
    { "unalias", "unalias <name>",            "Remove a short name: unalias <name>",
      1, cmd_unalias, SECTION_SHELL },
};

#define STATE_COUNT (sizeof(state_commands) / sizeof(state_commands[0]))

/* 0.12 stage 2: offered only when the context has a session (see shell.h);
 * "kill" then also takes -y. */
static enum shell_result cmd_set(int, char **, const struct shell_context *, const struct shell_output *);
static enum shell_result cmd_unset(int, char **, const struct shell_context *, const struct shell_output *);
static enum shell_result cmd_export(int, char **, const struct shell_context *, const struct shell_output *);
static enum shell_result cmd_env(int, char **, const struct shell_context *, const struct shell_output *);
static enum shell_result cmd_sleep(int, char **, const struct shell_context *, const struct shell_output *);
static enum shell_result cmd_presskey(int, char **, const struct shell_context *, const struct shell_output *);
static enum shell_result cmd_time(int, char **, const struct shell_context *, const struct shell_output *);

static const struct command kill_y_command =
    { "kill",    KILL_Y_USAGE,    "End a process: kill <pid> (asks first)", 2, cmd_kill, SECTION_PROGRAMS };

static const struct command session_commands[] = {
    { "set",      "set [NAME[=value]] (names are case-sensitive)", "Show or set variables: set [NAME[=value]]",
      SHELL_MAX_ARGS - 1, cmd_set, SECTION_SHELL },
    { "unset",    "unset <NAME>",      "Remove a variable: unset <NAME>",            1, cmd_unset, SECTION_SHELL },
    { "export",   "export <NAME>",     "Mark a variable as exported: export <NAME>", 1, cmd_export, SECTION_SHELL },
    { "env",      "env",               "Show the exported variables",                0, cmd_env, SECTION_SHELL },
    { "sleep",    "sleep <seconds>",   "Wait: sleep <seconds> (1-3600, Ctrl+C stops)", 1, cmd_sleep, SECTION_SHELL },
    { "presskey", "presskey",          "Wait for a key press (Ctrl+C cancels)",      0, cmd_presskey, SECTION_SHELL },
    { "time",     "time <command...>", "Time a command: time <command>",
      SHELL_MAX_ARGS - 1, cmd_time, SECTION_SHELL },
};

#define SESSION_COUNT (sizeof(session_commands) / sizeof(session_commands[0]))
#define TIME_COMMAND  (&session_commands[SESSION_COUNT - 1])

static const char *const section_names[SECTION_COUNT] = { "shell", "system", "programs" };
static const char *const section_titles[SECTION_COUNT] = {
    "The shell's own commands",
    "Information about the system",
    "Programs and processes",
};

static const struct command *ps_of(const struct shell_context *c)
{
    return c->procs ? &ps_procs_command : &ps_command;
}

static const struct command *kill_of(const struct shell_context *c)
{
    return c->session ? &kill_y_command : &kill_command;
}

/* Every command the context offers, in "help" order. Returns the count. */
#define OFFERED_MAX (COMMAND_COUNT + 1 + EXTRA_COUNT + 2 + STATE_COUNT + SESSION_COUNT)

static uint32_t offered(const struct shell_context *c, const struct command **list)
{
    uint32_t n = 0;
    for (uint32_t i = 0; i < COMMAND_COUNT; i++)
        list[n++] = &commands[i];
    if (c->threads)
        list[n++] = ps_of(c);
    if (c->extras)
        for (uint32_t i = 0; i < EXTRA_COUNT; i++)
            list[n++] = &extra_commands[i];
    if (c->procs) {
        list[n++] = &run_command;
        list[n++] = kill_of(c);
    }
    if (c->state)
        for (uint32_t i = 0; i < STATE_COUNT; i++)
            list[n++] = &state_commands[i];
    if (c->session)
        for (uint32_t i = 0; i < SESSION_COUNT; i++)
            list[n++] = &session_commands[i];
    return n;
}

/* 0.12: true for the name of any built-in command, whatever the context
 * offers (an alias may never take one). */
static bool builtin_name(const char *name)
{
    if (str_eq(name, ps_command.name) || str_eq(name, run_command.name) ||
        str_eq(name, kill_command.name))
        return true;
    for (uint32_t i = 0; i < COMMAND_COUNT; i++)
        if (str_eq(name, commands[i].name))
            return true;
    for (uint32_t i = 0; i < EXTRA_COUNT; i++)
        if (str_eq(name, extra_commands[i].name))
            return true;
    for (uint32_t i = 0; i < STATE_COUNT; i++)
        if (str_eq(name, state_commands[i].name))
            return true;
    for (uint32_t i = 0; i < SESSION_COUNT; i++)
        if (str_eq(name, session_commands[i].name))
            return true;
    return false;
}

static const struct command *find_command(const char *name, const struct shell_context *c)
{
    if (c->state)
        for (uint32_t i = 0; i < STATE_COUNT; i++)
            if (str_eq(name, state_commands[i].name))
                return &state_commands[i];
    if (c->session)
        for (uint32_t i = 0; i < SESSION_COUNT; i++)
            if (str_eq(name, session_commands[i].name))
                return &session_commands[i];
    for (uint32_t i = 0; i < COMMAND_COUNT; i++)
        if (str_eq(name, commands[i].name))
            return &commands[i];
    if (c->threads && str_eq(name, ps_command.name))
        return ps_of(c);
    if (c->extras)
        for (uint32_t i = 0; i < EXTRA_COUNT; i++)
            if (str_eq(name, extra_commands[i].name))
                return &extra_commands[i];
    if (c->procs && str_eq(name, run_command.name))
        return &run_command;
    if (c->procs && str_eq(name, kill_command.name))
        return kill_of(c);
    return 0;
}

/* "help"'s own usage: a command name (0.9.1), or a section (0.12). */
static const char *help_usage(const struct shell_context *c)
{
    return c->state ? "help [section | command]" : "help [command]";
}

/* "help mem" / "mem --help": the purpose, then the usage. "help" itself
 * takes an optional command name when the context has extras. */
static void help_topic(const struct shell_output *o, const struct command *cmd,
                       const struct shell_context *c)
{
    struct line l;
    l_reset(&l);
    l_str(&l, cmd->name);
    l_str(&l, ": ");
    l_str(&l, cmd->help);
    emit(o, &l, SHELL_TEXT);
    l_str(&l, "Usage: ");
    l_str(&l, cmd == &commands[0] ? help_usage(c) : cmd->usage);
    emit(o, &l, SHELL_TEXT);
}

#define HELP_NAME_SHOWN 40      /* longer names are shortened, so the line fits */

/* ---- 0.12: "Did you mean" ---- */

#define SUGGEST_MAX      3
#define SUGGEST_DISTANCE 2
#define DISTANCE_NAME    24     /* longer names are never within 2 of a candidate */

static uint32_t str_len(const char *s)
{
    uint32_t n = 0;
    while (s[n])
        n++;
    return n;
}

static int str_cmp(const char *a, const char *b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}

/* The edit distance (insertions, deletions, substitutions; case-sensitive),
 * or SUGGEST_DISTANCE + 1 for anything farther. */
static uint32_t edit_distance(const char *a, const char *b)
{
    uint32_t na = str_len(a), nb = str_len(b);
    if (na > nb + SUGGEST_DISTANCE || nb > na + SUGGEST_DISTANCE ||
        na > DISTANCE_NAME || nb > DISTANCE_NAME)
        return SUGGEST_DISTANCE + 1;
    uint32_t prev[DISTANCE_NAME + 1], cur[DISTANCE_NAME + 1];
    for (uint32_t j = 0; j <= nb; j++)
        prev[j] = j;
    for (uint32_t i = 1; i <= na; i++) {
        cur[0] = i;
        for (uint32_t j = 1; j <= nb; j++) {
            uint32_t best = prev[j - 1] + (a[i - 1] != b[j - 1]);
            if (prev[j] + 1 < best)
                best = prev[j] + 1;
            if (cur[j - 1] + 1 < best)
                best = cur[j - 1] + 1;
            cur[j] = best;
        }
        for (uint32_t j = 0; j <= nb; j++)
            prev[j] = cur[j];
    }
    return prev[nb] > SUGGEST_DISTANCE ? SUGGEST_DISTANCE + 1 : prev[nb];
}

/* Up to 3 names, nearest first, then alphabetically. */
struct suggestions {
    const char *name[SUGGEST_MAX];
    uint32_t distance[SUGGEST_MAX];
    uint32_t n;
};

static void suggest(struct suggestions *s, const char *typed, const char *name)
{
    uint32_t d = edit_distance(typed, name);
    if (d > SUGGEST_DISTANCE)
        return;
    for (uint32_t i = 0; i < s->n; i++)
        if (str_eq(s->name[i], name))
            return;
    uint32_t at = 0;
    while (at < s->n && (s->distance[at] < d ||
                         (s->distance[at] == d && str_cmp(s->name[at], name) < 0)))
        at++;
    if (at >= SUGGEST_MAX)
        return;
    uint32_t last = s->n < SUGGEST_MAX ? s->n : SUGGEST_MAX - 1;
    for (uint32_t i = last; i > at; i--) {
        s->name[i] = s->name[i - 1];
        s->distance[i] = s->distance[i - 1];
    }
    s->name[at] = name;
    s->distance[at] = d;
    if (s->n < SUGGEST_MAX)
        s->n++;
}

/* "Did you mean: cpu, mem?" (nothing when no name is close). */
static void say_suggestions(const struct shell_output *o, const struct suggestions *s)
{
    struct line l;
    if (s->n == 0)
        return;
    l_reset(&l);
    l_str(&l, "Did you mean: ");
    for (uint32_t i = 0; i < s->n; i++) {
        if (i)
            l_str(&l, ", ");
        l_str(&l, s->name[i]);
    }
    l_char(&l, '?');
    emit(o, &l, SHELL_TEXT);
}

/* The candidates for an unknown name: the commands offered, and for a
 * command line also the aliases. */
static void suggest_commands(struct suggestions *s, const char *typed,
                             const struct shell_context *c, bool aliases)
{
    const struct command *list[OFFERED_MAX];
    uint32_t n = offered(c, list);
    for (uint32_t i = 0; i < n; i++)
        suggest(s, typed, list[i]->name);
    if (aliases)
        for (uint32_t i = 0; i < c->state->alias_count; i++)
            suggest(s, typed, c->state->aliases[i].name);
}

static enum shell_result help_unknown(const struct shell_output *o, const char *name,
                                      const struct shell_context *c)
{
    struct line l;
    uint32_t n = str_len(name);
    l_reset(&l);
    l_str(&l, "No help for '");
    if (n <= HELP_NAME_SHOWN) {
        l_str(&l, name);
    } else {
        for (uint32_t i = 0; i < HELP_NAME_SHOWN - 3; i++)
            l_char(&l, name[i]);
        l_str(&l, "...");
    }
    l_str(&l, "': unknown command.");
    emit(o, &l, SHELL_ERROR);
    if (c->state) {                         /* 0.12: the sections and commands */
        struct suggestions s = { { 0 }, { 0 }, 0 };
        for (uint32_t i = 0; i < SECTION_COUNT; i++)
            suggest(&s, name, section_names[i]);
        suggest_commands(&s, name, c, false);
        say_suggestions(o, &s);
    }
    return SHELL_UNKNOWN;
}

static void help_row(const struct shell_output *o, const struct command *cmd)
{
    struct line l;
    l_reset(&l);
    l_str(&l, "  ");
    l_str(&l, cmd->name);
    l_pad_to(&l, 11);
    l_str(&l, cmd->help);
    emit(o, &l, SHELL_TEXT);
}

static enum shell_result cmd_help(int argc, char **argv, const struct shell_context *c,
                                  const struct shell_output *o)
{
    const struct command *list[OFFERED_MAX];
    uint32_t n = offered(c, list);
    (void)argc; (void)argv;
    say(o, "Available commands:", SHELL_TEXT);
    for (uint32_t i = 0; i < n; i++)
        help_row(o, list[i]);
    if (c->state)
        say(o, "Sections: shell system programs (help <section>)", SHELL_TEXT);
    return SHELL_OK;
}

/* 0.12: "help shell": the section's title, then its commands in "help" order. */
static void help_section(const struct shell_output *o, const struct shell_context *c, int section)
{
    const struct command *list[OFFERED_MAX];
    uint32_t n = offered(c, list);
    struct line l;
    l_reset(&l);
    l_str(&l, section_names[section]);
    l_str(&l, ": ");
    l_str(&l, section_titles[section]);
    emit(o, &l, SHELL_TEXT);
    for (uint32_t i = 0; i < n; i++)
        if (list[i]->section == section)
            help_row(o, list[i]);
}

/* ---- 0.12: prompt (only when the context has state) ---- */

static void prompt_set(struct shell_state *s, const char *text)
{
    uint32_t i = 0;
    for (; text[i] && i < SHELL_PROMPT_WIDTH; i++)
        s->prompt[i] = text[i];
    for (; i < SHELL_PROMPT_WIDTH; i++)
        s->prompt[i] = ' ';
    s->prompt[SHELL_PROMPT_WIDTH] = '\0';
}

void shell_state_init(struct shell_state *s)
{
    prompt_set(s, SHELL_PROMPT_DEFAULT);
    s->alias_count = 0;
}

static void l_quoted_prompt(struct line *l, const struct shell_state *s)
{
    l_char(l, '\'');
    l_str(l, s->prompt);
    l_char(l, '\'');
}

/* "prompt": show it; "prompt abc>": 1-8 printable characters, no spaces,
 * not starting with '-', shown padded to 8; "prompt --reset": the default.
 * Kept in memory only. */
static enum shell_result cmd_prompt(int argc, char **argv, const struct shell_context *c,
                                    const struct shell_output *o)
{
    struct shell_state *s = c->state;
    struct line l;
    l_reset(&l);
    if (argc == 1) {
        l_str(&l, "Prompt: ");
        l_quoted_prompt(&l, s);
        if (str_eq(s->prompt, SHELL_PROMPT_DEFAULT))
            l_str(&l, " (the default)");
        emit(o, &l, SHELL_TEXT);
        return SHELL_OK;
    }
    const char *t = argv[1];
    if (str_eq(t, "--reset")) {
        prompt_set(s, SHELL_PROMPT_DEFAULT);
        l_str(&l, "Prompt reset to ");
        l_quoted_prompt(&l, s);
        l_char(&l, '.');
        emit(o, &l, SHELL_TEXT);
        return SHELL_OK;
    }
    if (t[0] == '-') {
        say(o, "prompt: the text may not start with '-' (to reset: prompt --reset).", SHELL_ERROR);
        return SHELL_BAD_ARGS;
    }
    uint32_t n = str_len(t);
    if (n > SHELL_PROMPT_WIDTH) {
        l_str(&l, "prompt: at most 8 characters (this text has ");
        l_uint(&l, n, 0, ' ');
        l_str(&l, ").");
        emit(o, &l, SHELL_ERROR);
        return SHELL_BAD_ARGS;
    }
    for (uint32_t i = 0; i < n; i++) {
        if (t[i] <= ' ' || t[i] > '~') {
            say(o, "prompt: only printable characters, no spaces.", SHELL_ERROR);
            return SHELL_BAD_ARGS;
        }
    }
    prompt_set(s, t);
    l_str(&l, "Prompt set to ");
    l_quoted_prompt(&l, s);
    l_char(&l, '.');
    emit(o, &l, SHELL_TEXT);
    return SHELL_OK;
}

/* ---- 0.12: alias and unalias (only when the context has state) ---- */

static struct shell_alias *alias_find(struct shell_state *s, const char *name)
{
    for (uint32_t i = 0; i < s->alias_count; i++)
        if (str_eq(s->aliases[i].name, name))
            return &s->aliases[i];
    return 0;
}

/* "ll              echo hi": the name in 16 columns, then the command
 * (at most 16 + 63 = 79 columns). */
static void alias_row(const struct shell_output *o, const struct shell_alias *a)
{
    struct line l;
    l_reset(&l);
    l_str(&l, a->name);
    l_pad_to(&l, SHELL_ALIAS_NAME_MAX + 1);
    l_str(&l, a->value);
    emit(o, &l, SHELL_TEXT);
}

/* "<who>: <text>" or "<who>: <text>'<name>'<rest>"; the name shortened
 * to 40 characters. */
static enum shell_result alias_error(const struct shell_output *o, const char *text,
                                     const char *name, const char *rest)
{
    struct line l;
    l_reset(&l);
    l_str(&l, text);
    if (name) {
        l_char(&l, '\'');
        l_name(&l, name);
        l_char(&l, '\'');
        l_str(&l, rest);
    }
    emit(o, &l, SHELL_ERROR);
    return SHELL_BAD_ARGS;
}

/* "alias": all, by name; "alias ll": one; "alias ll echo hi": define (or
 * replace) ll. The command's first word must be a built-in command, so an
 * alias never names another alias; its words are kept with single spaces. */
static enum shell_result cmd_alias(int argc, char **argv, const struct shell_context *c,
                                   const struct shell_output *o)
{
    struct shell_state *s = c->state;
    struct line l;
    if (argc == 1) {
        for (uint32_t i = 0; i < s->alias_count; i++)
            alias_row(o, &s->aliases[i]);
        l_reset(&l);
        l_uint(&l, s->alias_count, 0, ' ');
        l_str(&l, s->alias_count == 1 ? " alias" : " aliases");
        l_str(&l, " (at most 16)");
        emit(o, &l, SHELL_TEXT);
        return SHELL_OK;
    }
    const char *name = argv[1];
    if (argc == 2) {
        const struct shell_alias *a = alias_find(s, name);
        if (!a)
            return alias_error(o, "alias: no alias named ", name, ".");
        alias_row(o, a);
        return SHELL_OK;
    }

    uint32_t n = str_len(name);
    if (n > SHELL_ALIAS_NAME_MAX) {
        l_reset(&l);
        l_str(&l, "alias: a name has at most 15 characters (this one has ");
        l_uint(&l, n, 0, ' ');
        l_str(&l, ").");
        emit(o, &l, SHELL_ERROR);
        return SHELL_BAD_ARGS;
    }
    for (uint32_t i = 0; i < n; i++)
        if (!((name[i] >= 'a' && name[i] <= 'z') || (name[i] >= '0' && name[i] <= '9') ||
              name[i] == '_'))
            return alias_error(o, "alias: a name uses only a-z, 0-9 and _.", 0, 0);
    if (builtin_name(name))
        return alias_error(o, "alias: ", name, " is a built-in command.");
    if (!find_command(argv[2], c))
        return alias_error(o, "alias: ", argv[2], " is not a built-in command.");

    uint32_t len = (uint32_t)(argc - 3);            /* the spaces between the words */
    for (int i = 2; i < argc; i++)
        len += str_len(argv[i]);
    if (len > SHELL_ALIAS_VALUE_MAX) {
        l_reset(&l);
        l_str(&l, "alias: the command has at most 63 characters (this one has ");
        l_uint(&l, len, 0, ' ');
        l_str(&l, ").");
        emit(o, &l, SHELL_ERROR);
        return SHELL_BAD_ARGS;
    }

    struct shell_alias *a = alias_find(s, name);
    bool replaced = a != 0;
    if (!a) {
        if (s->alias_count >= SHELL_ALIAS_MAX)
            return alias_error(o, "alias: the table is full (16 aliases); remove one with unalias.",
                               0, 0);
        uint32_t at = 0;
        while (at < s->alias_count && str_cmp(s->aliases[at].name, name) < 0)
            at++;
        for (uint32_t i = s->alias_count; i > at; i--)
            s->aliases[i] = s->aliases[i - 1];
        s->alias_count++;
        a = &s->aliases[at];
        for (uint32_t i = 0; i <= n; i++)
            a->name[i] = name[i];
    }
    uint32_t k = 0;
    for (int i = 2; i < argc; i++) {
        if (i > 2)
            a->value[k++] = ' ';
        for (const char *p = argv[i]; *p; p++)
            a->value[k++] = *p;
    }
    a->value[k] = '\0';
    l_reset(&l);
    l_str(&l, "Alias '");
    l_str(&l, name);
    l_str(&l, replaced ? "' replaced." : "' added.");
    emit(o, &l, SHELL_TEXT);
    return SHELL_OK;
}

static enum shell_result cmd_unalias(int argc, char **argv, const struct shell_context *c,
                                     const struct shell_output *o)
{
    struct shell_state *s = c->state;
    struct line l;
    if (argc != 2)
        return usage_error(o, "unalias <name>");
    struct shell_alias *a = alias_find(s, argv[1]);
    if (!a)
        return alias_error(o, "unalias: no alias named ", argv[1], ".");
    uint32_t at = (uint32_t)(a - s->aliases);
    for (uint32_t i = at; i + 1 < s->alias_count; i++)
        s->aliases[i] = s->aliases[i + 1];
    s->alias_count--;
    l_reset(&l);
    l_str(&l, "Alias '");
    l_str(&l, argv[1]);
    l_str(&l, "' removed.");
    emit(o, &l, SHELL_TEXT);
    return SHELL_OK;
}

/* ---- 0.12 stage 2: variables (only when the context has a session) ---- */

void shell_session_init(struct shell_session *s)
{
    s->var_count = 0;
    s->sleep_seconds = 0;
    s->timed = false;
}

/* [A-Za-z_][A-Za-z0-9_]{0,15}: case-sensitive. */
static bool var_name_ok(const char *name)
{
    uint32_t n = str_len(name);
    if (n == 0 || n > SHELL_VAR_NAME_MAX)
        return false;
    for (uint32_t i = 0; i < n; i++) {
        char ch = name[i];
        bool letter = (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || ch == '_';
        if (!letter && (i == 0 || ch < '0' || ch > '9'))
            return false;
    }
    return true;
}

static struct shell_var *var_find(struct shell_session *s, const char *name)
{
    for (uint32_t i = 0; i < s->var_count; i++)
        if (str_eq(s->vars[i].name, name))
            return &s->vars[i];
    return 0;
}

/* "NAME=value". From the console a row is at most 67 characters: the typed
 * line holds "set " and the row's text. */
static void var_row(const struct shell_output *o, const struct shell_var *v)
{
    struct line l;
    l_reset(&l);
    l_str(&l, v->name);
    l_char(&l, '=');
    l_str(&l, v->value);
    emit(o, &l, SHELL_TEXT);
}

/* "<who>: NAME is not set", the name shortened to 40 characters. */
static enum shell_result var_not_set(const struct shell_output *o, const char *who,
                                     const char *name)
{
    struct line l;
    l_reset(&l);
    l_str(&l, who);
    l_str(&l, ": ");
    l_name(&l, name);
    l_str(&l, " is not set");
    emit(o, &l, SHELL_ERROR);
    return SHELL_BAD_ARGS;
}

/* "set": all, by name; "set NAME": one; "set NAME=value words": define or
 * change NAME. The name ends at the first '='; the value is the rest of
 * that word and the words after it, joined with single spaces ("set A="
 * gives an empty value). A changed variable keeps its export mark. */
static enum shell_result cmd_set(int argc, char **argv, const struct shell_context *c,
                                 const struct shell_output *o)
{
    struct shell_session *s = c->session;
    struct line l;
    if (argc == 1) {
        if (s->var_count == 0)
            say(o, "No variables.", SHELL_TEXT);
        for (uint32_t i = 0; i < s->var_count; i++)
            var_row(o, &s->vars[i]);
        return SHELL_OK;
    }
    char *name = argv[1];
    char *eq = name;
    while (*eq && *eq != '=')
        eq++;
    if (!*eq) {
        if (argc > 2)
            return usage_error(o, session_commands[0].usage);
        const struct shell_var *v = var_find(s, name);
        if (!v)
            return var_not_set(o, "set", name);
        var_row(o, v);
        return SHELL_OK;
    }
    *eq = '\0';
    const char *first = eq + 1;
    if (!var_name_ok(name)) {
        l_reset(&l);
        l_str(&l, "set: invalid name: ");
        if (*name)
            l_name(&l, name);
        else
            l_str(&l, "(empty)");
        emit(o, &l, SHELL_ERROR);
        return SHELL_BAD_ARGS;
    }
    uint32_t len = str_len(first), words = *first ? 1 : 0;
    for (int i = 2; i < argc; i++) {
        len += (words ? 1 : 0) + str_len(argv[i]);
        words++;
    }
    if (len > SHELL_VAR_VALUE_MAX) {
        say(o, "set: value longer than 63 characters", SHELL_ERROR);
        return SHELL_BAD_ARGS;
    }
    struct shell_var *v = var_find(s, name);
    if (!v) {
        if (s->var_count >= SHELL_VAR_MAX) {
            say(o, "set: no room (32 variables)", SHELL_ERROR);
            return SHELL_BAD_ARGS;
        }
        uint32_t at = 0;
        while (at < s->var_count && str_cmp(s->vars[at].name, name) < 0)
            at++;
        for (uint32_t i = s->var_count; i > at; i--)
            s->vars[i] = s->vars[i - 1];
        s->var_count++;
        v = &s->vars[at];
        uint32_t k = 0;
        for (; name[k]; k++)
            v->name[k] = name[k];
        v->name[k] = '\0';
        v->exported = false;
    }
    uint32_t k = 0;
    for (const char *p = first; *p; p++)
        v->value[k++] = *p;
    for (int i = 2; i < argc; i++) {
        if (k)
            v->value[k++] = ' ';
        for (const char *p = argv[i]; *p; p++)
            v->value[k++] = *p;
    }
    v->value[k] = '\0';
    return SHELL_OK;
}

/* "unset NAME": removes it, with its export mark. */
static enum shell_result cmd_unset(int argc, char **argv, const struct shell_context *c,
                                   const struct shell_output *o)
{
    struct shell_session *s = c->session;
    if (argc != 2)
        return usage_error(o, "unset <NAME>");
    struct shell_var *v = var_find(s, argv[1]);
    if (!v)
        return var_not_set(o, "unset", argv[1]);
    for (uint32_t i = (uint32_t)(v - s->vars); i + 1 < s->var_count; i++)
        s->vars[i] = s->vars[i + 1];
    s->var_count--;
    return SHELL_OK;
}

/* "export NAME": marks a set variable, so "env" lists it. No environment
 * reaches a program yet (0.12 plan: the program start-up layout comes
 * later); "export NAME=value" is refused. */
static enum shell_result cmd_export(int argc, char **argv, const struct shell_context *c,
                                    const struct shell_output *o)
{
    struct shell_session *s = c->session;
    if (argc != 2)
        return usage_error(o, "export <NAME>");
    for (const char *p = argv[1]; *p; p++) {
        if (*p == '=') {
            say(o, "export: NAME=value is not supported; use set NAME=value, then export NAME.",
                SHELL_ERROR);
            return SHELL_BAD_ARGS;
        }
    }
    struct shell_var *v = var_find(s, argv[1]);
    if (!v)
        return var_not_set(o, "export", argv[1]);
    v->exported = true;
    return SHELL_OK;
}

/* "env": the exported variables, by name. */
static enum shell_result cmd_env(int argc, char **argv, const struct shell_context *c,
                                 const struct shell_output *o)
{
    struct shell_session *s = c->session;
    uint32_t shown = 0;
    (void)argc; (void)argv;
    for (uint32_t i = 0; i < s->var_count; i++) {
        if (s->vars[i].exported) {
            var_row(o, &s->vars[i]);
            shown++;
        }
    }
    if (shown == 0)
        say(o, "No exported variables.", SHELL_TEXT);
    return SHELL_OK;
}

/* ---- 0.12 stage 2: sleep, presskey, time (the console does the waiting) ---- */

/* "sleep 5": whole seconds, 1-3600; the console waits (SHELL_SLEEP). */
static enum shell_result cmd_sleep(int argc, char **argv, const struct shell_context *c,
                                   const struct shell_output *o)
{
    uint32_t n = 0;
    if (argc != 2)
        return usage_error(o, "sleep <seconds>");
    const char *p = argv[1];
    bool ok = *p != '\0';
    for (; *p && ok; p++) {
        ok = *p >= '0' && *p <= '9';
        n = n * 10 + (uint32_t)(*p - '0');
        if (n > SHELL_SLEEP_MAX)
            ok = false;
    }
    if (!ok || n == 0) {
        say(o, "sleep: give whole seconds from 1 to 3600", SHELL_ERROR);
        return SHELL_BAD_ARGS;
    }
    if (!c->timer_ok) {
        say(o, "sleep: unavailable (timer not running)", SHELL_ERROR);
        return SHELL_BAD_ARGS;
    }
    c->session->sleep_seconds = n;
    return SHELL_SLEEP;
}

/* "presskey": the text, then the console waits for one key (SHELL_PRESSKEY). */
static enum shell_result cmd_presskey(int argc, char **argv, const struct shell_context *c,
                                      const struct shell_output *o)
{
    (void)argc; (void)argv; (void)c;
    say(o, SHELL_PRESSKEY_TEXT, SHELL_TEXT);
    return SHELL_PRESSKEY;
}

/* "time" alone; "time <command>" itself is handled in shell_execute. */
static enum shell_result cmd_time(int argc, char **argv, const struct shell_context *c,
                                  const struct shell_output *o)
{
    (void)argc; (void)argv; (void)c;
    return usage_error(o, TIME_COMMAND->usage);
}

/* "time <command>": the command must be a built-in (an alias is not
 * expanded) and not "time". On success the command takes the place of the
 * line and session->timed is set. */
static bool time_prepare(int *argc, char **argv, const struct shell_context *c,
                         const struct shell_output *o)
{
    const char *name = argv[1];
    if (str_eq(name, TIME_COMMAND->name)) {
        say(o, "time: cannot time time", SHELL_ERROR);
        return false;
    }
    if (!find_command(name, c)) {
        if (c->state && alias_find(c->state, name))
            alias_error(o, "time: ", name, " is an alias; time runs built-in commands only.");
        else
            alias_error(o, "time: ", name, " is not a built-in command.");
        return false;
    }
    for (int i = 0; i + 1 < *argc; i++)
        argv[i] = argv[i + 1];
    (*argc)--;
    c->session->timed = true;
    return true;
}

void shell_time_line(char *out, uint64_t ticks, uint32_t hz)
{
    struct line l;
    l_reset(&l);
    l_str(&l, "time: ");
    if (ticks <= 0xFFFFFFFFull) {
        uint32_t t = (uint32_t)ticks;       /* 32-bit division only */
        l_uint(&l, t, 0, ' ');
        l_str(&l, t == 1 ? " tick" : " ticks");
        if (hz) {
            l_str(&l, " (");
            l_uint(&l, t / hz, 0, ' ');
            l_char(&l, '.');
            l_uint(&l, (t % hz) * 100 / hz, 2, '0');
            l_str(&l, " s)");
        }
    } else {
        l_str(&l, "more than 4294967295 ticks");
    }
    uint32_t k = 0;
    for (; k + 1 < SHELL_TIME_LINE_MAX && l.s[k]; k++)
        out[k] = l.s[k];
    out[k] = '\0';
}

/* 0.12: an alias's value replaces the first word, and the new line is split
 * again (at most 8 words); `buf` holds it. Returns false (and says why)
 * when it does not fit. Lines from the console always fit. */
#define EXPANDED_MAX (SHELL_ALIAS_VALUE_MAX + 1 + LINE_CAP)

static bool expand_alias(const struct shell_alias *a, int *argc, char **argv, char *buf,
                         const struct shell_output *o)
{
    uint32_t n = 0;
    for (const char *p = a->value; *p; p++)
        buf[n++] = *p;
    for (int i = 1; i < *argc; i++) {
        uint32_t k = str_len(argv[i]);
        if (n + 1 + k >= EXPANDED_MAX) {
            say(o, "The alias makes the line too long.", SHELL_ERROR);
            return false;
        }
        buf[n++] = ' ';
        for (uint32_t j = 0; j < k; j++)
            buf[n++] = argv[i][j];
    }
    buf[n] = '\0';
    if (shell_parse(buf, argc, argv) != PARSE_OK) {
        say(o, "Too many arguments (max 8).", SHELL_ERROR);
        return false;
    }
    return true;
}

enum shell_result shell_execute(char *line, const struct shell_context *c,
                                const struct shell_output *o)
{
    char *argv[SHELL_MAX_ARGS];
    int argc;

    if (c->session) {                       /* 0.12 stage 2: nothing pending */
        c->session->sleep_seconds = 0;
        c->session->timed = false;
    }
    switch (shell_parse(line, &argc, argv)) {
    case PARSE_EMPTY:
        return SHELL_EMPTY;
    case PARSE_TOO_MANY:
        say(o, "Too many arguments (max 8).", SHELL_ERROR);
        return SHELL_TOO_MANY;
    case PARSE_OK:
        break;
    }

    const struct command *cmd = find_command(argv[0], c);
    char expanded[EXPANDED_MAX];
    if (!cmd && c->state) {                 /* 0.12: an alias, one level */
        const struct shell_alias *a = alias_find(c->state, argv[0]);
        if (a) {
            if (!expand_alias(a, &argc, argv, expanded, o))
                return SHELL_TOO_MANY;
            cmd = find_command(argv[0], c);
        }
    }
    /* 0.12 stage 2: "time <command>" runs the command as if typed alone
     * ("time --help" is time's own help). */
    if (cmd == TIME_COMMAND && argc >= 2 && !(argc == 2 && str_eq(argv[1], "--help"))) {
        if (!time_prepare(&argc, argv, c, o))
            return SHELL_BAD_ARGS;
        cmd = find_command(argv[0], c);
    }
    if (cmd && c->extras && argc == 2) {
        /* 0.9.1: "<command> --help" for every command, "help <command>". */
        if (str_eq(argv[1], "--help")) {
            help_topic(o, cmd, c);
            return SHELL_OK;
        }
        if (cmd == &commands[0]) {
            if (c->state)                   /* 0.12: a section first */
                for (int i = 0; i < SECTION_COUNT; i++)
                    if (str_eq(argv[1], section_names[i])) {
                        help_section(o, c, i);
                        return SHELL_OK;
                    }
            const struct command *topic = find_command(argv[1], c);
            if (!topic)
                return help_unknown(o, argv[1], c);
            help_topic(o, topic, c);
            return SHELL_OK;
        }
    }
    if (cmd) {
        if (argc - 1 > cmd->max_args) {
            struct line l;
            l_reset(&l);
            l_str(&l, "Usage: ");
            l_str(&l, cmd == &commands[0] && c->extras ? help_usage(c) : cmd->usage);
            emit(o, &l, SHELL_ERROR);
            return SHELL_BAD_ARGS;
        }
        return cmd->run(argc, argv, c, o);
    }

    struct line l;
    l_reset(&l);
    l_str(&l, "Unknown command: ");
    l_str(&l, argv[0]);
    l_str(&l, ". Type 'help' for a list of commands.");
    emit(o, &l, SHELL_ERROR);
    if (c->state) {                         /* 0.12 */
        struct suggestions s = { { 0 }, { 0 }, 0 };
        suggest_commands(&s, argv[0], c, true);
        say_suggestions(o, &s);
    }
    return SHELL_UNKNOWN;
}
