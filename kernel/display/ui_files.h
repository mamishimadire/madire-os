#ifndef MADIRE_DISPLAY_UI_FILES_H
#define MADIRE_DISPLAY_UI_FILES_H

/*
 * The Files window's picture and hit testing, the pure part (1.5 stage 12;
 * docs/plans/1.5.md, C5, C6b, C6c and the S12 row with its approvals; host
 * unit tests in tests/unit/test_ui_files.c; the kernel side, its memory and
 * its file calls, is files_app.c). It draws what the caller's view
 * (struct ui_files_view) says; it keeps no state of its own.
 *
 * The layout (ui_files_layout), in the window's rectangle r at scale s,
 * d(n) = ui_px(n, s), the window's edge E = d(1) and title bar d(32); the
 * inner span x0 = r.x + E to x1 = r.x + r.w - E (width iw):
 *   - the bar (Live or read-only only): (x0, r.y + d(32), iw, d(28));
 *   - the toolbar, d(40) high below it: Back, Forward and Up, d(32)
 *     squares d(4) down, from x0 + d(8), d(4) apart; Grid and List, each
 *     d(36) + its label's width + d(12) wide, d(32) high, d(4) down, Grid
 *     ending d(8) before x1, List d(4) before Grid; the path bar from d(8)
 *     after Up to d(8) before List, d(32) high, d(4) down;
 *   - the command row, d(40) high below it: New Folder, New File, Open
 *     and Rename from x0 + d(8), d(4) apart, and Show hidden files ending
 *     d(8) before x1; each d(36) + its label's width + d(12) wide, d(32)
 *     high, d(4) down;
 *   - the status line: (x0, r.y + r.h - E - d(28), iw, d(28));
 *   - the body between the command row (plus a d(1) divider) and the
 *     status line (less a d(1) divider): the sidebar d(168) wide at x0,
 *     a d(1) divider, then the content to x1.
 * The picture (tests/desktop_ref.py's files_draw draws the same; the
 * window's frame and title bar are ui_window.c's; nothing is drawn outside
 * the inner span or below the status line's top + d(28)):
 *   - the bar: filled `surface`; its text in `text` at x0 + d(12), centred
 *     down; a d(1) `border` line along its bottom;
 *   - a button (1.5 stage 12, the owner's mockup decisions): a rounded
 *     rectangle of radius d(4) filled `surface` with a d(1) `border` edge
 *     when resting (and greyed), `surface.hover` on hover, `accent.pressed`
 *     while pressed (the edge kept); a chosen one (List or Grid, the view
 *     shown) filled `surface.active` with a d(1) `accent` edge (unless
 *     pressed). Which of List and Grid is chosen follows the list's own
 *     view (list->view), never a separate flag, so the mark always matches
 *     the view drawn (the owner's review, S12). Back, Forward and Up: their icon (16 dp) centred; List,
 *     Grid and the command row: the icon (16 dp) at d(12) from the left,
 *     centred down, the label at d(36), centred down; in `text`
 *     (`text.muted` and no hover or pressed state when greyed). The icons:
 *     back, forward, up, list, grid, new_folder, new_file, open, rename
 *     (ui_icon.h);
 *   - Show hidden files: a checkbox, no resting fill or edge (hover and
 *     pressed as a button's): a d(16) box at d(12), centred down, a
 *     rounded rectangle of radius d(3): off, a d(2) `text.muted` frame; on,
 *     filled `accent` with the check icon (16 dp) in `text.on.accent` over
 *     it; the label at d(36) in `text`;
 *   - the path bar: a rounded rectangle of radius d(4) filled `surface`;
 *     the home icon (16 dp, the Home place's) in `accent` at path.x + d(8),
 *     centred down; the parts (files_core.h's files_pathbar, max_cells from
 *     ui_files_path_cells) from x = path.x + d(32), one font cell per byte,
 *     centred down: the parts in `text`, "..." (clipped) and the " > "
 *     before every part but the first (and before the first part when it
 *     starts at cell 6: after "...") in `text.muted`;
 *   - a d(1) `border` line above the body (across iw), one along the
 *     sidebar's right (its height) and one above the status line;
 *   - the sidebar: filled `surface`; item k (the nine places of
 *     UI_FILES_PLACES) at (side.x, side.y + d(8) + k x d(32) (+ d(9) for
 *     Trash and This PC), d(168), d(32)), clipped to the sidebar; a d(1)
 *     `border` divider from side.x + d(12) to d(12) before its right at
 *     side.y + d(8) + 7 x d(32) + d(4); the current place filled
 *     `surface.active` with a d(2) `accent` bar along its left, d(4) in
 *     from its top and bottom; its stroke icon (16 dp) in `accent` at d(12),
 *     centred down, and its label at d(36) in `text` (both `text.muted` for
 *     a place not in this stage: Trash, This PC);
 *   - the content: the list or the grid (ui_list.h, drawn in the content's
 *     rectangle), or the info panel: the item's icon (48 dp) at (c.x +
 *     d(24), c.y + d(24)); the rows Name, Size, Type, Modified from y =
 *     c.y + d(24), d(28) apart, each label in `text.muted` at c.x + d(96)
 *     and its value in `text` at c.x + d(192), cut with "..." to c.x + c.w
 *     - d(16), centred down in its row; then "No app can open this file
 *     yet." in `text` at c.x + d(96), d(8) below the last row (its own
 *     d(28) row); Close a button (its label only, centred; d(24) + its
 *     label's width wide) at (c.x + d(96), that row's bottom + d(8));
 *   - the rename field (ui_textfield.h, focused) over the item's name: in
 *     the list (row.x + d(32), row.y, the Name column less d(36), the row's
 *     height); in the grid (cell.x + d(2), cell.y + d(58), cell.w - d(4),
 *     d(26));
 *   - the status line: its text in `text.muted` (`danger` for a message
 *     about something that failed) at x0 + d(12), centred down, cut with
 *     "..." to iw - d(24);
 *   - the tooltip (after the pointer rests on Back, Forward or Up): its
 *     text (ui_files_tip_texts: "Back (Alt+Left)", "Forward (Alt+Right)",
 *     "Up (Alt+Up)", the owner's decision, S12) in `text` in a box its width plus 2 x d(8) wide, d(24) high, at
 *     (button.x, button.y + button.h + d(4)), filled `surface` as a rounded
 *     rectangle of radius d(4) with a d(1) `border.panel` frame;
 *   - no memory (UI_FILES_NOMEM): only "Not enough memory to open Files."
 *     in `text` at (x0 + d(24), r.y + d(32) + d(24)).
 *
 * 1.5 stage 13 (the S13 decisions Q1, Q2, Q4, D2-D5; these rules replace
 * the ones above where they differ):
 *   - the command row in a folder: New Folder and New File as above, then,
 *     d(12) after New File, six d(32) squares d(4) apart, each its icon (16
 *     dp) centred as Back's: Open, Rename, Cut, Copy, Paste (icon paste)
 *     and Move to Trash (icon place_trash); Show hidden files as above;
 *   - the command row in the Trash view (view->trash): Restore, Restore
 *     all (icon restore) and Empty Trash (icon place_trash), labelled as
 *     New Folder, from x0 + d(8), d(4) apart; d(12) after Empty Trash the
 *     square Delete permanently (icon place_trash); Show hidden files as
 *     above; the folder row's buttons are not drawn or hit there, nor the
 *     Trash row's in a folder;
 *   - the tooltip comes after the pointer rests on any square (Back,
 *     Forward, Up and the squares above): "Open (Enter)", "Rename (F2)",
 *     "Cut (Ctrl+X)", "Copy (Ctrl+C)", "Paste (Ctrl+V)", "Move to Trash
 *     (Delete)", "Delete permanently (Delete)";
 *   - the path bar in the Trash view: the trash place's icon (16 dp, in
 *     `accent`) in the home icon's place;
 *   - the sidebar's Trash is a place of this stage (its icon in `accent`,
 *     its label in `text`); This PC stays `text.muted`;
 *   - the info panel (Properties, D5): the line "No app can open this file
 *     yet." only when view->panel_has_app is false (Close moves nowhere);
 *   - the question panel (UI_FILES_ASK, Q2), in the content: line1 in
 *     `text` at (c.x + d(24), c.y + d(24)) and line2 at c.y + d(44), each
 *     cut with "..." to c.w - d(48); the buttons from c.y + d(88), d(32)
 *     high: Cancel d(96) wide at c.x + d(24) (ui_widget.h's button in
 *     ui_style_cancel) and the action, d(128) wide, d(8) after it
 *     (ui_style_restart: the main action, bold, last), each in its state
 *     (ask_state), the focused one (ask_focus: 0 Cancel, 1 the action)
 *     with the focus ring when ask_ring is on.
 *
 * 1.5 stage 14b (docs/plans/1.5.md, notes (12), (14), (15); these rules
 * replace the ones above where they differ):
 *   - the sidebar's This PC is a place (its icon in `accent`, its label in
 *     `text`); while a drag would drop into a place (drop_on, drop_place), that
 *     place is filled `surface.hover` with a d(1) `accent` frame of radius
 *     d(4) (after the current place's marks);
 *   - the This PC view (view->thispc): the command row shows only the Open
 *     square, at x0 + d(8) (lay->pc_open; Show hidden files, New Folder,
 *     New File and the other squares not drawn or hit); the path bar shows
 *     the This PC colour icon (16 dp) in the home icon's place; the content
 *     is filled `surface.window` and holds a card per storage area
 *     (ui_files_card_rect: card k at (c.x + d(16), c.y + d(16) + k x
 *     (d(64) + d(8))), min(c.w - d(32), d(480)) wide, d(64) high): a
 *     rounded rectangle of radius d(6) filled `surface` with a d(1) `border`
 *     edge (on hover `surface.hover`; selected `surface.active` with a d(1)
 *     `accent` edge); its icon (48 dp) at (x + d(8), y + d(8)); with a bar:
 *     the name in `text` at (x + d(68), y + d(8)), the bar at (x + d(68), y
 *     + d(30)), d(200) x d(8), a rounded rectangle of radius d(4) in
 *     `border` with its used part (files_bar_fill) in `accent` (`text.muted`
 *     when read-only) over it, the figures in `text.muted` (`text` on hover:
 *     `text.muted` on `surface.hover` is below the text rule) at (x + d(68),
 *     y + d(44)), each text cut with "..." to the card's width less d(76);
 *     without a bar the name alone, centred down;
 *   - box selection: while band_on, the band (on the screen) as a d(1)
 *     `accent` frame (no fill), clipped to the list's body.
 */

#include <stdbool.h>
#include <stdint.h>
#include "kernel/display/ui_draw.h"
#include "kernel/display/ui_font.h"
#include "kernel/display/ui_list.h"
#include "kernel/display/ui_textfield.h"
#include "kernel/display/ui_widget.h"
#include "kernel/files/files_core.h"

#define UI_FILES_W_DP     720
#define UI_FILES_H_DP     480
#define UI_FILES_MIN_W_DP 480
#define UI_FILES_MIN_H_DP 320
#define UI_FILES_PLACES   9
#define UI_FILES_TIP_TICKS 50u      /* the tooltip after 500 ms at rest (100 Hz) */

enum ui_files_bar { UI_FILES_BAR_NONE, UI_FILES_BAR_LIVE, UI_FILES_BAR_RO };
enum ui_files_mode { UI_FILES_LIST, UI_FILES_PANEL, UI_FILES_NOMEM,
                     UI_FILES_ASK };                    /* 1.5 stage 13: the question panel */

/* The parts a point can hit, and the buttons' order in the view's states
 * (1.5 stage 13: the buttons from UF_CUT on added after UF_CLOSE). */
enum ui_files_part {
    UF_NONE, UF_BACK, UF_FORWARD, UF_UP, UF_LIST, UF_GRID, UF_NEWFOLDER, UF_NEWFILE, UF_OPEN, UF_RENAME,
    UF_HIDDEN, UF_CLOSE, UF_CUT, UF_COPY, UF_PASTE, UF_TRASH, UF_RESTORE, UF_RESTORE_ALL, UF_EMPTY, UF_TDELETE,
    UF_ASK_CANCEL, UF_ASK_OK, UF_BUTTONS,               /* the buttons above */
    UF_PATH = UF_BUTTONS, UF_PLACE, UF_CONTENT, UF_PANEL, UF_BAR, UF_STATUS
};

/* 1.5 stage 13: a square button (its icon only, a tooltip after a rest). */
bool ui_files_square(enum ui_files_part p);

extern const char *const ui_files_place_names[UI_FILES_PLACES];  /* Home ... This PC */
extern const int ui_files_place_icons[UI_FILES_PLACES];       /* the sidebar's stroke icons */
extern const int ui_files_button_icons[UF_BUTTONS];
extern const char *const ui_files_button_labels[UF_BUTTONS];     /* by part (texts for the text buttons) */
extern const char *const ui_files_panel_labels[4];               /* Name, Size, Type, Modified */
extern const char *const ui_files_tip_texts[UF_BUTTONS];         /* Back (Alt+Left) ... (the owner, S12) */

#define UI_FILES_LIVE_TEXT   "Live mode: files are kept in memory only and are lost at restart."
#define UI_FILES_RO_TEXT     "/home is read-only: changes can't be saved."
#define UI_FILES_NOMEM_TEXT  "Not enough memory to open Files."
#define UI_FILES_NOAPP_TEXT  "No app can open this file yet."

/* 1.5 stage 14b: a This PC card (the rules above). */
#define UI_FILES_CARDS 3
struct ui_files_card {
    const char *name;                       /* "Memory (Live): /, /tmp", "Disk 0 'madire-test': /home", ... */
    const char *figures;                    /* "<U> used, <X> free of <Y>" (with a bar) */
    int icon;
    bool bar, ro;
    uint64_t used, total;
};

struct ui_files_layout {
    struct rect bar, toolbar, path, cmd, body, side, content, status;
    struct rect pc_open;                    /* 1.5 stage 14b: the Open square in the This PC view */
    struct rect button[UF_BUTTONS];         /* empty for UF_NONE; UF_CLOSE the panel's (below); both command
                                             * rows' buttons (1.5 stage 13; the view chooses one) and the
                                             * question panel's */
    struct rect panel_close;                /* in the info panel's content */
    int32_t x0, iw;
};

/* The view the caller keeps (files_app.c), drawn as it is. */
struct ui_files_view {
    enum ui_files_mode mode;
    enum ui_files_bar bar;
    bool enabled[UF_BUTTONS];               /* greyed if false */
    bool chosen[UF_BUTTONS];                /* List or Grid (the view shown), Show hidden files when on */
    enum ui_files_part hover, pressed;      /* a button (or UF_NONE) */
    int32_t place;                          /* the current place, or -1 */
    const struct files_pathbar *pb;         /* the path bar's parts (files_pathbar) */
    struct ui_list *list;                   /* the list or the grid (UI_FILES_LIST) */
    bool renaming;                          /* the rename field over list->cursor */
    const struct ui_textfield *field;
    int panel_icon;                         /* the info panel (UI_FILES_PANEL) */
    const char *panel_values[4];
    const char *status;
    bool status_error;
    enum ui_files_part tip;                 /* a square (1.5 stage 13; Back, Forward or Up in S12), or UF_NONE */
    /* 1.5 stage 13 */
    bool trash;                             /* the Trash view: its command row, the list's three columns */
    bool panel_has_app;                     /* the info panel without "No app can open this file yet." */
    const char *ask_line1, *ask_line2, *ask_label;  /* the question panel (UI_FILES_ASK) */
    int ask_focus;                          /* 0 Cancel, 1 the action */
    bool ask_ring;
    enum ui_button_state ask_state[2];      /* Cancel's and the action's */
    /* 1.5 stage 14b */
    bool thispc;                            /* the This PC view: its cards, its command row */
    const struct ui_files_card *cards;
    uint32_t ncards;
    int32_t card_sel, card_hover;           /* or -1 */
    bool band_on;                           /* box selection: the band, on the screen */
    struct rect band;
    bool drop_on;                           /* a drag would drop into place drop_place */
    int32_t drop_place;
};

/* Esc in the window (the owner's condition on the Esc rule, S12): the
 * rename field cancels the rename (the window stays), else the info panel
 * closes (only the panel), else the window closes. 1.5 stage 13: the
 * question panel takes Esc first (Cancel). */
enum ui_files_esc { UF_ESC_CANCEL_RENAME, UF_ESC_CLOSE_PANEL, UF_ESC_CLOSE_WINDOW, UF_ESC_CANCEL_ASK };
enum ui_files_esc ui_files_esc(const struct ui_files_view *v);

/* The status line's count (C6b, the S12 texts): "<n> items, <free> free",
 * "1 item, <free> free" for one; n is the folder's shown entries
 * (files_core.h: names starting with '.' are skipped and never counted
 * while Show hidden files is off). out: size bytes, cut to fit. */
void ui_files_count_text(uint32_t n, const char *free_text, char *out, uint32_t size);

/* A place's rectangle in the sidebar (k 0 to UI_FILES_PLACES - 1). */
struct rect ui_files_place_rect(const struct ui_files_layout *lay, uint32_t s, int32_t k);

/* The layout for the window at r (bar: the Live or read-only bar shown). */
void ui_files_layout(struct rect r, uint32_t s, const struct font *f, bool bar, struct ui_files_layout *out);

/* The path bar's cells for files_pathbar (0 or more). */
uint32_t ui_files_path_cells(const struct ui_files_layout *lay, uint32_t s, const struct font *f);

/* The rename field's rectangle over item i of the list (its row or cell). */
struct rect ui_files_field_rect(const struct ui_list *l, uint32_t i);

/* 1.5 stage 14b: This PC's card k (the rules above). */
struct rect ui_files_card_rect(const struct ui_files_layout *lay, uint32_t s, int32_t k);

/* 1.5 stage 14b: a drag's badge with the pointer at (x, y): text (the
 * item's name or "<n> items", bytes outside printable ASCII as '?') cut
 * with "..." to d(160); the rectangle (x + d(16), y + d(20), d(40) + the
 * cut text's width, d(28)): a rounded rectangle of radius d(4) filled
 * `surface` with a d(1) `border.panel` frame, the icon (16 dp) at d(8),
 * centred down, the text in `text` at d(32), centred down. */
struct rect ui_files_badge_rect(int32_t x, int32_t y, uint32_t s, const struct font *f, const char *text);
void ui_files_badge_draw(const struct ui_canvas *c, struct rect r, uint32_t s, const struct font *f, int icon,
                         const char *text);

/* What is at (x, y): a button, the path bar's part *index, the place
 * *index, the content, the panel, the bar, the status line or nothing.
 * 1.5 stage 14b: in the This PC view, the content with *index the card
 * there (or -1). */
enum ui_files_part ui_files_hit(const struct ui_files_layout *lay, const struct ui_files_view *v, uint32_t s,
                                const struct font *f, int32_t x, int32_t y, int32_t *index);

/* Draws the client of the window at r (the rules above). */
void ui_files_draw(const struct ui_canvas *c, struct rect r, uint32_t s, const struct ui_fonts *f,
                   const struct ui_files_view *v);

#endif
