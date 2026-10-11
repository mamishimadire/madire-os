#include <stdbool.h>
#include <stdint.h>
#include "drivers/serial/serial.h"
#include "drivers/vga/vga_text.h"
#include "kernel/kprintf.h"
#include "kernel/logbuf.h"
#include "kernel/proc/preempt.h"
#include "kernel/proc/thread.h"

/* 0.9.1: every character kputc and klogf send to the serial port is also
 * kept in the kernel log (dmesg). Written with preemption off (below), so
 * no locking is needed; never from interrupt context. Text sent to the
 * serial port directly (the boot screen's status lines, the console's
 * output lines) is not in it. */
static char klog_storage[LOGBUF_KERNEL_SIZE];
static struct logbuf klog = { klog_storage, LOGBUF_KERNEL_SIZE, 0, 0 };

const struct logbuf *klog_buffer(void)
{
    return &klog;
}

/* 0.9: the VGA cursor and the serial port are shared, unlocked state. Each
 * call (a character, a string, a whole formatted line) runs with
 * preemption off, so lines from different threads never interleave.
 * ksnprintf only writes to the caller's buffer and needs no guard.
 * Only thread 0 writes to the screen (other threads use klogf); a breach
 * panics, except on the crash screen itself. */
void kputc(char c)
{
    if (!thread_screen_allowed())
        thread_screen_violation();
    preempt_disable();
    vga_putc(c);
    serial_putc(c);
    logbuf_putc(&klog, c);
    preempt_enable();
}

void kputs(const char *s)
{
    preempt_disable();
    while (*s)
        kputc(*s++);
    preempt_enable();
}

/* Output target for the formatter. SINK_SERIAL_ONLY (0.12): the serial
 * port without the kernel log. */
enum sink_kind { SINK_CONSOLE, SINK_SERIAL, SINK_SERIAL_ONLY, SINK_BUFFER };

struct sink {
    enum sink_kind kind;
    char *buf;
    size_t size;
    size_t len;
};

static void sink_putc(struct sink *out, char c)
{
    switch (out->kind) {
    case SINK_CONSOLE:
        kputc(c);
        break;
    case SINK_SERIAL:
        serial_putc(c);
        logbuf_putc(&klog, c);
        break;
    case SINK_SERIAL_ONLY:
        serial_putc(c);
        break;
    case SINK_BUFFER:
        if (out->len + 1 < out->size)
            out->buf[out->len++] = c;
        break;
    }
}

static void sink_puts(struct sink *out, const char *s)
{
    while (*s)
        sink_putc(out, *s++);
}

static void put_uint(struct sink *out, uint32_t value, unsigned base, int width, char pad)
{
    static const char digits[] = "0123456789abcdef";
    char buf[32];
    int n = 0;

    do {
        buf[n++] = digits[value % base];
        value /= base;
    } while (value);

    while (width-- > n)
        sink_putc(out, pad);
    while (n--)
        sink_putc(out, buf[n]);
}

/* 64-bit value in base 10 or 16 using only 32-bit arithmetic (no libgcc in
 * a 32-bit build): the value is divided by the base 16 bits at a time. */
static void put_u64(struct sink *out, uint64_t value, unsigned base, int width, char pad)
{
    static const char digits[] = "0123456789abcdef";
    char buf[24];
    int n = 0;

    if ((value >> 32) == 0) {
        put_uint(out, (uint32_t)value, base, width, pad);
        return;
    }
    while (value) {
        uint64_t q = 0;
        uint32_t r = 0;
        for (int shift = 48; shift >= 0; shift -= 16) {
            uint32_t cur = (r << 16) | (uint32_t)((value >> shift) & 0xFFFFu);
            q |= (uint64_t)(cur / base) << shift;
            r = cur % base;
        }
        buf[n++] = digits[r];
        value = q;
    }
    while (width-- > n)
        sink_putc(out, pad);
    while (n--)
        sink_putc(out, buf[n]);
}

static void format(struct sink *out, const char *fmt, va_list ap)
{
    for (; *fmt; fmt++) {
        if (*fmt != '%') {
            sink_putc(out, *fmt);
            continue;
        }
        fmt++;

        char pad = ' ';
        int width = 0;
        if (*fmt == '0') {
            pad = '0';
            fmt++;
        }
        while (*fmt >= '0' && *fmt <= '9')
            width = width * 10 + (*fmt++ - '0');

        /* Length: l (long) and ll (long long), for d, u and x. */
        int longs = 0;
        while (*fmt == 'l' && longs < 2) {
            longs++;
            fmt++;
        }
        if (longs && (*fmt == 'd' || *fmt == 'u' || *fmt == 'x')) {
            uint64_t v;
            bool negative = false;
            if (*fmt == 'd') {
                int64_t s = longs == 2 ? (int64_t)va_arg(ap, long long) : (int64_t)va_arg(ap, long);
                negative = s < 0;
                v = negative ? -(uint64_t)s : (uint64_t)s;
            } else {
                v = longs == 2 ? (uint64_t)va_arg(ap, unsigned long long)
                               : (uint64_t)va_arg(ap, unsigned long);
            }
            if (negative) {
                sink_putc(out, '-');
                width--;
            }
            put_u64(out, v, *fmt == 'x' ? 16 : 10, width, pad);
            continue;
        }

        switch (*fmt) {
        case 's': {
            const char *s = va_arg(ap, const char *);
            sink_puts(out, s ? s : "(null)");
            break;
        }
        case 'c':
            sink_putc(out, (char)va_arg(ap, int));
            break;
        case 'd': {
            int v = va_arg(ap, int);
            if (v < 0) {
                sink_putc(out, '-');
                put_uint(out, -(uint32_t)v, 10, width - 1, pad);
            } else {
                put_uint(out, (uint32_t)v, 10, width, pad);
            }
            break;
        }
        case 'u':
            put_uint(out, va_arg(ap, uint32_t), 10, width, pad);
            break;
        case 'x':
            put_uint(out, va_arg(ap, uint32_t), 16, width, pad);
            break;
        case 'p':                       /* full pointer width: 8 or 16 digits */
            sink_puts(out, "0x");
            put_u64(out, (uint64_t)(uintptr_t)va_arg(ap, void *), 16,
                    (int)(sizeof(void *) * 2), '0');
            break;
        case '%':
            sink_putc(out, '%');
            break;
        case '\0':
            fmt--;          /* let the loop condition end on the terminator */
            break;
        default:
            sink_putc(out, '%');
            sink_putc(out, *fmt);
            break;
        }
    }
}

void kvprintf(const char *fmt, va_list ap)
{
    struct sink out = { SINK_CONSOLE, NULL, 0, 0 };
    preempt_disable();
    format(&out, fmt, ap);
    preempt_enable();
}

void klogf(const char *fmt, ...)
{
    struct sink out = { SINK_SERIAL, NULL, 0, 0 };
    va_list ap;
    va_start(ap, fmt);
    preempt_disable();
    format(&out, fmt, ap);
    preempt_enable();
    va_end(ap);
}

/* 0.12: like klogf, but kept out of the kernel log, so a periodic line (the
 * uptime heartbeat) never fills dmesg. */
void kserialf(const char *fmt, ...)
{
    struct sink out = { SINK_SERIAL_ONLY, NULL, 0, 0 };
    va_list ap;
    va_start(ap, fmt);
    preempt_disable();
    format(&out, fmt, ap);
    preempt_enable();
    va_end(ap);
}

void kprintf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    kvprintf(fmt, ap);
    va_end(ap);
}

size_t ksnprintf(char *buf, size_t size, const char *fmt, ...)
{
    struct sink out = { SINK_BUFFER, buf, size, 0 };
    va_list ap;
    va_start(ap, fmt);
    format(&out, fmt, ap);
    va_end(ap);
    if (size)
        buf[out.len] = '\0';
    return out.len;
}
