/*
 * The desktop's menus and the Restart dialog (1.3 stage 5); see ui_menus.h.
 */
#include "kernel/display/ui_menus.h"

static void menus_dlg_states(struct ui_menus *m);

void ui_menus_init(struct ui_menus *m, struct ui_wm *wm, struct ui_window *about, struct ui_window *console,
                   struct rect m_cell, const struct ui_fonts *f)
{
    m->wm = wm;
    m->about = about;
    m->console = console;
    m->m_cell = m_cell;
    m->fonts.regular = f->regular;
    m->fonts.bold = f->bold;
    m->menu_open = false;
    ui_menu_init(&m->menu, UI_MENU_M, wm->scale, (struct rect){ 0, 0, 0, 0 });
    m->dlg_open = false;
    ui_dialog_init(&m->dlg, wm->scale, f, wm->width, wm->bar_y, false);
    m->x = wm->x;
    m->y = wm->y;
    m->left = false;
    m->press = UI_MPRESS_NONE;
    m->m_state = UI_M_NORMAL;
    m->done = UI_MENU_NONE;
    m->restart = false;
    m->n_ev = 0;
    m->lost_ev = 0;
}

void ui_menus_clear_events(struct ui_menus *m)
{
    m->n_ev = 0;
}

enum ui_menu_action ui_menus_take_done(struct ui_menus *m)
{
    enum ui_menu_action a = m->done;
    m->done = UI_MENU_NONE;
    return a;
}

/* A new event, filled from the menu as it is now (0 if the list is full). */
static struct ui_menus_event *menus_event(struct ui_menus *m, enum ui_menus_ev_kind kind)
{
    if (m->n_ev >= UI_MENUS_EVENTS) {
        m->lost_ev++;
        return 0;
    }
    struct ui_menus_event *e = &m->ev[m->n_ev++];
    e->kind = kind;
    e->menu = m->menu.kind;
    e->row = m->menu.sel;
    e->ring = m->menu.ring;
    e->why = UI_WHY_MOUSE;
    e->state = 0;
    e->r = m->menu.r;
    return e;
}

static void menus_damage(struct ui_menus *m, struct rect r)
{
    if (m->wm->damage)
        ui_damage_add(m->wm->damage, r);
}

static bool menus_over_m(const struct ui_menus *m)
{
    return ui_rect_contains(m->m_cell, m->x, m->y);
}

static void menus_open(struct ui_menus *m, enum ui_menu_kind k, bool by_key)
{
    uint32_t s = m->wm->scale;
    struct rect r = k == UI_MENU_M ? ui_menu_place_m(s, m->wm->bar_y)
                                   : ui_menu_place_at(s, m->wm->width, m->wm->bar_y, m->x, m->y);
    ui_menu_init(&m->menu, k, s, r);
    if (by_key) {
        m->menu.sel = 0;
        m->menu.ring = true;
    }
    m->menu_open = true;
    menus_damage(m, r);
    struct ui_menus_event *e = menus_event(m, UI_MEV_MENU_OPEN);
    if (e)
        e->why = by_key ? UI_WHY_KEY : UI_WHY_MOUSE;
}

/* After a menu or the dialog closed: the window manager follows the pointer again. */
static void menus_wm_follows(struct ui_menus *m)
{
    ui_wm_pointer(m->wm, m->x, m->y, false, false);
}

static void menus_close(struct ui_menus *m, enum ui_menus_why why)
{
    if (!m->menu_open)
        return;
    m->menu_open = false;
    menus_damage(m, m->menu.r);
    struct ui_menus_event *e = menus_event(m, UI_MEV_MENU_CLOSE);
    if (e)
        e->why = why;
    menus_wm_follows(m);
}

static void menus_select(struct ui_menus *m, int row, bool ring)
{
    if (m->menu.sel == row && m->menu.ring == ring)
        return;
    m->menu.sel = row;
    m->menu.ring = ring;
    menus_damage(m, m->menu.r);
    menus_event(m, UI_MEV_MENU_SELECT);
}

static void menus_dlg_open(struct ui_menus *m, bool ring)
{
    ui_dialog_init(&m->dlg, m->wm->scale, &m->fonts, m->wm->width, m->wm->bar_y, ring);
    m->dlg_open = true;
    menus_damage(m, m->dlg.w.r);
    struct ui_menus_event *e = menus_event(m, UI_MEV_DLG_OPEN);
    if (e) {
        e->r = m->dlg.w.r;
        e->ring = ring;
    }
    menus_dlg_states(m);
}

static void menus_dlg_close(struct ui_menus *m, enum ui_menus_why why)
{
    m->dlg_open = false;
    menus_damage(m, m->dlg.w.r);
    struct ui_menus_event *e = menus_event(m, UI_MEV_DLG_CLOSE);
    if (e)
        e->why = why;
    menus_wm_follows(m);
}

static void menus_restart(struct ui_menus *m)
{
    m->restart = true;
    menus_event(m, UI_MEV_RESTART);
}

static void menus_choose(struct ui_menus *m, int row, bool by_key)
{
    enum ui_menu_action a = ui_menu_action(m->menu.kind, row);
    struct ui_menus_event *e = menus_event(m, UI_MEV_MENU_CHOSEN);
    if (e)
        e->row = row;
    menus_close(m, UI_WHY_CHOSEN);
    if (a == UI_MENU_ABOUT || a == UI_MENU_CONSOLE) {
        if (ui_wm_open(m->wm, a == UI_MENU_ABOUT ? m->about : m->console))
            m->done = a;
    } else if (a == UI_MENU_RESTART) {
        menus_dlg_open(m, by_key);
    }
}

/* A button's (or the close button's) state: pressed while the press on it
 * is held and the pointer is over it, hover while over it with no press
 * held, else normal. */
static enum ui_button_state menus_part_state(const struct ui_menus *m, enum ui_menus_press p,
                                             enum ui_dialog_part part, enum ui_dialog_part at)
{
    bool over = at == part;
    if (m->left && m->press == p)
        return over ? UI_BUTTON_PRESSED : UI_BUTTON_NORMAL;
    return over && !m->left ? UI_BUTTON_HOVER : UI_BUTTON_NORMAL;
}

static void menus_dlg_states(struct ui_menus *m)
{
    enum ui_dialog_part at = ui_dialog_hit(&m->dlg, m->x, m->y);
    for (int i = 0; i < 3; i++) {
        enum ui_menus_press p = i == 0 ? UI_MPRESS_CANCEL : i == 1 ? UI_MPRESS_RESTART : UI_MPRESS_CLOSE;
        enum ui_dialog_part part = i == 0 ? UI_DLG_PART_CANCEL : i == 1 ? UI_DLG_PART_RESTART : UI_DLG_PART_CLOSE;
        enum ui_button_state st = menus_part_state(m, p, part, at);
        enum ui_button_state *now = i < 2 ? &m->dlg.btn[i] : &m->dlg.w.close;
        if (st == *now)
            continue;
        *now = st;
        menus_damage(m, m->dlg.w.r);
        struct ui_menus_event *e = menus_event(m, UI_MEV_DLG_STATE);
        if (e) {
            e->row = i;
            e->state = st;
        }
    }
}

static void menus_update_m(struct ui_menus *m)
{
    bool over = menus_over_m(m);
    enum ui_m_state st = UI_M_NORMAL;
    if (m->menu_open && m->menu.kind == UI_MENU_M)
        st = UI_M_OPEN;
    else if (m->left && m->press == UI_MPRESS_M)
        st = over ? UI_M_PRESSED : UI_M_NORMAL;
    else if (over && !m->left && !m->wm->left && !m->menu_open && !m->dlg_open)
        st = UI_M_HOVER;
    if (st == m->m_state)
        return;
    m->m_state = st;
    menus_damage(m, m->m_cell);
    struct ui_menus_event *e = menus_event(m, UI_MEV_M_STATE);
    if (e)
        e->state = st;
}

/* The left button released after a press the menus kept. */
static void menus_release(struct ui_menus *m)
{
    enum ui_menus_press p = m->press;
    m->left = false;
    m->press = UI_MPRESS_NONE;
    if (p == UI_MPRESS_M && menus_over_m(m)) {
        if (m->menu_open && m->menu.kind == UI_MENU_M)
            menus_close(m, UI_WHY_M_BUTTON);
        else if (!m->menu_open)
            menus_open(m, UI_MENU_M, false);
    } else if (p == UI_MPRESS_MENU && m->menu_open) {
        int row = ui_menu_row_at(&m->menu, m->x, m->y);
        if (row >= 0)
            menus_choose(m, row, false);
    }
}

static void menus_dlg_pointer(struct ui_menus *m, bool down, bool up)
{
    enum ui_dialog_part at = ui_dialog_hit(&m->dlg, m->x, m->y);
    if (down && !m->left) {
        m->left = true;
        m->press = at == UI_DLG_PART_CANCEL ? UI_MPRESS_CANCEL : at == UI_DLG_PART_RESTART ? UI_MPRESS_RESTART
                 : at == UI_DLG_PART_CLOSE ? UI_MPRESS_CLOSE : UI_MPRESS_OUTSIDE;
    }
    if (up && m->left) {
        enum ui_menus_press p = m->press;
        m->left = false;
        m->press = UI_MPRESS_NONE;
        if (p == UI_MPRESS_CANCEL && at == UI_DLG_PART_CANCEL) {
            menus_dlg_close(m, UI_WHY_CANCEL);
            return;
        }
        if (p == UI_MPRESS_CLOSE && at == UI_DLG_PART_CLOSE) {
            menus_dlg_close(m, UI_WHY_CLOSE_BUTTON);
            return;
        }
        if (p == UI_MPRESS_RESTART && at == UI_DLG_PART_RESTART)
            menus_restart(m);
    }
    menus_dlg_states(m);
}

static void menus_menu_pointer(struct ui_menus *m, bool down, bool up, bool right)
{
    int row = ui_menu_row_at(&m->menu, m->x, m->y);
    if (row >= 0)
        menus_select(m, row, false);
    else if (!m->menu.ring)
        menus_select(m, -1, false);
    bool inside = ui_rect_contains(m->menu.r, m->x, m->y);
    if (down && !m->left) {
        m->left = true;
        if (m->menu.kind == UI_MENU_M && menus_over_m(m)) {
            m->press = UI_MPRESS_M;
        } else if (inside) {
            m->press = UI_MPRESS_MENU;
        } else {
            m->press = UI_MPRESS_OUTSIDE;
            menus_close(m, UI_WHY_OUTSIDE);
        }
    } else if (right && !inside) {
        menus_close(m, UI_WHY_OUTSIDE);
    }
    if (up && m->left)
        menus_release(m);
}

void ui_menus_pointer(struct ui_menus *m, int32_t x, int32_t y, bool down, bool up, bool right)
{
    m->x = x;
    m->y = y;
    if (m->dlg_open) {
        menus_dlg_pointer(m, down, up);
    } else if (m->menu_open) {
        menus_menu_pointer(m, down, up, right);
    } else if (m->left) {
        if (up)
            menus_release(m);
    } else if (down && menus_over_m(m) && !m->wm->left) {
        m->left = true;
        m->press = UI_MPRESS_M;
    } else {
        ui_wm_pointer(m->wm, x, y, down, up);
        struct ui_window *w = 0;
        if (right && !m->wm->left && ui_wm_hit(m->wm, x, y, &w) == UI_HIT_DESKTOP)
            menus_open(m, UI_MENU_DESKTOP, false);
    }
    menus_update_m(m);
}

enum ui_menus_route ui_menus_key(struct ui_menus *m, enum ui_menus_key k, bool pressed)
{
    enum ui_menus_route route = UI_MENUS_TAKEN;
    if (m->dlg_open) {
        if (pressed && k == UI_MKEY_ENTER) {
            if (m->dlg.focus == UI_DLG_CANCEL)
                menus_dlg_close(m, UI_WHY_CANCEL);
            else
                menus_restart(m);
        } else if (pressed && k == UI_MKEY_TAB) {
            m->dlg.focus = m->dlg.focus == UI_DLG_CANCEL ? UI_DLG_RESTART : UI_DLG_CANCEL;
            m->dlg.ring = true;
            menus_damage(m, m->dlg.w.r);
            struct ui_menus_event *e = menus_event(m, UI_MEV_DLG_FOCUS);
            if (e)
                e->row = m->dlg.focus;
        } else if (pressed && k == UI_MKEY_ESC) {
            menus_dlg_close(m, UI_WHY_ESC);
        }
    } else if (k == UI_MKEY_WINDOWS) {
        if (pressed && m->menu_open && m->menu.kind == UI_MENU_M) {
            menus_close(m, UI_WHY_WINDOWS);
        } else if (pressed) {
            menus_close(m, UI_WHY_WINDOWS);
            m->left = false;
            m->press = UI_MPRESS_NONE;
            ui_wm_cancel(m->wm);
            menus_open(m, UI_MENU_M, true);
        }
    } else if (m->menu_open) {
        if (pressed && (k == UI_MKEY_UP || k == UI_MKEY_DOWN))
            menus_select(m, ui_menu_next(&m->menu, k == UI_MKEY_DOWN ? 1 : -1), true);
        else if (pressed && k == UI_MKEY_ENTER && m->menu.sel >= 0)
            menus_choose(m, m->menu.sel, true);
        else if (pressed && k == UI_MKEY_ESC)
            menus_close(m, UI_WHY_ESC);
    } else {
        route = UI_MENUS_TO_WM;
    }
    menus_update_m(m);
    return route;
}
