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

/* One key event from the keyboard queue. */
void console_handle_key(const struct key_event *e);

#endif
