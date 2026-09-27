#include <stdint.h>
#include "drivers/serial/serial.h"
#include "drivers/vga/vga_text.h"
#include "kernel/kprintf.h"

void kputc(char c)
{
    vga_putc(c);
    serial_putc(c);
}

void kputs(const char *s)
{
    while (*s)
        kputc(*s++);
}

/* Output target for the formatter. */
enum sink_kind { SINK_CONSOLE, SINK_SERIAL, SINK_BUFFER };

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
        case 'p':
            sink_puts(out, "0x");
            put_uint(out, (uint32_t)(uintptr_t)va_arg(ap, void *), 16, 8, '0');
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
    format(&out, fmt, ap);
}

void klogf(const char *fmt, ...)
{
    struct sink out = { SINK_SERIAL, NULL, 0, 0 };
    va_list ap;
    va_start(ap, fmt);
    format(&out, fmt, ap);
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
