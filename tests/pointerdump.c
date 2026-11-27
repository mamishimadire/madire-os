/* The v1.5 pointer set as the kernel makes it (1.5 stage 14b; docs/plans/
 * 1.5.md, notes (11), (12)(b), (14), (15)): kernel/display/pointer_core.c
 * itself, built as a host tool. For every kind at every scale step (4, 5,
 * 6, 8) and, for the busy pointer, every frame, it prints
 *   KIND <name> scale <s> frame <f> size <w>x<h> hot <hx>,<hy>
 * and then the sprite's rows, one character per pixel ('.' none, 'o' edge,
 * 'w' fill, 'a' accent, 'd' danger), and the arrow once more as
 * pointer_sprite_make makes it (the path every boot used before 1.5 stage
 * 14b):
 *   ARROW scale <s> size <w>x<h>
 * Part static compares every block with tests/pointer_ref.py's rendering
 * of the same rules. */
#include <stdint.h>
#include <stdio.h>
#include "kernel/display/pointer_core.h"

static const char pd_chars[5] = { '.', 'o', 'w', 'a', 'd' };

static void pd_rows(const struct pointer_sprite *s)
{
    for (uint32_t y = 0; y < s->h; y++) {
        for (uint32_t x = 0; x < s->w; x++) {
            uint8_t px = s->px[y * s->w + x];
            putchar(px < 5 ? pd_chars[px] : '?');
        }
        putchar('\n');
    }
}

int main(void)
{
    static const uint32_t scales[4] = { 4, 5, 6, 8 };
    static struct pointer_sprite s;
    int bad = 0;
    for (int k = 0; k < (int)POINTER_KINDS; k++)
        for (int i = 0; i < 4; i++) {
            uint32_t frames = k == POINTER_BUSY ? POINTER_BUSY_FRAMES : 1u;
            for (uint32_t f = 0; f < frames; f++) {
                int32_t hx = 0, hy = 0;
                if (!pointer_sprite_kind(&s, (enum pointer_kind)k, scales[i], f)) {
                    printf("FAILED kind %d scale %u\n", k, scales[i]);
                    bad = 1;
                    continue;
                }
                pointer_hot((enum pointer_kind)k, scales[i], &hx, &hy);
                printf("KIND %s scale %u frame %u size %ux%u hot %d,%d\n", pointer_defs[k].name, scales[i], f, s.w,
                       s.h, hx, hy);
                pd_rows(&s);
            }
        }
    for (int i = 0; i < 4; i++) {
        if (!pointer_sprite_make(&s, scales[i])) {
            printf("FAILED arrow scale %u\n", scales[i]);
            bad = 1;
            continue;
        }
        printf("ARROW scale %u size %ux%u\n", scales[i], s.w, s.h);
        pd_rows(&s);
    }
    return bad;
}
