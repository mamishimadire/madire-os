/*
 * What the boot loader gave for the display, the pure part (1.2 stage 5);
 * see fbinfo_core.h.
 */
#include <stdbool.h>
#include "kernel/display/fbinfo_core.h"
#include "kernel/display/mbfb.h"
#include "kernel/kprintf.h"

#define FBINFO_MAX_PITCH 65536u

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

void fbinfo_read(const uint8_t *info, struct fbinfo_raw *raw)
{
    /* Field by field: no struct assignment, which could become a memset. */
    raw->flags = rd32(info);
    raw->addr = 0;
    raw->pitch = raw->width = raw->height = 0;
    raw->bpp = raw->type = 0;
    for (uint32_t i = 0; i < 6; i++)
        raw->colour[i] = 0;
    if (!(raw->flags & MULTIBOOT_INFO_FRAMEBUFFER))
        return;
    raw->addr = (uint64_t)rd32(info + MBFB_ADDR) | (uint64_t)rd32(info + MBFB_ADDR + 4) << 32;
    raw->pitch = rd32(info + MBFB_PITCH);
    raw->width = rd32(info + MBFB_WIDTH);
    raw->height = rd32(info + MBFB_HEIGHT);
    raw->bpp = info[MBFB_BPP];
    raw->type = info[MBFB_TYPE];
    for (uint32_t i = 0; i < 6; i++)
        raw->colour[i] = info[MBFB_COLOUR + i];
}

/* True if a type 1 framebuffer can't be used; the reason is in `why`. */
static bool rgb_refused(const struct fbinfo_raw *r, char *why, size_t size)
{
    static const uint8_t want[6] = { 16, 8, 8, 8, 0, 8 };
    bool colours_ok = true;
    for (uint32_t i = 0; i < 6; i++)
        colours_ok = colours_ok && r->colour[i] == want[i];
    uint64_t bytes = (uint64_t)r->pitch * r->height;

    if (r->bpp != 32 && r->bpp != 24)
        ksnprintf(why, size, "depth not 32 or 24");
    else if (!colours_ok)
        ksnprintf(why, size, "colours red %u/%u green %u/%u blue %u/%u, not 16/8 8/8 0/8",
                  r->colour[0], r->colour[1], r->colour[2], r->colour[3], r->colour[4], r->colour[5]);
    else if (r->width < FBINFO_MIN_WIDTH || r->height < FBINFO_MIN_HEIGHT)
        ksnprintf(why, size, "smaller than %ux%u", FBINFO_MIN_WIDTH, FBINFO_MIN_HEIGHT);
    else if (r->width > FBINFO_MAX_SIDE || r->height > FBINFO_MAX_SIDE)
        ksnprintf(why, size, "larger than %ux%u", FBINFO_MAX_SIDE, FBINFO_MAX_SIDE);
    else if (r->pitch < r->width * (r->bpp / 8u))
        ksnprintf(why, size, "pitch %u below the width x %u bytes per pixel", r->pitch, r->bpp / 8u);
    else if (r->pitch > FBINFO_MAX_PITCH)
        ksnprintf(why, size, "pitch %u above %u", r->pitch, FBINFO_MAX_PITCH);
    else if (r->addr == 0)
        ksnprintf(why, size, "address 0");
    else if (r->addr >= FBINFO_PHYS_LIMIT || bytes > FBINFO_PHYS_LIMIT - r->addr)
        ksnprintf(why, size, "beyond the 52-bit physical range");
    else
        return false;
    return true;
}

enum fb_kind fbinfo_parse(const struct fbinfo_raw *raw, struct fb_info *out, char *line)
{
    out->kind = FB_NONE;
    out->addr = 0;
    out->pitch = out->width = out->height = out->bpp = out->bytes_per_pixel = 0;
    if (!(raw->flags & MULTIBOOT_INFO_FRAMEBUFFER)) {
        ksnprintf(line, FBINFO_LINE, "Display: VGA text 80x25 (no framebuffer)");
        return out->kind;
    }
    if (raw->type == MBFB_TYPE_TEXT) {
        out->kind = FB_TEXT;
        ksnprintf(line, FBINFO_LINE, "Display: VGA text 80x25 (the boot loader's text mode, %ux%u at 0x%llx)",
                  raw->width, raw->height, (unsigned long long)raw->addr);
        return out->kind;
    }
    char why[80];
    bool refused = true;
    if (raw->type == MBFB_TYPE_INDEXED)
        ksnprintf(why, sizeof(why), "type 0, indexed colours");
    else if (raw->type != MBFB_TYPE_RGB)
        ksnprintf(why, sizeof(why), "type %u, unknown", raw->type);
    else
        refused = rgb_refused(raw, why, sizeof(why));
    if (refused) {
        out->kind = FB_UNSUPPORTED;
        ksnprintf(line, FBINFO_LINE, "Display: framebuffer %ux%ux%u at 0x%llx not supported (%s); "
                  "VGA text and serial kept", raw->width, raw->height, raw->bpp,
                  (unsigned long long)raw->addr, why);
        return out->kind;
    }
    out->kind = FB_USABLE;
    out->addr = raw->addr;
    out->pitch = raw->pitch;
    out->width = raw->width;
    out->height = raw->height;
    out->bpp = raw->bpp;
    out->bytes_per_pixel = raw->bpp / 8u;
    ksnprintf(line, FBINFO_LINE, "Display: framebuffer %ux%ux%u at 0x%llx, pitch %u, RGB at bits 16/8/0",
              raw->width, raw->height, raw->bpp, (unsigned long long)raw->addr, raw->pitch);
    return out->kind;
}

#define FBPLAN_PAGE       4096u
#define FBPLAN_PT_PAGES    512u     /* pages one page table maps */

static uint32_t pages_for(uint64_t bytes)
{
    return (uint32_t)((bytes + FBPLAN_PAGE - 1) / FBPLAN_PAGE);
}

static uint32_t tables_for(uint32_t pages)
{
    return (pages + FBPLAN_PT_PAGES - 1) / FBPLAN_PT_PAGES;
}

/* The sizes, from a FB_USABLE fb (fbinfo_parse bounds every field). */
static void plan_sizes(const struct fb_info *fb, struct fb_plan *p)
{
    p->fb_phys = fb->addr & ~(uint64_t)(FBPLAN_PAGE - 1);
    p->fb_offset = (uint32_t)(fb->addr & (FBPLAN_PAGE - 1));
    p->fb_pages = pages_for((uint64_t)p->fb_offset + (uint64_t)fb->pitch * fb->height);
    p->bb_pitch = fb->width * fb->bytes_per_pixel;
    p->bb_pages = pages_for((uint64_t)p->bb_pitch * fb->height);
    p->tables = 1 + 2 + tables_for(p->fb_pages) + tables_for(p->bb_pages);
}

bool fbinfo_overlaps(const struct fb_info *fb, const struct fb_ram *ram)
{
    struct fb_plan p;
    plan_sizes(fb, &p);
    uint64_t start = p.fb_phys, end = p.fb_phys + (uint64_t)p.fb_pages * FBPLAN_PAGE;
    return ram->length != 0 && start < ram->base + ram->length && ram->base < end;
}

enum fb_plan_kind fbinfo_plan(const struct fb_info *fb, const struct fb_ram *overlap,
                              uint32_t free_pages, struct fb_plan *plan, char *line)
{
    plan_sizes(fb, plan);
    line[0] = '\0';
    if (overlap) {
        ksnprintf(line, FBINFO_LINE, "Display: framebuffer %ux%ux%u at 0x%llx overlaps usable RAM "
                  "0x%llx-0x%llx (memory map); not mapped, VGA text and serial kept",
                  fb->width, fb->height, fb->bpp, (unsigned long long)fb->addr,
                  (unsigned long long)overlap->base,
                  (unsigned long long)(overlap->base + overlap->length));
        return FBPLAN_OVERLAP;
    }
    uint32_t need = plan->bb_pages + plan->tables;
    if (plan->tables > FBPLAN_MAX_TABLES || need > free_pages / FBPLAN_SHARE) {
        ksnprintf(line, FBINFO_LINE, "Display: framebuffer %ux%ux%u at 0x%llx not mapped (back buffer "
                  "and page tables: %u pages, over a quarter of the %u free pages); VGA text and "
                  "serial kept", fb->width, fb->height, fb->bpp, (unsigned long long)fb->addr,
                  need, free_pages);
        return FBPLAN_NO_ROOM;
    }
    return FBPLAN_MAP;
}
