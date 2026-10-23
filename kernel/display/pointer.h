#ifndef MADIRE_DISPLAY_POINTER_H
#define MADIRE_DISPLAY_POINTER_H

/*
 * The mouse on thread 0 (1.2 stage 7): decodes the bytes IRQ12 queued
 * (drivers/mouse/ps2mouse.h), keeps the pointer's position and, on a boot
 * whose console is on the framebuffer, draws the pointer on the screen
 * (the framebuffer only: the back buffer keeps the console's picture).
 * Only thread 0 draws (decision 6). The sprite and its save-under are
 * kernel/display/pointer_core.h's.
 *
 * The pointer starts at the screen's centre and is hidden until the first
 * packet; a panic hides it for good (the crash screen shows no pointer).
 * With the boot option "mouselog" (off by default) or during test=mouse,
 * every packet and every draw is logged on serial (not in dmesg):
 *   mouse: packet <n>: dx <+-d> dy <+-d> buttons <L|-><M|-><R|->
 *          [ pressed <LMR>][ released <LMR>][ overflow (motion dropped)]
 *          [ at (<x>, <y>)]
 *   pointer: drawn at (<x>, <y>)
 * "at" only with a pointer on the screen (its clamped hot spot).
 */

#include <stdbool.h>
#include <stdint.h>
#include "kernel/display/surface_core.h"

/* After the mouse started: the pointer on the framebuffer console, if it
 * is active, and the logging switch (logs the "Mouse: packets ..." line). */
void pointer_start(bool log_packets);

/* Thread 0: every decoded packet moves the pointer (and is logged); the
 * pointer is then drawn once. Returns the number of packets taken. */
uint32_t pointer_poll(void);

/* The framebuffer console, around every copy to the framebuffer: hides
 * the pointer if r touches it (true), then shows it again. */
bool pointer_hide_over(struct rect r);
void pointer_unhide(void);

/* test=mouse (a diagnostic): logs every packet until 5 s after the last
 * one (or 60 s without any), then the counts; on a framebuffer console
 * also the report-only timing of one pointer move. */
void mouse_test(void);

#endif
