/* ext2, the write core (1.4 stage 9): see ext2_write.h for the save design
 * (R1-R4) and the policy. Pure; linked into the kernel since stage 10.
 * Every static is ext2_w_-prefixed and every byte loop is written out (no
 * libc call, so the kernel's flags need nothing more: part static checks
 * it). */
#include "kernel/fs/ext2_write.h"

#define EXT2_W_EA_MAGIC     0xEA020000u

/* ---- bytes ---- */

static uint16_t ext2_w_le16(const uint8_t *p)
{
    return (uint16_t)((uint32_t)p[0] | (uint32_t)p[1] << 8);
}

static uint32_t ext2_w_le32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static void ext2_w_p16(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static void ext2_w_p32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static void ext2_w_copy(void *d, const void *s, uint32_t n)
{
    uint8_t *a = d;
    const uint8_t *b = s;
    for (uint32_t i = 0; i < n; i++)
        a[i] = b[i];
}

static void ext2_w_zero(void *p, uint64_t n)
{
    uint8_t *b = p;
    for (uint64_t i = 0; i < n; i++)
        b[i] = 0;
}

static uint32_t ext2_w_now(struct ext2_wfs *w)
{
    return w->wops.now ? w->wops.now(w->wops.ctx) : 0u;
}

static void ext2_w_touch(struct ext2_wfs *w, uint32_t ino)
{
    if (w->wops.touch && ino)
        w->wops.touch(w->wops.ctx, w->saves_done, ino);
}

static uint32_t ext2_w_bs(const struct ext2_wfs *w)
{
    return w->fs.block_size;
}

/* ---- blocks, through the block layer ---- */

static int ext2_w_rd(void *ctx, uint64_t off, uint32_t len, uint8_t *buf)
{
    struct ext2_wfs *w = ctx;
    return blk_sv_read(w->sv, off, len, buf) == 0 ? 0 : -1;
}

static int ext2_w_bread(struct ext2_wfs *w, uint32_t blk, uint8_t *buf)
{
    if (blk >= w->fs.blocks_count)
        return EXT2_ECORRUPT;
    if (blk_sv_read(w->sv, (uint64_t)blk * ext2_w_bs(w), ext2_w_bs(w), buf) != 0)
        return EXT2_EIO;
    return EXT2_OK;
}

/* Every block the read core keeps that is `blk` takes the new bytes. */
static void ext2_w_keep(struct ext2_wfs *w, uint32_t blk, const uint8_t *buf)
{
    struct ext2_fs *fs = &w->fs;
    uint32_t bs = fs->block_size;
    if (fs->gdt_valid && fs->gdt_block == blk)
        ext2_w_copy(fs->gdt, buf, bs);
    if (fs->itb_valid && fs->itb_block == blk)
        ext2_w_copy(fs->itb, buf, bs);
    if (fs->dirb_valid && fs->dirb_block == blk)
        ext2_w_copy(fs->dirb, buf, bs);
    for (uint32_t d = 0; d < 3; d++)
        if (fs->ind_valid[d] && fs->ind_block[d] == blk)
            ext2_w_copy(fs->ind[d], buf, bs);
}

static int ext2_w_bput(struct ext2_wfs *w, uint32_t blk, const uint8_t *buf, uint32_t cls)
{
    if (blk < w->fs.first_data_block || blk == 0 || blk >= w->fs.blocks_count)
        return EXT2_ECORRUPT;
    int r = blk_sv_put(w->sv, (uint64_t)blk * ext2_w_bs(w), ext2_w_bs(w), buf, cls);
    if (r == -BLK_SV_FULL)
        return EXT2_EAGAIN;
    if (r)
        return EXT2_EIO;
    ext2_w_keep(w, blk, buf);
    return EXT2_OK;
}

/* The superblock (always chunk 1 alone): the counts, the state, the write time. */
static int ext2_w_sb_put(struct ext2_wfs *w)
{
    uint8_t *s = w->fs.sb;
    ext2_w_p32(s + 12, w->fs.free_blocks);
    ext2_w_p32(s + 16, w->fs.free_inodes);
    ext2_w_p32(s + 48, ext2_w_now(w));
    ext2_w_p16(s + 58, w->fs.state);
    int r = blk_sv_put(w->sv, EXT2_SB_OFFSET, EXT2_SB_SIZE, s, BLK_SV_SUPER);
    if (r == -BLK_SV_FULL)
        return EXT2_EAGAIN;
    return r ? EXT2_EIO : EXT2_OK;
}

/* Group g's counts changed by the deltas (descriptor and superblock). */
static int ext2_w_gd_add(struct ext2_wfs *w, uint32_t g, int32_t dblocks, int32_t dinodes, int32_t ddirs)
{
    struct ext2_fs *fs = &w->fs;
    if (g >= fs->groups)
        return EXT2_ECORRUPT;
    uint64_t byte = (uint64_t)g * EXT2_GD_SIZE;
    uint32_t blk = fs->first_data_block + 1u + (uint32_t)(byte / fs->block_size);
    uint32_t within = (uint32_t)(byte % fs->block_size);
    int r = ext2_w_bread(w, blk, w->gb);
    if (r)
        return r;
    uint8_t *p = w->gb + within;
    int64_t fb = (int64_t)ext2_w_le16(p + 12) + dblocks;
    int64_t fi = (int64_t)ext2_w_le16(p + 14) + dinodes;
    int64_t nd = (int64_t)ext2_w_le16(p + 16) + ddirs;
    int64_t tb = (int64_t)fs->free_blocks + dblocks;
    int64_t ti = (int64_t)fs->free_inodes + dinodes;
    if (fb < 0 || fb > 0xFFFF || fi < 0 || fi > 0xFFFF || nd < 0 || nd > 0xFFFF || tb < 0 || ti < 0)
        return EXT2_ECORRUPT;
    ext2_w_p16(p + 12, (uint32_t)fb);
    ext2_w_p16(p + 14, (uint32_t)fi);
    ext2_w_p16(p + 16, (uint32_t)nd);
    r = ext2_w_bput(w, blk, w->gb, BLK_SV_SUPER);
    if (r)
        return r;
    fs->free_blocks = (uint32_t)tb;
    fs->free_inodes = (uint32_t)ti;
    return ext2_w_sb_put(w);
}

static uint32_t ext2_w_group_blocks(const struct ext2_fs *fs, uint32_t g)
{
    uint32_t n = fs->blocks_count - fs->first_data_block - g * fs->blocks_per_group;
    return n < fs->blocks_per_group ? n : fs->blocks_per_group;
}

static uint32_t ext2_w_group_inodes(const struct ext2_fs *fs, uint32_t g)
{
    uint32_t n = fs->inodes_count - g * fs->inodes_per_group;
    return n < fs->inodes_per_group ? n : fs->inodes_per_group;
}

/* ---- the pending list, the fresh list, the step's reserve ---- */

static void ext2_w_pend_add(struct ext2_wfs *w, uint8_t kind, uint32_t num, uint32_t count)
{
    if (kind == EXT2_W_PEND_NAME && w->keep_ino && num == w->keep_ino) {
        w->kept = true;                 /* stage 10: still open; ext2_write_forget at its last close */
        return;
    }
    if (kind == EXT2_W_PEND_BLOCKS && w->pend_n > w->pend_floor) {
        struct ext2_w_pend *last = &w->pend[w->pend_n - 1u];
        if (last->kind == EXT2_W_PEND_BLOCKS && last->num + last->count == num) {
            last->count += count;
            return;
        }
        if (last->kind == EXT2_W_PEND_BLOCKS && num + count == last->num) {
            last->num = num;
            last->count += count;
            return;
        }
    }
    if (w->pend_n >= EXT2_W_PEND_MAX) {
        w->leaks += count;              /* left marked in use: a leak, never a clash */
        return;
    }
    w->pend[w->pend_n].kind = kind;
    w->pend[w->pend_n].num = num;
    w->pend[w->pend_n].count = count;
    w->pend_n++;
}

static bool ext2_w_is_fresh(const struct ext2_wfs *w, uint32_t blk)
{
    for (uint32_t i = 0; i < w->fresh_n; i++)
        if (w->fresh[i] == blk)
            return true;
    return false;
}

static void ext2_w_fresh_add(struct ext2_wfs *w, uint32_t blk)
{
    if (!ext2_w_is_fresh(w, blk) && w->fresh_n < EXT2_W_FRESH_MAX)
        w->fresh[w->fresh_n++] = blk;
}

/* ---- bitmaps and allocation ---- */

static int ext2_w_alloc_block(struct ext2_wfs *w, uint32_t goal, uint32_t *out)
{
    struct ext2_fs *fs = &w->fs;
    uint32_t g0 = 0, b0 = 0;
    *out = 0;
    if (goal >= fs->first_data_block && goal < fs->blocks_count) {
        g0 = (goal - fs->first_data_block) / fs->blocks_per_group;
        b0 = (goal - fs->first_data_block) % fs->blocks_per_group;
    }
    for (uint32_t k = 0; k < fs->groups; k++) {
        uint32_t g = (g0 + k) % fs->groups;
        struct ext2_gd gd = { 0, 0, 0, 0, 0, 0 };
        int r = ext2_group(fs, g, &gd);
        if (r)
            return r;
        if (gd.free_blocks == 0)
            continue;
        uint32_t n = ext2_w_group_blocks(fs, g);
        r = ext2_w_bread(w, gd.block_bitmap, w->bm);
        if (r)
            return r;
        uint32_t start = (k == 0 && b0 < n) ? b0 : 0u;
        for (uint32_t j = 0; j < n; j++) {
            uint32_t i = (start + j) % n;
            if (w->bm[i / 8u] & (1u << (i % 8u)))
                continue;
            w->bm[i / 8u] |= (uint8_t)(1u << (i % 8u));
            r = ext2_w_bput(w, gd.block_bitmap, w->bm, BLK_SV_BITMAP);
            if (r)
                return r;
            r = ext2_w_gd_add(w, g, -1, 0, 0);
            if (r)
                return r;
            *out = fs->first_data_block + g * fs->blocks_per_group + i;
            w->blocks_alloc++;
            return EXT2_OK;
        }
    }
    if (w->pend_n) {
        w->eagain++;
        return EXT2_EAGAIN;             /* pending frees: save, then try again (C5) */
    }
    return EXT2_ENOSPC;
}

/* Clears the bits of blocks first..first+count-1 (counts updated). */
static int ext2_w_blocks_clear(struct ext2_wfs *w, uint32_t first, uint32_t count, bool undo)
{
    struct ext2_fs *fs = &w->fs;
    while (count) {
        if (first < fs->first_data_block || first >= fs->blocks_count)
            return EXT2_ECORRUPT;
        uint32_t g = (first - fs->first_data_block) / fs->blocks_per_group;
        uint32_t bit = (first - fs->first_data_block) % fs->blocks_per_group;
        uint32_t n = fs->blocks_per_group - bit;
        if (n > count)
            n = count;
        struct ext2_gd gd = { 0, 0, 0, 0, 0, 0 };
        int r = ext2_group(fs, g, &gd);
        if (r)
            return r;
        r = ext2_w_bread(w, gd.block_bitmap, w->bm);
        if (r)
            return r;
        uint32_t cleared = 0;
        for (uint32_t i = bit; i < bit + n; i++) {
            if (w->bm[i / 8u] & (1u << (i % 8u))) {
                w->bm[i / 8u] &= (uint8_t)~(1u << (i % 8u));
                cleared++;
            }
        }
        r = ext2_w_bput(w, gd.block_bitmap, w->bm, BLK_SV_BITMAP);
        if (r)
            return r;
        r = ext2_w_gd_add(w, g, (int32_t)cleared, 0, 0);
        if (r)
            return r;
        if (undo)
            w->blocks_alloc -= cleared;
        else
            w->blocks_cleared += cleared;
        first += n;
        count -= n;
    }
    return EXT2_OK;
}

static int ext2_w_alloc_inode(struct ext2_wfs *w, uint32_t goal_group, bool dir, uint32_t *out)
{
    struct ext2_fs *fs = &w->fs;
    *out = 0;
    for (uint32_t k = 0; k < fs->groups; k++) {
        uint32_t g = (goal_group + k) % fs->groups;
        struct ext2_gd gd = { 0, 0, 0, 0, 0, 0 };
        int r = ext2_group(fs, g, &gd);
        if (r)
            return r;
        if (gd.free_inodes == 0)
            continue;
        uint32_t n = ext2_w_group_inodes(fs, g);
        r = ext2_w_bread(w, gd.inode_bitmap, w->bm);
        if (r)
            return r;
        for (uint32_t i = 0; i < n; i++) {
            uint32_t ino = g * fs->inodes_per_group + i + 1u;
            if (ino < fs->first_ino || (w->bm[i / 8u] & (1u << (i % 8u))))
                continue;
            w->bm[i / 8u] |= (uint8_t)(1u << (i % 8u));
            r = ext2_w_bput(w, gd.inode_bitmap, w->bm, BLK_SV_BITMAP);
            if (r)
                return r;
            r = ext2_w_gd_add(w, g, 0, -1, dir ? 1 : 0);
            if (r)
                return r;
            *out = ino;
            w->inodes_alloc++;
            return EXT2_OK;
        }
    }
    if (w->pend_n) {
        w->eagain++;
        return EXT2_EAGAIN;
    }
    return EXT2_ENOSPC;
}

static int ext2_w_inode_clear(struct ext2_wfs *w, uint32_t ino, bool dir, bool undo)
{
    struct ext2_fs *fs = &w->fs;
    if (ino == 0 || ino > fs->inodes_count)
        return EXT2_EINODE;
    uint32_t g = (ino - 1u) / fs->inodes_per_group, i = (ino - 1u) % fs->inodes_per_group;
    struct ext2_gd gd = { 0, 0, 0, 0, 0, 0 };
    int r = ext2_group(fs, g, &gd);
    if (r)
        return r;
    r = ext2_w_bread(w, gd.inode_bitmap, w->bm);
    if (r)
        return r;
    if (!(w->bm[i / 8u] & (1u << (i % 8u))))
        return EXT2_OK;
    w->bm[i / 8u] &= (uint8_t)~(1u << (i % 8u));
    r = ext2_w_bput(w, gd.inode_bitmap, w->bm, BLK_SV_BITMAP);
    if (r)
        return r;
    if (undo)
        w->inodes_alloc--;
    else
        w->inodes_cleared++;
    return ext2_w_gd_add(w, g, 0, 1, dir ? -1 : 0);
}

/* The step's reserve: every block a step may need is taken first, so a
 * step that cannot have them all changes nothing (xv6's idea of a fixed
 * most-blocks reserve per operation). */
static int ext2_w_release(struct ext2_wfs *w)
{
    int res = EXT2_OK;
    while (w->pool_n) {
        int r = ext2_w_blocks_clear(w, w->pool[--w->pool_n], 1, true);
        if (r && !res)
            res = r;
    }
    return res;
}

static int ext2_w_reserve(struct ext2_wfs *w, uint32_t n, uint32_t goal)
{
    if (n > EXT2_W_POOL_MAX || w->pool_n)
        return EXT2_EINVAL;
    for (uint32_t i = 0; i < n; i++) {
        uint32_t b = 0;
        int r = ext2_w_alloc_block(w, goal, &b);
        if (r) {
            ext2_w_release(w);
            return r;
        }
        w->pool[w->pool_n++] = b;
        goal = b + 1u;
    }
    return EXT2_OK;
}

static uint32_t ext2_w_take(struct ext2_wfs *w)
{
    return w->pool_n ? w->pool[--w->pool_n] : 0u;
}

/* ---- inodes ---- */

static int ext2_w_inode_loc(struct ext2_wfs *w, uint32_t ino, uint32_t *blk, uint32_t *within)
{
    struct ext2_fs *fs = &w->fs;
    if (ino == 0 || ino > fs->inodes_count)
        return EXT2_EINODE;
    uint32_t g = (ino - 1u) / fs->inodes_per_group, idx = (ino - 1u) % fs->inodes_per_group;
    struct ext2_gd gd = { 0, 0, 0, 0, 0, 0 };
    int r = ext2_group(fs, g, &gd);
    if (r)
        return r;
    uint64_t byte = (uint64_t)idx * fs->inode_size;
    *blk = gd.inode_table + (uint32_t)(byte / fs->block_size);
    *within = (uint32_t)(byte % fs->block_size);
    return EXT2_OK;
}

/* Writes the inode's fields over its record (the other bytes kept). */
static int ext2_w_iput(struct ext2_wfs *w, const struct ext2_inode *in)
{
    uint32_t blk = 0, within = 0;
    int r = ext2_w_inode_loc(w, in->ino, &blk, &within);
    if (r)
        return r;
    r = ext2_w_bread(w, blk, w->ib);
    if (r)
        return r;
    uint8_t *p = w->ib + within;
    ext2_w_p16(p, in->mode);
    ext2_w_p16(p + 2, in->uid);
    ext2_w_p32(p + 4, (uint32_t)in->size);
    ext2_w_p32(p + 8, in->atime);
    ext2_w_p32(p + 12, in->ctime);
    ext2_w_p32(p + 16, in->mtime);
    ext2_w_p32(p + 20, in->dtime);
    ext2_w_p16(p + 24, in->gid);
    ext2_w_p16(p + 26, in->links);
    ext2_w_p32(p + 28, in->blocks512);
    ext2_w_p32(p + 32, in->flags);
    for (uint32_t i = 0; i < EXT2_NBLOCKS; i++)
        ext2_w_p32(p + 40 + 4u * i, in->block[i]);
    ext2_w_p32(p + 100, in->generation);
    ext2_w_p32(p + 104, in->file_acl);
    if (w->fs.rev >= EXT2_REV_DYNAMIC && (in->mode & EXT2_S_IFMT) == EXT2_S_IFREG)
        ext2_w_p32(p + 108, (uint32_t)(in->size >> 32));
    r = ext2_w_bput(w, blk, w->ib, BLK_SV_INODE);
    if (r)
        return r;
    ext2_w_touch(w, in->ino);
    return EXT2_OK;
}

/* A new inode's record zeroed (with the extra size of 256-byte inodes)
 * and its fields set in *in; the caller fills it and writes it. */
static int ext2_w_inode_new(struct ext2_wfs *w, uint32_t ino, uint32_t mode, uint32_t links, struct ext2_inode *in)
{
    uint32_t blk = 0, within = 0;
    int r = ext2_w_inode_loc(w, ino, &blk, &within);
    if (r)
        return r;
    r = ext2_w_bread(w, blk, w->ib);
    if (r)
        return r;
    ext2_w_zero(w->ib + within, w->fs.inode_size);
    if (w->fs.inode_size >= 160u)
        ext2_w_p16(w->ib + within + 128, 32);   /* i_extra_isize, as mke2fs sets it */
    r = ext2_w_bput(w, blk, w->ib, BLK_SV_INODE);
    if (r)
        return r;
    uint32_t t = ext2_w_now(w);
    ext2_w_zero(in, sizeof *in);
    in->ino = ino;
    in->mode = (uint16_t)mode;
    in->links = (uint16_t)links;
    in->atime = in->ctime = in->mtime = t;
    in->generation = ++w->gen;
    return EXT2_OK;
}

static uint32_t ext2_w_ftype(uint32_t mode)
{
    switch (mode & EXT2_S_IFMT) {
    case EXT2_S_IFREG:  return 1;
    case EXT2_S_IFDIR:  return 2;
    case EXT2_S_IFCHR:  return 3;
    case EXT2_S_IFBLK:  return 4;
    case EXT2_S_IFIFO:  return 5;
    case EXT2_S_IFSOCK: return 6;
    case EXT2_S_IFLNK:  return 7;
    default:            return 0;
    }
}

static bool ext2_w_is_dir(const struct ext2_inode *in)
{
    return (in->mode & EXT2_S_IFMT) == EXT2_S_IFDIR;
}

static uint32_t ext2_w_igroup(const struct ext2_wfs *w, uint32_t ino)
{
    return (ino - 1u) / w->fs.inodes_per_group;
}

static uint32_t ext2_w_group_goal(const struct ext2_wfs *w, uint32_t g)
{
    return w->fs.first_data_block + g * w->fs.blocks_per_group;
}

static void ext2_w_less_blocks(struct ext2_wfs *w, struct ext2_inode *in)
{
    uint32_t u = ext2_w_bs(w) / 512u;
    if (in && in->blocks512 >= u)
        in->blocks512 -= u;
}

/* ---- the block map (R1, R2) ---- */

static int ext2_w_path(const struct ext2_fs *fs, uint64_t lblk, uint32_t *slot, uint32_t *depth, uint32_t idx[3])
{
    uint64_t p = fs->ptrs;
    idx[0] = idx[1] = idx[2] = 0;
    if (lblk < EXT2_NDIR) {
        *slot = (uint32_t)lblk;
        *depth = 0;
    } else if ((lblk -= EXT2_NDIR) < p) {
        *slot = EXT2_IND;
        *depth = 1;
        idx[0] = (uint32_t)lblk;
    } else if ((lblk -= p) < p * p) {
        *slot = EXT2_DIND;
        *depth = 2;
        idx[0] = (uint32_t)(lblk / p);
        idx[1] = (uint32_t)(lblk % p);
    } else if ((lblk -= p * p) < p * p * p) {
        *slot = EXT2_TIND;
        *depth = 3;
        idx[0] = (uint32_t)(lblk / (p * p));
        idx[1] = (uint32_t)((lblk / p) % p);
        idx[2] = (uint32_t)(lblk % p);
    } else {
        return EXT2_ERANGE;
    }
    return EXT2_OK;
}

/* The disk block of the file's block lblk; a new one (*isnew, its bytes
 * not yet written: the caller writes it whole, class 1) if there is none.
 * The indirect blocks on the way: a missing one is made fresh, a fresh one
 * changed in place, one the disk already uses copied to a fresh block (R2)
 * up to the first level that need not change. *in changes (the map and
 * i_blocks); the caller writes it. */
static int ext2_w_map(struct ext2_wfs *w, struct ext2_inode *in, uint64_t lblk, uint32_t goal, uint32_t *pblk,
                      bool *isnew)
{
    struct ext2_fs *fs = &w->fs;
    uint32_t bs = fs->block_size, slot = 0, depth = 0, idx[3] = { 0, 0, 0 };
    *pblk = 0;
    *isnew = false;
    int r = ext2_w_path(fs, lblk, &slot, &depth, idx);
    if (r)
        return r;
    if (depth == 0) {
        if (in->block[slot]) {
            if (in->block[slot] >= fs->blocks_count)
                return EXT2_ECORRUPT;
            *pblk = in->block[slot];
            return EXT2_OK;
        }
        r = ext2_w_reserve(w, 1, goal);
        if (r)
            return r;
        in->block[slot] = ext2_w_take(w);
        in->blocks512 += bs / 512u;
        *pblk = in->block[slot];
        *isnew = true;
        return EXT2_OK;
    }
    uint32_t c[3] = { 0, 0, 0 }, cur = in->block[slot], leaf = 0;
    for (uint32_t L = 0; L < depth; L++) {
        c[L] = cur;
        if (cur == 0)
            break;
        r = ext2_w_bread(w, cur, w->mb[L]);
        if (r)
            return r;
        cur = ext2_w_le32(w->mb[L] + 4u * idx[L]);
        if (L + 1u == depth)
            leaf = cur;
    }
    if (leaf) {
        if (leaf >= fs->blocks_count)
            return EXT2_ECORRUPT;
        *pblk = leaf;
        return EXT2_OK;
    }
    /* Which levels need a block of their own, from the bottom up. */
    bool nl[3] = { false, false, false }, root = true;
    uint32_t need = 1;
    for (int L = (int)depth - 1; L >= 0; L--) {
        if (c[L] != 0 && ext2_w_is_fresh(w, c[L])) {
            root = false;
            break;
        }
        nl[L] = true;
        need++;
    }
    r = ext2_w_reserve(w, need, goal);
    if (r)
        return r;
    uint32_t data = ext2_w_take(w), child = data;
    for (int L = (int)depth - 1; L >= 0; L--) {
        if (c[L] == 0)
            ext2_w_zero(w->mb[L], bs);
        ext2_w_p32(w->mb[L] + 4u * idx[L], child);
        if (!nl[L]) {
            r = ext2_w_bput(w, c[L], w->mb[L], BLK_SV_DATA);   /* fresh: changed in place */
            if (r)
                return r;
            break;
        }
        uint32_t nb = ext2_w_take(w);
        if (c[L]) {
            ext2_w_pend_add(w, EXT2_W_PEND_BLOCKS, c[L], 1);  /* R2: the old copy freed */
            w->copies++;
        } else {
            in->blocks512 += bs / 512u;
        }
        ext2_w_fresh_add(w, nb);
        r = ext2_w_bput(w, nb, w->mb[L], BLK_SV_DATA);
        if (r)
            return r;
        child = nb;
    }
    if (root)
        in->block[slot] = child;
    in->blocks512 += bs / 512u;
    *pblk = data;
    *isnew = true;
    return EXT2_OK;
}

static uint64_t ext2_w_span(const struct ext2_fs *fs, uint32_t h)
{
    uint64_t s = 1;
    while (h--)
        s *= fs->ptrs;
    return s;
}

/* Every block of the subtree (height h; 0: a data block) becomes a
 * pending free; *in's i_blocks lowered (in may be 0). */
static int ext2_w_free_tree(struct ext2_wfs *w, struct ext2_inode *in, uint32_t blk, uint32_t h, uint32_t L)
{
    if (blk == 0)
        return EXT2_OK;
    if (blk >= w->fs.blocks_count || L > 3)
        return EXT2_ECORRUPT;
    if (h > 0) {
        int r = ext2_w_bread(w, blk, w->mb[L]);
        if (r)
            return r;
        for (uint32_t i = 0; i < w->fs.ptrs; i++) {
            uint32_t child = ext2_w_le32(w->mb[L] + 4u * i);
            if (child) {
                r = ext2_w_free_tree(w, in, child, h - 1u, L + 1u);
                if (r)
                    return r;
            }
        }
    }
    ext2_w_pend_add(w, EXT2_W_PEND_BLOCKS, blk, 1);
    ext2_w_less_blocks(w, in);
    return EXT2_OK;
}

/* The copies truncate needs: one per partial indirect block the disk
 * already uses on the boundary's path. */
static int ext2_w_trunc_count(struct ext2_wfs *w, uint32_t blk, uint32_t h, uint64_t base, uint64_t keep, uint32_t L,
                              uint32_t *n)
{
    if (blk == 0 || h == 0)
        return EXT2_OK;
    uint64_t span = ext2_w_span(&w->fs, h);
    if (base >= keep || base + span <= keep)
        return EXT2_OK;
    if (!ext2_w_is_fresh(w, blk))
        (*n)++;
    int r = ext2_w_bread(w, blk, w->mb[L]);
    if (r)
        return r;
    uint64_t cs = ext2_w_span(&w->fs, h - 1u);
    uint32_t i = (uint32_t)((keep - base) / cs);
    return ext2_w_trunc_count(w, ext2_w_le32(w->mb[L] + 4u * i), h - 1u, base + (uint64_t)i * cs, keep, L + 1u, n);
}

/* The subtree at blk (height h, covering the file's blocks from base)
 * without the blocks from `keep` on: *out is its new root (0: gone; blk:
 * unchanged; a fresh copy: R2). */
static int ext2_w_trunc_sub(struct ext2_wfs *w, struct ext2_inode *in, uint32_t blk, uint32_t h, uint64_t base,
                            uint64_t keep, uint32_t L, uint32_t *out)
{
    *out = blk;
    if (blk == 0)
        return EXT2_OK;
    if (blk >= w->fs.blocks_count)
        return EXT2_ECORRUPT;
    uint64_t span = ext2_w_span(&w->fs, h);
    if (base >= keep) {
        *out = 0;
        return ext2_w_free_tree(w, in, blk, h, L);
    }
    if (base + span <= keep || h == 0)
        return EXT2_OK;
    int r = ext2_w_bread(w, blk, w->mb[L]);
    if (r)
        return r;
    uint64_t cs = ext2_w_span(&w->fs, h - 1u);
    bool changed = false, any = false;
    for (uint32_t i = 0; i < w->fs.ptrs; i++) {
        uint32_t child = ext2_w_le32(w->mb[L] + 4u * i), nc = 0;
        if (child == 0)
            continue;
        r = ext2_w_trunc_sub(w, in, child, h - 1u, base + (uint64_t)i * cs, keep, L + 1u, &nc);
        if (r)
            return r;
        if (nc != child) {
            ext2_w_p32(w->mb[L] + 4u * i, nc);
            changed = true;
        }
        if (nc)
            any = true;
    }
    if (!changed)
        return EXT2_OK;
    if (!any) {
        *out = 0;
        ext2_w_pend_add(w, EXT2_W_PEND_BLOCKS, blk, 1);
        ext2_w_less_blocks(w, in);
        return EXT2_OK;
    }
    if (ext2_w_is_fresh(w, blk))
        return ext2_w_bput(w, blk, w->mb[L], BLK_SV_DATA);
    uint32_t nb = ext2_w_take(w);
    if (nb == 0)
        return EXT2_EINVAL;                     /* the reserve was counted short: never */
    ext2_w_fresh_add(w, nb);
    r = ext2_w_bput(w, nb, w->mb[L], BLK_SV_DATA);
    if (r)
        return r;
    ext2_w_pend_add(w, EXT2_W_PEND_BLOCKS, blk, 1);
    w->copies++;
    *out = nb;
    return EXT2_OK;
}

/* ---- folders ---- */

struct ext2_w_loc {
    uint32_t pb, pos, ino, rec, act;
    int32_t prev;                   /* the entry before it in its block, or -1 */
};

static uint32_t ext2_w_reclen(uint32_t nl)
{
    return (8u + nl + 3u) & ~3u;
}

static int ext2_w_dirent(const struct ext2_wfs *w, const uint8_t *b, uint32_t pos, uint32_t *ino, uint32_t *rec,
                         uint32_t *nl)
{
    uint32_t bs = w->fs.block_size;
    if (pos % 4u || pos + EXT2_DIRENT_HEAD > bs)
        return EXT2_ECORRUPT;
    *ino = ext2_w_le32(b + pos);
    *rec = ext2_w_le16(b + pos + 4);
    *nl = w->fs.filetype ? b[pos + 6] : ext2_w_le16(b + pos + 6);
    if (*rec < EXT2_DIRENT_MIN || *rec % 4u || pos + *rec > bs)
        return EXT2_ECORRUPT;
    if (*ino && (*nl == 0 || *nl > EXT2_NAME_MAX || EXT2_DIRENT_HEAD + *nl > *rec || *ino > w->fs.inodes_count))
        return EXT2_ECORRUPT;
    return EXT2_OK;
}

static int ext2_w_dir_check(const struct ext2_wfs *w, const struct ext2_inode *d)
{
    if (!ext2_w_is_dir(d))
        return EXT2_ENOTDIR;
    if (d->links == 0)
        return EXT2_ENOENT;
    if (d->size % w->fs.block_size || d->size > EXT2_DIR_MAX)
        return EXT2_ECORRUPT;
    return EXT2_OK;
}

/* Folder block lblk into w->db. */
static int ext2_w_dir_block(struct ext2_wfs *w, const struct ext2_inode *d, uint64_t lblk, uint32_t *pb)
{
    int r = ext2_bmap(&w->fs, d, lblk, pb);
    if (r)
        return r;
    if (*pb == 0)
        return EXT2_ECORRUPT;
    return ext2_w_bread(w, *pb, w->db);
}

static int ext2_w_find(struct ext2_wfs *w, const struct ext2_inode *d, const char *name, uint32_t len,
                       struct ext2_w_loc *loc)
{
    int r = ext2_w_dir_check(w, d);
    if (r)
        return r;
    uint32_t bs = w->fs.block_size;
    uint64_t nb = d->size / bs;
    for (uint64_t lb = 0; lb < nb; lb++) {
        uint32_t pb = 0;
        r = ext2_w_dir_block(w, d, lb, &pb);
        if (r)
            return r;
        int32_t prev = -1;
        for (uint32_t pos = 0; pos < bs;) {
            uint32_t ino = 0, rec = 0, nl = 0;
            r = ext2_w_dirent(w, w->db, pos, &ino, &rec, &nl);
            if (r)
                return r;
            if (ino && nl == len) {
                uint32_t i = 0;
                while (i < len && w->db[pos + EXT2_DIRENT_HEAD + i] == (uint8_t)name[i])
                    i++;
                if (i == len) {
                    loc->pb = pb;
                    loc->pos = pos;
                    loc->ino = ino;
                    loc->rec = rec;
                    loc->act = ext2_w_reclen(nl);
                    loc->prev = prev;
                    return EXT2_OK;
                }
            }
            prev = (int32_t)pos;
            pos += rec;
        }
    }
    return EXT2_ENOENT;
}

/* Room for an entry of `need` bytes in a block the disk already uses (R4):
 * EXT2_OK with *loc (its block left in w->db), or EXT2_ENOENT
 * (*saw_fresh: a fresh block was passed over). */
static int ext2_w_slot(struct ext2_wfs *w, const struct ext2_inode *d, uint32_t need, struct ext2_w_loc *loc,
                       bool *saw_fresh)
{
    uint32_t bs = w->fs.block_size;
    uint64_t nb = d->size / bs;
    *saw_fresh = false;
    for (uint64_t lb = 0; lb < nb; lb++) {
        uint32_t pb = 0;
        int r = ext2_bmap(&w->fs, d, lb, &pb);
        if (r)
            return r;
        if (pb == 0)
            return EXT2_ECORRUPT;
        if (ext2_w_is_fresh(w, pb)) {
            *saw_fresh = true;
            continue;
        }
        r = ext2_w_bread(w, pb, w->db);
        if (r)
            return r;
        for (uint32_t pos = 0; pos < bs;) {
            uint32_t ino = 0, rec = 0, nl = 0;
            r = ext2_w_dirent(w, w->db, pos, &ino, &rec, &nl);
            if (r)
                return r;
            uint32_t act = ino ? ext2_w_reclen(nl) : 0u;
            if ((!ino && rec >= need) || (ino && rec >= act + need)) {
                loc->pb = pb;
                loc->pos = pos;
                loc->ino = ino;
                loc->rec = rec;
                loc->act = act;
                loc->prev = -1;
                return EXT2_OK;
            }
            pos += rec;
        }
    }
    return EXT2_ENOENT;
}

/* A dir record change: the times, the index flag cleared (dir_index), the
 * link count. */
static int ext2_w_dir_changed(struct ext2_wfs *w, struct ext2_inode *d, int32_t dlinks)
{
    int64_t links = (int64_t)d->links + dlinks;
    if (links < 0)
        links = 0;
    d->links = (uint16_t)links;
    d->mtime = d->ctime = ext2_w_now(w);
    d->flags &= ~(uint32_t)EXT2_INDEX_FL;
    return ext2_w_iput(w, d);
}

/* R4: an empty block added to the folder, then EXT2_EAGAIN. */
static int ext2_w_dir_grow(struct ext2_wfs *w, struct ext2_inode *d)
{
    uint32_t bs = w->fs.block_size, pb = 0;
    bool isnew = false;
    if (d->size + bs > EXT2_DIR_MAX)
        return EXT2_EFBIG;
    uint32_t goal = d->block[0] ? d->block[0] : ext2_w_group_goal(w, ext2_w_igroup(w, d->ino));
    int r = ext2_w_map(w, d, d->size / bs, goal, &pb, &isnew);
    if (r)
        return r;
    if (!isnew)
        return EXT2_ECORRUPT;
    ext2_w_zero(w->db, bs);
    ext2_w_p16(w->db + 4, bs);
    ext2_w_fresh_add(w, pb);
    r = ext2_w_bput(w, pb, w->db, BLK_SV_DATA);
    if (r)
        return r;
    d->size += bs;
    r = ext2_w_dir_changed(w, d, 0);
    if (r)
        return r;
    w->eagain++;
    return EXT2_EAGAIN;
}

/* Makes sure an entry of a `len`-byte name fits (R4): EXT2_OK, or
 * EXT2_EAGAIN after growing (or when only a fresh block has room). */
static int ext2_w_room(struct ext2_wfs *w, struct ext2_inode *d, uint32_t len)
{
    struct ext2_w_loc loc = { 0, 0, 0, 0, 0, -1 };
    bool fresh = false;
    int r = ext2_w_slot(w, d, ext2_w_reclen(len), &loc, &fresh);
    if (r == EXT2_OK || r != EXT2_ENOENT)
        return r;
    if (fresh) {
        w->eagain++;
        return EXT2_EAGAIN;
    }
    return ext2_w_dir_grow(w, d);
}

static void ext2_w_put_name(const struct ext2_wfs *w, uint8_t *e, const char *name, uint32_t len, uint32_t ft)
{
    if (w->fs.filetype) {
        e[6] = (uint8_t)len;
        e[7] = (uint8_t)ft;
    } else {
        ext2_w_p16(e + 6, len);
    }
    for (uint32_t i = 0; i < len; i++)
        e[EXT2_DIRENT_HEAD + i] = (uint8_t)name[i];
}

/* The entry added (room made sure first), then the folder's record. */
static int ext2_w_add(struct ext2_wfs *w, struct ext2_inode *d, const char *name, uint32_t len, uint32_t ino,
                      uint32_t ft, int32_t dlinks)
{
    struct ext2_w_loc loc = { 0, 0, 0, 0, 0, -1 };
    bool fresh = false;
    int r = ext2_w_slot(w, d, ext2_w_reclen(len), &loc, &fresh);
    if (r == EXT2_ENOENT)
        return EXT2_EAGAIN;
    if (r)
        return r;
    uint32_t pos = loc.pos, rec = loc.rec;
    if (loc.ino) {
        ext2_w_p16(w->db + loc.pos + 4, loc.act);
        pos = loc.pos + loc.act;
        rec = loc.rec - loc.act;
    }
    uint8_t *e = w->db + pos;
    ext2_w_p32(e, ino);
    ext2_w_p16(e + 4, rec);
    ext2_w_put_name(w, e, name, len, ft);
    r = ext2_w_bput(w, loc.pb, w->db, BLK_SV_FOLDER);
    if (r)
        return r;
    ext2_w_touch(w, ino);
    return ext2_w_dir_changed(w, d, dlinks);
}

static int ext2_w_remove(struct ext2_wfs *w, struct ext2_inode *d, const struct ext2_w_loc *loc, int32_t dlinks)
{
    int r = ext2_w_bread(w, loc->pb, w->db);
    if (r)
        return r;
    if (loc->prev >= 0) {
        uint8_t *pe = w->db + (uint32_t)loc->prev;
        ext2_w_p16(pe + 4, ext2_w_le16(pe + 4) + loc->rec);
    } else {
        ext2_w_p32(w->db + loc->pos, 0);
    }
    r = ext2_w_bput(w, loc->pb, w->db, ext2_w_is_fresh(w, loc->pb) ? BLK_SV_DATA : BLK_SV_FOLDER);
    if (r)
        return r;
    ext2_w_touch(w, loc->ino);
    return ext2_w_dir_changed(w, d, dlinks);
}

/* The entry at *loc names `ino` from now on. */
static int ext2_w_retarget(struct ext2_wfs *w, struct ext2_inode *d, const struct ext2_w_loc *loc, uint32_t ino,
                           uint32_t ft, int32_t dlinks)
{
    int r = ext2_w_bread(w, loc->pb, w->db);
    if (r)
        return r;
    ext2_w_p32(w->db + loc->pos, ino);
    if (w->fs.filetype)
        w->db[loc->pos + 7] = (uint8_t)ft;
    r = ext2_w_bput(w, loc->pb, w->db, ext2_w_is_fresh(w, loc->pb) ? BLK_SV_DATA : BLK_SV_FOLDER);
    if (r)
        return r;
    ext2_w_touch(w, loc->ino);
    ext2_w_touch(w, ino);
    return ext2_w_dir_changed(w, d, dlinks);
}

/* EXT2_OK if the folder holds only "." and "..". */
static int ext2_w_empty(struct ext2_wfs *w, const struct ext2_inode *d)
{
    int r = ext2_w_dir_check(w, d);
    if (r)
        return r;
    uint32_t bs = w->fs.block_size;
    for (uint64_t lb = 0; lb < d->size / bs; lb++) {
        uint32_t pb = 0;
        r = ext2_w_dir_block(w, d, lb, &pb);
        if (r)
            return r;
        for (uint32_t pos = 0; pos < bs;) {
            uint32_t ino = 0, rec = 0, nl = 0;
            r = ext2_w_dirent(w, w->db, pos, &ino, &rec, &nl);
            if (r)
                return r;
            const uint8_t *n = w->db + pos + EXT2_DIRENT_HEAD;
            bool dots = (nl == 1 && n[0] == '.') || (nl == 2 && n[0] == '.' && n[1] == '.');
            if (ino && !dots)
                return EXT2_ENOTEMPTY;
            pos += rec;
        }
    }
    return EXT2_OK;
}

static int ext2_w_name(const char *name, uint32_t len)
{
    if (len == 0)
        return EXT2_EINVAL;
    if (len > EXT2_NAME_MAX)
        return EXT2_ENAMETOOLONG;
    for (uint32_t i = 0; i < len; i++)
        if (name[i] == 0 || name[i] == '/')
            return EXT2_EINVAL;
    if ((len == 1 && name[0] == '.') || (len == 2 && name[0] == '.' && name[1] == '.'))
        return EXT2_EINVAL;
    return EXT2_OK;
}

/* Every operation starts here: read-only, or not enough room for a step
 * (dirty chunks, the pending list, the fresh list): save first. */
static int ext2_w_begin(struct ext2_wfs *w)
{
    if (!w->rw || w->failed)
        return EXT2_EROFS;
    if (w->sv->saving || !blk_sv_room(w->sv, EXT2_W_ROOM_CHUNKS) || w->pend_n + 256u > EXT2_W_PEND_MAX ||
        w->fresh_n + 16u > EXT2_W_FRESH_MAX) {
        w->eagain++;
        return EXT2_EAGAIN;
    }
    return EXT2_OK;
}

static bool ext2_w_step_room(const struct ext2_wfs *w)
{
    return blk_sv_room(w->sv, EXT2_W_ROOM_CHUNKS) && w->pend_n + 256u <= EXT2_W_PEND_MAX &&
           w->fresh_n + 16u <= EXT2_W_FRESH_MAX;
}

/* ---- the operations ---- */

int ext2_write_create(struct ext2_wfs *w, uint32_t dir, const char *name, uint32_t len, uint32_t mode, uint32_t *ino)
{
    struct ext2_inode d, in;
    struct ext2_w_loc loc = { 0, 0, 0, 0, 0, -1 };
    ext2_w_zero(&in, sizeof in);
    ext2_w_zero(&d, sizeof d);
    *ino = 0;
    int r = ext2_w_begin(w);
    if (!r)
        r = ext2_w_name(name, len);
    if (!r)
        r = ext2_inode_get(&w->fs, dir, &d);
    if (!r)
        r = ext2_w_dir_check(w, &d);
    if (r)
        return r;
    r = ext2_w_find(w, &d, name, len, &loc);
    if (r == EXT2_OK)
        return EXT2_EEXIST;
    if (r != EXT2_ENOENT)
        return r;
    r = ext2_w_room(w, &d, len);
    if (r)
        return r;
    if ((mode & EXT2_S_IFMT) == 0 || (mode & EXT2_S_IFMT) == EXT2_S_IFDIR)
        mode = EXT2_S_IFREG | (mode & 07777u);
    uint32_t n = 0;
    r = ext2_w_alloc_inode(w, ext2_w_igroup(w, dir), false, &n);
    if (r)
        return r;
    r = ext2_w_inode_new(w, n, mode, 1, &in);
    if (!r)
        r = ext2_w_iput(w, &in);
    if (!r)
        r = ext2_w_add(w, &d, name, len, n, ext2_w_ftype(mode), 0);
    if (r)
        return r;
    *ino = n;
    return EXT2_OK;
}

int ext2_write_mkdir(struct ext2_wfs *w, uint32_t dir, const char *name, uint32_t len, uint32_t mode, uint32_t *ino)
{
    struct ext2_inode d, in;
    struct ext2_w_loc loc = { 0, 0, 0, 0, 0, -1 };
    ext2_w_zero(&in, sizeof in);
    ext2_w_zero(&d, sizeof d);
    *ino = 0;
    int r = ext2_w_begin(w);
    if (!r)
        r = ext2_w_name(name, len);
    if (!r)
        r = ext2_inode_get(&w->fs, dir, &d);
    if (!r)
        r = ext2_w_dir_check(w, &d);
    if (r)
        return r;
    r = ext2_w_find(w, &d, name, len, &loc);
    if (r == EXT2_OK)
        return EXT2_EEXIST;
    if (r != EXT2_ENOENT)
        return r;
    if (d.links >= EXT2_W_LINK_MAX)
        return EXT2_EMLINK;
    r = ext2_w_room(w, &d, len);
    if (r)
        return r;
    uint32_t n = 0, bs = w->fs.block_size;
    r = ext2_w_alloc_inode(w, ext2_w_igroup(w, dir), true, &n);
    if (r)
        return r;
    r = ext2_w_reserve(w, 1, ext2_w_group_goal(w, ext2_w_igroup(w, n)));
    if (r) {
        int u = ext2_w_inode_clear(w, n, true, true);
        return u ? u : r;
    }
    uint32_t b = ext2_w_take(w);
    /* The folder's first block, fresh (class 1): "." and "..". */
    ext2_w_zero(w->db, bs);
    ext2_w_p32(w->db, n);
    ext2_w_p16(w->db + 4, 12);
    ext2_w_put_name(w, w->db, ".", 1, 2);
    ext2_w_p32(w->db + 12, dir);
    ext2_w_p16(w->db + 16, bs - 12u);
    ext2_w_put_name(w, w->db + 12, "..", 2, 2);
    ext2_w_fresh_add(w, b);
    r = ext2_w_bput(w, b, w->db, BLK_SV_DATA);
    if (!r)
        r = ext2_w_inode_new(w, n, EXT2_S_IFDIR | (mode & 07777u), 2, &in);
    if (r)
        return r;
    in.block[0] = b;
    in.size = bs;
    in.blocks512 = bs / 512u;
    r = ext2_w_iput(w, &in);
    if (!r)
        r = ext2_w_add(w, &d, name, len, n, 2, 1);
    if (r)
        return r;
    *ino = n;
    return EXT2_OK;
}

int ext2_write_unlink(struct ext2_wfs *w, uint32_t dir, const char *name, uint32_t len)
{
    struct ext2_inode d, t;
    struct ext2_w_loc loc = { 0, 0, 0, 0, 0, -1 };
    ext2_w_zero(&d, sizeof d);
    ext2_w_zero(&t, sizeof t);
    int r = ext2_w_begin(w);
    if (!r)
        r = ext2_w_name(name, len);
    if (!r)
        r = ext2_inode_get(&w->fs, dir, &d);
    if (!r)
        r = ext2_w_find(w, &d, name, len, &loc);
    if (!r)
        r = ext2_inode_get(&w->fs, loc.ino, &t);
    if (r)
        return r;
    if (ext2_w_is_dir(&t))
        return EXT2_EISDIR;
    r = ext2_w_remove(w, &d, &loc, 0);
    if (r)
        return r;
    ext2_w_pend_add(w, EXT2_W_PEND_NAME, loc.ino, 1);  /* R3: the inode in save E+1 */
    return EXT2_OK;
}

int ext2_write_rmdir(struct ext2_wfs *w, uint32_t dir, const char *name, uint32_t len)
{
    struct ext2_inode d, t;
    struct ext2_w_loc loc = { 0, 0, 0, 0, 0, -1 };
    ext2_w_zero(&d, sizeof d);
    ext2_w_zero(&t, sizeof t);
    int r = ext2_w_begin(w);
    if (!r)
        r = ext2_w_name(name, len);
    if (!r)
        r = ext2_inode_get(&w->fs, dir, &d);
    if (!r)
        r = ext2_w_find(w, &d, name, len, &loc);
    if (!r)
        r = ext2_inode_get(&w->fs, loc.ino, &t);
    if (r)
        return r;
    if (!ext2_w_is_dir(&t))
        return EXT2_ENOTDIR;
    r = ext2_w_empty(w, &t);
    if (r)
        return r;
    r = ext2_inode_get(&w->fs, dir, &d);
    if (!r)
        r = ext2_w_remove(w, &d, &loc, -1);
    if (r)
        return r;
    ext2_w_pend_add(w, EXT2_W_PEND_NAME, loc.ino, 1);
    return EXT2_OK;
}

int ext2_write_rename(struct ext2_wfs *w, uint32_t odir, const char *oname, uint32_t olen, uint32_t ndir,
                      const char *nname, uint32_t nlen)
{
    struct ext2_inode od, nd, s, t;
    struct ext2_w_loc ol = { 0, 0, 0, 0, 0, -1 }, nl = { 0, 0, 0, 0, 0, -1 };
    ext2_w_zero(&od, sizeof od);
    ext2_w_zero(&nd, sizeof nd);
    ext2_w_zero(&s, sizeof s);
    ext2_w_zero(&t, sizeof t);
    int r = ext2_w_begin(w);
    if (!r)
        r = ext2_w_name(oname, olen);
    if (!r)
        r = ext2_w_name(nname, nlen);
    if (!r)
        r = ext2_inode_get(&w->fs, odir, &od);
    if (!r)
        r = ext2_w_find(w, &od, oname, olen, &ol);
    if (!r)
        r = ext2_inode_get(&w->fs, ol.ino, &s);
    if (!r)
        r = ext2_inode_get(&w->fs, ndir, &nd);
    if (!r)
        r = ext2_w_dir_check(w, &nd);
    if (r)
        return r;
    bool sdir = ext2_w_is_dir(&s), cross = odir != ndir;
    if (sdir && cross) {
        /* The new folder must not be the moved one or inside it. */
        uint32_t cur = ndir;
        for (uint32_t k = 0; k < 4096u; k++) {
            struct ext2_inode c;
            struct ext2_w_loc up = { 0, 0, 0, 0, 0, -1 };
            ext2_w_zero(&c, sizeof c);
            if (cur == s.ino)
                return EXT2_EINVAL;
            if (cur == EXT2_ROOT_INO)
                break;
            r = ext2_inode_get(&w->fs, cur, &c);
            if (!r)
                r = ext2_w_find(w, &c, "..", 2, &up);
            if (r)
                return r;
            cur = up.ino;
        }
    }
    r = ext2_w_find(w, &nd, nname, nlen, &nl);
    if (r == EXT2_OK) {
        if (nl.ino == s.ino)
            return EXT2_OK;
        r = ext2_inode_get(&w->fs, nl.ino, &t);
        if (r)
            return r;
        bool tdir = ext2_w_is_dir(&t);
        if (tdir && !sdir)
            return EXT2_EISDIR;
        if (!tdir && sdir)
            return EXT2_ENOTDIR;
        if (tdir) {
            r = ext2_w_empty(w, &t);
            if (r)
                return r;
        }
        int32_t dl = (tdir ? -1 : 0) + ((sdir && cross) ? 1 : 0);
        r = ext2_inode_get(&w->fs, ndir, &nd);
        if (!r)
            r = ext2_w_retarget(w, &nd, &nl, s.ino, ext2_w_ftype(s.mode), dl);
        if (r)
            return r;
        ext2_w_pend_add(w, EXT2_W_PEND_NAME, t.ino, 1);    /* R3: the replaced inode */
    } else if (r == EXT2_ENOENT) {
        if (sdir && cross && nd.links >= EXT2_W_LINK_MAX)
            return EXT2_EMLINK;
        r = ext2_w_room(w, &nd, nlen);
        if (!r)
            r = ext2_w_add(w, &nd, nname, nlen, s.ino, ext2_w_ftype(s.mode), (sdir && cross) ? 1 : 0);
        if (r)
            return r;
    } else {
        return r;
    }
    /* The old name, found again (its block may have changed). */
    r = ext2_inode_get(&w->fs, odir, &od);
    if (!r)
        r = ext2_w_find(w, &od, oname, olen, &ol);
    if (!r)
        r = ext2_w_remove(w, &od, &ol, (sdir && cross) ? -1 : 0);
    if (r)
        return r;
    r = ext2_inode_get(&w->fs, s.ino, &s);
    if (r)
        return r;
    if (sdir && cross) {
        struct ext2_w_loc up = { 0, 0, 0, 0, 0, -1 };
        r = ext2_w_find(w, &s, "..", 2, &up);
        if (!r)
            r = ext2_w_retarget(w, &s, &up, ndir, 2, 0);    /* the moved folder's ".." */
        return r;
    }
    s.ctime = ext2_w_now(w);
    return ext2_w_iput(w, &s);
}

int64_t ext2_write_pwrite(struct ext2_wfs *w, uint32_t ino, uint64_t off, const uint8_t *buf, uint32_t len)
{
    struct ext2_inode in;
    ext2_w_zero(&in, sizeof in);
    int r = ext2_w_begin(w);
    if (!r)
        r = ext2_inode_get(&w->fs, ino, &in);
    if (r)
        return r;
    if (ext2_w_is_dir(&in))
        return EXT2_EISDIR;
    if ((in.mode & EXT2_S_IFMT) != EXT2_S_IFREG || in.links == 0)
        return EXT2_EINVAL;
    if (len == 0)
        return 0;
    if (off >= w->file_max || len > w->file_max - off)
        return EXT2_EFBIG;
    uint32_t bs = w->fs.block_size;
    uint32_t goal = in.block[0] ? in.block[0] : ext2_w_group_goal(w, ext2_w_igroup(w, ino));
    uint64_t done = 0;
    while (done < len) {
        if (done && !ext2_w_step_room(w))
            break;
        uint64_t pos = off + done;
        uint32_t within = (uint32_t)(pos % bs);
        uint64_t chunk = bs - within;
        if (chunk > len - done)
            chunk = len - done;
        uint32_t pb = 0;
        bool isnew = false;
        r = ext2_w_map(w, &in, pos / bs, goal, &pb, &isnew);
        if (r) {
            if (done && (r == EXT2_EAGAIN || r == EXT2_ENOSPC))
                break;
            return r;
        }
        if (isnew)
            ext2_w_zero(w->xb, bs);
        else if (chunk < bs && (r = ext2_w_bread(w, pb, w->xb)) != 0)
            return r;
        ext2_w_copy(w->xb + within, buf + done, (uint32_t)chunk);
        r = ext2_w_bput(w, pb, w->xb, BLK_SV_DATA);
        if (r)
            return r;
        goal = pb + 1u;
        done += chunk;
    }
    if (off + done > in.size)
        in.size = off + done;
    in.mtime = in.ctime = ext2_w_now(w);
    r = ext2_w_iput(w, &in);
    return r ? r : (int64_t)done;
}

int ext2_write_truncate(struct ext2_wfs *w, uint32_t ino, uint64_t size)
{
    struct ext2_fs *fs = &w->fs;
    struct ext2_inode in;
    ext2_w_zero(&in, sizeof in);
    int r = ext2_w_begin(w);
    if (!r)
        r = ext2_inode_get(fs, ino, &in);
    if (r)
        return r;
    if (ext2_w_is_dir(&in))
        return EXT2_EISDIR;
    if ((in.mode & EXT2_S_IFMT) != EXT2_S_IFREG || in.links == 0)
        return EXT2_EINVAL;
    if (size > w->file_max)
        return EXT2_EFBIG;
    uint32_t t = ext2_w_now(w);
    if (size >= in.size) {
        in.size = size;                         /* a hole at the end */
        in.mtime = in.ctime = t;
        return ext2_w_iput(w, &in);
    }
    uint32_t bs = fs->block_size;
    uint64_t keep = (size + bs - 1u) / bs, p = fs->ptrs;
    uint64_t base[3] = { EXT2_NDIR, EXT2_NDIR + p, EXT2_NDIR + p + p * p };
    uint32_t need = 0;
    for (uint32_t k = 0; k < 3; k++) {
        r = ext2_w_trunc_count(w, in.block[EXT2_IND + k], k + 1u, base[k], keep, 0, &need);
        if (r)
            return r;
    }
    uint32_t goal = in.block[0] ? in.block[0] : ext2_w_group_goal(w, ext2_w_igroup(w, ino));
    r = ext2_w_reserve(w, need, goal);
    if (r)
        return r;
    /* The rest of the last kept block reads as zeros from now on. */
    if (size % bs) {
        uint32_t pb = 0;
        r = ext2_bmap(fs, &in, size / bs, &pb);
        if (!r && pb) {
            r = ext2_w_bread(w, pb, w->xb);
            if (!r) {
                ext2_w_zero(w->xb + size % bs, bs - size % bs);
                r = ext2_w_bput(w, pb, w->xb, BLK_SV_DATA);
            }
        }
        if (r) {
            ext2_w_release(w);
            return r;
        }
    }
    for (uint64_t i = keep; i < EXT2_NDIR; i++) {
        if (in.block[i]) {
            ext2_w_pend_add(w, EXT2_W_PEND_BLOCKS, in.block[i], 1);
            ext2_w_less_blocks(w, &in);
            in.block[i] = 0;
        }
    }
    for (uint32_t k = 0; k < 3 && !r; k++) {
        uint32_t nb = 0;
        r = ext2_w_trunc_sub(w, &in, in.block[EXT2_IND + k], k + 1u, base[k], keep, 0, &nb);
        if (!r)
            in.block[EXT2_IND + k] = nb;
    }
    int u = ext2_w_release(w);
    if (r)
        return r;
    if (u)
        return u;
    in.size = size;
    in.mtime = in.ctime = t;
    return ext2_w_iput(w, &in);
}

/* ---- R3's steps after a save ---- */

/* A name of `ino` was removed in the save that has just finished: the
 * inode is written as deleted (its blocks, attribute block and bit then
 * pending), or a hard-linked file's count lowered. */
static int ext2_w_name_gone(struct ext2_wfs *w, uint32_t ino)
{
    struct ext2_inode in;
    ext2_w_zero(&in, sizeof in);
    int r = ext2_inode_get(&w->fs, ino, &in);
    if (r)
        return r;
    bool dir = ext2_w_is_dir(&in);
    if (!dir && in.links > 1) {
        in.links--;
        in.ctime = ext2_w_now(w);
        return ext2_w_iput(w, &in);
    }
    if (in.links == 0)
        return EXT2_OK;                         /* already deleted */
    /* Only these hold block numbers (a device's i_block holds its number,
     * a fast link's its target, a FIFO's or socket's nothing). */
    uint32_t type = in.mode & EXT2_S_IFMT;
    bool mapped = type == EXT2_S_IFREG || type == EXT2_S_IFDIR ||
                  (type == EXT2_S_IFLNK && !ext2_fast_link(&w->fs, &in));
    if (mapped) {
        for (uint32_t i = 0; i < EXT2_NDIR; i++)
            if (in.block[i])
                ext2_w_pend_add(w, EXT2_W_PEND_BLOCKS, in.block[i], 1);
        for (uint32_t k = 0; k < 3 && !r; k++)
            r = ext2_w_free_tree(w, 0, in.block[EXT2_IND + k], k + 1u, 0);
        if (r)
            return r;
    }
    if (in.file_acl) {
        r = ext2_w_bread(w, in.file_acl, w->xb);
        if (r)
            return r;
        if (ext2_w_le32(w->xb) == EXT2_W_EA_MAGIC && ext2_w_le32(w->xb + 4) > 1u)
            ext2_w_pend_add(w, EXT2_W_PEND_ATTR, in.file_acl, 1);
        else
            ext2_w_pend_add(w, EXT2_W_PEND_BLOCKS, in.file_acl, 1);
    }
    in.links = 0;
    in.dtime = ext2_w_now(w);
    in.size = 0;
    in.blocks512 = 0;
    in.file_acl = 0;
    for (uint32_t i = 0; i < EXT2_NBLOCKS; i++)
        in.block[i] = 0;
    r = ext2_w_iput(w, &in);
    if (!r && dir)
        r = ext2_w_gd_add(w, ext2_w_igroup(w, ino), 0, 0, -1);
    if (!r)
        ext2_w_pend_add(w, EXT2_W_PEND_INODE, ino, 1);
    return r;
}

/* A shared attribute block's reference count lowered (the inode that used
 * it was written as deleted in the save that has just finished). */
static int ext2_w_attr_down(struct ext2_wfs *w, uint32_t blk)
{
    int r = ext2_w_bread(w, blk, w->xb);
    if (r)
        return r;
    uint32_t refs = ext2_w_le32(w->xb + 4);
    if (ext2_w_le32(w->xb) != EXT2_W_EA_MAGIC || refs <= 1u) {
        ext2_w_pend_add(w, EXT2_W_PEND_BLOCKS, blk, 1);
        return EXT2_OK;
    }
    ext2_w_p32(w->xb + 4, refs - 1u);
    return ext2_w_bput(w, blk, w->xb, BLK_SV_INODE);
}

static void ext2_w_fail(struct ext2_wfs *w, const char *why)
{
    w->failed = true;
    w->rw = false;
    w->ro_why = why;
    /* D9: the error state written once, if writing still works. */
    uint8_t *s = w->xb;
    ext2_w_copy(s, w->fs.sb, EXT2_SB_SIZE);
    ext2_w_p16(s + 58, w->fs.state | EXT2_STATE_ERROR);
    if (w->sv->ops->write(w->sv->ops->ctx, EXT2_SB_OFFSET, EXT2_SB_SIZE, s, BLK_SV_SUPER) == 0)
        (void)w->sv->ops->flush(w->sv->ops->ctx);
}

static void ext2_w_after_save(struct ext2_wfs *w)
{
    uint32_t n = w->pend_n;
    int r = EXT2_OK;
    w->saves_done++;
    w->fresh_n = 0;
    w->pend_floor = n;
    for (uint32_t i = 0; i < n && !r; i++) {
        struct ext2_w_pend e = w->pend[i];
        switch (e.kind) {
        case EXT2_W_PEND_BLOCKS:
            r = ext2_w_blocks_clear(w, e.num, e.count, false);
            break;
        case EXT2_W_PEND_INODE:
            r = ext2_w_inode_clear(w, e.num, false, false);
            break;
        case EXT2_W_PEND_NAME:
            r = ext2_w_name_gone(w, e.num);
            break;
        case EXT2_W_PEND_ATTR:
            r = ext2_w_attr_down(w, e.num);
            break;
        default:
            r = EXT2_EINVAL;
            break;
        }
    }
    uint32_t m = w->pend_n - n;
    for (uint32_t k = 0; k < m; k++)
        w->pend[k] = w->pend[n + k];
    w->pend_n = m;
    w->pend_floor = 0;
    if (r)
        ext2_w_fail(w, "a step after a save failed");
}

int ext2_write_save_step(struct ext2_wfs *w, uint32_t max_cmds, bool *done)
{
    *done = false;
    if (w->failed)
        return EXT2_EIO;
    if (!w->rw) {
        *done = true;
        return EXT2_OK;
    }
    bool fin = false;
    if (blk_sv_save_step(w->sv, max_cmds, &fin) != 0) {
        ext2_w_fail(w, "a save failed");
        return EXT2_EIO;
    }
    if (!fin)
        return EXT2_OK;
    ext2_w_after_save(w);
    *done = true;
    return w->failed ? EXT2_EIO : EXT2_OK;
}

int ext2_write_save(struct ext2_wfs *w)
{
    bool done = false;
    int r = ext2_write_save_step(w, 0xFFFFFFFFu, &done);
    return r ? r : (done ? EXT2_OK : EXT2_EIO);
}

bool ext2_write_saving(const struct ext2_wfs *w)
{
    return w->sv->saving;
}

void ext2_write_forget(struct ext2_wfs *w, uint32_t ino)
{
    if (w->rw && !w->failed && ino)
        ext2_w_pend_add(w, EXT2_W_PEND_NAME, ino, 1);
}

int ext2_write_mark_clean(struct ext2_wfs *w)
{
    if (!w->rw || w->failed)
        return w->failed ? EXT2_EIO : EXT2_OK;
    if (w->sv->saving)
        return EXT2_EAGAIN;
    w->fs.state |= EXT2_STATE_VALID;
    w->fs.state_clean = true;
    return ext2_w_sb_put(w);
}

bool ext2_write_busy(const struct ext2_wfs *w)
{
    return blk_sv_dirty(w->sv) != 0 || w->pend_n != 0;
}

int ext2_write_sync(struct ext2_wfs *w)
{
    for (uint32_t k = 0; k < 8u; k++) {
        if (!ext2_write_busy(w) || !w->rw)
            return w->failed ? EXT2_EIO : EXT2_OK;
        int r = ext2_write_save(w);
        if (r)
            return r;
    }
    return ext2_write_busy(w) ? EXT2_EAGAIN : EXT2_OK;
}

int ext2_write_unmount(struct ext2_wfs *w)
{
    if (!w->rw)
        return w->failed ? EXT2_EIO : EXT2_OK;
    int r = ext2_write_sync(w);
    if (r)
        return r;
    r = ext2_write_mark_clean(w);
    if (r)
        return r;
    return ext2_write_save(w);
}

int ext2_write_recount(struct ext2_wfs *w)
{
    struct ext2_fs *fs = &w->fs;
    uint64_t tb = 0, ti = 0;
    for (uint32_t g = 0; g < fs->groups; g++) {
        struct ext2_gd gd = { 0, 0, 0, 0, 0, 0 };
        int r = ext2_group(fs, g, &gd);
        if (!r)
            r = ext2_w_bread(w, gd.block_bitmap, w->bm);
        if (r)
            return r;
        uint32_t nb = ext2_w_group_blocks(fs, g), ni = ext2_w_group_inodes(fs, g), fb = 0, fi = 0, nd = 0;
        for (uint32_t i = 0; i < nb; i++)
            fb += (w->bm[i / 8u] & (1u << (i % 8u))) ? 0u : 1u;
        r = ext2_w_bread(w, gd.inode_bitmap, w->bm);
        if (r)
            return r;
        uint32_t loaded = 0;
        bool have = false;
        for (uint32_t i = 0; i < ni; i++) {
            fi += (w->bm[i / 8u] & (1u << (i % 8u))) ? 0u : 1u;
            uint64_t byte = (uint64_t)i * fs->inode_size;
            uint32_t blk = gd.inode_table + (uint32_t)(byte / fs->block_size);
            if (!have || blk != loaded) {
                r = ext2_w_bread(w, blk, w->xb);
                if (r)
                    return r;
                loaded = blk;
                have = true;
            }
            const uint8_t *p = w->xb + byte % fs->block_size;
            if ((ext2_w_le16(p) & EXT2_S_IFMT) == EXT2_S_IFDIR && ext2_w_le16(p + 26) != 0)
                nd++;
        }
        uint64_t byte = (uint64_t)g * EXT2_GD_SIZE;
        uint32_t gblk = fs->first_data_block + 1u + (uint32_t)(byte / fs->block_size);
        r = ext2_w_bread(w, gblk, w->gb);
        if (r)
            return r;
        uint8_t *p = w->gb + byte % fs->block_size;
        ext2_w_p16(p + 12, fb);
        ext2_w_p16(p + 14, fi);
        ext2_w_p16(p + 16, nd);
        r = ext2_w_bput(w, gblk, w->gb, BLK_SV_SUPER);
        if (r)
            return r;
        tb += fb;
        ti += fi;
    }
    fs->free_blocks = (uint32_t)tb;
    fs->free_inodes = (uint32_t)ti;
    return ext2_w_sb_put(w);
}

int ext2_write_mount(struct ext2_wfs *w, struct blk_sv *sv, uint64_t part_bytes, const struct ext2_w_ops *ops,
                     bool want_rw)
{
    ext2_w_zero(w, sizeof *w);
    w->sv = sv;
    if (ops)
        w->wops = *ops;
    struct ext2_ops rd = { ext2_w_rd, w };
    int r = ext2_mount(&w->fs, &rd, part_bytes);
    if (r)
        return r;
    struct ext2_fs *fs = &w->fs;
    w->state_read = fs->state;                      /* stage 10: as read, for the mount's line */
    w->clean_read = fs->state_clean;
    uint64_t lim = (fs->rev >= EXT2_REV_DYNAMIC && (fs->ro_compat & EXT2_RO_COMPAT_LARGE_FILE)) ? 0xFFFFFFFFull
                                                                                              : 0x7FFFFFFFull;
    w->file_max = ext2_max_size(fs) < lim ? ext2_max_size(fs) : lim;
    w->gen = ext2_w_now(w);
    if (!want_rw)
        w->ro_why = "mounted read-only";
    else if (fs->readonly)
        w->ro_why = "an unknown RO_COMPAT flag";
    else if (fs->compat_unknown)
        w->ro_why = "an unknown COMPAT flag";
    else if (fs->state_error)
        w->ro_why = "the error bit is set";
    else if (sv->failed)
        w->ro_why = "the disk failed";
    if (w->ro_why)
        return EXT2_OK;
    w->rw = true;
    if (!fs->state_clean) {
        r = ext2_write_recount(w);                  /* D12 */
        if (r) {
            ext2_w_fail(w, "the recount failed");
            return EXT2_OK;
        }
        w->recounted = true;
    }
    fs->state &= (uint16_t)~EXT2_STATE_VALID;
    fs->state_clean = false;
    ext2_w_p32(fs->sb + 44, ext2_w_now(w));         /* s_mtime */
    r = ext2_w_sb_put(w);
    if (r) {
        ext2_w_fail(w, "the superblock could not be changed");
        return EXT2_OK;
    }
    (void)ext2_write_save(w);                       /* save 0: "not clean" on the disk */
    return EXT2_OK;
}

const char *ext2_write_err_str(int err)
{
    switch (err) {
    case EXT2_EEXIST:       return "EEXIST";
    case EXT2_ENOSPC:       return "ENOSPC";
    case EXT2_EAGAIN:       return "EAGAIN";
    case EXT2_EROFS:        return "EROFS";
    case EXT2_ENOTEMPTY:    return "ENOTEMPTY";
    case EXT2_EISDIR:       return "EISDIR";
    case EXT2_EINVAL:       return "EINVAL";
    case EXT2_EFBIG:        return "EFBIG";
    case EXT2_EMLINK:       return "EMLINK";
    case EXT2_ENAMETOOLONG: return "ENAMETOOLONG";
    default:                return ext2_err_str(err);
    }
}
