/* The Trash's pure part (1.5 stage 11); the rules are in trash_core.h. */

#include "kernel/files/trash_core.h"

static bool tc_same(const char *a, const char *b, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++)
        if (a[i] != b[i])
            return false;
    return true;
}

/* Whether the n bytes at p start with the NUL-terminated pre. */
static bool tc_starts(const uint8_t *p, uint32_t n, const char *pre)
{
    for (uint32_t i = 0; pre[i]; i++)
        if (i >= n || p[i] != (uint8_t)pre[i])
            return false;
    return true;
}

static int tc_hex(uint8_t c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

static bool tc_leap(uint32_t y)
{
    return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
}

static uint32_t tc_month_days(uint32_t y, uint32_t m)
{
    if (m == 2)
        return tc_leap(y) ? 29 : 28;
    return (m == 4 || m == 6 || m == 9 || m == 11) ? 30 : 31;
}

bool trash_date_valid(const struct trash_date *d)
{
    return d->year >= 1 && d->year <= 9999 && d->month >= 1 && d->month <= 12 && d->day >= 1 &&
           d->day <= tc_month_days(d->year, d->month) && d->hour <= 23 && d->minute <= 59 && d->second <= 59;
}

/* n decimal digits at p into *v. */
static bool tc_digits(const uint8_t *p, uint32_t n, uint32_t *v)
{
    *v = 0;
    for (uint32_t i = 0; i < n; i++) {
        if (p[i] < '0' || p[i] > '9')
            return false;
        *v = *v * 10 + (uint32_t)(p[i] - '0');
    }
    return true;
}

/* YYYY-MM-DDThh:mm:ss, exactly. */
static bool tc_date_parse(const uint8_t *p, uint32_t n, struct trash_date *d)
{
    uint32_t y, mo, da, h, mi, s;
    if (n != TRASH_DATE_LEN || p[4] != '-' || p[7] != '-' || p[10] != 'T' || p[13] != ':' || p[16] != ':')
        return false;
    if (!tc_digits(p, 4, &y) || !tc_digits(p + 5, 2, &mo) || !tc_digits(p + 8, 2, &da) ||
        !tc_digits(p + 11, 2, &h) || !tc_digits(p + 14, 2, &mi) || !tc_digits(p + 17, 2, &s))
        return false;
    d->year = (uint16_t)y;
    d->month = (uint8_t)mo;
    d->day = (uint8_t)da;
    d->hour = (uint8_t)h;
    d->minute = (uint8_t)mi;
    d->second = (uint8_t)s;
    return trash_date_valid(d);
}

enum trash_info_result trash_path_decode(const char *text, uint32_t len, char *out, uint32_t *out_len)
{
    uint32_t n = 0;
    *out_len = 0;
    out[0] = 0;
    for (uint32_t i = 0; i < len; i++) {
        uint8_t c = (uint8_t)text[i];
        if (c == '%') {
            if (len - i < 3)
                return TI_BAD_ESCAPE;
            int hi = tc_hex((uint8_t)text[i + 1]), lo = tc_hex((uint8_t)text[i + 2]);
            if (hi < 0 || lo < 0)
                return TI_BAD_ESCAPE;
            c = (uint8_t)(hi * 16 + lo);
            i += 2;
            if (c == 0)
                return TI_NUL;
            if (c == '/')
                return TI_ESCAPED_SLASH;
        } else if (c == 0) {
            return TI_NUL;
        }
        if (n == TRASH_PATH_MAX)
            return TI_PATH_TOO_LONG;
        out[n++] = (char)c;
    }
    out[n] = 0;
    *out_len = n;
    return TI_OK;
}

enum trash_info_result trash_path_check(const char *p, uint32_t len)
{
    const uint32_t home = sizeof(TRASH_HOME) - 1, trash = sizeof(TRASH_DIR) - 1;
    if (len > TRASH_PATH_MAX)
        return TI_PATH_TOO_LONG;
    if (len == 0 || p[0] != '/')
        return TI_RELATIVE;
    if (len > 1 && p[len - 1] == '/')
        return TI_TRAILING_SLASH;
    for (uint32_t i = 1; i < len; i++) {
        uint32_t s = i;
        while (i < len && p[i] != '/')
            i++;
        uint32_t c = i - s;
        if (c == 0)
            return TI_EMPTY_COMPONENT;
        if ((c == 1 && p[s] == '.') || (c == 2 && p[s] == '.' && p[s + 1] == '.'))
            return TI_DOT_COMPONENT;
        if (c > TRASH_NAME_MAX)
            return TI_COMPONENT_TOO_LONG;
    }
    if (len <= home || !tc_same(p, TRASH_HOME, home))
        return TI_OUTSIDE_HOME;
    if (len >= trash && tc_same(p, TRASH_DIR, trash) && (len == trash || p[trash] == '/'))
        return TI_INSIDE_TRASH;
    return TI_OK;
}

enum trash_info_result trash_info_parse(const uint8_t *buf, uint32_t len, struct trash_info *out)
{
    bool header = false, bad_header = false, have_path = false, have_date = false;
    uint32_t path_at = 0, path_n = 0;
    out->path[0] = 0;
    out->path_len = 0;
    out->date_ok = false;
    if (len > TRASH_INFO_MAX)
        return TI_FILE_TOO_LARGE;
    for (uint32_t i = 0; i < len; i++) {
        uint32_t s = i;
        while (i < len && buf[i] != '\n')
            i++;
        uint32_t n = i - s;
        if (n > TRASH_LINE_MAX)
            return TI_LINE_TOO_LONG;
        if (bad_header)
            continue;
        if (!header) {
            if (n == 0)
                continue;
            if (n == 12 && tc_starts(buf + s, n, "[Trash Info]"))
                header = true;
            else
                bad_header = true;
            continue;
        }
        if (!have_path && tc_starts(buf + s, n, "Path=")) {
            have_path = true;
            path_at = s + 5;
            path_n = n - 5;
        } else if (!have_date && tc_starts(buf + s, n, "DeletionDate=")) {
            have_date = true;
            out->date_ok = tc_date_parse(buf + s + 13, n - 13, &out->date);
        }
    }
    if (!header)
        return TI_BAD_HEADER;
    if (!have_path)
        return TI_NO_PATH;
    uint32_t plen;
    enum trash_info_result r = trash_path_decode((const char *)buf + path_at, path_n, out->path, &plen);
    if (r != TI_OK)
        return r;
    r = trash_path_check(out->path, plen);
    if (r != TI_OK) {
        out->path[0] = 0;
        return r;
    }
    out->path_len = plen;
    return TI_OK;
}

const char *trash_info_reason(enum trash_info_result r)
{
    switch (r) {
    case TI_OK:                 return "ok";
    case TI_FILE_TOO_LARGE:     return "file-too-large";
    case TI_LINE_TOO_LONG:      return "line-too-long";
    case TI_BAD_HEADER:         return "bad-header";
    case TI_NO_PATH:            return "no-path";
    case TI_BAD_ESCAPE:         return "bad-escape";
    case TI_NUL:                return "nul";
    case TI_ESCAPED_SLASH:      return "escaped-slash";
    case TI_PATH_TOO_LONG:      return "path-too-long";
    case TI_RELATIVE:           return "relative";
    case TI_TRAILING_SLASH:     return "trailing-slash";
    case TI_EMPTY_COMPONENT:    return "empty-component";
    case TI_DOT_COMPONENT:      return "dot-component";
    case TI_COMPONENT_TOO_LONG: return "component-too-long";
    case TI_OUTSIDE_HOME:       return "outside-home";
    case TI_INSIDE_TRASH:       return "inside-trash";
    case TI_RESULTS:            break;
    }
    return "?";
}

static bool tc_keep(uint8_t c)
{
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))
        return true;
    return c == '-' || c == '_' || c == '.' || c == '!' || c == '~' || c == '*' || c == '\'' || c == '(' ||
           c == ')' || c == '/';
}

uint32_t trash_path_encode(const char *path, uint32_t len, char *out, uint32_t out_size)
{
    static const char hexd[16] = { '0', '1', '2', '3', '4', '5', '6', '7',
                                   '8', '9', 'A', 'B', 'C', 'D', 'E', 'F' };
    uint32_t n = 0;
    if (out_size == 0)
        return 0;
    for (uint32_t i = 0; i < len; i++) {
        uint8_t c = (uint8_t)path[i];
        if (tc_keep(c)) {
            if (n + 2 > out_size)
                return 0;
            out[n++] = (char)c;
        } else {
            if (n + 4 > out_size)
                return 0;
            out[n++] = '%';
            out[n++] = hexd[c >> 4];
            out[n++] = hexd[c & 15];
        }
    }
    out[n] = 0;
    return n;
}

/* Appends the NUL-terminated s at *n if it fits (keeping room for a NUL). */
static bool tc_put(char *out, uint32_t cap, uint32_t *n, const char *s)
{
    for (uint32_t i = 0; s[i]; i++) {
        if (*n + 1 >= cap)
            return false;
        out[(*n)++] = s[i];
    }
    return true;
}

static void tc_num(char *p, uint32_t v, uint32_t digits)
{
    for (uint32_t i = digits; i > 0; i--) {
        p[i - 1] = (char)('0' + v % 10);
        v /= 10;
    }
}

uint32_t trash_info_build(const char *path, uint32_t len, const struct trash_date *d, char *out,
                          uint32_t out_size)
{
    uint32_t cap = out_size > TRASH_INFO_MAX + 1 ? TRASH_INFO_MAX + 1 : out_size, n = 0;
    char date[TRASH_DATE_LEN + 1];
    if (trash_path_check(path, len) != TI_OK || !trash_date_valid(d))
        return 0;
    for (uint32_t i = 0; i < len; i++)
        if (path[i] == 0)
            return 0;
    if (!tc_put(out, cap, &n, "[Trash Info]\nPath="))
        return 0;
    uint32_t e = trash_path_encode(path, len, out + n, cap - n);
    if (e == 0 || 5 + e > TRASH_LINE_MAX)
        return 0;
    n += e;
    tc_num(date, d->year, 4);
    date[4] = '-';
    tc_num(date + 5, d->month, 2);
    date[7] = '-';
    tc_num(date + 8, d->day, 2);
    date[10] = 'T';
    tc_num(date + 11, d->hour, 2);
    date[13] = ':';
    tc_num(date + 14, d->minute, 2);
    date[16] = ':';
    tc_num(date + 17, d->second, 2);
    date[TRASH_DATE_LEN] = 0;
    if (!tc_put(out, cap, &n, "\nDeletionDate=") || !tc_put(out, cap, &n, date) || !tc_put(out, cap, &n, "\n"))
        return 0;
    out[n] = 0;
    return n;
}

uint32_t trash_slot_name(const char *name, uint32_t len, uint32_t n, char *out)
{
    char suffix[4];
    uint32_t sl = 0, k = 0;
    if (n < 1 || n > TRASH_SLOT_MAX || len < 1 || len > TRASH_NAME_MAX)
        return 0;
    for (uint32_t i = 0; i < len; i++)
        if (name[i] == '/' || name[i] == 0)
            return 0;
    if (n >= 2) {
        suffix[sl++] = '.';
        if (n >= 100)
            suffix[sl++] = (char)('0' + n / 100);
        if (n >= 10)
            suffix[sl++] = (char)('0' + n / 10 % 10);
        suffix[sl++] = (char)('0' + n % 10);
    }
    uint32_t room = TRASH_NAME_MAX - (uint32_t)(sizeof(TRASH_INFO_EXT) - 1) - sl;
    uint32_t base = len < room ? len : room;
    for (uint32_t i = 0; i < base; i++)
        out[k++] = name[i];
    for (uint32_t i = 0; i < sl; i++)
        out[k++] = suffix[i];
    out[k] = 0;
    return k;
}

enum trash_restore trash_restore_check(const char *path, uint32_t len, trash_lstat_fn lstat, void *ctx,
                                       char *scratch, uint32_t *at)
{
    enum trash_kind k;
    *at = 0;
    if (len == 0 || len > TRASH_PATH_MAX || path[0] != '/')
        return TR_ERROR;
    for (uint32_t i = 0; i < len; i++)
        scratch[i] = path[i];
    scratch[len] = 0;
    uint32_t last = len;                /* the index after the last '/' */
    while (last > 0 && path[last - 1] != '/')
        last--;
    for (uint32_t j = 1; j < last; j++) {       /* each folder of the path, from the root */
        if (path[j] != '/')
            continue;
        scratch[j] = 0;
        k = lstat(ctx, scratch);
        scratch[j] = '/';
        *at = j;
        if (k == TRASH_K_NONE)
            return TR_MAKE_PARENTS;
        if (k == TRASH_K_ERROR)
            return TR_ERROR;
        if (k != TRASH_K_DIR)
            return TR_PARENT_NOT_FOLDER;
    }
    k = lstat(ctx, scratch);
    *at = len;
    if (k == TRASH_K_NONE)
        return TR_OK;
    if (k == TRASH_K_ERROR)
        return TR_ERROR;
    return TR_EXISTS;
}
