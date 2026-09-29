#ifndef MADIRE_KPRINTF_H
#define MADIRE_KPRINTF_H

#include <stdarg.h>
#include <stddef.h>

/* Kernel console output: every character goes to the VGA screen (at the
 * cursor, in the current colour) and to the serial port. */
void kputc(char c);
void kputs(const char *s);

/* Supports %s %c %d %u %x %p %%, with optional zero-pad and width (%08x);
 * l and ll lengths for d, u, x (%llx for uint64_t on both 32- and 64-bit
 * builds). %p prints the full pointer width (8 or 16 hex digits). */
void kprintf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void kvprintf(const char *fmt, va_list ap);

/* Kernel log: formatted output to the serial port only (not the screen). */
void klogf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

/* Formats into buf (always NUL-terminated when size > 0; output that does
 * not fit is cut off). Returns the number of characters stored. */
size_t ksnprintf(char *buf, size_t size, const char *fmt, ...) __attribute__((format(printf, 3, 4)));

#endif
