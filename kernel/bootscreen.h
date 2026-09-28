#ifndef MADIRE_BOOTSCREEN_H
#define MADIRE_BOOTSCREEN_H

/* Boot screen. Normal (non-interrupt) context only. */

#include <stddef.h>
#include <stdint.h>
#include "drivers/keyboard/scancode.h"
#include "kernel/memory/memory.h"

enum boot_status {
    BOOT_OK,
    BOOT_FAILED,
    BOOT_PENDING,
};

/* Logo, "M A D I R E   O S" and version line. */
void bootscreen_draw_header(void);

/* One start-up check in the left column: "GDT loaded:   OK". The note goes
 * to the serial log only, to keep the screen readable. note may be NULL. */
void bootscreen_status(const char *label, enum boot_status status, const char *note);

/* Memory figures in the right column (and the serial log). */
void bootscreen_memory(const struct memory_stats *stats);

/* Live lines in the right column, updated by the idle loop (screen only;
 * the idle loop logs to serial separately). e may be NULL for "none". */
void bootscreen_uptime(uint32_t seconds);
void bootscreen_last_key(const struct key_event *e);

/* Keyboard diagnostic view: clears the status panel, then shows lines in
 * it, wrapping back to the top when full. */
void bootscreen_diag_begin(void);
void bootscreen_diag_line(uint32_t index, const char *text);

/* A highlighted one-line message below the status area. */
void bootscreen_message(const char *text);

/* Final "System ready." line, followed by what happens next
 * (e.g. "Press any key to open the console."). */
void bootscreen_ready(const char *hint);

#endif
