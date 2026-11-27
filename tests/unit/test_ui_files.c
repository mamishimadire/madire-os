/* Unit tests for kernel/display/ui_files.c and ui_viewer.c (the Files
 * window's picture and the Text Viewer, 1.5 stage 12; docs/plans/1.5.md,
 * C5): the window's layout at 100 % (the bar, the toolbar, the path bar's
 * cells, the command row, the body, the sidebar, the content, the status
 * line) with and without the bar; hit testing (buttons, a path-bar part,
 * a place, the content, the info panel's Close; nothing without memory);
 * the picture's marks (the bar, the dividers, the sidebar's current place,
 * a chosen button's fill and bar, hover and pressed fills, the tooltip, the
 * no-memory view alone; made-up fonts with no pixels). The Text Viewer:
 * the owner's security rule (S12): every byte outside printable ASCII but
 * newline and tab (NUL, BEL, BS, CR, ESC, DEL, 0x80-0xFF) is one '?' cell
 * with no other effect (a CR does not end a line; the lines and their
 * offsets are those of the same text with plain letters there); the lines
 * (wrapping, a consumed newline after a full line, a wide tab, an empty
 * buffer, a final newline); scrolling within its bounds. The pixel-exact
 * comparison with tests/desktop_ref.py is part static's. The modules are
 * included directly, except surface_core.c, wallpaper_core.c and
 * coverage_core.c, linked as separate files (the Makefile). */
#include <stdint.h>
#include <string.h>
#include "tests/unit/unit.h"
#include "kernel/display/tokens.h"
#include "kernel/display/ui_rect.c"
#include "kernel/display/ui_draw.c"
#include "kernel/display/icons.c"
#include "kernel/display/ui_icon.c"
#include "kernel/display/cicons.c"
#include "kernel/display/ui_cicon.c"
#include "kernel/display/ui_about.c"
#include "kernel/display/ui_widget.c"         /* 1.5 stage 13: the question panel's buttons */
#include "kernel/display/ui_textfield.c"
#include "kernel/display/ui_list.c"
#include "kernel/display/ui_files.c"
#include "kernel/display/ui_viewer.c"
#include "kernel/files/files_core.c"

#define SW 720
#define SH 480

static uint8_t buf[SH * SW * 4];
static struct surface surf;
static struct ui_canvas cv;

static uint8_t blank_bits[256 * 16];
static const struct font blank = { 8, 16, 1, blank_bits };
static const struct ui_fonts fonts = { &blank, &blank };
static uint8_t q_bits[256 * 16];          /* only '?' has pixels (every row) */
static const struct font qfont = { 8, 16, 1, q_bits };
static const struct ui_fonts qfonts = { &qfont, &qfont };

static void setup(void)
{
    surface_init(&surf, buf, SW, SH, SW * 4, 4);
    ui_canvas_init(&cv, &surf);
    ui_fill(&cv, (struct rect){ 0, 0, SW, SH }, UI_SURFACE_WINDOW);
}

static uint32_t at(int x, int y)
{
    return surface_get(&surf, (uint32_t)x, (uint32_t)y);
}

static bool same_rect(struct rect a, int32_t x, int32_t y, int32_t w, int32_t h)
{
    return a.x == x && a.y == y && a.w == w && a.h == h;
}

static void test_layout(void)
{
    struct ui_files_layout l;
    ui_files_layout((struct rect){ 0, 0, 720, 480 }, 4, &blank, true, &l);
    CHECK(same_rect(l.bar, 1, 32, 718, 28));
    CHECK(same_rect(l.toolbar, 1, 60, 718, 40));
    CHECK(same_rect(l.button[UF_BACK], 9, 64, 32, 32));
    CHECK(same_rect(l.button[UF_FORWARD], 45, 64, 32, 32));
    CHECK(same_rect(l.button[UF_UP], 81, 64, 32, 32));
    CHECK(same_rect(l.button[UF_GRID], 631, 64, 80, 32));      /* d(36) + 4 cells + d(12) */
    CHECK(same_rect(l.button[UF_LIST], 547, 64, 80, 32));
    CHECK(same_rect(l.path, 121, 64, 418, 32));
    CHECK_EQ(ui_files_path_cells(&l, 4, &blank), 47);           /* (418 - 32 - 8) / 8 */
    CHECK(same_rect(l.cmd, 1, 100, 718, 40));
    CHECK(same_rect(l.button[UF_NEWFOLDER], 9, 104, 128, 32));
    CHECK(same_rect(l.button[UF_NEWFILE], 141, 104, 112, 32));
    CHECK(same_rect(l.button[UF_OPEN], 265, 104, 32, 32));          /* 1.5 stage 13: squares, d(12) on */
    CHECK(same_rect(l.button[UF_RENAME], 301, 104, 32, 32));
    CHECK(same_rect(l.button[UF_HIDDEN], 527, 104, 184, 32));
    CHECK(same_rect(l.status, 1, 451, 718, 28));
    CHECK(same_rect(l.body, 1, 141, 718, 309));
    CHECK(same_rect(l.side, 1, 141, 168, 309));
    CHECK(same_rect(l.content, 170, 141, 549, 309));
    CHECK(same_rect(l.panel_close, 266, 141 + 24 + 4 * 28 + 8 + 28 + 8, 64, 32));
    CHECK(same_rect(ui_files_place_rect(&l, 4, 6), 1, 341, 168, 32));
    CHECK(same_rect(ui_files_place_rect(&l, 4, 7), 1, 382, 168, 32));     /* after the divider: + d(9) */
    ui_files_layout((struct rect){ 0, 0, 720, 480 }, 4, &blank, false, &l);
    CHECK(same_rect(l.bar, 1, 32, 0, 0));
    CHECK(same_rect(l.toolbar, 1, 32, 718, 40));
    CHECK(same_rect(l.body, 1, 113, 718, 337));
}

static void test_hit(void)
{
    struct ui_files_layout l;
    struct ui_files_view v;
    struct files_pathbar pb;
    memset(&v, 0, sizeof(v));
    ui_files_layout((struct rect){ 0, 0, 720, 480 }, 4, &blank, true, &l);
    files_pathbar(FILES_VIEW_FOLDER, "/home/user/Documents", 20, ui_files_path_cells(&l, 4, &blank), &pb);
    CHECK_EQ(pb.n, 2);
    v.mode = UI_FILES_LIST;
    v.pb = &pb;
    int32_t i = 0;
    CHECK_EQ(ui_files_hit(&l, &v, 4, &blank, 20, 70, &i), UF_BACK);
    CHECK_EQ(ui_files_hit(&l, &v, 4, &blank, 560, 80, &i), UF_LIST);
    CHECK_EQ(ui_files_hit(&l, &v, 4, &blank, 560, 110, &i), UF_HIDDEN);
    CHECK_EQ(ui_files_hit(&l, &v, 4, &blank, 153 + 8 * 8 + 1, 80, &i), UF_PATH);       /* "Documents" at cell 7 */
    CHECK_EQ(i, 1);
    CHECK_EQ(ui_files_hit(&l, &v, 4, &blank, 153 + 2 * 8, 80, &i), UF_PATH);           /* "Home" */
    CHECK_EQ(i, 0);
    CHECK_EQ(ui_files_hit(&l, &v, 4, &blank, 153 + 5 * 8, 80, &i), UF_NONE);           /* " > " */
    CHECK_EQ(ui_files_hit(&l, &v, 4, &blank, 130, 80, &i), UF_NONE);                   /* the home icon */
    CHECK_EQ(ui_files_hit(&l, &v, 4, &blank, 20, 141 + 8 + 2 * 32 + 5, &i), UF_PLACE);
    CHECK_EQ(i, 2);
    CHECK_EQ(ui_files_hit(&l, &v, 4, &blank, 20, 378, &i), UF_NONE);                  /* the divider's gap */
    CHECK_EQ(ui_files_hit(&l, &v, 4, &blank, 20, 390, &i), UF_PLACE);                  /* Trash */
    CHECK_EQ(i, 7);
    CHECK_EQ(ui_files_hit(&l, &v, 4, &blank, 20, 447, &i), UF_NONE);                   /* below This PC */
    CHECK_EQ(ui_files_hit(&l, &v, 4, &blank, 300, 300, &i), UF_CONTENT);
    CHECK_EQ(ui_files_hit(&l, &v, 4, &blank, 300, 40, &i), UF_BAR);
    CHECK_EQ(ui_files_hit(&l, &v, 4, &blank, 300, 460, &i), UF_STATUS);
    v.mode = UI_FILES_PANEL;
    CHECK_EQ(ui_files_hit(&l, &v, 4, &blank, 270, l.panel_close.y + 4, &i), UF_CLOSE);
    CHECK_EQ(ui_files_hit(&l, &v, 4, &blank, 600, 300, &i), UF_PANEL);
    v.mode = UI_FILES_NOMEM;
    CHECK_EQ(ui_files_hit(&l, &v, 4, &blank, 20, 70, &i), UF_NONE);
}

static void test_draw_marks(void)
{
    struct ui_files_view v;
    struct files_pathbar pb;
    struct ui_list list;
    static struct ui_list_item items[3];
    memset(&v, 0, sizeof(v));
    for (int k = 0; k < 3; k++) {
        items[k].name = "x";
        items[k].icon = UI_ICON_NONE;
    }
    struct ui_files_layout l;
    ui_files_layout((struct rect){ 0, 0, 720, 480 }, 4, &blank, true, &l);
    files_pathbar(FILES_VIEW_FOLDER, "/home/user", 10, 56, &pb);
    ui_list_init(&list, UI_LIST_DETAILS, items, 3, ui_files_panel_labels, l.content, 4);
    v.mode = UI_FILES_LIST;
    v.bar = UI_FILES_BAR_LIVE;
    for (int k = 0; k < UF_BUTTONS; k++)
        v.enabled[k] = true;
    v.enabled[UF_BACK] = false;
    v.chosen[UF_LIST] = true;
    v.chosen[UF_HIDDEN] = true;                     /* Show hidden files ticked */
    v.hover = UF_NEWFILE;
    v.pressed = UF_OPEN;
    v.place = 0;
    v.pb = &pb;
    v.list = &list;
    v.status = "3 items, 1.0 MiB free";
    v.tip = UF_UP;
    setup();
    ui_files_draw(&cv, (struct rect){ 0, 0, 720, 480 }, 4, &fonts, &v);
    CHECK_EQ(at(300, 40), UI_SURFACE);              /* the bar */
    CHECK_EQ(at(300, 59), UI_BORDER);               /* its line */
    CHECK_EQ(at(12, 90), UI_SURFACE);               /* Back greyed: the resting fill */
    CHECK_EQ(at(25, 64), UI_BORDER);                /* and its edge */
    CHECK_EQ(at(300, 70), UI_SURFACE);              /* the path bar */
    CHECK_EQ(at(600, 80), UI_SURFACE_ACTIVE);       /* List chosen: (547, 64) 80 x 32 */
    CHECK_EQ(at(587, 64), UI_ACCENT);               /* its accent edge */
    CHECK_EQ(at(705, 80), UI_SURFACE);              /* Grid resting */
    CHECK_EQ(at(671, 64), UI_BORDER);               /* its edge */
    CHECK_EQ(at(247, 120), UI_SURFACE_HOVER);       /* New File on hover */
    CHECK_EQ(at(292, 120), UI_ACCENT_PRESSED);      /* Open pressed (1.5 stage 13: its square at 265, the icon to 288) */
    /* Show hidden files ticked: the box (539, 112) 16 x 16 (corners 3)
     * filled accent; the tick (6, 12) (10, 16) (18, 8) dp at 16 px is
     * (543, 120) (545.7, 122.7) (551, 117.3), radius 0.67 px: pixel
     * (544, 121)'s centre lies on it, coverage full. */
    CHECK_EQ(at(547, 114), UI_ACCENT);
    CHECK_EQ(at(547, 112), UI_ACCENT);
    CHECK_EQ(at(544, 121), UI_TEXT_ON_ACCENT);
    CHECK_EQ(at(700, 120), UI_SURFACE_WINDOW);      /* the checkbox control: no resting fill */
    CHECK_EQ(at(80, 377), UI_BORDER);               /* the sidebar's divider above Trash */
    CHECK_EQ(at(300, 140), UI_BORDER);              /* above the body */
    CHECK_EQ(at(169, 300), UI_BORDER);              /* the sidebar's right */
    CHECK_EQ(at(300, 450), UI_BORDER);              /* above the status line */
    CHECK_EQ(at(100, 165), UI_SURFACE_ACTIVE);      /* Home, the current place */
    CHECK_EQ(at(1, 165), UI_ACCENT);                /* its bar */
    CHECK_EQ(at(100, 197), UI_SURFACE);             /* Desktop */
    CHECK_EQ(at(95, 110), UI_SURFACE);              /* the tooltip under Up */
    CHECK_EQ(at(97, 100), UI_BORDER_PANEL);         /* its frame's top */
    /* Show hidden files unticked: the box's 2 px text.muted frame (rows
     * 112-113), the window colour inside, no tick. */
    v.chosen[UF_HIDDEN] = false;
    setup();
    ui_files_draw(&cv, (struct rect){ 0, 0, 720, 480 }, 4, &fonts, &v);
    CHECK_EQ(at(547, 112), UI_TEXT_MUTED);
    CHECK_EQ(at(547, 113), UI_TEXT_MUTED);
    CHECK_EQ(at(547, 114), UI_SURFACE_WINDOW);
    CHECK_EQ(at(544, 121), UI_SURFACE_WINDOW);
    /* No memory: only the message (no pixels with this font). */
    v.mode = UI_FILES_NOMEM;
    setup();
    ui_files_draw(&cv, (struct rect){ 0, 0, 720, 480 }, 4, &fonts, &v);
    int changed = 0;
    for (int y = 0; y < SH; y++)
        for (int x = 0; x < SW; x++)
            changed += at(x, y) != UI_SURFACE_WINDOW;
    CHECK_EQ(changed, 0);
}

/* The owner's review (S12): the List and Grid marks follow the view drawn,
 * whatever the chosen flags say: List chosen -> list rows with List
 * marked; Grid chosen -> grid cells with Grid marked. */
static void test_view_marking(void)
{
    struct ui_files_view v;
    struct ui_list list;
    static struct ui_list_item items[3];
    memset(&v, 0, sizeof(v));
    for (int k = 0; k < 3; k++) {
        items[k].name = "x";
        items[k].icon = UI_ICON_NONE;
        items[k].selected = k == 0;
    }
    struct ui_files_layout l;
    ui_files_layout((struct rect){ 0, 0, 720, 480 }, 4, &blank, true, &l);
    v.mode = UI_FILES_LIST;
    v.bar = UI_FILES_BAR_LIVE;
    for (int k = 0; k < UF_BUTTONS; k++)
        v.enabled[k] = true;
    v.place = -1;
    v.list = &list;
    /* The list, with the flags stale on purpose (Grid "chosen"). */
    ui_list_init(&list, UI_LIST_DETAILS, items, 3, ui_files_panel_labels, l.content, 4);
    v.chosen[UF_GRID] = true;
    setup();
    ui_files_draw(&cv, (struct rect){ 0, 0, 720, 480 }, 4, &fonts, &v);
    CHECK_EQ(at(600, 80), UI_SURFACE_ACTIVE);       /* List marked */
    CHECK_EQ(at(587, 64), UI_ACCENT);
    CHECK_EQ(at(705, 80), UI_SURFACE);              /* Grid resting */
    CHECK_EQ(at(600, 183), UI_SURFACE_ACTIVE);      /* row 0 selected: the whole row (y 169-196) */
    /* The grid, with List "chosen". */
    list.view = UI_LIST_GRID;
    v.chosen[UF_GRID] = false;
    v.chosen[UF_LIST] = true;
    setup();
    ui_files_draw(&cv, (struct rect){ 0, 0, 720, 480 }, 4, &fonts, &v);
    CHECK_EQ(at(705, 80), UI_SURFACE_ACTIVE);       /* Grid marked */
    CHECK_EQ(at(671, 64), UI_ACCENT);
    CHECK_EQ(at(600, 80), UI_SURFACE);              /* List resting */
    CHECK_EQ(at(200, 183), UI_SURFACE_ACTIVE);      /* cell 0 selected: (172, 143) 92 x 84 */
    CHECK_EQ(at(600, 183), UI_SURFACE_WINDOW);      /* no row across the content */
}

/* The owner's review (S12): Show hidden files unticked -> no dot entries
 * shown or counted; ticked -> shown and counted; unticked again (the folder
 * read again) -> hidden again. The folder as files_app.c reads it
 * (files_folder_add with the checkbox's state) and the status line's count
 * (ui_files_count_text). */
static void test_hidden_count(void)
{
    static struct files_entry e[8];
    static char names[8 * (FILES_NAME_MAX + 1)];
    static const char *const list[5] = { "Work", ".hidden", "notes.txt", ".config", "plan.md" };
    struct files_folder f;
    char out[64];
    for (int pass = 0; pass < 3; pass++) {
        bool ticked = pass == 1;
        files_folder_init(&f, e, 8, names, sizeof(names), ticked);
        for (int k = 0; k < 5; k++)
            files_folder_add(&f, list[k], (uint32_t)strlen(list[k]), k == 0 ? FILES_K_DIR : FILES_K_FILE, 0, 0);
        uint32_t dots = 0;
        for (uint32_t i = 0; i < f.n; i++)
            dots += files_entry_name(&f, i)[0] == '.';
        ui_files_count_text(f.n, "1.0 KiB", out, sizeof(out));
        CHECK_EQ(f.n, ticked ? 5 : 3);
        CHECK_EQ(dots, ticked ? 2 : 0);
        CHECK(strcmp(out, ticked ? "5 items, 1.0 KiB free" : "3 items, 1.0 KiB free") == 0);
    }
    ui_files_count_text(1, "0 B", out, sizeof(out));
    CHECK(strcmp(out, "1 item, 0 B free") == 0);
    ui_files_count_text(0, "12.3 MiB", out, sizeof(out));
    CHECK(strcmp(out, "0 items, 12.3 MiB free") == 0);
    ui_files_count_text(1000, "2.0 GiB", out, sizeof(out));
    CHECK(strcmp(out, "1000 items, 2.0 GiB free") == 0);
    ui_files_count_text(25, "x", out, 6);                   /* cut to its buffer */
    CHECK(strcmp(out, "25 it") == 0);
}

static uint32_t lines_of(const char *s, uint32_t n, uint32_t cols)
{
    return ui_viewer_lines((const uint8_t *)s, n, cols);
}

static void test_viewer_lines(void)
{
    CHECK_EQ(lines_of("", 0, 80), 0);
    CHECK_EQ(lines_of("a\n", 2, 80), 1);
    CHECK_EQ(lines_of("\n\n", 2, 80), 2);
    CHECK_EQ(lines_of("abcdefgh", 8, 4), 2);
    CHECK_EQ(lines_of("abcd\nef", 7, 4), 2);       /* the newline after a full line is consumed */
    CHECK_EQ(lines_of("abc\nef", 6, 4), 2);
    CHECK_EQ(lines_of("\tA", 2, 4), 2);            /* a tab wider than the line takes it */
    CHECK_EQ(lines_of("a\tb", 3, 80), 1);
    uint32_t next = 0;
    CHECK_EQ(ui_viewer_next((const uint8_t *)"a\tbc", 4, 0, 9, &next), 3);   /* a, tab to 8, b; c wraps */
    CHECK_EQ(next, 3);
    CHECK_EQ(ui_viewer_line_start((const uint8_t *)"abcdefgh", 8, 4, 1), 4);
    CHECK_EQ(ui_viewer_line_start((const uint8_t *)"abcdefgh", 8, 4, 9), 8);
}

/* The owner's security rule (S12): control and other non-printable bytes
 * are one '?' cell each, with no other effect. */
static void test_viewer_control_bytes(void)
{
    static const uint8_t odd[] = { 'a', 0x00, 'b', 0x07, 'c', 0x08, 0x1B, '[', '2', 'J', 0x0D, 'd', 0x7F, 0x80, 0xFF,
                                   0x0C, 0x0B, 'e', '\n', 0x0D, 0x0D, 'f' };
    uint8_t plain[sizeof(odd)];
    for (uint32_t i = 0; i < sizeof(odd); i++) {
        uint8_t b = odd[i];
        bool keep = b == '\n' || b == '\t' || (b >= 0x20 && b <= 0x7E);
        plain[i] = keep ? b : 'X';
        CHECK_EQ(ui_viewer_shown(b), (b >= 0x20 && b <= 0x7E) ? (char)b : '?');
    }
    for (uint32_t cols = 1; cols <= 30; cols++) {
        CHECK_EQ(ui_viewer_lines(odd, sizeof(odd), cols), ui_viewer_lines(plain, sizeof(plain), cols));
        uint32_t a = 0, b = 0;
        while (a < sizeof(odd)) {
            uint32_t na = 0, nb = 0;
            CHECK_EQ(ui_viewer_next(odd, sizeof(odd), a, cols, &na), ui_viewer_next(plain, sizeof(plain), b, cols, &nb));
            CHECK_EQ(na, nb);
            if (na <= a)
                break;
            a = na;
            b = nb;
        }
    }
    CHECK_EQ(ui_viewer_lines(odd, sizeof(odd), 80), 2);     /* only the newline ends a line; CRs do not */
    /* Drawn: four control bytes are four '?' cells on one row, a CR among
     * them; printable letters draw none ('?' is the only glyph with pixels). */
    for (uint32_t r = 0; r < 16; r++)
        q_bits[0x3F * 16 + r] = 0xFF;
    static const uint8_t ctl[] = { 0x01, 0x0D, 0x1B, 0x00, 'a', 'b' };
    struct ui_viewer v = { ctl, sizeof(ctl), false, 0, UI_SB_NORMAL };
    setup();
    ui_viewer_draw(&cv, &v, (struct rect){ 0, 0, 400, 300 }, 4, &qfonts);
    for (int k = 0; k < 4; k++)
        CHECK_EQ(at(9 + 8 * k + 3, 41), UI_TEXT);
    CHECK_EQ(at(9 + 8 * 4 + 3, 41), UI_SURFACE_WINDOW);
    CHECK_EQ(at(9 + 3, 41 + 16), UI_SURFACE_WINDOW);        /* no second row: the CR broke nothing */
}

static void test_viewer_scroll(void)
{
    static uint8_t text[200];
    for (int i = 0; i < 200; i++)
        text[i] = (i % 10 == 9) ? '\n' : 'x';               /* 20 lines */
    struct ui_viewer v = { text, sizeof(text), false, 0, UI_SB_NORMAL };
    struct rect r = { 0, 0, 400, 300 };
    uint32_t page = ui_viewer_page(&v, r, 4, &blank);       /* (267 - 16) / 16 = 15 */
    CHECK_EQ(page, 15);
    ui_viewer_scroll(&v, r, 4, &blank, 100);
    CHECK_EQ(v.top, 5);
    ui_viewer_scroll(&v, r, 4, &blank, -2);
    CHECK_EQ(v.top, 3);
    ui_viewer_scroll(&v, r, 4, &blank, -100);
    CHECK_EQ(v.top, 0);
    v.cut = true;                                           /* the bar takes 28 px */
    CHECK_EQ(ui_viewer_page(&v, r, 4, &blank), 13);
}

/* Esc (the owner's condition on the Esc rule, S12): in a rename field it
 * cancels the rename and never closes the window; with the info panel open
 * it closes only the panel; with nothing else, it closes Files (files_app.c
 * acts on ui_files_esc's answer; desktop.c closes the window only for
 * UF_ESC_CLOSE_WINDOW). */
static void test_esc(void)
{
    struct ui_files_view v;
    memset(&v, 0, sizeof(v));
    v.mode = UI_FILES_LIST;
    v.renaming = true;
    CHECK_EQ(ui_files_esc(&v), UF_ESC_CANCEL_RENAME);
    v.renaming = false;
    CHECK_EQ(ui_files_esc(&v), UF_ESC_CLOSE_WINDOW);
    v.mode = UI_FILES_PANEL;
    CHECK_EQ(ui_files_esc(&v), UF_ESC_CLOSE_PANEL);
    v.renaming = true;                      /* (the panel is never shown while renaming) */
    CHECK_EQ(ui_files_esc(&v), UF_ESC_CLOSE_PANEL);
    v.mode = UI_FILES_NOMEM;
    v.renaming = false;
    CHECK_EQ(ui_files_esc(&v), UF_ESC_CLOSE_WINDOW);
}

/* ---- 1.5 stage 13 (Q1, Q2, Q4, D2-D5) ---- */

static void test_layout_s13(void)
{
    struct ui_files_layout l;
    ui_files_layout((struct rect){ 0, 0, 720, 480 }, 4, &blank, true, &l);
    /* The folder row: six squares from d(12) after New File (ends 253). */
    CHECK(same_rect(l.button[UF_CUT], 337, 104, 32, 32));
    CHECK(same_rect(l.button[UF_COPY], 373, 104, 32, 32));
    CHECK(same_rect(l.button[UF_PASTE], 409, 104, 32, 32));
    CHECK(same_rect(l.button[UF_TRASH], 445, 104, 32, 32));
    CHECK(l.button[UF_TRASH].x + 32 < l.button[UF_HIDDEN].x);        /* clear of Show hidden files */
    /* The Trash row: Restore (7 cells), Restore all and Empty Trash (11). */
    CHECK(same_rect(l.button[UF_RESTORE], 9, 104, 104, 32));
    CHECK(same_rect(l.button[UF_RESTORE_ALL], 117, 104, 136, 32));
    CHECK(same_rect(l.button[UF_EMPTY], 257, 104, 136, 32));
    CHECK(same_rect(l.button[UF_TDELETE], 405, 104, 32, 32));
    /* The question panel's buttons in the content (170, 141). */
    CHECK(same_rect(l.button[UF_ASK_CANCEL], 194, 229, 96, 32));
    CHECK(same_rect(l.button[UF_ASK_OK], 298, 229, 128, 32));
    CHECK(ui_files_square(UF_BACK) && ui_files_square(UF_OPEN) && ui_files_square(UF_TRASH));
    CHECK(ui_files_square(UF_TDELETE) && !ui_files_square(UF_NEWFOLDER) && !ui_files_square(UF_RESTORE));
    CHECK(!ui_files_square(UF_HIDDEN) && !ui_files_square(UF_ASK_OK));
    CHECK(strcmp(ui_files_tip_texts[UF_TRASH], "Move to Trash (Delete)") == 0);
    CHECK(strcmp(ui_files_tip_texts[UF_TDELETE], "Delete permanently (Delete)") == 0);
    CHECK(strcmp(ui_files_tip_texts[UF_PASTE], "Paste (Ctrl+V)") == 0);
}

static void test_hit_s13(void)
{
    struct ui_files_layout l;
    struct ui_files_view v;
    memset(&v, 0, sizeof(v));
    ui_files_layout((struct rect){ 0, 0, 720, 480 }, 4, &blank, true, &l);
    v.mode = UI_FILES_LIST;
    int32_t i = 0;
    CHECK_EQ(ui_files_hit(&l, &v, 4, &blank, 350, 110, &i), UF_CUT);
    CHECK_EQ(ui_files_hit(&l, &v, 4, &blank, 460, 110, &i), UF_TRASH);
    CHECK_EQ(ui_files_hit(&l, &v, 4, &blank, 20, 110, &i), UF_NEWFOLDER);
    CHECK_EQ(ui_files_hit(&l, &v, 4, &blank, 300, 240, &i), UF_CONTENT);    /* no question: the content */
    v.trash = true;                                     /* the Trash view's own row */
    CHECK_EQ(ui_files_hit(&l, &v, 4, &blank, 20, 110, &i), UF_RESTORE);
    CHECK_EQ(ui_files_hit(&l, &v, 4, &blank, 130, 110, &i), UF_RESTORE_ALL);
    CHECK_EQ(ui_files_hit(&l, &v, 4, &blank, 350, 110, &i), UF_EMPTY);
    CHECK_EQ(ui_files_hit(&l, &v, 4, &blank, 420, 110, &i), UF_TDELETE);
    CHECK_EQ(ui_files_hit(&l, &v, 4, &blank, 460, 110, &i), UF_NONE);       /* no Move to Trash there */
    CHECK_EQ(ui_files_hit(&l, &v, 4, &blank, 560, 110, &i), UF_HIDDEN);
    v.trash = false;
    v.mode = UI_FILES_ASK;
    CHECK_EQ(ui_files_hit(&l, &v, 4, &blank, 200, 240, &i), UF_ASK_CANCEL);
    CHECK_EQ(ui_files_hit(&l, &v, 4, &blank, 300, 240, &i), UF_ASK_OK);
    CHECK_EQ(ui_files_hit(&l, &v, 4, &blank, 600, 300, &i), UF_PANEL);
    v.mode = UI_FILES_LIST;                             /* the question's buttons only while asking */
    CHECK_EQ(ui_files_hit(&l, &v, 4, &blank, 200, 240, &i), UF_CONTENT);
}

static void test_esc_ask(void)
{
    struct ui_files_view v;
    memset(&v, 0, sizeof(v));
    v.mode = UI_FILES_ASK;
    CHECK_EQ(ui_files_esc(&v), UF_ESC_CANCEL_ASK);
}

static void test_draw_s13(void)
{
    struct ui_files_view v;
    struct files_pathbar pb;
    memset(&v, 0, sizeof(v));
    files_pathbar(FILES_VIEW_TRASH, "", 0, 56, &pb);
    v.mode = UI_FILES_ASK;
    v.bar = UI_FILES_BAR_LIVE;
    v.trash = true;
    v.pb = &pb;
    v.place = 7;
    v.status = "3 items, 1.0 MiB free";
    v.ask_line1 = "Empty the Trash?";
    v.ask_line2 = "3 items will be deleted permanently.";
    v.ask_label = "Empty Trash";
    v.ask_focus = 0;
    v.ask_ring = true;
    for (int k = 0; k < UF_BUTTONS; k++)
        v.enabled[k] = true;
    setup();
    ui_files_draw(&cv, (struct rect){ 0, 0, 720, 480 }, 4, &fonts, &v);
    CHECK_EQ(at(60, 120), UI_SURFACE);                  /* Restore resting: (9, 104) 104 x 32 */
    CHECK_EQ(at(330, 108), UI_SURFACE);                 /* Empty Trash's fill */
    CHECK_EQ(at(470, 120), UI_SURFACE_WINDOW);          /* no Move to Trash square in the Trash */
    CHECK_EQ(at(100, 400), UI_SURFACE_ACTIVE);          /* Trash the current place */
    CHECK_EQ(at(1, 400), UI_ACCENT);
    CHECK_EQ(at(220, 245), UI_SURFACE_ACTIVE);          /* Cancel: ui_style_cancel */
    CHECK_EQ(at(410, 245), UI_ACCENT);                  /* the action: ui_style_restart */
    v.ask_state[1] = UI_BUTTON_HOVER;
    setup();
    ui_files_draw(&cv, (struct rect){ 0, 0, 720, 480 }, 4, &fonts, &v);
    CHECK_EQ(at(410, 245), UI_ACCENT_HOVER);
    /* The folder view draws its squares; the info panel's line follows
     * panel_has_app (with the '?' font, the line's '.' draws nothing, so
     * the panel is compared through its Close button only: unchanged). */
    v.trash = false;
    v.mode = UI_FILES_LIST;
    setup();
    ui_files_draw(&cv, (struct rect){ 0, 0, 720, 480 }, 4, &fonts, &v);
    CHECK_EQ(at(470, 108), UI_SURFACE);                 /* Move to Trash's fill */
    CHECK_EQ(at(60, 120), UI_SURFACE);                  /* New Folder there now */
}

/* 1.5 stage 14b: This PC (its Open square, cards, hit test and picture),
 * a place as a drop target, the band. The window (0, 0, 720, 480) at 100 %
 * with the bar: the command row at y 100, the content (170, 141, 549, 309);
 * card k at (186, 157 + 72 k, 480, 64), its bar at (254, 187 + 72 k, 200,
 * 8). Each literal worked out by hand (T4). */
static void test_thispc(void)
{
    static struct ui_files_card cards[3] = {
        { "Memory (Live): /, /tmp", "512.0 KiB used, 512.0 KiB free of 1.0 MiB", UI_ICON_MEMORY, true, false, 512, 1024 },
        { "Disk 0 'madire-test': /home (read-only)", "", UI_ICON_DISK, true, true, 3, 4 },
        { "CD drive: not used", "", UI_ICON_CD, false, false, 0, 0 },
    };
    struct ui_files_layout l;
    struct ui_files_view v;
    struct files_pathbar pb;
    int32_t i;
    memset(&v, 0, sizeof(v));
    ui_files_layout((struct rect){ 0, 0, 720, 480 }, 4, &blank, true, &l);
    CHECK(same_rect(l.pc_open, 9, 104, 32, 32));
    CHECK(same_rect(l.content, 170, 141, 549, 309));
    CHECK(same_rect(ui_files_card_rect(&l, 4, 0), 186, 157, 480, 64));
    CHECK(same_rect(ui_files_card_rect(&l, 4, 2), 186, 301, 480, 64));
    v.mode = UI_FILES_LIST;
    v.bar = UI_FILES_BAR_LIVE;
    v.thispc = true;
    v.cards = cards;
    v.ncards = 3;
    v.card_sel = 1;
    v.card_hover = 2;
    v.place = 8;
    for (int k = 0; k < UF_BUTTONS; k++)
        v.enabled[k] = true;
    /* the hit test: Open at the row's start, the other squares and Show
     * hidden files gone, the cards in the content */
    CHECK_EQ(ui_files_hit(&l, &v, 4, &blank, 20, 110, &i), UF_OPEN);
    CHECK_EQ(ui_files_hit(&l, &v, 4, &blank, 270, 110, &i), UF_NONE);     /* where Open is in a folder */
    CHECK_EQ(ui_files_hit(&l, &v, 4, &blank, l.button[UF_HIDDEN].x + 4, 110, &i), UF_NONE);
    CHECK_EQ(ui_files_hit(&l, &v, 4, &blank, 300, 180, &i), UF_CONTENT);
    CHECK_EQ(i, 0);
    CHECK_EQ(ui_files_hit(&l, &v, 4, &blank, 665, 157, &i), UF_CONTENT);  /* card 0's top right pixel */
    CHECK_EQ(i, 0);
    CHECK_EQ(ui_files_hit(&l, &v, 4, &blank, 300, 225, &i), UF_CONTENT);  /* between cards 0 and 1 */
    CHECK_EQ(i, -1);
    CHECK_EQ(ui_files_hit(&l, &v, 4, &blank, 300, 340, &i), UF_CONTENT);
    CHECK_EQ(i, 2);
    v.ncards = 2;                                               /* no CD drive: no third card */
    CHECK_EQ(ui_files_hit(&l, &v, 4, &blank, 300, 340, &i), UF_CONTENT);
    CHECK_EQ(i, -1);
    v.ncards = 3;
    /* the picture */
    files_pathbar(FILES_VIEW_THISPC, "/", 1, ui_files_path_cells(&l, 4, &blank), &pb);
    v.pb = &pb;
    setup();
    ui_files_draw(&cv, (struct rect){ 0, 0, 720, 480 }, 4, &fonts, &v);
    CHECK_EQ(at(426, 157), UI_BORDER);                  /* card 0's edge (at rest) */
    CHECK_EQ(at(600, 200), UI_SURFACE);                 /* card 0's fill, right of its bar */
    CHECK_EQ(at(304, 191), UI_ACCENT);                  /* its bar: 512 of 1024 = 100 of 200 px used */
    CHECK_EQ(at(404, 191), UI_BORDER);                  /* the rest of the track */
    CHECK_EQ(at(426, 229), UI_ACCENT);                  /* card 1 selected: its accent edge */
    CHECK_EQ(at(600, 270), UI_SURFACE_ACTIVE);
    CHECK_EQ(at(304, 263), UI_TEXT_MUTED);              /* read-only: 150 of 200 px in text.muted */
    CHECK_EQ(at(600, 340), UI_SURFACE_HOVER);           /* card 2 under the pointer, no bar */
    CHECK_EQ(at(330, 340), UI_SURFACE_HOVER);
    CHECK_EQ(at(700, 420), UI_SURFACE_WINDOW);          /* the content's fill below the cards */
    CHECK_EQ(at(137, 79), UI_ACCENT);                   /* the path bar's This PC icon: its screen */
    CHECK_EQ(at(277, 110), UI_SURFACE_WINDOW);          /* no square where Open is in a folder */
    CHECK_EQ(at(25, 104), UI_BORDER);                   /* the Open square at the row's start */
    /* a place as a drop target: Documents (2) at (1, 213, 168, 32) */
    v.drop_on = true;
    v.drop_place = 2;
    setup();
    ui_files_draw(&cv, (struct rect){ 0, 0, 720, 480 }, 4, &fonts, &v);
    CHECK_EQ(at(100, 229), UI_SURFACE_HOVER);
    CHECK_EQ(at(100, 213), UI_ACCENT);
    v.drop_on = false;
    setup();
    ui_files_draw(&cv, (struct rect){ 0, 0, 720, 480 }, 4, &fonts, &v);
    CHECK_EQ(at(100, 229), UI_SURFACE);
}

static void test_band_frame(void)
{
    struct ui_files_view v;
    struct files_pathbar pb;
    struct ui_list list;
    static struct ui_list_item items[3];
    struct ui_files_layout l;
    memset(&v, 0, sizeof(v));
    for (int k = 0; k < 3; k++) {
        items[k].name = "x";
        items[k].icon = UI_ICON_NONE;
    }
    ui_files_layout((struct rect){ 0, 0, 720, 480 }, 4, &blank, true, &l);
    files_pathbar(FILES_VIEW_FOLDER, "/home/user", 10, 56, &pb);
    ui_list_init(&list, UI_LIST_DETAILS, items, 3, ui_files_panel_labels, l.content, 4);
    v.mode = UI_FILES_LIST;
    v.bar = UI_FILES_BAR_LIVE;
    v.pb = &pb;
    v.list = &list;
    v.band_on = true;
    v.band = (struct rect){ 300, 300, 100, 50 };
    setup();
    ui_files_draw(&cv, (struct rect){ 0, 0, 720, 480 }, 4, &fonts, &v);
    CHECK_EQ(at(350, 300), UI_ACCENT);                  /* the band's top */
    CHECK_EQ(at(300, 320), UI_ACCENT);                  /* its left */
    CHECK_EQ(at(399, 349), UI_ACCENT);                  /* its bottom right */
    CHECK_EQ(at(350, 325), UI_SURFACE_WINDOW);          /* no fill */
    v.band = (struct rect){ 300, 100, 100, 200 };       /* above the list's body: clipped there */
    setup();
    ui_files_draw(&cv, (struct rect){ 0, 0, 720, 480 }, 4, &fonts, &v);
    CHECK(at(350, 100) != UI_ACCENT);
    CHECK_EQ(at(300, 250), UI_ACCENT);
}

int main(void)
{
    printf("test_ui_files\n");
    RUN(test_thispc);               /* 1.5 stage 14b */
    RUN(test_band_frame);
    RUN(test_layout_s13);           /* 1.5 stage 13 */
    RUN(test_hit_s13);
    RUN(test_esc_ask);
    RUN(test_draw_s13);
    RUN(test_esc);
    RUN(test_layout);
    RUN(test_hit);
    RUN(test_draw_marks);
    RUN(test_view_marking);
    RUN(test_hidden_count);
    RUN(test_viewer_lines);
    RUN(test_viewer_control_bytes);
    RUN(test_viewer_scroll);
    return unit_report("test_ui_files");
}
