/*
 * Madire OS boot logo: a geometric "M" of four rounded strokes (capsules)
 * with a dark-blue-to-cyan gradient.
 *
 * The logo is not an image. Each stroke is a line segment with a radius; a
 * pixel is lit if its centre lies within the radius of the segment. Strokes
 * are drawn back to front, so the two up-strokes overlap the down-strokes
 * like folded ribbons. Integer maths only (no FPU is set up).
 *
 * The same data and maths live in scripts/logo-preview.py, which produces the
 * expected pixel count checked by `make test`. Keep the two in sync.
 */
#include <stdint.h>
#include "drivers/serial/serial.h"
#include "drivers/vga/vga_text.h"
#include "kernel/logo.h"

#define SUB     8       /* stroke coordinates are in 1/8 pixel */
#define RX      20      /* stroke radius, horizontal, in 1/8 pixel */
#define RY      23      /* stroke radius, vertical (pixels are 9 wide x 8 tall) */
#define STEPS   6       /* gradient steps per stroke */
#define EMPTY   0xFF

struct stroke {
    int32_t x1, y1, x2, y2;
};

static const struct stroke strokes[] = {
    {  95,  57, 139, 169 },   /* left peak -> valley      (back)  */
    { 182,  23, 229, 122 },   /* right peak -> right foot (back)  */
    {  59, 123,  95,  57 },   /* left foot -> left peak   (front) */
    { 139, 169, 182,  23 },   /* valley -> right peak     (front) */
};

/* Gradient, dark blue -> cyan, as 6-bit VGA DAC values, and the VGA colour
 * slots it replaces (slots the rest of the kernel doesn't use). */
static const uint8_t gradient_rgb[STEPS][3] = {
    { 0, 16, 50 }, { 0, 25, 63 }, { 0, 35, 63 },
    { 5, 44, 63 }, { 10, 52, 63 }, { 16, 58, 63 },
};
static const uint8_t gradient_slot[STEPS] = {
    VGA_BLUE, VGA_MAGENTA, VGA_BROWN, VGA_DARK_GREY, VGA_LIGHT_BLUE, VGA_LIGHT_MAGENTA,
};

static uint8_t pixels[LOGO_HEIGHT][LOGO_WIDTH];   /* gradient step or EMPTY */

/* Scale y so the capsule is round on screen despite non-square pixels. */
static int32_t scale_y(int32_t y)
{
    return y * RX / RY;
}

static int inside_stroke(int32_t px, int32_t py, const struct stroke *s)
{
    int32_t x1 = s->x1, y1 = scale_y(s->y1);
    int32_t x2 = s->x2, y2 = scale_y(s->y2);
    py = scale_y(py);

    int64_t dx = x2 - x1, dy = y2 - y1;
    int64_t ax = px - x1, ay = py - y1;
    int64_t dot = ax * dx + ay * dy;
    int64_t len2 = dx * dx + dy * dy;
    int64_t r2 = (int64_t)RX * RX;

    if (dot <= 0)                       /* nearest point is the first end */
        return ax * ax + ay * ay <= r2;
    if (dot >= len2) {                  /* nearest point is the second end */
        int64_t bx = px - x2, by = py - y2;
        return bx * bx + by * by <= r2;
    }
    int64_t cross = ax * dy - ay * dx;  /* distance to the line, times length */
    return cross * cross <= r2 * len2;
}

unsigned logo_render(void)
{
    for (int i = 0; i < STEPS; i++)
        vga_set_palette(gradient_slot[i], gradient_rgb[i][0],
                        gradient_rgb[i][1], gradient_rgb[i][2]);

    for (int y = 0; y < LOGO_HEIGHT; y++)
        for (int x = 0; x < LOGO_WIDTH; x++)
            pixels[y][x] = EMPTY;

    for (size_t s = 0; s < sizeof(strokes) / sizeof(strokes[0]); s++) {
        const struct stroke *st = &strokes[s];
        int32_t lo = (st->x1 < st->x2 ? st->x1 : st->x2) - RX;
        int32_t hi = (st->x1 > st->x2 ? st->x1 : st->x2) + RX;

        for (int y = 0; y < LOGO_HEIGHT; y++) {
            for (int x = 0; x < LOGO_WIDTH; x++) {
                int32_t px = x * SUB + SUB / 2, py = y * SUB + SUB / 2;
                if (!inside_stroke(px, py, st))
                    continue;
                int32_t step = (px - lo) * STEPS / (hi - lo + 1);
                pixels[y][x] = (uint8_t)(step < 0 ? 0 : step >= STEPS ? STEPS - 1 : step);
            }
        }
    }

    unsigned lit = 0;
    for (int y = 0; y < LOGO_HEIGHT; y++)
        for (int x = 0; x < LOGO_WIDTH; x++)
            lit += pixels[y][x] != EMPTY;
    return lit;
}

static uint8_t pixel_color(uint8_t p)
{
    return p == EMPTY ? VGA_BLACK : gradient_slot[p];
}

void logo_draw(size_t row, size_t col)
{
    for (int r = 0; r < LOGO_HEIGHT / 2; r++) {
        for (int x = 0; x < LOGO_WIDTH; x++) {
            uint8_t top = pixels[2 * r][x], bottom = pixels[2 * r + 1][x];
            if (top == EMPTY && bottom == EMPTY)
                vga_put_cell(row + r, col + x, ' ', VGA_BLACK, VGA_BLACK);
            else
                vga_put_cell(row + r, col + x, VGA_CHAR_UPPER_HALF,
                             pixel_color(top), pixel_color(bottom));
        }
    }
}

static void serial_put_uint(unsigned v)
{
    char buf[12];
    int n = 0;
    do {
        buf[n++] = (char)('0' + v % 10);
        v /= 10;
    } while (v);
    while (n--)
        serial_putc(buf[n]);
}

void logo_print_serial(unsigned lit)
{
    for (int y = 0; y < LOGO_HEIGHT; y++) {
        for (int x = 0; x < LOGO_WIDTH; x++)
            serial_putc(pixels[y][x] == EMPTY ? '.' : '#');
        serial_putc('\n');
    }
    serial_write("Logo: ");
    serial_put_uint(LOGO_WIDTH);
    serial_putc('x');
    serial_put_uint(LOGO_HEIGHT);
    serial_write(", ");
    serial_put_uint(lit);
    serial_write(" pixels lit\n");
}
