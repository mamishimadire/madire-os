#ifndef MADIRE_DISPLAY_POINTER_CORE_H
#define MADIRE_DISPLAY_POINTER_CORE_H

/*
 * The mouse pointer, the pure part (1.2 stage 7; host unit tests in
 * tests/unit/test_pointer.c). The kernel side is pointer.c.
 *
 * The sprite is Madire's own: a dart (its tip at the top-left pixel, the
 * hot spot), filled in the Design Guide's `text`, edged in `bg.deep`, with
 * an `accent` spine one pixel inside its left edge. It is drawn on a
 * 12 x 18 dp grid (pointer_shape: '.' none, 'o' edge, 'w' fill, 'a'
 * spine) and scaled nearest-neighbour (the Design Guide, section 6): at
 * scale s (quarter steps: 4, 5, 6, 8 for 100, 125, 150, 200 %) it is
 * (12 s + 2) / 4 x (18 s + 2) / 4 pixels, and pixel (x, y) shows the grid
 * cell (x 12 / w, y 18 / h). tests/pointer_ref.py renders it from these
 * rules and the rows below, with the colours read from the Design Guide.
 *
 * The pointer's position is its hot spot, clamped to the screen
 * ([0, width - 1] x [0, height - 1]); the sprite is clipped at the right
 * and bottom edges. Save-under: before the sprite is drawn, the screen's
 * pixels under it are copied to a buffer; hiding it copies them back.
 *
 * 1.5 stage 14b (docs/plans/1.5.md, notes (11), (12)(b), (14), (15)): the
 * v1.5 pointer set. Besides the arrow (pointer_shape and
 * pointer_sprite_make, unchanged: pixel-identical) eleven more kinds
 * (enum pointer_kind), each Madire's own drawing on its own grid, one
 * character per dp (pointer_defs: its rows, size and hot spot): the
 * resize pointers (east-west 16 x 9, hot spot (8, 4); north-south 9 x 16,
 * (4, 8); the two diagonals 13 x 13, (6, 6)), move (17 x 17, (8, 8)), the
 * I-beam (7 x 18, (3, 9)), the hand (14 x 18, (5, 0)), busy (the arrow with
 * a ring of eight dots, 22 x 22, (0, 0); 8 frames: the dot '1' + frame in
 * `accent`, the others in `text`) and the drag pointers (the arrow with a
 * page: drag move; with an `accent` disc and a plus: drag copy; with a
 * `danger` circle and slash: not allowed; 22 x 22, (0, 0)). Their
 * characters: '.' none, 'o' edge (`bg.deep`), 'w' fill (`text`), 'a'
 * `accent`, 'd' `danger`, '1' to '8' the busy pointer's dots. A sprite of
 * kind k (pointer_sprite_kind) is scaled as the arrow: at scale s it is
 * (W s + 2) / 4 x (H s + 2) / 4 pixels, pixel (x, y) showing the cell
 * (x W / w, y H / h); its hot spot in pixels is ((hx s + 2) / 4, (hy s +
 * 2) / 4) (pointer_hot: the arrow's (0, 0) stays (0, 0)). The pointer's
 * position stays the hot spot: the sprite's top left is the position less
 * the hot spot (pointer_area and pointer_paint clip at every edge).
 * tests/pointer_ref.py renders every kind from these rules and the rows
 * below.
 */

#include <stdbool.h>
#include <stdint.h>
#include "kernel/display/surface_core.h"
#include "kernel/display/tokens.h"

#define POINTER_DP_W   12
#define POINTER_DP_H   18
#define POINTER_MAX_W  44       /* at scale 8 (200 %): 22 dp (1.5 stage 14b; the arrow's 24 x 36 before) */
#define POINTER_MAX_H  44

/* The Design Guide's tokens (section 2), from tokens.h (1.3 stage 3). */
#define POINTER_EDGE   UI_BG_DEEP       /* bg.deep */
#define POINTER_FILL   UI_TEXT          /* text */
#define POINTER_SPINE  UI_ACCENT        /* accent */
#define POINTER_DANGER UI_DANGER        /* danger (1.5 stage 14b: not allowed) */

extern const char *const pointer_shape[POINTER_DP_H];

enum { POINTER_NONE, POINTER_PX_EDGE, POINTER_PX_FILL, POINTER_PX_SPINE,
       POINTER_PX_DANGER };             /* 1.5 stage 14b */

/* 1.5 stage 14b: the kinds of pointer (the rules above). */
enum pointer_kind {
    POINTER_ARROW, POINTER_RESIZE_EW, POINTER_RESIZE_NS, POINTER_RESIZE_NWSE, POINTER_RESIZE_NESW, POINTER_MOVE,
    POINTER_IBEAM, POINTER_HAND, POINTER_BUSY, POINTER_DRAG_MOVE, POINTER_DRAG_COPY, POINTER_NO_DROP, POINTER_KINDS
};
#define POINTER_BUSY_FRAMES 8u

struct pointer_def {
    const char *name;                   /* as logged: "arrow", "resize-ew", ... */
    const char *const *rows;            /* h rows of w characters */
    uint8_t w, h, hx, hy;               /* in dp; the hot spot's cell */
};
extern const struct pointer_def pointer_defs[POINTER_KINDS];

struct pointer_sprite {
    uint32_t scale;
    uint32_t w, h;
    uint8_t px[POINTER_MAX_W * POINTER_MAX_H];  /* row by row, w per row */
};

/* The sprite at scale 4, 5, 6 or 8; false (and *s unchanged) for any other. */
bool pointer_sprite_make(struct pointer_sprite *s, uint32_t scale);

/* 1.5 stage 14b: the sprite of kind k at scale 4, 5, 6 or 8 (the arrow's
 * is pointer_sprite_make's), the busy pointer at frame `frame` (modulo 8);
 * false (and *s unchanged) for any other scale or kind. */
bool pointer_sprite_kind(struct pointer_sprite *s, enum pointer_kind k, uint32_t scale, uint32_t frame);

/* 1.5 stage 14b: kind k's hot spot in pixels at scale s ((0, 0) for an
 * unknown kind). */
void pointer_hot(enum pointer_kind k, uint32_t scale, int32_t *hx, int32_t *hy);

/* 1.5 stage 14b: the busy pointer's frame at `ticks` (100 Hz): one frame
 * every 125 ms, 8 frames a turn. */
uint32_t pointer_busy_frame(uint64_t ticks);

/* 1.5 stage 14b: which pointer the desktop shows (desktop.c asks every
 * packet and every pass of its loop). The edges are ui_window.h's
 * UI_EDGE_* bits (left 1, right 2, top 4, bottom 8). In this order:
 *   1. Files dragging items: Files' kind (drag move, drag copy, not
 *      allowed), wherever the pointer is;
 *   2. a window dragged by its title bar: move;
 *   3. a window being resized: the resize pointer of its edges;
 *   4. a menu or the dialog open: the arrow;
 *   5. over a window's grab band: the resize pointer of its edges;
 *   6. Files' own kind over its window (busy, the I-beam over the rename
 *      field, the hand over a path-bar part), if it has one (>= 0);
 *   7. the arrow.
 * The resize pointer of the edges: left or right alone east-west, top or
 * bottom alone north-south, top left or bottom right north-west to
 * south-east, top right or bottom left north-east to south-west. */
struct pointer_wants {
    bool files_drag;                    /* Files is dragging (files its kind) */
    bool moving;                        /* a title-bar drag */
    uint32_t resizing;                  /* a resize's edges, or 0 */
    bool menu;                          /* a menu or the dialog is open */
    uint32_t band;                      /* the grab band's edges under the pointer, or 0 */
    int files;                          /* Files' kind (enum pointer_kind), or -1 */
};
enum pointer_kind pointer_edges_kind(uint32_t edges);
enum pointer_kind pointer_choose(const struct pointer_wants *w);

/* The colour 0xRRGGBB of a sprite pixel (POINTER_PX_*), 0 for none. */
uint32_t pointer_colour(uint8_t px);

/* Clamps a hot spot to a width x height screen. */
void pointer_clamp(int32_t *x, int32_t *y, uint32_t width, uint32_t height);

/* The sprite's rectangle with its hot spot at (x, y), clipped to the
 * screen (empty if nothing is left). */
struct rect pointer_area(const struct pointer_sprite *s, int32_t x, int32_t y, uint32_t width,
                         uint32_t height);

/* The pixels under the sprite. */
struct pointer_under {
    struct rect r;              /* where they came from; empty if none saved */
    uint8_t pixels[POINTER_MAX_W * POINTER_MAX_H * 4];
};

/* Copies the screen's pixels in r (inside the screen, at most the largest
 * sprite) to u; restore copies them back to the same place and forgets them. */
void pointer_save(struct pointer_under *u, const struct surface *screen, struct rect r);
void pointer_restore(struct pointer_under *u, struct surface *screen);

/* Draws the sprite's pixels (not its transparent ones) with the hot spot
 * at (x, y), clipped to the screen. */
void pointer_paint(struct surface *screen, const struct pointer_sprite *s, int32_t x, int32_t y);

#endif
