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

#include <stdbool.h>
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

/*
 * 1.2 stage 5b: the plan for PML4 slot 506 (vm_layout.h), made for a
 * FB_USABLE framebuffer once the allocator is set up, before paging_init()
 * maps it. Two refusals keep VGA text and serial, each with its "Display:"
 * line:
 *   FBPLAN_OVERLAP  the framebuffer's pages overlap usable RAM (type 1 in
 *                   the Multiboot memory map): mapping them would give a
 *                   second, writable view of pages the allocator hands out
 *   FBPLAN_NO_ROOM  the back buffer and slot 506's page tables need more
 *                   than a quarter of the free pages (FBPLAN_SHARE): a
 *                   large mode on a small machine (the parser accepts up to
 *                   4096x4096, 64 MiB at 32 bpp) leaves the rest of the
 *                   system its memory
 * The back buffer has the framebuffer's format with a pitch of exactly
 * width x bytes per pixel. Slot 506's tables: one PDPT, one page directory
 * per area (each area starts at a 1 GiB boundary) and one page table per
 * 512 pages of each area.
 */
#define FBPLAN_SHARE       4u       /* at most 1/FBPLAN_SHARE of the free pages */
/* The most tables a FB_USABLE framebuffer can need: a pitch of 65536 x 4096
 * rows plus a page offset is 65537 pages (129 page tables); the back buffer
 * at 4096x4096x32 is 16384 pages (32); plus the PDPT and two directories. */
#define FBPLAN_MAX_TABLES  164u

/* A range of usable RAM from the memory map. */
struct fb_ram {
    uint64_t base;
    uint64_t length;
};

enum fb_plan_kind {
    FBPLAN_MAP,
    FBPLAN_OVERLAP,
    FBPLAN_NO_ROOM,
};

struct fb_plan {
    uint64_t fb_phys;           /* the framebuffer's first page (physical) */
    uint32_t fb_offset;         /* the framebuffer's offset in that page */
    uint32_t fb_pages;          /* pages mapped at VM_FB_BASE */
    uint32_t bb_pitch;          /* the back buffer's bytes per row */
    uint32_t bb_pages;          /* frames of free RAM at VM_BACKBUF_BASE */
    uint32_t tables;            /* slot 506's PDPT + directories + page tables */
};

/* True if the framebuffer's pages (rounded out to whole pages) overlap the
 * range of usable RAM [ram->base, ram->base + ram->length). */
bool fbinfo_overlaps(const struct fb_info *fb, const struct fb_ram *ram);

/* The plan for a FB_USABLE `fb`. `overlap` is the first usable range that
 * fbinfo_overlaps() found, or NULL if none does; `free_pages` the
 * allocator's free pages now. Fills *plan in every case (the sizes); for a
 * refusal it writes the "Display:" line into `line` (FBINFO_LINE bytes),
 * for FBPLAN_MAP it leaves `line` empty (paging.c logs the mapping). */
enum fb_plan_kind fbinfo_plan(const struct fb_info *fb, const struct fb_ram *overlap,
                              uint32_t free_pages, struct fb_plan *plan, char *line);

#endif
