/* System-call numbers and argument rules (see syscall_core.h). */
#include "kernel/proc/syscall_core.h"

#define NSEC_PER_SEC 1000000000ll

enum syscall_id syscall_lookup(uint64_t nr)
{
    switch (nr) {
    case SYS_READ:        return SC_READ;
    case SYS_WRITE:       return SC_WRITE;
    case SYS_CLOSE:       return SC_CLOSE;
    case SYS_DUP:         return SC_DUP;
    case SYS_DUP2:        return SC_DUP2;
    case SYS_SCHED_YIELD: return SC_SCHED_YIELD;
    case SYS_NANOSLEEP:   return SC_NANOSLEEP;
    case SYS_GETPID:      return SC_GETPID;
    case SYS_EXIT:        return SC_EXIT;
    case SYS_EXIT_GROUP:  return SC_EXIT_GROUP;
    case SYS_OPEN:        return SC_OPEN;
    case SYS_STAT:        return SC_STAT;
    case SYS_FSTAT:       return SC_FSTAT;
    case SYS_LSTAT:       return SC_LSTAT;
    case SYS_LSEEK:       return SC_LSEEK;
    case SYS_FTRUNCATE:   return SC_FTRUNCATE;
    case SYS_OPENAT:      return SC_OPENAT;
    case SYS_NEWFSTATAT:  return SC_NEWFSTATAT;
    case SYS_GETCWD:      return SC_GETCWD;
    case SYS_CHDIR:       return SC_CHDIR;
    case SYS_RENAME:      return SC_RENAME;
    case SYS_MKDIR:       return SC_MKDIR;
    case SYS_RMDIR:       return SC_RMDIR;
    case SYS_UNLINK:      return SC_UNLINK;
    case SYS_GETDENTS64:  return SC_GETDENTS64;
    case SYS_MKDIRAT:     return SC_MKDIRAT;
    case SYS_UNLINKAT:    return SC_UNLINKAT;
    default:              return SC_UNKNOWN;
    }
}

bool syscall_is_madire(uint64_t nr)
{
    return nr >= SYS_MADIRE_FIRST && nr <= SYS_MADIRE_LAST;
}

const char *syscall_name(enum syscall_id id)
{
    switch (id) {
    case SC_UNKNOWN:     return "unknown";
    case SC_WRITE:       return "write";
    case SC_SCHED_YIELD: return "sched_yield";
    case SC_NANOSLEEP:   return "nanosleep";
    case SC_GETPID:      return "getpid";
    case SC_EXIT:        return "exit";
    case SC_EXIT_GROUP:  return "exit_group";
    case SC_READ:        return "read";
    case SC_CLOSE:       return "close";
    case SC_DUP:         return "dup";
    case SC_DUP2:        return "dup2";
    case SC_OPEN:        return "open";
    case SC_STAT:        return "stat";
    case SC_FSTAT:       return "fstat";
    case SC_LSTAT:       return "lstat";
    case SC_LSEEK:       return "lseek";
    case SC_FTRUNCATE:   return "ftruncate";
    case SC_OPENAT:      return "openat";
    case SC_NEWFSTATAT:  return "newfstatat";
    case SC_GETCWD:      return "getcwd";
    case SC_CHDIR:       return "chdir";
    case SC_RENAME:      return "rename";
    case SC_MKDIR:       return "mkdir";
    case SC_RMDIR:       return "rmdir";
    case SC_UNLINK:      return "unlink";
    case SC_GETDENTS64:  return "getdents64";
    case SC_MKDIRAT:     return "mkdirat";
    case SC_UNLINKAT:    return "unlinkat";
    case SC_COUNT:       break;
    }
    return "unknown";
}

uint64_t syscall_console_count(uint64_t count)
{
    return count < SYSCALL_WRITE_MAX ? count : SYSCALL_WRITE_MAX;
}

int64_t syscall_timespec_ticks(int64_t sec, int64_t nsec, uint32_t hz, uint32_t *ticks)
{
    if (sec < 0 || nsec < 0 || nsec >= NSEC_PER_SEC || hz == 0)
        return -MADIRE_EINVAL;
    /* ticks = ceil(sec * hz + nsec * hz / 1e9), without overflow: sec is
     * clamped first (any sec above UINT32_MAX already means the maximum). */
    uint64_t s = (uint64_t)sec > UINT32_MAX ? UINT32_MAX : (uint64_t)sec;
    uint64_t t = s * hz + ((uint64_t)nsec * hz + NSEC_PER_SEC - 1) / NSEC_PER_SEC;
    *ticks = t > UINT32_MAX ? UINT32_MAX : (uint32_t)t;
    return 0;
}

int32_t syscall_exit_status(uint64_t status)
{
    return (int32_t)(status & 0xFF);
}

/* "[pid N] " into out (at least 24 bytes); returns its length. */
static size_t prefix(char *out, int32_t pid)
{
    char digits[12];
    size_t nd = 0, n = 0;
    uint32_t v = pid < 0 ? 0u : (uint32_t)pid;
    do {
        digits[nd++] = (char)('0' + v % 10);
        v /= 10;
    } while (v);
    static const char head[] = "[pid ";
    for (size_t i = 0; head[i]; i++)
        out[n++] = head[i];
    while (nd)
        out[n++] = digits[--nd];
    out[n++] = ']';
    out[n++] = ' ';
    return n;
}

uint32_t syscall_write_lines(int32_t pid, const uint8_t *buf, size_t n,
                             syscall_emit_fn emit, void *ctx)
{
    char line[24 + SYSCALL_LINE_MAX];
    size_t head = prefix(line, pid);
    size_t len = head;
    uint32_t lines = 0;
    for (size_t i = 0; i < n; i++) {
        uint8_t c = buf[i];
        if (c == '\n') {
            emit(ctx, line, len);
            lines++;
            len = head;
            continue;
        }
        if (len - head == SYSCALL_LINE_MAX) {
            emit(ctx, line, len);
            lines++;
            len = head;
        }
        line[len++] = (c >= 0x20 && c <= 0x7E) ? (char)c : '?';
    }
    if (len > head) {
        emit(ctx, line, len);
        lines++;
    }
    return lines;
}

#define RFLAGS_KEEP (0x1ull | 0x4ull | 0x10ull | 0x40ull | 0x80ull | 0x100ull | 0x400ull | \
                     0x800ull | 0x40000ull | 0x200000ull)  /* CF PF AF ZF SF TF DF OF AC ID */
#define RFLAGS_IF   0x200ull
#define RFLAGS_RES1 0x2ull

uint64_t syscall_user_rflags(uint64_t rflags)
{
    return (rflags & RFLAGS_KEEP) | RFLAGS_IF | RFLAGS_RES1;
}
