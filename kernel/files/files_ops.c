/*
 * The Files app's actions, their pure part (1.5 stage 13); see files_ops.h.
 */
#include "kernel/files/files_ops.h"
#include "kernel/files/files_core.h"

static const char fo_trash[] = TRASH_DIR;
static const char fo_home[] = TRASH_HOME;      /* "/home/user/" */

/* The first n bytes of a and b are equal. */
static bool fo_prefix(const char *a, const char *b, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++)
        if (a[i] != b[i])
            return false;
    return true;
}

static uint32_t fo_len(const char *s)
{
    uint32_t n = 0;
    while (s[n])
        n++;
    return n;
}

bool files_ops_trashable(const char *path, uint32_t len)
{
    uint32_t hl = sizeof(fo_home) - 1u, tl = sizeof(fo_trash) - 1u;
    if (len <= hl || len > FILES_PATH_MAX || !fo_prefix(path, fo_home, hl))
        return false;
    /* The Trash folder itself, or anything in it, never. */
    if (len >= tl && fo_prefix(path, fo_trash, tl) && (len == tl || path[tl] == '/'))
        return false;
    /* Nor a parent of it (.local, .local/share): it holds the Trash. */
    if (len < tl && fo_prefix(fo_trash, path, len) && fo_trash[len] == '/')
        return false;
    return true;
}

bool files_ops_into_itself(const char *src, uint32_t src_len, const char *dst, uint32_t dst_len)
{
    if (src_len == 0 || dst_len < src_len || !fo_prefix(src, dst, src_len))
        return false;
    return dst_len == src_len || dst[src_len] == '/' || (src_len == 1 && src[0] == '/');
}

uint32_t files_ops_parent_len(const char *path, uint32_t len)
{
    if (len <= 1)
        return 0;
    uint32_t k = len;
    while (k > 0 && path[k - 1] != '/')
        k--;
    if (k == 0)
        return 0;
    return k == 1 ? 1u : k - 1u;
}

uint32_t files_ops_name_off(const char *path, uint32_t len)
{
    uint32_t k = len;
    while (k > 0 && path[k - 1] != '/')
        k--;
    return k;
}

uint32_t files_ops_location(const char *folder, uint32_t len, uint32_t cells, char *out, uint32_t size)
{
    if (size == 0)
        return 0;
    struct files_pathbar pb;
    uint32_t room = cells < size - 1u ? cells : size - 1u;
    files_pathbar(FILES_VIEW_FOLDER, folder, len, room, &pb);
    uint32_t used = pb.cells < room ? pb.cells : room;
    for (uint32_t i = 0; i < used; i++)
        out[i] = ' ';
    if (pb.clipped)
        for (uint32_t i = 0; i < 3 && i < used; i++)
            out[i] = '.';
    for (uint32_t k = 0; k < pb.n && k < FILES_PB_MAX; k++) {
        const struct files_pb_part *q = &pb.part[k];
        if ((k > 0 || (pb.clipped && q->x >= 6)) && q->x >= 3) {
            static const char sep[3] = { ' ', '>', ' ' };
            for (uint32_t i = 0; i < 3; i++)
                if (q->x - 3u + i < used)
                    out[q->x - 3u + i] = sep[i];
        }
        for (uint32_t i = 0; i < q->len; i++) {
            uint32_t at = (uint32_t)q->x + i;
            if (at >= used)
                break;
            char ch = q->text[i];
            out[at] = ch >= 0x20 && ch <= 0x7E ? ch : '?';
        }
    }
    out[used] = '\0';
    return used;
}

void files_ops_date(int64_t seconds, struct trash_date *d)
{
    if (seconds < 0)
        seconds = 0;
    if (seconds > FILES_DATE_MAX)
        seconds = FILES_DATE_MAX;
    int64_t days = seconds / 86400, rest = seconds % 86400;
    /* The civil date of a day count from 1970-01-01 (days >= 0). */
    int64_t z = days + 719468, era = z / 146097, doe = z - era * 146097;
    int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100), mp = (5 * doy + 2) / 153;
    int64_t day = doy - (153 * mp + 2) / 5 + 1, month = mp < 10 ? mp + 3 : mp - 9;
    int64_t year = yoe + era * 400 + (month <= 2 ? 1 : 0);
    d->year = (uint16_t)year;
    d->month = (uint8_t)month;
    d->day = (uint8_t)day;
    d->hour = (uint8_t)(rest / 3600);
    d->minute = (uint8_t)(rest / 60 % 60);
    d->second = (uint8_t)(rest % 60);
}

/* ---- the questions ---- */

struct fo_out {
    char *buf;
    uint32_t size, at;
};

static void fo_put(struct fo_out *o, const char *s)
{
    for (uint32_t i = 0; s[i] && o->at + 1u < o->size; i++)
        o->buf[o->at++] = s[i];
    o->buf[o->at] = '\0';
}

static void fo_put_u(struct fo_out *o, uint32_t v)
{
    char digits[12];
    uint32_t nd = 0;
    do {
        digits[nd++] = (char)('0' + v % 10u);
        v /= 10u;
    } while (v && nd < sizeof(digits));
    char s[13];
    uint32_t k = 0;
    while (nd)
        s[k++] = digits[--nd];
    s[k] = '\0';
    fo_put(o, s);
}

/* A name as a question shows it: '?' for each byte outside printable
 * ASCII, cut with "..." past FILES_OPS_NAME_SHOWN bytes. */
static void fo_put_name(struct fo_out *o, const char *name)
{
    char s[FILES_OPS_NAME_SHOWN + 1];
    uint32_t n = name ? fo_len(name) : 0, keep = n > FILES_OPS_NAME_SHOWN ? FILES_OPS_NAME_SHOWN - 3u : n;
    uint32_t k = 0;
    for (; k < keep; k++)
        s[k] = name[k] >= 0x20 && name[k] <= 0x7E ? name[k] : '?';
    if (keep < n)
        for (uint32_t i = 0; i < 3; i++)
            s[k++] = '.';
    s[k] = '\0';
    fo_put(o, s);
}

void files_ops_question(enum files_question q, const char *name, uint32_t n, const char *folder, char *line1,
                        char *line2, char *label)
{
    struct fo_out a = { line1, FILES_OPS_LINE_MAX, 0 }, b = { line2, FILES_OPS_LINE_MAX, 0 };
    struct fo_out c = { label, FILES_OPS_LABEL_MAX, 0 };
    line1[0] = line2[0] = label[0] = '\0';
    switch (q) {
    case FILES_Q_DELETE_ONE:
    case FILES_Q_NOTRASH_ONE:
        fo_put(&a, "Delete '");
        fo_put_name(&a, name);
        fo_put(&a, "' permanently?");
        if (q == FILES_Q_NOTRASH_ONE)
            fo_put(&b, "It can't go to the Trash.");
        fo_put(&c, "Delete");
        break;
    case FILES_Q_DELETE_MANY:
    case FILES_Q_NOTRASH_MANY:
        fo_put(&a, "Delete ");
        fo_put_u(&a, n);
        fo_put(&a, " items permanently?");
        if (q == FILES_Q_NOTRASH_MANY)
            fo_put(&b, "They can't go to the Trash.");
        fo_put(&c, "Delete");
        break;
    case FILES_Q_EMPTY:
        fo_put(&a, "Empty the Trash?");
        fo_put_u(&b, n);
        fo_put(&b, n == 1 ? " item will be deleted permanently." : " items will be deleted permanently.");
        fo_put(&c, "Empty Trash");
        break;
    case FILES_Q_EXISTS:
        fo_put(&a, "'");
        fo_put_name(&a, name);
        fo_put(&a, "' already exists in ");
        fo_put(&a, folder ? folder : "");
        fo_put(&a, ".");
        fo_put(&c, "Keep both");
        break;
    }
}

void files_ops_deep_text(uint32_t done, char *out)
{
    struct fo_out o = { out, FILES_OPS_LINE_MAX, 0 };
    out[0] = '\0';
    fo_put(&o, "This folder is nested too deeply (more than 64 levels) (");
    fo_put_u(&o, done);
    fo_put(&o, done == 1 ? " item done)." : " items done).");
}

/* ---- the menus' greyed rows ---- */

uint8_t files_ops_greyed(enum files_ops_menu menu, const struct files_ops_state *st)
{
    bool one = st->selected == 1, any = st->selected > 0, w = st->writable;
    uint8_t g = 0;
    switch (menu) {
    case FILES_OPS_MENU_ITEM:   /* Open, Cut, Copy, Paste, Rename, Move to Trash, Properties */
        g |= one ? 0u : 1u << 0;
        g |= any && w ? 0u : 1u << 1;
        g |= any ? 0u : 1u << 2;
        g |= st->clip && st->paste_writable ? 0u : 1u << 3;
        g |= one && w ? 0u : 1u << 4;
        g |= any && w ? 0u : 1u << 5;
        g |= one ? 0u : 1u << 6;
        break;
    case FILES_OPS_MENU_SPACE:  /* New Folder, New File, Paste, Show hidden files */
        g |= w ? 0u : (1u << 0) | (1u << 1);
        g |= st->clip && w ? 0u : 1u << 2;
        break;
    case FILES_OPS_MENU_TRASH_ITEM:     /* Restore, Delete permanently, Properties */
        g |= st->restorable > 0 && w ? 0u : 1u << 0;
        g |= any && w ? 0u : 1u << 1;
        g |= one ? 0u : 1u << 2;
        break;
    case FILES_OPS_MENU_TRASH_SPACE:    /* Restore all, Empty Trash, Show hidden files */
        g |= st->in_trash > 0 && w ? 0u : (1u << 0) | (1u << 1);
        break;
    }
    return g;
}
