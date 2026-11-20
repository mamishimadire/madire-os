/*
 * Windows and the window manager (1.3 stage 4a); see ui_window.h.
 */
#include "kernel/display/ui_window.h"
#include "kernel/display/tokens.h"
#include "kernel/display/ui_icon.h"
#include "kernel/display/ui_trace.h"

/* The close button: the guide's close style (ui_widget.c), filled at the
 * window's top-right corner only, with the window's radius. */
static const struct ui_button_style window_close = {
    { UI_COLOUR_NONE, UI_DANGER, UI_DANGER_PRESSED }, { UI_TEXT, UI_TEXT, UI_TEXT }, false, 6, UI_CORNER_TR, false,
};

/* 1.5 stage 8: the minimise and maximise buttons (ui_window.h): square,
 * `surface.hover` on hover (a state the pointer shows, as Cancel's), and
 * `accent.pressed` while pressed. */
static const struct ui_button_style win_titlebtn = {
    { UI_COLOUR_NONE, UI_SURFACE_HOVER, UI_ACCENT_PRESSED }, { UI_TEXT, UI_TEXT, UI_TEXT }, false, 0, 0, true,
};

int32_t ui_window_title_h(uint32_t s)
{
    return ui_px(32, s);
}

int32_t ui_window_edge(uint32_t s)
{
    return ui_px(1, s);
}

int32_t ui_window_radius(uint32_t s)
{
    return ui_px(6, s);
}

void ui_window_size_for_client(uint32_t s, int32_t cw, int32_t ch, int32_t *w, int32_t *h)
{
    *w = cw + 2 * ui_window_edge(s);
    *h = ui_window_title_h(s) + ch + ui_window_edge(s);
}

struct rect ui_window_close_rect(struct rect r, uint32_t s)
{
    int32_t tb = ui_window_title_h(s);
    return (struct rect){ r.x + r.w - tb, r.y, tb, tb };
}

/* 1.5 stage 8: the title-bar buttons from the right: close (k 0), then
 * maximise if the window has it, then minimise if it has it. */
struct rect ui_window_part_rect(const struct ui_window *w, struct rect r, uint32_t s, enum ui_hit part)
{
    int32_t tb = ui_window_title_h(s), k;
    if (part == UI_HIT_CLOSE)
        k = 0;
    else if (part == UI_HIT_MAXIMISE && (w->buttons & UI_WIN_MAXIMISE))
        k = 1;
    else if (part == UI_HIT_MINIMISE && (w->buttons & UI_WIN_MINIMISE))
        k = (w->buttons & UI_WIN_MAXIMISE) ? 2 : 1;
    else
        return (struct rect){ 0, 0, 0, 0 };
    return (struct rect){ r.x + r.w - (k + 1) * tb, r.y, tb, tb };
}

/* The leftmost title-bar button's left edge. */
static int32_t win_buttons_left(const struct ui_window *w, struct rect r, uint32_t s)
{
    int32_t k = 1 + ((w->buttons & UI_WIN_MAXIMISE) ? 1 : 0) + ((w->buttons & UI_WIN_MINIMISE) ? 1 : 0);
    return r.x + r.w - k * ui_window_title_h(s);
}

void ui_window_draw(const struct ui_canvas *c, const struct ui_window *w, struct rect r, bool active,
                    enum ui_button_state close, uint32_t s, const struct ui_fonts *f)
{
    int32_t R = ui_window_radius(s), tb = ui_window_title_h(s);
    if (ui_rect_empty(ui_rect_intersect(c->clip, r)))
        return;
    ui_fill_round(c, r, R, UI_CORNERS_ALL, UI_SURFACE_WINDOW);
    ui_frame_round(c, r, R, ui_window_edge(s), UI_CORNERS_ALL, active ? UI_BORDER_WINDOW : UI_BORDER_INACTIVE);
    /* 1.5 stage 8: a maximised window's client in its own size, centred
     * across the window, from its top; 1.5 stage 9: a resized one's too. */
    struct rect client = r;
    if (w->maximised || (w->resizable && (r.w != w->w || r.h != w->h)))
        client = (struct rect){ r.x + (r.w - w->w) / 2, r.y, w->w, w->h };
    if (w->paint)
        w->paint(c, client, w->ctx);
    int32_t ip = ui_px(16, s);
    ui_icon_draw(c, w->icon, r.x + ui_px(8, s), r.y + (tb - ip) / 2, ip, UI_TEXT);
    int32_t tx = r.x + ui_px(32, s), end = win_buttons_left(w, r, s) - ui_px(8, s);
    struct ui_canvas t = { c->s, ui_rect_intersect(c->clip, (struct rect){ tx, r.y, end - tx, tb }) };
    if (!ui_rect_empty(t.clip))
        ui_text(&t, f->bold, tx, r.y + (tb - (int32_t)f->bold->height) / 2, w->title,
                active ? UI_TEXT : UI_TEXT_MUTED);
    struct ui_button b = { ui_window_close_rect(r, s), 0, UI_ICON_CLOSE, 16 };
    ui_button_draw(c, &b, &window_close, close, false, s, f);
    if (w->buttons & UI_WIN_MAXIMISE) {
        struct ui_button mb = { ui_window_part_rect(w, r, s, UI_HIT_MAXIMISE), 0,
                                w->maximised ? UI_ICON_UNMAXIMISE : UI_ICON_MAXIMISE, 16 };
        ui_button_draw(c, &mb, &win_titlebtn, w->max_st, false, s, f);
    }
    if (w->buttons & UI_WIN_MINIMISE) {
        struct ui_button nb = { ui_window_part_rect(w, r, s, UI_HIT_MINIMISE), 0, UI_ICON_MINIMISE, 16 };
        ui_button_draw(c, &nb, &win_titlebtn, w->min_st, false, s, f);
    }
}

/* ---- The window manager ---- */

void ui_wm_init(struct ui_wm *m, int32_t width, int32_t height, int32_t bar_y, uint32_t s, struct ui_damage *d)
{
    m->width = width;
    m->height = height;
    m->bar_y = bar_y;
    m->scale = s;
    m->damage = d;
    m->n = 0;
    m->focus = 0;
    m->x = width / 2;
    m->y = height / 2;
    m->left = false;
    m->drag = 0;
    m->pressed = 0;
    m->n_ev = 0;
    m->lost_ev = 0;
    /* 1.5 stage 8 */
    m->pressed_part = UI_HIT_CLOSE;
    m->task_pressed = 0;
    m->task_x0 = 0;
    m->task_x1 = 0;
    m->switching = false;
    m->switch_i = 0;
    m->target = 0;
    m->now = 0;
    m->click_w = 0;
    m->click_at = 0;
    m->click_x = 0;
    m->click_y = 0;
    m->press_x = 0;
    m->press_y = 0;
    m->resize = 0;                      /* 1.5 stage 9 */
    m->resize_edges = 0;
}

void ui_wm_clear_events(struct ui_wm *m)
{
    m->n_ev = 0;
}

/* An event with a state (1.5 stage 8: the button states' events). */
static void event_state(struct ui_wm *m, enum ui_wm_event_kind kind, struct ui_window *w, enum ui_button_state st)
{
    if (m->n_ev >= UI_WM_EVENTS) {
        m->lost_ev++;
        return;
    }
    struct ui_wm_event *e = &m->ev[m->n_ev++];
    e->kind = kind;
    e->w = w;
    e->r = w ? w->r : (struct rect){ 0, 0, 0, 0 };
    e->state = st;
}

static void event(struct ui_wm *m, enum ui_wm_event_kind kind, struct ui_window *w)
{
    event_state(m, kind, w, w ? w->close : UI_BUTTON_NORMAL);
}

static void damage(struct ui_wm *m, struct rect r)
{
    if (m->damage)
        ui_damage_add(m->damage, r);
}

/* 1.5 stage 8: the taskbar buttons changed (their number, a state, the
 * focus, the Alt+Tab mark): the whole strip is drawn again. */
static void task_damage(struct ui_wm *m)
{
    damage(m, ui_wm_task_strip(m));
}

/* Removes w from list a of n entries (if it is there); true if it was. */
static bool win_list_remove(struct ui_window **a, uint32_t n, const struct ui_window *w)
{
    for (uint32_t i = 0; i < n; i++)
        if (a[i] == w) {
            for (uint32_t k = i; k + 1 < n; k++)
                a[k] = a[k + 1];
            return true;
        }
    return false;
}

/* The top window that is not minimised, or none. */
static struct ui_window *win_top_shown(const struct ui_wm *m)
{
    for (uint32_t i = m->n; i-- > 0;)
        if (!m->z[i]->minimised)
            return m->z[i];
    return 0;
}

/* w's look changed (focus or close button): its surface needs painting again. */
static void look_changed(struct ui_wm *m, struct ui_window *w)
{
    w->dirty = true;
    damage(m, w->r);
}

static int32_t win_clamp(int64_t v, int64_t lo, int64_t hi)
{
    if (v > hi)
        v = hi;
    if (v < lo)
        v = lo;
    return (int32_t)v;
}

struct rect ui_wm_clamp(const struct ui_wm *m, struct rect r)
{
    int32_t keep = ui_px(48, m->scale);
    int64_t ymax = (int64_t)m->bar_y - r.h;
    r.x = win_clamp(r.x, (int64_t)keep - r.w, (int64_t)m->width - keep);
    r.y = win_clamp(r.y, 0, ymax < 0 ? 0 : ymax);
    return r;
}

static bool place_taken(const struct ui_wm *m, int32_t x, int32_t y)
{
    for (uint32_t i = 0; i < m->n; i++)
        if (m->z[i]->r.x == x && m->z[i]->r.y == y)
            return true;
    return false;
}

struct rect ui_wm_place(const struct ui_wm *m, int32_t w, int32_t h)
{
    int32_t step = ui_px(24, m->scale);
    int32_t x = (m->width - w) / 2, y = (m->bar_y - h) / 2;
    if (y < 0)
        y = 0;
    for (uint32_t i = 0; i < UI_WINDOWS_MAX && place_taken(m, x, y); i++) {
        x += step;
        y += step;
    }
    return ui_wm_clamp(m, (struct rect){ x, y, w, h });
}

enum ui_hit ui_wm_hit(const struct ui_wm *m, int32_t x, int32_t y, struct ui_window **w)
{
    *w = 0;
    for (uint32_t i = m->n; i-- > 0;) {
        struct ui_window *win = m->z[i];
        if (win->minimised || !ui_rect_contains(win->r, x, y))     /* 1.5 stage 8: minimised: no hit */
            continue;
        *w = win;
        if (ui_rect_contains(ui_window_close_rect(win->r, m->scale), x, y))
            return UI_HIT_CLOSE;
        if (ui_rect_contains(ui_window_part_rect(win, win->r, m->scale, UI_HIT_MAXIMISE), x, y))
            return UI_HIT_MAXIMISE;
        if (ui_rect_contains(ui_window_part_rect(win, win->r, m->scale, UI_HIT_MINIMISE), x, y))
            return UI_HIT_MINIMISE;
        if (ui_window_edges(win, win->r, m->scale, x, y))   /* 1.5 stage 9: after the buttons */
            return UI_HIT_RESIZE;
        if (y < (int64_t)win->r.y + ui_window_title_h(m->scale))
            return UI_HIT_TITLE;
        return UI_HIT_BODY;
    }
    if (y >= m->bar_y) {
        for (uint32_t k = 0; k < m->n; k++)                     /* 1.5 stage 8: the window buttons */
            if (ui_rect_contains(ui_wm_task_rect(m, k), x, y)) {
                *w = m->order[k];
                return UI_HIT_TASKBUTTON;
            }
        return UI_HIT_TASKBAR;
    }
    return UI_HIT_DESKTOP;
}

static int find(const struct ui_wm *m, const struct ui_window *w)
{
    for (uint32_t i = 0; i < m->n; i++)
        if (m->z[i] == w)
            return (int)i;
    return -1;
}

static void set_focus(struct ui_wm *m, struct ui_window *w)
{
    if (m->focus == w)
        return;
    struct ui_window *old = m->focus;
    m->focus = w;
    if (old)
        look_changed(m, old);
    task_damage(m);                     /* 1.5 stage 8: the focused window's button */
    if (w) {
        /* 1.5 stage 8: most recently focused first. */
        if (win_list_remove(m->mru, m->n, w))
            for (uint32_t k = m->n - 1; k > 0; k--)
                m->mru[k] = m->mru[k - 1];
        m->mru[0] = w;
        look_changed(m, w);
        event(m, UI_EV_FOCUS, w);
    } else {
        event(m, UI_EV_UNFOCUS, 0);
    }
}

static void raise_window(struct ui_wm *m, struct ui_window *w)
{
    int i = find(m, w);
    if (i < 0 || (uint32_t)i == m->n - 1)
        return;
    for (uint32_t k = (uint32_t)i; k + 1 < m->n; k++)
        m->z[k] = m->z[k + 1];
    m->z[m->n - 1] = w;
    damage(m, w->r);
    event(m, UI_EV_RAISE, w);
}

/* 1.5 stage 8: a taskbar button's state (as the M button's): pressed while
 * the button pressed on it is held over it; hover while the pointer is over
 * it with no button held and nothing pressed; else normal. */
static enum ui_button_state win_task_state(const struct ui_wm *m, const struct ui_window *w)
{
    if (!w->open || m->drag || m->resize)   /* 1.5 stage 9: a resize as a drag */
        return UI_BUTTON_NORMAL;
    struct ui_window *at;
    bool over = ui_wm_hit(m, m->x, m->y, &at) == UI_HIT_TASKBUTTON && at == w;
    if (m->task_pressed)
        return m->task_pressed == w && over ? UI_BUTTON_PRESSED : UI_BUTTON_NORMAL;
    return over && !m->left && !m->pressed ? UI_BUTTON_HOVER : UI_BUTTON_NORMAL;
}

/* The title-bar buttons' and the taskbar buttons' states, after any change
 * of the pointer or the windows (1.5 stage 8: close, maximise, minimise,
 * then the taskbar button, window by window from the bottom). */
static void update_close_states(struct ui_wm *m)
{
    for (uint32_t i = 0; i < m->n; i++) {
        struct ui_window *w = m->z[i];
        enum ui_button_state st = ui_wm_close_state(m, w);
        if (st != w->close) {
            w->close = st;
            w->dirty = true;
            damage(m, ui_window_close_rect(w->r, m->scale));
            event(m, UI_EV_CLOSE_STATE, w);
        }
        st = ui_wm_part_state(m, w, UI_HIT_MAXIMISE);
        if (st != w->max_st) {
            w->max_st = st;
            w->dirty = true;
            damage(m, ui_window_part_rect(w, w->r, m->scale, UI_HIT_MAXIMISE));
            event_state(m, UI_EV_MAX_STATE, w, st);
        }
        st = ui_wm_part_state(m, w, UI_HIT_MINIMISE);
        if (st != w->min_st) {
            w->min_st = st;
            w->dirty = true;
            damage(m, ui_window_part_rect(w, w->r, m->scale, UI_HIT_MINIMISE));
            event_state(m, UI_EV_MIN_STATE, w, st);
        }
        st = win_task_state(m, w);
        if (st != w->task_st) {
            w->task_st = st;
            task_damage(m);
            event_state(m, UI_EV_TASK_STATE, w, st);
        }
    }
}

enum ui_button_state ui_wm_part_state(const struct ui_wm *m, const struct ui_window *w, enum ui_hit part)
{
    if (!w->open || m->drag || m->resize || w->minimised)      /* 1.5 stage 9: a resize as a drag */
        return UI_BUTTON_NORMAL;
    struct ui_window *at;
    bool over = ui_wm_hit(m, m->x, m->y, &at) == part && at == w;
    if (m->pressed)
        return m->pressed == w && m->pressed_part == part && over ? UI_BUTTON_PRESSED : UI_BUTTON_NORMAL;
    return over && !m->left ? UI_BUTTON_HOVER : UI_BUTTON_NORMAL;
}

enum ui_button_state ui_wm_close_state(const struct ui_wm *m, const struct ui_window *w)
{
    return ui_wm_part_state(m, w, UI_HIT_CLOSE);
}

bool ui_wm_open(struct ui_wm *m, struct ui_window *w)
{
    if (!w->open) {
        if (m->n >= UI_WINDOWS_MAX)
            return false;
        w->r = ui_wm_place(m, w->w, w->h);
        w->open = true;
        w->close = UI_BUTTON_NORMAL;
        w->min_st = UI_BUTTON_NORMAL;       /* 1.5 stage 8 */
        w->max_st = UI_BUTTON_NORMAL;
        w->task_st = UI_BUTTON_NORMAL;
        w->minimised = false;
        w->maximised = false;
        w->dirty = true;
        m->order[m->n] = w;
        m->mru[m->n] = w;
        m->z[m->n++] = w;
        damage(m, w->r);
        task_damage(m);
        event(m, UI_EV_OPEN, w);
    } else if (w->minimised) {
        ui_wm_activate(m, w);               /* 1.5 stage 8: restored, raised and focused */
        return true;
    } else {
        raise_window(m, w);
    }
    set_focus(m, w);
    update_close_states(m);
    return true;
}

static void close_window(struct ui_wm *m, struct ui_window *w, enum ui_wm_event_kind why)
{
    int i = find(m, w);
    if (i < 0)
        return;
    for (uint32_t k = (uint32_t)i; k + 1 < m->n; k++)
        m->z[k] = m->z[k + 1];
    win_list_remove(m->order, m->n, w);     /* 1.5 stage 8 */
    win_list_remove(m->mru, m->n, w);
    m->n--;
    w->open = false;
    w->close = UI_BUTTON_NORMAL;
    if (m->pressed == w)
        m->pressed = 0;
    if (m->drag == w)
        m->drag = 0;
    if (m->resize == w)                     /* 1.5 stage 9 */
        m->resize = 0;
    if (m->task_pressed == w)
        m->task_pressed = 0;
    if (m->click_w == w)
        m->click_w = 0;
    if (m->switching && m->target == w) {   /* the marked window gone: no switch */
        m->switching = false;
        m->target = 0;
    }
    damage(m, w->r);
    task_damage(m);
    event(m, why, w);
    if (m->focus == w) {
        m->focus = 0;
        struct ui_window *top = win_top_shown(m);       /* 1.5 stage 8: not a minimised one */
        if (top)
            set_focus(m, top);
        else
            event(m, UI_EV_UNFOCUS, 0);
    }
}

/* 1.5 stage 8: minimise, restore, maximise (ui_window.h). */
void ui_wm_minimise(struct ui_wm *m, struct ui_window *w)
{
    if (!w->open || w->minimised)
        return;
    w->minimised = true;
    if (m->drag == w)
        m->drag = 0;
    if (m->resize == w)                     /* 1.5 stage 9 */
        m->resize = 0;
    if (m->pressed == w)
        m->pressed = 0;
    if (m->click_w == w)
        m->click_w = 0;
    damage(m, w->r);
    task_damage(m);
    event(m, UI_EV_MINIMISE, w);
    if (m->focus == w) {
        m->focus = 0;
        look_changed(m, w);
        struct ui_window *top = win_top_shown(m);
        if (top)
            set_focus(m, top);
        else
            event(m, UI_EV_UNFOCUS, 0);
    }
}

void ui_wm_activate(struct ui_wm *m, struct ui_window *w)
{
    if (!w->open)
        return;
    if (w->minimised) {
        w->minimised = false;
        w->dirty = true;
        damage(m, w->r);
        task_damage(m);
        event(m, UI_EV_RESTORE, w);
    }
    raise_window(m, w);
    set_focus(m, w);
    update_close_states(m);
}

void ui_wm_toggle_maximise(struct ui_wm *m, struct ui_window *w)
{
    if (!w->open || w->minimised || !(w->buttons & UI_WIN_MAXIMISE))
        return;
    damage(m, w->r);
    if (w->maximised) {
        w->maximised = false;
        w->r = w->normal_r;
    } else {
        w->normal_r = w->r;
        w->maximised = true;
        w->r = (struct rect){ 0, 0, m->width, m->bar_y };
    }
    w->dirty = true;
    damage(m, w->r);
    event(m, w->maximised ? UI_EV_MAXIMISE : UI_EV_UNMAXIMISE, w);
}

static int32_t win_abs(int32_t v)
{
    return v < 0 ? -v : v;
}

static void press(struct ui_wm *m)
{
    struct ui_window *w;
    enum ui_hit hit = ui_wm_hit(m, m->x, m->y, &w);
    /* 1.5 stage 8: a click is two presses' business (the double click). */
    struct ui_window *click = m->click_w;
    bool dclick = click && click == w && hit == UI_HIT_TITLE && m->now - m->click_at <= UI_WM_DCLICK_TICKS &&
                  win_abs(m->x - m->click_x) <= ui_px(4, m->scale) && win_abs(m->y - m->click_y) <= ui_px(4, m->scale);
    m->click_w = 0;
    m->press_x = m->x;
    m->press_y = m->y;
    if (hit == UI_HIT_TASKBUTTON) {         /* 1.5 stage 8: pressed; its action on the release */
        m->task_pressed = w;
        return;
    }
    if (hit == UI_HIT_TASKBAR)
        return;
    if (hit == UI_HIT_DESKTOP) {
        set_focus(m, 0);
        return;
    }
    raise_window(m, w);
    set_focus(m, w);
    if (hit == UI_HIT_CLOSE || hit == UI_HIT_MINIMISE || hit == UI_HIT_MAXIMISE) {
        m->pressed = w;
        m->pressed_part = hit;
    } else if (hit == UI_HIT_RESIZE) {      /* 1.5 stage 9: the grab band */
        m->resize = w;
        m->resize_edges = ui_window_edges(w, w->r, m->scale, m->x, m->y);
        m->grab_x = m->x;
        m->grab_y = m->y;
        m->grab_r = w->r;
    } else if (hit == UI_HIT_TITLE && dclick && (w->buttons & UI_WIN_MAXIMISE)) {
        ui_wm_toggle_maximise(m, w);        /* 1.5 stage 8: the double click; no drag */
    } else if (hit == UI_HIT_TITLE) {
        m->drag = w;
        m->grab_x = m->x;
        m->grab_y = m->y;
        m->grab_r = w->r;
    }
}

/* 1.5 stage 9: a resize ends (the release, or the menus taking the pointer):
 * its event if the rectangle changed. */
static void win_resize_end(struct ui_wm *m)
{
    struct ui_window *w = m->resize;
    if (!w)
        return;
    m->resize = 0;
    struct rect g = m->grab_r;
    if (w->r.x != g.x || w->r.y != g.y || w->r.w != g.w || w->r.h != g.h)
        event(m, UI_EV_RESIZE, w);
}

static void release(struct ui_wm *m)
{
    win_resize_end(m);                      /* 1.5 stage 9 */
    if (m->drag) {
        struct ui_window *w = m->drag;
        m->drag = 0;
        if (w->r.x != m->grab_r.x || w->r.y != m->grab_r.y) {
            event(m, UI_EV_DRAG, w);
        } else {                            /* 1.5 stage 8: a click on the title bar */
            m->click_w = w;
            m->click_at = m->now;
            m->click_x = m->press_x;
            m->click_y = m->press_y;
        }
    }
    if (m->pressed) {
        struct ui_window *w = m->pressed, *at;
        enum ui_hit part = m->pressed_part;
        m->pressed = 0;
        if (ui_wm_hit(m, m->x, m->y, &at) == part && at == w) {
            if (part == UI_HIT_CLOSE)
                close_window(m, w, UI_EV_CLOSE_BUTTON);
            else if (part == UI_HIT_MINIMISE)
                ui_wm_minimise(m, w);
            else
                ui_wm_toggle_maximise(m, w);
        }
    }
    if (m->task_pressed) {                  /* 1.5 stage 8: the taskbar button's click */
        struct ui_window *w = m->task_pressed, *at;
        m->task_pressed = 0;
        if (ui_wm_hit(m, m->x, m->y, &at) == UI_HIT_TASKBUTTON && at == w) {
            if (w->minimised)
                ui_wm_activate(m, w);
            else if (m->focus == w)
                ui_wm_minimise(m, w);
            else
                ui_wm_activate(m, w);
        }
    }
}

void ui_wm_pointer(struct ui_wm *m, int32_t x, int32_t y, bool down, bool up)
{
    m->x = x;
    m->y = y;
    if (m->drag && !m->drag->maximised) {   /* 1.5 stage 8: a maximised window stays */
        struct ui_window *w = m->drag;
        struct rect r = m->grab_r;
        r.x = win_clamp((int64_t)r.x + x - m->grab_x, INT32_MIN, INT32_MAX);
        r.y = win_clamp((int64_t)r.y + y - m->grab_y, INT32_MIN, INT32_MAX);
        r = ui_wm_clamp(m, r);
        if (r.x != w->r.x || r.y != w->r.y) {
            damage(m, w->r);
            w->r = r;
            damage(m, w->r);
        }
    }
    if (m->resize) {                        /* 1.5 stage 9 */
        struct ui_window *w = m->resize;
        struct rect r = ui_wm_resize_rect(m, w, m->grab_r, m->resize_edges, (int64_t)x - m->grab_x,
                                          (int64_t)y - m->grab_y);
        if (r.x != w->r.x || r.y != w->r.y || r.w != w->r.w || r.h != w->r.h) {
            damage(m, w->r);
            w->r = r;
            w->dirty = true;                /* its surface painted again at the new size */
            damage(m, w->r);
        }
    }
    if (down && !m->left) {
        m->left = true;
        press(m);
    }
    if (up && m->left) {
        m->left = false;
        release(m);
    }
    update_close_states(m);
}

bool ui_wm_esc(struct ui_wm *m)
{
    struct ui_window *w = m->focus;
    if (!w || !w->esc_closes)
        return false;
    close_window(m, w, UI_EV_CLOSE_ESC);
    update_close_states(m);
    return true;
}

void ui_wm_cancel(struct ui_wm *m)
{
    if (!m->left)
        return;
    m->left = false;
    win_resize_end(m);                      /* 1.5 stage 9 */
    if (m->drag) {
        struct ui_window *w = m->drag;
        m->drag = 0;
        if (w->r.x != m->grab_r.x || w->r.y != m->grab_r.y)
            event(m, UI_EV_DRAG, w);
    }
    m->pressed = 0;
    m->task_pressed = 0;                    /* 1.5 stage 8 */
    m->click_w = 0;
    update_close_states(m);
}

enum ui_key_route ui_wm_key(struct ui_wm *m, struct ui_window *console, bool pressed, bool esc)
{
    if (!m->focus) {
        if (!pressed || !ui_wm_open(m, console))
            return UI_KEY_TAKEN;
        return UI_KEY_OPENED;
    }
    if (m->focus == console)
        return UI_KEY_TO_CONSOLE;
    if (pressed && esc)
        ui_wm_esc(m);
    return UI_KEY_TAKEN;
}

/* ---- 1.5 stage 8: the taskbar's window buttons, the middle button, Alt+Tab ---- */

void ui_wm_set_taskbar(struct ui_wm *m, int32_t x0, int32_t x1)
{
    m->task_x0 = x0;
    m->task_x1 = x1;
}

/* Each button's width: the span shared evenly (4 dp gaps), at most 160 dp,
 * never under 40 dp. */
static int32_t win_task_width(const struct ui_wm *m)
{
    int32_t gap = ui_px(4, m->scale), most = ui_px(160, m->scale), least = ui_px(40, m->scale);
    if (m->n == 0)
        return 0;
    int64_t room = (int64_t)m->task_x1 - m->task_x0 - (int64_t)(m->n - 1) * gap;
    int64_t w = room < 0 ? 0 : room / m->n;
    if (w > most)
        w = most;
    if (w < least)
        w = least;
    return (int32_t)w;
}

struct rect ui_wm_task_rect(const struct ui_wm *m, uint32_t k)
{
    if (k >= m->n)
        return (struct rect){ 0, 0, 0, 0 };
    int32_t top = m->bar_y + ui_window_edge(m->scale), bw = win_task_width(m), gap = ui_px(4, m->scale);
    return (struct rect){ m->task_x0 + (int32_t)k * (bw + gap), top, bw, m->height - top };
}

struct rect ui_wm_task_strip(const struct ui_wm *m)
{
    int32_t top = m->bar_y + ui_window_edge(m->scale);
    return (struct rect){ m->task_x0, top, m->task_x1 - m->task_x0, m->height - top };
}

/* The colour under (x, y) before a state mark is drawn there (ui_trace.h). */
static void win_trace(const struct ui_canvas *c, int kind, uint32_t rgb, int64_t x, int64_t y)
{
#ifdef UI_TRACE
    if (x >= 0 && y >= 0 && x < c->s->width && y < c->s->height)
        UI_TRACE_PAIR(kind, rgb, surface_get(c->s, (uint32_t)x, (uint32_t)y));
#else
    (void)c;
    (void)kind;
    (void)rgb;
    (void)x;
    (void)y;
#endif
}

void ui_wm_draw_taskbar(const struct ui_canvas *c, const struct ui_wm *m, const struct ui_fonts *f)
{
    uint32_t s = m->scale;
    for (uint32_t k = 0; k < m->n; k++) {
        const struct ui_window *w = m->order[k];
        struct rect b = ui_wm_task_rect(m, k);
        struct ui_canvas in = { c->s, ui_rect_intersect(c->clip, b) };
        if (ui_rect_empty(in.clip))
            continue;
        bool bar = m->focus == w || w->task_st == UI_BUTTON_PRESSED;
        if (bar || w->task_st == UI_BUTTON_HOVER) {
            win_trace(&in, bar ? UI_TRACE_CARRIED_BAR : UI_TRACE_CARRIED_POINTER, UI_SURFACE_ACTIVE,
                      (int64_t)b.x + b.w / 2, (int64_t)b.y + b.h / 2);
            ui_fill(&in, b, UI_SURFACE_ACTIVE);
        }
        if (bar) {
            int32_t h = ui_px(2, s);
            struct rect line = { b.x, b.y + b.h - h, b.w, h };
            win_trace(&in, UI_TRACE_STATE, UI_ACCENT, (int64_t)line.x + line.w / 2, line.y);
            ui_fill(&in, line, UI_ACCENT);
        }
        int32_t ip = ui_px(16, s);
        ui_icon_draw(&in, w->icon, b.x + ui_px(8, s), b.y + (b.h - ip) / 2, ip, UI_TEXT);
        int32_t tx = b.x + ui_px(32, s);
        struct ui_canvas t = { c->s, ui_rect_intersect(in.clip,
                                                      (struct rect){ tx, b.y, b.x + b.w - ui_px(8, s) - tx, b.h }) };
        if (!ui_rect_empty(t.clip))
            ui_text(&t, f->regular, tx, b.y + (b.h - (int32_t)f->regular->height) / 2, w->title, UI_TEXT);
        if (m->switching && m->target == w) {   /* the Alt+Tab mark */
            int32_t g = ui_px(2, s);
            struct rect ring = ui_rect_inflate(b, -g);
            win_trace(&in, UI_TRACE_STATE, UI_ACCENT, (int64_t)ring.x + ring.w / 2, ring.y);
            ui_frame_round(&in, ring, ui_px(4, s), g, UI_CORNERS_ALL, UI_ACCENT);
        }
    }
}

void ui_wm_set_now(struct ui_wm *m, uint64_t now)
{
    m->now = now;
}

void ui_wm_middle(struct ui_wm *m, int32_t x, int32_t y)
{
    struct ui_window *w;
    m->x = x;
    m->y = y;
    if (ui_wm_hit(m, x, y, &w) == UI_HIT_TASKBUTTON)
        close_window(m, w, UI_EV_CLOSE_MIDDLE);
    update_close_states(m);
}

bool ui_wm_switch_step(struct ui_wm *m, bool back)
{
    if (m->n == 0)
        return false;
    if (!m->switching) {
        m->switching = true;
        m->switch_i = back ? m->n - 1 : (m->n > 1 ? 1 : 0);
    } else {
        m->switch_i = (m->switch_i + (back ? m->n - 1 : 1)) % m->n;
    }
    m->target = m->mru[m->switch_i];
    task_damage(m);
    event(m, UI_EV_SWITCH_SELECT, m->target);
    return true;
}

void ui_wm_switch_commit(struct ui_wm *m)
{
    if (!m->switching)
        return;
    struct ui_window *w = m->target;
    m->switching = false;
    m->target = 0;
    task_damage(m);
    event(m, UI_EV_SWITCH_CHOSEN, w);
    ui_wm_activate(m, w);
}

void ui_wm_switch_cancel(struct ui_wm *m)
{
    if (!m->switching)
        return;
    m->switching = false;
    m->target = 0;
    task_damage(m);
    event(m, UI_EV_SWITCH_CANCEL, 0);
}

/* ---- 1.5 stage 9: resizing by the edges and corners ---- */

int32_t ui_window_band(uint32_t s)
{
    return ui_px(8, s);
}

uint32_t ui_window_edges(const struct ui_window *w, struct rect r, uint32_t s, int32_t x, int32_t y)
{
    if (!w->resizable || w->maximised || w->minimised || !ui_rect_contains(r, x, y))
        return 0;
    int64_t b = ui_window_band(s);
    uint32_t e = 0;
    if ((int64_t)x < (int64_t)r.x + b)
        e |= UI_EDGE_LEFT;
    else if ((int64_t)x >= (int64_t)r.x + r.w - b)
        e |= UI_EDGE_RIGHT;
    if ((int64_t)y < (int64_t)r.y + b)
        e |= UI_EDGE_TOP;
    else if ((int64_t)y >= (int64_t)r.y + r.h - b)
        e |= UI_EDGE_BOTTOM;
    return e;
}

static int64_t win_max64(int64_t a, int64_t b)
{
    return a > b ? a : b;
}

static int64_t win_min64(int64_t a, int64_t b)
{
    return a < b ? a : b;
}

/* v kept in [lo, hi]; the callers' ranges always hold the edge's start, so
 * lo <= hi. */
static int64_t win_clamp64(int64_t v, int64_t lo, int64_t hi)
{
    return v > hi ? hi : v < lo ? lo : v;
}

struct rect ui_wm_resize_rect(const struct ui_wm *m, const struct ui_window *w, struct rect g, uint32_t edges,
                              int64_t dx, int64_t dy)
{
    int64_t keep = ui_px(48, m->scale);
    /* The smallest: the window's minimum (at least 1 px), never above the
     * size it started with; the largest: the area above the taskbar, never
     * below the size it started with. */
    int64_t minw = win_min64(win_max64(w->min_w, 1), g.w), minh = win_min64(win_max64(w->min_h, 1), g.h);
    int64_t maxw = win_max64(m->width, g.w), maxh = win_max64(m->bar_y, g.h);
    int64_t left = g.x, top = g.y, right = (int64_t)g.x + g.w, bottom = (int64_t)g.y + g.h;
    if (edges & UI_EDGE_LEFT)
        left = win_clamp64(g.x + dx, win_max64(right - maxw, win_min64(0, g.x)),
                           win_min64(right - minw, win_max64((int64_t)m->width - keep, g.x)));
    else if (edges & UI_EDGE_RIGHT)
        right = win_clamp64(right + dx, win_max64(left + minw, win_min64(keep, right)),
                            win_min64(left + maxw, win_max64(m->width, right)));
    if (edges & UI_EDGE_TOP)
        top = win_clamp64(g.y + dy, win_max64(bottom - maxh, win_min64(0, g.y)), bottom - minh);
    else if (edges & UI_EDGE_BOTTOM)
        bottom = win_clamp64(bottom + dy, top + minh, win_min64(top + maxh, win_max64(m->bar_y, bottom)));
    return (struct rect){ (int32_t)left, (int32_t)top, (int32_t)(right - left), (int32_t)(bottom - top) };
}
