/* Unit tests for kernel/display/fbinfo_core.c (the display the boot loader
 * gave, 1.2 stage 5). The module and the formatter it uses
 * (kernel/kprintf.c, ksnprintf) are included directly, with kprintf.c's
 * output drivers replaced by stubs, as in test_kprintf.c. */
#include <stdint.h>
#include <string.h>
#include "tests/unit/unit.h"

void vga_putc(char c) { (void)c; }
void serial_putc(char c) { (void)c; }
/* 0.9: the preemption-disable count kprintf.c uses (kernel/proc/thread.c). */
volatile uint32_t preempt_count_value;
void preempt_underflow(void) { printf("    FAIL: preempt_enable() underflow\n"); unit_failures++; }
/* 0.11, policy (b): never set here, so preempt_enable() never switches. */
volatile uint8_t preempt_resched_pending;
void preempt_resched(void) {}
/* 0.9: kputc's thread-0 screen check (kernel/proc/thread.c). */
#include <stdbool.h>
#include <stdlib.h>
#include "kernel/proc/thread.h"
bool thread_screen_allowed(void) { return true; }
void thread_screen_violation(void) { printf("    FAIL: screen violation\n"); exit(1); }

#include "kernel/kprintf.c"
#include "kernel/display/fbinfo_core.c"
#include "kernel/display/mbfb.h"

/* A Multiboot information block (120 bytes, as Limine allocates it). */
static uint8_t info[120];
static struct fbinfo_raw raw;
static struct fb_info fb;
static char line[FBINFO_LINE];

static const uint8_t RGB[6] = { 16, 8, 8, 8, 0, 8 };
#define FLAGS_FB  0x00001245u   /* the stage 3 probe's flags: bit 12 set */
#define FLAGS_KERNEL 0x0000024fu /* bit 12 clear (QEMU -kernel) */

static void put32(uint32_t off, uint32_t v)
{
    for (int i = 0; i < 4; i++)
        info[off + i] = (uint8_t)(v >> (8 * i));
}

/* Fills the block: flags, the framebuffer fields, colours at byte 112;
 * every other byte 0xA5 (bytes 110-111 among them: padding). */
static void make(uint32_t flags, uint64_t addr, uint32_t pitch, uint32_t w, uint32_t h,
                 uint8_t bpp, uint8_t type, const uint8_t colour[6])
{
    memset(info, 0xA5, sizeof(info));
    put32(0, flags);
    put32(MBFB_ADDR, (uint32_t)addr);
    put32(MBFB_ADDR + 4, (uint32_t)(addr >> 32));
    put32(MBFB_PITCH, pitch);
    put32(MBFB_WIDTH, w);
    put32(MBFB_HEIGHT, h);
    info[MBFB_BPP] = bpp;
    info[MBFB_TYPE] = type;
    for (int i = 0; i < 6; i++)
        info[MBFB_COLOUR + i] = colour[i];
}

static enum fb_kind parse(void)
{
    fbinfo_read(info, &raw);
    return fbinfo_parse(&raw, &fb, line);
}

static int line_is(const char *expected)
{
    if (strcmp(line, expected) == 0)
        return 1;
    printf("    got      \"%s\"\n    expected \"%s\"\n", line, expected);
    return 0;
}

/* The raw bytes 108-119 the stage 3 probe read from Limine 12.9.3 (32 bpp):
 * the colour fields at byte 112, bytes 110-111 padding. */
static void test_probe_bytes(void)
{
    static const uint8_t bytes[12] = { 0x20, 0x01, 0x00, 0x00, 0x10, 0x08, 0x08, 0x08, 0x00, 0x08, 0x00, 0x00 };
    make(FLAGS_FB, 0xfd000000u, 4096, 1024, 768, 0, 0, RGB);
    memcpy(info + 108, bytes, sizeof(bytes));
    CHECK_EQ(parse(), FB_USABLE);
    CHECK_EQ(raw.bpp, 32);
    CHECK_EQ(raw.type, 1);
    CHECK(memcmp(raw.colour, RGB, 6) == 0);
    CHECK_EQ(fb.kind, FB_USABLE);
    CHECK_EQ(fb.addr, 0xfd000000u);
    CHECK_EQ(fb.pitch, 4096);
    CHECK_EQ(fb.width, 1024);
    CHECK_EQ(fb.height, 768);
    CHECK_EQ(fb.bpp, 32);
    CHECK_EQ(fb.bytes_per_pixel, 4);
    CHECK(line_is("Display: framebuffer 1024x768x32 at 0xfd000000, pitch 4096, RGB at bits 16/8/0"));
}

/* Bytes 110-111 are padding: whatever they hold, the colours are read at 112. */
static void test_padding_ignored(void)
{
    make(FLAGS_FB, 0xe0000000u, 4096, 1024, 768, 32, 1, RGB);
    info[110] = 0x10;
    info[111] = 0x08;
    CHECK_EQ(parse(), FB_USABLE);
    CHECK(line_is("Display: framebuffer 1024x768x32 at 0xe0000000, pitch 4096, RGB at bits 16/8/0"));
}

/* The colour fields where the specification's table puts them (byte 110)
 * are not read there: the fields at 112 then hold other values. */
static void test_colours_at_110_refused(void)
{
    static const uint8_t at110[8] = { 0x10, 0x08, 0x08, 0x08, 0x00, 0x08, 0x00, 0x00 };
    make(FLAGS_FB, 0xfd000000u, 4096, 1024, 768, 32, 1, RGB);
    memcpy(info + 110, at110, sizeof(at110));
    CHECK_EQ(parse(), FB_UNSUPPORTED);
    CHECK(line_is("Display: framebuffer 1024x768x32 at 0xfd000000 not supported (colours red 8/8 "
                  "green 0/8 blue 0/0, not 16/8 8/8 0/8); VGA text and serial kept"));
}

/* QEMU -kernel: flag bit 12 clear. Nothing past the flags is read. */
static void test_no_framebuffer(void)
{
    make(FLAGS_KERNEL, 0xfd000000u, 4096, 1024, 768, 32, 1, RGB);
    CHECK_EQ(parse(), FB_NONE);
    CHECK_EQ(raw.flags, FLAGS_KERNEL);
    CHECK_EQ(raw.addr, 0);
    CHECK_EQ(raw.width, 0);
    CHECK_EQ(raw.type, 0);
    CHECK_EQ(raw.colour[0], 0);
    CHECK_EQ(fb.kind, FB_NONE);
    CHECK_EQ(fb.width, 0);
    CHECK(line_is("Display: VGA text 80x25 (no framebuffer)"));
}

/* Limine with "textmode: yes": type 2, 80x25 at 0xB8000 (stage 3 probe (b)). */
static void test_text_mode(void)
{
    make(FLAGS_FB, 0xb8000u, 160, 80, 25, 16, 2, RGB);
    CHECK_EQ(parse(), FB_TEXT);
    CHECK_EQ(fb.kind, FB_TEXT);
    CHECK_EQ(fb.width, 0);
    CHECK(line_is("Display: VGA text 80x25 (the boot loader's text mode, 80x25 at 0xb8000)"));
}

static void test_24bpp(void)
{
    make(FLAGS_FB, 0xfd000000u, 3840, 1280, 800, 24, 1, RGB);
    CHECK_EQ(parse(), FB_USABLE);
    CHECK_EQ(fb.bpp, 24);
    CHECK_EQ(fb.bytes_per_pixel, 3);
    CHECK(line_is("Display: framebuffer 1280x800x24 at 0xfd000000, pitch 3840, RGB at bits 16/8/0"));
}

static void test_sizes(void)
{
    make(FLAGS_FB, 0xfd000000u, 3200, 800, 600, 32, 1, RGB);
    CHECK_EQ(parse(), FB_USABLE);
    make(FLAGS_FB, 0xfd000000u, 3200, 799, 600, 32, 1, RGB);
    CHECK_EQ(parse(), FB_UNSUPPORTED);
    CHECK(line_is("Display: framebuffer 799x600x32 at 0xfd000000 not supported (smaller than "
                  "800x600); VGA text and serial kept"));
    make(FLAGS_FB, 0xfd000000u, 3200, 800, 599, 32, 1, RGB);
    CHECK_EQ(parse(), FB_UNSUPPORTED);
    make(FLAGS_FB, 0xfd000000u, 16384, 4096, 4096, 32, 1, RGB);
    CHECK_EQ(parse(), FB_USABLE);
    make(FLAGS_FB, 0xfd000000u, 16388, 4097, 768, 32, 1, RGB);
    CHECK_EQ(parse(), FB_UNSUPPORTED);
    CHECK(line_is("Display: framebuffer 4097x768x32 at 0xfd000000 not supported (larger than "
                  "4096x4096); VGA text and serial kept"));
    make(FLAGS_FB, 0xfd000000u, 4096, 1024, 4097, 32, 1, RGB);
    CHECK_EQ(parse(), FB_UNSUPPORTED);
}

static void test_types(void)
{
    make(FLAGS_FB, 0xfd000000u, 1024, 1024, 768, 8, 0, RGB);
    CHECK_EQ(parse(), FB_UNSUPPORTED);
    CHECK_EQ(fb.kind, FB_UNSUPPORTED);
    CHECK_EQ(fb.width, 0);
    CHECK(line_is("Display: framebuffer 1024x768x8 at 0xfd000000 not supported (type 0, indexed "
                  "colours); VGA text and serial kept"));
    make(FLAGS_FB, 0xfd000000u, 4096, 1024, 768, 32, 3, RGB);
    CHECK_EQ(parse(), FB_UNSUPPORTED);
    CHECK(line_is("Display: framebuffer 1024x768x32 at 0xfd000000 not supported (type 3, unknown); "
                  "VGA text and serial kept"));
}

static void test_depth_and_colours(void)
{
    static const uint8_t bgr[6] = { 0, 8, 8, 8, 16, 8 };
    static const uint8_t rgb565[6] = { 11, 5, 5, 6, 0, 5 };
    make(FLAGS_FB, 0xfd000000u, 2048, 1024, 768, 16, 1, rgb565);
    CHECK_EQ(parse(), FB_UNSUPPORTED);
    CHECK(line_is("Display: framebuffer 1024x768x16 at 0xfd000000 not supported (depth not 32 or 24); "
                  "VGA text and serial kept"));
    make(FLAGS_FB, 0xfd000000u, 4096, 1024, 768, 32, 1, bgr);
    CHECK_EQ(parse(), FB_UNSUPPORTED);
    CHECK(line_is("Display: framebuffer 1024x768x32 at 0xfd000000 not supported (colours red 0/8 "
                  "green 8/8 blue 16/8, not 16/8 8/8 0/8); VGA text and serial kept"));
}

static void test_pitch(void)
{
    make(FLAGS_FB, 0xfd000000u, 4095, 1024, 768, 32, 1, RGB);
    CHECK_EQ(parse(), FB_UNSUPPORTED);
    CHECK(line_is("Display: framebuffer 1024x768x32 at 0xfd000000 not supported (pitch 4095 below "
                  "the width x 4 bytes per pixel); VGA text and serial kept"));
    make(FLAGS_FB, 0xfd000000u, 4096, 1024, 768, 32, 1, RGB);
    CHECK_EQ(parse(), FB_USABLE);
    make(FLAGS_FB, 0xfd000000u, 3071, 1024, 768, 24, 1, RGB);
    CHECK_EQ(parse(), FB_UNSUPPORTED);
    CHECK(line_is("Display: framebuffer 1024x768x24 at 0xfd000000 not supported (pitch 3071 below "
                  "the width x 3 bytes per pixel); VGA text and serial kept"));
    make(FLAGS_FB, 0xfd000000u, 3072, 1024, 768, 24, 1, RGB);
    CHECK_EQ(parse(), FB_USABLE);
    /* Pitch 0. */
    make(FLAGS_FB, 0xfd000000u, 0, 1024, 768, 32, 1, RGB);
    CHECK_EQ(parse(), FB_UNSUPPORTED);
    make(FLAGS_FB, 0xfd000000u, 65536, 1024, 768, 32, 1, RGB);
    CHECK_EQ(parse(), FB_USABLE);
    make(FLAGS_FB, 0xfd000000u, 65537, 1024, 768, 32, 1, RGB);
    CHECK_EQ(parse(), FB_UNSUPPORTED);
    CHECK(line_is("Display: framebuffer 1024x768x32 at 0xfd000000 not supported (pitch 65537 above "
                  "65536); VGA text and serial kept"));
}

static void test_address(void)
{
    const uint64_t limit = 1ull << 52, bytes = 4096ull * 768;
    make(FLAGS_FB, 0, 4096, 1024, 768, 32, 1, RGB);
    CHECK_EQ(parse(), FB_UNSUPPORTED);
    CHECK(line_is("Display: framebuffer 1024x768x32 at 0x0 not supported (address 0); "
                  "VGA text and serial kept"));
    make(FLAGS_FB, 0x800000000ull, 4096, 1024, 768, 32, 1, RGB);
    CHECK_EQ(parse(), FB_USABLE);
    CHECK_EQ(fb.addr, 0x800000000ull);
    CHECK(line_is("Display: framebuffer 1024x768x32 at 0x800000000, pitch 4096, RGB at bits 16/8/0"));
    make(FLAGS_FB, limit - bytes, 4096, 1024, 768, 32, 1, RGB);
    CHECK_EQ(parse(), FB_USABLE);
    make(FLAGS_FB, limit - bytes + 4096, 4096, 1024, 768, 32, 1, RGB);
    CHECK_EQ(parse(), FB_UNSUPPORTED);
    CHECK(line_is("Display: framebuffer 1024x768x32 at 0xfffffffd01000 not supported (beyond the "
                  "52-bit physical range); VGA text and serial kept"));
    make(FLAGS_FB, 0xffffffffff000000ull, 4096, 1024, 768, 32, 1, RGB);
    CHECK_EQ(parse(), FB_UNSUPPORTED);
}

/* The longest line fits the buffer whole (FBINFO_LINE). */
static void test_longest_line(void)
{
    static const uint8_t wide[6] = { 255, 255, 255, 255, 255, 255 };
    make(FLAGS_FB, 0xffffffffffffffffull, 4294967295u, 4294967295u, 4294967295u, 255, 1, wide);
    CHECK_EQ(parse(), FB_UNSUPPORTED);
    CHECK(strlen(line) < FBINFO_LINE - 1);
    CHECK(line_is("Display: framebuffer 4294967295x4294967295x255 at 0xffffffffffffffff not supported "
                  "(depth not 32 or 24); VGA text and serial kept"));
    make(FLAGS_FB, 0xffffffffffffffffull, 4294967295u, 4294967295u, 4294967295u, 32, 1, wide);
    CHECK_EQ(parse(), FB_UNSUPPORTED);
    CHECK(strlen(line) < FBINFO_LINE - 1);
    CHECK(line_is("Display: framebuffer 4294967295x4294967295x32 at 0xffffffffffffffff not supported "
                  "(colours red 255/255 green 255/255 blue 255/255, not 16/8 8/8 0/8); "
                  "VGA text and serial kept"));
}

/* ---- 1.2 stage 5b: the plan for slot 506 ---- */

static struct fb_plan plan;

static struct fb_info usable(uint64_t addr, uint32_t pitch, uint32_t w, uint32_t h, uint8_t bpp)
{
    make(FLAGS_FB, addr, pitch, w, h, bpp, 1, RGB);
    parse();
    return fb;
}

/* The requested mode on the normal boot's 32,160 free pages: mapped. The
 * sizes: 768 framebuffer pages, a back buffer of 4096-byte rows (768
 * pages), 1 PDPT + 2 directories + 2 + 2 page tables. */
static void test_plan_1024x768(void)
{
    struct fb_info f = usable(0xfd000000u, 4096, 1024, 768, 32);
    line[0] = 'x';
    CHECK_EQ(fbinfo_plan(&f, NULL, 32160, &plan, line), FBPLAN_MAP);
    CHECK_EQ(line[0], '\0');
    CHECK_EQ(plan.fb_phys, 0xfd000000u);
    CHECK_EQ(plan.fb_offset, 0);
    CHECK_EQ(plan.fb_pages, 768);
    CHECK_EQ(plan.bb_pitch, 4096);
    CHECK_EQ(plan.bb_pages, 768);
    CHECK_EQ(plan.tables, 7);
    /* And on a 32 MiB machine's 7,597: 775 pages, at most 1,899. */
    CHECK_EQ(fbinfo_plan(&f, NULL, 7597, &plan, line), FBPLAN_MAP);
}

/* A wide pitch and a framebuffer not on a page boundary: the framebuffer
 * maps whole pages from its first one; the back buffer's rows are exact. */
static void test_plan_sizes(void)
{
    struct fb_info f = usable(0xe0000100u, 3200, 800, 600, 24);
    CHECK_EQ(fbinfo_plan(&f, NULL, 32160, &plan, line), FBPLAN_MAP);
    CHECK_EQ(plan.fb_phys, 0xe0000000u);
    CHECK_EQ(plan.fb_offset, 0x100);
    CHECK_EQ(plan.fb_pages, 469);           /* 256 + 3200 x 600 = 1,920,256 bytes */
    CHECK_EQ(plan.bb_pitch, 2400);
    CHECK_EQ(plan.bb_pages, 352);           /* 2400 x 600 = 1,440,000 bytes */
    CHECK_EQ(plan.tables, 5);
    f = usable(0xfd000000u, 7680, 1920, 1080, 32);
    fbinfo_plan(&f, NULL, 32160, &plan, line);
    CHECK_EQ(plan.fb_pages, 2025);
    CHECK_EQ(plan.bb_pages, 2025);
    CHECK_EQ(plan.tables, 11);              /* 1 + 2 + 4 + 4 */
}

/* Up to a quarter of the free pages: the back buffer and the slot's
 * tables, 775 pages at 1024x768x32. */
static void test_plan_no_room(void)
{
    struct fb_info f = usable(0xfd000000u, 4096, 1024, 768, 32);
    CHECK_EQ(fbinfo_plan(&f, NULL, 3100, &plan, line), FBPLAN_MAP);      /* 775 = 3100 / 4 */
    CHECK_EQ(fbinfo_plan(&f, NULL, 3099, &plan, line), FBPLAN_NO_ROOM);
    CHECK(line_is("Display: framebuffer 1024x768x32 at 0xfd000000 not mapped (back buffer and page "
                  "tables: 775 pages, over a quarter of the 3099 free pages); VGA text and serial kept"));
    CHECK_EQ(plan.bb_pages, 768);           /* the sizes are filled in anyway */
    CHECK_EQ(fbinfo_plan(&f, NULL, 0, &plan, line), FBPLAN_NO_ROOM);
    /* The largest mode the parser accepts on the normal boot: 16384 + 3 +
     * 32 + 32 pages, over 8040. */
    f = usable(0xfd000000u, 16384, 4096, 4096, 32);
    CHECK_EQ(fbinfo_plan(&f, NULL, 32160, &plan, line), FBPLAN_NO_ROOM);
    CHECK(line_is("Display: framebuffer 4096x4096x32 at 0xfd000000 not mapped (back buffer and page "
                  "tables: 16451 pages, over a quarter of the 32160 free pages); VGA text and serial kept"));
    /* 1920x1080x32 on a 32 MiB machine: 2036 pages, over 1899. */
    f = usable(0xfd000000u, 7680, 1920, 1080, 32);
    CHECK_EQ(fbinfo_plan(&f, NULL, 7597, &plan, line), FBPLAN_NO_ROOM);
    CHECK_EQ(fbinfo_plan(&f, NULL, 4 * 2036, &plan, line), FBPLAN_MAP);
}

/* The most tables a usable framebuffer needs is FBPLAN_MAX_TABLES: a pitch
 * of 65536 x 4096 rows at an offset of one byte short of a page, with the
 * back buffer at 4096x4096x32. */
static void test_plan_max_tables(void)
{
    struct fb_info f = usable(0x800000ffcull, 65536, 4096, 4096, 32);
    CHECK_EQ(f.kind, FB_USABLE);
    fbinfo_plan(&f, NULL, 0xFFFFFFFFu, &plan, line);
    CHECK_EQ(plan.fb_pages, 65537);
    CHECK_EQ(plan.bb_pages, 16384);
    CHECK_EQ(plan.tables, FBPLAN_MAX_TABLES);
    CHECK_EQ(fbinfo_plan(&f, NULL, 0xFFFFFFFFu, &plan, line), FBPLAN_MAP);
}

/* Usable RAM over any of the framebuffer's pages (rounded out) refuses it. */
static void test_plan_overlap(void)
{
    struct fb_info f = usable(0xfd000000u, 4096, 1024, 768, 32);   /* 0xfd000000-0xfd300000 */
    struct fb_ram below = { 0x100000u, 0xfcf00000u };              /* ends at 0xfd000000 */
    struct fb_ram above = { 0xfd300000u, 0x1000u };
    struct fb_ram first = { 0xfcfff000u, 0x2000u };                /* its first page */
    struct fb_ram last = { 0xfd2ff000u, 0x1000u };                 /* its last page */
    struct fb_ram inside = { 0xfd100000u, 0x1000u };
    struct fb_ram around = { 0, 0x100000000ull };
    struct fb_ram empty = { 0xfd100000u, 0 };
    CHECK(!fbinfo_overlaps(&f, &below));
    CHECK(!fbinfo_overlaps(&f, &above));
    CHECK(fbinfo_overlaps(&f, &first));
    CHECK(fbinfo_overlaps(&f, &last));
    CHECK(fbinfo_overlaps(&f, &inside));
    CHECK(fbinfo_overlaps(&f, &around));
    CHECK(!fbinfo_overlaps(&f, &empty));
    CHECK_EQ(fbinfo_plan(&f, &first, 32160, &plan, line), FBPLAN_OVERLAP);
    CHECK(line_is("Display: framebuffer 1024x768x32 at 0xfd000000 overlaps usable RAM "
                  "0xfcfff000-0xfd001000 (memory map); not mapped, VGA text and serial kept"));
    /* An unaligned framebuffer: its first page counts, from its start. */
    f = usable(0xfd000800u, 4096, 1024, 768, 32);
    struct fb_ram page_below = { 0xfcfff000u, 0x1000u };
    struct fb_ram same_page = { 0xfd000000u, 0x800u };
    CHECK(!fbinfo_overlaps(&f, &page_below));
    CHECK(fbinfo_overlaps(&f, &same_page));
    /* The overlap is refused before the room is looked at. */
    CHECK_EQ(fbinfo_plan(&f, &same_page, 0, &plan, line), FBPLAN_OVERLAP);
}

/* The refusals' longest lines fit the buffer whole. */
static void test_plan_longest_line(void)
{
    struct fb_info f = usable((1ull << 52) - 65536ull * 4096, 65536, 4096, 4096, 32);
    CHECK_EQ(f.kind, FB_USABLE);
    struct fb_ram huge = { 0xffffffffffff0000ull, 0xffff };
    CHECK_EQ(fbinfo_plan(&f, &huge, 0, &plan, line), FBPLAN_OVERLAP);
    CHECK(strlen(line) < FBINFO_LINE - 1);
    /* A refusal needs fewer than 4 x 16548 free pages, so its numbers have
     * at most 5 digits: 66187 is the most free pages refused here. */
    CHECK_EQ(fbinfo_plan(&f, NULL, 66187, &plan, line), FBPLAN_NO_ROOM);
    CHECK(strlen(line) < FBINFO_LINE - 1);
    CHECK(line_is("Display: framebuffer 4096x4096x32 at 0xffffff0000000 not mapped (back buffer and "
                  "page tables: 16547 pages, over a quarter of the 66187 free pages); VGA text and "
                  "serial kept"));
    CHECK_EQ(fbinfo_plan(&f, NULL, 66188, &plan, line), FBPLAN_MAP);
}

int main(void)
{
    RUN(test_probe_bytes);
    RUN(test_padding_ignored);
    RUN(test_colours_at_110_refused);
    RUN(test_no_framebuffer);
    RUN(test_text_mode);
    RUN(test_24bpp);
    RUN(test_sizes);
    RUN(test_types);
    RUN(test_depth_and_colours);
    RUN(test_pitch);
    RUN(test_address);
    RUN(test_longest_line);
    RUN(test_plan_1024x768);
    RUN(test_plan_sizes);
    RUN(test_plan_no_room);
    RUN(test_plan_max_tables);
    RUN(test_plan_overlap);
    RUN(test_plan_longest_line);
    return unit_report("fbinfo");
}
