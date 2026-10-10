#ifndef MADIRE_CONSOLE_CONSOLE_H
#define MADIRE_CONSOLE_CONSOLE_H

/*
 * Interactive text console with the command shell. Normal (non-interrupt)
 * context only: the idle loop passes it the key events the 0.4 keyboard
 * driver queued.
 *
 * Once started it owns the whole 80x25 screen: its own character grid
 * (textbuf) is copied to VGA with vga_put_cell() and the hardware cursor
 * marks the edit position. Every submitted line ("madire> ...") and every
 * output line is also written to the serial log.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "drivers/keyboard/scancode.h"
#include "kernel/memory/memory.h"

/* Checks the pure console/shell modules with known input: line editor,
 * parser, shell (echo), text grid scrolling. It does NOT test keyboard or
 * VGA hardware; those are proven by the QEMU and VirtualBox tests. */
bool console_selftest(void);

/* Clears the screen, shows the banner and the first prompt. mem is the
 * boot memory summary (used read-only by the "mem" command). */
void console_start(const struct memory_stats *mem);

bool console_active(void);

/* One key event from the keyboard queue. While a program runs in the
 * foreground (0.11, "run") key events are ignored. */
void console_handle_key(const struct key_event *e);

/* 0.11: called from the idle loop (thread 0) on every pass: collects the
 * foreground program once it has ended (its end line, then the prompt)
 * and any background program that has ended (its end line in the serial
 * log and dmesg only). */
void console_poll(void);

/* 0.11 stage 5: queues cleaned output from the calling process. Foreground
 * output waits for room; background output is accepted for serial only. */
size_t console_program_write(int32_t pid, const uint8_t *text, size_t len);

#endif
