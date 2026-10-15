/* fdops (1.0 stage 4, test=fdsys): the descriptor system calls, read,
 * write, close, dup and dup2, one case per arg. Cases 10 and up use the
 * files the kernel gives it at its start (fd 3, and fd 4 in case 13);
 * every file it reads holds, at each multiple of 8, that offset as a
 * little-endian 64-bit word. Exits with the number of wrong results,
 * except case 16 (see below).
 *   0  fds 0, 1, 2 at start        1  close           2  dup (lowest free)
 *   3  dup2                        4  EMFILE          5  console short count
 *   10 read 10000 bytes            11 write 9000 bytes
 *   12 two dup'd fds share one offset
 *   13 write to a read-only fd, read from a write-only fd
 *   14 read into bad buffers (EFAULT, the offset unchanged)
 *   15 the first of two readers sharing an open file: 40960 bytes
 *   16 the second: one 4096-byte read; exits with the offset it started
 *      at, in 4096-byte units (254, 255: a wrong result)
 *   17 one of three sharers: 64 reads of 4096 bytes */
#include "user/ulib.h"

static uint64_t wbuf[48 * 1024 / 8];
#define BUF ((uint8_t *)wbuf)
static const char ro[128] = { 1 };      /* read-only data: not writable */
static int bad;

static void want(const char *what, long got, long expected)
{
    bad += u_expect(what, got, expected);
}

/* BUF[0, n) holds the file's bytes from offset start on. */
static int bytes_from(uint64_t start, uint64_t n)
{
    for (uint64_t i = 0; i < n; i++) {
        uint64_t p = start + i;
        if (BUF[i] != (uint8_t)((p & ~7ull) >> (8 * (p & 7))))
            return 0;
    }
    return 1;
}

static void console_cases(long arg)
{
    if (arg == 0) {
        want("fdops 0: write(1): ", sys_write(1, "fdops: fd 1\n", 12), 12);
        want("fdops 0: write(2): ", sys_write(2, "fdops: fd 2\n", 12), 12);
        want("fdops 0: write(0): ", sys_write(0, BUF, 8), -EBADF);
        want("fdops 0: read(0): ", sys_read(0, BUF, 8), 0);
        want("fdops 0: read(1): ", sys_read(1, BUF, 8), -EBADF);
        want("fdops 0: read(2): ", sys_read(2, BUF, 8), -EBADF);
    } else if (arg == 1) {
        want("fdops 1: close(2): ", sys_close(2), 0);
        want("fdops 1: write(2) closed: ", sys_write(2, "fdops: closed\n", 14), -EBADF);
        want("fdops 1: close(2) again: ", sys_close(2), -EBADF);
        want("fdops 1: close(16): ", sys_close(16), -EBADF);
        want("fdops 1: close(-1): ", sys_close((uint64_t)-1), -EBADF);
        want("fdops 1: close(2^32+1): ", sys_close((1ull << 32) + 1), -EBADF);
        want("fdops 1: write(1): ", sys_write(1, "fdops: fd 1 still open\n", 23), 23);
    } else if (arg == 2) {
        want("fdops 2: dup(1): ", sys_dup(1), 3);
        want("fdops 2: write(3): ", sys_write(3, "fdops: through a dup\n", 21), 21);
        want("fdops 2: close(3): ", sys_close(3), 0);
        want("fdops 2: close(2): ", sys_close(2), 0);
        want("fdops 2: dup(1) after close(2): ", sys_dup(1), 2);
        want("fdops 2: write(2): ", sys_write(2, "fdops: fd 2 again\n", 18), 18);
    } else if (arg == 3) {
        want("fdops 3: dup2(1, 1): ", sys_dup2(1, 1), 1);
        want("fdops 3: dup(1): ", sys_dup(1), 3);
        want("fdops 3: dup2(0, 3): ", sys_dup2(0, 3), 3);
        want("fdops 3: write(3) now fd 0's: ", sys_write(3, BUF, 8), -EBADF);
        want("fdops 3: read(3) now fd 0's: ", sys_read(3, BUF, 8), 0);
        want("fdops 3: dup2(1, 16): ", sys_dup2(1, 16), -EBADF);
        want("fdops 3: dup2(1, -1): ", sys_dup2(1, (uint64_t)-1), -EBADF);
        want("fdops 3: dup2(9, 2): ", sys_dup2(9, 2), -EBADF);
        want("fdops 3: write(2) kept: ", sys_write(2, "fdops: fd 2 kept\n", 17), 17);
    } else if (arg == 4) {
        for (long fd = 3; fd < 16; fd++)
            want("fdops 4: dup(1): ", sys_dup(1), fd);
        want("fdops 4: dup(1) with 16 open: ", sys_dup(1), -EMFILE);
        want("fdops 4: close(7): ", sys_close(7), 0);
        want("fdops 4: dup(1) after close(7): ", sys_dup(1), 7);
        want("fdops 4: write(15): ", sys_write(15, "fdops: fd 15\n", 13), 13);
        for (uint64_t fd = 3; fd < 16; fd++)
            want("fdops 4: close: ", sys_close(fd), 0);
    } else if (arg == 5) {
        for (unsigned i = 0; i < 5000; i++)
            BUF[i] = (uint8_t)('a' + i % 26);
        want("fdops 5: dup(1): ", sys_dup(1), 3);
        want("fdops 5: write(3) 5000 bytes: ", sys_write(3, BUF, 5000), 4096);
    } else {
        bad = 99;
    }
}

static int file_cases(long arg)
{
    if (arg == 10) {
        want("fdops 10: read 10000: ", sys_read(3, BUF, 10000), 10000);
        want("fdops 10: bytes: ", bytes_from(0, 10000), 1);
        want("fdops 10: read at the end: ", sys_read(3, BUF, 10), 0);
    } else if (arg == 11) {
        for (unsigned i = 0; i < 9000; i++)
            BUF[i] = (uint8_t)(i * 13 + 5);
        want("fdops 11: write 9000: ", sys_write(3, BUF, 9000), 9000);
    } else if (arg == 12) {
        want("fdops 12: dup(3): ", sys_dup(3), 4);
        want("fdops 12: read(3) 100: ", sys_read(3, BUF, 100), 100);
        want("fdops 12: bytes 0-99: ", bytes_from(0, 100), 1);
        want("fdops 12: read(4) 100: ", sys_read(4, BUF, 100), 100);
        want("fdops 12: bytes 100-199: ", bytes_from(100, 100), 1);
    } else if (arg == 13) {
        want("fdops 13: write(3) read-only: ", sys_write(3, BUF, 8), -EBADF);
        want("fdops 13: read(4) write-only: ", sys_read(4, BUF, 8), -EBADF);
    } else if (arg == 14) {
        want("fdops 14: unmapped: ", sys_read(3, (void *)0x10000000ull, 100), -EFAULT);
        want("fdops 14: read-only data: ", sys_read(3, (void *)(uintptr_t)ro, 100), -EFAULT);
        want("fdops 14: kernel: ", sys_read(3, (void *)K_TEXT, 100), -EFAULT);
        want("fdops 14: read 100: ", sys_read(3, BUF, 100), 100);
        want("fdops 14: bytes 0-99 (offset unchanged): ", bytes_from(0, 100), 1);
    } else if (arg == 15) {
        want("fdops 15: read 40960: ", sys_read(3, BUF, 40960), 40960);
        want("fdops 15: bytes: ", bytes_from(0, 40960), 1);
    } else if (arg == 16) {
        if (sys_read(3, BUF, 4096) != 4096)
            return 255;
        uint64_t start = wbuf[0];
        if (start % 4096 || start / 4096 > 253 || !bytes_from(start, 4096))
            return 254;
        return (int)(start / 4096);
    } else if (arg == 17) {
        for (int k = 0; k < 64; k++) {
            long r = sys_read(3, BUF, 4096);
            uint64_t start = wbuf[0];
            if (r != 4096 || start % 4096 || !bytes_from(start, 4096))
                bad++;
        }
    } else {
        bad = 99;
    }
    return bad;
}

int main(long arg)
{
    if (arg >= 10)
        return file_cases(arg);
    console_cases(arg);
    return bad;
}
