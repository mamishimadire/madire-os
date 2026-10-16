/* bigio (1.0 stage 5, test=bigio and test=filecontend): large reads and
 * writes through the in-kernel chunk loop, one case per arg. A program
 * holds at most 64 KiB of data (item 2 of the stage 5 report), so the
 * largest single call is 61,440 bytes (15 chunks) and the 1 MiB and 2 MiB
 * files are written in 32 KiB calls. Exits with the number of wrong
 * results (each one logged), except the sharer (11).
 *   1  one 61,440-byte write and one 61,440-byte read
 *   2  a 1 MiB file: 32 writes and 32 reads of 32 KiB
 *   3  a 2 MiB file: 64 writes of 32 KiB      4  one more byte at 2 MiB: EFBIG
 *   5  a write crossing 2 MiB; then the 2 MiB file truncated to 0
 *   6  the budget (the kernel leaves 5 pages first)
 *   7  a write whose buffer turns unmapped after 6000 bytes
 *   8  a read whose buffer turns unmapped after 6000 bytes
 *   9, 10  writer 1 or 2: 8 writes of 32 KiB to fd 3 (an O_APPEND open file
 *      the kernel gives it); each 8-byte word: writer << 56 | write << 48 |
 *      its index in the write
 *   11 a sharer of fd 3 (a 1 MiB file holding each 8-byte word's offset):
 *      64 reads of 4096 bytes; exits with the offset of its first read in
 *      4096-byte units (255: a wrong result)
 *   12 a worker: /tmp/fc-<pid>, 256 KiB written and read back in 32 KiB calls
 *   13 the same, then twice a 2 MiB file written and truncated to 0
 *
 * The 61,440-byte buffer is the program's only bss object (everything
 * else is in .data or on the stack), so the last mapped data page ends
 * inside or just after it: cases 7 and 8 use its last 6000 bytes. */
#include "user/ulib.h"

#define BIG   61440u
#define BLOCK 32768u
#define DATA_DATA __attribute__((section(".data")))

extern char __bss_end[];                /* user/link.ld */

static uint64_t bigw[BIG / 8];
#define big ((uint8_t *)bigw)

static int bad DATA_DATA;
static struct stat st DATA_DATA;

static void want(const char *what, long got, long expected)
{
    bad += u_expect(what, got, expected);
}

static uint8_t pat(uint64_t pos, uint32_t seed)
{
    return (uint8_t)(pos * 7 + (pos >> 12) + seed);
}

static void fill(uint64_t pos, uint32_t len, uint32_t seed)
{
    for (uint32_t i = 0; i < len; i++)
        big[i] = pat(pos + i, seed);
}

static long holds(uint64_t pos, uint32_t len, uint32_t seed)
{
    for (uint32_t i = 0; i < len; i++)
        if (big[i] != pat(pos + i, seed))
            return 0;
    return 1;
}

static void clear(uint32_t len)
{
    for (uint32_t i = 0; i < len; i++)
        big[i] = 0;
}

static long fsize(uint64_t fd)
{
    return sys_fstat(fd, &st) == 0 ? st.st_size : -1;
}

/* The end of the last mapped data page. */
static uint64_t data_end(void)
{
    return ((uint64_t)(uintptr_t)__bss_end + 4095) & ~4095ull;
}

static void case_single(void)
{
    want("bigio 1: open: ", sys_open("/tmp/bio-1", O_CREAT | O_RDWR | O_TRUNC, 0644), 3);
    fill(0, BIG, 1);
    want("bigio 1: one 61440-byte write: ", sys_write(3, big, BIG), BIG);
    want("bigio 1: back to 0: ", sys_lseek(3, 0, SEEK_SET), 0);
    clear(BIG);
    want("bigio 1: one 61440-byte read: ", sys_read(3, big, BIG), BIG);
    want("bigio 1: bytes: ", holds(0, BIG, 1), 1);
    want("bigio 1: close: ", sys_close(3), 0);
}

static void case_mib(void)
{
    want("bigio 2: open: ", sys_open("/tmp/bio-m", O_CREAT | O_RDWR | O_TRUNC, 0644), 3);
    for (uint32_t i = 0; i < 32; i++) {
        fill((uint64_t)i * BLOCK, BLOCK, 2);
        want("bigio 2: write 32 KiB: ", sys_write(3, big, BLOCK), BLOCK);
    }
    want("bigio 2: size 1 MiB: ", fsize(3), 1 << 20);
    want("bigio 2: back to 0: ", sys_lseek(3, 0, SEEK_SET), 0);
    for (uint32_t i = 0; i < 32; i++) {
        clear(BLOCK);
        want("bigio 2: read 32 KiB: ", sys_read(3, big, BLOCK), BLOCK);
        want("bigio 2: bytes: ", holds((uint64_t)i * BLOCK, BLOCK, 2), 1);
    }
    want("bigio 2: read at the end: ", sys_read(3, big, BLOCK), 0);
    want("bigio 2: close: ", sys_close(3), 0);
}

static void case_limit(void)
{
    want("bigio 3: open: ", sys_open("/tmp/bio-l", O_CREAT | O_WRONLY | O_TRUNC, 0644), 3);
    for (uint32_t i = 0; i < 64; i++) {
        fill((uint64_t)i * BLOCK, BLOCK, 3);
        want("bigio 3: write 32 KiB: ", sys_write(3, big, BLOCK), BLOCK);
    }
    want("bigio 3: size 2 MiB: ", fsize(3), 2 << 20);
    want("bigio 3: close: ", sys_close(3), 0);
}

static void case_efbig(void)
{
    want("bigio 4: open: ", sys_open("/tmp/bio-l", O_WRONLY, 0), 3);
    want("bigio 4: to the end: ", sys_lseek(3, 0, SEEK_END), 2 << 20);
    want("bigio 4: one more byte: ", sys_write(3, big, 1), -EFBIG);
    want("bigio 4: size unchanged: ", fsize(3), 2 << 20);
    want("bigio 4: close: ", sys_close(3), 0);
}

static void case_cross(void)
{
    want("bigio 5: open: ", sys_open("/tmp/bio-l", O_WRONLY, 0), 3);
    want("bigio 5: 1000 below 2 MiB: ", sys_lseek(3, (2 << 20) - 1000, SEEK_SET), (2 << 20) - 1000);
    fill(0, BIG, 5);
    want("bigio 5: a 61440-byte write crossing 2 MiB: ", sys_write(3, big, BIG), 1000);
    want("bigio 5: size 2 MiB: ", fsize(3), 2 << 20);
    want("bigio 5: truncate the 2 MiB file to 0: ", sys_ftruncate(3, 0), 0);
    want("bigio 5: size 0: ", fsize(3), 0);
    want("bigio 5: no page left: ", st.st_blocks, 0);
    want("bigio 5: close: ", sys_close(3), 0);
}

static void case_budget(void)
{
    want("bigio 6: open: ", sys_open("/tmp/bio-b", O_CREAT | O_RDWR | O_TRUNC, 0644), 3);
    fill(0, BIG, 6);
    want("bigio 6: a 61440-byte write with 5 pages left: ", sys_write(3, big, BIG), 16384);
    want("bigio 6: the next write: ", sys_write(3, big, 4096), -ENOSPC);
    want("bigio 6: ftruncate growth: ", sys_ftruncate(3, 2 << 20), 0);
    want("bigio 6: size 2 MiB: ", fsize(3), 2 << 20);
    want("bigio 6: 5 pages (blocks 40): ", st.st_blocks, 40);
    want("bigio 6: close: ", sys_close(3), 0);
}

static long placed(void)
{
    uint64_t end = data_end(), b = (uint64_t)(uintptr_t)big;
    return b + 6000 <= end - 6000 && b + BIG <= end && end - (b + BIG) < 4096;
}

static void case_efault_write(void)
{
    if (!placed()) {
        bad = 250;
        return;
    }
    uint8_t *tail = (uint8_t *)(uintptr_t)(data_end() - 6000);
    want("bigio 7: open: ", sys_open("/tmp/bio-e", O_CREAT | O_RDWR | O_TRUNC, 0644), 3);
    for (uint32_t i = 0; i < 12000; i++)
        big[i] = (uint8_t)(i * 13 + 5);
    want("bigio 7: write 12000: ", sys_write(3, big, 12000), 12000);
    for (uint32_t i = 0; i < 6000; i++)
        tail[i] = (uint8_t)(i * 3 + 1);
    want("bigio 7: a 10000-byte write, unmapped after 6000: ",
         sys_write(3, tail, 10000), 6000);
    want("bigio 7: size 18000: ", fsize(3), 18000);
    want("bigio 7: offset 18000: ", sys_lseek(3, 0, SEEK_CUR), 18000);
    want("bigio 7: no valid byte: ", sys_write(3, (const void *)0x10000000ull, 100), -EFAULT);
    want("bigio 7: size unchanged: ", fsize(3), 18000);
    want("bigio 7: offset unchanged: ", sys_lseek(3, 0, SEEK_CUR), 18000);
    want("bigio 7: to 12000: ", sys_lseek(3, 12000, SEEK_SET), 12000);
    want("bigio 7: read the 6000 back: ", sys_read(3, big, 6000), 6000);
    long same = 1;
    for (uint32_t i = 0; i < 6000; i++)
        if (big[i] != (uint8_t)(i * 3 + 1))
            same = 0;
    want("bigio 7: the 6000 bytes before the bad page: ", same, 1);
    want("bigio 7: close: ", sys_close(3), 0);
}

static void case_efault_read(void)
{
    if (!placed()) {
        bad = 250;
        return;
    }
    uint8_t *tail = (uint8_t *)(uintptr_t)(data_end() - 6000);
    want("bigio 8: open: ", sys_open("/tmp/bio-e", O_RDONLY, 0), 3);
    want("bigio 8: a 10000-byte read, unmapped after 6000: ", sys_read(3, tail, 10000), 6000);
    want("bigio 8: offset 6000: ", sys_lseek(3, 0, SEEK_CUR), 6000);
    long same = 1;
    for (uint32_t i = 0; i < 6000; i++)
        if (tail[i] != (uint8_t)(i * 13 + 5))
            same = 0;
    want("bigio 8: the file's first 6000 bytes: ", same, 1);
    want("bigio 8: no valid byte: ", sys_read(3, (void *)0x10000000ull, 100), -EFAULT);
    want("bigio 8: offset unchanged: ", sys_lseek(3, 0, SEEK_CUR), 6000);
    want("bigio 8: close: ", sys_close(3), 0);
}

static void case_writer(uint64_t w)
{
    for (uint64_t c = 0; c < 8; c++) {
        for (uint64_t k = 0; k < BLOCK / 8; k++)
            bigw[k] = (w << 56) | (c << 48) | k;
        want("bigio writer: write 32 KiB: ", sys_write(3, big, BLOCK), BLOCK);
    }
}

static int case_sharer(void)
{
    uint64_t first = 0;
    for (int k = 0; k < 64; k++) {
        long r = sys_read(3, big, 4096);
        uint64_t start = bigw[0];
        if (k == 0)
            first = start;
        if (r != 4096 || start % 4096)
            return 255;
        for (uint64_t i = 0; i < 512; i++)
            if (bigw[i] != start + 8 * i)
                return 255;
    }
    return first / 4096 > 200 ? 255 : (int)(first / 4096);
}

/* "/tmp/fc-<n>" into name. */
static void fc_name(char *name, uint64_t n)
{
    const char *head = "/tmp/fc-";
    int p = 0;
    while (head[p]) {
        name[p] = head[p];
        p++;
    }
    char d[20];
    int nd = 0;
    do {
        d[nd++] = (char)('0' + n % 10);
        n /= 10;
    } while (n);
    while (nd)
        name[p++] = d[--nd];
    name[p] = '\0';
}

static void case_worker(int truncating)
{
    char name[32];
    uint64_t pid = (uint64_t)sys_getpid();
    uint32_t seed = (uint32_t)pid;
    fc_name(name, pid);
    want("bigio worker: open: ", sys_open(name, O_CREAT | O_RDWR | O_TRUNC, 0644), 3);
    for (uint32_t i = 0; i < 8; i++) {
        fill((uint64_t)i * BLOCK, BLOCK, seed);
        want("bigio worker: write 32 KiB: ", sys_write(3, big, BLOCK), BLOCK);
    }
    want("bigio worker: back to 0: ", sys_lseek(3, 0, SEEK_SET), 0);
    for (uint32_t i = 0; i < 8; i++) {
        clear(BLOCK);
        want("bigio worker: read 32 KiB: ", sys_read(3, big, BLOCK), BLOCK);
        want("bigio worker: bytes: ", holds((uint64_t)i * BLOCK, BLOCK, seed), 1);
    }
    want("bigio worker: close: ", sys_close(3), 0);
    if (!truncating)
        return;
    for (int round = 0; round < 2; round++) {
        want("bigio worker: open the 2 MiB file: ",
             sys_open("/tmp/fc-big", O_CREAT | O_RDWR | O_TRUNC, 0644), 3);
        for (uint32_t i = 0; i < 64; i++) {
            fill((uint64_t)i * BLOCK, BLOCK, 13);
            want("bigio worker: write 32 KiB: ", sys_write(3, big, BLOCK), BLOCK);
        }
        want("bigio worker: truncate 2 MiB to 0: ", sys_ftruncate(3, 0), 0);
        want("bigio worker: size 0: ", fsize(3), 0);
        want("bigio worker: no page left: ", st.st_blocks, 0);
        want("bigio worker: close: ", sys_close(3), 0);
    }
}

int main(int argc, char **argv)
{
    long arg = u_arg(argc, argv);
    switch (arg) {
    case 1: case_single(); break;
    case 2: case_mib(); break;
    case 3: case_limit(); break;
    case 4: case_efbig(); break;
    case 5: case_cross(); break;
    case 6: case_budget(); break;
    case 7: case_efault_write(); break;
    case 8: case_efault_read(); break;
    case 9: case_writer(1); break;
    case 10: case_writer(2); break;
    case 11: return case_sharer();
    case 12: case_worker(0); break;
    case 13: case_worker(1); break;
    default: bad = 99; break;
    }
    return bad > 255 ? 255 : bad;
}
