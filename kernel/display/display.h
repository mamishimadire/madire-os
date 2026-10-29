#ifndef MADIRE_DISPLAY_DISPLAY_H
#define MADIRE_DISPLAY_DISPLAY_H

/*
 * The display the boot loader gave (1.2 stage 5): read once from the
 * Multiboot information, logged as the "Display: ..." boot line. Until a
 * later stage draws on a framebuffer, every boot keeps VGA text and serial
 * (decision 4); only the verdict changes what is logged.
 *
 * 1.2 stage 5b: a usable framebuffer is mapped in PML4 slot 506 with a back
 * buffer (vm_layout.h), planned here after memory_init() and built by
 * paging_init(). Only thread 0 draws (decision 6); the console stays VGA
 * text and serial in 5b, so nothing draws on the framebuffer yet but the
 * test option test=fbpattern.
 */

#include <stdbool.h>
#include "kernel/display/fbinfo_core.h"
#include "kernel/display/surface_core.h"
#include "kernel/multiboot.h"

/* Reads the framebuffer fields of the Multiboot information at physical
 * address `mb_info` and logs the "Display: ..." line. Must run before
 * memory_init(): the fields reach byte 117, past the 116 bytes reserve.c
 * keeps (mbfb.h). */
void display_probe(uint32_t mb_info);

/* What display_probe() found. */
const struct fb_info *display_info(void);

/* After memory_init(), before paging_init(): for a usable framebuffer,
 * checks it against the usable RAM of the memory map (`mbi`, read through
 * the physical view) and the free pages, and either plans slot 506 or logs
 * the refusal (fbinfo_core.h). Nothing for any other display. */
void display_plan_slot(const struct multiboot_info *mbi);

/* The plan for paging_init(), or NULL if slot 506 is not to be built. */
const struct fb_plan *display_slot_plan(void);

/* test=fbfail=N (a test option, set before paging_init()): step N of
 * building slot 506 fails as a mapping would, so the undo path runs. The
 * steps count from 1: the framebuffer's pages first, then one per
 * back-buffer page (before its frame is taken). 0: no failure. */
void display_test_fail_at(uint32_t step);
uint32_t display_test_fail_step(void);

/* paging_init(): slot 506 is built and the kernel's tables are loaded. */
void display_slot_ready(void);

/* True once slot 506 is live: the framebuffer and the back buffer can be
 * drawn on. */
bool display_active(void);

/* The back buffer (the framebuffer's format), for thread 0 to draw on;
 * only while display_active(). */
struct surface *display_back_buffer(void);

/* The framebuffer itself (1.2 stage 6: the console copies each changed
 * cell to it); only while display_active(). */
struct surface *display_front_buffer(void);

/* Copies the whole back buffer to the framebuffer; returns the frame
 * number (1 for the first). Only while display_active(). */
uint32_t display_present(void);

/* 1.3 stage 3: the compositor presented a frame (its damaged rectangles,
 * kernel/display/compositor.h); returns its number, counted with
 * display_present's. */
uint32_t display_frame_done(void);

/* 1.2 stage 10, the early crash screen (fbcon.h): the framebuffer to draw
 * the crash screen on directly, or NULL. Slot 506's mapping once it is
 * live; before paging_init() loaded the kernel's tables, the boot tables'
 * physical view (0-4 GiB) of a usable framebuffer that lies below 4 GiB
 * and whose slot was not refused; otherwise NULL (the crash screen then
 * stays on serial only). */
struct surface *display_crash_screen(void);

/* test=fbpattern (a test option): draws a fixed pattern (fills and blits,
 * clipped ones among them) on the back buffer, presents it, logs the CRC-32
 * of the back buffer's and of the framebuffer's pixels (read back), then
 * the marker "display: frame N" (tests/qmp_fb.py takes its screendump
 * after it). Without a mapped framebuffer it logs that and draws nothing. */
void display_fbpattern(void);

#endif
