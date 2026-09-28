/* Kernel heap core (see heap.h). */
#include "kernel/memory/heap.h"

#define NO_BLOCK 0xFFFFFFFFu

struct block {
    uint32_t magic;
    uint32_t size;
    uint32_t prev_size;
    uint32_t check;
};

static struct block *at(const struct heap *h, uint32_t off)
{
    return (struct block *)(h->base + off);
}

static uint32_t check_of(uint32_t magic, uint32_t size, uint32_t prev_size)
{
    return magic ^ size ^ prev_size ^ HEAP_CHECK_SALT;
}

static void set_header(struct block *b, uint32_t magic, uint32_t size, uint32_t prev_size)
{
    b->magic = magic;
    b->size = size;
    b->prev_size = prev_size;
    b->check = check_of(magic, size, prev_size);
}

static void clear_header(struct block *b)
{
    b->magic = b->size = b->prev_size = b->check = 0;
}

/* The block at off is well-formed and fits in the arena. */
static bool header_ok(const struct heap *h, uint32_t off, uint32_t prev_size)
{
    if (h->arena - off < HEAP_MIN_BLOCK)
        return false;
    const struct block *b = at(h, off);
    return (b->magic == HEAP_MAGIC_USED || b->magic == HEAP_MAGIC_FREE)
        && b->check == check_of(b->magic, b->size, b->prev_size)
        && b->size >= HEAP_MIN_BLOCK
        && (b->size & (HEAP_ALIGN - 1)) == 0
        && b->size <= h->arena - off
        && b->prev_size == prev_size;
}

/*
 * Validates every block. If target != NO_BLOCK, *hit = offset of the block
 * containing arena offset target. *bad = offset of the first bad block.
 */
static enum heap_error walk(const struct heap *h, uint32_t target, uint32_t *hit, uint32_t *bad)
{
    uint32_t off = 0, prev_size = 0;
    bool prev_free = false;
    while (off < h->arena) {
        if (!header_ok(h, off, prev_size)) {
            *bad = off;
            return HEAP_ERR_CORRUPT;
        }
        const struct block *b = at(h, off);
        bool is_free = b->magic == HEAP_MAGIC_FREE;
        if (is_free && prev_free) {             /* should have been merged */
            *bad = off;
            return HEAP_ERR_CORRUPT;
        }
        if (target != NO_BLOCK && target >= off && target - off < b->size)
            *hit = off;
        prev_size = b->size;
        prev_free = is_free;
        off += b->size;
    }
    return HEAP_OK;
}

bool heap_init(struct heap *h, uintptr_t base, uint32_t initial, uint32_t max,
               uint32_t grow_min, heap_grow_fn grow, void *ctx)
{
    h->ready = false;
    h->base = base;
    h->arena = 0;
    h->max = max;
    h->grow_min = grow_min;
    h->grow = grow;
    h->ctx = ctx;
    h->allocations = h->frees = h->grow_count = h->failed_allocs = 0;
    if ((base & (HEAP_ALIGN - 1)) || (initial & (HEAP_ALIGN - 1)) ||
        initial < HEAP_MIN_BLOCK || initial > max || base + initial < base)
        return false;
    h->arena = initial;
    set_header(at(h, 0), HEAP_MAGIC_FREE, initial, 0);
    h->ready = true;
    return true;
}

/* First free block of at least need bytes; NO_BLOCK if none (or a bad
 * header stops the search). *last / *last_free describe the final block. */
static uint32_t find_fit(const struct heap *h, uint32_t need, uint32_t *last, bool *corrupt)
{
    uint32_t off = 0, prev_size = 0;
    *last = NO_BLOCK;
    *corrupt = false;
    while (off < h->arena) {
        if (!header_ok(h, off, prev_size)) {
            *corrupt = true;
            return NO_BLOCK;
        }
        const struct block *b = at(h, off);
        if (b->magic == HEAP_MAGIC_FREE && b->size >= need)
            return off;
        *last = off;
        prev_size = b->size;
        off += b->size;
    }
    return NO_BLOCK;
}

static uint32_t page_round_up(uint32_t n)
{
    return (n + (HEAP_GROW_PAGE - 1)) & ~(HEAP_GROW_PAGE - 1);
}

/* Grows the arena so that a block of need bytes fits at its end. */
static bool grow_for(struct heap *h, uint32_t need, uint32_t last)
{
    struct block *lb = at(h, last);
    uint32_t tail_free = lb->magic == HEAP_MAGIC_FREE ? lb->size : 0;
    uint32_t extra = need - tail_free;           /* need > tail_free: it didn't fit */
    uint32_t room = h->max - h->arena;
    if (extra > room || !h->grow)
        return false;
    uint32_t step = page_round_up(extra);
    if (step < h->grow_min)
        step = page_round_up(h->grow_min);
    if (step > room)
        step = room & ~(HEAP_GROW_PAGE - 1);
    if (step < extra)
        return false;
    if (!h->grow(h->ctx, step))
        return false;

    if (tail_free)
        set_header(lb, HEAP_MAGIC_FREE, lb->size + step, lb->prev_size);
    else
        set_header(at(h, h->arena), HEAP_MAGIC_FREE, step, lb->size);
    h->arena += step;
    h->grow_count++;
    return true;
}

/* Marks the free block at off used, splitting off a big enough remainder. */
static void *take(struct heap *h, uint32_t off, uint32_t need)
{
    struct block *b = at(h, off);
    uint32_t size = b->size, rest = size - need;
    if (rest >= HEAP_MIN_BLOCK) {
        set_header(b, HEAP_MAGIC_USED, need, b->prev_size);
        set_header(at(h, off + need), HEAP_MAGIC_FREE, rest, need);
        if (off + size < h->arena) {
            struct block *next = at(h, off + size);
            set_header(next, next->magic, next->size, rest);
        }
    } else {
        set_header(b, HEAP_MAGIC_USED, size, b->prev_size);
    }
    h->allocations++;
    return (void *)(h->base + off + HEAP_HEADER);
}

void *heap_alloc(struct heap *h, uint32_t size)
{
    if (size == 0)
        return 0;
    if (!h->ready || size > h->max || size > 0xFFFFFFFFu - HEAP_HEADER - HEAP_ALIGN) {
        h->failed_allocs++;
        return 0;
    }
    uint32_t need = ((size + (HEAP_ALIGN - 1)) & ~(HEAP_ALIGN - 1)) + HEAP_HEADER;
    if (need < HEAP_MIN_BLOCK)
        need = HEAP_MIN_BLOCK;

    for (int attempt = 0; attempt < 2; attempt++) {
        uint32_t last;
        bool corrupt;
        uint32_t off = find_fit(h, need, &last, &corrupt);
        if (off != NO_BLOCK)
            return take(h, off, need);
        if (corrupt || last == NO_BLOCK || !grow_for(h, need, last))
            break;
    }
    h->failed_allocs++;
    return 0;
}

void *heap_calloc(struct heap *h, uint32_t n, uint32_t size)
{
    if (n != 0 && size > 0xFFFFFFFFu / n) {         /* 32-bit division only */
        h->failed_allocs++;
        return 0;
    }
    uint32_t total = n * size;
    uint32_t *p = heap_alloc(h, total);
    if (p) {
        uint32_t words = ((total + (HEAP_ALIGN - 1)) & ~(HEAP_ALIGN - 1)) / 4;
        for (uint32_t i = 0; i < words; i++)
            p[i] = 0;
    }
    return p;
}

enum heap_error heap_free(struct heap *h, void *p)
{
    if (!p)
        return HEAP_OK;
    uintptr_t a = (uintptr_t)p;
    if (!h->ready || a < h->base + HEAP_HEADER || a - h->base >= h->arena)
        return HEAP_ERR_NOT_HEAP;
    if ((a - h->base) & (HEAP_ALIGN - 1))
        return HEAP_ERR_MISALIGNED;

    uint32_t target = (uint32_t)(a - h->base), hit = NO_BLOCK, bad;
    enum heap_error err = walk(h, target, &hit, &bad);
    if (err != HEAP_OK)
        return err;
    struct block *b = at(h, hit);
    if (b->magic == HEAP_MAGIC_FREE)            /* freed already (maybe merged since) */
        return HEAP_ERR_DOUBLE_FREE;
    if (target != hit + HEAP_HEADER)            /* inside a used block */
        return HEAP_ERR_NOT_HEAP;

    uint32_t off = hit, size = b->size, prev_size = b->prev_size;
    if (off + size < h->arena) {
        struct block *next = at(h, off + size);
        if (next->magic == HEAP_MAGIC_FREE) {
            size += next->size;
            clear_header(next);
        }
    }
    if (prev_size && at(h, off - prev_size)->magic == HEAP_MAGIC_FREE) {
        clear_header(b);
        off -= prev_size;
        size += prev_size;
        prev_size = at(h, off)->prev_size;
    }
    set_header(at(h, off), HEAP_MAGIC_FREE, size, prev_size);
    if (off + size < h->arena) {
        struct block *next = at(h, off + size);
        set_header(next, next->magic, next->size, size);
    }
    h->frees++;
    return HEAP_OK;
}

enum heap_error heap_check(const struct heap *h, uintptr_t *bad_block)
{
    if (!h->ready)
        return HEAP_ERR_NOT_HEAP;
    uint32_t hit, bad = 0;
    enum heap_error err = walk(h, NO_BLOCK, &hit, &bad);
    if (err != HEAP_OK && bad_block)
        *bad_block = h->base + bad;
    return err;
}

void heap_stats(const struct heap *h, struct heap_stats *out)
{
    out->arena_bytes = h->arena;
    out->used_bytes = out->free_bytes = out->largest_free = out->live_blocks = 0;
    out->allocations = h->allocations;
    out->frees = h->frees;
    out->grow_count = h->grow_count;
    out->failed_allocs = h->failed_allocs;
    uint32_t off = 0, prev_size = 0;
    while (h->ready && off < h->arena && header_ok(h, off, prev_size)) {
        const struct block *b = at(h, off);
        if (b->magic == HEAP_MAGIC_FREE) {
            out->free_bytes += b->size;
            if (b->size - HEAP_HEADER > out->largest_free)
                out->largest_free = b->size - HEAP_HEADER;
        } else {
            out->used_bytes += b->size;
            out->live_blocks++;
        }
        prev_size = b->size;
        off += b->size;
    }
}

const char *heap_error_str(enum heap_error error)
{
    switch (error) {
    case HEAP_OK:              return "ok";
    case HEAP_ERR_NOT_HEAP:    return "pointer not from the heap";
    case HEAP_ERR_MISALIGNED:  return "pointer not 16-byte aligned";
    case HEAP_ERR_DOUBLE_FREE: return "double free";
    case HEAP_ERR_CORRUPT:     return "heap corrupted";
    }
    return "unknown heap error";
}
