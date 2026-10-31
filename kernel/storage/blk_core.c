/* The block cache, the pure part (1.4 stage 4: reads only): see
 * blk_core.h. */
#include "kernel/storage/blk_core.h"

uint32_t blk_cache_pages(uint32_t free_pages)
{
    uint32_t share = free_pages / 16u;
    return share < BLK_CACHE_MAX ? share : BLK_CACHE_MAX;
}

void blk_cache_init(struct blk_cache *c, const struct blk_ops *ops, struct blk_slot *slots, uint32_t cap,
                    uint64_t sectors)
{
    c->ops = ops;
    c->slots = slots;
    c->cap = cap;
    c->made = 0;
    c->sectors = sectors;
    c->clock = 0;
    c->reads = c->hits = c->misses = c->commands = c->replaced = c->errors = c->refused = 0;
    c->short_of_pages = false;
    for (uint32_t i = 0; i < cap; i++) {
        slots[i].page = 0;
        slots[i].stamp = 0;
        slots[i].valid = false;
    }
}

static int32_t blk_lookup(const struct blk_cache *c, uint64_t page)
{
    for (uint32_t i = 0; i < c->made; i++)
        if (c->slots[i].valid && c->slots[i].page == page)
            return (int32_t)i;
    return -1;
}

/* A slot for a page about to be read: a new one while the cache grows,
 * else an unused one, else the least recently used; never one of the
 * run's own (taken[0..n-1]). -1 if there is none. */
static int32_t blk_victim(struct blk_cache *c, const uint32_t *taken, uint32_t n)
{
    if (c->made < c->cap && !c->short_of_pages) {
        if (c->ops->ready(c->ops->ctx, c->made))
            return (int32_t)c->made++;
        c->short_of_pages = true;
    }
    int32_t best = -1;
    for (uint32_t i = 0; i < c->made; i++) {
        bool mine = false;
        for (uint32_t k = 0; k < n; k++)
            mine = mine || taken[k] == i;
        if (mine)
            continue;
        if (!c->slots[i].valid)
            return (int32_t)i;
        if (best < 0 || c->slots[i].stamp < c->slots[best].stamp)
            best = (int32_t)i;
    }
    return best;
}

/* Copies the part of [lba, lba + count) that lies in `page` (held by slot s). */
static void blk_copy_out(struct blk_cache *c, uint32_t s, uint64_t page, uint64_t lba, uint32_t count, uint8_t *dst)
{
    uint64_t first = page * BLK_PER_PAGE, from = lba > first ? lba : first;
    uint64_t end = lba + count, page_end = first + BLK_PER_PAGE, to = end < page_end ? end : page_end;
    const uint8_t *src = c->ops->data(c->ops->ctx, s) + (from - first) * BLK_SECTOR;
    uint8_t *out = dst + (from - lba) * BLK_SECTOR;
    for (uint64_t i = 0; i < (to - from) * BLK_SECTOR; i++)
        out[i] = src[i];
    c->slots[s].stamp = ++c->clock;
}

int blk_cache_read(struct blk_cache *c, uint64_t lba, uint32_t count, uint8_t *dst)
{
    c->reads++;
    if (count == 0)
        return 0;
    if (lba >= c->sectors || count > c->sectors - lba) {
        c->refused++;
        return -BLK_EIO;
    }
    uint64_t page = lba / BLK_PER_PAGE, last = (lba + count - 1) / BLK_PER_PAGE;
    while (page <= last) {
        int32_t s = blk_lookup(c, page);
        if (s >= 0) {
            c->hits++;
            blk_copy_out(c, (uint32_t)s, page, lba, count, dst);
            page++;
            continue;
        }
        /* A run of pages not in the cache, and its slots. */
        uint32_t slots[BLK_RUN_MAX], n = 0;
        for (uint32_t i = 0; i < BLK_RUN_MAX; i++)
            slots[i] = 0;
        while (n < BLK_RUN_MAX && page + n <= last && (n == 0 || blk_lookup(c, page + n) < 0)) {
            int32_t v = blk_victim(c, slots, n);
            if (v < 0)
                break;
            slots[n++] = (uint32_t)v;
        }
        if (n == 0) {
            c->errors++;
            return -BLK_EIO;
        }
        for (uint32_t i = 0; i < n; i++) {
            c->replaced += c->slots[slots[i]].valid;
            c->slots[slots[i]].valid = false;
        }
        uint64_t from = page * BLK_PER_PAGE, to = (page + n) * BLK_PER_PAGE;
        if (to > c->sectors)
            to = c->sectors;
        c->commands++;
        if (c->ops->read(c->ops->ctx, from, (uint32_t)(to - from), slots, n) != 0) {
            c->errors++;
            return -BLK_EIO;
        }
        if (to < (page + n) * BLK_PER_PAGE) {       /* the disk's last page, in part: the rest zero */
            uint8_t *tail = c->ops->data(c->ops->ctx, slots[n - 1]);
            for (uint32_t i = (uint32_t)(to - (page + n - 1) * BLK_PER_PAGE) * BLK_SECTOR; i < BLK_PAGE; i++)
                tail[i] = 0;
        }
        for (uint32_t i = 0; i < n; i++) {
            c->misses++;
            c->slots[slots[i]].page = page + i;
            c->slots[slots[i]].valid = true;
            blk_copy_out(c, slots[i], page + i, lba, count, dst);
        }
        page += n;
    }
    return 0;
}

/* The CRC32 table by nibbles (polynomial 0xEDB88320, reflected). */
static const uint32_t blk_crc_nibble[16] = {
    0x00000000u, 0x1DB71064u, 0x3B6E20C8u, 0x26D930ACu, 0x76DC4190u, 0x6B6B51F4u, 0x4DB26158u, 0x5005713Cu,
    0xEDB88320u, 0xF00F9344u, 0xD6D6A3E8u, 0xCB61B38Cu, 0x9B64C2B0u, 0x86D3D2D4u, 0xA00AE278u, 0xBDBDF21Cu,
};

uint32_t blk_crc32(uint32_t crc, const uint8_t *p, uint32_t n)
{
    crc = ~crc;
    for (uint32_t i = 0; i < n; i++) {
        crc ^= p[i];
        crc = (crc >> 4) ^ blk_crc_nibble[crc & 0xFu];
        crc = (crc >> 4) ^ blk_crc_nibble[crc & 0xFu];
    }
    return ~crc;
}
