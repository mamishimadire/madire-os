/* Command history (see history.h). */
#include "kernel/console/history.h"

static char *slot(struct history *h, uint32_t number)
{
    return h->lines[(number - 1) % HISTORY_MAX];
}

void history_init(struct history *h)
{
    for (uint32_t i = 0; i < HISTORY_MAX; i++)
        h->lines[i][0] = '\0';
    h->count = 0;
    h->browse = 0;
}

static bool blank(const char *s)
{
    for (; *s; s++)
        if (*s != ' ' && *s != '\t')
            return false;
    return true;
}

bool history_add(struct history *h, const char *line)
{
    h->browse = 0;
    if (!line || blank(line))
        return false;
    h->count++;
    char *dst = slot(h, h->count);
    uint32_t n = 0;
    while (line[n] && n < HISTORY_LINE_MAX) {
        dst[n] = line[n];
        n++;
    }
    dst[n] = '\0';
    return true;
}

uint32_t history_size(const struct history *h)
{
    return h->count < HISTORY_MAX ? h->count : HISTORY_MAX;
}

uint32_t history_first(const struct history *h)
{
    return h->count ? h->count - history_size(h) + 1 : 0;
}

const char *history_get(const struct history *h, uint32_t number)
{
    if (number == 0 || number > h->count || number < history_first(h))
        return 0;
    return h->lines[(number - 1) % HISTORY_MAX];
}

const char *history_up(struct history *h)
{
    if (h->count == 0)
        return 0;
    if (h->browse == 0) {
        h->browse = h->count;
    } else {
        if (h->browse <= history_first(h))
            return 0;               /* already at the oldest: stay */
        h->browse--;
    }
    return slot(h, h->browse);
}

const char *history_down(struct history *h)
{
    if (h->browse == 0)
        return 0;
    if (h->browse >= h->count) {
        h->browse = 0;              /* past the newest: back to an empty line */
        return "";
    }
    h->browse++;
    return slot(h, h->browse);
}

void history_end_browse(struct history *h)
{
    h->browse = 0;
}
