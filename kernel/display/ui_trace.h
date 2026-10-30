#ifndef MADIRE_DISPLAY_UI_TRACE_H
#define MADIRE_DISPLAY_UI_TRACE_H

/*
 * The colour pairs the toolkit draws (1.3 stage 4a), for part static's
 * contrast check (tests/desktop_ref.py --contrast; the Design Guide,
 * section 12). Only a host tool built with -DUI_TRACE (tests/desktopdump.c)
 * defines ui_trace() and records them; everywhere else, the kernel
 * included, the hook is no code at all.
 *
 * Recorded, as (kind, foreground, background), each 0xRRGGBB:
 *   - UI_TRACE_TEXT: every pixel ui_text() sets, and every pixel a capsule
 *     text line (wp_line_draw: the About window's wordmark) paints, with
 *     the pixel's colour just before;
 *   - UI_TRACE_ICON: every pixel ui_icon_draw() blends, with the pixel's
 *     colour just before;
 *   - UI_TRACE_STATE: a button's fill in its hover or pressed state (a
 *     state mark), with the colour under the centre of the button's
 *     rectangle just before it is filled.
 * 1.3 stage 5: also the marks and fills of the menus, the M button and the
 * dialog:
 *   - UI_TRACE_STATE also for an `accent` selection bar (a menu row's, the
 *     M button's) and a focus ring, with the colour under the mark's
 *     centre (the ring: the middle of its top side) just before;
 *   - UI_TRACE_CARRIED_BAR: a fill that section 12 marks "fail alone",
 *     whose state an `accent` bar carries (a selected menu row, the pressed
 *     or open M button), with the colour under its centre just before;
 *   - UI_TRACE_CARRIED_POINTER: a hover fill that section 12 marks "fail
 *     alone", whose state the pointer over it shows (the M button's hover,
 *     the dialog's Cancel on hover), likewise.
 * The logo in its gradient is not recorded: it is not under a rule (the
 * guide, section 12, "Not under a rule").
 */

#include <stdint.h>

enum { UI_TRACE_TEXT, UI_TRACE_ICON, UI_TRACE_STATE, UI_TRACE_CARRIED_BAR, UI_TRACE_CARRIED_POINTER };

#ifdef UI_TRACE
void ui_trace(int kind, uint32_t fg, uint32_t bg);
#define UI_TRACE_PAIR(kind, fg, bg) ui_trace((kind), (fg), (bg))
#else
#define UI_TRACE_PAIR(kind, fg, bg) ((void)0)
#endif

#endif
