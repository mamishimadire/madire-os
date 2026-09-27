#ifndef MADIRE_BOOTSCREEN_H
#define MADIRE_BOOTSCREEN_H

enum boot_status {
    BOOT_OK,
    BOOT_FAILED,
    BOOT_PENDING,
};

/* Logo, "M A D I R E   O S" and version line. */
void bootscreen_draw_header(void);

/* One start-up check, e.g. "GDT loaded:   OK   (note)". note may be NULL. */
void bootscreen_status(const char *label, enum boot_status status, const char *note);

/* A highlighted one-line message below the status list. */
void bootscreen_message(const char *text);

/* Final "System ready." line. */
void bootscreen_ready(void);

#endif
