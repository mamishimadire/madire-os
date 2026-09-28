#ifndef MADIRE_MEMORY_HEAP_H
#define MADIRE_MEMORY_HEAP_H

/*
 * Kernel heap core: first-fit allocator with boundary tags over one
 * contiguous arena that can grow at its end.
 *
 * Pure module: the arena is plain memory given by the caller, and growth
 * goes through a callback (the kernel maps new pages there, kmalloc.c; the
 * unit tests just widen a static buffer, tests/unit/test_heap.c).
 *
 * Block format (every size is a multiple of 16):
 *   [header 16 B: magic | size | prev_size | check] [payload >= 16 B]
 *   size      = the whole block, header included
 *   prev_size = size of the block just before it in memory (0 for the first)
 *   check     = magic ^ size ^ prev_size ^ HEAP_CHECK_SALT
 *
 * Guarantees: payloads are 16-byte aligned; heap_alloc(0) returns NULL;
 * running out of space returns NULL (never an error state); the arena never
 * shrinks. heap_free validates the whole heap first, so a corrupt heap is
 * reported instead of being made worse.
 *
 * Not reentrant and not interrupt-safe: the caller serialises all calls.
 */

#include <stdbool.h>
#include <stdint.h>

#define HEAP_ALIGN        16u
#define HEAP_HEADER       16u
#define HEAP_MIN_BLOCK    32u               /* header + 16 bytes of payload */
#define HEAP_MAGIC_USED   0xA110C8EDu
#define HEAP_MAGIC_FREE   0xF4EEB10Cu
#define HEAP_CHECK_SALT   0x5A5A5A5Au
#define HEAP_GROW_PAGE    4096u             /* growth is whole pages */

/* Extends the arena end by bytes (a multiple of HEAP_GROW_PAGE). All or
 * nothing: false = the arena is unchanged. */
typedef bool (*heap_grow_fn)(void *ctx, uint32_t bytes);

struct heap {
    uintptr_t base;
    uint32_t arena;             /* current arena size in bytes */
    uint32_t max;               /* arena cap */
    uint32_t grow_min;          /* smallest growth step */
    heap_grow_fn grow;
    void *ctx;
    bool ready;
    uint32_t allocations, frees, grow_count, failed_allocs;
};

struct heap_stats {
    uint32_t arena_bytes;       /* used_bytes + free_bytes */
    uint32_t used_bytes;        /* used blocks, headers included */
    uint32_t free_bytes;        /* free blocks, headers included */
    uint32_t largest_free;      /* largest payload available without growing */
    uint32_t allocations;       /* successful heap_alloc calls */
    uint32_t live_blocks;       /* used blocks now */
    uint32_t frees;             /* successful heap_free calls (not NULL) */
    uint32_t grow_count;        /* successful growth steps */
    uint32_t failed_allocs;     /* heap_alloc calls that returned NULL (size > 0) */
};

enum heap_error {
    HEAP_OK,
    HEAP_ERR_NOT_HEAP,          /* outside the arena, or not the start of a block */
    HEAP_ERR_MISALIGNED,        /* not 16-byte aligned */
    HEAP_ERR_DOUBLE_FREE,       /* the block (or the memory it pointed to) is already free */
    HEAP_ERR_CORRUPT,           /* a header fails validation */
};

/* [base, base + initial) must already be usable memory. base 16-aligned,
 * initial a multiple of 16 and >= HEAP_MIN_BLOCK, initial <= max.
 * false = parameters refused; the heap then returns NULL for everything. */
bool heap_init(struct heap *h, uintptr_t base, uint32_t initial, uint32_t max,
               uint32_t grow_min, heap_grow_fn grow, void *ctx);

void *heap_alloc(struct heap *h, uint32_t size);
/* n * size bytes, zeroed; NULL if n * size overflows 32 bits. */
void *heap_calloc(struct heap *h, uint32_t n, uint32_t size);
/* NULL is a no-op (HEAP_OK). On any error the heap is left unchanged. */
enum heap_error heap_free(struct heap *h, void *p);

/* Full walk: headers, prev_size links, sizes adding up to the arena, no two
 * adjacent free blocks. *bad_block = address of the first bad header.
 * HEAP_ERR_NOT_HEAP if the heap was never initialised. */
enum heap_error heap_check(const struct heap *h, uintptr_t *bad_block);
void heap_stats(const struct heap *h, struct heap_stats *out);

const char *heap_error_str(enum heap_error error);

#endif
