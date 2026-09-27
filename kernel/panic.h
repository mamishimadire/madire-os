#ifndef MADIRE_PANIC_H
#define MADIRE_PANIC_H

#include "kernel/arch/x86/interrupts.h"

/* Show the red crash screen (also on serial) and halt the CPU forever. */
void panic(const char *message) __attribute__((noreturn));
void panic_exception(const struct interrupt_frame *frame) __attribute__((noreturn));

#endif
