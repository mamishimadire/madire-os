/* The block layer's write-back part and its save order (1.4 stage 9): see
 * blk_core_save.h. Pure; not linked into the kernel in stage 9. Every
 * static is blk_sv_-prefixed and every byte loop is written out (no libc
 * call). */
#include "kernel/storage/blk_core_save.h"

static void blk_sv_copy(uint8_t *d, const uint8_t *s, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++)
        d[i] = s[i];
}

void blk_sv_init(struct blk_sv *s, const struct blk_sv_ops *ops, struct blk_sv_ent *ents, uint8_t *data,
                 uint32_t cap, uint8_t *run, uint64_t part_bytes)
{
    s->ops = ops;
    s->ents = ents;
    s->data = data;
    s->run = run;
    s->cap = cap;
    s->n = 0;
    s->chunks = part_bytes / BLK_SV_CHUNK;
    s->failed = false;
    s->writes = s->flushes = s->saves = s->failed_saves = s->puts = 0;
    for (uint32_t c = 0; c <= BLK_SV_CLASSES; c++)
        s->class_writes[c] = 0;
}

/* The index of `chunk` in the sorted table, or where it would go (*at). */
static bool blk_sv_find(const struct blk_sv *s, uint64_t chunk, uint32_t *at)
{
    uint32_t lo = 0, hi = s->n;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2u;
        if (s->ents[mid].chunk < chunk)
            lo = mid + 1u;
        else
            hi = mid;
    }
    *at = lo;
    return lo < s->n && s->ents[lo].chunk == chunk;
}

int blk_sv_read(struct blk_sv *s, uint64_t off, uint32_t len, uint8_t *buf)
{
    uint64_t end = off + len;
    if (end < off || end > s->chunks * BLK_SV_CHUNK)
        return -BLK_EIO;
    uint64_t pos = off;
    while (pos < end) {
        uint32_t at = 0;
        uint64_t c = pos / BLK_SV_CHUNK;
        uint32_t within = (uint32_t)(pos % BLK_SV_CHUNK);
        uint64_t take = BLK_SV_CHUNK - within;
        if (take > end - pos)
            take = end - pos;
        if (blk_sv_find(s, c, &at)) {
            blk_sv_copy(buf + (pos - off), s->data + (uint64_t)at * BLK_SV_CHUNK + within, (uint32_t)take);
            pos += take;
            continue;
        }
        /* A run of clean chunks: one read of the disk. */
        uint64_t stop = pos + take;
        while (stop < end && !blk_sv_find(s, stop / BLK_SV_CHUNK, &at))
            stop += (end - stop < BLK_SV_CHUNK) ? end - stop : BLK_SV_CHUNK;
        if (s->ops->read(s->ops->ctx, pos, (uint32_t)(stop - pos), buf + (pos - off)) != 0)
            return -BLK_EIO;
        pos = stop;
    }
    return 0;
}

int blk_sv_put(struct blk_sv *s, uint64_t off, uint32_t len, const uint8_t *buf, uint32_t cls)
{
    if (off % BLK_SV_CHUNK || len % BLK_SV_CHUNK || len == 0 || cls < 1 || cls > BLK_SV_CLASSES)
        return -BLK_EIO;
    uint64_t first = off / BLK_SV_CHUNK, count = len / BLK_SV_CHUNK;
    if (first >= s->chunks || count > s->chunks - first)
        return -BLK_EIO;
    uint32_t at = 0, more = 0;
    for (uint64_t i = 0; i < count; i++)
        more += blk_sv_find(s, first + i, &at) ? 0u : 1u;
    if (more > s->cap - s->n)
        return -BLK_SV_FULL;
    for (uint64_t i = 0; i < count; i++) {
        uint64_t c = first + i;
        if (!blk_sv_find(s, c, &at)) {
            /* Open a slot at `at`: move the later entries and their bytes up. */
            for (uint32_t k = s->n; k > at; k--) {
                s->ents[k] = s->ents[k - 1u];
                blk_sv_copy(s->data + (uint64_t)k * BLK_SV_CHUNK, s->data + (uint64_t)(k - 1u) * BLK_SV_CHUNK,
                            BLK_SV_CHUNK);
            }
            s->n++;
            s->ents[at].chunk = (uint32_t)c;
        }
        s->ents[at].cls = (uint8_t)cls;
        blk_sv_copy(s->data + (uint64_t)at * BLK_SV_CHUNK, buf + i * BLK_SV_CHUNK, BLK_SV_CHUNK);
    }
    s->puts++;
    return 0;
}

uint32_t blk_sv_dirty(const struct blk_sv *s)
{
    return s->n;
}

bool blk_sv_room(const struct blk_sv *s, uint32_t chunks)
{
    return s->n + chunks <= s->cap / 2u;
}

/* Drops the entries already written (class 0), keeping the order. */
static void blk_sv_compact(struct blk_sv *s)
{
    uint32_t k = 0;
    for (uint32_t i = 0; i < s->n; i++) {
        if (s->ents[i].cls == 0)
            continue;
        if (k != i) {
            s->ents[k] = s->ents[i];
            blk_sv_copy(s->data + (uint64_t)k * BLK_SV_CHUNK, s->data + (uint64_t)i * BLK_SV_CHUNK, BLK_SV_CHUNK);
        }
        k++;
    }
    s->n = k;
}

static int blk_sv_fail(struct blk_sv *s)
{
    blk_sv_compact(s);
    s->failed = true;
    s->failed_saves++;
    return -BLK_EIO;
}

int blk_sv_save(struct blk_sv *s)
{
    if (s->failed)
        return -BLK_EIO;
    for (uint32_t cls = 1; cls <= BLK_SV_CLASSES; cls++) {
        bool wrote = false;
        uint32_t i = 0;
        while (i < s->n) {
            if (s->ents[i].cls != cls) {
                i++;
                continue;
            }
            uint32_t j = i, k = 0;
            while (j < s->n && k < BLK_SV_RUN_MAX && s->ents[j].cls == cls && s->ents[j].chunk == s->ents[i].chunk + k) {
                blk_sv_copy(s->run + (uint64_t)k * BLK_SV_CHUNK, s->data + (uint64_t)j * BLK_SV_CHUNK, BLK_SV_CHUNK);
                j++;
                k++;
            }
            s->writes++;
            s->class_writes[cls]++;
            if (s->ops->write(s->ops->ctx, (uint64_t)s->ents[i].chunk * BLK_SV_CHUNK, k * BLK_SV_CHUNK, s->run, cls) != 0)
                return blk_sv_fail(s);
            for (uint32_t m = i; m < j; m++)
                s->ents[m].cls = 0;
            wrote = true;
            i = j;
        }
        if (wrote) {
            s->flushes++;
            if (s->ops->flush(s->ops->ctx) != 0)
                return blk_sv_fail(s);
            blk_sv_compact(s);
        }
    }
    s->saves++;
    return 0;
}
