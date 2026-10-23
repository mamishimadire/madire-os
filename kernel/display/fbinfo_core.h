#ifndef MADIRE_DISPLAY_FBINFO_CORE_H
#define MADIRE_DISPLAY_FBINFO_CORE_H

/*
 * What the boot loader gave for the display, the pure part (1.2 stage 5;
 * host unit tests in tests/unit/test_fbinfo.c; the kernel side is
 * display.c). The fields come from the Multiboot information (mbfb.h).
 *
 * The verdicts (docs/plans/1.2.md, decisions 2 and 4, the stage 5 row):
 *   FB_NONE         flag bit 12 clear (QEMU -kernel): VGA text, as before 1.2
 *   FB_TEXT         type 2, the loader's text mode (Limine's "textmode:
 *                   yes"): VGA text, as before 1.2
 *   FB_USABLE       type 1 (direct RGB) at 32 or 24 bits per pixel, red at
 *                   bit 16, green at 8, blue at 0 (8 bits each), from
 *                   800x600 to 4096x4096, a pitch from width x bytes per
 *                   pixel to 65536, a non-zero address, below the 52-bit
 *                   physical limit
 *   FB_UNSUPPORTED  anything else: VGA text and serial kept (the screen may
 *                   show nothing: the loader set a mode the kernel can't use)
 * Each verdict comes with its "Display: ..." boot line (FBINFO_LINE bytes
 * at most, the newline not included).
 */

#include <stddef.h>
#include <stdint.h>

#define FBINFO_MIN_WIDTH   800u
#define FBINFO_MIN_HEIGHT  600u
#define FBINFO_MAX_SIDE   4096u
#define FBINFO_PHYS_LIMIT (1ull << 52)  /* x86-64's largest physical address width */
#define FBINFO_LINE        192u     /* the longest line is 190 bytes */

/* The fields as read from the Multiboot information (all zero when flag
 * bit 12 is clear). */
struct fbinfo_raw {
    uint32_t flags;         /* the information's flags */
    uint64_t addr;
    uint32_t pitch;
    uint32_t width;
    uint32_t height;
    uint8_t bpp;
    uint8_t type;
    uint8_t colour[6];      /* bytes 112-117 */
};

enum fb_kind {
    FB_NONE,
    FB_TEXT,
    FB_USABLE,
    FB_UNSUPPORTED,
};

/* The result. For FB_USABLE every field is valid; otherwise only kind. */
struct fb_info {
    enum fb_kind kind;
    uint64_t addr;
    uint32_t pitch;
    uint32_t width;
    uint32_t height;
    uint32_t bpp;
    uint32_t bytes_per_pixel;   /* 4 or 3 */
};

/* The fields of the Multiboot information at `info` (at least MBFB_END
 * bytes readable when flag bit 12 is set; only the flags are read when it
 * is clear). */
void fbinfo_read(const uint8_t *info, struct fbinfo_raw *raw);

/* Decides the verdict and writes the boot line into `line`
 * (FBINFO_LINE bytes). */
enum fb_kind fbinfo_parse(const struct fbinfo_raw *raw, struct fb_info *out, char *line);

#endif
