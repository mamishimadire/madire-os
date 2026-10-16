/*
 * Paths (1.0), pure: see path_core.h. The host unit tests build this file
 * too (tests/unit/test_path.c).
 */
#include "kernel/fs/path_core.h"
#include "kernel/fs/fs_errno.h"

const char *fs_errno_name(int err)
{
    switch (err < 0 ? -err : err) {
    case FS_ENOENT:       return "ENOENT";
    case FS_EBADF:        return "EBADF";
    case FS_ENOMEM:       return "ENOMEM";
    case FS_EFAULT:       return "EFAULT";
    case FS_EBUSY:        return "EBUSY";
    case FS_EEXIST:       return "EEXIST";
    case FS_ENOTDIR:      return "ENOTDIR";
    case FS_EISDIR:       return "EISDIR";
    case FS_EINVAL:       return "EINVAL";
    case FS_ENFILE:       return "ENFILE";
    case FS_EMFILE:       return "EMFILE";
    case FS_EFBIG:        return "EFBIG";
    case FS_ENOSPC:       return "ENOSPC";
    case FS_ESPIPE:       return "ESPIPE";
    case FS_ERANGE:       return "ERANGE";
    case FS_ENAMETOOLONG: return "ENAMETOOLONG";
    case FS_ENOSYS:       return "ENOSYS";
    case FS_ENOTEMPTY:    return "ENOTEMPTY";
    default:              return "E?";
    }
}

int path_begin(struct path_iter *it, const char *path)
{
    size_t n = 0;
    while (n < PATH_MAX_BYTES && path[n] != '\0')
        n++;
    it->path = path;
    it->len = n;
    it->pos = 0;
    it->absolute = n > 0 && path[0] == '/';
    it->trailing_slash = false;
    if (n == PATH_MAX_BYTES)
        return -FS_ENAMETOOLONG;        /* no NUL within PATH_MAX bytes */
    if (n == 0)
        return -FS_ENOENT;
    /* A trailing '/' after a name ("a/", "a//"); "/" alone has no name. */
    size_t end = n;
    while (end > 0 && path[end - 1] == '/')
        end--;
    it->trailing_slash = end > 0 && end < n;
    return 0;
}

int path_next(struct path_iter *it, const char **name, size_t *len)
{
    while (it->pos < it->len && it->path[it->pos] == '/')
        it->pos++;
    if (it->pos == it->len)
        return 0;
    size_t start = it->pos;
    while (it->pos < it->len && it->path[it->pos] != '/')
        it->pos++;
    *name = it->path + start;
    *len = it->pos - start;
    if (*len > PATH_NAME_MAX)
        return -FS_ENAMETOOLONG;
    return 1;
}

bool path_at_end(const struct path_iter *it)
{
    for (size_t i = it->pos; i < it->len; i++)
        if (it->path[i] != '/')
            return false;
    return true;
}

enum path_dot path_dot_kind(const char *name, size_t len)
{
    if (len == 1 && name[0] == '.')
        return PATH_DOT;
    if (len == 2 && name[0] == '.' && name[1] == '.')
        return PATH_DOTDOT;
    return PATH_NAME;
}

bool path_name_eq(const char *a, size_t alen, const char *b, size_t blen)
{
    if (alen != blen)
        return false;
    for (size_t i = 0; i < alen; i++)
        if (a[i] != b[i])
            return false;
    return true;
}
