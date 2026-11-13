#ifndef MADIRE_DISPLAY_UI_DIALOG_H
#define MADIRE_DISPLAY_UI_DIALOG_H

/*
 * The power dialog, the pure part (1.3 stage 5's Restart dialog; 1.5 stage
 * 6: the power dialog, docs/plans/1.5.md C3 and C6b, decision 4; the
 * Design Guide, section 9; host unit tests in tests/unit/test_ui_dialog.c).
 * tests/desktop_ref.py draws the same picture from these rules.
 *
 * A window of ui_window.h's style (its frame, title bar and close button),
 * always drawn active: 360 x 172 dp at scale s, titled "Power" with the
 * restart icon, centred in the area above the taskbar (x = (W - w) / 2,
 * y = (bar - h) / 2, 0 if negative). Inside it, places in dp from the
 * window's top-left corner as ui_menu.h says ("at a dp" is ui_px(a) from
 * the corner, "from a to b dp" is [ui_px(a), ui_px(b))):
 *   - the text in `text`, in the console's (regular) font, at 24 dp
 *     across: "Shut down or restart Madire OS?" at 56 dp down and the
 *     second line at 76 dp: 1.4's per mode, unchanged ("Files in Live mode
 *     are lost." until ui_dialog_set_line2; 39 characters of 8 dp fit the
 *     312 dp between the paddings);
 *   - the buttons from 116 to 148 dp down, 96 dp wide, 8 dp apart: Cancel
 *     from 32 to 128 dp across and Restart from 136 to 232 dp (both
 *     ui_style_cancel), Shut down from 240 to 336 dp (ui_style_restart,
 *     bold: the main action, last, as 1.3's Restart was), each
 *     ui_widget.h's button in its state, the focused one with the focus
 *     ring when the ring is on.
 * The focus is on Cancel when it opens; the ring is on if it was opened by
 * the keyboard, and turns on when Tab moves the focus to the next button
 * (Cancel, Restart, Shut down, then Cancel again).
 */

#include <stdbool.h>
#include <stdint.h>
#include "kernel/display/ui_draw.h"
#include "kernel/display/ui_font.h"
#include "kernel/display/ui_widget.h"
#include "kernel/display/ui_window.h"

#define UI_DIALOG_TITLE "Power"
#define UI_DIALOG_LINE1 "Shut down or restart Madire OS?"
#define UI_DIALOG_LINE2 "Files in Live mode are lost."
/* 1.4 stage 12 (docs/plans/1.4.md, C10): the second line in Disk mode. */
#define UI_DIALOG_LINE2_DISK   "Files in /home are saved first."
#define UI_DIALOG_LINE2_DISKRO "Disk is read-only; /home is unchanged."

/* 1.5 stage 6: the buttons left to right (Tab's order). */
enum ui_dialog_button { UI_DLG_CANCEL, UI_DLG_RESTART, UI_DLG_SHUTDOWN, UI_DLG_BUTTONS };
enum ui_dialog_part { UI_DLG_NONE, UI_DLG_BODY, UI_DLG_PART_CANCEL, UI_DLG_PART_RESTART, UI_DLG_PART_CLOSE,
                      UI_DLG_PART_SHUTDOWN };

/* Each button's label (UI_DLG_CANCEL, UI_DLG_RESTART, UI_DLG_SHUTDOWN). */
extern const char *const ui_dialog_labels[UI_DLG_BUTTONS];

struct ui_dialog {
    struct ui_window w;         /* its frame: w.r where it lies, w.close its close button's state */
    uint32_t scale;
    struct ui_fonts fonts;
    enum ui_dialog_button focus;
    bool ring;
    enum ui_button_state btn[UI_DLG_BUTTONS];
};

/* The dialog's size at scale s. */
void ui_dialog_size(uint32_t s, int32_t *w, int32_t *h);

/* d set up at scale s with fonts f, placed on a screen `width` wide with
 * the taskbar's top at bar_y; Cancel focused, the ring as given, every
 * button normal. d's window paints d itself: d is not to be copied. */
void ui_dialog_init(struct ui_dialog *d, uint32_t s, const struct ui_fonts *f, int32_t width, int32_t bar_y,
                    bool ring);

/* 1.4 stage 12: the second line every dialog paints from now on
 * (UI_DIALOG_LINE2 until set): UI_DIALOG_LINE2_DISK or _DISKRO in Disk
 * mode. The desktop sets it once, before any dialog opens. */
void ui_dialog_set_line2(const char *text);

/* Button b's rectangle on the screen. */
struct rect ui_dialog_button_rect(const struct ui_dialog *d, enum ui_dialog_button b);

/* What is at (x, y): a button, the close button, the rest of the dialog, or none. */
enum ui_dialog_part ui_dialog_hit(const struct ui_dialog *d, int32_t x, int32_t y);

/* The window's client paint (struct ui_window's paint; ctx the dialog). */
void ui_dialog_paint(const struct ui_canvas *c, struct rect r, void *ctx);

/* Draws the whole dialog (ui_window_draw, active, with its close button's state). */
void ui_dialog_draw(const struct ui_canvas *c, const struct ui_dialog *d);

#endif
