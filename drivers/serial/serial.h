#ifndef MADIRE_DRIVERS_SERIAL_H
#define MADIRE_DRIVERS_SERIAL_H

/* COM1 at 38400 baud, 8N1. Used for debug output and automated tests. */
void serial_init(void);
void serial_putc(char c);
void serial_write(const char *s);

#endif
