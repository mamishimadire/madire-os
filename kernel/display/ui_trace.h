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
 * The logo in its gradient is not recorded: it is not under a rule (the
 * guide, section 12, "Not under a rule").
 */

#include <stdint.h>

enum { UI_TRACE_TEXT, UI_TRACE_ICON, UI_TRACE_STATE };

#ifdef UI_TRACE
void ui_trace(int kind, uint32_t fg, uint32_t bg);
#define UI_TRACE_PAIR(kind, fg, bg) ui_trace((kind), (fg), (bg))
#else
#define UI_TRACE_PAIR(kind, fg, bg) ((void)0)
#endif

#endif
