#ifndef MADIRE_ARCH_X86_RESET_H
#define MADIRE_ARCH_X86_RESET_H

/*
 * Restarting the machine (0.9.1), for the shell's "reboot".
 *
 * With interrupts off it first pulses the CPU reset line through the
 * keyboard controller (command 0xFE to port 0x64, after waiting, bounded,
 * for the controller's input buffer to empty) and waits about 100 ms. If
 * the machine is still running, it forces a triple fault: an empty
 * interrupt table, then int3, which the CPU can't deliver (#GP, then #DF,
 * then a shutdown that resets the machine). If even that returns, it halts.
 * Each step is logged to serial first.
 *
 * keyboard_controller = false skips the first method (test=reboottriple, to
 * test the fallback in QEMU).
 */

#include <stdbool.h>

void reset_machine(bool keyboard_controller) __attribute__((noreturn));

#endif
