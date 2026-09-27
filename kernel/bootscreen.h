#ifndef MADIRE_BOOTSCREEN_H
#define MADIRE_BOOTSCREEN_H

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

/* A highlighted one-line message below the status area. */
void bootscreen_message(const char *text);

/* Final "System ready." line. */
void bootscreen_ready(void);

#endif
