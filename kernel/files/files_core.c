/* The Files app's pure part (1.5 stage 11); the rules are in files_core.h. */

#include "kernel/files/files_core.h"

_Static_assert(sizeof(struct files_entry) == 24, "struct files_entry is 24 bytes");

static uint32_t fc_strnlen(const char *s, uint32_t max)
{
    uint32_t n = 0;
    while (n < max && s[n])
        n++;
    return n;
}

static void fc_copy(char *d, const char *s, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++)
        d[i] = s[i];
}

static bool fc_same(const char *a, const char *b, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++)
        if (a[i] != b[i])
            return false;
    return true;
}

static uint8_t fc_lower(uint8_t c)
{
    return (c >= 'A' && c <= 'Z') ? (uint8_t)(c + 32) : c;
}

/* v in decimal at out; the digits written. */
static uint32_t fc_utoa(uint64_t v, char *out)
{
    char t[20];
    uint32_t n = 0;
    do {
        t[n++] = (char)('0' + v % 10);
        v /= 10;
    } while (v);
    for (uint32_t i = 0; i < n; i++)
        out[i] = t[n - 1 - i];
    return n;
}

static void fc_num2(char *p, uint32_t v)
{
    p[0] = (char)('0' + v / 10 % 10);
    p[1] = (char)('0' + v % 10);
}

/* ---- The folder ---- */

uint32_t files_folder_bytes(uint32_t cap)
{
    return cap * (uint32_t)sizeof(struct files_entry) + cap * (FILES_NAME_MAX + 1u);
}

void files_folder_init(struct files_folder *f, struct files_entry *e, uint32_t cap, char *names,
                       uint32_t names_size, bool show_hidden)
{
    f->e = e;
    f->cap = cap;
    f->n = 0;
    f->names = names;
    f->names_size = names_size;
    f->names_used = 0;
    f->show_hidden = show_hidden;
    f->truncated = false;
}

enum files_add files_folder_add(struct files_folder *f, const char *name, uint32_t len, enum files_kind kind,
                                uint64_t size, int64_t mtime)
{
    uint8_t flags = 0;
    if ((len == 1 && name[0] == '.') || (len == 2 && name[0] == '.' && name[1] == '.'))
        return FILES_ADD_SKIPPED;
    if (len == 0 || len > FILES_NAME_MAX || (kind != FILES_K_DIR && kind != FILES_K_FILE && kind != FILES_K_LINK))
        return FILES_ADD_BAD;
    for (uint32_t i = 0; i < len; i++) {
        uint8_t c = (uint8_t)name[i];
        if (c == '/' || c == 0)
            return FILES_ADD_BAD;
        if (c < 0x20 || c > 0x7E)
            flags |= FILES_F_ODD;
    }
    if (name[0] == '.') {
        flags |= FILES_F_HIDDEN;
        if (!f->show_hidden)
            return FILES_ADD_SKIPPED;
    }
    if (f->n >= f->cap || f->names_size - f->names_used < len + 1) {
        f->truncated = true;
        return FILES_ADD_FULL;
    }
    struct files_entry *x = &f->e[f->n++];
    x->name_off = f->names_used;
    x->name_len = (uint16_t)len;
    x->kind = (uint8_t)kind;
    x->flags = flags;
    x->size = size;
    x->mtime = mtime;
    fc_copy(f->names + f->names_used, name, len);
    f->names[f->names_used + len] = 0;
    f->names_used += len + 1;
    return FILES_ADD_OK;
}

const char *files_entry_name(const struct files_folder *f, uint32_t i)
{
    return i < f->n && i < f->cap ? f->names + f->e[i].name_off : 0;
}

int files_name_compare(const char *a, uint32_t la, const char *b, uint32_t lb)
{
    uint32_t n = la < lb ? la : lb;
    for (uint32_t i = 0; i < n; i++) {
        uint8_t ca = fc_lower((uint8_t)a[i]), cb = fc_lower((uint8_t)b[i]);
        if (ca != cb)
            return ca < cb ? -1 : 1;
    }
    if (la != lb)
        return la < lb ? -1 : 1;
    for (uint32_t i = 0; i < n; i++)
        if (a[i] != b[i])
            return (uint8_t)a[i] < (uint8_t)b[i] ? -1 : 1;
    return 0;
}

static int fc_entry_cmp(const struct files_folder *f, uint32_t i, uint32_t j)
{
    const struct files_entry *a = &f->e[i], *b = &f->e[j];
    bool da = a->kind == FILES_K_DIR, db = b->kind == FILES_K_DIR;
    if (da != db)
        return da ? -1 : 1;
    return files_name_compare(f->names + a->name_off, a->name_len, f->names + b->name_off, b->name_len);
}

static void fc_swap(struct files_entry *a, struct files_entry *b)
{
    uint32_t off = a->name_off;
    uint16_t len = a->name_len;
    uint8_t kind = a->kind, flags = a->flags;
    uint64_t size = a->size;
    int64_t mtime = a->mtime;
    a->name_off = b->name_off;
    a->name_len = b->name_len;
    a->kind = b->kind;
    a->flags = b->flags;
    a->size = b->size;
    a->mtime = b->mtime;
    b->name_off = off;
    b->name_len = len;
    b->kind = kind;
    b->flags = flags;
    b->size = size;
    b->mtime = mtime;
}

static void fc_sift(struct files_folder *f, uint32_t root, uint32_t end)
{
    for (;;) {
        uint32_t child = 2 * root + 1;
        if (child >= end)
            return;
        if (child + 1 < end && fc_entry_cmp(f, child, child + 1) < 0)
            child++;
        if (fc_entry_cmp(f, root, child) >= 0)
            return;
        fc_swap(&f->e[root], &f->e[child]);
        root = child;
    }
}

void files_folder_sort(struct files_folder *f)
{
    uint32_t n = f->n;
    if (n < 2)
        return;
    for (uint32_t i = n / 2; i > 0; i--)
        fc_sift(f, i - 1, n);
    for (uint32_t end = n - 1; end > 0; end--) {
        fc_swap(&f->e[0], &f->e[end]);
        fc_sift(f, 0, end);
    }
}

/* ---- The column texts ---- */

const char *files_type_text(enum files_kind kind, const char *name, uint32_t len)
{
    static const char exts[] = "txt md log conf cfg ini csv json sh c h";
    if (kind == FILES_K_DIR)
        return "Folder";
    if (kind == FILES_K_LINK)
        return "Link";
    uint32_t d = len;
    while (d > 0 && name[d - 1] != '.')
        d--;
    if (d >= 2) {                       /* the last '.' is not the first byte */
        const char *e = name + d;
        uint32_t el = len - d;
        for (uint32_t i = 0; exts[i];) {
            uint32_t s = i;
            while (exts[i] && exts[i] != ' ')
                i++;
            if (i - s == el) {
                uint32_t k = 0;
                while (k < el && fc_lower((uint8_t)e[k]) == (uint8_t)exts[s + k])
                    k++;
                if (k == el && el > 0)
                    return "Text file";
            }
            if (exts[i])
                i++;
        }
    }
    return "File";
}

void files_size_text(uint64_t bytes, char out[FILES_SIZE_TEXT])
{
    uint32_t n;
    if (bytes < 1024) {
        n = fc_utoa(bytes, out);
        out[n++] = ' ';
        out[n++] = 'B';
        out[n] = 0;
        return;
    }
    uint64_t unit = 1024;
    char u = 'K';
    if (bytes >= (1ull << 30)) {
        unit = 1ull << 30;
        u = 'G';
    } else if (bytes >= (1ull << 20)) {
        unit = 1ull << 20;
        u = 'M';
    }
    n = fc_utoa(bytes / unit, out);
    out[n++] = '.';
    out[n++] = (char)('0' + (bytes % unit) * 10 / unit);
    out[n++] = ' ';
    out[n++] = u;
    out[n++] = 'i';
    out[n++] = 'B';
    out[n] = 0;
}

/* 1.5 stage 14b: appends s at out[*k] (out holds FILES_USAGE_TEXT bytes). */
static void fc_usage_put(char *out, uint32_t *k, const char *s)
{
    while (*s && *k + 1 < FILES_USAGE_TEXT)
        out[(*k)++] = *s++;
    out[*k] = 0;
}

void files_usage_text(uint64_t used, uint64_t total, char out[FILES_USAGE_TEXT])
{
    char part[FILES_SIZE_TEXT];
    uint32_t k = 0;
    out[0] = 0;
    files_size_text(used, part);
    fc_usage_put(out, &k, part);
    fc_usage_put(out, &k, " used, ");
    files_size_text(total > used ? total - used : 0, part);
    fc_usage_put(out, &k, part);
    fc_usage_put(out, &k, " free of ");
    files_size_text(total, part);
    fc_usage_put(out, &k, part);
}

int32_t files_bar_fill(uint64_t used, uint64_t total, int32_t w)
{
    if (w <= 0 || total == 0)
        return 0;
    if (used >= total)
        return w;
    uint64_t uw = (uint64_t)w;
    while (used > UINT64_MAX / uw) {    /* used < total here, so total stays above 0 */
        used >>= 1;
        total >>= 1;
    }
    uint64_t px = used * uw / total;
    return px >= uw ? w - 1 : (int32_t)px;  /* the halving can make them equal: not full */
}

static bool fc_leap(uint32_t y)
{
    return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
}

void files_date_text(int64_t seconds, char out[FILES_DATE_TEXT])
{
    static const uint8_t month_days[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    if (seconds < 0)
        seconds = 0;
    if (seconds > FILES_DATE_MAX)
        seconds = FILES_DATE_MAX;
    uint64_t days = (uint64_t)seconds / 86400;
    uint32_t rest = (uint32_t)((uint64_t)seconds % 86400);
    uint32_t year = 1970 + 400 * (uint32_t)(days / 146097), month = 0;   /* 146,097 days every 400 years */
    days %= 146097;
    while (days >= (fc_leap(year) ? 366u : 365u)) {
        days -= fc_leap(year) ? 366u : 365u;
        year++;
    }
    for (;;) {
        uint32_t md = month_days[month] + (month == 1 && fc_leap(year) ? 1u : 0u);
        if (days < md)
            break;
        days -= md;
        month++;
    }
    fc_num2(out, year / 100);
    fc_num2(out + 2, year % 100);
    out[4] = '-';
    fc_num2(out + 5, month + 1);
    out[7] = '-';
    fc_num2(out + 8, (uint32_t)days + 1);
    out[10] = ' ';
    fc_num2(out + 11, rest / 3600);
    out[13] = ':';
    fc_num2(out + 14, rest / 60 % 60);
    out[16] = 0;
}

/* ---- The history ---- */

static uint32_t fc_phys(const struct files_history *h, uint32_t i)
{
    return (h->first + i) % FILES_HISTORY_MAX;
}

static char *fc_slot(const struct files_history *h, uint32_t i)
{
    return h->store + fc_phys(h, i) * (FILES_PATH_MAX + 1u);
}

void files_history_init(struct files_history *h, char *store)
{
    h->store = store;
    h->first = 0;
    h->count = 0;
    h->pos = 0;
}

bool files_history_go(struct files_history *h, enum files_view view, const char *path, uint32_t len)
{
    if (view == FILES_VIEW_FOLDER && (len == 0 || len > FILES_PATH_MAX))
        return false;
    if (view != FILES_VIEW_FOLDER)
        len = 0;
    if (h->count) {
        uint32_t p = fc_phys(h, h->pos);
        if (h->view[p] == view && h->len[p] == len && fc_same(fc_slot(h, h->pos), path, len))
            return true;
        h->count = h->pos + 1;          /* the forward entries dropped */
        if (h->count == FILES_HISTORY_MAX) {
            h->first = (h->first + 1) % FILES_HISTORY_MAX;  /* the oldest dropped */
            h->count--;
        }
    }
    uint32_t p = fc_phys(h, h->count);
    char *d = fc_slot(h, h->count);
    fc_copy(d, path, len);
    d[len] = 0;
    h->len[p] = (uint16_t)len;
    h->view[p] = (uint8_t)view;
    h->pos = h->count;
    h->count++;
    return true;
}

bool files_history_current(const struct files_history *h, enum files_view *view, const char **path,
                           uint32_t *len)
{
    if (!h->count)
        return false;
    uint32_t p = fc_phys(h, h->pos);
    *view = (enum files_view)h->view[p];
    *path = fc_slot(h, h->pos);
    *len = h->len[p];
    return true;
}

bool files_history_can_back(const struct files_history *h)
{
    return h->count && h->pos > 0;
}

bool files_history_can_forward(const struct files_history *h)
{
    return h->pos + 1 < h->count;
}

bool files_history_can_up(const struct files_history *h)
{
    if (!h->count)
        return false;
    uint32_t p = fc_phys(h, h->pos);
    return h->view[p] == FILES_VIEW_FOLDER && !(h->len[p] == 1 && fc_slot(h, h->pos)[0] == '/');
}

/* Removes entry k; the later ones move down by one. */
static void fc_hist_remove(struct files_history *h, uint32_t k)
{
    for (uint32_t i = k; i + 1 < h->count; i++) {
        uint32_t a = fc_phys(h, i), b = fc_phys(h, i + 1);
        fc_copy(fc_slot(h, i), fc_slot(h, i + 1), h->len[b] + 1u);
        h->len[a] = h->len[b];
        h->view[a] = h->view[b];
    }
    h->count--;
}

static bool fc_hist_exists(const struct files_history *h, uint32_t i, files_exists_fn exists, void *ctx)
{
    return h->view[fc_phys(h, i)] != FILES_VIEW_FOLDER || exists(ctx, fc_slot(h, i));
}

bool files_history_back(struct files_history *h, files_exists_fn exists, void *ctx, uint32_t *removed)
{
    *removed = 0;
    while (h->count && h->pos > 0) {
        h->pos--;
        if (fc_hist_exists(h, h->pos, exists, ctx))
            return true;
        fc_hist_remove(h, h->pos);      /* the old current entry is at pos again */
        (*removed)++;
    }
    return false;
}

bool files_history_forward(struct files_history *h, files_exists_fn exists, void *ctx, uint32_t *removed)
{
    *removed = 0;
    while (h->pos + 1 < h->count) {
        if (fc_hist_exists(h, h->pos + 1, exists, ctx)) {
            h->pos++;
            return true;
        }
        fc_hist_remove(h, h->pos + 1);
        (*removed)++;
    }
    return false;
}

bool files_history_up(struct files_history *h)
{
    if (!files_history_can_up(h))
        return false;
    const char *s = fc_slot(h, h->pos);
    uint32_t cut = h->len[fc_phys(h, h->pos)];
    while (cut > 0 && s[cut - 1] != '/')
        cut--;                          /* after the last '/' */
    return files_history_go(h, FILES_VIEW_FOLDER, s, cut > 1 ? cut - 1 : 1);
}

/* ---- The path bar ---- */

static void fc_part(struct files_pb_part *p, const char *text, uint32_t len, uint32_t x, uint32_t target)
{
    p->text = text;
    p->len = (uint16_t)len;
    p->x = (uint16_t)x;
    p->target = (uint16_t)target;
}

void files_pathbar(enum files_view view, const char *path, uint32_t len, uint32_t max_cells,
                   struct files_pathbar *pb)
{
    const char *rt;
    uint32_t rl, rtarget, cs, n = 1, total;
    pb->n = 0;
    pb->clipped = false;
    pb->cells = 0;
    if (view == FILES_VIEW_TRASH || view == FILES_VIEW_THISPC) {
        rt = view == FILES_VIEW_TRASH ? "Trash" : "This PC";
        rl = view == FILES_VIEW_TRASH ? 5 : 7;
        rtarget = 0;
        cs = 1;
        len = 0;
    } else if ((len == 10 && fc_same(path, FILES_HOME, 10)) || (len > 10 && fc_same(path, FILES_HOME "/", 11))) {
        rt = "Home";
        rl = 4;
        rtarget = 10;
        cs = 11;
    } else {
        rt = "/";
        rl = 1;
        rtarget = 1;
        cs = 1;
    }
    total = rl;
    for (uint32_t i = cs; i < len; i++) {
        uint32_t s = i;
        while (i < len && path[i] != '/')
            i++;
        n++;
        total += 3 + (i - s);
    }
    if (total <= max_cells && n <= FILES_PB_MAX) {
        uint32_t x = rl, k = 1;
        fc_part(&pb->part[0], rt, rl, 0, rtarget);
        for (uint32_t i = cs; i < len; i++) {
            uint32_t s = i;
            while (i < len && path[i] != '/')
                i++;
            fc_part(&pb->part[k++], path + s, i - s, x + 3, i);
            x += 3 + (i - s);
        }
        pb->n = n;
        pb->cells = total;
        return;
    }
    pb->clipped = true;
    uint32_t k = 0, w = 3, e = len;     /* w: "..." and, per part, " > " and the part */
    while (k + 1 < n && k < FILES_PB_MAX) {
        uint32_t s = e;
        while (s > cs && path[s - 1] != '/')
            s--;
        if (w + 3 + (e - s) > max_cells)
            break;
        w += 3 + (e - s);
        k++;
        fc_part(&pb->part[FILES_PB_MAX - k], path + s, e - s, 0, e);
        e = s - 1;
    }
    if (k > 0) {
        uint32_t x = 6;
        for (uint32_t i = 0; i < k; i++) {
            const struct files_pb_part *q = &pb->part[FILES_PB_MAX - k + i];
            fc_part(&pb->part[i], q->text, q->len, x, q->target);
            x += pb->part[i].len + 3u;
        }
        pb->n = k;
        pb->cells = w;
        return;
    }
    if (max_cells < 4)
        return;
    const char *lt = rt;
    uint32_t ll = rl, lg = rtarget;
    if (n > 1) {
        uint32_t s = len;
        while (s > cs && path[s - 1] != '/')
            s--;
        lt = path + s;
        ll = len - s;
        lg = len;
    }
    uint32_t shown = ll < max_cells - 3 ? ll : max_cells - 3;
    fc_part(&pb->part[0], lt + (ll - shown), shown, 3, lg);
    pb->n = 1;
    pb->cells = 3 + shown;
}

/* ---- The clipboard ---- */

void files_clip_init(struct files_clip *c, char *store)
{
    c->store = store;
    files_clip_clear(c);
}

void files_clip_begin(struct files_clip *c, bool cut)
{
    c->n = 0;
    c->used = 0;
    c->cut = cut;
}

enum files_clip_add files_clip_add(struct files_clip *c, const char *path, uint32_t len)
{
    if (len == 0 || len > FILES_PATH_MAX)
        return FILES_CLIP_TOO_LONG;
    if (c->n >= FILES_CLIP_MAX)
        return FILES_CLIP_FULL;
    fc_copy(c->store + c->used, path, len);
    c->store[c->used + len] = 0;
    c->off[c->n] = c->used;
    c->len[c->n] = (uint16_t)len;
    c->n++;
    c->used += len + 1;
    return FILES_CLIP_OK;
}

bool files_clip_empty(const struct files_clip *c)
{
    return c->n == 0;
}

const char *files_clip_path(const struct files_clip *c, uint32_t i, uint32_t *len)
{
    if (i >= c->n || i >= FILES_CLIP_MAX)       /* the count, and the arrays' own size */
        return 0;
    *len = c->len[i];
    return c->store + c->off[i];
}

void files_clip_clear(struct files_clip *c)
{
    c->n = 0;
    c->used = 0;
    c->cut = false;
}

/* ---- Names for Paste and Restore ---- */

uint32_t files_name_variant(const char *name, uint32_t len, bool folder, enum files_variant v, uint32_t n,
                            char *out)
{
    static const char restored[] = " (restored";
    char suffix[24];
    uint32_t sl = 0, k = 0, base = len;
    if (len < 1 || len > FILES_NAME_MAX || n < 1 || n > FILES_VARIANT_MAX)
        return 0;
    if (v == FILES_VARIANT_NUMBER && n == 1) {
        fc_copy(out, name, len);
        out[len] = 0;
        return len;
    }
    if (!folder) {
        uint32_t d = len;
        while (d > 0 && name[d - 1] != '.')
            d--;
        if (d >= 2)
            base = d - 1;               /* the extension starts at the last '.' (not the first byte) */
    }
    uint32_t el = len - base;
    if (v == FILES_VARIANT_NUMBER) {
        suffix[sl++] = ' ';
        sl += fc_utoa(n, suffix + sl);
    } else {
        for (uint32_t i = 0; restored[i]; i++)
            suffix[sl++] = restored[i];
        if (n >= 2) {
            suffix[sl++] = ' ';
            sl += fc_utoa(n, suffix + sl);
        }
        suffix[sl++] = ')';
    }
    if (sl + el + 1 > FILES_NAME_MAX)
        return 0;
    uint32_t b = base + sl + el > FILES_NAME_MAX ? FILES_NAME_MAX - sl - el : base;
    fc_copy(out, name, b);
    k = b;
    fc_copy(out + k, suffix, sl);
    k += sl;
    fc_copy(out + k, name + base, el);
    k += el;
    out[k] = 0;
    return k;
}

uint32_t files_unique_name(const char *name, uint32_t len, bool folder, enum files_variant v,
                           files_exists_fn exists, void *ctx, char *out)
{
    for (uint32_t n = 1; n <= FILES_VARIANT_MAX; n++) {
        uint32_t l = files_name_variant(name, len, folder, v, n, out);
        if (l == 0)
            return 0;
        if (!exists(ctx, out))
            return l;
    }
    out[0] = 0;
    return 0;
}

/* ---- The recursive steps ---- */

static enum files_walk_result fc_finish(struct files_walk *w, enum files_walk_result r)
{
    w->finished = true;
    w->result = r;
    return r;
}

static bool fc_append(char *p, uint32_t *plen, const char *name, uint32_t nl)
{
    if (*plen + 1 + nl > FILES_PATH_MAX)
        return false;
    p[(*plen)++] = '/';
    fc_copy(p + *plen, name, nl);
    *plen += nl;
    p[*plen] = 0;
    return true;
}

static void fc_push(struct files_walk *w)
{
    struct files_walk_level *lv = &w->lv[w->depth++];
    lv->cookie = 0;
    lv->src_len = (uint16_t)w->src_len;
    lv->dst_len = (uint16_t)w->dst_len;
}

/* src (and dst) back to the open folder's own path. */
static void fc_back(struct files_walk *w)
{
    const struct files_walk_level *lv = &w->lv[w->depth - 1];
    w->src_len = lv->src_len;
    w->src[w->src_len] = 0;
    if (w->op == FILES_WALK_COPY) {
        w->dst_len = lv->dst_len;
        w->dst[w->dst_len] = 0;
    }
}

static void fc_name_of(char *out, const char *path, uint32_t len)
{
    uint32_t s = len, k = 0;
    while (s > 0 && path[s - 1] != '/')
        s--;
    while (s < len && k < FILES_NAME_MAX)
        out[k++] = path[s++];
    out[k] = 0;
}

bool files_walk_start(struct files_walk *w, const struct files_fs *fs, enum files_walk_op op, const char *src,
                      const char *dst)
{
    w->fs = fs;
    w->op = op;
    w->depth = 0;
    w->started = false;
    w->copying = false;
    w->finished = false;
    w->offset = 0;
    w->done = 0;
    w->links_skipped = 0;
    w->err = 0;
    w->result = FILES_WALK_MORE;
    w->name[0] = 0;
    w->link_name[0] = 0;
    w->src_len = fc_strnlen(src, FILES_PATH_MAX + 1);
    w->dst_len = op == FILES_WALK_COPY ? fc_strnlen(dst, FILES_PATH_MAX + 1) : 0;
    if (w->src_len > FILES_PATH_MAX || w->dst_len > FILES_PATH_MAX) {
        w->src_len = w->dst_len = 0;
        w->src[0] = w->dst[0] = 0;
        fc_finish(w, FILES_WALK_TOO_LONG);
        return false;
    }
    fc_copy(w->src, src, w->src_len);
    w->src[w->src_len] = 0;
    if (op == FILES_WALK_COPY)
        fc_copy(w->dst, dst, w->dst_len);
    w->dst[w->dst_len] = 0;
    if (w->src_len < 2 || src[0] != '/' || (op == FILES_WALK_COPY && (w->dst_len < 2 || dst[0] != '/'))) {
        fc_finish(w, FILES_WALK_ERROR);
        return false;
    }
    return true;
}

static enum files_walk_result fc_fail(struct files_walk *w, int64_t err)
{
    w->err = err;
    return fc_finish(w, FILES_WALK_ERROR);
}

enum files_walk_result files_walk_step(struct files_walk *w)
{
    const struct files_fs *fs = w->fs;
    enum files_kind k;
    int64_t r;
    if (w->finished)
        return w->result;
    if (!w->started) {                  /* the start itself */
        w->started = true;
        k = fs->lstat(fs->ctx, w->src);
        if (k != FILES_K_DIR && k != FILES_K_FILE && k != FILES_K_LINK)
            return fc_finish(w, FILES_WALK_ERROR);
        if (w->op == FILES_WALK_DELETE) {
            if (k == FILES_K_DIR) {
                fc_push(w);
                return FILES_WALK_MORE;
            }
            r = fs->unlink(fs->ctx, w->src);        /* a link removed as itself */
            if (r < 0)
                return fc_fail(w, r);
            w->done = 1;
            return fc_finish(w, FILES_WALK_DONE);
        }
        if (k == FILES_K_LINK) {
            fc_name_of(w->link_name, w->src, w->src_len);
            return fc_finish(w, FILES_WALK_LINK);
        }
        if (k == FILES_K_DIR) {
            r = fs->mkdir(fs->ctx, w->dst);
            if (r < 0)
                return fc_fail(w, r);
            fc_push(w);
            return FILES_WALK_MORE;
        }
        w->copying = true;
        w->offset = 0;
        return FILES_WALK_MORE;
    }
    if (w->copying) {                   /* one chunk of a file */
        r = fs->copy_chunk(fs->ctx, w->src, w->dst, w->offset);
        if (r < 0) {
            fs->unlink(fs->ctx, w->dst);            /* no partial copy left */
            if (w->depth)
                fc_back(w);
            return fc_fail(w, r);
        }
        if (r > 0) {
            w->offset += (uint64_t)r;
            return FILES_WALK_MORE;
        }
        w->copying = false;
        w->done++;
        if (w->depth == 0)
            return fc_finish(w, FILES_WALK_DONE);
        fc_back(w);
        return FILES_WALK_MORE;
    }
    struct files_walk_level *lv = &w->lv[w->depth - 1];     /* the open folder's next entry */
    uint64_t first = 0;                 /* delete: always the first entry left */
    int rd = fs->read_dir(fs->ctx, w->src, w->op == FILES_WALK_DELETE ? &first : &lv->cookie, w->name);
    if (rd < 0)
        return fc_fail(w, rd);
    if (rd == 0) {                      /* the folder is finished */
        if (w->op == FILES_WALK_DELETE) {
            r = fs->rmdir(fs->ctx, w->src);
            if (r < 0)
                return fc_fail(w, r);
        }
        w->done++;
        w->depth--;
        if (w->depth == 0)
            return fc_finish(w, FILES_WALK_DONE);
        fc_back(w);
        return FILES_WALK_MORE;
    }
    uint32_t nl = fc_strnlen(w->name, FILES_NAME_MAX + 1);
    if (nl == 0 || nl > FILES_NAME_MAX)
        return fc_finish(w, FILES_WALK_ERROR);
    if (!fc_append(w->src, &w->src_len, w->name, nl) ||
        (w->op == FILES_WALK_COPY && !fc_append(w->dst, &w->dst_len, w->name, nl))) {
        fc_back(w);
        return fc_finish(w, FILES_WALK_TOO_LONG);
    }
    k = fs->lstat(fs->ctx, w->src);     /* the entry's own kind (rule SL) */
    if (k != FILES_K_DIR && k != FILES_K_FILE && k != FILES_K_LINK) {
        fc_back(w);
        return fc_finish(w, FILES_WALK_ERROR);
    }
    if (k == FILES_K_DIR) {
        if (w->depth >= FILES_DEPTH_MAX + 1) {  /* lv[] holds FILES_DEPTH_MAX + 1 levels */
            fc_back(w);
            return fc_finish(w, FILES_WALK_DEEP);
        }
        if (w->op == FILES_WALK_COPY) {
            r = fs->mkdir(fs->ctx, w->dst);
            if (r < 0) {
                fc_back(w);
                return fc_fail(w, r);
            }
        }
        fc_push(w);
        return FILES_WALK_MORE;
    }
    if (w->op == FILES_WALK_DELETE) {
        r = fs->unlink(fs->ctx, w->src);            /* a link removed as an entry, never followed */
        fc_back(w);
        if (r < 0)
            return fc_fail(w, r);
        w->done++;
        return FILES_WALK_MORE;
    }
    if (k == FILES_K_LINK) {            /* links can't be copied yet: skipped and counted */
        if (w->links_skipped++ == 0) {
            fc_copy(w->link_name, w->name, nl);
            w->link_name[nl] = 0;
        }
        fc_back(w);
        return FILES_WALK_MORE;
    }
    w->copying = true;
    w->offset = 0;
    return FILES_WALK_MORE;
}

enum files_target files_check_target(const struct files_fs *fs, const char *path, char *scratch, uint32_t *at)
{
    uint32_t len = fc_strnlen(path, FILES_PATH_MAX + 1);
    *at = 0;
    if (len == 0 || len > FILES_PATH_MAX || path[0] != '/')
        return FILES_TARGET_ERROR;
    fc_copy(scratch, path, len);
    scratch[len] = 0;
    for (uint32_t j = 1; j <= len; j++) {
        if (j < len && path[j] != '/')
            continue;
        char saved = scratch[j];
        scratch[j] = 0;
        enum files_kind k = fs->lstat(fs->ctx, scratch);
        scratch[j] = saved;
        *at = j;
        if (k == FILES_K_NONE)
            return FILES_TARGET_MISSING;
        if (k == FILES_K_ERROR)
            return FILES_TARGET_ERROR;
        if (k != FILES_K_DIR)
            return FILES_TARGET_NOT_FOLDER;
    }
    return FILES_TARGET_OK;
}

enum files_move files_move_how(enum files_kind kind, bool same_fs)
{
    if (same_fs)
        return FILES_MOVE_RENAME;
    return kind == FILES_K_LINK ? FILES_MOVE_REFUSE_LINK : FILES_MOVE_COPY_DELETE;
}
