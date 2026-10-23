#ifndef MADIRE_DISPLAY_FBCON_H
#define MADIRE_DISPLAY_FBCON_H

/*
 * The framebuffer console (1.2 stage 6). On a boot with a mapped
 * framebuffer (display_active()), it draws the VGA text driver's cell grid
 * (drivers/vga/vga_text.h: the cells, the 16 colour slots, the cursor) on
 * the screen with the font of the screen's scale (fbcon_core.h). Every
 * other boot keeps VGA text (decision 4) and never calls it.
 *
 * Only drivers/vga/vga_text.c calls the drawing functions, and only while
 * fbcon_active(); they run on the caller's stack, so the screen rule holds
 * as for VGA text: only thread 0 draws (decision 6), and the crash screen
 * (panic_active). A draw from any other thread is refused and counted.
 * Each cell is drawn on the back buffer and copied to the framebuffer at
 * once; a cell already showing what the grid holds is not drawn again.
 *
 * Known gap (follow-up for stage 8/10): before fbcon_start(), a
 * framebuffer screen shows nothing the kernel writes (the grid goes to
 * vga_shadow and to video memory the graphics mode does not show), so a
 * panic before paging_init(), or boot.asm's halt message, is on serial
 * only.
 */

#include <stdbool.h>
#include <stdint.h>

/* After paging_init(), on a boot with display_active(): lays out the grid,
 * draws the whole screen from the shadow and logs the "Display: console
 * on the framebuffer" line. From then on fbcon_active(). */
void fbcon_start(void);

bool fbcon_active(void);

/* Called by vga_text.c (only while active): cell i (row x 80 + col) of the
 * shadow changed; the whole shadow changed; colour slot `slot` changed;
 * the cursor was shown, hidden or moved. */
void fbcon_cell(uint32_t i);
void fbcon_redraw(void);
void fbcon_palette(uint8_t slot);
void fbcon_cursor(void);

/* test=fbcon (a test option): two screens for tests/qmp_fbcon.py, each
 * after a "display: frame N" line and held still for 3 s: the boot screen
 * as it is, then a chart of the 256 characters and the 16 x 16 colour
 * pairs with the cursor shown; then the boot screen back, and the
 * report-only timing lines of a full-screen redraw and of one scroll line. */
void fbcon_test(void);

#endif
