/*
 * Command shell core (see shell.h).
 *
 * Output lines are built in a small fixed buffer with local helpers (the
 * kernel's ksnprintf is not usable here: this module must stay pure).
 * Only 32-bit division is used; the 64-bit tick count is never divided.
 */
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
    l_str(&l, " (32-bit x86, Multiboot 1)");
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
    text_row(o, "Paging:", "enabled (4 KiB pages, identity-mapped kernel)");
    mem_row(o, "Mapped pages:", p->mapped_pages, "");
    mem_row(o, "Page tables:", p->page_tables, "");
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
    l_str(&l, (c->cr0 & 1u) ? "32-bit protected mode" : "real mode");
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

static enum shell_result cmd_help(int argc, char **argv, const struct shell_context *c,
                                  const struct shell_output *o)
{
    struct line l;
    (void)argc; (void)argv; (void)c;
    say(o, "Available commands:", SHELL_TEXT);
    for (uint32_t i = 0; i < COMMAND_COUNT; i++) {
        l_reset(&l);
        l_str(&l, "  ");
        l_str(&l, commands[i].name);
        l_pad_to(&l, 11);
        l_str(&l, commands[i].help);
        emit(o, &l, SHELL_TEXT);
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

    for (uint32_t i = 0; i < COMMAND_COUNT; i++) {
        if (!str_eq(argv[0], commands[i].name))
            continue;
        if (argc - 1 > commands[i].max_args) {
            struct line l;
            l_reset(&l);
            l_str(&l, "Usage: ");
            l_str(&l, commands[i].usage);
            emit(o, &l, SHELL_ERROR);
            return SHELL_BAD_ARGS;
        }
        return commands[i].run(argc, argv, c, o);
    }

    struct line l;
    l_reset(&l);
    l_str(&l, "Unknown command: ");
    l_str(&l, argv[0]);
    l_str(&l, ". Type 'help' for a list of commands.");
    emit(o, &l, SHELL_ERROR);
    return SHELL_UNKNOWN;
}
