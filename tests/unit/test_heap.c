/* Unit tests for kernel/memory/heap.c, on a static arena. */
#include <stdint.h>
#include <string.h>
#include "kernel/memory/heap.h"
#include "tests/unit/unit.h"

#define ARENA_BYTES (1024u * 1024)
#define INITIAL     (64u * 1024)
#define GROW_MIN    (16u * 1024)

static uint8_t arena[ARENA_BYTES] __attribute__((aligned(4096)));
static struct heap heap;
static struct heap *h = &heap;

/* Fake growth: the arena buffer is already there; just count and allow. */
static bool grow_allowed;
static uint32_t grow_calls, grow_total, last_grow;

static bool fake_grow(void *ctx, uint32_t bytes)
{
    (void)ctx;
    grow_calls++;
    if (!grow_allowed)
        return false;
    last_grow = bytes;
    grow_total += bytes;
    return true;
}

static void setup(uint32_t max, bool with_grow)
{
    memset(arena, 0xEE, sizeof(arena));
    grow_allowed = true;
    grow_calls = grow_total = last_grow = 0;
    CHECK(heap_init(h, (uintptr_t)arena, INITIAL, max, GROW_MIN, with_grow ? fake_grow : 0, 0));
}

static struct heap_stats stats(void)
{
    struct heap_stats s;
    heap_stats(h, &s);
    return s;
}

static bool heap_ok(void)
{
    uintptr_t bad;
    return heap_check(h, &bad) == HEAP_OK;
}

/* Exactly one free block covering the whole arena. */
static bool heap_empty(void)
{
    struct heap_stats s = stats();
    return heap_ok() && s.used_bytes == 0 && s.live_blocks == 0 &&
           s.free_bytes == s.arena_bytes && s.largest_free == s.arena_bytes - HEAP_HEADER;
}

static uint32_t *header_of(void *p)
{
    return (uint32_t *)((uint8_t *)p - HEAP_HEADER);     /* magic, size, prev_size, check */
}

static void test_init(void)
{
    setup(INITIAL, false);
    CHECK(heap_empty());
    CHECK_EQ(stats().arena_bytes, INITIAL);
    struct heap bad;
    CHECK(!heap_init(&bad, (uintptr_t)arena + 8, INITIAL, INITIAL, GROW_MIN, 0, 0));   /* base */
    CHECK(!heap_init(&bad, (uintptr_t)arena, 16, INITIAL, GROW_MIN, 0, 0));            /* too small */
    CHECK(!heap_init(&bad, (uintptr_t)arena, INITIAL + 8, INITIAL * 2, GROW_MIN, 0, 0));
    CHECK(!heap_init(&bad, (uintptr_t)arena, INITIAL, INITIAL - 16, GROW_MIN, 0, 0));  /* > max */
    CHECK(heap_alloc(&bad, 16) == 0);
    CHECK_EQ(heap_free(&bad, arena + 16), HEAP_ERR_NOT_HEAP);
    CHECK_EQ(heap_check(&bad, 0), HEAP_ERR_NOT_HEAP);
}

static void test_basic_alloc_free(void)
{
    setup(INITIAL, false);
    uint8_t *p = heap_alloc(h, 100);
    CHECK(p != 0);
    CHECK(p >= arena + HEAP_HEADER && p < arena + INITIAL);
    memset(p, 0x5C, 100);
    CHECK(heap_ok());
    struct heap_stats s = stats();
    CHECK_EQ(s.live_blocks, 1);
    CHECK_EQ(s.used_bytes, 128);                    /* 100 -> 112 + header */
    CHECK_EQ(s.used_bytes + s.free_bytes, s.arena_bytes);
    CHECK_EQ(heap_free(h, p), HEAP_OK);
    CHECK(heap_empty());
    CHECK_EQ(stats().allocations, 1);
    CHECK_EQ(stats().frees, 1);
}

static void test_alignment_all_sizes(void)
{
    setup(INITIAL, false);
    for (uint32_t size = 1; size <= 4096; size++) {
        uint8_t *p = heap_alloc(h, size);
        if (!p || ((uintptr_t)p & 15)) {
            CHECK(0 && "allocation missing or misaligned");
            return;
        }
        memset(p, (int)size, size);
        CHECK_EQ(header_of(p)[1] - HEAP_HEADER >= size, 1);   /* payload big enough */
        if (heap_free(h, p) != HEAP_OK) {
            CHECK(0 && "free failed");
            return;
        }
    }
    CHECK(heap_empty());
    CHECK_EQ(stats().allocations, 4096);
}

static void test_alloc_zero(void)
{
    setup(INITIAL, true);
    CHECK(heap_alloc(h, 0) == 0);
    CHECK(heap_calloc(h, 0, 16) == 0);
    CHECK(heap_calloc(h, 16, 0) == 0);
    CHECK_EQ(stats().failed_allocs, 0);             /* size 0 is not a failure */
    CHECK_EQ(grow_calls, 0);
    CHECK_EQ(heap_free(h, 0), HEAP_OK);             /* free(NULL) is a no-op */
    CHECK_EQ(stats().frees, 0);
    CHECK(heap_empty());
}

static void test_many_small(void)
{
    static uint8_t *ptrs[1000];
    setup(INITIAL, false);
    for (uint32_t i = 0; i < 1000; i++) {
        ptrs[i] = heap_alloc(h, 24);                /* 48-byte blocks: 48,000 bytes */
        CHECK(ptrs[i] != 0);
        if (!ptrs[i])
            return;
        memset(ptrs[i], (int)(i & 0xFF), 24);
    }
    for (uint32_t i = 1; i < 1000; i++)
        CHECK_EQ(ptrs[i] - ptrs[i - 1], 48);        /* first fit on a fresh heap: packed */
    CHECK_EQ(stats().live_blocks, 1000);
    CHECK(heap_ok());
    for (uint32_t i = 0; i < 1000; i++) {
        for (uint32_t j = 0; j < 24; j++)
            if (ptrs[i][j] != (uint8_t)(i & 0xFF)) {
                CHECK(0 && "payload overwritten");
                return;
            }
        CHECK_EQ(heap_free(h, ptrs[i]), HEAP_OK);
    }
    CHECK(heap_empty());
}

static void test_whole_arena_without_growth(void)
{
    setup(INITIAL, false);
    void *p = heap_alloc(h, INITIAL - HEAP_HEADER);
    CHECK(p == arena + HEAP_HEADER);
    CHECK_EQ(stats().free_bytes, 0);
    CHECK(heap_alloc(h, 1) == 0);                   /* full, and no growth */
    CHECK(heap_alloc(h, INITIAL) == 0);
    CHECK_EQ(stats().failed_allocs, 2);
    CHECK(heap_ok());
    CHECK_EQ(heap_free(h, p), HEAP_OK);
    CHECK(heap_empty());
}

static void test_larger_than_arena_grows(void)
{
    setup(ARENA_BYTES, true);
    uint8_t *p = heap_alloc(h, 100 * 1024);
    CHECK(p != 0);
    CHECK(grow_calls >= 1);
    CHECK_EQ(grow_total % 4096, 0);                 /* whole pages */
    CHECK(last_grow >= GROW_MIN);
    CHECK_EQ(stats().arena_bytes, INITIAL + grow_total);
    CHECK_EQ(stats().grow_count, grow_calls);
    memset(p, 0x77, 100 * 1024);                    /* the whole payload is usable */
    CHECK(heap_ok());
    CHECK_EQ(heap_free(h, p), HEAP_OK);
    CHECK(heap_empty());                            /* grown space merged into one block */
    CHECK_EQ(stats().arena_bytes, INITIAL + grow_total);   /* never shrinks */
}

static void test_small_growth_uses_minimum_step(void)
{
    setup(ARENA_BYTES, true);
    void *fill = heap_alloc(h, INITIAL - HEAP_HEADER);
    CHECK(fill != 0);
    void *p = heap_alloc(h, 16);                    /* needs 32 bytes more */
    CHECK(p != 0);
    CHECK_EQ(grow_calls, 1);
    CHECK_EQ(last_grow, GROW_MIN);                  /* at least 16 KiB per step */
    CHECK_EQ(stats().arena_bytes, INITIAL + GROW_MIN);
    CHECK(heap_ok());
}

static void test_larger_than_max(void)
{
    setup(128u * 1024, true);
    CHECK(heap_alloc(h, 128u * 1024) == 0);         /* header does not fit under the cap */
    CHECK(heap_alloc(h, 0xFFFFFFFFu) == 0);
    CHECK(heap_alloc(h, 0xFFFFFFF0u) == 0);
    CHECK_EQ(grow_calls, 0);                        /* refused without growing */
    CHECK_EQ(stats().failed_allocs, 3);
    CHECK(heap_empty());
}

static void test_growth_refused(void)
{
    setup(ARENA_BYTES, true);
    grow_allowed = false;
    CHECK(heap_alloc(h, INITIAL) == 0);
    CHECK_EQ(grow_calls, 1);
    CHECK_EQ(stats().arena_bytes, INITIAL);         /* unchanged */
    CHECK(heap_empty());
    grow_allowed = true;
    CHECK(heap_alloc(h, INITIAL) != 0);             /* works once growth is possible */
    CHECK(heap_ok());
}

static void test_cap_reached(void)
{
    setup(96u * 1024, true);
    void *a = heap_alloc(h, 60u * 1024);
    CHECK(a != 0);
    void *b = heap_alloc(h, 30u * 1024);            /* grows by 28 KiB: 4 KiB below the cap */
    CHECK(b != 0);
    CHECK_EQ(stats().arena_bytes, INITIAL + 28u * 1024);
    uint32_t calls = grow_calls;
    CHECK(heap_alloc(h, 8u * 1024) == 0);           /* 4 KiB of room is not enough */
    CHECK_EQ(grow_calls, calls);                    /* ...so growth is not even tried */
    CHECK_EQ(stats().arena_bytes, INITIAL + 28u * 1024);
    CHECK(heap_alloc(h, 3000) != 0);                /* the last 4 KiB (below the 16 KiB step) */
    CHECK_EQ(stats().arena_bytes, 96u * 1024);      /* exactly the cap, never beyond */
    CHECK(heap_alloc(h, 4000) == 0);
    CHECK(heap_ok());
}

static void test_exhaustion_repeated(void)
{
    static void *ptrs[128];
    uint32_t n = 0;
    setup(256u * 1024, true);
    while (n < 128) {
        void *p = heap_alloc(h, 4000);
        if (!p)
            break;
        ptrs[n++] = p;
    }
    CHECK(n > 50 && n < 128);
    CHECK(stats().arena_bytes <= 256u * 1024);
    for (int i = 0; i < 100; i++)
        CHECK(heap_alloc(h, 4000) == 0);
    CHECK(heap_ok());
    CHECK_EQ(stats().arena_bytes, 256u * 1024);     /* grew to the cap, not beyond */
    void *small = heap_alloc(h, 16);                /* small ones may still fit */
    for (uint32_t i = 0; i < n; i++)
        CHECK_EQ(heap_free(h, ptrs[i]), HEAP_OK);
    CHECK_EQ(heap_free(h, small), HEAP_OK);         /* NULL is fine too */
    CHECK(heap_empty());
}

static void test_fragmentation_then_merge(void)
{
    static void *ptrs[64];
    setup(INITIAL, false);
    for (int i = 0; i < 64; i++)
        ptrs[i] = heap_alloc(h, 240);               /* 256-byte blocks */
    for (int i = 0; i < 64; i += 2)
        CHECK_EQ(heap_free(h, ptrs[i]), HEAP_OK);
    CHECK(heap_ok());                               /* no adjacent free blocks */
    CHECK_EQ(stats().live_blocks, 32);
    CHECK(heap_alloc(h, 256) != ptrs[0]);           /* holes are 240 bytes: too small */
    for (int i = 1; i < 64; i += 2)
        CHECK_EQ(heap_free(h, ptrs[i]), HEAP_OK);
    uintptr_t bad;
    CHECK_EQ(heap_check(h, &bad), HEAP_OK);
    CHECK_EQ(stats().live_blocks, 1);               /* the 256-byte one above */
}

/* Four blocks A B C D; D keeps C away from the arena's tail free block. */
static uint8_t *A, *B, *C, *D;
static void four_blocks(void)
{
    setup(INITIAL, false);
    A = heap_alloc(h, 48);
    B = heap_alloc(h, 48);
    C = heap_alloc(h, 48);
    D = heap_alloc(h, 48);
}

static void test_merge_with_next(void)
{
    four_blocks();
    CHECK_EQ(heap_free(h, C), HEAP_OK);
    CHECK_EQ(heap_free(h, B), HEAP_OK);             /* B absorbs C */
    CHECK(heap_ok());
    CHECK_EQ(header_of(B)[0], HEAP_MAGIC_FREE);
    CHECK_EQ(header_of(B)[1], 128);
    CHECK_EQ(header_of(D)[2], 128);                 /* D's prev_size follows */
    CHECK_EQ(header_of(C)[0], 0);                   /* absorbed header cleared */
}

static void test_merge_with_previous(void)
{
    four_blocks();
    CHECK_EQ(heap_free(h, B), HEAP_OK);
    CHECK_EQ(heap_free(h, C), HEAP_OK);             /* B absorbs C */
    CHECK(heap_ok());
    CHECK_EQ(header_of(B)[1], 128);
    CHECK_EQ(header_of(D)[2], 128);
    CHECK_EQ(header_of(B)[2], 64);                  /* B keeps A as its previous block */
}

static void test_merge_both_sides(void)
{
    four_blocks();
    CHECK_EQ(heap_free(h, A), HEAP_OK);
    CHECK_EQ(heap_free(h, C), HEAP_OK);
    CHECK_EQ(heap_free(h, B), HEAP_OK);             /* A absorbs B and C */
    CHECK(heap_ok());
    CHECK_EQ(header_of(A)[1], 192);
    CHECK_EQ(header_of(A)[2], 0);
    CHECK_EQ(header_of(D)[2], 192);
    CHECK_EQ(stats().live_blocks, 1);
    CHECK_EQ(heap_free(h, D), HEAP_OK);             /* D merges with both neighbours too */
    CHECK(heap_empty());
}

static void test_split_threshold(void)
{
    setup(INITIAL, false);
    void *a = heap_alloc(h, 64);                    /* 80-byte block */
    void *guard = heap_alloc(h, 16);
    CHECK(guard != 0);
    heap_free(h, a);
    void *p = heap_alloc(h, 48);                    /* needs 64: 16 left, no split */
    CHECK(p == a);
    CHECK_EQ(header_of(p)[1], 80);
    CHECK(heap_ok());
    heap_free(h, p);
    p = heap_alloc(h, 32);                          /* needs 48: 32 left, split */
    CHECK(p == a);
    CHECK_EQ(header_of(p)[1], 48);
    CHECK_EQ(header_of((uint8_t *)p + 48)[0], HEAP_MAGIC_FREE);
    CHECK_EQ(header_of((uint8_t *)p + 48)[1], 32);
    CHECK_EQ(header_of(guard)[2], 32);
    CHECK(heap_ok());
}

static void test_double_free(void)
{
    four_blocks();
    CHECK_EQ(heap_free(h, B), HEAP_OK);
    CHECK_EQ(heap_free(h, B), HEAP_ERR_DOUBLE_FREE);
    CHECK_EQ(heap_free(h, C), HEAP_OK);             /* B and C merge */
    CHECK_EQ(heap_free(h, C), HEAP_ERR_DOUBLE_FREE);   /* stale pointer into merged block */
    CHECK_EQ(heap_free(h, B), HEAP_ERR_DOUBLE_FREE);
    CHECK_EQ(stats().frees, 2);
    CHECK(heap_ok());
}

static void test_invalid_pointers(void)
{
    uint32_t local;
    four_blocks();
    struct heap_stats before = stats();
    CHECK_EQ(heap_free(h, &local), HEAP_ERR_NOT_HEAP);             /* outside */
    CHECK_EQ(heap_free(h, arena), HEAP_ERR_NOT_HEAP);              /* the first header */
    CHECK_EQ(heap_free(h, arena + INITIAL), HEAP_ERR_NOT_HEAP);    /* just past the end */
    CHECK_EQ(heap_free(h, A + 16), HEAP_ERR_NOT_HEAP);             /* inside, not a block start */
    CHECK_EQ(heap_free(h, B - 16), HEAP_ERR_NOT_HEAP);             /* B's header */
    CHECK_EQ(heap_free(h, A + 4), HEAP_ERR_MISALIGNED);
    CHECK_EQ(heap_free(h, D + 1), HEAP_ERR_MISALIGNED);
    struct heap_stats after = stats();
    CHECK_EQ(after.live_blocks, before.live_blocks);               /* nothing changed */
    CHECK_EQ(after.frees, 0);
    CHECK(heap_ok());
}

static void test_corrupt_headers(void)
{
    uintptr_t bad = 0;
    uint32_t saved[4];
    four_blocks();
    uint32_t *hb = header_of(B);
    memcpy(saved, hb, sizeof(saved));

    hb[0] = 0x12345678u;                            /* overwritten magic */
    CHECK_EQ(heap_check(h, &bad), HEAP_ERR_CORRUPT);
    CHECK_EQ(bad, (uintptr_t)hb);
    CHECK_EQ(heap_free(h, B), HEAP_ERR_CORRUPT);
    memcpy(hb, saved, sizeof(saved));

    hb[3] ^= 1;                                     /* overwritten check value */
    CHECK_EQ(heap_check(h, &bad), HEAP_ERR_CORRUPT);
    CHECK_EQ(heap_free(h, A), HEAP_ERR_CORRUPT);    /* any free refuses a corrupt heap */
    memcpy(hb, saved, sizeof(saved));

    hb[2] = 128;                                    /* wrong prev_size, consistent check */
    hb[3] = hb[0] ^ hb[1] ^ hb[2] ^ HEAP_CHECK_SALT;
    CHECK_EQ(heap_check(h, &bad), HEAP_ERR_CORRUPT);
    CHECK_EQ(bad, (uintptr_t)hb);
    memcpy(hb, saved, sizeof(saved));

    hb[1] = 4096;                                   /* size overrunning the next block */
    hb[3] = hb[0] ^ hb[1] ^ hb[2] ^ HEAP_CHECK_SALT;
    CHECK_EQ(heap_check(h, &bad), HEAP_ERR_CORRUPT);
    memcpy(hb, saved, sizeof(saved));

    CHECK(heap_ok());
    CHECK_EQ(stats().live_blocks, 4);               /* refused frees changed nothing */
    CHECK_EQ(heap_free(h, B), HEAP_OK);
}

static void test_payload_overrun_detected(void)
{
    four_blocks();
    memset(A, 0x41, 64 + 4);                        /* overrun into B's header */
    CHECK_EQ(heap_check(h, 0), HEAP_ERR_CORRUPT);
    CHECK_EQ(heap_free(h, A), HEAP_ERR_CORRUPT);
    CHECK(heap_alloc(h, 16) == 0);                  /* alloc refuses a corrupt heap too */
}

static void test_calloc(void)
{
    setup(INITIAL, false);
    uint8_t *junk = heap_alloc(h, 256);
    memset(junk, 0xFF, 256);
    heap_free(h, junk);
    uint8_t *p = heap_calloc(h, 3, 70);             /* 210 bytes, reuses the dirty block */
    CHECK(p == junk);
    int nonzero = 0;
    for (int i = 0; i < 210; i++)
        nonzero |= p[i];
    CHECK_EQ(nonzero, 0);
    CHECK(heap_calloc(h, 0x10000u, 0x10000u) == 0); /* 2^32: overflow */
    CHECK(heap_calloc(h, 0xFFFFFFFFu, 2) == 0);
    CHECK(heap_calloc(h, 2, 0x80000000u) == 0);
    CHECK_EQ(stats().failed_allocs, 3);
    CHECK(heap_ok());
}

static void test_stats_consistent(void)
{
    static void *ptrs[40];
    setup(ARENA_BYTES, true);
    for (int i = 0; i < 40; i++) {
        ptrs[i] = heap_alloc(h, (uint32_t)(i * 997 % 5000 + 1));
        struct heap_stats s = stats();
        CHECK_EQ(s.used_bytes + s.free_bytes, s.arena_bytes);
        CHECK_EQ(s.live_blocks, (uint32_t)i + 1);
        CHECK_EQ(s.allocations, (uint32_t)i + 1);
    }
    for (int i = 39; i >= 0; i -= 3)
        heap_free(h, ptrs[i]);
    struct heap_stats s = stats();
    CHECK_EQ(s.used_bytes + s.free_bytes, s.arena_bytes);
    CHECK_EQ(s.frees, 14);
    CHECK_EQ(s.live_blocks, 26);
    CHECK(s.largest_free <= s.free_bytes);
}

/* 10,000 pseudo-random steps; payloads carry a tag, checked before free. */
#define SLOTS 256
static struct { uint8_t *p; uint32_t size; uint8_t tag; } slot[SLOTS];
static uint32_t rng = 12345;
static uint32_t next_rand(void)
{
    rng = rng * 1103515245u + 12345u;
    return rng >> 8;
}

static bool payload_intact(int i)
{
    for (uint32_t j = 0; j < slot[i].size; j++)
        if (slot[i].p[j] != (uint8_t)(slot[i].tag + j))
            return false;
    return true;
}

static void test_random_alloc_free(void)
{
    setup(ARENA_BYTES, true);
    memset(slot, 0, sizeof(slot));
    uint32_t failures = 0, nulls = 0;
    for (int step = 0; step < 10000; step++) {
        int i = (int)(next_rand() % SLOTS);
        if (slot[i].p) {
            if (!payload_intact(i))
                failures++;
            if (heap_free(h, slot[i].p) != HEAP_OK)
                failures++;
            slot[i].p = 0;
        } else {
            uint32_t r = next_rand();
            uint32_t size = (r & 7) == 0 ? r % 20000 + 1 : r % 600 + 1;
            slot[i].p = heap_alloc(h, size);
            if (!slot[i].p) {
                nulls++;
                continue;
            }
            slot[i].size = size;
            slot[i].tag = (uint8_t)step;
            for (uint32_t j = 0; j < size; j++)
                slot[i].p[j] = (uint8_t)(slot[i].tag + j);
        }
        if (step % 250 == 0) {
            struct heap_stats s = stats();
            if (!heap_ok() || s.used_bytes + s.free_bytes != s.arena_bytes)
                failures++;
            for (int a = 0; a < SLOTS; a++)             /* no two live payloads overlap */
                for (int b = a + 1; b < SLOTS && slot[a].p; b++)
                    if (slot[b].p && slot[a].p < slot[b].p + slot[b].size &&
                        slot[b].p < slot[a].p + slot[a].size)
                        failures++;
        }
    }
    CHECK_EQ(failures, 0);
    CHECK_EQ(nulls, 0);                             /* 1 MiB is enough for this mix */
    CHECK(stats().arena_bytes <= ARENA_BYTES);
    for (int i = 0; i < SLOTS; i++)
        if (slot[i].p) {
            CHECK(payload_intact(i));
            CHECK_EQ(heap_free(h, slot[i].p), HEAP_OK);
        }
    CHECK(heap_empty());
}

static void test_error_strings(void)
{
    for (int e = HEAP_OK; e <= HEAP_ERR_CORRUPT; e++)
        CHECK(strcmp(heap_error_str((enum heap_error)e), "unknown heap error") != 0);
    CHECK(strcmp(heap_error_str(HEAP_ERR_DOUBLE_FREE), "double free") == 0);
}

int main(void)
{
    printf("heap unit tests\n");
    RUN(test_init);
    RUN(test_basic_alloc_free);
    RUN(test_alignment_all_sizes);
    RUN(test_alloc_zero);
    RUN(test_many_small);
    RUN(test_whole_arena_without_growth);
    RUN(test_larger_than_arena_grows);
    RUN(test_small_growth_uses_minimum_step);
    RUN(test_larger_than_max);
    RUN(test_growth_refused);
    RUN(test_cap_reached);
    RUN(test_exhaustion_repeated);
    RUN(test_fragmentation_then_merge);
    RUN(test_merge_with_next);
    RUN(test_merge_with_previous);
    RUN(test_merge_both_sides);
    RUN(test_split_threshold);
    RUN(test_double_free);
    RUN(test_invalid_pointers);
    RUN(test_corrupt_headers);
    RUN(test_payload_overrun_detected);
    RUN(test_calloc);
    RUN(test_stats_consistent);
    RUN(test_random_alloc_free);
    RUN(test_error_strings);
    return unit_report("heap");
}
