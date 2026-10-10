/* Pure byte queue for foreground program output (see outq.h). */
#include "kernel/console/outq.h"

void outq_init(struct outq *q)
{
    q->head = 0;
    q->tail = 0;
    q->used = 0;
}

size_t outq_write(struct outq *q, const uint8_t *src, size_t len)
{
    size_t room = OUTQ_CAPACITY - q->used;
    size_t n = len < room ? len : room;
    for (size_t i = 0; i < n; i++) {
        q->data[q->tail] = src[i];
        q->tail++;
        if (q->tail == OUTQ_CAPACITY)
            q->tail = 0;
    }
    q->used += (uint32_t)n;
    return n;
}

size_t outq_read(struct outq *q, uint8_t *dst, size_t len)
{
    size_t n = len < q->used ? len : q->used;
    for (size_t i = 0; i < n; i++) {
        dst[i] = q->data[q->head];
        q->head++;
        if (q->head == OUTQ_CAPACITY)
            q->head = 0;
    }
    q->used -= (uint32_t)n;
    return n;
}

size_t outq_used(const struct outq *q)
{
    return q->used;
}
