/*
 * The compositor (1.3 stage 3); see compositor.h.
 */
#include "kernel/display/compositor.h"

void comp_init(struct compositor *c, struct surface *back, void (*present)(void *ctx, struct rect r),
               void *ctx)
{
    c->back = back;
    ui_damage_init(&c->damage, back->width, back->height);
    c->n_layers = 0;
    c->present = present;
    c->present_ctx = ctx;
    c->passes = 0;
    c->rects = 0;
}

bool comp_add_layer(struct compositor *c, void (*draw)(void *ctx, struct surface *back, struct rect clip),
                    void *ctx)
{
    if (c->n_layers >= COMP_LAYERS)
        return false;
    c->layers[c->n_layers++] = (struct comp_layer){ draw, ctx };
    return true;
}

void comp_damage(struct compositor *c, struct rect r)
{
    ui_damage_add(&c->damage, r);
}

bool comp_pending(const struct compositor *c)
{
    return c->damage.n > 0;
}

uint32_t comp_compose(struct compositor *c)
{
    /* Taken first, so damage added while composing (none in stage 3)
     * waits for the next pass. */
    struct ui_damage d = c->damage;
    ui_damage_clear(&c->damage);
    for (uint32_t i = 0; i < d.n; i++) {
        for (uint32_t k = 0; k < c->n_layers; k++)
            c->layers[k].draw(c->layers[k].ctx, c->back, d.r[i]);
        c->present(c->present_ctx, d.r[i]);
    }
    if (d.n) {
        c->passes++;
        c->rects += d.n;
    }
    return d.n;
}
