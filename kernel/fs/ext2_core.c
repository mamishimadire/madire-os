/* ext2, the read core (1.4 stage 6): see ext2_core.h. Pure and read only;
 * not linked into the kernel in stage 6. Every helper is ext2_-prefixed and
 * every byte loop is written out (no libc call, so the kernel's flags need
 * nothing more: part static checks it). */
#include "kernel/fs/ext2_core.h"

static uint16_t ext2_le16(const uint8_t *p)
{
    return (uint16_t)((uint32_t)p[0] | (uint32_t)p[1] << 8);
}

static uint32_t ext2_le32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static void ext2_zero(void *p, uint64_t n)
{
    uint8_t *b = p;
    for (uint64_t i = 0; i < n; i++)
        b[i] = 0;
}

/* Records the problem (static text) and returns the error. */
static int ext2_err(struct ext2_fs *fs, int err, const char *why)
{
    fs->why = why;
    return err;
}

/* Block `blk` into `buf` unless it already holds it. */
static int ext2_load(struct ext2_fs *fs, uint32_t blk, uint8_t *buf, bool *valid, uint32_t *key, const char *beyond)
{
    if (*valid && *key == blk)
        return EXT2_OK;
    if (blk >= fs->blocks_count)
        return ext2_err(fs, EXT2_ECORRUPT, beyond);
    *valid = false;
    if (fs->ops.read(fs->ops.ctx, (uint64_t)blk * fs->block_size, fs->block_size, buf) != 0)
        return ext2_err(fs, EXT2_EIO, "a block could not be read");
    *valid = true;
    *key = blk;
    return EXT2_OK;
}

uint64_t ext2_max_size(const struct ext2_fs *fs)
{
    return fs->max_blocks * fs->block_size;
}

const char *ext2_err_str(int err)
{
    switch (err) {
    case EXT2_OK:           return "ok";
    case EXT2_EIO:          return "EIO";
    case EXT2_ESUPER:       return "ESUPER";
    case EXT2_EINCOMPAT:    return "EINCOMPAT";
    case EXT2_ECORRUPT:     return "ECORRUPT";
    case EXT2_EINODE:       return "EINODE";
    case EXT2_ENOTDIR:      return "ENOTDIR";
    case EXT2_ENOENT:       return "ENOENT";
    case EXT2_ERANGE:       return "ERANGE";
    default:                return "unknown";
    }
}

/* Group descriptor g, from the table in the blocks after the superblock's
 * (the block after first_data_block: block 2 with 1 KiB blocks, else 1). */
static int ext2_group_read(struct ext2_fs *fs, uint32_t g, struct ext2_gd *out)
{
    if (g >= fs->groups)
        return ext2_err(fs, EXT2_ECORRUPT, "group descriptor: group number beyond the group count");
    uint64_t byte = (uint64_t)g * EXT2_GD_SIZE;
    uint32_t blk = fs->first_data_block + 1u + (uint32_t)(byte / fs->block_size);
    uint32_t within = (uint32_t)(byte % fs->block_size);
    int r = ext2_load(fs, blk, fs->gdt, &fs->gdt_valid, &fs->gdt_block, "group descriptor: table beyond the last block");
    if (r)
        return r;
    const uint8_t *p = fs->gdt + within;
    out->block_bitmap = ext2_le32(p);
    out->inode_bitmap = ext2_le32(p + 4);
    out->inode_table = ext2_le32(p + 8);
    out->free_blocks = ext2_le16(p + 12);
    out->free_inodes = ext2_le16(p + 14);
    out->used_dirs = ext2_le16(p + 16);
    return EXT2_OK;
}

int ext2_group(struct ext2_fs *fs, uint32_t g, struct ext2_gd *out)
{
    return ext2_group_read(fs, g, out);
}

int ext2_mount(struct ext2_fs *fs, const struct ext2_ops *ops, uint64_t part_bytes)
{
    ext2_zero(fs, sizeof *fs);
    fs->ops.read = ops->read;
    fs->ops.ctx = ops->ctx;
    fs->part_bytes = part_bytes;
    if (part_bytes < (uint64_t)EXT2_SB_OFFSET + EXT2_SB_SIZE)
        return ext2_err(fs, EXT2_ESUPER, "superblock: the partition is smaller than 2 KiB");
    if (fs->ops.read(fs->ops.ctx, EXT2_SB_OFFSET, EXT2_SB_SIZE, fs->sb) != 0)
        return ext2_err(fs, EXT2_EIO, "superblock: could not be read");
    const uint8_t *s = fs->sb;
    if (ext2_le16(s + 56) != EXT2_MAGIC)
        return ext2_err(fs, EXT2_ESUPER, "superblock: no ext2 magic (0xEF53)");
    fs->rev = ext2_le32(s + 76);
    fs->minor_rev = ext2_le16(s + 62);
    if (fs->rev > EXT2_REV_DYNAMIC)
        return ext2_err(fs, EXT2_ESUPER, "superblock: revision not 0 or 1");
    fs->state = ext2_le16(s + 58);
    fs->errors = ext2_le16(s + 60);
    fs->state_clean = (fs->state & EXT2_STATE_VALID) != 0;
    fs->state_error = (fs->state & EXT2_STATE_ERROR) != 0;

    /* Revision 0 has no feature fields: its fixed values. */
    if (fs->rev == EXT2_REV_DYNAMIC) {
        fs->first_ino = ext2_le32(s + 84);
        fs->inode_size = ext2_le16(s + 88);
        fs->compat = ext2_le32(s + 92);
        fs->incompat = ext2_le32(s + 96);
        fs->ro_compat = ext2_le32(s + 100);
        fs->reserved_gdt = ext2_le16(s + 206);
    } else {
        fs->first_ino = EXT2_GOOD_OLD_FIRST_INO;
        fs->inode_size = EXT2_GOOD_OLD_INODE_SIZE;
    }
    /* The feature policy (D11), before the geometry: an unknown INCOMPAT
     * flag means the layout itself may differ. */
    fs->incompat_unknown = fs->incompat & ~(uint32_t)EXT2_INCOMPAT_KNOWN;
    fs->ro_compat_unknown = fs->ro_compat & ~(uint32_t)EXT2_RO_COMPAT_KNOWN;
    fs->compat_unknown = fs->compat & ~(uint32_t)EXT2_COMPAT_KNOWN;
    if (fs->incompat_unknown)
        return ext2_err(fs, EXT2_EINCOMPAT, "features: an unknown INCOMPAT flag (not mounted)");
    fs->readonly = fs->ro_compat_unknown != 0;
    fs->filetype = (fs->incompat & EXT2_INCOMPAT_FILETYPE) != 0;

    /* The geometry. */
    fs->log_block_size = ext2_le32(s + 24);
    if (fs->log_block_size > EXT2_LOG_BLOCK_MAX)
        return ext2_err(fs, EXT2_ESUPER, "superblock: block size above 4096 bytes");
    fs->block_size = EXT2_BLOCK_MIN << fs->log_block_size;
    fs->ptrs = fs->block_size / 4u;
    fs->inodes_count = ext2_le32(s);
    fs->blocks_count = ext2_le32(s + 4);
    fs->r_blocks_count = ext2_le32(s + 8);
    fs->free_blocks = ext2_le32(s + 12);
    fs->free_inodes = ext2_le32(s + 16);
    fs->first_data_block = ext2_le32(s + 20);
    fs->blocks_per_group = ext2_le32(s + 32);
    fs->inodes_per_group = ext2_le32(s + 40);
    if (fs->first_data_block != (fs->block_size == EXT2_BLOCK_MIN ? 1u : 0u))
        return ext2_err(fs, EXT2_ESUPER, "superblock: first data block not 1 (1 KiB blocks) or 0 (larger)");
    if (fs->blocks_count <= fs->first_data_block)
        return ext2_err(fs, EXT2_ESUPER, "superblock: block count too small");
    if ((uint64_t)fs->blocks_count * fs->block_size > part_bytes)
        return ext2_err(fs, EXT2_ESUPER, "superblock: the filesystem is larger than its partition");
    if (fs->blocks_per_group == 0 || fs->blocks_per_group > 8u * fs->block_size)
        return ext2_err(fs, EXT2_ESUPER, "superblock: blocks per group not 1 to 8 x block size");
    if (fs->inodes_per_group == 0 || fs->inodes_per_group > 8u * fs->block_size)
        return ext2_err(fs, EXT2_ESUPER, "superblock: inodes per group not 1 to 8 x block size");
    if (fs->inode_size != 128u && fs->inode_size != 256u)
        return ext2_err(fs, EXT2_ESUPER, "superblock: inode size not 128 or 256");
    uint64_t groups = ((uint64_t)fs->blocks_count - fs->first_data_block + fs->blocks_per_group - 1u) /
                      fs->blocks_per_group;
    fs->groups = (uint32_t)groups;
    if (fs->inodes_count == 0 || (uint64_t)fs->inodes_count > groups * fs->inodes_per_group)
        return ext2_err(fs, EXT2_ESUPER, "superblock: inode count not 1 to groups x inodes per group");
    if (fs->first_ino < EXT2_GOOD_OLD_FIRST_INO || fs->first_ino > fs->inodes_count)
        return ext2_err(fs, EXT2_ESUPER, "superblock: first inode not 11 to the inode count");
    fs->itable_blocks = (uint32_t)(((uint64_t)fs->inodes_per_group * fs->inode_size + fs->block_size - 1u) /
                                   fs->block_size);
    fs->gdt_blocks = (uint32_t)((groups * EXT2_GD_SIZE + fs->block_size - 1u) / fs->block_size);
    if ((uint64_t)fs->first_data_block + 1u + fs->gdt_blocks > fs->blocks_count)
        return ext2_err(fs, EXT2_ESUPER, "superblock: the group descriptors lie beyond the last block");
    uint64_t p = fs->ptrs;
    fs->max_blocks = EXT2_NDIR + p + p * p + p * p * p;
    for (uint32_t i = 0; i < 16; i++)
        fs->uuid[i] = s[104 + i];
    for (uint32_t i = 0; i < 16; i++) {
        uint8_t c = s[120 + i];
        fs->label[i] = (char)(c == 0 ? 0 : (c >= 0x20 && c < 0x7F ? c : '?'));
    }
    fs->label[16] = 0;

    /* Every group descriptor: its bitmaps and inode table inside the
     * filesystem. */
    for (uint32_t g = 0; g < fs->groups; g++) {
        struct ext2_gd gd = { 0, 0, 0, 0, 0, 0 };
        int r = ext2_group_read(fs, g, &gd);
        if (r) {
            fs->bad_group = g;
            return r;
        }
        if (gd.block_bitmap < fs->first_data_block || gd.block_bitmap >= fs->blocks_count ||
            gd.inode_bitmap < fs->first_data_block || gd.inode_bitmap >= fs->blocks_count) {
            fs->bad_group = g;
            return ext2_err(fs, EXT2_ECORRUPT, "group descriptor: a bitmap beyond the last block");
        }
        if (gd.inode_table < fs->first_data_block ||
            (uint64_t)gd.inode_table + fs->itable_blocks > fs->blocks_count) {
            fs->bad_group = g;
            return ext2_err(fs, EXT2_ECORRUPT, "group descriptor: the inode table beyond the last block");
        }
        fs->gd_free_blocks += gd.free_blocks;
        fs->gd_free_inodes += gd.free_inodes;
        fs->gd_used_dirs += gd.used_dirs;
    }

    struct ext2_inode root;
    ext2_zero(&root, sizeof root);
    int r = ext2_inode_get(fs, EXT2_ROOT_INO, &root);
    if (r)
        return r;
    if ((root.mode & EXT2_S_IFMT) != EXT2_S_IFDIR)
        return ext2_err(fs, EXT2_ECORRUPT, "root inode: not a folder");
    fs->why = 0;
    return EXT2_OK;
}

int ext2_inode_get(struct ext2_fs *fs, uint32_t ino, struct ext2_inode *out)
{
    if (ino == 0 || ino > fs->inodes_count)
        return ext2_err(fs, EXT2_EINODE, "inode: number out of range");
    uint32_t g = (ino - 1u) / fs->inodes_per_group;
    uint32_t idx = (ino - 1u) % fs->inodes_per_group;
    struct ext2_gd gd = { 0, 0, 0, 0, 0, 0 };
    int r = ext2_group_read(fs, g, &gd);
    if (r)
        return r;
    if (gd.inode_table < fs->first_data_block ||
        (uint64_t)gd.inode_table + fs->itable_blocks > fs->blocks_count)
        return ext2_err(fs, EXT2_ECORRUPT, "group descriptor: the inode table beyond the last block");
    uint64_t byte = (uint64_t)idx * fs->inode_size;
    uint32_t blk = gd.inode_table + (uint32_t)(byte / fs->block_size);
    uint32_t within = (uint32_t)(byte % fs->block_size);
    r = ext2_load(fs, blk, fs->itb, &fs->itb_valid, &fs->itb_block, "inode table: beyond the last block");
    if (r)
        return r;
    const uint8_t *p = fs->itb + within;
    out->ino = ino;
    out->mode = ext2_le16(p);
    out->uid = ext2_le16(p + 2);
    out->size = ext2_le32(p + 4);
    out->atime = ext2_le32(p + 8);
    out->ctime = ext2_le32(p + 12);
    out->mtime = ext2_le32(p + 16);
    out->dtime = ext2_le32(p + 20);
    out->gid = ext2_le16(p + 24);
    out->links = ext2_le16(p + 26);
    out->blocks512 = ext2_le32(p + 28);
    out->flags = ext2_le32(p + 32);
    for (uint32_t i = 0; i < EXT2_NBLOCKS; i++)
        out->block[i] = ext2_le32(p + 40 + 4u * i);
    out->generation = ext2_le32(p + 100);
    out->file_acl = ext2_le32(p + 104);
    /* Revision 1: for a regular file, i_dir_acl holds the size's high 32
     * bits (large_file). */
    if (fs->rev >= EXT2_REV_DYNAMIC && (out->mode & EXT2_S_IFMT) == EXT2_S_IFREG)
        out->size |= (uint64_t)ext2_le32(p + 108) << 32;
    return EXT2_OK;
}

int ext2_bmap(struct ext2_fs *fs, const struct ext2_inode *in, uint64_t lblk, uint32_t *pblk)
{
    uint64_t p = fs->ptrs;
    uint32_t idx[3] = { 0, 0, 0 };
    uint32_t depth = 0;
    uint32_t cur = 0;
    *pblk = 0;
    if (lblk < EXT2_NDIR) {
        cur = in->block[lblk];
    } else if ((lblk -= EXT2_NDIR) < p) {
        cur = in->block[EXT2_IND];
        depth = 1;
        idx[0] = (uint32_t)lblk;
    } else if ((lblk -= p) < p * p) {
        cur = in->block[EXT2_DIND];
        depth = 2;
        idx[0] = (uint32_t)(lblk / p);
        idx[1] = (uint32_t)(lblk % p);
    } else if ((lblk -= p * p) < p * p * p) {
        cur = in->block[EXT2_TIND];
        depth = 3;
        idx[0] = (uint32_t)(lblk / (p * p));
        idx[1] = (uint32_t)((lblk / p) % p);
        idx[2] = (uint32_t)(lblk % p);
    } else {
        return ext2_err(fs, EXT2_ERANGE, "block map: beyond the triple indirect block");
    }
    /* One level at a time; the depth is fixed by the position. */
    for (uint32_t d = 0; d < depth; d++) {
        if (cur == 0)
            return EXT2_OK;                     /* a hole */
        int r = ext2_load(fs, cur, fs->ind[d], &fs->ind_valid[d], &fs->ind_block[d],
                          "indirect block: beyond the last block");
        if (r)
            return r;
        cur = ext2_le32(fs->ind[d] + 4u * idx[d]);
    }
    if (cur >= fs->blocks_count)
        return ext2_err(fs, EXT2_ECORRUPT, "block map: a data block beyond the last block");
    *pblk = cur;
    return EXT2_OK;
}

bool ext2_fast_link(const struct ext2_fs *fs, const struct ext2_inode *in)
{
    if ((in->mode & EXT2_S_IFMT) != EXT2_S_IFLNK || in->size >= EXT2_FAST_LINK_MAX)
        return false;
    /* No data block: i_blocks counts only an attribute block, if any. */
    uint32_t ea = in->file_acl ? fs->block_size / 512u : 0u;
    return in->blocks512 == ea;
}

int64_t ext2_read(struct ext2_fs *fs, const struct ext2_inode *in, uint64_t off, uint8_t *buf, uint32_t len)
{
    if (off >= in->size)
        return 0;
    uint64_t n = in->size - off;
    if (n > len)
        n = len;
    if (ext2_fast_link(fs, in)) {
        for (uint64_t i = 0; i < n; i++) {
            uint64_t j = off + i;
            buf[i] = (uint8_t)(in->block[j / 4u] >> (8u * (uint32_t)(j % 4u)));
        }
        return (int64_t)n;
    }
    if (in->size > ext2_max_size(fs))
        return ext2_err(fs, EXT2_ECORRUPT, "inode: size beyond what the block map can address");
    uint32_t bs = fs->block_size;
    uint64_t done = 0;
    while (done < n) {
        uint64_t pos = off + done;
        uint32_t within = (uint32_t)(pos % bs);
        uint64_t chunk = bs - within;
        if (chunk > n - done)
            chunk = n - done;
        uint32_t pb = 0;
        int r = ext2_bmap(fs, in, pos / bs, &pb);
        if (r)
            return r;
        if (pb == 0)
            ext2_zero(buf + done, chunk);
        else if (fs->ops.read(fs->ops.ctx, (uint64_t)pb * bs + within, (uint32_t)chunk, buf + done) != 0)
            return ext2_err(fs, EXT2_EIO, "data block: could not be read");
        done += chunk;
    }
    return (int64_t)n;
}

int ext2_dir_next(struct ext2_fs *fs, const struct ext2_inode *dir, struct ext2_dir_iter *it,
                  struct ext2_dirent *out)
{
    uint32_t bs = fs->block_size;
    if ((dir->mode & EXT2_S_IFMT) != EXT2_S_IFDIR)
        return ext2_err(fs, EXT2_ENOTDIR, "not a folder");
    if (dir->size % bs)
        return ext2_err(fs, EXT2_ECORRUPT, "folder: size not a multiple of the block size");
    if (dir->size > EXT2_DIR_MAX || dir->size > (uint64_t)fs->blocks_count * bs)
        return ext2_err(fs, EXT2_ECORRUPT, "folder: size beyond the filesystem");
    while (it->off < dir->size) {
        uint32_t pos = (uint32_t)(it->off % bs);
        if (pos % 4u || pos + EXT2_DIRENT_HEAD > bs)
            return ext2_err(fs, EXT2_ECORRUPT, "folder entry: not on a 4-byte boundary inside its block");
        uint32_t pb = 0;
        int r = ext2_bmap(fs, dir, it->off / bs, &pb);
        if (r)
            return r;
        if (pb == 0)
            return ext2_err(fs, EXT2_ECORRUPT, "folder: a hole");
        r = ext2_load(fs, pb, fs->dirb, &fs->dirb_valid, &fs->dirb_block, "folder: a block beyond the last block");
        if (r)
            return r;
        const uint8_t *e = fs->dirb + pos;
        uint32_t ino = ext2_le32(e);
        uint32_t rec = ext2_le16(e + 4);
        uint32_t nl = fs->filetype ? e[6] : ext2_le16(e + 6);
        uint8_t ft = fs->filetype ? e[7] : 0;
        if (rec < EXT2_DIRENT_MIN || rec % 4u || pos + rec > bs)
            return ext2_err(fs, EXT2_ECORRUPT, "folder entry: rec_len not a multiple of 4, at least 12, inside its block");
        if (ino != 0) {
            if (nl == 0 || nl > EXT2_NAME_MAX || EXT2_DIRENT_HEAD + nl > rec)
                return ext2_err(fs, EXT2_ECORRUPT, "folder entry: name_len not 1 to rec_len - 8");
            if (ino > fs->inodes_count)
                return ext2_err(fs, EXT2_ECORRUPT, "folder entry: inode number out of range");
            for (uint32_t i = 0; i < nl; i++)
                if (e[EXT2_DIRENT_HEAD + i] == 0 || e[EXT2_DIRENT_HEAD + i] == '/')
                    return ext2_err(fs, EXT2_ECORRUPT, "folder entry: a name with '/' or NUL");
        }
        uint64_t here = it->off;
        it->off += rec;
        if (ino == 0)
            continue;                           /* an unused slot (or an index block) */
        out->ino = ino;
        out->rec_len = (uint16_t)rec;
        out->name_len = (uint8_t)nl;
        out->file_type = ft;
        out->off = here;
        for (uint32_t i = 0; i < nl; i++)
            out->name[i] = (char)e[EXT2_DIRENT_HEAD + i];
        out->name[nl] = 0;
        return 1;
    }
    return 0;
}

int ext2_lookup(struct ext2_fs *fs, const struct ext2_inode *dir, const char *name, uint32_t len,
                uint32_t *ino)
{
    struct ext2_dir_iter it;
    struct ext2_dirent de;
    ext2_zero(&de, sizeof de);
    *ino = 0;
    it.off = 0;
    if (len == 0 || len > EXT2_NAME_MAX)
        return ext2_err(fs, EXT2_ENOENT, "no such name");
    for (;;) {
        int r = ext2_dir_next(fs, dir, &it, &de);
        if (r < 0)
            return r;
        if (r == 0)
            return ext2_err(fs, EXT2_ENOENT, "no such name");
        if (de.name_len != len)
            continue;
        uint32_t i = 0;
        while (i < len && de.name[i] == name[i])
            i++;
        if (i == len) {
            *ino = de.ino;
            return EXT2_OK;
        }
    }
}
