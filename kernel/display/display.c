/*
 * The display the boot loader gave (1.2 stage 5); see display.h.
 */
#include "kernel/display/display.h"
#include "kernel/kprintf.h"
#include "kernel/memory/kmalloc.h"
#include "kernel/memory/mmap.h"
#include "kernel/memory/paging.h"
#include "kernel/memory/pmm.h"
#include "kernel/memory/vm_layout.h"

/* boot/boot.asm's tables: the physical view covers 0-4 GiB. */
#define BOOT_VIEW_END (4ull << 30)

static struct fb_info info;
static struct fb_plan plan;
static bool planned, refused, active;
static struct surface fb, back;
static uint32_t frames;
static uint32_t fail_step;              /* test=fbfail=N */

void display_probe(uint32_t mb_info)
{
    struct fbinfo_raw raw;
    char line[FBINFO_LINE];
    /* The boot tables still map 0-4 GiB in the physical view. */
    fbinfo_read(phys_view(mb_info), &raw);
    fbinfo_parse(&raw, &info, line);
    klogf("%s\n", line);
}

const struct fb_info *display_info(void)
{
    return &info;
}

void display_plan_slot(const struct multiboot_info *mbi)
{
    planned = false;
    if (info.kind != FB_USABLE || !(mbi->flags & MULTIBOOT_INFO_MEM_MAP))
        return;
    /* memory_init() validated this map (or stopped the boot); validated
     * again here, as mmap_next() requires, through the same view. */
    const void *view = phys_view(mbi->mmap_addr);
    if (mmap_validate(mbi->mmap_addr, mbi->mmap_length, view, 0) != MMAP_OK)
        return;
    struct mmap_iter it;
    struct mem_region r;
    struct fb_ram ram, *overlap = 0;
    mmap_iter_init(&it, view, mbi->mmap_length);
    while (!overlap && mmap_next(&it, &r)) {
        ram.base = r.base;
        ram.length = r.length;
        if (r.kind == MEM_USABLE && fbinfo_overlaps(&info, &ram))
            overlap = &ram;
    }
    char line[FBINFO_LINE];
    if (fbinfo_plan(&info, overlap, pmm_free_pages(), &plan, line) == FBPLAN_MAP) {
        planned = true;
    } else {
        refused = true;
        klogf("%s\n", line);
    }
}

const struct fb_plan *display_slot_plan(void)
{
    return planned ? &plan : 0;
}

void display_test_fail_at(uint32_t step)
{
    fail_step = step;
}

uint32_t display_test_fail_step(void)
{
    return fail_step;
}

void display_slot_ready(void)
{
    if (!planned)
        return;
    active = surface_init(&fb, (void *)(uintptr_t)(VM_FB_BASE + plan.fb_offset), info.width,
                          info.height, info.pitch, info.bytes_per_pixel) &&
             surface_init(&back, (void *)(uintptr_t)VM_BACKBUF_BASE, info.width, info.height,
                          plan.bb_pitch, info.bytes_per_pixel);
}

bool display_active(void)
{
    return active;
}

struct surface *display_back_buffer(void)
{
    return active ? &back : 0;
}

struct surface *display_front_buffer(void)
{
    return active ? &fb : 0;
}

uint32_t display_present(void)
{
    surface_blit(&fb, 0, 0, &back, 0, 0, (int32_t)back.width, (int32_t)back.height);
    return ++frames;
}

struct surface *display_crash_screen(void)
{
    if (active)
        return &fb;
    if (info.kind != FB_USABLE || refused || paging_enabled() ||
        info.addr + (uint64_t)info.pitch * info.height > BOOT_VIEW_END)
        return 0;
    return surface_init(&fb, phys_view(info.addr), info.width, info.height, info.pitch, info.bytes_per_pixel)
           ? &fb : 0;
}

/* ---- test=fbpattern ---- */

#define SPRITE 32u

/* A colour for rectangle i: every channel changes from one to the next. */
static uint32_t pattern_colour(uint32_t i)
{
    return ((i * 53u + 40u) & 0xFFu) << 16 | ((i * 97u + 80u) & 0xFFu) << 8 | ((i * 29u + 120u) & 0xFFu);
}

void display_fbpattern(void)
{
    if (!active) {
        klogf("fbpattern: no framebuffer mapped (see the Display: lines); nothing drawn\n");
        return;
    }
    int32_t w = (int32_t)back.width, h = (int32_t)back.height;
    uint32_t rects = 0, blits = 0;

    surface_fill(&back, (struct rect){ 0, 0, w, h }, 0x0B1220u);
    rects++;
    /* An 8 x 6 grid of tiles, each inset by 4 pixels. */
    for (uint32_t i = 0; i < 48; i++, rects++) {
        int32_t tw = w / 8, th = h / 6;
        surface_fill(&back, (struct rect){ (int32_t)(i % 8) * tw + 4, (int32_t)(i / 8) * th + 4,
                                           tw - 8, th - 8 }, pattern_colour(i));
    }
    /* Clipped: past the top-left corner, past the bottom-right one,
     * across the top edge, and one wholly outside (draws nothing). */
    surface_fill(&back, (struct rect){ -40, -30, 120, 90 }, pattern_colour(60));
    surface_fill(&back, (struct rect){ w - 60, h - 50, 200, 200 }, pattern_colour(61));
    surface_fill(&back, (struct rect){ w / 2, -100, 50, 150 }, pattern_colour(62));
    surface_fill(&back, (struct rect){ -500, 0, 100, 100 }, pattern_colour(63));
    rects += 4;

    /* A 32 x 32 sprite at 4 bytes per pixel, blitted whole, clipped at
     * the top-left and the bottom-right corners, in part, and from a
     * source rectangle partly outside the sprite (converted when the
     * framebuffer has 3 bytes per pixel). */
    uint8_t *pix = kmalloc(SPRITE * SPRITE * 4);
    struct surface sprite;
    if (pix && surface_init(&sprite, pix, SPRITE, SPRITE, SPRITE * 4, 4)) {
        for (uint32_t y = 0; y < SPRITE; y++)
            for (uint32_t x = 0; x < SPRITE; x++)
                surface_fill(&sprite, (struct rect){ (int32_t)x, (int32_t)y, 1, 1 },
                             (x * 8) << 16 | (y * 8) << 8 | (((x ^ y) * 8) & 0xFFu));
        surface_blit(&back, 16, 16, &sprite, 0, 0, SPRITE, SPRITE);
        surface_blit(&back, -10, -12, &sprite, 0, 0, SPRITE, SPRITE);
        surface_blit(&back, w - 20, h - 9, &sprite, 0, 0, SPRITE, SPRITE);
        surface_blit(&back, w / 3, h / 3, &sprite, 8, 8, 16, 16);
        surface_blit(&back, w / 2, h / 2, &sprite, -8, 20, 40, 40);
        blits = 5;
    }
    kfree(pix);

    uint32_t frame = display_present();
    uint32_t crc_back = surface_crc32(&back), crc_fb = surface_crc32(&fb);
    klogf("fbpattern: %ux%ux%u: %u fills and %u blits drawn on the back buffer, presented; CRC-32 of "
          "the RGB bytes: back buffer 0x%08x, framebuffer 0x%08x (%s)\n", info.width, info.height,
          info.bpp, rects, blits, crc_back, crc_fb, crc_back == crc_fb ? "equal" : "DIFFERENT");
    klogf("display: frame %u\n", frame);
}
