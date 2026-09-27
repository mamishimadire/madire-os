#include <stdarg.h>
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

static void put_uint(uint32_t value, unsigned base, int width, char pad)
{
    static const char digits[] = "0123456789abcdef";
    char buf[32];
    int n = 0;

    do {
        buf[n++] = digits[value % base];
        value /= base;
    } while (value);

    while (width-- > n)
        kputc(pad);
    while (n--)
        kputc(buf[n]);
}

void kprintf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);

    for (; *fmt; fmt++) {
        if (*fmt != '%') {
            kputc(*fmt);
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
            kputs(s ? s : "(null)");
            break;
        }
        case 'c':
            kputc((char)va_arg(ap, int));
            break;
        case 'd': {
            int v = va_arg(ap, int);
            if (v < 0) {
                kputc('-');
                put_uint(-(uint32_t)v, 10, width - 1, pad);
            } else {
                put_uint((uint32_t)v, 10, width, pad);
            }
            break;
        }
        case 'u':
            put_uint(va_arg(ap, uint32_t), 10, width, pad);
            break;
        case 'x':
            put_uint(va_arg(ap, uint32_t), 16, width, pad);
            break;
        case 'p':
            kputs("0x");
            put_uint((uint32_t)(uintptr_t)va_arg(ap, void *), 16, 8, '0');
            break;
        case '%':
            kputc('%');
            break;
        case '\0':
            fmt--;          /* let the loop condition end on the terminator */
            break;
        default:
            kputc('%');
            kputc(*fmt);
            break;
        }
    }

    va_end(ap);
}
