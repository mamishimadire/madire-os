/* The Files and Trash cores as a host tool (1.5 stage 11): reads a mode
 * name and its inputs on stdin, one per line (bytes in hexadecimal, "-" for
 * none), runs the real cores (kernel/files/files_core.c, trash_core.c) on
 * them and prints one answer per input. tests/files_ref.py sends the inputs
 * and compares every answer with its own model (part static). Built plainly
 * (make unit, through test_files_core's rule) and with AddressSanitizer and
 * UndefinedBehaviorSanitizer (build/filesdump-asan). Modes: trash, encode,
 * sort, limit, pathbar, history, walk, clip (files_ref.py describes each). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include "kernel/files/files_core.h"
#include "kernel/files/trash_core.h"
#include "tests/files_fakefs.h"

static char *line;
static size_t line_cap;

static int next_line(void)
{
    ssize_t n = getline(&line, &line_cap, stdin);
    if (n < 0)
        return 0;
    while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r'))
        line[--n] = 0;
    return 1;
}

/* Hex text (or "-") into out; the bytes, or -1. A NUL is added. */
static long unhex(const char *s, size_t len, uint8_t *out, size_t cap)
{
    size_t n = 0;
    if (len == 1 && s[0] == '-') {
        out[0] = 0;
        return 0;
    }
    if (len % 2)
        return -1;
    for (size_t i = 0; i < len; i += 2) {
        unsigned v;
        if (n + 1 >= cap || sscanf(s + i, "%2x", &v) != 1)
            return -1;
        out[n++] = (uint8_t)v;
    }
    out[n] = 0;
    return (long)n;
}

static void put_hex(const void *p, size_t n)
{
    const uint8_t *b = p;
    if (!n) {
        putchar('-');
        return;
    }
    for (size_t i = 0; i < n; i++)
        printf("%02x", b[i]);
}

/* The next space-separated field of *s (NUL-terminated in place). */
static char *field(char **s)
{
    char *p = *s, *e;
    while (*p == ' ')
        p++;
    e = strchr(p, ' ');
    if (e) {
        *e = 0;
        *s = e + 1;
    } else {
        *s = p + strlen(p);
    }
    return p;
}

static uint8_t buf[3 * FILES_PATH_MAX * 4 + 64];
static uint8_t buf2[3 * FILES_PATH_MAX * 4 + 64];
static struct trash_info info;

static void mode_trash(void)
{
    while (next_line()) {
        long n = unhex(line, strlen(line), buf, sizeof(buf));
        if (n < 0) {
            printf("bad-input\n");
            continue;
        }
        enum trash_info_result r = trash_info_parse(buf, (uint32_t)n, &info);
        printf("%s ", trash_info_reason(r));
        if (r == TI_OK) {
            put_hex(info.path, info.path_len);
            if (info.date_ok)
                printf(" %04u-%02u-%02uT%02u:%02u:%02u\n", info.date.year, info.date.month, info.date.day,
                       info.date.hour, info.date.minute, info.date.second);
            else
                printf(" -\n");
        } else {
            printf("- -\n");
        }
    }
}

/* YYYY-MM-DDThh:mm:ss into d by position only (any digits); anything else
 * gives month 0 (never valid). */
static void date_fields(const char *s, struct trash_date *d)
{
    unsigned y, mo, da, h, mi, se;
    memset(d, 0, sizeof(*d));
    if (strlen(s) != 19 || s[4] != '-' || s[7] != '-' || s[10] != 'T' || s[13] != ':' || s[16] != ':')
        return;
    for (int i = 0; i < 19; i++)
        if (i != 4 && i != 7 && i != 10 && i != 13 && i != 16 && (s[i] < '0' || s[i] > '9'))
            return;
    if (sscanf(s, "%4u-%2u-%2uT%2u:%2u:%2u", &y, &mo, &da, &h, &mi, &se) != 6)
        return;
    d->year = (uint16_t)y;
    d->month = (uint8_t)mo;
    d->day = (uint8_t)da;
    d->hour = (uint8_t)h;
    d->minute = (uint8_t)mi;
    d->second = (uint8_t)se;
}

static void mode_encode(void)
{
    static char enc[3 * FILES_PATH_MAX + 1], dec[TRASH_PATH_MAX + 1], built[TRASH_INFO_MAX + 8];
    while (next_line()) {
        char *s = line, *ph = field(&s), *dt = field(&s);
        long n = unhex(ph, strlen(ph), buf, sizeof(buf));
        if (n < 0) {
            printf("bad-input\n");
            continue;
        }
        uint32_t e = trash_path_encode((const char *)buf, (uint32_t)n, enc, sizeof(enc));
        if (e) {
            uint32_t dl;
            put_hex(enc, e);
            enum trash_info_result r = trash_path_decode(enc, e, dec, &dl);
            printf(" %s ", trash_info_reason(r));
            if (r == TI_OK)
                put_hex(dec, dl);
            else
                putchar('-');
        } else {
            printf("- - -");
        }
        struct trash_date d;
        date_fields(dt, &d);
        uint32_t b = trash_info_build((const char *)buf, (uint32_t)n, &d, built, sizeof(built));
        if (!b) {
            printf(" 0 - 0\n");
            continue;
        }
        enum trash_info_result r = trash_info_parse((const uint8_t *)built, b, &info);
        int same = r == TI_OK && info.path_len == (uint32_t)n && memcmp(info.path, buf, (size_t)n) == 0 &&
                   info.date_ok && info.date.year == d.year && info.date.month == d.month &&
                   info.date.day == d.day && info.date.hour == d.hour && info.date.minute == d.minute &&
                   info.date.second == d.second;
        printf(" %u %s %d\n", b, trash_info_reason(r), same);
    }
}

static struct files_entry entries[FILES_MAX_ENTRIES];
static char names[FILES_MAX_ENTRIES * (FILES_NAME_MAX + 1)];

/* "<kind> <hex name>" into the folder; its result. */
static enum files_add add_row(struct files_folder *f)
{
    char *s = line, *k = field(&s), *nh = field(&s);
    long n = unhex(nh, strlen(nh), buf, sizeof(buf));
    if (n < 0)
        return FILES_ADD_BAD;
    return files_folder_add(f, (const char *)buf, (uint32_t)n, (enum files_kind)atoi(k), 0, 0);
}

static void mode_sort(void)
{
    struct files_folder f;
    while (next_line()) {
        int count = atoi(line + 2);
        files_folder_init(&f, entries, FILES_MAX_ENTRIES, names, sizeof(names), true);
        for (int i = 0; i < count && next_line(); i++)
            add_row(&f);
        files_folder_sort(&f);
        for (uint32_t i = 0; i < f.n; i++) {
            put_hex(files_entry_name(&f, i), f.e[i].name_len);
            putchar('\n');
        }
        printf("END\n");
    }
}

static void mode_limit(void)
{
    static char codes[4096];
    struct files_folder f;
    while (next_line()) {
        char *s = line + 2, *sh = field(&s), *cn = field(&s);
        int count = atoi(cn), nc = 0, stop = 0;
        if (files_folder_bytes(FILES_MAX_ENTRIES) != sizeof(entries) + sizeof(names))
            printf("bytes ");
        files_folder_init(&f, entries, FILES_MAX_ENTRIES, names, sizeof(names), atoi(sh) != 0);
        for (int i = 0; i < count && next_line(); i++) {
            if (stop)
                continue;
            enum files_add r = add_row(&f);
            codes[nc++] = "OSFB"[r];
            if (r == FILES_ADD_FULL)
                stop = 1;
        }
        codes[nc] = 0;
        printf("%s %u %d ", nc ? codes : "-", f.n, f.truncated);
        if (!f.n)
            putchar('-');
        for (uint32_t i = 0; i < f.n; i++) {
            if (i)
                putchar(',');
            put_hex(files_entry_name(&f, i), f.e[i].name_len);
        }
        putchar('\n');
    }
}

static void mode_pathbar(void)
{
    static struct files_pathbar pb;
    while (next_line()) {
        char *s = line, *v = field(&s), *mx = field(&s), *ph = field(&s);
        long n = unhex(ph, strlen(ph), buf, sizeof(buf));
        if (n < 0) {
            printf("bad-input\n");
            continue;
        }
        files_pathbar((enum files_view)atoi(v), (const char *)buf, (uint32_t)n, (uint32_t)atoi(mx), &pb);
        printf("%u %d %u", pb.n, pb.clipped, pb.cells);
        for (uint32_t i = 0; i < pb.n; i++) {
            putchar(' ');
            put_hex(pb.part[i].text, pb.part[i].len);
            printf(",%u,%u", pb.part[i].x, pb.part[i].target);
        }
        putchar('\n');
    }
}

static char missing[64][FILES_PATH_MAX + 1];
static int missing_n;

static bool hist_exists(void *ctx, const char *path)
{
    (void)ctx;
    for (int i = 0; i < missing_n; i++)
        if (strcmp(missing[i], path) == 0)
            return false;
    return true;
}

static void mode_history(void)
{
    static char store[FILES_HISTORY_BYTES];
    static struct files_history h;
    files_history_init(&h, store);
    missing_n = 0;
    while (next_line()) {
        char *s = line, *op = field(&s);
        bool ret = true;
        uint32_t removed = 0;
        if (strcmp(op, "RESET") == 0) {
            files_history_init(&h, store);
            missing_n = 0;
            continue;
        }
        if (op[0] == 'X' || op[0] == 'Y') {
            char *ph = field(&s);
            long n = unhex(ph, strlen(ph), buf, sizeof(buf));
            int at = -1;
            for (int i = 0; i < missing_n; i++)
                if (n >= 0 && strcmp(missing[i], (char *)buf) == 0)
                    at = i;
            if (op[0] == 'X' && at < 0 && missing_n < 64 && n >= 0)
                strcpy(missing[missing_n++], (char *)buf);
            if (op[0] == 'Y' && at >= 0)
                strcpy(missing[at], missing[--missing_n]);
            continue;
        }
        if (op[0] == 'G') {
            char *v = field(&s), *ph = field(&s);
            long n = unhex(ph, strlen(ph), buf, sizeof(buf));
            ret = n >= 0 && files_history_go(&h, (enum files_view)atoi(v), (const char *)buf, (uint32_t)n);
        } else if (op[0] == 'B') {
            ret = files_history_back(&h, hist_exists, 0, &removed);
        } else if (op[0] == 'F') {
            ret = files_history_forward(&h, hist_exists, 0, &removed);
        } else {
            ret = files_history_up(&h);
        }
        enum files_view view = FILES_VIEW_FOLDER;
        const char *cur = 0;
        uint32_t cl = 0;
        bool have = files_history_current(&h, &view, &cur, &cl);
        printf("%d %u %u %u %d %d %d %d ", ret, removed, h.count, h.count ? h.pos : 0,
               files_history_can_back(&h), files_history_can_forward(&h), files_history_can_up(&h),
               have ? (int)view : 0);
        put_hex(cur, have ? cl : 0);
        putchar('\n');
    }
}

static const char *const walk_names[] = { "more", "done", "deep", "too-long", "link", "error" };
static const char *const target_names[] = { "ok", "not-folder", "missing", "error" };

static int dump_cmp(const void *a, const void *b)
{
    static char pa[FILES_PATH_MAX * 2 + 2], pb[FILES_PATH_MAX * 2 + 2];
    ffs_path(*(const int *)a, pa);
    ffs_path(*(const int *)b, pb);
    size_t la = strlen(pa), lb = strlen(pb), n = la < lb ? la : lb;
    int c = memcmp(pa, pb, n);
    return c ? c : (la < lb ? -1 : la > lb);
}

static void mode_walk(void)
{
    static struct files_walk w;
    static char scratch[FILES_PATH_MAX + 1], src[FILES_PATH_MAX * 2 + 2], dst[FILES_PATH_MAX * 2 + 2];
    static char path[FILES_PATH_MAX * 2 + 2];
    static int order[FFS_NODES];
    while (next_line()) {
        char *s = line, *op = field(&s);
        if (op[0] == 'T') {
            int count = atoi(field(&s));
            ffs_reset();
            for (int i = 0; i < count && next_line(); i++) {
                char *t = line, *par = field(&t), *k = field(&t), *nh = field(&t), *sz = field(&t), *sd = field(&t);
                long n = unhex(nh, strlen(nh), buf, sizeof(buf));
                if (n >= 0)
                    ffs_add(atoi(par), atoi(k), (const char *)buf, strtoull(sz, 0, 10), (uint32_t)atoi(sd));
            }
        } else if (op[0] == 'K') {
            char *ph = field(&s);
            uint32_t at = 0;
            long n = unhex(ph, strlen(ph), buf, sizeof(buf));
            enum files_target r = n >= 0 ? files_check_target(&ffs_fs, (const char *)buf, scratch, &at)
                                         : FILES_TARGET_ERROR;
            printf("%s %u\n", target_names[r], at);
        } else if (op[0] == 'W') {
            char *o = field(&s), *sh = field(&s), *dh = field(&s), *fa = field(&s);
            if (unhex(sh, strlen(sh), (uint8_t *)src, sizeof(src)) < 0 ||
                unhex(dh, strlen(dh), (uint8_t *)dst, sizeof(dst)) < 0) {
                printf("bad-input\nEND\n");
                continue;
            }
            ffs_fail_at = atoi(fa);
            ffs_calls = 0;
            files_walk_start(&w, &ffs_fs, o[0] == 'D' ? FILES_WALK_DELETE : FILES_WALK_COPY, src, dst);
            enum files_walk_result r = FILES_WALK_MORE;
            long steps = 0;
            while (steps < 2000000 && (r = files_walk_step(&w)) == FILES_WALK_MORE)
                steps++;
            printf("%s %u %u ", r == FILES_WALK_MORE ? "runaway" : walk_names[r], w.done, w.links_skipped);
            put_hex(w.link_name, strlen(w.link_name));
            printf(" %lld\n", (long long)w.err);
            int n = 0;
            for (int i = 1; i < ffs_count; i++)
                if (ffs_node[i].used)
                    order[n++] = i;
            qsort(order, (size_t)n, sizeof(order[0]), dump_cmp);
            for (int i = 0; i < n; i++) {
                const struct ffs_node *x = &ffs_node[order[i]];
                ffs_path(order[i], path);
                put_hex(path, strlen(path));
                printf(" %d %llu %u\n", x->kind, (unsigned long long)x->size, x->seed);
            }
            printf("END\n");
        }
    }
}

static char taken[1000][FILES_NAME_MAX + 1];
static int taken_n;

static bool name_taken(void *ctx, const char *name)
{
    (void)ctx;
    for (int i = 0; i < taken_n; i++)
        if (strcmp(taken[i], name) == 0)
            return true;
    return false;
}

static void mode_clip(void)
{
    static char store[FILES_CLIP_BYTES], out[FILES_NAME_MAX + 1];
    static struct files_clip c;
    files_clip_init(&c, store);
    while (next_line()) {
        char *s = line, *op = field(&s);
        if (op[0] == 'B' || op[0] == 'C') {
            if (op[0] == 'B')
                files_clip_begin(&c, atoi(field(&s)) != 0);
            else
                files_clip_clear(&c);
            printf("1 %u %d %d\n", c.n, c.cut, files_clip_empty(&c));
        } else if (op[0] == 'A') {
            char *ph = field(&s);
            long n = unhex(ph, strlen(ph), buf, sizeof(buf));
            enum files_clip_add r = files_clip_add(&c, (const char *)buf, n < 0 ? 0 : (uint32_t)n);
            printf("%d %u %d %d\n", r, c.n, c.cut, files_clip_empty(&c));
        } else if (op[0] == 'P') {
            uint32_t len = 0;
            const char *p = files_clip_path(&c, (uint32_t)atoi(field(&s)), &len);
            put_hex(p, p ? len : 0);
            putchar('\n');
        } else if (op[0] == 'V' || op[0] == 'U') {
            char *fo = field(&s), *re = field(&s);
            uint32_t l, nn = op[0] == 'V' ? (uint32_t)atoi(field(&s)) : 0;
            char *nh = field(&s);
            long n = unhex(nh, strlen(nh), buf, sizeof(buf));
            if (n < 0) {
                printf("bad-input\n");
                continue;
            }
            if (op[0] == 'V') {
                l = files_name_variant((const char *)buf, (uint32_t)n, atoi(fo) != 0,
                                       atoi(re) ? FILES_VARIANT_RESTORED : FILES_VARIANT_NUMBER, nn, out);
            } else {
                char *tl = field(&s);
                taken_n = 0;
                if (strcmp(tl, "-") != 0) {
                    for (char *t = strtok(tl, ","); t && taken_n < 1000; t = strtok(0, ","))
                        if (unhex(t, strlen(t), buf2, sizeof(buf2)) >= 0)
                            strcpy(taken[taken_n++], (char *)buf2);
                }
                l = files_unique_name((const char *)buf, (uint32_t)n, atoi(fo) != 0,
                                      atoi(re) ? FILES_VARIANT_RESTORED : FILES_VARIANT_NUMBER, name_taken, 0, out);
            }
            put_hex(out, l);
            putchar('\n');
        }
    }
}

int main(void)
{
    if (!next_line()) {
        fprintf(stderr, "filesdump: no mode\n");
        return 2;
    }
    if (strcmp(line, "trash") == 0)
        mode_trash();
    else if (strcmp(line, "encode") == 0)
        mode_encode();
    else if (strcmp(line, "sort") == 0)
        mode_sort();
    else if (strcmp(line, "limit") == 0)
        mode_limit();
    else if (strcmp(line, "pathbar") == 0)
        mode_pathbar();
    else if (strcmp(line, "history") == 0)
        mode_history();
    else if (strcmp(line, "walk") == 0)
        mode_walk();
    else if (strcmp(line, "clip") == 0)
        mode_clip();
    else {
        fprintf(stderr, "filesdump: unknown mode %s\n", line);
        return 2;
    }
    free(line);
    return 0;
}
