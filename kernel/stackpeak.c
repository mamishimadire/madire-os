/* Stack high-water mark (see stackpeak.h). */
#include "kernel/stackpeak.h"

void stack_paint(uint8_t *lo, size_t len)
{
    /* volatile: a loop the compiler must not turn into a memset call. */
    volatile uint8_t *p = lo;
    for (size_t i = 0; i < len; i++)
        p[i] = STACK_PAINT_BYTE;
}

size_t stack_peak(const uint8_t *lo, size_t len)
{
    size_t untouched = 0;
    while (untouched < len && lo[untouched] == STACK_PAINT_BYTE)
        untouched++;
    return len - untouched;
}
