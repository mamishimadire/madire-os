#ifndef MADIRE_CONSOLE_OUTQ_H
#define MADIRE_CONSOLE_OUTQ_H

/*
 * Pure byte queue for foreground program output. The console owns the queue;
 * callers must disable preemption around every operation so thread 0 and a
 * process cannot mutate it at the same time.
 */

#include <stddef.h>
#include <stdint.h>

#define OUTQ_CAPACITY 8192u

struct outq {
    uint8_t data[OUTQ_CAPACITY];
    uint32_t head;
    uint32_t tail;
    uint32_t used;
};

void outq_init(struct outq *q);
size_t outq_write(struct outq *q, const uint8_t *src, size_t len);
size_t outq_read(struct outq *q, uint8_t *dst, size_t len);
size_t outq_used(const struct outq *q);

#endif
