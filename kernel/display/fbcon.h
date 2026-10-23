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
 * Before fbcon_start(), a framebuffer screen shows nothing the kernel
 * writes (the grid goes to vga_shadow and to video memory the graphics
 * mode does not show). 1.2 stage 10 (option A, decided at stage 8): a
 * panic in that time draws the crash screen directly on the framebuffer
 * (fbcon_crash_early). Still on serial only: boot.asm's halt message, a
 * panic before display_probe() (no framebuffer known yet), and a panic on
 * a framebuffer display.c cannot reach (display_crash_screen()).
 */

#include <stdbool.h>
#include <stdint.h>

/* After paging_init(), on a boot with display_active(): lays out the grid,
 * draws the whole screen from the shadow and logs the "Display: console
 * on the framebuffer" line. From then on fbcon_active(). 1.2 stage 8: with
 * `hold` (the splash, splash.h) nothing is drawn: the grid changes only in
 * the shadow until fbcon_release(), or a panic, draws the whole screen. */
void fbcon_start(bool hold);

bool fbcon_active(void);

/* 1.2 stage 10, from the crash screen (panic.c) before it clears the
 * grid: if the console is not active yet, makes it active on
 * display_crash_screen(), drawing each cell straight on the framebuffer
 * (no back buffer, no pointer), and logs that on serial. Nothing if the
 * console is already active or there is no such framebuffer. */
void fbcon_crash_early(void);

/* 1.2 stage 8: whether the splash holds the screen; the console's layout
 * (its scale and font; NULL if not active); and the screen back to the
 * console, drawn whole from the shadow. */
bool fbcon_held(void);
struct fbcon_layout;
const struct fbcon_layout *fbcon_current_layout(void);
void fbcon_release(void);

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
