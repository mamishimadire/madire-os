/* Unit tests for kernel/display/ui_menus.c (the desktop's menus and the
 * Restart dialog, what the pointer and the keys do to them; 1.3 stage 5,
 * decisions 9 and 20): the M button's states (hover, pressed, open; none
 * while a menu or the dialog has the pointer); a click on it opens the M
 * menu with no row selected, another closes it; the pointer selects rows
 * (and clears its own selection, not the keyboard's); a click on a row
 * chooses it (About and Console open and focus their windows; Restart
 * opens the dialog); a click or a right click outside closes a menu and
 * does nothing else; the desktop menu on a right click on the desktop only
 * (not on a window or the taskbar), placed at the hot spot; Up, Down,
 * Enter and Esc in a menu, and every key kept while one is open; the
 * Windows key: it opens the M menu with About selected and the ring, closes
 * it, replaces the desktop menu, is never passed on (also with a window
 * focused), does nothing while the dialog is open, and lets go of a drag;
 * the dialog: Cancel focused, Tab, Enter on each button, Esc, its buttons'
 * and close button's states and clicks, and every click kept. Fonts are
 * made here (a fixed bit pattern). The toolkit's modules are included
 * directly; surface_core.c, wallpaper_core.c and coverage_core.c are
 * linked (the Makefile). */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "tests/unit/unit.h"
#include "kernel/display/ui_rect.c"
#include "kernel/display/ui_draw.c"
#include "kernel/display/icons.c"
#include "kernel/display/ui_icon.c"
#include "kernel/display/ui_widget.c"
#include "kernel/display/ui_window.c"
#include "kernel/display/ui_menu.c"
#include "kernel/display/ui_dialog.c"
#include "kernel/display/ui_menus.c"

static uint8_t font_bits[2][256 * 16];
static struct font font_store[2];

static struct ui_fonts fonts_4(void)
{
    for (int b = 0; b < 2; b++) {
        struct font *f = &font_store[b];
        if (!f->bits) {
            for (uint32_t k = 0; k < 256u * 16; k++)
                font_bits[b][k] = (uint8_t)((k * (37u + 6u * b) + (k >> 3)) & (b ? 0xB5u : 0xA5u));
            *f = (struct font){ 8, 16, 1, font_bits[b] };
        }
    }
    return (struct ui_fonts){ &font_store[0], &font_store[1] };
}

static bool same_rect(struct rect a, int32_t x, int32_t y, int32_t w, int32_t h)
{
    return a.x == x && a.y == y && a.w == w && a.h == h;
}

/* A 1024 x 768 screen at 100 %: the taskbar from 728, the M button's cell
 * (0, 728) 40 x 40; the M menu at (8, 538) 288 x 182, its rows' centres
 * About (152, 617), Console (152, 653), Restart (152, 698); the About
 * window opens at (336, 175) 352 x 377, the Console window at (191, 147)
 * 642 x 433; the dialog at (332, 278) 360 x 172: Cancel's centre (516,
 * 410), Restart's (620, 410), its close button's (676, 294). */
static struct ui_damage dmg;
static struct ui_wm wm;
static struct ui_window about, console;
static struct ui_menus mn;

static void setup(void)
{
    struct ui_fonts f = fonts_4();
    ui_damage_init(&dmg, 1024, 768);
    ui_wm_init(&wm, 1024, 768, 728, 4, &dmg);
    about = (struct ui_window){ .name = "About", .title = "About", .icon = UI_ICON_ABOUT, .w = 352, .h = 377,
                                .esc_closes = true };
    console = (struct ui_window){ .name = "Console", .title = "Console", .icon = UI_ICON_CONSOLE, .w = 642,
                                  .h = 433 };
    ui_menus_init(&mn, &wm, &about, &console, (struct rect){ 0, 728, 40, 40 }, &f);
}

static void clear(void)
{
    ui_menus_clear_events(&mn);
    ui_wm_clear_events(&wm);
    ui_damage_clear(&dmg);
}

static void move(int32_t x, int32_t y)
{
    ui_menus_pointer(&mn, x, y, false, false, false);
}

static void tpress(int32_t x, int32_t y)
{
    ui_menus_pointer(&mn, x, y, true, false, false);
}

static void trelease(int32_t x, int32_t y)
{
    ui_menus_pointer(&mn, x, y, false, true, false);
}

static void click(int32_t x, int32_t y)
{
    tpress(x, y);
    trelease(x, y);
}

static void right(int32_t x, int32_t y)
{
    ui_menus_pointer(&mn, x, y, false, false, true);
}

static enum ui_menus_route key(enum ui_menus_key k)
{
    enum ui_menus_route r = ui_menus_key(&mn, k, true);
    enum ui_menus_route r2 = ui_menus_key(&mn, k, false);
    return r == UI_MENUS_TAKEN && r2 == UI_MENUS_TAKEN ? UI_MENUS_TAKEN : UI_MENUS_TO_WM;
}

static int mevents(enum ui_menus_ev_kind k)
{
    int n = 0;
    for (uint32_t i = 0; i < mn.n_ev; i++)
        n += mn.ev[i].kind == k;
    return n;
}

/* The last event of kind k (its why), or -1. */
static int last_why(enum ui_menus_ev_kind k)
{
    int why = -1;
    for (uint32_t i = 0; i < mn.n_ev; i++)
        if (mn.ev[i].kind == k)
            why = mn.ev[i].why;
    return why;
}

static int wevents(enum ui_wm_event_kind k)
{
    int n = 0;
    for (uint32_t i = 0; i < wm.n_ev; i++)
        n += wm.ev[i].kind == k;
    return n;
}

static void test_m_button(void)
{
    setup();
    CHECK_EQ(mn.m_state, UI_M_NORMAL);
    move(20, 748);
    CHECK_EQ(mn.m_state, UI_M_HOVER);
    CHECK_EQ(mevents(UI_MEV_M_STATE), 1);
    CHECK(dmg.n > 0);
    tpress(20, 748);
    CHECK_EQ(mn.m_state, UI_M_PRESSED);
    CHECK(!wm.left);                                /* the menus keep the press */
    move(60, 748);
    CHECK_EQ(mn.m_state, UI_M_NORMAL);              /* off the button while held */
    move(20, 748);
    CHECK_EQ(mn.m_state, UI_M_PRESSED);
    clear();
    trelease(20, 748);
    CHECK(mn.menu_open && mn.menu.kind == UI_MENU_M && mn.menu.sel == -1 && !mn.menu.ring);
    CHECK(same_rect(mn.menu.r, 8, 538, 288, 182));
    CHECK_EQ(mn.m_state, UI_M_OPEN);
    CHECK_EQ(last_why(UI_MEV_MENU_OPEN), UI_WHY_MOUSE);
    CHECK(wm.focus == 0 && wm.n_ev == 0);
    /* Released off the button: nothing opens. */
    setup();
    tpress(20, 748);
    trelease(300, 300);
    CHECK(!mn.menu_open && mn.m_state == UI_M_NORMAL);
    /* A click on the button with the M menu open closes it. */
    setup();
    click(20, 748);
    clear();
    click(20, 748);
    CHECK(!mn.menu_open);
    CHECK_EQ(last_why(UI_MEV_MENU_CLOSE), UI_WHY_M_BUTTON);
    CHECK_EQ(mn.m_state, UI_M_HOVER);
}

static void test_rows_by_pointer(void)
{
    setup();
    click(20, 748);
    clear();
    move(152, 617);
    CHECK(mn.menu.sel == 0 && !mn.menu.ring);
    CHECK_EQ(mevents(UI_MEV_MENU_SELECT), 1);
    move(152, 560);                                 /* the header: the pointer's selection cleared */
    CHECK_EQ(mn.menu.sel, -1);
    move(152, 617);
    clear();
    click(152, 617);
    CHECK(!mn.menu_open && about.open && wm.focus == &about);
    CHECK_EQ(mevents(UI_MEV_MENU_CHOSEN), 1);
    CHECK_EQ(last_why(UI_MEV_MENU_CLOSE), UI_WHY_CHOSEN);
    CHECK_EQ(wevents(UI_EV_OPEN), 1);
    CHECK_EQ(ui_menus_take_done(&mn), UI_MENU_ABOUT);
    CHECK_EQ(ui_menus_take_done(&mn), UI_MENU_NONE);
    click(20, 748);
    click(152, 653);
    CHECK(console.open && wm.focus == &console && about.open);
    CHECK_EQ(ui_menus_take_done(&mn), UI_MENU_CONSOLE);
    /* About again: raised and focused, not opened twice. */
    click(20, 748);
    clear();
    click(152, 617);
    CHECK(wm.focus == &about && wm.z[wm.n - 1] == &about && wm.n == 2);
    CHECK_EQ(wevents(UI_EV_OPEN), 0);
    CHECK_EQ(wevents(UI_EV_RAISE), 1);
    /* A press in the menu released off its rows chooses nothing. */
    click(20, 748);
    tpress(152, 617);
    trelease(152, 560);
    CHECK(mn.menu_open);
}

static void test_outside(void)
{
    setup();
    ui_wm_open(&wm, &about);
    click(20, 748);
    clear();
    tpress(500, 100);                                /* the desktop */
    CHECK(!mn.menu_open);
    CHECK_EQ(last_why(UI_MEV_MENU_CLOSE), UI_WHY_OUTSIDE);
    trelease(500, 100);
    CHECK(wm.focus == &about && !wm.left);          /* the click did nothing else */
    CHECK_EQ(wevents(UI_EV_UNFOCUS), 0);
    click(436, 191);                                /* after it, the window manager has the pointer again */
    CHECK(wm.focus == &about);
    click(500, 100);
    CHECK(wm.focus == 0);
    /* A right click outside the M menu closes it. */
    click(20, 748);
    right(900, 300);
    CHECK(!mn.menu_open);
    CHECK_EQ(last_why(UI_MEV_MENU_CLOSE), UI_WHY_OUTSIDE);
}

static void test_desktop_menu(void)
{
    setup();
    right(900, 700);
    CHECK(mn.menu_open && mn.menu.kind == UI_MENU_DESKTOP && mn.menu.sel == -1);
    CHECK(same_rect(mn.menu.r, 800, 648, 224, 80));
    CHECK_EQ(mn.m_state, UI_M_NORMAL);
    CHECK_EQ(key(UI_MKEY_OTHER), UI_MENUS_TAKEN);   /* every key kept while it is open */
    CHECK_EQ(key(UI_MKEY_ESC), UI_MENUS_TAKEN);
    CHECK(!mn.menu_open);
    CHECK_EQ(last_why(UI_MEV_MENU_CLOSE), UI_WHY_ESC);
    CHECK_EQ(key(UI_MKEY_OTHER), UI_MENUS_TO_WM);
    ui_wm_open(&wm, &about);
    right(436, 300);                                /* on a window: nothing */
    CHECK(!mn.menu_open);
    right(500, 740);                                /* on the taskbar: nothing */
    CHECK(!mn.menu_open);
    right(100, 100);
    CHECK(same_rect(mn.menu.r, 100, 100, 224, 80));
    move(150, 120);
    CHECK_EQ(mn.menu.sel, 0);
    click(150, 120);
    CHECK(!mn.menu_open && console.open && wm.focus == &console);
    CHECK_EQ(ui_menus_take_done(&mn), UI_MENU_CONSOLE);
    right(100, 100);
    click(150, 160);
    CHECK(wm.focus == &about);
    CHECK_EQ(ui_menus_take_done(&mn), UI_MENU_ABOUT);
    /* A press on the M button with the desktop menu open is outside it. */
    right(100, 100);
    click(20, 748);
    CHECK(!mn.menu_open);
}

static void test_keyboard(void)
{
    setup();
    clear();
    CHECK_EQ(ui_menus_key(&mn, UI_MKEY_WINDOWS, true), UI_MENUS_TAKEN);
    CHECK(mn.menu_open && mn.menu.kind == UI_MENU_M && mn.menu.sel == 0 && mn.menu.ring);
    CHECK_EQ(last_why(UI_MEV_MENU_OPEN), UI_WHY_KEY);
    CHECK_EQ(mn.m_state, UI_M_OPEN);
    CHECK_EQ(ui_menus_key(&mn, UI_MKEY_WINDOWS, false), UI_MENUS_TAKEN);
    CHECK(mn.menu_open);
    key(UI_MKEY_DOWN);
    CHECK(mn.menu.sel == 1 && mn.menu.ring);
    key(UI_MKEY_DOWN);
    CHECK_EQ(mn.menu.sel, 2);
    key(UI_MKEY_DOWN);
    CHECK_EQ(mn.menu.sel, 0);
    key(UI_MKEY_UP);
    CHECK_EQ(mn.menu.sel, 2);
    /* The pointer off the rows keeps the keyboard's selection. */
    move(500, 100);
    CHECK(mn.menu.sel == 2 && mn.menu.ring);
    move(152, 653);
    CHECK(mn.menu.sel == 1 && !mn.menu.ring);
    move(500, 100);
    CHECK_EQ(mn.menu.sel, -1);
    key(UI_MKEY_ENTER);                             /* none selected: nothing */
    CHECK(mn.menu_open);
    key(UI_MKEY_UP);
    CHECK(mn.menu.sel == 2 && mn.menu.ring);
    key(UI_MKEY_ENTER);
    CHECK(!mn.menu_open && mn.dlg_open && mn.dlg.ring && mn.dlg.focus == UI_DLG_CANCEL);
    CHECK(same_rect(mn.dlg.w.r, 332, 278, 360, 172));
}

static void test_windows_key_routing(void)
{
    setup();
    ui_wm_open(&wm, &console);
    CHECK_EQ(key(UI_MKEY_OTHER), UI_MENUS_TO_WM);   /* to the window manager: the console */
    CHECK_EQ(key(UI_MKEY_WINDOWS), UI_MENUS_TAKEN); /* never passed on, with a window focused */
    CHECK(mn.menu_open && wm.focus == &console);
    CHECK_EQ(key(UI_MKEY_OTHER), UI_MENUS_TAKEN);
    CHECK_EQ(key(UI_MKEY_ENTER), UI_MENUS_TAKEN);   /* About chosen */
    CHECK(about.open && wm.focus == &about);
    key(UI_MKEY_WINDOWS);
    CHECK(mn.menu_open);
    key(UI_MKEY_WINDOWS);
    CHECK(!mn.menu_open);
    CHECK_EQ(last_why(UI_MEV_MENU_CLOSE), UI_WHY_WINDOWS);
    key(UI_MKEY_WINDOWS);
    key(UI_MKEY_DOWN);
    CHECK_EQ(key(UI_MKEY_ENTER), UI_MENUS_TAKEN);
    CHECK(wm.focus == &console);
    CHECK_EQ(ui_menus_take_done(&mn), UI_MENU_CONSOLE);
    CHECK_EQ(key(UI_MKEY_ESC), UI_MENUS_TO_WM);     /* no menu: Esc goes on */
    /* The Windows key replaces the desktop menu with the M menu. */
    right(100, 100);
    CHECK(mn.menu_open && mn.menu.kind == UI_MENU_DESKTOP);
    clear();
    key(UI_MKEY_WINDOWS);
    CHECK(mn.menu_open && mn.menu.kind == UI_MENU_M && mn.menu.sel == 0);
    CHECK_EQ(mevents(UI_MEV_MENU_CLOSE), 1);
    CHECK_EQ(mevents(UI_MEV_MENU_OPEN), 1);
    /* With the dialog open the Windows key does nothing. */
    key(UI_MKEY_UP);
    key(UI_MKEY_ENTER);
    CHECK(mn.dlg_open);
    clear();
    CHECK_EQ(key(UI_MKEY_WINDOWS), UI_MENUS_TAKEN);
    CHECK(mn.dlg_open && !mn.menu_open && mn.n_ev == 0);
    CHECK_EQ(key(UI_MKEY_OTHER), UI_MENUS_TAKEN);
    CHECK_EQ(key(UI_MKEY_UP), UI_MENUS_TAKEN);
}

static void test_dialog_keys(void)
{
    setup();
    key(UI_MKEY_WINDOWS);
    key(UI_MKEY_UP);
    key(UI_MKEY_ENTER);
    CHECK(mn.dlg_open && mn.dlg.focus == UI_DLG_CANCEL);
    clear();
    key(UI_MKEY_TAB);
    CHECK(mn.dlg.focus == UI_DLG_RESTART && mn.dlg.ring);
    CHECK_EQ(mevents(UI_MEV_DLG_FOCUS), 1);
    key(UI_MKEY_TAB);
    CHECK_EQ(mn.dlg.focus, UI_DLG_CANCEL);
    key(UI_MKEY_ENTER);                             /* Enter on Cancel */
    CHECK(!mn.dlg_open && !mn.restart);
    CHECK_EQ(last_why(UI_MEV_DLG_CLOSE), UI_WHY_CANCEL);
    key(UI_MKEY_WINDOWS);
    key(UI_MKEY_UP);
    key(UI_MKEY_ENTER);
    key(UI_MKEY_ESC);
    CHECK(!mn.dlg_open && !mn.restart);
    CHECK_EQ(last_why(UI_MEV_DLG_CLOSE), UI_WHY_ESC);
    key(UI_MKEY_WINDOWS);
    key(UI_MKEY_UP);
    key(UI_MKEY_ENTER);
    key(UI_MKEY_TAB);
    clear();
    key(UI_MKEY_ENTER);                             /* Enter on Restart */
    CHECK(mn.restart && mn.dlg_open);
    CHECK_EQ(mevents(UI_MEV_RESTART), 1);
}

static void test_dialog_pointer(void)
{
    setup();
    click(20, 748);
    click(152, 698);
    CHECK(mn.dlg_open && !mn.dlg.ring && mn.dlg.focus == UI_DLG_CANCEL);
    CHECK_EQ(mn.m_state, UI_M_NORMAL);
    move(20, 748);
    CHECK_EQ(mn.m_state, UI_M_NORMAL);              /* no hover while the dialog is open */
    move(516, 410);
    CHECK_EQ(mn.dlg.btn[UI_DLG_CANCEL], UI_BUTTON_HOVER);
    tpress(516, 410);
    CHECK_EQ(mn.dlg.btn[UI_DLG_CANCEL], UI_BUTTON_PRESSED);
    move(400, 300);
    CHECK_EQ(mn.dlg.btn[UI_DLG_CANCEL], UI_BUTTON_NORMAL);
    trelease(400, 300);
    CHECK(mn.dlg_open && !mn.restart);
    move(620, 410);
    CHECK(mn.dlg.btn[UI_DLG_RESTART] == UI_BUTTON_HOVER && mn.dlg.btn[UI_DLG_CANCEL] == UI_BUTTON_NORMAL);
    tpress(620, 410);
    CHECK_EQ(mn.dlg.btn[UI_DLG_RESTART], UI_BUTTON_PRESSED);
    trelease(620, 410);
    CHECK(mn.restart && mn.dlg_open);
    mn.restart = false;
    move(676, 294);
    CHECK_EQ(mn.dlg.w.close, UI_BUTTON_HOVER);
    tpress(100, 100);                                /* outside: kept, nothing happens */
    trelease(100, 100);
    CHECK(mn.dlg_open && !wm.left && wm.n_ev == 0);
    CHECK_EQ(mn.dlg.w.close, UI_BUTTON_NORMAL);
    click(676, 294);
    CHECK(!mn.dlg_open);
    CHECK_EQ(last_why(UI_MEV_DLG_CLOSE), UI_WHY_CLOSE_BUTTON);
    /* Cancel by a click. */
    click(20, 748);
    click(152, 698);
    click(516, 410);
    CHECK(!mn.dlg_open && !mn.restart);
    CHECK_EQ(last_why(UI_MEV_DLG_CLOSE), UI_WHY_CANCEL);
}

static void test_windows_key_ends_drag(void)
{
    setup();
    ui_wm_open(&wm, &about);
    tpress(436, 191);                                /* the About window's title bar */
    CHECK(wm.drag == &about && wm.left);
    move(456, 201);
    CHECK_EQ(about.r.x, 356);
    clear();
    key(UI_MKEY_WINDOWS);
    CHECK(!wm.left && wm.drag == 0 && mn.menu_open);
    CHECK_EQ(wevents(UI_EV_DRAG), 1);
    trelease(456, 201);                              /* the release that follows does nothing */
    CHECK(mn.menu_open && about.r.x == 356 && about.open);
    /* The close button pressed, then the Windows key: nothing closes. */
    setup();
    ui_wm_open(&wm, &about);
    tpress(672, 191);
    CHECK(wm.pressed == &about);
    key(UI_MKEY_WINDOWS);
    CHECK(wm.pressed == 0 && about.open);
    key(UI_MKEY_ESC);
    trelease(672, 191);
    CHECK(about.open);
}

int main(void)
{
    printf("test_ui_menus\n");
    RUN(test_m_button);
    RUN(test_rows_by_pointer);
    RUN(test_outside);
    RUN(test_desktop_menu);
    RUN(test_keyboard);
    RUN(test_windows_key_routing);
    RUN(test_dialog_keys);
    RUN(test_dialog_pointer);
    RUN(test_windows_key_ends_drag);
    return unit_report("test_ui_menus");
}
