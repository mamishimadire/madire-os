#ifndef MADIRE_DISPLAY_COMPOSITOR_H
#define MADIRE_DISPLAY_COMPOSITOR_H

/*
 * The compositor (1.3 stage 3; docs/plans/1.3.md, C1). From System ready on
 * a desktop boot it owns the back buffer, and its present step is the only
 * code that copies to the framebuffer (desktop.c gives it one that keeps
 * the pointer on top through pointer_hide_over / pointer_unhide). Pure: no
 * kernel dependency; host unit tests in tests/unit/test_compositor.c.
 *
 * Damage is kept in ui_rect.h's list (32 rectangles; when it is full, the
 * list becomes their bounding rectangle). comp_compose(), called once per
 * pass of the idle loop, takes the list, empties it and, for each damaged
 * rectangle in turn, has every layer draw that rectangle on the back
 * buffer, from the bottom up (in 1.3 stage 3: the background, then the
 * taskbar), each clipped to it, and then presents it.
 */

#include <stdbool.h>
#include <stdint.h>
#include "kernel/display/surface_core.h"
#include "kernel/display/ui_rect.h"

#define COMP_LAYERS 8

struct comp_layer {
    void (*draw)(void *ctx, struct surface *back, struct rect clip);
    void *ctx;
};

struct compositor {
    struct surface *back;
    struct ui_damage damage;
    struct comp_layer layers[COMP_LAYERS];
    uint32_t n_layers;
    void (*present)(void *ctx, struct rect r);
    void *present_ctx;
    uint32_t passes;            /* comp_compose calls that composed something */
    uint32_t rects;             /* rectangles composed in all */
};

/* A compositor for `back` (the screen's size), with no layer and no damage. */
void comp_init(struct compositor *c, struct surface *back, void (*present)(void *ctx, struct rect r),
               void *ctx);

/* Adds a layer above the others. False if there are COMP_LAYERS already. */
bool comp_add_layer(struct compositor *c, void (*draw)(void *ctx, struct surface *back, struct rect clip),
                    void *ctx);

/* Marks r damaged (the list's rules). */
void comp_damage(struct compositor *c, struct rect r);

/* True if something is damaged. */
bool comp_pending(const struct compositor *c);

/* Composes and presents every damaged rectangle (above); returns how many. */
uint32_t comp_compose(struct compositor *c);

#endif
