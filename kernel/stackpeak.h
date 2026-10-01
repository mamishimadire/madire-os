#ifndef MADIRE_STACKPEAK_H
#define MADIRE_STACKPEAK_H

/*
 * Stack high-water mark (0.8). The unused part of a stack is filled with a
 * known byte; later, the lowest byte that no longer holds it shows how deep
 * the stack has ever been used. Stacks grow down, from `lo + len` towards
 * `lo`.
 *
 * Pure module: no hardware access, printing or panics, so it is unit-tested
 * on the host (tests/unit/test_stackpeak.c).
 *
 * The result can be too low by a few bytes, never too high: if the deepest
 * byte ever written happened to equal the pattern, it looks unused.
 */

#include <stddef.h>
#include <stdint.h>

#define STACK_PAINT_BYTE 0xA5u

/* Fills [lo, lo + len) with STACK_PAINT_BYTE. */
void stack_paint(uint8_t *lo, size_t len);

/* Bytes of [lo, lo + len) used at the deepest point: len minus the number
 * of untouched pattern bytes at the bottom. 0 if the whole range still
 * holds the pattern. */
size_t stack_peak(const uint8_t *lo, size_t len);

#endif
