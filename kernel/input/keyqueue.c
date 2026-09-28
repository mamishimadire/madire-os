/* Key-event queue (see keyqueue.h). */
#include "kernel/input/keyqueue.h"

#define INDEX(n) ((n) & (KEYQUEUE_SIZE - 1))
#define BARRIER() __asm__ volatile ("" : : : "memory")

void keyqueue_init(struct keyqueue *q)
{
    q->head = 0;
    q->tail = 0;
    q->dropped = 0;
}

uint32_t keyqueue_count(const struct keyqueue *q)
{
    return q->head - q->tail;       /* free-running counters: wrap-safe */
}

bool keyqueue_push(struct keyqueue *q, const struct key_event *e)
{
    if (keyqueue_count(q) >= KEYQUEUE_SIZE) {
        q->dropped++;
        return false;
    }
    q->items[INDEX(q->head)] = *e;
    BARRIER();                      /* item written before it is published */
    q->head++;
    return true;
}

bool keyqueue_pop(struct keyqueue *q, struct key_event *out)
{
    if (q->head == q->tail)
        return false;
    *out = q->items[INDEX(q->tail)];
    BARRIER();                      /* item read before the slot is released */
    q->tail++;
    return true;
}

uint32_t keyqueue_dropped(const struct keyqueue *q)
{
    return q->dropped;
}
