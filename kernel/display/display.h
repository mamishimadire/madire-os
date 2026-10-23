#ifndef MADIRE_DISPLAY_DISPLAY_H
#define MADIRE_DISPLAY_DISPLAY_H

/*
 * The display the boot loader gave (1.2 stage 5): read once from the
 * Multiboot information, logged as the "Display: ..." boot line. Until a
 * later stage draws on a framebuffer, every boot keeps VGA text and serial
 * (decision 4); only the verdict changes what is logged.
 */

#include "kernel/display/fbinfo_core.h"

/* Reads the framebuffer fields of the Multiboot information at physical
 * address `mb_info` and logs the "Display: ..." line. Must run before
 * memory_init(): the fields reach byte 117, past the 116 bytes reserve.c
 * keeps (mbfb.h). */
void display_probe(uint32_t mb_info);

/* What display_probe() found. */
const struct fb_info *display_info(void);

#endif
