#ifndef MADIRE_DRIVERS_PIT_DIVISOR_H
#define MADIRE_DRIVERS_PIT_DIVISOR_H

/*
 * PIT divisor calculation. Pure (header-only), unit-tested on the host
 * (tests/unit/test_pit.c).
 *
 * The PIT input clock is 1,193,182 Hz; channel 0 fires IRQ0 at
 * base / divisor. The divisor register is 16 bits (0 would mean 65536,
 * which is not used here), so valid rates are 19 Hz .. 1,193,182 Hz.
 */

#include <stdbool.h>
#include <stdint.h>

#define PIT_BASE_HZ 1193182u

/* Nearest divisor for the requested rate; false if the rate is out of range. */
static inline bool pit_divisor_for(uint32_t hz, uint16_t *divisor)
{
    if (hz == 0 || hz > PIT_BASE_HZ)
        return false;
    uint32_t d = (PIT_BASE_HZ + hz / 2) / hz;       /* 32-bit: no libgcc */
    if (d == 0 || d > 0xFFFFu)
        return false;
    *divisor = (uint16_t)d;
    return true;
}

#endif
