#ifndef MADIRE_LOGO_H
#define MADIRE_LOGO_H

#include <stddef.h>

#define LOGO_WIDTH  36   /* pixels = text columns */
#define LOGO_HEIGHT 24   /* pixels = 2 per text row */

/* Loads the logo's gradient colours into the VGA palette, rasterises the
 * logo, and returns the number of lit pixels. */
unsigned logo_render(void);

/* Draws the rendered logo with its top-left corner at a text cell. */
void logo_draw(size_t row, size_t col);

/* Writes the rendered logo to the serial port as '#'/'.' text, followed by
 * "Logo: 36x24, <lit> pixels lit" (checked by `make test`). */
void logo_print_serial(unsigned lit);

#endif
