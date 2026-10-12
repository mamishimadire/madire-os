/* Unit tests for the 1.0 paths (kernel/fs/path_core.c): the length limits,
 * absolute and relative paths, repeated and trailing '/', "." and "..",
 * and the errno names. */
#include <string.h>
#include "kernel/fs/fs_errno.h"
#include "kernel/fs/path_core.h"
#include "tests/unit/unit.h"

static char big[PATH_MAX_BYTES + 16];

/* The names of path, joined by '|' ("" for none); the walk's result in *rc. */
static const char *names(const char *path, int *rc)
{
    static char out[PATH_MAX_BYTES + 16];
    struct path_iter it;
    size_t o = 0;
    out[0] = '\0';
    *rc = path_begin(&it, path);
    if (*rc < 0)
        return out;
    const char *n;
    size_t len;
    int r;
    while ((r = path_next(&it, &n, &len)) == 1) {
        if (o)
            out[o++] = '|';
        memcpy(out + o, n, len);
        o += len;
        out[o] = '\0';
    }
    *rc = r;
    return out;
}

static void test_split(void)
{
    int rc;
    CHECK(strcmp(names("/", &rc), "") == 0 && rc == 0);
    CHECK(strcmp(names("/a", &rc), "a") == 0 && rc == 0);
    CHECK(strcmp(names("a/b/c", &rc), "a|b|c") == 0 && rc == 0);
    CHECK(strcmp(names("//a///b//", &rc), "a|b") == 0 && rc == 0);
    CHECK(strcmp(names("./../.", &rc), ".|..|.") == 0 && rc == 0);
    CHECK(strcmp(names("a b/c\td", &rc), "a b|c\td") == 0 && rc == 0);   /* any byte but '/' */
    CHECK(strcmp(names("\x01\xff/x", &rc), "\x01\xff|x") == 0 && rc == 0);
}

static void test_absolute_and_trailing(void)
{
    struct path_iter it;
    CHECK_EQ(path_begin(&it, "/a"), 0);
    CHECK(it.absolute && !it.trailing_slash);
    CHECK_EQ(path_begin(&it, "a"), 0);
    CHECK(!it.absolute && !it.trailing_slash);
    CHECK_EQ(path_begin(&it, "a/"), 0);
    CHECK(!it.absolute && it.trailing_slash);
    CHECK_EQ(path_begin(&it, "/a//"), 0);
    CHECK(it.absolute && it.trailing_slash);
    CHECK_EQ(path_begin(&it, "/"), 0);
    CHECK(it.absolute && !it.trailing_slash);                   /* "/" has no name */
    CHECK_EQ(path_begin(&it, "///"), 0);
    CHECK(it.absolute && !it.trailing_slash);
    CHECK_EQ(path_begin(&it, "./"), 0);
    CHECK(!it.absolute && it.trailing_slash);
}

static void test_at_end(void)
{
    struct path_iter it;
    const char *n;
    size_t len;
    CHECK_EQ(path_begin(&it, "a/b//"), 0);
    CHECK_EQ(path_next(&it, &n, &len), 1);
    CHECK(!path_at_end(&it));
    CHECK_EQ(path_next(&it, &n, &len), 1);
    CHECK(len == 1 && n[0] == 'b');
    CHECK(path_at_end(&it));                                    /* only '/' left */
    CHECK_EQ(path_next(&it, &n, &len), 0);
    CHECK_EQ(path_next(&it, &n, &len), 0);                      /* stays at the end */
}

static void test_empty(void)
{
    struct path_iter it;
    CHECK_EQ(path_begin(&it, ""), -FS_ENOENT);
}

static void test_name_limit(void)
{
    int rc;
    memset(big, 0, sizeof(big));
    memset(big, 'n', PATH_NAME_MAX);                            /* 255: allowed */
    names(big, &rc);
    CHECK_EQ(rc, 0);
    big[PATH_NAME_MAX] = 'n';                                   /* 256 */
    names(big, &rc);
    CHECK_EQ(rc, -FS_ENAMETOOLONG);
    /* A long name after a good one: the good one comes first. */
    memset(big, 0, sizeof(big));
    big[0] = 'a';
    big[1] = '/';
    memset(big + 2, 'n', PATH_NAME_MAX + 1);
    struct path_iter it;
    const char *n;
    size_t len;
    CHECK_EQ(path_begin(&it, big), 0);
    CHECK_EQ(path_next(&it, &n, &len), 1);
    CHECK_EQ(path_next(&it, &n, &len), -FS_ENAMETOOLONG);
}

static void test_path_limit(void)
{
    struct path_iter it;
    memset(big, 0, sizeof(big));
    memset(big, '/', PATH_MAX_BYTES - 1);                       /* 4095 + NUL: allowed */
    CHECK_EQ(path_begin(&it, big), 0);
    CHECK_EQ(it.len, PATH_MAX_BYTES - 1);
    big[PATH_MAX_BYTES - 1] = '/';                              /* 4096: no room for the NUL */
    CHECK_EQ(path_begin(&it, big), -FS_ENAMETOOLONG);
    /* Never reads past PATH_MAX_BYTES: no NUL at all within the buffer. */
    memset(big, 'x', sizeof(big));
    CHECK_EQ(path_begin(&it, big), -FS_ENAMETOOLONG);
}

static void test_dots_and_names(void)
{
    CHECK_EQ(path_dot_kind(".", 1), PATH_DOT);
    CHECK_EQ(path_dot_kind("..", 2), PATH_DOTDOT);
    CHECK_EQ(path_dot_kind("...", 3), PATH_NAME);
    CHECK_EQ(path_dot_kind(".a", 2), PATH_NAME);
    CHECK_EQ(path_dot_kind("a", 1), PATH_NAME);
    CHECK(path_name_eq("abc", 3, "abc", 3));
    CHECK(!path_name_eq("abc", 3, "abd", 3));
    CHECK(!path_name_eq("abc", 3, "ab", 2));
    CHECK(path_name_eq("a/b", 1, "a", 1));                      /* by length, not NUL */
}

static void test_errno_names(void)
{
    CHECK(strcmp(fs_errno_name(-FS_ENOENT), "ENOENT") == 0);
    CHECK(strcmp(fs_errno_name(FS_ENOTEMPTY), "ENOTEMPTY") == 0);
    CHECK(strcmp(fs_errno_name(-FS_ENAMETOOLONG), "ENAMETOOLONG") == 0);
    CHECK(strcmp(fs_errno_name(-1), "E?") == 0);
    CHECK(strcmp(fs_errno_name(0), "E?") == 0);
    CHECK_EQ(FS_ENOENT, 2);                                     /* Linux's numbers */
    CHECK_EQ(FS_EEXIST, 17);
    CHECK_EQ(FS_ENOTDIR, 20);
    CHECK_EQ(FS_EISDIR, 21);
    CHECK_EQ(FS_EINVAL, 22);
    CHECK_EQ(FS_ENOSPC, 28);
    CHECK_EQ(FS_ENAMETOOLONG, 36);
    CHECK_EQ(FS_ENOTEMPTY, 39);
}

int main(void)
{
    RUN(test_split);
    RUN(test_absolute_and_trailing);
    RUN(test_at_end);
    RUN(test_empty);
    RUN(test_name_limit);
    RUN(test_path_limit);
    RUN(test_dots_and_names);
    RUN(test_errno_names);
    return unit_report("path");
}
