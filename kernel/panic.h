#ifndef MADIRE_PANIC_H
#define MADIRE_PANIC_H

#include <stdbool.h>
#include "kernel/arch/x86/interrupts.h"

/* True once a panic has started (0.9): the crash screen may be written from
 * any thread (kernel/proc/thread.c, thread_screen_allowed). */
extern volatile bool panic_active;

/* Show the red crash screen (also on serial) and halt the CPU forever. */
void panic(const char *message) __attribute__((noreturn));
void panicf(const char *fmt, ...) __attribute__((noreturn, format(printf, 1, 2)));
void panic_exception(const struct interrupt_frame *frame) __attribute__((noreturn));

#endif
