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
};

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
 * refused here. */
static enum shell_result cmd_kill(int argc, char **argv, const struct shell_context *c,
                                  const struct shell_output *o)
{
    struct line l;
    uint64_t pid = 0;
    if (argc != 2)
        return usage_error(o, "kill <pid>");
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
    { "help",    "help",          "Show this list",             0, cmd_help },
    { "clear",   "clear",         "Clear the screen",           0, cmd_clear },
    { "version", "version",       "Show the Madire OS version", 0, cmd_version },
    { "uptime",  "uptime",        "Show time since boot",       0, cmd_uptime },
    { "mem",     "mem",           "Show memory information",    0, cmd_mem },
    { "cpu",     "cpu",           "Show processor information", 0, cmd_cpu },
    { "echo",    "echo <text>",   "Print text: echo <text>",    SHELL_MAX_ARGS - 1, cmd_echo },
};

#define COMMAND_COUNT (sizeof(commands) / sizeof(commands[0]))

/* 0.9: offered only when the context has threads (see shell.h). */
static const struct command ps_command =
    { "ps",      "ps",            "Show threads",               0, cmd_ps };

/* 0.9.1: offered only when the context has extras (see shell.h). */
static const struct command extra_commands[] = {
    { "history",  "history",       "Show previous commands (Up/Down recall)", 0, cmd_history },
    { "uname",    "uname [-a]",    "Show the system name: uname [-a]",        1, cmd_uname },
    { "hostname", "hostname",      "Show the computer's name",                0, cmd_hostname },
    { "date",     "date",          "Show the date and time (RTC)",            0, cmd_date },
    { "dmesg",    "dmesg [lines]", "Show kernel messages: dmesg [lines]",     1, cmd_dmesg },
    { "reboot",   "reboot",        "Restart the computer (asks first)",       0, cmd_reboot },
};

#define EXTRA_COUNT (sizeof(extra_commands) / sizeof(extra_commands[0]))

/* 0.11: offered only when the context has procs (see shell.h); "ps" then
 * also shows the processes. */
static const struct command ps_procs_command =
    { "ps",      "ps",            "Show threads and processes", 0, cmd_ps };
static const struct command run_command =
    { "run",     RUN_USAGE,       "Run a program: run [-b] <name> [n]", 3, cmd_run };
static const struct command kill_command =
    { "kill",    "kill <pid>",    "End a process: kill <pid> (asks first)", 1, cmd_kill };

static const struct command *ps_of(const struct shell_context *c)
{
    return c->procs ? &ps_procs_command : &ps_command;
}

static const struct command *find_command(const char *name, const struct shell_context *c)
{
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
        return &kill_command;
    return 0;
}

/* "help mem" / "mem --help": the purpose, then the usage. "help" itself
 * takes an optional command name when the context has extras. */
static void help_topic(const struct shell_output *o, const struct command *cmd)
{
    struct line l;
    l_reset(&l);
    l_str(&l, cmd->name);
    l_str(&l, ": ");
    l_str(&l, cmd->help);
    emit(o, &l, SHELL_TEXT);
    l_str(&l, "Usage: ");
    l_str(&l, cmd == &commands[0] ? "help [command]" : cmd->usage);
    emit(o, &l, SHELL_TEXT);
}

#define HELP_NAME_SHOWN 40      /* longer names are shortened, so the line fits */

static enum shell_result help_unknown(const struct shell_output *o, const char *name)
{
    struct line l;
    uint32_t n = 0;
    while (name[n])
        n++;
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
    (void)argc; (void)argv;
    say(o, "Available commands:", SHELL_TEXT);
    for (uint32_t i = 0; i < COMMAND_COUNT; i++)
        help_row(o, &commands[i]);
    if (c->threads)
        help_row(o, ps_of(c));
    if (c->extras)
        for (uint32_t i = 0; i < EXTRA_COUNT; i++)
            help_row(o, &extra_commands[i]);
    if (c->procs) {
        help_row(o, &run_command);
        help_row(o, &kill_command);
    }
    return SHELL_OK;
}

enum shell_result shell_execute(char *line, const struct shell_context *c,
                                const struct shell_output *o)
{
    char *argv[SHELL_MAX_ARGS];
    int argc;

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
    if (cmd && c->extras && argc == 2) {
        /* 0.9.1: "<command> --help" for every command, "help <command>". */
        if (str_eq(argv[1], "--help")) {
            help_topic(o, cmd);
            return SHELL_OK;
        }
        if (cmd == &commands[0]) {
            const struct command *topic = find_command(argv[1], c);
            if (!topic)
                return help_unknown(o, argv[1]);
            help_topic(o, topic);
            return SHELL_OK;
        }
    }
    if (cmd) {
        if (argc - 1 > cmd->max_args) {
            struct line l;
            l_reset(&l);
            l_str(&l, "Usage: ");
            l_str(&l, cmd == &commands[0] && c->extras ? "help [command]" : cmd->usage);
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
    return SHELL_UNKNOWN;
}
