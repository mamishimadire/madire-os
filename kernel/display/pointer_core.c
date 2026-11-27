/* The mouse pointer, the pure part (see pointer_core.h). */
#include "kernel/display/pointer_core.h"

/* Madire's own pointer, one character per dp (see pointer_core.h). */
const char *const pointer_shape[POINTER_DP_H] = {
    "o...........",
    "oo..........",
    "oao.........",
    "oawo........",
    "oawwo.......",
    "oawwwo......",
    "oawwwwo.....",
    "oawwwwwo....",
    "oawwwwwwo...",
    "oawwwwwwwo..",
    "oawwwwwwwwo.",
    "oawwwwwwwwwo",
    "oawwwwoooooo",
    "oawwwo......",
    "oawwo.......",
    "oawo........",
    "oao.........",
    "oo..........",
};

static uint8_t shape_px(char c)
{
    switch (c) {
    case 'o': return POINTER_PX_EDGE;
    case 'w': return POINTER_PX_FILL;
    case 'a': return POINTER_PX_SPINE;
    }
    return POINTER_NONE;
}

bool pointer_sprite_make(struct pointer_sprite *s, uint32_t scale)
{
    if (scale != 4 && scale != 5 && scale != 6 && scale != 8)
        return false;
    s->scale = scale;
    s->w = (POINTER_DP_W * scale + 2) / 4;
    s->h = (POINTER_DP_H * scale + 2) / 4;
    for (uint32_t y = 0; y < s->h; y++)
        for (uint32_t x = 0; x < s->w; x++)
            s->px[y * s->w + x] = shape_px(pointer_shape[y * POINTER_DP_H / s->h][x * POINTER_DP_W / s->w]);
    return true;
}

/* ---- 1.5 stage 14b: the v1.5 pointer set (see pointer_core.h) ---- */

static const char *const pointer_rows_resize_ew[9] = {
    "...ooo....ooo...",
    "..oowo....owoo..",
    ".oowwo....owwoo.",
    "oowwwoooooowwwoo",
    "owwwwwwwwwwwwwwo",
    "oowwwoooooowwwoo",
    ".oowwo....owwoo.",
    "..oowo....owoo..",
    "...ooo....ooo...",
};

static const char *const pointer_rows_resize_ns[16] = {
    "...ooo...",
    "..oowoo..",
    ".oowwwoo.",
    "oowwwwwoo",
    "owwwwwwwo",
    "oooowoooo",
    "...owo...",
    "...owo...",
    "...owo...",
    "...owo...",
    "oooowoooo",
    "owwwwwwwo",
    "oowwwwwoo",
    ".oowwwoo.",
    "..oowoo..",
    "...ooo...",
};

static const char *const pointer_rows_resize_nwse[13] = {
    "oooooooo.....",
    "owwwwwwo.....",
    "owwwwwoo.....",
    "owwwwoo......",
    "owwwwwoo.....",
    "owwowwwoo.ooo",
    "owooowwwooowo",
    "ooo.oowwwowwo",
    ".....oowwwwwo",
    "......oowwwwo",
    ".....oowwwwwo",
    ".....owwwwwwo",
    ".....oooooooo",
};

static const char *const pointer_rows_resize_nesw[13] = {
    ".....oooooooo",
    ".....owwwwwwo",
    ".....oowwwwwo",
    "......oowwwwo",
    ".....oowwwwwo",
    "ooo.oowwwowwo",
    "owooowwwooowo",
    "owwowwwoo.ooo",
    "owwwwwoo.....",
    "owwwwoo......",
    "owwwwwoo.....",
    "owwwwwwo.....",
    "oooooooo.....",
};

static const char *const pointer_rows_move[17] = {
    ".......ooo.......",
    "......oowoo......",
    ".....oowwwoo.....",
    "....oowwwwwoo....",
    "...oowwwwwwwoo...",
    "..oowooowooowoo..",
    ".oowwo.owo.owwoo.",
    "oowwwooowooowwwoo",
    "owwwwwwwwwwwwwwwo",
    "oowwwooowooowwwoo",
    ".oowwo.owo.owwoo.",
    "..oowooowooowoo..",
    "...oowwwwwwwoo...",
    "....oowwwwwoo....",
    ".....oowwwoo.....",
    "......oowoo......",
    ".......ooo.......",
};

static const char *const pointer_rows_ibeam[18] = {
    "ooooooo",
    "owwwwwo",
    "owwwwwo",
    "ooowooo",
    "..owo..",
    "..owo..",
    "..owo..",
    "..owo..",
    "..owo..",
    "..owo..",
    "..owo..",
    "..owo..",
    "..owo..",
    "..owo..",
    "ooowooo",
    "owwwwwo",
    "owwwwwo",
    "ooooooo",
};

static const char *const pointer_rows_hand[18] = {
    "...ooooo......",
    "...owwwo......",
    "...owwwo......",
    "...owwwo......",
    "...owwwo......",
    "...owwwoooo...",
    "...owwwowwoooo",
    "...owwwowwowwo",
    "oooowwwowwowwo",
    "owwowwwowwowwo",
    "owwowwwowwowwo",
    "owwwwwwwwwwwwo",
    "owwwwwwwwwwwwo",
    "ooowwwwwwwwwwo",
    "..owwwwwwwwwwo",
    "..oowwwwwwwwoo",
    "...oaaaaaaaao.",
    "...oooooooooo.",
};

static const char *const pointer_rows_busy[22] = {
    "o.....................",
    "oo....................",
    "oao...................",
    "oawo..................",
    "oawwo.................",
    "oawwwo................",
    "oawwwwo...............",
    "oawwwwwo..............",
    "oawwwwwwo.............",
    "oawwwwwwwo............",
    "oawwwwwwwwo...oooo....",
    "oawwwwwwwwwoooo11oooo.",
    "oawwwwoooooo88o11o22o.",
    "oawwwo.....o88oooo22o.",
    "oawwo.....ooooo..ooooo",
    "oawo......o77o....o33o",
    "oao.......o77o....o33o",
    "oo........ooooo..ooooo",
    "...........o66oooo44o.",
    "...........o66o55o44o.",
    "...........oooo55oooo.",
    "..............oooo....",
};

static const char *const pointer_rows_drag_move[22] = {
    "o.....................",
    "oo....................",
    "oao...................",
    "oawo..................",
    "oawwo.................",
    "oawwwo................",
    "oawwwwo...............",
    "oawwwwwo..............",
    "oawwwwwwo.............",
    "oawwwwwwwo............",
    "oawwwwwwwwooooooooooo.",
    "oawwwwwwwwwowwwwwwwwo.",
    "oawwwwoooooowwwwwwwwo.",
    "oawwwo.....owwwwwwwwo.",
    "oawwo......owwoooowwo.",
    "oawo.......owwwwwwwwo.",
    "oao........owwoooowwo.",
    "oo.........owwwwwwwwo.",
    "...........owwoooowwo.",
    "...........owwwwwwwwo.",
    "...........owwwwwwwwo.",
    "...........oooooooooo.",
};

static const char *const pointer_rows_drag_copy[22] = {
    "o.....................",
    "oo....................",
    "oao...................",
    "oawo..................",
    "oawwo.................",
    "oawwwo................",
    "oawwwwo...............",
    "oawwwwwo..............",
    "oawwwwwwo.............",
    "oawwwwwwwo............",
    "oawwwwwwwwo...........",
    "oawwwwwwwwwo.ooooooo..",
    "oawwwwooooooooaaaaaoo.",
    "oawwwo.....ooaaaaaaaoo",
    "oawwo......oaaaaoaaaao",
    "oawo.......oaaaaoaaaao",
    "oao........oaaoooooaao",
    "oo.........oaaaaoaaaao",
    "...........oaaaaoaaaao",
    "...........ooaaaaaaaoo",
    "............ooaaaaaoo.",
    ".............ooooooo..",
};

static const char *const pointer_rows_no_drop[22] = {
    "o.....................",
    "oo....................",
    "oao...................",
    "oawo..................",
    "oawwo.................",
    "oawwwo................",
    "oawwwwo...............",
    "oawwwwwo..............",
    "oawwwwwwo.............",
    "oawwwwwwwo............",
    "oawwwwwwwwo...........",
    "oawwwwwwwwwo.ooooooo..",
    "oawwwwoooooooodddddoo.",
    "oawwwo.....ooddoooddoo",
    "oawwo......odddooooddo",
    "oawo.......odoodoooodo",
    "oao........odooodooodo",
    "oo.........odoooodoodo",
    "...........oddoooodddo",
    "...........ooddoooddoo",
    "............oodddddoo.",
    ".............ooooooo..",
};

/* { name, rows, w, h, hot spot x, hot spot y } (tests/pointer_ref.py reads this table) */
const struct pointer_def pointer_defs[POINTER_KINDS] = {
    [POINTER_ARROW] = { "arrow", pointer_shape, 12, 18, 0, 0 },
    [POINTER_RESIZE_EW] = { "resize-ew", pointer_rows_resize_ew, 16, 9, 8, 4 },
    [POINTER_RESIZE_NS] = { "resize-ns", pointer_rows_resize_ns, 9, 16, 4, 8 },
    [POINTER_RESIZE_NWSE] = { "resize-nwse", pointer_rows_resize_nwse, 13, 13, 6, 6 },
    [POINTER_RESIZE_NESW] = { "resize-nesw", pointer_rows_resize_nesw, 13, 13, 6, 6 },
    [POINTER_MOVE] = { "move", pointer_rows_move, 17, 17, 8, 8 },
    [POINTER_IBEAM] = { "ibeam", pointer_rows_ibeam, 7, 18, 3, 9 },
    [POINTER_HAND] = { "hand", pointer_rows_hand, 14, 18, 5, 0 },
    [POINTER_BUSY] = { "busy", pointer_rows_busy, 22, 22, 0, 0 },
    [POINTER_DRAG_MOVE] = { "drag-move", pointer_rows_drag_move, 22, 22, 0, 0 },
    [POINTER_DRAG_COPY] = { "drag-copy", pointer_rows_drag_copy, 22, 22, 0, 0 },
    [POINTER_NO_DROP] = { "no-drop", pointer_rows_no_drop, 22, 22, 0, 0 },
};

/* A cell of a 1.5 kind: '1' to '8' are the busy pointer's dots, the one
 * of this frame in accent. */
static uint8_t pointer_kind_px(char c, uint32_t frame)
{
    switch (c) {
    case 'o': return POINTER_PX_EDGE;
    case 'w': return POINTER_PX_FILL;
    case 'a': return POINTER_PX_SPINE;
    case 'd': return POINTER_PX_DANGER;
    }
    if (c >= '1' && c <= '8')
        return (uint32_t)(c - '1') == frame % POINTER_BUSY_FRAMES ? POINTER_PX_SPINE : POINTER_PX_FILL;
    return POINTER_NONE;
}

bool pointer_sprite_kind(struct pointer_sprite *s, enum pointer_kind k, uint32_t scale, uint32_t frame)
{
    if (k == POINTER_ARROW)
        return pointer_sprite_make(s, scale);
    if ((uint32_t)k >= POINTER_KINDS || (scale != 4 && scale != 5 && scale != 6 && scale != 8))
        return false;
    const struct pointer_def *d = &pointer_defs[k];
    uint32_t w = (d->w * scale + 2) / 4, h = (d->h * scale + 2) / 4;
    if (w > POINTER_MAX_W || h > POINTER_MAX_H)
        return false;
    s->scale = scale;
    s->w = w;
    s->h = h;
    for (uint32_t y = 0; y < h; y++)
        for (uint32_t x = 0; x < w; x++)
            s->px[y * w + x] = pointer_kind_px(d->rows[y * d->h / h][x * d->w / w], frame);
    return true;
}

void pointer_hot(enum pointer_kind k, uint32_t scale, int32_t *hx, int32_t *hy)
{
    *hx = *hy = 0;
    if ((uint32_t)k >= POINTER_KINDS)
        return;
    *hx = (int32_t)((pointer_defs[k].hx * scale + 2) / 4);
    *hy = (int32_t)((pointer_defs[k].hy * scale + 2) / 4);
}

uint32_t pointer_busy_frame(uint64_t ticks)
{
    return (uint32_t)(ticks * 2u / 25u % POINTER_BUSY_FRAMES);
}

enum pointer_kind pointer_edges_kind(uint32_t edges)
{
    bool l = edges & 0x1u, r = edges & 0x2u, t = edges & 0x4u, b = edges & 0x8u;
    if ((t && l) || (b && r))
        return POINTER_RESIZE_NWSE;
    if ((t && r) || (b && l))
        return POINTER_RESIZE_NESW;
    if (l || r)
        return POINTER_RESIZE_EW;
    if (t || b)
        return POINTER_RESIZE_NS;
    return POINTER_ARROW;
}

enum pointer_kind pointer_choose(const struct pointer_wants *w)
{
    if (w->files_drag && w->files >= 0 && w->files < (int)POINTER_KINDS)
        return (enum pointer_kind)w->files;
    if (w->moving)
        return POINTER_MOVE;
    if (w->resizing)
        return pointer_edges_kind(w->resizing);
    if (w->menu)
        return POINTER_ARROW;
    if (w->band)
        return pointer_edges_kind(w->band);
    if (w->files >= 0 && w->files < (int)POINTER_KINDS)
        return (enum pointer_kind)w->files;
    return POINTER_ARROW;
}

uint32_t pointer_colour(uint8_t px)
{
    switch (px) {
    case POINTER_PX_EDGE:  return POINTER_EDGE;
    case POINTER_PX_FILL:  return POINTER_FILL;
    case POINTER_PX_SPINE: return POINTER_SPINE;
    case POINTER_PX_DANGER: return POINTER_DANGER;    /* 1.5 stage 14b */
    }
    return 0;
}

void pointer_clamp(int32_t *x, int32_t *y, uint32_t width, uint32_t height)
{
    if (*x < 0)
        *x = 0;
    if (*y < 0)
        *y = 0;
    if (width && *x > (int32_t)width - 1)
        *x = (int32_t)width - 1;
    if (height && *y > (int32_t)height - 1)
        *y = (int32_t)height - 1;
}

struct rect pointer_area(const struct pointer_sprite *s, int32_t x, int32_t y, uint32_t width,
                         uint32_t height)
{
    struct rect r = { x, y, (int32_t)s->w, (int32_t)s->h };
    rect_clip(&r, width, height);
    return r;
}

static bool under_surface(struct surface *u, struct pointer_under *p, const struct surface *screen)
{
    return p->r.w > 0 && p->r.h > 0 && p->r.w <= POINTER_MAX_W && p->r.h <= POINTER_MAX_H &&
           surface_init(u, p->pixels, (uint32_t)p->r.w, (uint32_t)p->r.h,
                        (uint32_t)p->r.w * screen->bytes_per_pixel, screen->bytes_per_pixel);
}

void pointer_save(struct pointer_under *u, const struct surface *screen, struct rect r)
{
    struct surface us;
    u->r = r;
    if (!under_surface(&us, u, screen)) {
        u->r = (struct rect){ 0, 0, 0, 0 };
        return;
    }
    surface_blit(&us, 0, 0, screen, r.x, r.y, r.w, r.h);
}

void pointer_restore(struct pointer_under *u, struct surface *screen)
{
    struct surface us;
    if (under_surface(&us, u, screen))
        surface_blit(screen, u->r.x, u->r.y, &us, 0, 0, u->r.w, u->r.h);
    u->r = (struct rect){ 0, 0, 0, 0 };
}

/* Each row in runs of one colour, each run one fill (clipped by it). */
void pointer_paint(struct surface *screen, const struct pointer_sprite *s, int32_t x, int32_t y)
{
    for (uint32_t row = 0; row < s->h; row++) {
        const uint8_t *px = &s->px[row * s->w];
        uint32_t i = 0;
        while (i < s->w) {
            uint32_t j = i + 1;
            while (j < s->w && px[j] == px[i])
                j++;
            if (px[i] != POINTER_NONE)
                surface_fill(screen, (struct rect){ x + (int32_t)i, y + (int32_t)row, (int32_t)(j - i), 1 },
                             pointer_colour(px[i]));
            i = j;
        }
    }
}
