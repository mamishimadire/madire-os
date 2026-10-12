#ifndef MADIRE_FS_PATH_CORE_H
#define MADIRE_FS_PATH_CORE_H

/*
 * Paths (1.0), pure: tests/unit/test_path.c. The rules (docs/plans/1.0.md,
 * B4):
 *   - "/" first: absolute, from the root; anything else is relative to a
 *     start folder (the current folder, or an *at call's folder);
 *   - repeated '/' count as one; a trailing '/' asks for a folder;
 *   - a name is 1 to PATH_NAME_MAX bytes, any byte but '/' and NUL;
 *   - a path is at most PATH_MAX_BYTES bytes including its NUL;
 *   - "." and ".." are names here; the lookup (vfs_core.c) gives them their
 *     meaning, so a path is walked in a loop, never by recursion;
 *   - the empty path is ENOENT.
 *
 * A path is taken apart in place, one name at a time: nothing is copied and
 * nothing is allocated.
 */

#include <stdbool.h>
#include <stddef.h>

#define PATH_NAME_MAX   255u            /* NAME_MAX */
#define PATH_MAX_BYTES  4096u           /* PATH_MAX, the NUL included */

struct path_iter {
    const char *path;
    size_t len;                         /* without the NUL */
    size_t pos;                         /* where the next name is looked for */
    bool absolute;
    bool trailing_slash;                /* ends in '/' after at least one name */
};

/* Checks the length and starts the walk: 0, -FS_ENOENT (empty) or
 * -FS_ENAMETOOLONG (no NUL within PATH_MAX_BYTES bytes). */
int path_begin(struct path_iter *it, const char *path);

/* The next name: 1 with *name and *len set, 0 when no name is left, or
 * -FS_ENAMETOOLONG for a name over PATH_NAME_MAX bytes. */
int path_next(struct path_iter *it, const char **name, size_t *len);

/* True when no name follows the one path_next returned last. */
bool path_at_end(const struct path_iter *it);

enum path_dot { PATH_NAME, PATH_DOT, PATH_DOTDOT };

/* "." , ".." or an ordinary name. */
enum path_dot path_dot_kind(const char *name, size_t len);

/* True when the two names are the same bytes. */
bool path_name_eq(const char *a, size_t alen, const char *b, size_t blen);

#endif
