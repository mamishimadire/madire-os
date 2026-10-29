/*
 * Windows and the window manager (1.3 stage 4a); see ui_window.h.
 */
#include "kernel/display/ui_window.h"
#include "kernel/display/tokens.h"
#include "kernel/display/ui_icon.h"

/* The close button: the guide's close style (ui_widget.c), filled at the
 * window's top-right corner only, with the window's radius. */
static const struct ui_button_style window_close = {
    { UI_COLOUR_NONE, UI_DANGER, UI_DANGER_PRESSED }, { UI_TEXT, UI_TEXT, UI_TEXT }, false, 6, UI_CORNER_TR,
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

void ui_window_draw(const struct ui_canvas *c, const struct ui_window *w, struct rect r, bool active,
                    enum ui_button_state close, uint32_t s, const struct ui_fonts *f)
{
    int32_t R = ui_window_radius(s), tb = ui_window_title_h(s);
    if (ui_rect_empty(ui_rect_intersect(c->clip, r)))
        return;
    ui_fill_round(c, r, R, UI_CORNERS_ALL, UI_SURFACE_WINDOW);
    ui_frame_round(c, r, R, ui_window_edge(s), UI_CORNERS_ALL, active ? UI_BORDER_WINDOW : UI_BORDER_INACTIVE);
    if (w->paint)
        w->paint(c, r, w->ctx);
    int32_t ip = ui_px(16, s);
    ui_icon_draw(c, w->icon, r.x + ui_px(8, s), r.y + (tb - ip) / 2, ip, UI_TEXT);
    struct rect cr = ui_window_close_rect(r, s);
    int32_t tx = r.x + ui_px(32, s);
    struct ui_canvas t = { c->s, ui_rect_intersect(c->clip, (struct rect){ tx, r.y, cr.x - ui_px(8, s) - tx, tb }) };
    if (!ui_rect_empty(t.clip))
        ui_text(&t, f->bold, tx, r.y + (tb - (int32_t)f->bold->height) / 2, w->title,
                active ? UI_TEXT : UI_TEXT_MUTED);
    struct ui_button b = { cr, 0, UI_ICON_CLOSE, 16 };
    ui_button_draw(c, &b, &window_close, close, false, s, f);
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
}

void ui_wm_clear_events(struct ui_wm *m)
{
    m->n_ev = 0;
}

static void event(struct ui_wm *m, enum ui_wm_event_kind kind, struct ui_window *w)
{
    if (m->n_ev >= UI_WM_EVENTS) {
        m->lost_ev++;
        return;
    }
    struct ui_wm_event *e = &m->ev[m->n_ev++];
    e->kind = kind;
    e->w = w;
    e->r = w ? w->r : (struct rect){ 0, 0, 0, 0 };
    e->state = w ? w->close : UI_BUTTON_NORMAL;
}

static void damage(struct ui_wm *m, struct rect r)
{
    if (m->damage)
        ui_damage_add(m->damage, r);
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
        if (!ui_rect_contains(win->r, x, y))
            continue;
        *w = win;
        if (ui_rect_contains(ui_window_close_rect(win->r, m->scale), x, y))
            return UI_HIT_CLOSE;
        if (y < (int64_t)win->r.y + ui_window_title_h(m->scale))
            return UI_HIT_TITLE;
        return UI_HIT_BODY;
    }
    if (y >= m->bar_y)
        return UI_HIT_TASKBAR;
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
    if (w) {
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

/* The close buttons' states, after any change of the pointer or the windows. */
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
    }
}

enum ui_button_state ui_wm_close_state(const struct ui_wm *m, const struct ui_window *w)
{
    if (!w->open || m->drag)
        return UI_BUTTON_NORMAL;
    struct ui_window *at;
    bool over = ui_wm_hit(m, m->x, m->y, &at) == UI_HIT_CLOSE && at == w;
    if (m->pressed)
        return m->pressed == w && over ? UI_BUTTON_PRESSED : UI_BUTTON_NORMAL;
    return over && !m->left ? UI_BUTTON_HOVER : UI_BUTTON_NORMAL;
}

bool ui_wm_open(struct ui_wm *m, struct ui_window *w)
{
    if (!w->open) {
        if (m->n >= UI_WINDOWS_MAX)
            return false;
        w->r = ui_wm_place(m, w->w, w->h);
        w->open = true;
        w->close = UI_BUTTON_NORMAL;
        w->dirty = true;
        m->z[m->n++] = w;
        damage(m, w->r);
        event(m, UI_EV_OPEN, w);
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
    m->n--;
    w->open = false;
    w->close = UI_BUTTON_NORMAL;
    if (m->pressed == w)
        m->pressed = 0;
    if (m->drag == w)
        m->drag = 0;
    damage(m, w->r);
    event(m, why, w);
    if (m->focus == w) {
        m->focus = 0;
        if (m->n)
            set_focus(m, m->z[m->n - 1]);
        else
            event(m, UI_EV_UNFOCUS, 0);
    }
}

static void press(struct ui_wm *m)
{
    struct ui_window *w;
    enum ui_hit hit = ui_wm_hit(m, m->x, m->y, &w);
    if (hit == UI_HIT_TASKBAR)
        return;
    if (hit == UI_HIT_DESKTOP) {
        set_focus(m, 0);
        return;
    }
    raise_window(m, w);
    set_focus(m, w);
    if (hit == UI_HIT_CLOSE) {
        m->pressed = w;
    } else if (hit == UI_HIT_TITLE) {
        m->drag = w;
        m->grab_x = m->x;
        m->grab_y = m->y;
        m->grab_r = w->r;
    }
}

static void release(struct ui_wm *m)
{
    if (m->drag) {
        struct ui_window *w = m->drag;
        m->drag = 0;
        if (w->r.x != m->grab_r.x || w->r.y != m->grab_r.y)
            event(m, UI_EV_DRAG, w);
    }
    if (m->pressed) {
        struct ui_window *w = m->pressed, *at;
        m->pressed = 0;
        if (ui_wm_hit(m, m->x, m->y, &at) == UI_HIT_CLOSE && at == w)
            close_window(m, w, UI_EV_CLOSE_BUTTON);
    }
}

void ui_wm_pointer(struct ui_wm *m, int32_t x, int32_t y, bool down, bool up)
{
    m->x = x;
    m->y = y;
    if (m->drag) {
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
