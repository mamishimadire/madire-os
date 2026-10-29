#ifndef MADIRE_DISPLAY_DESKTOP_H
#define MADIRE_DISPLAY_DESKTOP_H

/*
 * The desktop, the kernel side (1.3 stage 3; docs/plans/1.3.md, C1, C4, C6;
 * the pictures are desktop_core.h's, the composition compositor.h's). In
 * stage 3 only on a boot with the test option test=desktop (test ISOs; the
 * real ISO and every other boot are as in 1.2): at System ready, if the
 * console is offered and is on the framebuffer, the desktop takes the
 * screen from the console (fbcon_hold) and the compositor owns the back
 * buffer: the background (the wallpaper with the M logo alone, at the
 * splash's place: the owner's decision in stage 3b), the taskbar with the M button
 * (drawn, its normal state only) and the clock, and the pointer from the
 * first mouse packet on (the 1.2 save-under overlay on the framebuffer).
 *
 * Any key hands the screen back to 1.2's full-screen console, and the
 * console opens; the key is not typed (as since 0.5). Stage 4's Console
 * window replaces this. A panic draws 1.2's crash screen (fbcon).
 *
 * The background cache: slot 506's third area (VM_DESKTOP_BASE), planned
 * and mapped once when the desktop starts, never freed; when it does not
 * fit the share (desktop_core.h), or with the test option desktopnocache,
 * the background is drawn again for each damaged rectangle. The clock is
 * read from the CMOS clock once per second of uptime and redrawn when its
 * text changes.
 *
 * Logged (serial and dmesg): the "Desktop: ..." lines when it starts, and
 * when a key closes it. On a test boot (every boot with test=desktop in
 * stage 3) each composed frame is followed by the marker "display: frame
 * N" and "desktop: clock HH:MM" (the clock it shows). With the test option
 * desktoplog, also "desktop: composed ..." per frame (serial only).
 */

#include <stdbool.h>

/* The boot options: test=desktop (`wanted`), desktoplog, desktopnocache. */
void desktop_options(bool wanted, bool log, bool nocache);

/* At System ready, after its line: starts the desktop if wanted (above).
 * True if it shows. */
bool desktop_start(bool console_offered);

bool desktop_showing(void);

/* Thread 0's idle loop, after pointer_poll(): the clock, then one
 * composition of whatever is damaged. */
void desktop_poll(void);

/* A key while the desktop shows: the screen goes back to the console. */
void desktop_close_key(void);

#endif
