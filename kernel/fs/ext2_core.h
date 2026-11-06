#ifndef MADIRE_FS_EXT2_CORE_H
#define MADIRE_FS_EXT2_CORE_H

/*
 * ext2, the read core (1.4 stage 6; docs/plans/1.4.md, D11, D12, C7).
 * PURE and READ ONLY: no hardware access, printing or allocation, and NOT
 * LINKED into the kernel in stage 6 (the Makefile leaves kernel/fs/ext2_*.c
 * out of the kernel's sources; stage 8 mounts it read only at /home). Host
 * unit tests: tests/unit/test_ext2_core.c; the host tool tests/ext2dump.c is
 * built from this same file and compared with debugfs in part static.
 *
 * Written from the ext2 specification (facts only): the superblock at byte
 * 1024 of the partition (revision 0 and 1), the group descriptors (32 bytes
 * each, in the blocks after the superblock's), the inodes (128 or 256 bytes;
 * bytes after 128 are read, not used), the twelve direct blocks and the
 * single, double and triple indirect blocks, holes (a block number 0) read
 * as zeros, folders in their on-disk order (rec_len, name_len, file_type),
 * and symbolic links (a fast link's target in the inode's block array, a
 * slow link's in its data block).
 *
 * The feature policy (D11), decided by ext2_mount:
 *   INCOMPAT   only filetype is known; any other flag: NOT MOUNTED
 *              (EXT2_EINCOMPAT; Live mode in stage 8).
 *   RO_COMPAT  sparse_super and large_file are known; any other flag:
 *              mounted READ ONLY (fs->readonly, fs->ro_compat_unknown).
 *   COMPAT     ext_attr, resize_inode and dir_index are known; they change
 *              nothing in reading (an indexed folder is also a valid linear
 *              folder: the index's blocks look like unused entries, so it is
 *              read in its on-disk order; an attribute block is only
 *              reported, i_file_acl; inode 7 and the reserved descriptor
 *              blocks are only reported). Any other COMPAT flag is reported
 *              (fs->compat_unknown) and changes nothing: by the
 *              specification a COMPAT feature never stops a mount.
 *   States     the clean bit and the error bit are read and reported
 *              (fs->state_clean, fs->state_error); the decisions of D12 and
 *              C7 (the error bit: read only; not clean: read-write with a
 *              warning after a recount) are taken at the mount in stage 8.
 *
 * EVERY number read from the disk is checked before it is used (risk 31):
 * the superblock's geometry against the partition's size, each group
 * descriptor's blocks against the block count, inode numbers against the
 * inode count, block numbers against the block count at every level of
 * indirection (the depth is fixed by the position, so it cannot loop),
 * file sizes against what the block map can address, and each folder
 * entry's rec_len (a multiple of 4, at least 12, inside its block),
 * name_len (at least 1 and at most rec_len - 8 for a used entry), its
 * inode number and its name (no '/' or NUL). Malformed data gives an error
 * code and `why` (static text naming the first problem), never a crash, a
 * loop or a read outside the partition: every read through ext2_ops lies
 * inside [0, blocks_count * block_size), which ext2_mount checks is inside
 * the partition.
 *
 * Ideas from the references (docs/plans/1.4.md, section M; ideas only, no
 * code): the block-group count by ceiling division with zero refused, the
 * descriptor table's place depending on the block size, the state read at
 * mount (SerenityOS's Ext2FS); the logical-to-disk block map one level at
 * a time, a read trimmed to the file's size and copied block by block, a
 * folder walk that skips entries whose inode is 0 (xv6's fs.c). Unlike
 * either, a bad number is an error code here, never an assertion.
 */

#include <stdbool.h>
#include <stdint.h>

#define EXT2_SB_OFFSET          1024u   /* the superblock's byte in the partition */
#define EXT2_SB_SIZE            1024u
#define EXT2_MAGIC              0xEF53u
#define EXT2_BLOCK_MIN          1024u
#define EXT2_BLOCK_MAX          4096u   /* larger blocks are refused (C7) */
#define EXT2_LOG_BLOCK_MAX      2u      /* block size = 1024 << log */
#define EXT2_REV_GOOD_OLD       0u
#define EXT2_REV_DYNAMIC        1u
#define EXT2_GOOD_OLD_FIRST_INO 11u
#define EXT2_GOOD_OLD_INODE_SIZE 128u
#define EXT2_ROOT_INO           2u
#define EXT2_RESIZE_INO         7u
#define EXT2_GD_SIZE            32u
#define EXT2_NDIR               12u     /* direct blocks */
#define EXT2_IND                12u     /* i_block[] slots of the indirect blocks */
#define EXT2_DIND               13u
#define EXT2_TIND               14u
#define EXT2_NBLOCKS            15u
#define EXT2_FAST_LINK_MAX      60u     /* bytes of i_block[]: a fast link's target is shorter */
#define EXT2_NAME_MAX           255u
#define EXT2_DIRENT_HEAD        8u      /* inode, rec_len, name_len (and file_type) */
#define EXT2_DIRENT_MIN         12u     /* the smallest rec_len */
#define EXT2_DIR_MAX            0xFFFFFFFFull   /* a folder's size at most (no large_dir) */

/* Feature flags (the specification's values). */
#define EXT2_COMPAT_DIR_PREALLOC    0x0001u
#define EXT2_COMPAT_IMAGIC_INODES   0x0002u
#define EXT2_COMPAT_HAS_JOURNAL     0x0004u
#define EXT2_COMPAT_EXT_ATTR        0x0008u
#define EXT2_COMPAT_RESIZE_INODE    0x0010u
#define EXT2_COMPAT_DIR_INDEX       0x0020u
#define EXT2_COMPAT_KNOWN           (EXT2_COMPAT_EXT_ATTR | EXT2_COMPAT_RESIZE_INODE | EXT2_COMPAT_DIR_INDEX)
#define EXT2_INCOMPAT_FILETYPE      0x0002u
#define EXT2_INCOMPAT_KNOWN         EXT2_INCOMPAT_FILETYPE
#define EXT2_RO_COMPAT_SPARSE_SUPER 0x0001u
#define EXT2_RO_COMPAT_LARGE_FILE   0x0002u
#define EXT2_RO_COMPAT_KNOWN        (EXT2_RO_COMPAT_SPARSE_SUPER | EXT2_RO_COMPAT_LARGE_FILE)

#define EXT2_STATE_VALID        0x0001u /* cleanly unmounted */
#define EXT2_STATE_ERROR        0x0002u /* errors detected */

/* i_mode's file type and i_flags' index flag. */
#define EXT2_S_IFMT             0xF000u
#define EXT2_S_IFSOCK           0xC000u
#define EXT2_S_IFLNK            0xA000u
#define EXT2_S_IFREG            0x8000u
#define EXT2_S_IFBLK            0x6000u
#define EXT2_S_IFDIR            0x4000u
#define EXT2_S_IFCHR            0x2000u
#define EXT2_S_IFIFO            0x1000u
#define EXT2_INDEX_FL           0x00001000u

enum ext2_err {
    EXT2_OK = 0,
    EXT2_EIO = -1,          /* a read through ext2_ops failed */
    EXT2_ESUPER = -2,       /* the superblock fails its checks (not mounted) */
    EXT2_EINCOMPAT = -3,    /* an unknown INCOMPAT flag (not mounted, D11) */
    EXT2_ECORRUPT = -4,     /* a number on the disk is out of range */
    EXT2_EINODE = -5,       /* an inode number out of range */
    EXT2_ENOTDIR = -6,      /* not a folder */
    EXT2_ENOENT = -7,       /* no such name */
    EXT2_ERANGE = -8,       /* a block beyond what the block map can address */
};

/* Reads `len` bytes at byte `off` of the partition: 0, or not 0 if they
 * cannot be read. ext2_core never asks for bytes outside the partition. */
struct ext2_ops {
    int (*read)(void *ctx, uint64_t off, uint32_t len, uint8_t *buf);
    void *ctx;
};

struct ext2_gd {
    uint32_t block_bitmap, inode_bitmap, inode_table;
    uint16_t free_blocks, free_inodes, used_dirs;
};

struct ext2_inode {
    uint32_t ino;
    uint16_t mode, uid, gid, links;
    uint64_t size;              /* with the high 32 bits for a regular file (revision 1) */
    uint32_t atime, ctime, mtime, dtime;
    uint32_t blocks512;         /* i_blocks: 512-byte units, report only */
    uint32_t flags, file_acl, generation;
    uint32_t block[EXT2_NBLOCKS];
};

struct ext2_dirent {
    uint32_t ino;
    uint16_t rec_len;
    uint8_t name_len;
    uint8_t file_type;          /* 0 without the filetype feature */
    uint64_t off;               /* the entry's byte in the folder */
    char name[EXT2_NAME_MAX + 1];
};

/* A walk through a folder in its on-disk order: set off = 0 to start. */
struct ext2_dir_iter {
    uint64_t off;
};

struct ext2_fs {
    struct ext2_ops ops;
    uint64_t part_bytes;
    const char *why;            /* the first problem found (static text), or 0 */
    /* the superblock, as read and checked */
    uint32_t rev, minor_rev;
    uint32_t inodes_count, blocks_count, r_blocks_count, free_blocks, free_inodes;
    uint32_t first_data_block, log_block_size, block_size;
    uint32_t blocks_per_group, inodes_per_group, first_ino, inode_size;
    uint32_t compat, incompat, ro_compat;
    uint32_t reserved_gdt;      /* resize_inode: the reserved descriptor blocks (report only) */
    uint16_t state, errors;
    uint8_t uuid[16];
    char label[17];             /* ASCII ('?' for any other byte) */
    /* derived */
    uint32_t groups, gdt_blocks, itable_blocks, ptrs;   /* ptrs: block numbers per block */
    uint64_t max_blocks;        /* the block map's reach: 12 + p + p^2 + p^3 */
    bool filetype;              /* folder entries carry a file_type byte */
    /* the policy (D11) and the state (D12, reported) */
    bool readonly;              /* an unknown RO_COMPAT flag */
    uint32_t ro_compat_unknown, compat_unknown, incompat_unknown;
    bool state_clean, state_error;
    /* the group descriptors' sums (report only) and the bad group */
    uint64_t gd_free_blocks, gd_free_inodes, gd_used_dirs;
    uint32_t bad_group;
    /* buffers (each holds one block, keyed by its number) */
    bool gdt_valid, itb_valid, dirb_valid;
    uint32_t gdt_block, itb_block, dirb_block;
    bool ind_valid[3];
    uint32_t ind_block[3];
    uint8_t sb[EXT2_SB_SIZE];
    uint8_t gdt[EXT2_BLOCK_MAX];
    uint8_t itb[EXT2_BLOCK_MAX];
    uint8_t dirb[EXT2_BLOCK_MAX];
    uint8_t ind[3][EXT2_BLOCK_MAX];
};

/* Reads and checks the superblock and every group descriptor of the
 * filesystem in a partition of `part_bytes` bytes, then the root inode
 * (it must be a folder). `fs` is set from scratch. EXT2_OK (fs->readonly
 * tells the policy) or an error (fs->why). */
int ext2_mount(struct ext2_fs *fs, const struct ext2_ops *ops, uint64_t part_bytes);

/* Group descriptor `g` (0-based), checked at mount. */
int ext2_group(struct ext2_fs *fs, uint32_t g, struct ext2_gd *out);

/* Inode `ino` (1-based). */
int ext2_inode_get(struct ext2_fs *fs, uint32_t ino, struct ext2_inode *out);

/* The disk block holding the file's block `lblk`: *pblk = 0 for a hole. */
int ext2_bmap(struct ext2_fs *fs, const struct ext2_inode *in, uint64_t lblk, uint32_t *pblk);

/* Up to `len` bytes of the file at byte `off` (a fast link's target from
 * its inode): the number read (0 at or past the end), or an error. */
int64_t ext2_read(struct ext2_fs *fs, const struct ext2_inode *in, uint64_t off, uint8_t *buf, uint32_t len);

/* The next used entry of folder `dir` (unused slots, inode 0, skipped):
 * 1 with *out set, 0 at the end, or an error. */
int ext2_dir_next(struct ext2_fs *fs, const struct ext2_inode *dir, struct ext2_dir_iter *it,
                  struct ext2_dirent *out);

/* The inode number of `name` (`len` bytes) in folder `dir`. */
int ext2_lookup(struct ext2_fs *fs, const struct ext2_inode *dir, const char *name, uint32_t len,
                uint32_t *ino);

/* A fast symbolic link: its target is in i_block[] (no data block). */
bool ext2_fast_link(const struct ext2_fs *fs, const struct ext2_inode *in);

/* Forgets every kept block (the group descriptors', the inode table's, the
 * folder's and the three indirect levels'), so the next access reads the
 * disk again. For a caller that changed the disk under the core: the host
 * tests in stage 6; the write stages (S9, S10) update or drop the blocks
 * they write. */
void ext2_cache_drop(struct ext2_fs *fs);

/* The largest file size the block map can address (bytes). */
uint64_t ext2_max_size(const struct ext2_fs *fs);

const char *ext2_err_str(int err);

#endif
