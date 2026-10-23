#ifndef MADIRE_DISPLAY_MBFB_H
#define MADIRE_DISPLAY_MBFB_H

/*
 * The Multiboot 1 framebuffer fields (1.2 stage 5), read as bytes at fixed
 * offsets of the Multiboot information. They are kept here, not in
 * kernel/multiboot.h (unchanged since 0.9), whose struct ends at byte 116
 * with a colour_info at byte 110.
 *
 * The colour fields start at byte 112, not at 110 as the table of the
 * Multiboot specification (0.6.96, "Boot information format") gives:
 *   - the 1.2 stage 3 probe read the raw bytes 108-119 from Limine 12.9.3
 *     (QEMU and VirtualBox) as 20 01 00 00 10 08 08 08 00 08 00 00: bpp 32,
 *     type 1, two bytes of padding, then red at bit 16 size 8, green 8/8,
 *     blue 0/8 (docs/plans/1.2.md, probe results (a));
 *   - Limine 12.9.3's own source, common/protos/multiboot1.h (struct
 *     multiboot1_info, lines 72-84), has a uint16_t fb_reserved after
 *     fb_type and the six colour bytes after it, at 112-117 (its comment:
 *     the colour union is 4-byte aligned in GRUB's header and in the
 *     specification's own), and common/protos/multiboot1.c lines 497-502
 *     write them there;
 *   - the specification's own example header (multiboot.h, in the same
 *     document) puts the colour fields in a union with a 32-bit member,
 *     which aligns them to byte 112 as well.
 * The block holds 118 bytes when flag bit 12 is set; kernel/memory/
 * reserve.c reserves sizeof(struct multiboot_info), 116, so the display
 * code copies these fields before memory_init() hands out any page.
 */

#define MULTIBOOT_INFO_FRAMEBUFFER (1u << 12)  /* the fields below are valid */

#define MBFB_ADDR    88u    /* uint64_t: physical address */
#define MBFB_PITCH   96u    /* uint32_t: bytes per line */
#define MBFB_WIDTH  100u    /* uint32_t: pixels, or characters in text mode */
#define MBFB_HEIGHT 104u    /* uint32_t: pixels, or characters in text mode */
#define MBFB_BPP    108u    /* uint8_t: bits per pixel */
#define MBFB_TYPE   109u    /* uint8_t: 0 indexed, 1 direct RGB, 2 EGA text */
#define MBFB_COLOUR 112u    /* 6 x uint8_t, type 1: red position, red size,
                               green position, green size, blue position,
                               blue size (bytes 110-111 are padding) */
#define MBFB_END    118u    /* the first byte after the fields */

#define MBFB_TYPE_INDEXED 0u
#define MBFB_TYPE_RGB     1u
#define MBFB_TYPE_TEXT    2u

#endif
