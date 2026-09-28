/*
 * VGA text mode driver (80x25, 16 colours).
 *
 * Writes directly to the text buffer at 0xB8000 and programs the VGA
 * registers for the cursor, blink bit and colour palette. Only works under
 * BIOS firmware; EFI machines need a framebuffer driver (later milestone).
 */
#include "drivers/vga/vga_text.h"
#include "kernel/arch/x86/io.h"

#define VGA_CRTC_INDEX   0x3D4
#define VGA_CRTC_DATA    0x3D5
#define VGA_AC_WRITE     0x3C0   /* attribute controller: index/data flip-flop */
#define VGA_AC_READ      0x3C1
#define VGA_INPUT_STATUS 0x3DA   /* reading resets the AC flip-flop to "index" */
#define VGA_DAC_WRITE    0x3C8
#define VGA_DAC_DATA     0x3C9

#define VGA_AC_PAS       0x20    /* palette address source: display enabled */
#define VGA_AC_MODE      0x10
#define VGA_AC_MODE_BLINK 0x08

static volatile uint16_t *const vga = (volatile uint16_t *)0xB8000;
static size_t cursor_row;
static size_t cursor_col;
static uint8_t color;

static inline uint16_t vga_entry(uint8_t ch, uint8_t attr)
{
    return (uint16_t)ch | (uint16_t)attr << 8;
}

static void vga_disable_cursor(void)
{
    outb(VGA_CRTC_INDEX, 0x0A);
    outb(VGA_CRTC_DATA, 0x20);
}

static void vga_disable_blink(void)
{
    inb(VGA_INPUT_STATUS);
    outb(VGA_AC_WRITE, VGA_AC_MODE | VGA_AC_PAS);
    uint8_t mode = inb(VGA_AC_READ);
    outb(VGA_AC_WRITE, mode & (uint8_t)~VGA_AC_MODE_BLINK);
}

bool vga_init(void)
{
    vga_disable_cursor();
    vga_disable_blink();
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    vga_clear();

    vga[0] = vga_entry('M', color);
    bool ok = vga[0] == vga_entry('M', color);
    vga[0] = vga_entry(' ', color);
    return ok;
}

void vga_set_color(enum vga_color fg, enum vga_color bg)
{
    color = (uint8_t)(fg | bg << 4);
}

void vga_clear(void)
{
    for (size_t i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++)
        vga[i] = vga_entry(' ', color);
    cursor_row = 0;
    cursor_col = 0;
}

void vga_set_cursor(size_t row, size_t col)
{
    cursor_row = row < VGA_HEIGHT ? row : 0;
    cursor_col = col < VGA_WIDTH ? col : 0;
}

void vga_putc(char c)
{
    if (c == '\n') {
        cursor_col = 0;
        cursor_row++;
    } else {
        vga[cursor_row * VGA_WIDTH + cursor_col] = vga_entry((uint8_t)c, color);
        if (++cursor_col == VGA_WIDTH) {
            cursor_col = 0;
            cursor_row++;
        }
    }
    /* No scrolling yet (milestone 0.4): wrap to the top. */
    if (cursor_row == VGA_HEIGHT)
        cursor_row = 0;
}

void vga_put_cell(size_t row, size_t col, uint8_t ch, uint8_t fg, uint8_t bg)
{
    if (row < VGA_HEIGHT && col < VGA_WIDTH)
        vga[row * VGA_WIDTH + col] = vga_entry(ch, (uint8_t)((fg & 0x0F) | (bg & 0x0F) << 4));
}

#define VGA_CRTC_CURSOR_START 0x0A
#define VGA_CRTC_CURSOR_END   0x0B
#define VGA_CRTC_CURSOR_HIGH  0x0E
#define VGA_CRTC_CURSOR_LOW   0x0F
#define VGA_CURSOR_OFF        0x20      /* start register bit 5 */

void vga_cursor_show(bool visible)
{
    if (!visible) {
        vga_disable_cursor();
        return;
    }
    /* Underline cursor on scan lines 13-14 of the 16-line character cell. */
    outb(VGA_CRTC_INDEX, VGA_CRTC_CURSOR_START);
    outb(VGA_CRTC_DATA, (uint8_t)((inb(VGA_CRTC_DATA) & 0xC0) | 13));
    outb(VGA_CRTC_INDEX, VGA_CRTC_CURSOR_END);
    outb(VGA_CRTC_DATA, (uint8_t)((inb(VGA_CRTC_DATA) & 0xE0) | 14));
}

void vga_cursor_move(size_t row, size_t col)
{
    if (row >= VGA_HEIGHT || col >= VGA_WIDTH)
        return;
    uint16_t pos = (uint16_t)(row * VGA_WIDTH + col);
    outb(VGA_CRTC_INDEX, VGA_CRTC_CURSOR_LOW);
    outb(VGA_CRTC_DATA, (uint8_t)(pos & 0xFF));
    outb(VGA_CRTC_INDEX, VGA_CRTC_CURSOR_HIGH);
    outb(VGA_CRTC_DATA, (uint8_t)(pos >> 8));
}

void vga_set_palette(uint8_t slot, uint8_t r, uint8_t g, uint8_t b)
{
    slot &= 0x0F;

    /* Point attribute-controller slot N at DAC entry N. BIOS maps some slots
     * elsewhere (e.g. 6 -> 0x14, 8-15 -> 0x38-0x3F). The palette registers
     * are only writable with PAS clear, which blanks the display briefly. */
    inb(VGA_INPUT_STATUS);
    outb(VGA_AC_WRITE, slot);
    outb(VGA_AC_WRITE, slot);
    inb(VGA_INPUT_STATUS);
    outb(VGA_AC_WRITE, VGA_AC_PAS);

    outb(VGA_DAC_WRITE, slot);
    outb(VGA_DAC_DATA, r & 0x3F);
    outb(VGA_DAC_DATA, g & 0x3F);
    outb(VGA_DAC_DATA, b & 0x3F);
}
