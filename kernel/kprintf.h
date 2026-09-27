#ifndef MADIRE_KPRINTF_H
#define MADIRE_KPRINTF_H

/* Kernel console output: every character goes to the VGA screen (at the
 * cursor, in the current colour) and to the serial port. */
void kputc(char c);
void kputs(const char *s);

/* Supports %s %c %d %u %x %p %%, with optional zero-pad and width (%08x). */
void kprintf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

#endif
