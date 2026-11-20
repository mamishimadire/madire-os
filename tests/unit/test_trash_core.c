/* Unit tests for the Trash's pure part, kernel/files/trash_core.c (1.5
 * stage 11; docs/plans/1.5.md, C6 with addition a, C6c rule SL, risks 55,
 * 63 and 66): every named ti_* case of the plan, the slot names, the file
 * Madire writes and its reading back, and Restore's checks by a fake lstat
 * (a link is never followed). Each literal was checked by hand (T4) and
 * against tests/files_ref.py. */
#include <stdint.h>
#include <string.h>
#include "kernel/files/trash_core.c"
#include "tests/unit/unit.h"

static struct trash_info ti;
static char big[TRASH_INFO_MAX + 64];

static enum trash_info_result parse(const char *s)
{
    return trash_info_parse((const uint8_t *)s, (uint32_t)strlen(s), &ti);
}

/* "[Trash Info]\nPath=<value>\n" in big. */
static enum trash_info_result parse_path(const char *value)
{
    strcpy(big, "[Trash Info]\nPath=");
    strcat(big, value);
    strcat(big, "\nDeletionDate=2026-11-20T10:20:30\n");
    return parse(big);
}

static void ti_ok_simple(void)
{
    CHECK_EQ(parse("[Trash Info]\nPath=/home/user/Documents/a.txt\nDeletionDate=2026-11-20T10:20:30\n"), TI_OK);
    CHECK(strcmp(ti.path, "/home/user/Documents/a.txt") == 0);
    CHECK_EQ(ti.path_len, 26);
    CHECK(ti.date_ok);
    CHECK_EQ(ti.date.year, 2026);
    CHECK_EQ(ti.date.month, 11);
    CHECK_EQ(ti.date.day, 20);
    CHECK_EQ(ti.date.hour, 10);
    CHECK_EQ(ti.date.minute, 20);
    CHECK_EQ(ti.date.second, 30);
    /* empty lines before the header, no newline at the end, other lines ignored */
    CHECK_EQ(parse("\n\n[Trash Info]\nX=1\nPath=/home/user/x"), TI_OK);
    CHECK(strcmp(ti.path, "/home/user/x") == 0);
    CHECK(!ti.date_ok);
}

static void ti_ok_encoded_space(void)
{
    CHECK_EQ(parse_path("/home/user/My%20File.txt"), TI_OK);
    CHECK(strcmp(ti.path, "/home/user/My File.txt") == 0);
    CHECK_EQ(parse_path("/home/user/My File.txt"), TI_OK);   /* unescaped bytes are kept */
    CHECK(strcmp(ti.path, "/home/user/My File.txt") == 0);
    CHECK_EQ(parse_path("/home/user/%41%62"), TI_OK);        /* hex in either case */
    CHECK(strcmp(ti.path, "/home/user/Ab") == 0);
}

static void ti_ok_utf8_bytes(void)
{
    CHECK_EQ(parse_path("/home/user/caf%C3%A9"), TI_OK);
    CHECK_EQ(ti.path_len, 16);
    CHECK(memcmp(ti.path, "/home/user/caf\xc3\xa9", 16) == 0);
    CHECK_EQ(parse_path("/home/user/caf\xc3\xa9"), TI_OK);
    CHECK_EQ(ti.path_len, 16);
}

static void ti_bad_header(void)
{
    CHECK_EQ(parse("[Trash info]\nPath=/home/user/x\n"), TI_BAD_HEADER);
    CHECK_EQ(parse("Path=/home/user/x\n[Trash Info]\n"), TI_BAD_HEADER);
    CHECK_EQ(parse("[Trash Info] \nPath=/home/user/x\n"), TI_BAD_HEADER);
    CHECK_EQ(parse(""), TI_BAD_HEADER);
    CHECK_EQ(parse("\n\n"), TI_BAD_HEADER);
}

static void ti_no_path(void)
{
    CHECK_EQ(parse("[Trash Info]\nDeletionDate=2026-11-20T10:20:30\n"), TI_NO_PATH);
    CHECK_EQ(parse("[Trash Info]\npath=/home/user/x\n"), TI_NO_PATH);
    CHECK_EQ(parse("[Trash Info]\n Path=/home/user/x\n"), TI_NO_PATH);
}

static void ti_relative_path(void)
{
    CHECK_EQ(parse_path("home/user/x"), TI_RELATIVE);
    CHECK_EQ(parse_path(""), TI_RELATIVE);
    CHECK_EQ(parse_path("%2Fhome/user/x"), TI_ESCAPED_SLASH);
}

static void ti_outside_home(void)
{
    CHECK_EQ(parse_path("/etc/passwd"), TI_OUTSIDE_HOME);
    CHECK_EQ(parse_path("/home/user"), TI_OUTSIDE_HOME);
    CHECK_EQ(parse_path("/home/username/x"), TI_OUTSIDE_HOME);
    CHECK_EQ(parse_path("/home/other/x"), TI_OUTSIDE_HOME);
    CHECK_EQ(parse_path("/"), TI_OUTSIDE_HOME);
}

static void ti_dotdot_component(void)
{
    CHECK_EQ(parse_path("/home/user/../../etc/passwd"), TI_DOT_COMPONENT);
    CHECK_EQ(parse_path("/home/user/%2E%2E/x"), TI_DOT_COMPONENT);
    CHECK_EQ(parse_path("/home/user/x/.."), TI_DOT_COMPONENT);
    CHECK_EQ(parse_path("/home/user/..x"), TI_OK);           /* a name that starts with two dots */
}

static void ti_dot_component(void)
{
    CHECK_EQ(parse_path("/home/user/./x"), TI_DOT_COMPONENT);
    CHECK_EQ(parse_path("/home/user/%2E"), TI_DOT_COMPONENT);
    CHECK_EQ(parse_path("/home/user/.x"), TI_OK);
}

static void ti_empty_component(void)
{
    CHECK_EQ(parse_path("/home/user//x"), TI_EMPTY_COMPONENT);
    CHECK_EQ(parse_path("//home/user/x"), TI_EMPTY_COMPONENT);
}

static void ti_trailing_slash(void)
{
    CHECK_EQ(parse_path("/home/user/x/"), TI_TRAILING_SLASH);
    CHECK_EQ(parse_path("/home/user/"), TI_TRAILING_SLASH);
}

static void ti_escape_short(void)
{
    CHECK_EQ(parse_path("/home/user/a%4"), TI_BAD_ESCAPE);
    CHECK_EQ(parse_path("/home/user/a%"), TI_BAD_ESCAPE);
}

static void ti_escape_nonhex(void)
{
    CHECK_EQ(parse_path("/home/user/a%G1"), TI_BAD_ESCAPE);
    CHECK_EQ(parse_path("/home/user/a%1g"), TI_BAD_ESCAPE);
    CHECK_EQ(parse_path("/home/user/a% 1"), TI_BAD_ESCAPE);
}

static void ti_escape_nul(void)
{
    static const char raw[] = "[Trash Info]\nPath=/home/user/a\0b\n";
    CHECK_EQ(parse_path("/home/user/a%00b"), TI_NUL);
    CHECK_EQ(trash_info_parse((const uint8_t *)raw, sizeof(raw) - 1, &ti), TI_NUL);
}

static void ti_escape_slash(void)
{
    CHECK_EQ(parse_path("/home/user/a%2Fb"), TI_ESCAPED_SLASH);
    CHECK_EQ(parse_path("/home/user/a%2fb"), TI_ESCAPED_SLASH);
}

static void ti_component_too_long(void)
{
    char v[300];
    strcpy(v, "/home/user/");
    memset(v + 11, 'c', 256);
    v[11 + 256] = 0;
    CHECK_EQ(parse_path(v), TI_COMPONENT_TOO_LONG);
    v[11 + 255] = 0;
    CHECK_EQ(parse_path(v), TI_OK);
    CHECK_EQ(ti.path_len, 266);
}

static void ti_path_too_long(void)
{
    /* 16 components of 250 bytes and one of 100: 4,127 bytes decoded, a
     * line of 4,132 (under the line limit) */
    static char v[4200];
    strcpy(v, "/home/user/");
    for (int i = 0; i < 16; i++) {
        memset(v + strlen(v), 'q', 250);
        v[11 + (i + 1) * 251 - 1] = 0;
        strcat(v, "/");
    }
    memset(v + strlen(v), 'r', 100);
    v[4127] = 0;
    CHECK_EQ(strlen(v), 4127);
    CHECK_EQ(parse_path(v), TI_PATH_TOO_LONG);
    v[4096] = 0;                        /* exactly 4,096 bytes: allowed */
    CHECK_EQ(parse_path(v), TI_OK);
    CHECK_EQ(ti.path_len, 4096);
}

static void ti_line_too_long(void)
{
    strcpy(big, "[Trash Info]\nPath=/home/user/x\n#");
    size_t at = strlen(big);
    memset(big + at, 'z', 4160);        /* a line of 4,161 bytes */
    big[at + 4160] = '\n';
    big[at + 4161] = 0;
    CHECK_EQ(parse(big), TI_LINE_TOO_LONG);
    big[at + 4159] = '\n';              /* 4,160 bytes: allowed */
    big[at + 4160] = 0;
    CHECK_EQ(parse(big), TI_OK);
    /* before the header too */
    memset(big, 'z', 4161);
    strcpy(big + 4161, "\n[Trash Info]\nPath=/home/user/x\n");
    CHECK_EQ(parse(big), TI_LINE_TOO_LONG);
}

static void ti_file_too_large(void)
{
    /* the header and Path, two comment lines of 4,000 bytes, then filler */
    strcpy(big, "[Trash Info]\nPath=/home/user/x\n");
    size_t at = strlen(big);
    for (int i = 0; i < 2; i++) {
        memset(big + at, '#', 4000);
        big[at + 4000] = '\n';
        at += 4001;
    }
    memset(big + at, '#', TRASH_INFO_MAX - at);
    CHECK_EQ(trash_info_parse((const uint8_t *)big, TRASH_INFO_MAX, &ti), TI_OK);
    big[TRASH_INFO_MAX] = '#';
    CHECK_EQ(trash_info_parse((const uint8_t *)big, TRASH_INFO_MAX + 1, &ti), TI_FILE_TOO_LARGE);
}

static void ti_inside_trash(void)
{
    CHECK_EQ(parse_path("/home/user/.local/share/Trash"), TI_INSIDE_TRASH);
    CHECK_EQ(parse_path("/home/user/.local/share/Trash/files/x"), TI_INSIDE_TRASH);
    CHECK_EQ(parse_path("/home/user/.local/share/Trash/info/x.trashinfo"), TI_INSIDE_TRASH);
    CHECK_EQ(parse_path("/home/user/.local/share/Trashcan"), TI_OK);
    CHECK_EQ(parse_path("/home/user/.local/share"), TI_OK);
}

static void ti_duplicate_keys_first_wins(void)
{
    CHECK_EQ(parse("[Trash Info]\nPath=/home/user/first\nPath=/etc/second\n"
                   "DeletionDate=2026-01-02T03:04:05\nDeletionDate=bad\n"), TI_OK);
    CHECK(strcmp(ti.path, "/home/user/first") == 0);
    CHECK(ti.date_ok);
    CHECK_EQ(ti.date.day, 2);
    CHECK_EQ(parse("[Trash Info]\nPath=/etc/first\nPath=/home/user/second\n"), TI_OUTSIDE_HOME);
}

static void ti_bad_date_restorable(void)
{
    static const char *const bad[] = {
        "2026-02-30T10:00:00", "2026-13-01T10:00:00", "2026-00-01T10:00:00", "2026-01-01T24:00:00",
        "2026-01-01T10:60:00", "2026-01-01T10:00:60", "2026-01-01 10:00:00", "2026-1-01T10:00:00",
        "0000-01-01T10:00:00", "2026-01-01T10:00:00Z", "2023-02-29T10:00:00", "2100-02-29T00:00:00",
    };
    for (unsigned i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        sprintf(big, "[Trash Info]\nPath=/home/user/x\nDeletionDate=%s\n", bad[i]);
        CHECK_EQ(parse(big), TI_OK);
        CHECK(!ti.date_ok);
    }
    CHECK_EQ(parse("[Trash Info]\nPath=/home/user/x\nDeletionDate=2024-02-29T23:59:59\n"), TI_OK);
    CHECK(ti.date_ok);
    CHECK_EQ(parse("[Trash Info]\nPath=/home/user/x\nDeletionDate=2000-02-29T00:00:00\n"), TI_OK);
    CHECK(ti.date_ok);
}

static void ti_encode_roundtrip(void)
{
    static char enc[3 * TRASH_PATH_MAX + 1], out[TRASH_INFO_MAX + 8], p[4200];
    const char *path = "/home/user/My File (1)%.txt";
    CHECK_EQ(trash_path_encode(path, (uint32_t)strlen(path), enc, sizeof(enc)), 33);
    CHECK(strcmp(enc, "/home/user/My%20File%20(1)%25.txt") == 0);
    CHECK_EQ(trash_path_encode(path, (uint32_t)strlen(path), enc, 33), 0);     /* no room for the NUL */
    CHECK_EQ(trash_path_encode(path, (uint32_t)strlen(path), enc, 34), 33);
    struct trash_date d = { 2026, 11, 20, 9, 5, 7 };
    uint32_t n = trash_info_build(path, (uint32_t)strlen(path), &d, out, sizeof(out));
    CHECK_EQ(n, 85);
    CHECK(strcmp(out, "[Trash Info]\nPath=/home/user/My%20File%20(1)%25.txt\nDeletionDate=2026-11-20T09:05:07\n") == 0);
    CHECK_EQ(trash_info_parse((const uint8_t *)out, n, &ti), TI_OK);
    CHECK(strcmp(ti.path, path) == 0);
    CHECK(ti.date_ok && ti.date.hour == 9 && ti.date.second == 7);
    /* every byte from 1 to 255 in one name, back again */
    strcpy(p, "/home/user/");
    uint32_t k = 11;
    for (int c = 1; c < 256; c++)
        if (c != '/')
            p[k++] = (char)c;
    p[k] = 0;
    n = trash_info_build(p, k, &d, out, sizeof(out));
    CHECK(n > 0);
    CHECK_EQ(trash_info_parse((const uint8_t *)out, n, &ti), TI_OK);
    CHECK_EQ(ti.path_len, k);
    CHECK(memcmp(ti.path, p, k) == 0);
    /* refused: what reading would refuse */
    CHECK_EQ(trash_info_build("/etc/x", 6, &d, out, sizeof(out)), 0);
    CHECK_EQ(trash_info_build("/home/user/x", 12, &(struct trash_date){ 2026, 2, 30, 0, 0, 0 }, out, sizeof(out)), 0);
    CHECK_EQ(trash_info_build("/home/user/a\0b", 14, &d, out, sizeof(out)), 0);
    /* 1,400 bytes outside the kept set encode to 4,200: over the line limit */
    memset(p + 11, ' ', 1400);
    for (int i = 0; i < 6; i++)
        p[11 + 200 * (i + 1)] = '/';
    p[1411] = 0;
    CHECK_EQ(trash_info_build(p, 1411, &d, out, sizeof(out)), 0);
    p[11 + 1200] = 0;                   /* 1,211 bytes: 3,601 encoded, fits */
    CHECK(trash_info_build(p, 1211, &d, out, sizeof(out)) > 0);
    CHECK_EQ(trash_info_build(path, (uint32_t)strlen(path), &d, out, 85), 0);  /* no room for the NUL */
}

static void ti_slot_names(void)
{
    static char name[300], out[TRASH_NAME_MAX + 1];
    CHECK_EQ(trash_slot_name("a.txt", 5, 1, out), 5);
    CHECK(strcmp(out, "a.txt") == 0);
    CHECK_EQ(trash_slot_name("a.txt", 5, 2, out), 7);
    CHECK(strcmp(out, "a.txt.2") == 0);
    CHECK_EQ(trash_slot_name("a.txt", 5, 999, out), 9);
    CHECK(strcmp(out, "a.txt.999") == 0);
    CHECK_EQ(trash_slot_name("a.txt", 5, 0, out), 0);
    CHECK_EQ(trash_slot_name("a.txt", 5, 1000, out), 0);
    CHECK_EQ(trash_slot_name("a/b", 3, 1, out), 0);
    memset(name, 'n', 255);
    CHECK_EQ(trash_slot_name(name, 255, 1, out), 245);              /* 245 + ".trashinfo" = 255 */
    CHECK_EQ(trash_slot_name(name, 255, 2, out), 245);
    CHECK(memcmp(out + 243, ".2", 2) == 0);
    CHECK_EQ(trash_slot_name(name, 255, 100, out), 245);
    CHECK(memcmp(out + 241, ".100", 4) == 0);
    CHECK_EQ(trash_slot_name(name, 256, 1, out), 0);
}

/* ---- Restore: a fake lstat over a list of paths ---- */

static const char *const *fake_paths;
static const enum trash_kind *fake_kinds;
static int fake_calls;
static char fake_seen[16][TRASH_PATH_MAX + 1];

static enum trash_kind fake_lstat(void *ctx, const char *path)
{
    (void)ctx;
    if (fake_calls < 16) {
        size_t n = strnlen(path, TRASH_PATH_MAX);      /* at most 4,096 bytes and the NUL */
        memcpy(fake_seen[fake_calls], path, n);
        fake_seen[fake_calls][n] = 0;
    }
    fake_calls++;
    for (int i = 0; fake_paths[i]; i++)
        if (strcmp(fake_paths[i], path) == 0)
            return fake_kinds[i];
    return TRASH_K_NONE;
}

static const char *const tree_paths[] = {
    "/home", "/home/user", "/home/user/Docs", "/home/user/real", "/home/user/real/a.txt", "/home/user/file",
    "/home/user/bad", 0
};
static const enum trash_kind tree_kinds[] = {
    TRASH_K_DIR, TRASH_K_DIR, TRASH_K_LINK, TRASH_K_DIR, TRASH_K_FILE, TRASH_K_FILE, TRASH_K_ERROR
};

static enum trash_restore restore(const char *path, uint32_t *at)
{
    static char scratch[TRASH_PATH_MAX + 1];
    fake_paths = tree_paths;
    fake_kinds = tree_kinds;
    fake_calls = 0;
    return trash_restore_check(path, (uint32_t)strlen(path), fake_lstat, 0, scratch, at);
}

static void ti_restore_through_symlink(void)
{
    uint32_t at = 0;
    /* /home/user/Docs is a link: refused there, never looked through */
    CHECK_EQ(restore("/home/user/Docs/a.txt", &at), TR_PARENT_NOT_FOLDER);
    CHECK_EQ(at, 15);
    CHECK_EQ(fake_calls, 3);
    CHECK(strcmp(fake_seen[2], "/home/user/Docs") == 0);
    /* the item itself a link is only a name already used */
    CHECK_EQ(restore("/home/user/Docs", &at), TR_EXISTS);
    CHECK_EQ(at, 15);
    /* a file as a folder */
    CHECK_EQ(restore("/home/user/file/x", &at), TR_PARENT_NOT_FOLDER);
    CHECK_EQ(at, 15);
    /* the plain cases */
    CHECK_EQ(restore("/home/user/real/b.txt", &at), TR_OK);
    CHECK_EQ(at, 21);
    CHECK_EQ(restore("/home/user/real/a.txt", &at), TR_EXISTS);
    CHECK_EQ(restore("/home/user/bad/x", &at), TR_ERROR);
    CHECK_EQ(at, 14);
    CHECK_EQ(restore("home/user/x", &at), TR_ERROR);
}

static void ti_restore_parent_is_link(void)
{
    uint32_t at = 0;
    /* making the missing folder New under the link Docs is refused */
    CHECK_EQ(restore("/home/user/Docs/New/a.txt", &at), TR_PARENT_NOT_FOLDER);
    CHECK_EQ(at, 15);
    CHECK_EQ(fake_calls, 3);
    /* a missing folder under real folders is made again from its start */
    CHECK_EQ(restore("/home/user/Gone/Deeper/a.txt", &at), TR_MAKE_PARENTS);
    CHECK_EQ(at, 15);
    CHECK_EQ(fake_calls, 3);
}

int main(void)
{
    printf("trash_core unit tests\n");
    RUN(ti_ok_simple);
    RUN(ti_ok_encoded_space);
    RUN(ti_ok_utf8_bytes);
    RUN(ti_bad_header);
    RUN(ti_no_path);
    RUN(ti_relative_path);
    RUN(ti_outside_home);
    RUN(ti_dotdot_component);
    RUN(ti_dot_component);
    RUN(ti_empty_component);
    RUN(ti_trailing_slash);
    RUN(ti_escape_short);
    RUN(ti_escape_nonhex);
    RUN(ti_escape_nul);
    RUN(ti_escape_slash);
    RUN(ti_component_too_long);
    RUN(ti_path_too_long);
    RUN(ti_line_too_long);
    RUN(ti_file_too_large);
    RUN(ti_inside_trash);
    RUN(ti_duplicate_keys_first_wins);
    RUN(ti_bad_date_restorable);
    RUN(ti_encode_roundtrip);
    RUN(ti_slot_names);
    RUN(ti_restore_through_symlink);
    RUN(ti_restore_parent_is_link);
    return unit_report("trash_core");
}
