/* lines (0.11): output for the console tests (screen output comes with
 * stage 5; until then it reaches the serial log and dmesg only).
 *   arg bits 0-15: N, the number of numbered lines ("line 1 of N" ... );
 *                  0 means 10
 *   arg bit 16:    pause 100 ms after each numbered line
 * Then one long line (200 characters after "long: "), one line with control
 * characters (each shown as '?' by the kernel), and "lines: done".
 * Exit status 0 if every write was taken in full, else 1. */
#include "user/ulib.h"

#define PAUSE_BIT 0x10000L

static int bad;

static void put(const char *s, uint64_t n)
{
    if (sys_write(1, s, n) != (long)n)
        bad = 1;
}

/* Appends the decimal form of v to buf at *n. */
static void add_num(char *buf, uint64_t *n, uint64_t v)
{
    char digits[20];
    int nd = 0;
    do {
        digits[nd++] = (char)('0' + v % 10);
        v /= 10;
    } while (v);
    while (nd)
        buf[(*n)++] = digits[--nd];
}

static void add_str(char *buf, uint64_t *n, const char *s)
{
    while (*s)
        buf[(*n)++] = *s++;
}

static const char controls[] = "ctl: tab\there bell\x07 esc\x1b[31mred cr\rend del\x7f\n";

int main(int argc, char **argv)
{
    long arg = u_arg(argc, argv);
    uint64_t count = (uint64_t)(arg & 0xFFFF);
    if (count == 0)
        count = 10;
    struct timespec pause = { 0, 100000000 };
    char buf[64];
    for (uint64_t i = 1; i <= count; i++) {
        uint64_t n = 0;
        add_str(buf, &n, "line ");
        add_num(buf, &n, i);
        add_str(buf, &n, " of ");
        add_num(buf, &n, count);
        buf[n++] = '\n';
        put(buf, n);
        if (arg & PAUSE_BIT)
            sys_nanosleep(&pause, 0);
    }
    static char longline[6 + 200 + 1];
    uint64_t n = 0;
    add_str(longline, &n, "long: ");
    for (int i = 0; i < 200; i++)
        longline[n++] = (char)('a' + i % 26);
    longline[n++] = '\n';
    put(longline, n);
    put(controls, sizeof(controls) - 1);
    put("lines: done\n", 12);
    return bad;
}
