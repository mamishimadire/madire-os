/* Program modules: names and the table (see progmod_core.h). */
#include "kernel/proc/progmod_core.h"

#define CMDLINE_MAX 4096u               /* longest command line looked at */

static bool is_space(char c)
{
    return c == ' ' || c == '\t';
}

static bool name_char(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
}

/* Copies [s, s + n) into out if it is a valid name. */
static bool take_name(const char *s, uint32_t n, char out[PROGMOD_NAME_MAX])
{
    if (n == 0 || n > PROGMOD_NAME_MAX - 1)
        return false;
    for (uint32_t i = 0; i < n; i++)
        if (!name_char(s[i]))
            return false;
    for (uint32_t i = 0; i < n; i++)
        out[i] = s[i];
    out[n] = '\0';
    return true;
}

enum progmod_result progmod_name(const char *cmdline, char out[PROGMOD_NAME_MAX])
{
    out[0] = '\0';
    if (!cmdline)
        return PROGMOD_ERR_NAME;
    /* Up to three words: the path, an optional name, and anything more
     * (which is refused). */
    const char *word[3] = { 0, 0, 0 };
    uint32_t len[3] = { 0, 0, 0 }, words = 0, i = 0;
    while (i < CMDLINE_MAX && cmdline[i]) {
        if (is_space(cmdline[i])) {
            i++;
            continue;
        }
        if (words == 3)
            break;
        word[words] = cmdline + i;
        while (i < CMDLINE_MAX && cmdline[i] && !is_space(cmdline[i])) {
            len[words]++;
            i++;
        }
        words++;
    }
    if (i == CMDLINE_MAX || words == 0 || words == 3)
        return PROGMOD_ERR_NAME;
    if (words == 2)
        return take_name(word[1], len[1], out) ? PROGMOD_OK : PROGMOD_ERR_NAME;

    /* The path's last part, without ".elf". */
    const char *base = word[0];
    uint32_t n = len[0];
    for (uint32_t k = 0; k < len[0]; k++) {
        if (word[0][k] == '/') {
            base = word[0] + k + 1;
            n = len[0] - k - 1;
        }
    }
    if (n > 4 && base[n - 4] == '.' && base[n - 3] == 'e' && base[n - 2] == 'l' &&
        base[n - 1] == 'f')
        n -= 4;
    return take_name(base, n, out) ? PROGMOD_OK : PROGMOD_ERR_NAME;
}

void progmod_init(struct progmod_table *t)
{
    t->count = 0;
}

static bool same(const char *a, const char *b)
{
    uint32_t i = 0;
    for (; i < PROGMOD_NAME_MAX && a[i] && a[i] == b[i]; i++)
        ;
    return i == PROGMOD_NAME_MAX || a[i] == b[i];
}

const struct progmod *progmod_find(const struct progmod_table *t, const char *name)
{
    if (!name)
        return 0;
    for (uint32_t i = 0; i < t->count; i++)
        if (same(t->e[i].name, name))
            return &t->e[i];
    return 0;
}

enum progmod_result progmod_add(struct progmod_table *t, const char *name, uint32_t module,
                                uint64_t phys, uint64_t size, const struct elf_plan *plan)
{
    char checked[PROGMOD_NAME_MAX];
    uint32_t n = 0;
    while (name && n < PROGMOD_NAME_MAX && name[n])
        n++;
    if (!name || !take_name(name, n, checked))
        return PROGMOD_ERR_NAME;
    if (progmod_find(t, checked))
        return PROGMOD_ERR_DUPLICATE;
    if (t->count >= PROGMOD_MAX)
        return PROGMOD_ERR_FULL;
    struct progmod *m = &t->e[t->count++];
    for (uint32_t i = 0; i < PROGMOD_NAME_MAX; i++)
        m->name[i] = checked[i];
    m->module = module;
    m->phys = phys;
    m->size = size;
    m->plan = *plan;
    return PROGMOD_OK;
}

void progmod_show(const char *cmdline, char out[PROGMOD_SHOW_BUF])
{
    uint32_t i = 0;
    for (; cmdline && cmdline[i] && i < PROGMOD_SHOW_MAX; i++)
        out[i] = (cmdline[i] >= 0x20 && cmdline[i] <= 0x7E) ? cmdline[i] : '?';
    if (cmdline && cmdline[i] && i == PROGMOD_SHOW_MAX) {
        out[i++] = '.';
        out[i++] = '.';
        out[i++] = '.';
    }
    out[i] = '\0';
}

const char *progmod_result_str(enum progmod_result r)
{
    switch (r) {
    case PROGMOD_OK:            return "OK";
    case PROGMOD_ERR_NAME:      return "bad name";
    case PROGMOD_ERR_DUPLICATE: return "duplicate name";
    case PROGMOD_ERR_FULL:      return "more than 8 program modules";
    }
    return "unknown result";
}
