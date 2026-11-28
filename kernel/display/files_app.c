/*
 * The Files app and the Text Viewer, the kernel side (1.5 stage 12); see
 * files_app.h.
 */
#include "kernel/display/files_app.h"
#include "kernel/display/pointer_core.h"   /* 1.5 stage 14b: the pointer Files asks for */
#include "kernel/display/ui_files.h"
#include "kernel/display/ui_icon.h"
#include "kernel/display/ui_list.h"
#include "kernel/display/ui_textfield.h"
#include "kernel/display/ui_viewer.h"
#include "kernel/files/files_core.h"
#include "kernel/files/files_ops.h"
#include "kernel/files/trash_core.h"
#include "kernel/fs/ext2_disk.h"
#include "kernel/fs/fs_errno.h"
#include "kernel/fs/ramfs_core.h"
#include "kernel/fs/vfs.h"
#include "kernel/fs/vfs_core.h"
#include "kernel/kprintf.h"
#include "kernel/memory/memory.h"
#include "kernel/memory/paging.h"
#include "kernel/memory/pmm.h"
#include "kernel/memory/pte.h"
#include "kernel/memory/vm_layout.h"
#include "kernel/proc/fd.h"
#include "kernel/storage/ahci.h"            /* 1.5 stage 14b: This PC's CD drive */

typedef unsigned long long ull;

#define FA_STATUS_MAX 320u
#define FA_HOLD_ENTRIES 16u
#define FA_CHUNK 4096u
#define FA_LOC_MAX 48u                  /* 1.5 stage 13: an Original location text and its NUL */
#define FA_TFILES TRASH_DIR "/files"
#define FA_TINFO  TRASH_DIR "/info"

/* The Files window's memory (at VM_FILES_BASE while it is open). */
struct fa_mem {
    struct files_entry entries[FILES_MAX_ENTRIES];
    char names[FILES_MAX_ENTRIES * (FILES_NAME_MAX + 1)];
    struct ui_list_item items[FILES_MAX_ENTRIES + 1];       /* the last: the limit's note */
    char sizes[FILES_MAX_ENTRIES][FILES_SIZE_TEXT];
    char dates[FILES_MAX_ENTRIES][FILES_DATE_TEXT];
    char hist_store[FILES_HISTORY_BYTES];
    char path[FILES_PATH_MAX + 1];          /* the folder shown */
    char rpath[FILES_PATH_MAX + 1];         /* the folder being read */
    char scratch[FILES_PATH_MAX + 1];
    char scratch2[FILES_PATH_MAX + 1];
    char status[FA_STATUS_MAX];
    char select_name[FILES_NAME_MAX + 1];   /* the item to select when the reading ends */
    char panel_size[FILES_SIZE_TEXT], panel_date[FILES_DATE_TEXT];
    struct files_folder folder;
    struct files_history hist;
    struct files_pathbar pb;
    struct ui_list list;
    struct ui_textfield field;
    struct ui_list_clicks clicks;
    struct ui_files_view view;
    /* 1.5 stage 13: the clipboard (Q8: lost when the window closes), the
     * walk (copy, delete), a file chunk, a .trashinfo file and its parse,
     * the Trash view's columns, the action's paths, the question's texts. */
    char clip_store[FILES_CLIP_BYTES];
    struct files_clip clip;
    struct files_walk walk;
    uint8_t chunk[FA_CHUNK];
    uint8_t info_buf[TRASH_INFO_MAX];
    struct trash_info info;
    char locs[FILES_MAX_ENTRIES][FA_LOC_MAX];
    uint8_t restorable[FILES_MAX_ENTRIES];
    char jsrc[FILES_PATH_MAX + 1];          /* the action's item */
    char jdst[FILES_PATH_MAX + 1];          /* where it goes */
    char jdir[FILES_PATH_MAX + 1];          /* Paste's target folder */
    char jinfo[FILES_PATH_MAX + 1];         /* the item's info file (the Trash) */
    char slot[TRASH_NAME_MAX + 1];
    char ask1[FILES_OPS_LINE_MAX], ask2[FILES_OPS_LINE_MAX], ask_label[FILES_OPS_LABEL_MAX];
    /* 1.5 stage 14b: box selection's starting selection (one flag per
     * item), a drag's target folder. */
    uint8_t band_base[FILES_MAX_ENTRIES + 1];
    char drop_path[FILES_PATH_MAX + 1];
};

/* The Text Viewer's memory (at VM_VIEWER_BASE while it is open). */
struct fa_vmem {
    uint8_t buf[UI_VIEWER_MAX];
    char title[FILES_NAME_MAX + 1];
    char path[FILES_PATH_MAX + 1];
    struct ui_viewer v;
};

#define FA_PAGES  ((uint32_t)((sizeof(struct fa_mem) + VM_PAGE_SIZE - 1) / VM_PAGE_SIZE))
#define FA_VPAGES ((uint32_t)((sizeof(struct fa_vmem) + VM_PAGE_SIZE - 1) / VM_PAGE_SIZE))
_Static_assert(sizeof(struct fa_mem) <= (uint64_t)VM_FILES_PAGES * VM_PAGE_SIZE, "Files fits VM_FILES_BASE's area");
_Static_assert(sizeof(struct fa_vmem) <= (uint64_t)VM_VIEWER_PAGES * VM_PAGE_SIZE,
               "the Text Viewer fits VM_VIEWER_BASE's area");

static struct files_app_env fa_env;
static const struct ui_files_view fa_nomem = { .mode = UI_FILES_NOMEM };
static bool fa_opt_nomem, fa_opt_fill;
static struct fa_mem *fa_m;             /* 0 while closed (or without memory) */
static struct fa_vmem *fa_v;
static bool fa_nomem_view;              /* the window shows only the message */
static uint32_t fa_pages_mapped, fa_vpages_mapped;
/* The folder being read (files_app_poll): its next entry, the entries
 * seen, the holds. */
static bool fa_reading, fa_rename_after;
static uint64_t fa_cookie;
static uint32_t fa_seen, fa_holds;
static bool fa_writable;                /* the folder shown may change */
static uint64_t fa_free_bytes;
/* The pointer: the part pressed (a button or a path part), the scroll bar
 * dragged, the tooltip's part and since when the pointer rests. */
static int32_t fa_press_path = -1;
static bool fa_sb_drag;
static enum ui_files_part fa_tip_part = UF_NONE;
static uint64_t fa_tip_since;
static int32_t fa_px = -1, fa_py = -1;
/* The Text Viewer's reading: its offset, holds, the file's size. */
static bool fa_vreading, fa_vsb_drag;
static uint64_t fa_voff, fa_vsize;
static uint32_t fa_vholds;
/* 1.5 stage 13: the action running (one step per pass), the question, the
 * Trash view's info files being read. */
enum fa_job { FA_JOB_NONE, FA_JOB_TRASH, FA_JOB_DELETE, FA_JOB_PASTE, FA_JOB_RESTORE, FA_JOB_EMPTY };
enum fa_step { FA_ST_ITEM, FA_ST_DIRS, FA_ST_SLOT, FA_ST_WRITE, FA_ST_MOVE, FA_ST_WALK, FA_ST_WALK2, FA_ST_RENAME, FA_ST_INFO, FA_ST_CHECK,
               FA_ST_PARENTS, FA_ST_RESTORE, FA_ST_UNINFO, FA_ST_ASKING, FA_ST_EFILES, FA_ST_EINFO };
enum fa_ask { FA_ASK_NONE, FA_ASK_DELETE, FA_ASK_NOTRASH, FA_ASK_EMPTY, FA_ASK_EXISTS };
static enum fa_job fa_job;
static enum fa_step fa_step;
static uint32_t fa_jnext, fa_jitem;     /* the next item to look at; the item being worked on */
static uint32_t fa_jdone, fa_junknown, fa_jk, fa_jdirs_made;
static uint64_t fa_joff;
static bool fa_jmove, fa_jall, fa_jinfo_made, fa_keep_status;
static enum fa_ask fa_ask;
static enum ui_files_part fa_ask_press = UF_NONE;
static bool fa_tinfo_on;                /* the Trash view's info files are being read */
static uint32_t fa_tinfo;
static int32_t fa_menu_item = -1;       /* the folder item a menu's Paste goes into, or -1 */
/* 1.5 stage 14b (notes (14), (15)): This PC's cards; a left press in the
 * content that is not yet a drag or a band (a press on an item already
 * selected leaves the selection until the release); box selection (its
 * anchor and point in content pixels); drag and drop (what a release
 * would do, the badge's place); the time of the last automatic scroll and
 * of the readings' starts (the busy pointer); a drop runs the Paste job
 * over the selection (fa_jdrag). */
#define FA_BUSY_TICKS 20u               /* a reading shows the busy pointer after 200 ms */
#define FA_AUTO_TICKS 10u               /* the band or a drag scrolls a line every 100 ms at an edge */
static struct ui_files_card fa_cards[UI_FILES_CARDS];
static char fa_card_names[UI_FILES_CARDS][64];
static char fa_card_figs[UI_FILES_CARDS][FILES_USAGE_TEXT];
static const char *fa_card_open[UI_FILES_CARDS];
static uint32_t fa_ncards;
enum fa_press { FA_PRESS_NONE, FA_PRESS_ITEM, FA_PRESS_SPACE };
static enum fa_press fa_press;
static int32_t fa_press_x, fa_press_y, fa_press_i;
static int64_t fa_press_cy;
static uint8_t fa_press_mods;
static bool fa_press_defer;
static bool fa_band, fa_band_add;
static int32_t fa_band_ax, fa_band_bx;
static int64_t fa_band_ay, fa_band_by;
static bool fa_drag, fa_drag_ctrl, fa_jdrag;
static uint32_t fa_drag_n;
static enum files_drop fa_drop;
static enum files_drop_why fa_drop_why;
static enum files_drop_target fa_drop_kind;
static int32_t fa_drop_item = -1, fa_drop_place = -1;
static bool fa_drop_same, fa_drop_writable;
static struct rect fa_badge_r;
static char fa_badge_text[FILES_NAME_MAX + 1];
static int fa_badge_icon = UI_ICON_NONE;
static uint64_t fa_now, fa_auto_at, fa_read_t0, fa_vread_t0;

static const char *const fa_place_paths[7] = {
    FILES_HOME, FILES_HOME "/Desktop", FILES_HOME "/Documents", FILES_HOME "/Downloads", FILES_HOME "/Pictures",
    FILES_HOME "/Music", FILES_HOME "/Videos",
};

/* ---- small helpers ---- */

static uint32_t fa_len(const char *s)
{
    uint32_t n = 0;
    while (s[n])
        n++;
    return n;
}

static bool fa_same(const char *a, const char *b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

/* dst (size bytes) = src, cut to fit; false if cut. */
static bool fa_copy(char *dst, uint32_t size, const char *src)
{
    uint32_t i = 0;
    if (size == 0)
        return false;
    for (; src[i] && i + 1 < size; i++)
        dst[i] = src[i];
    dst[i] = '\0';
    return src[i] == '\0';
}

/* out = dir "/" name (dir "/" gives "/name"); false if over FILES_PATH_MAX. */
static bool fa_join(char *out, const char *dir, const char *name)
{
    uint32_t dl = fa_len(dir), nl = fa_len(name), k = 0;
    bool root = dl == 1 && dir[0] == '/';
    if ((uint64_t)dl + (root ? 0u : 1u) + nl > FILES_PATH_MAX)
        return false;
    for (uint32_t i = 0; i < dl; i++)
        out[k++] = dir[i];
    if (!root)
        out[k++] = '/';
    for (uint32_t i = 0; i < nl; i++)
        out[k++] = name[i];
    out[k] = '\0';
    return true;
}

static void fa_damage(struct ui_window *w)
{
    if (w && w->open && fa_env.damage)
        fa_env.damage(w->r);
}

/* ---- memory (the owner's rule) ---- */

/* `pages` frames mapped from base, each zeroed through its mapping; on a
 * failure every page mapped is given back (unmapped first). fail_last:
 * filesnomem. *tables: the page tables added. */
static bool fa_area(uint64_t base, uint32_t pages, bool fail_last, uint32_t *tables, const char **why)
{
    uint32_t before = paging_stats()->page_tables, i = 0;
    *why = 0;
    if (pmm_free_pages() < pages + FILES_APP_SPARE_PAGES) {
        *why = "too few free pages";
    } else {
        for (; i < pages; i++) {
            uint32_t frame = (fail_last && i + 1u == pages) ? 0u : memory_alloc_page();
            if (!frame) {
                *why = fail_last && i + 1u == pages ? "test option filesnomem" : "no free page";
                break;
            }
            if (paging_map(base + (uint64_t)i * VM_PAGE_SIZE, frame, PTE_WRITE | paging_nx_flag()) != VMM_OK) {
                memory_free_page(frame);
                *why = "a page could not be mapped";
                break;
            }
            uint64_t *p = (uint64_t *)(uintptr_t)(base + (uint64_t)i * VM_PAGE_SIZE);
            for (uint32_t k = 0; k < VM_PAGE_SIZE / 8u; k++)
                p[k] = 0;
        }
    }
    *tables = paging_stats()->page_tables - before;
    if (!*why)
        return true;
    for (uint32_t k = 0; k < i; k++) {
        uint64_t phys = 0;
        if (paging_unmap(base + (uint64_t)k * VM_PAGE_SIZE, &phys) == VMM_OK)
            memory_free_page((uint32_t)phys);
    }
    return false;
}

/* Every page unmapped (its TLB entry invalidated by paging_unmap) before
 * its frame is given back; the pages unmapped. */
static uint32_t fa_unarea(uint64_t base, uint32_t pages)
{
    uint32_t n = 0;
    for (uint32_t k = 0; k < pages; k++) {
        uint64_t phys = 0;
        if (paging_unmap(base + (uint64_t)k * VM_PAGE_SIZE, &phys) == VMM_OK) {
            memory_free_page((uint32_t)phys);
            n++;
        }
    }
    return n;
}

/* ---- file calls (each one hold of the VFS lock) ---- */

/* The node at path (followed into a mount), or an error; the lock held. */
static int fa_lookup_locked(const char *path, struct vnode **out)
{
    struct vfs *v = vfs_root();
    struct vnode *n = 0;
    int r = vfs_lookup(v, v->root, path, &n);
    if (r == 0)
        *out = vfs_enter(v, n);
    return r;
}

/* files_exists_fn: path is a folder (a link is not one). */
static bool fa_is_folder(void *ctx, const char *path)
{
    (void)ctx;
    struct vnode *n = 0;
    fd_vfs_acquire();
    bool ok = fa_lookup_locked(path, &n) == 0 && n->type == VNODE_DIR;
    fd_vfs_release();
    return ok;
}

/* ui_name_exists_fn: the name is taken in the folder shown. */
static bool fa_name_taken(void *ctx, const char *name)
{
    (void)ctx;
    if (!fa_m || !fa_join(fa_m->scratch2, fa_m->path, name))
        return false;
    struct vnode *n = 0;
    fd_vfs_acquire();
    bool taken = fa_lookup_locked(fa_m->scratch2, &n) == 0;
    fd_vfs_release();
    return taken;
}

static int fa_create(const char *path, bool folder)
{
    uint32_t tries = 0;
    int r;
    do {
        fd_vfs_acquire();
        struct vfs *v = vfs_root();
        r = vfs_create(v, v->root, path, folder ? VNODE_DIR : VNODE_FILE, folder ? 0755u : 0644u, 0);
        fd_vfs_release();
    } while (ext2_disk_retry(&r, &tries));
    return r;
}

static int fa_rename(const char *from, const char *to)
{
    uint32_t tries = 0;
    int r;
    do {
        fd_vfs_acquire();
        struct vfs *v = vfs_root();
        r = vfs_rename(v, v->root, from, v->root, to);
        fd_vfs_release();
    } while (ext2_disk_retry(&r, &tries));
    return r;
}

/* ---- 1.5 stage 13: the file calls of the actions (struct files_fs and
 * the Trash's lstat; each call one hold, a change tried again after a save,
 * ext2_disk_retry) ---- */

/* The kind of path itself (a link is a link, never its target); the lock held. */
static enum files_kind fa_kind_locked(const char *path)
{
    struct vnode *n = 0;
    int r = fa_lookup_locked(path, &n);
    if (r == -FS_ENOENT)
        return FILES_K_NONE;
    if (r < 0)
        return FILES_K_ERROR;
    return vnode_is_link(n) ? FILES_K_LINK : n->type == VNODE_DIR ? FILES_K_DIR : FILES_K_FILE;
}

static enum files_kind fa_fs_lstat(void *ctx, const char *path)
{
    (void)ctx;
    fd_vfs_acquire();
    enum files_kind k = fa_kind_locked(path);
    fd_vfs_release();
    return k;
}

static enum trash_kind fa_trash_lstat(void *ctx, const char *path)
{
    switch (fa_fs_lstat(ctx, path)) {
    case FILES_K_NONE:
        return TRASH_K_NONE;
    case FILES_K_DIR:
        return TRASH_K_DIR;
    case FILES_K_FILE:
        return TRASH_K_FILE;
    case FILES_K_LINK:
        return TRASH_K_LINK;
    default:
        return TRASH_K_ERROR;
    }
}

/* The next entry of folder path from *cookie (never "." or ".."). */
static int fa_fs_read_dir(void *ctx, const char *path, uint64_t *cookie, char *name)
{
    (void)ctx;
    struct vfs *v = vfs_root();
    struct vnode *dir = 0;
    int out = 0;
    fd_vfs_acquire();
    int r = fa_lookup_locked(path, &dir);
    if (r == 0 && dir->type != VNODE_DIR)
        r = -FS_ENOTDIR;
    if (r < 0) {
        out = r;
    } else {
        struct vnode *e = vfs_entry_from(v, dir, *cookie);
        while (e) {
            size_t len = 0;
            const char *nm = vfs_entry_name(v, e, &len);
            *cookie = vfs_entry_pos(v, e) + 1;
            bool dots = (len == 1 && nm[0] == '.') || (len == 2 && nm[0] == '.' && nm[1] == '.');
            if (!dots && len >= 1 && len <= FILES_NAME_MAX) {
                for (size_t i = 0; i < len; i++)
                    name[i] = nm[i];
                name[len] = '\0';
                out = 1;
                break;
            }
            e = vfs_entry_next(v, dir, e);
        }
        if (!e) {
            int err = vfs_lookup_error(v, dir);
            out = err ? err : 0;
        }
    }
    fd_vfs_release();
    return out;
}

static int fa_fs_mkdir(void *ctx, const char *path)
{
    (void)ctx;
    return fa_create(path, true);
}

static int fa_fs_unlink(void *ctx, const char *path)
{
    (void)ctx;
    uint32_t tries = 0;
    int r;
    do {
        fd_vfs_acquire();
        struct vfs *v = vfs_root();
        r = vfs_unlink(v, v->root, path);
        fd_vfs_release();
    } while (ext2_disk_retry(&r, &tries));
    return r;
}

static int fa_fs_rmdir(void *ctx, const char *path)
{
    (void)ctx;
    uint32_t tries = 0;
    int r;
    do {
        fd_vfs_acquire();
        struct vfs *v = vfs_root();
        r = vfs_rmdir(v, v->root, path);
        fd_vfs_release();
    } while (ext2_disk_retry(&r, &tries));
    return r;
}

/* One chunk of file src at offset into dst (made, empty and exclusively,
 * at offset 0): the bytes copied, 0 at src's end, or an error. A short
 * write is not an error: its count is returned and the walk asks for the
 * rest at the next offset; a write that moves nothing is a full disk
 * (-FS_ENOSPC, as ext2_disk_retry's last answer), every other failure
 * keeps its own error (1.5 stage 13, the owner's rule after run
 * 44874459260). */
static int64_t fa_fs_copy_chunk(void *ctx, const char *src, const char *dst, uint64_t offset)
{
    (void)ctx;
    if (!fa_m)
        return -FS_EINVAL;
    if (offset == 0) {
        int c = fa_create(dst, false);
        if (c < 0)
            return c;
    }
    uint32_t tries = 0;
    int r;
    int64_t got = 0;
    do {
        struct vfs *v = vfs_root();
        struct vnode *s = 0, *d = 0;
        fd_vfs_acquire();
        r = fa_lookup_locked(src, &s);
        if (r == 0 && (s->type != VNODE_FILE || vnode_is_link(s)))
            r = -FS_EINVAL;             /* never a folder, never a link (rule SL) */
        if (r == 0)
            r = fa_lookup_locked(dst, &d);
        if (r == 0 && (d->type != VNODE_FILE || vnode_is_link(d)))
            r = -FS_EINVAL;
        if (r == 0) {
            got = vfs_read(v, s, offset, fa_m->chunk, FA_CHUNK);
            if (got < 0) {
                r = (int)got;
            } else if (got > 0) {
                int64_t put = vfs_write(v, d, offset, fa_m->chunk, (uint64_t)got);
                if (put < 0)
                    r = (int)put;
                else if (put == 0)
                    r = -FS_ENOSPC;     /* no progress: full */
                else if (put > got)
                    r = -FS_EIO;
                else
                    got = put;          /* a short write: the rest from offset + put */
            }
        }
        fd_vfs_release();
    } while (ext2_disk_retry(&r, &tries));
    return r < 0 ? r : got;
}

static const struct files_fs fa_fs = {
    0, fa_fs_lstat, fa_fs_read_dir, fa_fs_mkdir, fa_fs_unlink, fa_fs_rmdir, fa_fs_copy_chunk,
};

/* files_exists_fn for files_unique_name: the name is taken (any kind). */
static bool fa_exists(void *ctx, const char *path)
{
    return fa_fs_lstat(ctx, path) != FILES_K_NONE;
}

/* Renames from to `to` only if `to` is free, both in one hold (nothing can
 * take the name between): -FS_EEXIST if it is taken. */
static int fa_rename_free(const char *from, const char *to)
{
    uint32_t tries = 0;
    int r;
    do {
        fd_vfs_acquire();
        struct vfs *v = vfs_root();
        struct vnode *n = 0;
        r = fa_lookup_locked(to, &n);
        if (r == 0)
            r = -FS_EEXIST;
        else if (r == -FS_ENOENT)
            r = vfs_rename(v, v->root, from, v->root, to);
        fd_vfs_release();
    } while (ext2_disk_retry(&r, &tries));
    return r;
}

/* The filesystem numbers of a and b's folders are the same (both found). */
static bool fa_same_fs(const char *a, const char *b)
{
    struct vnode *x = 0, *y = 0;
    fd_vfs_acquire();
    bool same = fa_lookup_locked(a, &x) == 0 && fa_lookup_locked(b, &y) == 0 && x->fs_id == y->fs_id;
    fd_vfs_release();
    return same;
}

/* The free bytes where path lies (the lock held): the disk's under /home in
 * Disk mode, else the RAM filesystem's budget. */
static uint64_t fa_free_locked(const char *path)
{
    bool home = path[0] == '/' && path[1] == 'h' && path[2] == 'o' && path[3] == 'm' && path[4] == 'e' &&
                (path[5] == '\0' || path[5] == '/');
    if (home && ext2_disk_mounted()) {
        struct ext2_disk_dfinfo f;
        if (ext2_disk_df(&f))
            return f.blocks_free * f.block_size;
        return 0;
    }
    const struct ramfs *fs = vfs_ramfs();
    uint32_t left = fs->budget_pages > fs->data_pages ? fs->budget_pages - fs->data_pages : 0;
    return (uint64_t)left * VM_PAGE_SIZE;
}

/* ---- the view ---- */

static void fa_status(const char *text, bool error)
{
    if (!fa_m)
        return;
    fa_copy(fa_m->status, FA_STATUS_MAX, text);
    fa_m->view.status_error = error;
}

/* The message for a failed call ("<what> '<name>': <reason>."), or the
 * plan's own text for ENOMEM and ENAMETOOLONG. */
static void fa_error(const char *what, const char *name, int r)
{
    if (!fa_m)
        return;
    if (r == -FS_ENOMEM) {
        fa_status("Too many files are in use: close some windows and try again.", true);
        return;
    }
    if (r == -FS_ENAMETOOLONG) {
        fa_status("This path is too long.", true);
        return;
    }
    char reason[24];
    const char *why;
    if (r == -FS_ENOSPC) {
        /* 1.5 stage 13 (Q6): a folder in memory (Live mode, or outside
         * /home in Disk mode) is full of memory, not of disk. */
        const char *p = fa_m->path;
        bool home = p[0] == '/' && p[1] == 'h' && p[2] == 'o' && p[3] == 'm' && p[4] == 'e' &&
                    (p[5] == '\0' || p[5] == '/');
        why = ext2_disk_mounted() && home ? "the disk is full" : "there is no room left in memory";
    } else if (r == -FS_EROFS) {
        why = "/home is read-only";
    } else if (r == -FS_EIO) {
        why = "a disk error occurred";
    } else {
        ksnprintf(reason, sizeof(reason), "error %d", r < 0 ? -r : r);
        why = reason;
    }
    char shown[FILES_NAME_MAX + 1];
    if (name) {
        ui_name_shown(name, shown, sizeof(shown));
        ksnprintf(fa_m->status, FA_STATUS_MAX, "%s '%s': %s.", what, shown, why);
    } else {
        ksnprintf(fa_m->status, FA_STATUS_MAX, "%s: %s.", what, why);
    }
    fa_m->view.status_error = true;
}

static void fa_count_status(void)
{
    char free_text[FILES_SIZE_TEXT];
    files_size_text(fa_free_bytes, free_text);
    ui_files_count_text(fa_m->folder.n, free_text, fa_m->status, FA_STATUS_MAX);
    fa_m->view.status_error = false;
}

/* The selected item the actions act on: the focused one, if selected and
 * a real entry (not the limit's note); -1 if none. */
static int32_t fa_target(void)
{
    struct ui_list *l = &fa_m->list;
    if (fa_m->view.mode != UI_FILES_LIST || l->cursor >= fa_m->folder.n || !l->items[l->cursor].selected)
        return -1;
    return (int32_t)l->cursor;
}

/* 1.5 stage 13: the real items selected (never the limit's note). */
static uint32_t fa_nsel(void)
{
    uint32_t n = 0;
    for (uint32_t i = 0; i < fa_m->folder.n && i < fa_m->list.n; i++)
        n += fa_m->items[i].selected;
    return n;
}

/* 1.5 stage 13: the restorable items selected (the Trash view). */
static uint32_t fa_nrestorable(void)
{
    uint32_t n = 0;
    for (uint32_t i = 0; i < fa_m->folder.n && i < fa_m->list.n; i++)
        n += fa_m->items[i].selected && fa_m->restorable[i];
    return n;
}

/* 1.5 stage 13: what the menus and the buttons see (files_ops.h). */
static struct files_ops_state fa_ops_state(void)
{
    struct files_ops_state st = { fa_nsel(), 0, 0, fa_writable, !files_clip_empty(&fa_m->clip), fa_writable };
    if (fa_m->view.trash) {
        st.restorable = fa_nrestorable();
        st.in_trash = fa_m->folder.n;
    }
    if (fa_menu_item >= 0)
        st.paste_writable = fa_writable;        /* a folder of this folder: the same filesystem */
    return st;
}

static void fa_update_view(void)
{
    struct ui_files_view *v = &fa_m->view;
    enum files_view hv = FILES_VIEW_FOLDER;
    const char *p = 0;
    uint32_t pl = 0;
    if (!files_history_current(&fa_m->hist, &hv, &p, &pl)) {
        p = fa_m->path;
        pl = fa_len(p);
    }
    bool idle = !fa_reading && !v->renaming && v->mode == UI_FILES_LIST && fa_job == FA_JOB_NONE;
    v->trash = hv == FILES_VIEW_TRASH;
    v->thispc = hv == FILES_VIEW_THISPC;    /* 1.5 stage 14b */
    v->enabled[UF_BACK] = idle && files_history_can_back(&fa_m->hist);
    v->enabled[UF_FORWARD] = idle && files_history_can_forward(&fa_m->hist);
    v->enabled[UF_UP] = idle && files_history_can_up(&fa_m->hist);
    v->enabled[UF_LIST] = v->enabled[UF_GRID] = v->enabled[UF_HIDDEN] = idle;
    v->enabled[UF_NEWFOLDER] = v->enabled[UF_NEWFILE] = idle && fa_writable && !v->trash;
    v->enabled[UF_OPEN] = idle && fa_target() >= 0 && !v->trash;
    v->enabled[UF_RENAME] = idle && fa_writable && fa_target() >= 0 && !v->trash;
    v->enabled[UF_CLOSE] = v->mode == UI_FILES_PANEL;
    /* 1.5 stage 13: the menus' rules (files_ops_greyed) for the buttons too. */
    struct files_ops_state st = fa_ops_state();
    uint8_t gi = files_ops_greyed(FILES_OPS_MENU_ITEM, &st), gs = files_ops_greyed(FILES_OPS_MENU_SPACE, &st);
    uint8_t gt = files_ops_greyed(FILES_OPS_MENU_TRASH_ITEM, &st);
    uint8_t gts = files_ops_greyed(FILES_OPS_MENU_TRASH_SPACE, &st);
    v->enabled[UF_CUT] = idle && !v->trash && !(gi & (1u << 1));
    v->enabled[UF_COPY] = idle && !v->trash && !(gi & (1u << 2));
    v->enabled[UF_PASTE] = idle && !v->trash && !(gs & (1u << 2));
    v->enabled[UF_TRASH] = idle && !v->trash && !(gi & (1u << 5));
    v->enabled[UF_RESTORE] = idle && v->trash && !(gt & (1u << 0));
    v->enabled[UF_TDELETE] = idle && v->trash && !(gt & (1u << 1));
    v->enabled[UF_RESTORE_ALL] = idle && v->trash && !(gts & (1u << 0));
    v->enabled[UF_EMPTY] = idle && v->trash && !(gts & (1u << 1));
    v->enabled[UF_ASK_CANCEL] = v->enabled[UF_ASK_OK] = v->mode == UI_FILES_ASK;
    v->chosen[UF_LIST] = fa_m->list.view == UI_LIST_DETAILS;
    v->chosen[UF_GRID] = fa_m->list.view == UI_LIST_GRID;
    v->chosen[UF_HIDDEN] = fa_m->folder.show_hidden;
    v->place = -1;
    for (int32_t k = 0; k < 7 && !v->trash; k++)
        if (fa_same(fa_m->path, fa_place_paths[k]))
            v->place = k;
    if (v->trash)
        v->place = 7;                   /* the sidebar's Trash */
    if (v->thispc) {                    /* 1.5 stage 14b: This PC: Open a card that opens */
        v->place = 8;
        v->enabled[UF_OPEN] = idle && v->card_sel >= 0 && v->card_sel < (int32_t)fa_ncards &&
                              fa_card_open[v->card_sel] != 0;
        v->enabled[UF_LIST] = v->enabled[UF_GRID] = false;
    }
    v->cards = fa_cards;
    v->ncards = fa_ncards;
    v->drop_on = fa_drag && fa_drop != FILES_DROP_NO && fa_drop_place >= 0;
    v->drop_place = fa_drop_place;
    fa_m->list.drop = fa_drag && (fa_drop == FILES_DROP_MOVE || fa_drop == FILES_DROP_COPY) ? fa_drop_item : -1;
    v->ask_line1 = fa_m->ask1;
    v->ask_line2 = fa_m->ask2;
    v->ask_label = fa_m->ask_label;
    for (int k = 0; k < 2; k++) {       /* the question's buttons: pressed, on hover, or normal */
        enum ui_files_part bp = k ? UF_ASK_OK : UF_ASK_CANCEL;
        v->ask_state[k] = v->mode != UI_FILES_ASK ? UI_BUTTON_NORMAL : v->pressed == bp ? UI_BUTTON_PRESSED
                          : v->hover == bp ? UI_BUTTON_HOVER : UI_BUTTON_NORMAL;
    }
    struct ui_files_layout lay;
    ui_files_layout(fa_env.files->r, fa_env.scale, fa_env.fonts->regular, v->bar != UI_FILES_BAR_NONE, &lay);
    files_pathbar(hv, p, pl, ui_files_path_cells(&lay, fa_env.scale, fa_env.fonts->regular), &fa_m->pb);
    v->pb = &fa_m->pb;
    v->list = &fa_m->list;
    v->field = &fa_m->field;
    v->status = fa_m->status;
    /* The list's rectangle follows the window (moved, resized, maximised). */
    fa_m->list.r = lay.content;
    v->band_on = fa_band;               /* 1.5 stage 14b: the band, in the list placed above */
    if (fa_band)
        v->band = ui_list_band_rect(&fa_m->list, fa_band_ax, fa_band_ay, fa_band_bx, fa_band_by);
}

static const char *fa_part_name(enum ui_files_part p)
{
    return p > UF_NONE && p < UF_BUTTONS ? ui_files_button_labels[p] : "-";
}

/* desktoplog: the view as drawn, for tests/qmp_desktop.py (its rendering
 * of the window from these lines). */
static void fa_log_state(void)
{
    if (!fa_env.logging || !fa_env.files->open)
        return;
    if (fa_nomem_view || !fa_m) {
        kserialf("files: state mode=nomem\n");
        return;
    }
    struct ui_files_view *v = &fa_m->view;
    struct ui_list *l = &fa_m->list;
    char en[UF_BUTTONS + 1], ch[UF_BUTTONS + 1];
    for (int i = 0; i < UF_BUTTONS; i++) {
        en[i] = v->enabled[i] ? '1' : '0';
        ch[i] = v->chosen[i] ? '1' : '0';
    }
    en[UF_BUTTONS] = ch[UF_BUTTONS] = '\0';
    enum files_view hv = FILES_VIEW_FOLDER;
    const char *p = fa_m->path;
    uint32_t pl = 0;
    if (!files_history_current(&fa_m->hist, &hv, &p, &pl))
        p = fa_m->path;
    kserialf("files: path %u %s\n", (uint32_t)hv, p);
    if (v->renaming)        /* 1.5 stage 13: and whether the whole name is selected */
        kserialf("files: field %u %u %u %s\n", (uint32_t)fa_m->field.pos, (uint32_t)fa_m->field.first,
                 fa_m->field.sel ? 1u : 0u, fa_m->field.buf);
    if (v->mode == UI_FILES_ASK) {          /* 1.5 stage 13: the question panel */
        kserialf("files: ask %s\n", fa_m->ask1);
        kserialf("files: ask2 %s\n", fa_m->ask2);
        kserialf("files: asklabel %s\n", fa_m->ask_label);
    }
    kserialf("files: status %u %s\n", v->status_error ? 1u : 0u, fa_m->status);
    if (v->mode == UI_FILES_PANEL && l->cursor < fa_m->folder.n) {
        const struct files_entry *e = &fa_m->folder.e[l->cursor];
        kserialf("files: panel %c %llu %lld %s\n", e->kind == FILES_K_DIR ? 'D' : e->kind == FILES_K_LINK ? 'L' : 'F',
                 (ull)e->size, (long long)e->mtime, files_entry_name(&fa_m->folder, l->cursor));
    }
    /* 1.5 stage 14b: This PC's cards, the band and the drop target, only
     * while they are shown. */
    if (v->thispc) {
        kserialf("files: pc %u %d %d\n", fa_ncards, v->card_sel, v->card_hover);
        for (uint32_t c = 0; c < fa_ncards && c < UI_FILES_CARDS; c++) {
            const struct ui_files_card *cd = &fa_cards[c];
            kserialf("files: card %u %s bar=%u ro=%u used=%llu total=%llu %s\n", c,
                     cd->icon == UI_ICON_MEMORY ? "memory" : cd->icon == UI_ICON_CD ? "cd" : "disk",
                     cd->bar ? 1u : 0u, cd->ro ? 1u : 0u, (ull)cd->used, (ull)cd->total, cd->name);
        }
    }
    if (v->band_on)
        kserialf("files: band %d %d %d %d\n", v->band.x, v->band.y, v->band.w, v->band.h);
    if (l->drop >= 0 || v->drop_on)
        kserialf("files: drop %d %d\n", l->drop, v->drop_on ? v->drop_place : -1);
    char sel[96];
    uint32_t k = 0, nsel = 0;
    for (uint32_t i = 0; i < l->n; i++)
        if (l->items[i].selected) {
            nsel++;
            if (k + 12 < sizeof(sel))
                k += (uint32_t)ksnprintf(sel + k, sizeof(sel) - k, "%s%u", k ? "," : "", i);
        }
    if (!nsel)
        fa_copy(sel, sizeof(sel), "-");
    /* 1.5 stage 13: trash, app (the panel's has_app), job, askfocus,
     * askring and askst (Cancel's and the action's states) at the end. */
    kserialf("files: state mode=%s view=%s bar=%s place=%d en=%s chosen=%s hover=%s pressed=%s tip=%s top=%u "
             "cursor=%u ring=%u sb=%u rename=%u reading=%u sel=%s trash=%u app=%u job=%u askfocus=%d askring=%u "
             "askst=%u%u\n",
             v->mode == UI_FILES_PANEL ? "panel" : v->mode == UI_FILES_ASK ? "ask" : "list",
             l->view == UI_LIST_GRID ? "grid" : "list",
             v->bar == UI_FILES_BAR_LIVE ? "live" : v->bar == UI_FILES_BAR_RO ? "ro" : "none", v->place, en, ch,
             fa_part_name(v->hover), fa_part_name(v->pressed), fa_part_name(v->tip), l->top, l->cursor,
             l->ring ? 1u : 0u, (uint32_t)l->sb, v->renaming ? 1u : 0u, fa_reading ? 1u : 0u, sel, v->trash ? 1u : 0u,
             v->panel_has_app ? 1u : 0u, (uint32_t)fa_job, v->ask_focus, v->ask_ring ? 1u : 0u,
             (uint32_t)v->ask_state[0], (uint32_t)v->ask_state[1]);
}

static void fa_changed(void)
{
    if (!fa_m)
        return;
    fa_update_view();
    fa_damage(fa_env.files);
    fa_log_state();
}

/* ---- reading a folder ---- */

static void fa_read(const char *path)
{
    fa_copy(fa_m->rpath, sizeof(fa_m->rpath), path);
    files_folder_init(&fa_m->folder, fa_m->entries, FILES_MAX_ENTRIES, fa_m->names, sizeof(fa_m->names),
                      fa_m->folder.show_hidden);
    fa_m->list.n = 0;
    fa_m->list.top = fa_m->list.cursor = fa_m->list.anchor = 0;
    fa_m->view.mode = UI_FILES_LIST;
    fa_m->view.renaming = false;
    fa_reading = true;
    fa_read_t0 = fa_now;                /* 1.5 stage 14b: the busy pointer after FA_BUSY_TICKS */
    fa_press = FA_PRESS_NONE;
    fa_band = false;
    fa_cookie = 0;
    fa_seen = fa_holds = 0;
    fa_tinfo_on = false;                /* 1.5 stage 13: the Trash view follows the history */
    enum files_view hv = FILES_VIEW_FOLDER;
    const char *hp = 0;
    uint32_t hl = 0;
    fa_m->view.trash = files_history_current(&fa_m->hist, &hv, &hp, &hl) && hv == FILES_VIEW_TRASH;
}

static int fa_icon_for(uint32_t i)
{
    const struct files_entry *e = &fa_m->folder.e[i];
    const char *name = files_entry_name(&fa_m->folder, i);
    if (e->kind == FILES_K_DIR) {
        /* The home folders, in the Home folder, have their emblems. */
        static const int fa_home_icons[7] = {
            UI_ICON_HOME, UI_ICON_DESKTOP, UI_ICON_DOCUMENTS, UI_ICON_DOWNLOADS, UI_ICON_PICTURES, UI_ICON_MUSIC,
            UI_ICON_VIDEOS,
        };
        if (fa_same(fa_m->path, FILES_HOME))
            for (int k = 1; k < 7; k++)
                if (fa_same(name, ui_files_place_names[k]))
                    return fa_home_icons[k];
        return UI_ICON_FOLDER;
    }
    if (e->kind == FILES_K_LINK)
        return UI_ICON_LINK;
    return fa_same(files_type_text(FILES_K_FILE, name, e->name_len), "Text file") ? UI_ICON_TEXTFILE : UI_ICON_FILE;
}

static void fa_read_finish(void);

static void fa_read_done(void)
{
    struct files_folder *f = &fa_m->folder;
    files_folder_sort(f);
    fa_copy(fa_m->path, sizeof(fa_m->path), fa_m->rpath);
    for (uint32_t i = 0; i < f->n && i < FILES_MAX_ENTRIES; i++) {
        struct ui_list_item *it = &fa_m->items[i];
        const struct files_entry *e = &f->e[i];
        const char *name = files_entry_name(f, i);
        files_size_text(e->size, fa_m->sizes[i]);
        files_date_text(e->mtime, fa_m->dates[i]);
        it->name = name;
        it->size = e->kind == FILES_K_DIR ? 0 : fa_m->sizes[i];
        it->type = files_type_text((enum files_kind)e->kind, name, e->name_len);
        it->modified = fa_m->dates[i];
        it->icon = fa_icon_for(i);
        it->selected = false;
    }
    uint32_t n = f->n < FILES_MAX_ENTRIES ? f->n : FILES_MAX_ENTRIES;
    if (f->truncated) {
        struct ui_list_item *note = &fa_m->items[n];
        note->name = "Showing the first 1000 items of this folder.";
        note->size = note->type = note->modified = 0;
        note->icon = UI_ICON_NONE;
        note->selected = false;
        n++;
    }
    enum ui_list_view lv = fa_m->list.view;
    struct ui_files_layout lay;
    ui_files_layout(fa_env.files->r, fa_env.scale, fa_env.fonts->regular, fa_m->view.bar != UI_FILES_BAR_NONE, &lay);
    ui_list_init(&fa_m->list, lv, fa_m->items, n, ui_files_panel_labels, lay.content, fa_env.scale);
    if (fa_m->view.trash) {
        /* 1.5 stage 13 (Q4): the Trash view's three columns, filled from
         * each item's info file (fa_tinfo_step) before the reading ends. */
        static const char *const fa_trash_titles[4] = { "Name", "", "Original location", "Deleted" };
        fa_m->list.wide = true;
        fa_m->list.titles = fa_trash_titles;
        for (uint32_t i = 0; i < f->n && i < FILES_MAX_ENTRIES; i++) {
            fa_m->locs[i][0] = '\0';
            fa_m->dates[i][0] = '\0';
            fa_m->restorable[i] = 0;
            fa_m->items[i].type = fa_m->locs[i];
            fa_m->items[i].modified = fa_m->dates[i];
        }
        fa_tinfo = 0;
        fa_tinfo_on = true;
        return;                         /* still reading */
    }
    fa_read_finish();
}

/* The reading's end: the status line, the item to select, the logs. */
static void fa_read_finish(void)
{
    struct files_folder *f = &fa_m->folder;
    if (fa_keep_status)
        fa_keep_status = false;         /* 1.5 stage 13: an action's message stays */
    else
        fa_count_status();
    if (fa_m->select_name[0]) {
        for (uint32_t i = 0; i < f->n; i++)
            if (fa_same(files_entry_name(f, i), fa_m->select_name)) {
                fa_m->list.cursor = fa_m->list.anchor = i;
                fa_m->items[i].selected = true;
                ui_list_reveal(&fa_m->list);
                if (fa_rename_after) {
                    ui_textfield_set(&fa_m->field, fa_m->select_name);
                    ui_textfield_select_all(&fa_m->field);     /* 1.5 stage 13 (D1) */
                    fa_m->view.renaming = true;
                }
                break;
            }
        fa_m->select_name[0] = '\0';
    }
    fa_rename_after = false;
    fa_reading = false;
    klogf("Files: read %s: %u entries in %u holds of at most %u, %u shown\n", fa_m->path, fa_seen, fa_holds,
          FA_HOLD_ENTRIES, f->n);
    if (fa_env.logging) {
        kserialf("files: items %u %u\n", f->n, f->truncated ? 1u : 0u);
        for (uint32_t i = 0; i < f->n; i++) {
            const struct files_entry *e = &f->e[i];
            kserialf("files: item %u %c %llu %lld %s\n", i,
                     e->kind == FILES_K_DIR ? 'D' : e->kind == FILES_K_LINK ? 'L' : 'F', (ull)e->size,
                     (long long)e->mtime, files_entry_name(f, i));
        }
        for (uint32_t i = 0; fa_m->list.wide && i < f->n; i++) {    /* 1.5 stage 13: the Trash's columns */
            kserialf("files: tloc %u %s\n", i, fa_m->locs[i]);
            kserialf("files: tdate %u %u %s\n", i, (uint32_t)fa_m->restorable[i], fa_m->dates[i]);
        }
    }
}

/* 1.5 stage 13: one hold of the Trash view's info files: at most FA_CHUNK
 * bytes of item fa_tinfo's info/<name>.trashinfo (refused unread over
 * TRASH_INFO_MAX), parsed by trash_info_parse when it is whole. */
static uint64_t fa_toff;

static void fa_tinfo_step(void)
{
    struct files_folder *f = &fa_m->folder;
    uint32_t i = fa_tinfo;
    if (i >= f->n || i >= FILES_MAX_ENTRIES) {
        uint32_t unknown = 0;
        for (uint32_t k = 0; k < f->n && k < FILES_MAX_ENTRIES; k++)
            unknown += !fa_m->restorable[k];
        fa_tinfo_on = false;
        fa_toff = 0;
        klogf("Files: the Trash's info files read: %u items, %u without a known original location\n", f->n,
              unknown);
        fa_read_finish();
        return;
    }
    const char *name = files_entry_name(f, i);
    bool named = f->e[i].name_len + (uint32_t)(sizeof(TRASH_INFO_EXT) - 1u) <= TRASH_NAME_MAX &&
                 fa_join(fa_m->jinfo, FA_TINFO, name);
    uint32_t nl = fa_len(fa_m->jinfo);
    if (named && nl + (uint32_t)(sizeof(TRASH_INFO_EXT) - 1u) <= FILES_PATH_MAX) {
        for (uint32_t k = 0; TRASH_INFO_EXT[k]; k++)
            fa_m->jinfo[nl + k] = TRASH_INFO_EXT[k];
        fa_m->jinfo[nl + sizeof(TRASH_INFO_EXT) - 1u] = '\0';
    } else {
        named = false;
    }
    int r = -FS_ENOENT;
    uint64_t size = 0;
    int64_t got = 0;
    if (named) {
        struct vfs *v = vfs_root();
        struct vnode *n = 0;
        fd_vfs_acquire();
        r = fa_lookup_locked(fa_m->jinfo, &n);
        if (r == 0 && (n->type != VNODE_FILE || vnode_is_link(n)))
            r = -FS_EINVAL;             /* a folder or a link is no info file (rule SL) */
        if (r == 0) {
            size = n->size;
            if (size <= TRASH_INFO_MAX && fa_toff < size) {
                uint64_t want = size - fa_toff;
                if (want > FA_CHUNK)
                    want = FA_CHUNK;
                got = vfs_read(v, n, fa_toff, fa_m->info_buf + fa_toff, want);
                if (got < 0)
                    r = (int)got;
            }
        }
        fd_vfs_release();
    }
    if (r == 0 && size <= TRASH_INFO_MAX && got > 0 && fa_toff + (uint64_t)got < size) {
        fa_toff += (uint64_t)got;       /* the rest in the next hold */
        return;
    }
    uint64_t have = fa_toff + (uint64_t)(got > 0 ? got : 0);
    fa_toff = 0;
    fa_m->info.date_ok = false;
    enum trash_info_result pr = TI_NO_PATH;
    if (r == 0)
        pr = size > TRASH_INFO_MAX ? TI_FILE_TOO_LARGE
                                   : trash_info_parse(fa_m->info_buf, (uint32_t)have, &fa_m->info);
    fa_m->restorable[i] = pr == TI_OK;
    if (pr == TI_OK) {
        uint32_t cells = 0;
        int32_t room = ui_px(192, fa_env.scale) - ui_px(16, fa_env.scale);
        if (fa_env.fonts->regular->width > 0 && room > 0)
            cells = (uint32_t)room / fa_env.fonts->regular->width;
        if (cells > FA_LOC_MAX - 1u)
            cells = FA_LOC_MAX - 1u;
        uint32_t pl = files_ops_parent_len(fa_m->info.path, fa_m->info.path_len);
        files_ops_location(fa_m->info.path, pl, cells, fa_m->locs[i], FA_LOC_MAX);
    } else {
        fa_copy(fa_m->locs[i], FA_LOC_MAX, "(unknown original location)");
        if (r == 0 || named) {
            char shown[FILES_NAME_MAX + 1];
            ui_name_shown(name, shown, sizeof(shown));
            klogf("Files: Trash item '%s': its info file %s (%s): it can't be restored\n", shown,
                  r == 0 ? "was refused" : "is missing", r == 0 ? trash_info_reason(pr) : "not found");
        }
    }
    if (fa_m->info.date_ok)
        ksnprintf(fa_m->dates[i], FILES_DATE_TEXT, "%04u-%02u-%02u %02u:%02u", (uint32_t)fa_m->info.date.year,
                  (uint32_t)fa_m->info.date.month, (uint32_t)fa_m->info.date.day, (uint32_t)fa_m->info.date.hour,
                  (uint32_t)fa_m->info.date.minute);
    else
        fa_copy(fa_m->dates[i], FILES_DATE_TEXT, "(unknown date)");
    fa_tinfo++;
}

static void fa_read_failed(int r)
{
    fa_reading = false;
    fa_rename_after = false;
    fa_m->select_name[0] = '\0';
    fa_copy(fa_m->path, sizeof(fa_m->path), fa_m->rpath);
    if (r == -FS_ENOENT || r == -FS_ENOTDIR)
        fa_status("That folder no longer exists.", true);
    else
        fa_error("Can't read this folder", 0, r);
    klogf("Files: read %s: stopped (error %d) after %u entries in %u holds\n", fa_m->rpath, r < 0 ? -r : r,
          fa_seen, fa_holds);
}

/* One hold: at most FA_HOLD_ENTRIES entries. */
static void fa_read_hold(void)
{
    struct vfs *v = vfs_root();
    struct vnode *dir = 0;
    bool done = false;
    fd_vfs_acquire();
    int r = fa_lookup_locked(fa_m->rpath, &dir);
    if (r == 0 && dir->type != VNODE_DIR)
        r = -FS_ENOTDIR;
    if (r == -FS_ENOENT && fa_m->view.trash) {
        /* 1.5 stage 13: no Trash folder yet (made when first needed): the
         * Trash is empty; /home/user decides whether it can change. */
        struct vnode *home = 0;
        fa_writable = fa_lookup_locked(FILES_HOME, &home) == 0 && vfs_writable(v, home) == 0;
        fa_free_bytes = fa_free_locked(FILES_HOME);
        r = 0;
        done = true;
    } else if (r == 0) {
        uint32_t count = 0;
        struct vnode *e = vfs_entry_from(v, dir, fa_cookie);
        while (e && count < FA_HOLD_ENTRIES) {
            size_t len = 0;
            const char *name = vfs_entry_name(v, e, &len);
            struct vnode *in = vfs_enter(v, e);
            enum files_kind kind = vnode_is_link(e) ? FILES_K_LINK : in->type == VNODE_DIR ? FILES_K_DIR
                                                                                           : FILES_K_FILE;
            enum files_add a = files_folder_add(&fa_m->folder, name, (uint32_t)len, kind,
                                                kind == FILES_K_DIR ? 0 : in->size, in->mtime);
            fa_cookie = vfs_entry_pos(v, e) + 1;
            fa_seen++;
            count++;
            if (a == FILES_ADD_FULL) {
                done = true;
                break;
            }
            e = vfs_entry_next(v, dir, e);
        }
        if (!e && !done) {
            int err = vfs_lookup_error(v, dir);
            if (err)
                r = err;
            else
                done = true;
        }
        if (done) {
            fa_writable = vfs_writable(v, dir) == 0;
            fa_free_bytes = fa_free_locked(fa_m->rpath);
        }
    }
    fd_vfs_release();
    fa_holds++;
    if (r < 0)
        fa_read_failed(r);
    else if (done)
        fa_read_done();
}

/* ---- 1.5 stage 14b: This PC (plan C5, note (15), Q2-Q4) ---- */

/* Card k, field by field (no struct copy: the kernel has no memcpy). */
static void fa_card_set(uint32_t k, int icon, bool bar, bool ro, uint64_t used, uint64_t total)
{
    struct ui_files_card *c = &fa_cards[k];
    c->name = fa_card_names[k];
    c->figures = fa_card_figs[k];
    c->icon = icon;
    c->bar = bar;
    c->ro = ro;
    c->used = used;
    c->total = total;
}

/* The cards from the filesystems' figures (one hold): the memory (the RAM
 * filesystem's budget: /, /tmp, and in Live mode /home), the disk on
 * /home (or "No disk in use" in Live mode), the CD drive only when the
 * AHCI scan found an ATAPI device. */
static void fa_pc_read(void)
{
    fd_vfs_acquire();
    const struct ramfs *fs = vfs_ramfs();
    uint64_t budget = (uint64_t)fs->budget_pages * VM_PAGE_SIZE;
    uint64_t ram_used = (uint64_t)(fs->data_pages < fs->budget_pages ? fs->data_pages : fs->budget_pages) *
                        VM_PAGE_SIZE;
    bool disk = ext2_disk_mounted(), rw = ext2_disk_rw();
    struct ext2_disk_dfinfo df;
    bool dfok = disk && ext2_disk_df(&df);
    fd_vfs_release();
    uint32_t n = 0;
    fa_copy(fa_card_names[n], sizeof(fa_card_names[n]), disk ? "Memory: /, /tmp" : "Memory (Live): /, /tmp");
    files_usage_text(ram_used, budget, fa_card_figs[n]);
    fa_card_set(n, UI_ICON_MEMORY, true, false, ram_used, budget);
    fa_card_open[n++] = "/";
    if (disk) {
        ksnprintf(fa_card_names[n], sizeof(fa_card_names[n]), "Disk %u '%s': /home%s", dfok ? df.port : 0u,
                  dfok ? df.label : "", rw ? "" : " (read-only)");
        uint64_t total = dfok ? df.blocks * df.block_size : 0;
        uint64_t used = dfok && df.blocks >= df.blocks_free ? (df.blocks - df.blocks_free) * df.block_size : 0;
        files_usage_text(used, total, fa_card_figs[n]);
        fa_card_set(n, UI_ICON_DISK, dfok, !rw, used, total);
        fa_card_open[n++] = "/home";
    } else {
        fa_copy(fa_card_names[n], sizeof(fa_card_names[n]), "No disk in use");
        fa_card_figs[n][0] = '\0';
        fa_card_set(n, UI_ICON_DISK, false, false, 0, 0);
        fa_card_open[n++] = 0;
    }
    if (ahci_has_atapi()) {
        fa_copy(fa_card_names[n], sizeof(fa_card_names[n]), "CD drive: not used");
        fa_card_figs[n][0] = '\0';
        fa_card_set(n, UI_ICON_CD, false, false, 0, 0);
        fa_card_open[n++] = 0;
    }
    fa_ncards = n;
}

/* The This PC view (the history's current entry): its cards read, no
 * folder, nothing writable, no card selected. */
static void fa_pc_show(void)
{
    struct ui_files_view *v = &fa_m->view;
    fa_reading = false;
    fa_tinfo_on = false;
    fa_press = FA_PRESS_NONE;
    fa_band = false;
    files_folder_init(&fa_m->folder, fa_m->entries, FILES_MAX_ENTRIES, fa_m->names, sizeof(fa_m->names),
                      fa_m->folder.show_hidden);
    ui_list_init(&fa_m->list, fa_m->list.view, fa_m->items, 0, ui_files_panel_labels, fa_m->list.r, fa_env.scale);
    v->mode = UI_FILES_LIST;
    v->renaming = false;
    v->trash = false;
    v->thispc = true;
    v->card_sel = v->card_hover = -1;
    fa_writable = false;
    fa_m->path[0] = '\0';
    fa_m->status[0] = '\0';
    v->status_error = false;
    fa_pc_read();
    klogf("Files: This PC: %u storage areas\n", fa_ncards);
}

/* Opens card k's root (the memory "/", the disk "/home"); nothing for a
 * card without one. */
static void fa_go(enum files_view view, const char *path);

static void fa_pc_open(int32_t k)
{
    if (k < 0 || k >= (int32_t)fa_ncards || !fa_card_open[k])
        return;
    klogf("Files: This PC: '%s' opened at %s\n", fa_cards[k].name, fa_card_open[k]);
    fa_count_status();
    fa_go(FILES_VIEW_FOLDER, fa_card_open[k]);
}

/* ---- actions ---- */

static void fa_go(enum files_view view, const char *path)
{
    if (!files_history_go(&fa_m->hist, view, path, fa_len(path))) {
        fa_status("This path is too long.", true);
        return;
    }
    if (view == FILES_VIEW_THISPC)      /* 1.5 stage 14b */
        fa_pc_show();
    else
        fa_read(path);
}

/* The current history entry read again (after Back, Forward or Up). */
static void fa_read_current(void)
{
    enum files_view hv;
    const char *p;
    uint32_t pl;
    if (!files_history_current(&fa_m->hist, &hv, &p, &pl) || pl > FILES_PATH_MAX)
        return;
    if (hv == FILES_VIEW_TRASH) {       /* 1.5 stage 13: the Trash view */
        fa_read(FA_TFILES);
        return;
    }
    if (hv == FILES_VIEW_THISPC) {      /* 1.5 stage 14b: This PC */
        fa_pc_show();
        return;
    }
    for (uint32_t i = 0; i < pl; i++)
        fa_m->scratch[i] = p[i];
    fa_m->scratch[pl] = '\0';
    fa_read(fa_m->scratch);
}

static void fa_nav(enum ui_files_part part)
{
    uint32_t removed = 0;
    bool moved = false;
    if (part == UF_BACK)
        moved = files_history_back(&fa_m->hist, fa_is_folder, 0, &removed);
    else if (part == UF_FORWARD)
        moved = files_history_forward(&fa_m->hist, fa_is_folder, 0, &removed);
    else if (part == UF_UP)
        moved = files_history_up(&fa_m->hist);
    if (moved)
        fa_read_current();
    if (removed) {
        klogf("Files: %u folder%s no longer there: removed from the history\n", removed, removed == 1 ? "" : "s");
        fa_status("That folder no longer exists.", true);
    }
}

static void fa_open_viewer(const char *path, const char *name, uint64_t size);

static void fa_open_item(uint32_t i)
{
    if (i >= fa_m->folder.n)
        return;
    const struct files_entry *e = &fa_m->folder.e[i];
    const char *name = files_entry_name(&fa_m->folder, i);
    if (e->kind == FILES_K_DIR) {
        if (!fa_join(fa_m->scratch, fa_m->path, name)) {
            fa_status("This path is too long.", true);
            return;
        }
        fa_go(FILES_VIEW_FOLDER, fa_m->scratch);
        return;
    }
    bool text = e->kind == FILES_K_FILE && fa_same(files_type_text(FILES_K_FILE, name, e->name_len), "Text file");
    if (text) {
        if (!fa_join(fa_m->scratch, fa_m->path, name)) {
            fa_status("This path is too long.", true);
            return;
        }
        fa_open_viewer(fa_m->scratch, name, e->size);
        return;
    }
    if (e->kind == FILES_K_LINK) {
        char shown[FILES_NAME_MAX + 1];
        ui_name_shown(name, shown, sizeof(shown));
        klogf("Files: '%s' is a link: shown in the info panel (never followed)\n", shown);
    }
    files_size_text(e->size, fa_m->panel_size);
    files_date_text(e->mtime, fa_m->panel_date);
    fa_m->view.panel_icon = fa_m->items[i].icon;
    fa_m->view.panel_values[0] = name;
    fa_m->view.panel_values[1] = fa_m->panel_size;
    fa_m->view.panel_values[2] = files_type_text((enum files_kind)e->kind, name, e->name_len);
    fa_m->view.panel_values[3] = fa_m->panel_date;
    fa_m->view.panel_has_app = false;  /* 1.5 stage 13: no app: the S12 line shown */
    fa_m->view.mode = UI_FILES_PANEL;
}

static void fa_new(bool folder)
{
    char name[32];
    if (!ui_name_default(folder, fa_name_taken, 0, name, sizeof(name))) {
        fa_status("That name is already used here.", true);
        return;
    }
    if (!fa_join(fa_m->scratch, fa_m->path, name)) {
        fa_status("This path is too long.", true);
        return;
    }
    int r = fa_create(fa_m->scratch, folder);
    if (r < 0) {
        fa_error("Can't create", name, r);
        return;
    }
    klogf("Files: %s '%s' made in %s\n", folder ? "folder" : "file", name, fa_m->path);
    fa_copy(fa_m->select_name, sizeof(fa_m->select_name), name);
    fa_rename_after = true;
    fa_copy(fa_m->scratch2, sizeof(fa_m->scratch2), fa_m->path);
    fa_read(fa_m->scratch2);
}

static void fa_rename_start(void)
{
    int32_t i = fa_target();
    if (i < 0 || !fa_writable)
        return;
    ui_textfield_set(&fa_m->field, files_entry_name(&fa_m->folder, (uint32_t)i));
    ui_textfield_select_all(&fa_m->field);      /* 1.5 stage 13 (D1): the whole name selected */
    fa_m->view.renaming = true;
}

/* Enter in the rename field: the name checked, then the rename. */
static void fa_rename_submit(void)
{
    struct ui_list *l = &fa_m->list;
    if (l->cursor >= fa_m->folder.n) {
        fa_m->view.renaming = false;
        return;
    }
    const char *old = files_entry_name(&fa_m->folder, l->cursor);
    const char *name = fa_m->field.buf;
    if (fa_same(old, name)) {
        fa_m->view.renaming = false;
        return;
    }
    enum ui_name_check c = ui_name_check(name, fa_name_taken, 0);
    if (c == UI_NAME_EMPTY)
        return;
    if (c != UI_NAME_OK) {
        fa_status(ui_name_message(c), true);
        return;
    }
    if (!fa_join(fa_m->scratch, fa_m->path, old) || !fa_join(fa_m->scratch2, fa_m->path, name)) {
        fa_status("This path is too long.", true);
        return;
    }
    int r = fa_rename(fa_m->scratch, fa_m->scratch2);
    if (r < 0) {
        fa_error("Can't rename", old, r);
        return;
    }
    klogf("Files: '%s' renamed to '%s' in %s\n", old, name, fa_m->path);
    fa_copy(fa_m->select_name, sizeof(fa_m->select_name), name);
    fa_copy(fa_m->scratch2, sizeof(fa_m->scratch2), fa_m->path);
    fa_read(fa_m->scratch2);
}

/* 1.5 stage 13: the actions (below). */
static void fa_clip_set(bool cut);
static void fa_paste(int32_t into);
static void fa_delete(bool permanently, bool by_key);
static void fa_restore(bool all);
static void fa_empty(bool by_key);
static void fa_ask_answer(bool yes);

static void fa_button(enum ui_files_part part)
{
    if (part <= UF_NONE || part >= UF_BUTTONS || !fa_m->view.enabled[part])
        return;
    if (part != UF_CLOSE)
        fa_count_status();
    switch (part) {
    case UF_BACK:
    case UF_FORWARD:
    case UF_UP:
        fa_nav(part);
        break;
    case UF_LIST:
    case UF_GRID:
        fa_m->list.view = part == UF_GRID ? UI_LIST_GRID : UI_LIST_DETAILS;
        fa_m->list.top = 0;
        ui_list_reveal(&fa_m->list);
        break;
    case UF_HIDDEN:
        fa_m->folder.show_hidden = !fa_m->folder.show_hidden;
        fa_copy(fa_m->scratch2, sizeof(fa_m->scratch2), fa_m->path);
        fa_read(fa_m->scratch2);
        break;
    case UF_NEWFOLDER:
    case UF_NEWFILE:
        fa_new(part == UF_NEWFOLDER);
        break;
    case UF_OPEN:
        if (fa_m->view.thispc)          /* 1.5 stage 14b: the card selected */
            fa_pc_open(fa_m->view.card_sel);
        else if (fa_target() >= 0)
            fa_open_item((uint32_t)fa_target());
        break;
    case UF_RENAME:
        fa_rename_start();
        break;
    case UF_CLOSE:
        fa_m->view.mode = UI_FILES_LIST;
        break;
    /* 1.5 stage 13 (the mouse: no focus ring on a question) */
    case UF_CUT:
    case UF_COPY:
        fa_clip_set(part == UF_CUT);
        break;
    case UF_PASTE:
        fa_paste(-1);
        break;
    case UF_TRASH:
    case UF_TDELETE:
        fa_delete(false, false);
        break;
    case UF_RESTORE:
    case UF_RESTORE_ALL:
        fa_restore(part == UF_RESTORE_ALL);
        break;
    case UF_EMPTY:
        fa_empty(false);
        break;
    case UF_ASK_CANCEL:
    case UF_ASK_OK:
        fa_ask_answer(part == UF_ASK_OK);
        break;
    default:
        break;
    }
}

/* ---- 1.5 stage 13: the actions (one bounded step per pass) ---- */

static void fa_working(void)
{
    ksnprintf(fa_m->status, FA_STATUS_MAX, "Working... %u items done", fa_jdone);
    fa_m->view.status_error = false;
}

/* A message about item name: "<head> '<name>'<tail>". */
static void fa_name_msg(const char *head, const char *name, const char *tail)
{
    char shown[FILES_NAME_MAX + 1];
    ui_name_shown(name, shown, sizeof(shown));
    ksnprintf(fa_m->status, FA_STATUS_MAX, "%s '%s'%s", head, shown, tail);
    fa_m->view.status_error = true;
}

/* The reason a failed call gives (as fa_error's), into buf. */
static const char *fa_reason(int r, char *buf, uint32_t size)
{
    if (r == -FS_ENOSPC) {
        const char *p = fa_m->path;
        bool home = p[0] == '/' && p[1] == 'h' && p[2] == 'o' && p[3] == 'm' && p[4] == 'e' &&
                    (p[5] == '\0' || p[5] == '/');
        return ext2_disk_mounted() && home ? "the disk is full" : "there is no room left in memory";
    }
    if (r == -FS_EROFS)
        return "/home is read-only";
    if (r == -FS_EIO)
        return "a disk error occurred";
    ksnprintf(buf, size, "error %d", r < 0 ? -r : r);
    return buf;
}

/* path (NUL-terminated) and ".trashinfo" into out; false if too long. */
static bool fa_info_path(char *out, const char *slot)
{
    if (!fa_join(out, FA_TINFO, slot))
        return false;
    uint32_t n = fa_len(out), e = (uint32_t)sizeof(TRASH_INFO_EXT) - 1u;
    if (n + e > FILES_PATH_MAX)
        return false;
    for (uint32_t k = 0; k < e; k++)
        out[n + k] = TRASH_INFO_EXT[k];
    out[n + e] = '\0';
    return true;
}

static void fa_job_begin(enum fa_job j, bool all)
{
    fa_job = j;
    fa_step = FA_ST_ITEM;
    fa_jnext = fa_jitem = 0;
    fa_jdone = fa_junknown = fa_jk = fa_jdirs_made = 0;
    fa_joff = 0;
    fa_jall = all;
    fa_jinfo_made = false;
    fa_jdrag = false;                   /* 1.5 stage 14b: a drop sets it after this */
    fa_working();
}

/* The action's end: the folder read again (an action's message kept). */
static void fa_job_end(bool keep)
{
    fa_job = FA_JOB_NONE;
    fa_step = FA_ST_ITEM;
    fa_jdrag = false;                   /* 1.5 stage 14b */
    fa_keep_status = keep;
    fa_copy(fa_m->scratch2, sizeof(fa_m->scratch2), fa_m->path);
    fa_read(fa_m->scratch2);
}

/* A partial copy and an info file not yet followed by its item removed
 * (a failure, the window closed, Shut down); true if one was. */
static bool fa_job_clean(void)
{
    bool any = false;
    if ((fa_step == FA_ST_WALK || fa_step == FA_ST_WALK2) && fa_m->walk.copying) {
        fa_fs_unlink(0, fa_m->walk.dst);
        fa_m->walk.copying = false;
        any = true;
    }
    if (fa_jinfo_made) {
        fa_fs_unlink(0, fa_m->jinfo);
        fa_jinfo_made = false;
        any = true;
    }
    return any;
}

static void fa_job_fail(void)
{
    fa_job_clean();
    fa_job_end(true);
}

/* The next item: selected (or every one with fa_jall), never the note. */
static bool fa_job_next(void)
{
    for (uint32_t i = fa_jnext; i < fa_m->folder.n && i < fa_m->list.n; i++)
        if (fa_jall || fa_m->items[i].selected) {
            fa_jitem = i;
            fa_jnext = i + 1;
            return true;
        }
    return false;
}

/* A walk that ended without finishing: its message. */
static void fa_walk_failed(enum files_walk_result w, const char *what, const char *name)
{
    if (w == FILES_WALK_DEEP) {
        char deep[FILES_OPS_LINE_MAX];
        files_ops_deep_text(fa_m->walk.done, deep);     /* plan C5: how many items were done */
        fa_status(deep, true);
        klogf("Files: stopped: nested too deeply (more than 64 levels) after %u items\n", fa_m->walk.done);
    } else if (w == FILES_WALK_TOO_LONG) {
        fa_status("This path is too long.", true);
    } else if (w == FILES_WALK_LINK) {
        fa_name_msg("Can't copy", name, ": it is a link, and links can't be copied yet.");
    } else {
        fa_error(what, name, (int)fa_m->walk.err);
    }
}

/* One step of the walk running; true when it has finished (its result in
 * *res). */
static bool fa_walk_step(enum files_walk_result *res)
{
    *res = files_walk_step(&fa_m->walk);
    return *res != FILES_WALK_MORE;
}

/* ---- Move to Trash (C6) ---- */

static const char *const fa_trash_dirs[5] = { FILES_HOME "/.local", FILES_HOME "/.local/share", TRASH_DIR, FA_TFILES,
                                              FA_TINFO };

static void fa_step_trash(void)
{
    const char *name = files_entry_name(&fa_m->folder, fa_jitem);
    char rb[24];
    int r;
    switch (fa_step) {
    case FA_ST_ITEM:
        if (!fa_job_next()) {
            klogf("Files: %u items moved to the Trash\n", fa_jdone);
            fa_job_end(false);
            return;
        }
        if (!fa_join(fa_m->jsrc, fa_m->path, files_entry_name(&fa_m->folder, fa_jitem))) {
            fa_status("This path is too long.", true);
            fa_job_fail();
            return;
        }
        fa_jk = 1;
        fa_step = fa_jdirs_made >= 5 ? FA_ST_SLOT : FA_ST_DIRS;
        return;
    case FA_ST_DIRS:                       /* the Trash's folders, made when first needed (D3) */
        r = fa_create(fa_trash_dirs[fa_jdirs_made], true);
        if (r == -FS_EEXIST && !fa_is_folder(0, fa_trash_dirs[fa_jdirs_made]))
            r = -FS_ENOTDIR;            /* something else has the name: never followed (rule SL) */
        if (r < 0 && r != -FS_EEXIST) {
            fa_name_msg("Can't move", name, "");
            ksnprintf(fa_m->status + fa_len(fa_m->status), FA_STATUS_MAX - fa_len(fa_m->status),
                      " to the Trash: %s.", fa_reason(r, rb, sizeof(rb)));
            fa_job_fail();
            return;
        }
        if (r == 0 && fa_jdirs_made == 4)
            klogf("Files: the Trash folders made in " TRASH_DIR "\n");
        if (++fa_jdirs_made >= 5)
            fa_step = FA_ST_SLOT;
        return;
    case FA_ST_SLOT: {                     /* a free slot: no info file and no item of that name */
        uint32_t nl = fa_m->folder.e[fa_jitem].name_len;
        if (fa_jk > TRASH_SLOT_MAX) {
            fa_name_msg("Can't move", name, " to the Trash: too many items with this name.");
            fa_job_fail();
            return;
        }
        if (!trash_slot_name(name, nl, fa_jk, fa_m->slot) || !fa_join(fa_m->jdst, FA_TFILES, fa_m->slot) ||
            !fa_info_path(fa_m->jinfo, fa_m->slot)) {
            fa_status("This path is too long.", true);
            fa_job_fail();
            return;
        }
        uint32_t tries = 0;
        do {
            struct vfs *v = vfs_root();
            struct vnode *n = 0;
            fd_vfs_acquire();
            r = fa_lookup_locked(fa_m->jdst, &n);
            if (r == 0)
                r = -FS_EEXIST;
            else if (r == -FS_ENOENT)
                r = vfs_create(v, v->root, fa_m->jinfo, VNODE_FILE, 0600u, 0);    /* exclusively */
            fd_vfs_release();
        } while (ext2_disk_retry(&r, &tries));
        if (r == -FS_EEXIST) {
            fa_jk++;
            return;
        }
        if (r < 0) {
            fa_name_msg("Can't move", name, "");
            ksnprintf(fa_m->status + fa_len(fa_m->status), FA_STATUS_MAX - fa_len(fa_m->status),
                      " to the Trash: %s.", fa_reason(r, rb, sizeof(rb)));
            fa_job_fail();
            return;
        }
        fa_jinfo_made = true;
        fa_step = FA_ST_WRITE;
        return;
    }
    case FA_ST_WRITE: {                    /* [Trash Info], Path=, DeletionDate= (trash_info_build) */
        struct trash_date d;
        files_ops_date(vfs_now(), &d);
        uint32_t len = trash_info_build(fa_m->jsrc, fa_len(fa_m->jsrc), &d, (char *)fa_m->info_buf, TRASH_INFO_MAX);
        uint32_t tries = 0;
        r = -FS_EINVAL;
        if (len) {
            do {
                struct vfs *v = vfs_root();
                struct vnode *n = 0;
                fd_vfs_acquire();
                r = fa_lookup_locked(fa_m->jinfo, &n);
                int64_t put = 0;
                if (r == 0)
                    put = vfs_write(v, n, 0, fa_m->info_buf, len);
                fd_vfs_release();
                if (r == 0 && put != (int64_t)len)
                    r = put < 0 ? (int)put : -FS_EIO;
            } while (ext2_disk_retry(&r, &tries));
        }
        if (r < 0) {
            fa_name_msg("Can't move", name, "");
            ksnprintf(fa_m->status + fa_len(fa_m->status), FA_STATUS_MAX - fa_len(fa_m->status),
                      " to the Trash: %s.", fa_reason(r, rb, sizeof(rb)));
            fa_job_fail();
            return;
        }
        fa_step = FA_ST_MOVE;
        return;
    }
    case FA_ST_MOVE:                       /* the item into files/ (its name never replaced) */
        r = fa_rename_free(fa_m->jsrc, fa_m->jdst);
        if (r < 0) {
            fa_name_msg("Can't move", name, "");
            ksnprintf(fa_m->status + fa_len(fa_m->status), FA_STATUS_MAX - fa_len(fa_m->status),
                      " to the Trash: %s.", fa_reason(r, rb, sizeof(rb)));
            fa_job_fail();              /* the info file removed */
            return;
        }
        fa_jinfo_made = false;
        fa_jdone++;
        {
            char shown[FILES_NAME_MAX + 1], slot[FILES_NAME_MAX + 1];
            ui_name_shown(name, shown, sizeof(shown));
            ui_name_shown(fa_m->slot, slot, sizeof(slot));
            klogf("Files: '%s' moved to the Trash as '%s' (info file first)\n", shown, slot);
        }
        fa_working();
        fa_step = FA_ST_ITEM;
        return;
    default:
        fa_job_fail();
        return;
    }
}

/* ---- Delete permanently (in a folder or the Trash) ---- */

static void fa_step_delete(void)
{
    const char *name = files_entry_name(&fa_m->folder, fa_jitem);
    enum files_walk_result w;
    switch (fa_step) {
    case FA_ST_ITEM:
        if (!fa_job_next()) {
            klogf("Files: %u items deleted permanently\n", fa_jdone);
            fa_job_end(false);
            return;
        }
        name = files_entry_name(&fa_m->folder, fa_jitem);
        if (!fa_join(fa_m->jsrc, fa_m->view.trash ? FA_TFILES : fa_m->path, name) ||
            (fa_m->view.trash && !fa_info_path(fa_m->jinfo, name)) ||
            !files_walk_start(&fa_m->walk, &fa_fs, FILES_WALK_DELETE, fa_m->jsrc, fa_m->jsrc)) {
            fa_status("This path is too long.", true);
            fa_job_fail();
            return;
        }
        fa_step = FA_ST_WALK;
        return;
    case FA_ST_WALK:                       /* a link removed as an entry, never followed */
        if (!fa_walk_step(&w))
            return;
        if (w != FILES_WALK_DONE) {
            fa_walk_failed(w, "Can't delete", name);
            fa_job_fail();
            return;
        }
        fa_step = fa_m->view.trash ? FA_ST_UNINFO : FA_ST_ITEM;
        if (!fa_m->view.trash)
            goto deleted;
        return;
    case FA_ST_UNINFO:
        fa_fs_unlink(0, fa_m->jinfo);   /* none there: nothing to remove */
        fa_step = FA_ST_ITEM;
        goto deleted;
    default:
        fa_job_fail();
        return;
    }
deleted:
    fa_jdone++;
    {
        char shown[FILES_NAME_MAX + 1];
        ui_name_shown(name, shown, sizeof(shown));
        klogf("Files: '%s' deleted permanently\n", shown);
    }
    fa_working();
}

/* ---- Paste (copy, or move: a rename on one filesystem, else a copy and
 * a delete; C5, rule SL) ---- */

/* files_unique_name's test: the name is taken in Paste's target folder (or
 * the folder ctx names). */
static bool fa_taken_in(void *ctx, const char *name)
{
    const char *dir = ctx ? (const char *)ctx : fa_m->jdir;
    if (!fa_join(fa_m->scratch, dir, name))
        return true;
    return fa_exists(0, fa_m->scratch);
}

static void fa_step_paste(void)
{
    uint32_t sl = fa_len(fa_m->jsrc), off = files_ops_name_off(fa_m->jsrc, sl);
    const char *name = fa_m->jsrc + off;
    enum files_walk_result w;
    switch (fa_step) {
    case FA_ST_ITEM: {
        /* 1.5 stage 14b: a drop takes the selected items of the folder
         * shown (fa_job_next), a paste the clipboard's paths. */
        if (fa_jdrag ? !fa_job_next() : fa_jnext >= fa_m->clip.n) {
            klogf("Files: %s %u items into %s (%s)\n", fa_jdrag ? "dropped" : "pasted", fa_jdone, fa_m->jdir,
                  fa_jmove ? "move" : "copy");
            if (fa_jmove && !fa_jdrag)
                files_clip_clear(&fa_m->clip);      /* a cut is pasted once */
            fa_job_end(fa_keep_status);
            return;
        }
        uint32_t pl = 0;
        if (fa_jdrag) {
            if (!fa_join(fa_m->jsrc, fa_m->path, files_entry_name(&fa_m->folder, fa_jitem))) {
                fa_status("This path is too long.", true);
                fa_job_fail();
                return;
            }
            pl = fa_len(fa_m->jsrc);
        } else {
            const char *p = files_clip_path(&fa_m->clip, fa_jnext++, &pl);
            if (!p || pl == 0 || pl > FILES_PATH_MAX)
                return;
            for (uint32_t i = 0; i < pl; i++)
                fa_m->jsrc[i] = p[i];
            fa_m->jsrc[pl] = '\0';
        }
        off = files_ops_name_off(fa_m->jsrc, pl);
        name = fa_m->jsrc + off;
        uint32_t dl = fa_len(fa_m->jdir);
        if (files_ops_into_itself(fa_m->jsrc, pl, fa_m->jdir, dl)) {
            fa_name_msg("Can't paste", name, " into itself.");
            fa_job_fail();
            return;
        }
        uint32_t parl = files_ops_parent_len(fa_m->jsrc, pl);
        bool here = parl == dl;
        for (uint32_t i = 0; here && i < dl; i++)
            here = fa_m->jsrc[i] == fa_m->jdir[i];
        if (here && fa_jmove)
            return;                     /* cut and pasted where it is: nothing to do */
        enum files_kind k = fa_fs_lstat(0, fa_m->jsrc);
        if (k == FILES_K_NONE || k == FILES_K_ERROR) {
            fa_error("Can't paste", name, k == FILES_K_NONE ? -FS_ENOENT : -FS_EIO);
            fa_job_fail();
            return;
        }
        char saved = fa_m->jsrc[parl];
        fa_m->jsrc[parl] = '\0';
        bool same = fa_same_fs(parl ? fa_m->jsrc : "/", fa_m->jdir);
        fa_m->jsrc[parl] = saved;
        if (fa_jmove && same) {
            fa_jk = 1;                  /* a rename (a link moved as a link) */
            fa_step = FA_ST_RENAME;
            return;
        }
        if (k == FILES_K_LINK) {
            fa_name_msg("Can't copy", name, ": it is a link, and links can't be copied yet.");
            fa_job_fail();
            return;
        }
        uint32_t nl = fa_len(name);
        if (!files_unique_name(name, nl, k == FILES_K_DIR, FILES_VARIANT_NUMBER, fa_taken_in, 0, fa_m->slot)) {
            fa_name_msg("Can't paste", name, ": too many items with this name.");
            fa_job_fail();
            return;
        }
        if (!fa_join(fa_m->jdst, fa_m->jdir, fa_m->slot) ||
            !files_walk_start(&fa_m->walk, &fa_fs, FILES_WALK_COPY, fa_m->jsrc, fa_m->jdst)) {
            fa_status("This path is too long.", true);
            fa_job_fail();
            return;
        }
        fa_step = FA_ST_WALK;
        return;
    }
    case FA_ST_RENAME: {
        int r = -FS_EEXIST;
        if (fa_jk <= FILES_VARIANT_MAX) {
            bool folder = fa_fs_lstat(0, fa_m->jsrc) == FILES_K_DIR;
            uint32_t vl = files_name_variant(name, fa_len(name), folder, FILES_VARIANT_NUMBER, fa_jk, fa_m->slot);
            if (vl && fa_join(fa_m->jdst, fa_m->jdir, fa_m->slot))
                r = fa_rename_free(fa_m->jsrc, fa_m->jdst);
        }
        if (r == -FS_EEXIST && fa_jk < FILES_VARIANT_MAX) {
            fa_jk++;
            return;
        }
        if (r < 0) {
            if (r == -FS_EEXIST)
                fa_name_msg("Can't paste", name, ": too many items with this name.");
            else
                fa_error("Can't paste", name, r);
            fa_job_fail();
            return;
        }
        fa_jdone++;
        fa_working();
        fa_step = FA_ST_ITEM;
        return;
    }
    case FA_ST_WALK:
        if (!fa_walk_step(&w))
            return;
        if (w != FILES_WALK_DONE) {
            fa_walk_failed(w, "Can't paste", name);
            fa_job_fail();
            return;
        }
        if (fa_m->walk.links_skipped) {
            /* Links inside were left (rule SL): a move keeps its source. */
            fa_name_msg("Can't copy", fa_m->walk.link_name, ": it is a link, and links can't be copied yet.");
            fa_keep_status = true;
            fa_jdone++;
            fa_step = FA_ST_ITEM;
            return;
        }
        if (fa_jmove) {
            if (!files_walk_start(&fa_m->walk, &fa_fs, FILES_WALK_DELETE, fa_m->jsrc, fa_m->jsrc)) {
                fa_status("This path is too long.", true);
                fa_job_fail();
                return;
            }
            fa_step = FA_ST_WALK2;
            return;
        }
        fa_jdone++;
        fa_working();
        fa_step = FA_ST_ITEM;
        return;
    case FA_ST_WALK2:                      /* the source of a move across filesystems */
        if (!fa_walk_step(&w))
            return;
        if (w != FILES_WALK_DONE) {
            fa_walk_failed(w, "Can't delete", name);
            fa_job_fail();
            return;
        }
        fa_jdone++;
        fa_working();
        fa_step = FA_ST_ITEM;
        return;
    default:
        fa_job_fail();
        return;
    }
}

/* ---- Restore (C6, rule SL) ---- */

static void fa_ask_open(enum fa_ask kind, enum files_question q, const char *name, uint32_t n, const char *folder,
                        bool by_key);

static void fa_step_restore(void)
{
    const char *name = files_entry_name(&fa_m->folder, fa_jitem);
    int r;
    switch (fa_step) {
    case FA_ST_ITEM:
        if (!fa_job_next()) {
            klogf("Files: %u items restored, %u without a known original location\n", fa_jdone, fa_junknown);
            if (fa_junknown) {
                ksnprintf(fa_m->status, FA_STATUS_MAX,
                          "%u items can't be restored: their original location is unknown.", fa_junknown);
                fa_m->view.status_error = true;
            }
            fa_job_end(fa_junknown > 0);
            return;
        }
        if (!fa_m->restorable[fa_jitem]) {
            fa_junknown++;
            return;
        }
        name = files_entry_name(&fa_m->folder, fa_jitem);
        if (!fa_join(fa_m->jsrc, FA_TFILES, name) || !fa_info_path(fa_m->jinfo, name)) {
            fa_junknown++;
            return;
        }
        fa_joff = 0;
        fa_step = FA_ST_INFO;
        return;
    case FA_ST_INFO: {                     /* the info file again, through the parser only */
        struct vfs *v = vfs_root();
        struct vnode *n = 0;
        uint64_t size = 0;
        int64_t got = 0;
        fd_vfs_acquire();
        r = fa_lookup_locked(fa_m->jinfo, &n);
        if (r == 0 && (n->type != VNODE_FILE || vnode_is_link(n)))
            r = -FS_EINVAL;
        if (r == 0) {
            size = n->size;
            if (size <= TRASH_INFO_MAX && fa_joff < size) {
                uint64_t want = size - fa_joff;
                if (want > FA_CHUNK)
                    want = FA_CHUNK;
                got = vfs_read(v, n, fa_joff, fa_m->info_buf + fa_joff, want);
                if (got < 0)
                    r = (int)got;
            }
        }
        fd_vfs_release();
        if (r == 0 && size <= TRASH_INFO_MAX && got > 0 && fa_joff + (uint64_t)got < size) {
            fa_joff += (uint64_t)got;
            return;
        }
        uint64_t have = fa_joff + (uint64_t)(got > 0 ? got : 0);
        if (r < 0 || size > TRASH_INFO_MAX ||
            trash_info_parse(fa_m->info_buf, (uint32_t)have, &fa_m->info) != TI_OK) {
            fa_junknown++;
            fa_step = FA_ST_ITEM;
            return;
        }
        fa_step = FA_ST_CHECK;
        return;
    }
    case FA_ST_CHECK: {                    /* every folder there a real folder, never a link */
        uint32_t at = 0;
        enum trash_restore tr = trash_restore_check(fa_m->info.path, fa_m->info.path_len, fa_trash_lstat, 0,
                                                    fa_m->scratch, &at);
        if (tr == TR_OK) {
            fa_copy(fa_m->jdst, sizeof(fa_m->jdst), fa_m->info.path);
            fa_step = FA_ST_RESTORE;
        } else if (tr == TR_MAKE_PARENTS) {
            fa_jk = at;
            fa_step = FA_ST_PARENTS;
        } else if (tr == TR_EXISTS) {
            uint32_t pl = files_ops_parent_len(fa_m->info.path, fa_m->info.path_len);
            char where[FA_LOC_MAX];
            files_ops_location(fa_m->info.path, pl, FA_LOC_MAX - 1u, where, sizeof(where));
            fa_step = FA_ST_ASKING;
            fa_ask_open(FA_ASK_EXISTS, FILES_Q_EXISTS,
                        fa_m->info.path + files_ops_name_off(fa_m->info.path, fa_m->info.path_len), 0, where, false);
        } else if (tr == TR_PARENT_NOT_FOLDER && at <= fa_m->info.path_len) {
            char folder[FILES_PATH_MAX + 1], shown[96];
            for (uint32_t i = 0; i < at; i++)
                folder[i] = fa_m->info.path[i];
            folder[at] = '\0';
            ui_name_shown(folder, shown, sizeof(shown));
            fa_name_msg("Can't restore", name, "");
            ksnprintf(fa_m->status + fa_len(fa_m->status), FA_STATUS_MAX - fa_len(fa_m->status),
                      ": %s is not a folder.", shown);
            fa_job_fail();
        } else {
            fa_error("Can't restore", name, -FS_EIO);
            fa_job_fail();
        }
        return;
    }
    case FA_ST_PARENTS: {                  /* the next missing folder made again: path[0, fa_jk) */
        uint32_t pl = files_ops_parent_len(fa_m->info.path, fa_m->info.path_len), j = fa_jk < pl ? fa_jk : pl;
        char saved = fa_m->info.path[j];
        fa_m->info.path[j] = '\0';
        r = fa_create(fa_m->info.path, true);
        fa_m->info.path[j] = saved;
        if (r < 0 && r != -FS_EEXIST) {
            fa_error("Can't restore", name, r);
            fa_job_fail();
            return;
        }
        if (j >= pl) {
            fa_step = FA_ST_CHECK;         /* checked again (rule SL), then renamed */
            return;
        }
        j++;                            /* the next folder's end */
        while (j < pl && fa_m->info.path[j] != '/')
            j++;
        fa_jk = j;
        return;
    }
    case FA_ST_RESTORE:
        r = fa_rename_free(fa_m->jsrc, fa_m->jdst);
        if (r < 0) {
            fa_error("Can't restore", name, r);
            fa_job_fail();
            return;
        }
        fa_step = FA_ST_UNINFO;
        return;
    case FA_ST_UNINFO: {
        fa_fs_unlink(0, fa_m->jinfo);
        fa_jdone++;
        char shown[FILES_NAME_MAX + 1], where[96];
        uint32_t dl = fa_len(fa_m->jdst), pl = files_ops_parent_len(fa_m->jdst, dl);
        ui_name_shown(fa_m->jdst + files_ops_name_off(fa_m->jdst, dl), shown, sizeof(shown));     /* as restored */
        fa_m->jdst[pl] = '\0';
        ui_name_shown(pl ? fa_m->jdst : "/", where, sizeof(where));
        klogf("Files: '%s' restored to %s\n", shown, where);
        fa_working();
        fa_step = FA_ST_ITEM;
        return;
    }
    default:
        return;                         /* FA_ST_ASKING: the question's answer goes on */
    }
}

/* ---- Empty Trash: every item, then every info file ---- */

static void fa_step_empty(void)
{
    enum files_walk_result w;
    uint64_t cookie = 0;
    int r;
    switch (fa_step) {
    case FA_ST_ITEM:
    case FA_ST_EFILES:
    case FA_ST_EINFO: {
        bool info = fa_step == FA_ST_EINFO;
        const char *dir = info ? FA_TINFO : FA_TFILES;
        r = fa_fs_read_dir(0, dir, &cookie, fa_m->slot);
        if (r == -FS_ENOENT)
            r = 0;                      /* no such folder: nothing in it */
        if (r < 0) {
            fa_error("Can't empty the Trash", 0, r);
            fa_job_fail();
            return;
        }
        if (r == 0) {
            if (info) {
                klogf("Files: the Trash emptied: %u items deleted\n", fa_jdone);
                fa_job_end(false);
            } else {
                fa_step = FA_ST_EINFO;
            }
            return;
        }
        if (!fa_join(fa_m->jsrc, dir, fa_m->slot) ||
            !files_walk_start(&fa_m->walk, &fa_fs, FILES_WALK_DELETE, fa_m->jsrc, fa_m->jsrc)) {
            fa_status("This path is too long.", true);
            fa_job_fail();
            return;
        }
        fa_step = info ? FA_ST_WALK2 : FA_ST_WALK;
        return;
    }
    case FA_ST_WALK:
    case FA_ST_WALK2: {
        bool info = fa_step == FA_ST_WALK2;
        if (!fa_walk_step(&w))
            return;
        if (w != FILES_WALK_DONE) {
            fa_walk_failed(w, "Can't delete", fa_m->slot);
            fa_job_fail();
            return;
        }
        if (!info) {
            fa_jdone++;
            fa_working();
        }
        fa_step = info ? FA_ST_EINFO : FA_ST_EFILES;
        return;
    }
    default:
        fa_job_fail();
        return;
    }
}

static void fa_job_step(void)
{
    uint32_t before = fa_jdone;
    enum fa_job j = fa_job;
    switch (fa_job) {
    case FA_JOB_TRASH:
        fa_step_trash();
        break;
    case FA_JOB_DELETE:
        fa_step_delete();
        break;
    case FA_JOB_PASTE:
        fa_step_paste();
        break;
    case FA_JOB_RESTORE:
        fa_step_restore();
        break;
    case FA_JOB_EMPTY:
        fa_step_empty();
        break;
    default:
        return;
    }
    if (fa_job == j && fa_jdone != before)
        fa_changed();                   /* "Working... <n> items done" */
}

/* ---- the question panel (Q2) ---- */

static void fa_ask_open(enum fa_ask kind, enum files_question q, const char *name, uint32_t n, const char *folder,
                        bool by_key)
{
    struct ui_files_view *v = &fa_m->view;
    files_ops_question(q, name, n, folder, fa_m->ask1, fa_m->ask2, fa_m->ask_label);
    fa_ask = kind;
    v->mode = UI_FILES_ASK;
    v->ask_focus = 0;                   /* Cancel focused */
    v->ask_ring = by_key;
    v->hover = v->pressed = UF_NONE;
    fa_ask_press = UF_NONE;
    klogf("Files: asks '%s%s%s' (%s, Cancel)\n", fa_m->ask1, fa_m->ask2[0] ? " " : "", fa_m->ask2, fa_m->ask_label);
}

static void fa_ask_answer(bool yes)
{
    struct ui_files_view *v = &fa_m->view;
    enum fa_ask k = fa_ask;
    fa_ask = FA_ASK_NONE;
    v->mode = UI_FILES_LIST;
    v->hover = v->pressed = UF_NONE;
    klogf("Files: the question answered: %s\n", yes ? fa_m->ask_label : "Cancel");
    switch (k) {
    case FA_ASK_DELETE:
    case FA_ASK_NOTRASH:
        if (yes)
            fa_job_begin(FA_JOB_DELETE, false);
        else
            fa_count_status();
        break;
    case FA_ASK_EMPTY:
        if (yes)
            fa_job_begin(FA_JOB_EMPTY, false);
        else
            fa_count_status();
        break;
    case FA_ASK_EXISTS:                 /* Restore: Keep both, or Cancel stops (Q5) */
        if (yes) {
            uint32_t pl = files_ops_parent_len(fa_m->info.path, fa_m->info.path_len);
            uint32_t off = files_ops_name_off(fa_m->info.path, fa_m->info.path_len);
            const char *nm = fa_m->info.path + off;
            bool folder = fa_fs_lstat(0, fa_m->jsrc) == FILES_K_DIR;
            fa_copy(fa_m->jdir, sizeof(fa_m->jdir), fa_m->info.path);
            fa_m->jdir[pl] = '\0';
            if (pl == 0)
                fa_copy(fa_m->jdir, sizeof(fa_m->jdir), "/");
            if (!files_unique_name(nm, fa_m->info.path_len - off, folder, FILES_VARIANT_RESTORED, fa_taken_in, 0,
                                   fa_m->slot) || !fa_join(fa_m->jdst, fa_m->jdir, fa_m->slot)) {
                fa_name_msg("Can't restore", files_entry_name(&fa_m->folder, fa_jitem),
                            ": too many items with this name.");
                fa_job_fail();
                break;
            }
            fa_step = FA_ST_RESTORE;
        } else {
            klogf("Files: Restore stopped: %u items restored\n", fa_jdone);
            fa_job_end(false);
        }
        break;
    default:
        break;
    }
}

/* ---- starting the actions ---- */

/* Cut or Copy: every selected item (at most 64, Q-multi-select). */
static void fa_clip_set(bool cut)
{
    uint32_t n = fa_nsel();
    if (n == 0 || fa_m->view.trash || (cut && !fa_writable))
        return;
    if (n > FILES_CLIP_MAX) {
        fa_status("Only 64 items can be cut or copied at once.", true);
        return;
    }
    files_clip_begin(&fa_m->clip, cut);
    for (uint32_t i = 0; i < fa_m->folder.n && i < fa_m->list.n; i++)
        if (fa_m->items[i].selected) {
            if (!fa_join(fa_m->scratch, fa_m->path, files_entry_name(&fa_m->folder, i)) ||
                files_clip_add(&fa_m->clip, fa_m->scratch, fa_len(fa_m->scratch)) != FILES_CLIP_OK) {
                files_clip_clear(&fa_m->clip);
                fa_status("This path is too long.", true);
                return;
            }
        }
    fa_count_status();
    klogf("Files: %u items %s from %s\n", n, cut ? "cut" : "copied", fa_m->path);
}

/* Paste's (and since 1.5 stage 14b a drop's) target folder fa_m->jdir
 * checked by rule SL: every part a real folder; else the message. */
static bool fa_jdir_ok(void)
{
    uint32_t at = 0;
    enum files_target t = files_check_target(&fa_fs, fa_m->jdir, fa_m->scratch, &at);
    if (t == FILES_TARGET_OK)
        return true;
    char folder[FILES_PATH_MAX + 1], shown[96];
    uint32_t k = at <= fa_len(fa_m->jdir) ? at : 0;
    for (uint32_t i = 0; i < k; i++)
        folder[i] = fa_m->jdir[i];
    folder[k] = '\0';
    ui_name_shown(folder, shown, sizeof(shown));
    ksnprintf(fa_m->status, FA_STATUS_MAX, "Can't paste here: %s is not a folder.", shown);
    fa_m->view.status_error = true;
    return false;
}

/* Paste into the folder shown, or into folder item `into` (a menu's). */
static void fa_paste(int32_t into)
{
    if (files_clip_empty(&fa_m->clip) || fa_m->view.trash || !fa_writable)
        return;
    const char *dir = fa_m->path;
    if (into >= 0 && (uint32_t)into < fa_m->folder.n && fa_m->folder.e[into].kind == FILES_K_DIR) {
        if (!fa_join(fa_m->scratch2, fa_m->path, files_entry_name(&fa_m->folder, (uint32_t)into))) {
            fa_status("This path is too long.", true);
            return;
        }
        dir = fa_m->scratch2;
    }
    fa_copy(fa_m->jdir, sizeof(fa_m->jdir), dir);
    if (!fa_jdir_ok())
        return;
    fa_jmove = fa_m->clip.cut;
    fa_keep_status = false;
    fa_job_begin(FA_JOB_PASTE, false);
}

/* Delete (Move to Trash; in the Trash, Delete permanently) and Shift+Delete. */
static void fa_delete(bool permanently, bool by_key)
{
    uint32_t n = fa_nsel();
    if (n == 0 || !fa_writable)
        return;
    int32_t first = -1;
    bool all_trashable = true;
    for (uint32_t i = 0; i < fa_m->folder.n && i < fa_m->list.n; i++)
        if (fa_m->items[i].selected) {
            if (first < 0)
                first = (int32_t)i;
            if (!fa_join(fa_m->scratch, fa_m->path, files_entry_name(&fa_m->folder, i)) ||
                !files_ops_trashable(fa_m->scratch, fa_len(fa_m->scratch)))
                all_trashable = false;
        }
    const char *name = files_entry_name(&fa_m->folder, (uint32_t)first);
    if (fa_m->view.trash || permanently)
        fa_ask_open(FA_ASK_DELETE, n == 1 ? FILES_Q_DELETE_ONE : FILES_Q_DELETE_MANY, name, n, 0, by_key);
    else if (!all_trashable)            /* Q7: only items under /home/user go to the Trash */
        fa_ask_open(FA_ASK_NOTRASH, n == 1 ? FILES_Q_NOTRASH_ONE : FILES_Q_NOTRASH_MANY, name, n, 0, by_key);
    else
        fa_job_begin(FA_JOB_TRASH, false);
}

static void fa_empty(bool by_key)
{
    if (!fa_m->view.trash || !fa_writable || fa_m->folder.n == 0)
        return;
    fa_ask_open(FA_ASK_EMPTY, FILES_Q_EMPTY, 0, fa_m->folder.n, 0, by_key);
}

static void fa_restore(bool all)
{
    if (!fa_m->view.trash || !fa_writable || (!all && fa_nrestorable() == 0) || fa_m->folder.n == 0)
        return;
    fa_job_begin(FA_JOB_RESTORE, all);
}

/* Properties (D5): the info panel for item i, whatever it is. */
static void fa_properties(uint32_t i)
{
    if (i >= fa_m->folder.n)
        return;
    const struct files_entry *e = &fa_m->folder.e[i];
    const char *name = files_entry_name(&fa_m->folder, i);
    files_size_text(e->size, fa_m->panel_size);
    files_date_text(e->mtime, fa_m->panel_date);
    fa_m->view.panel_icon = fa_m->items[i].icon;
    fa_m->view.panel_values[0] = name;
    fa_m->view.panel_values[1] = fa_m->panel_size;
    fa_m->view.panel_values[2] = files_type_text((enum files_kind)e->kind, name, e->name_len);
    fa_m->view.panel_values[3] = fa_m->panel_date;
    fa_m->view.panel_has_app = !fa_m->view.trash && (e->kind == FILES_K_DIR ||
                               (e->kind == FILES_K_FILE &&
                                fa_same(files_type_text(FILES_K_FILE, name, e->name_len), "Text file")));
    fa_m->view.mode = UI_FILES_PANEL;
    fa_m->list.cursor = i;
}

/* Ctrl+A: every real item selected (never the limit's note). */
static void fa_select_all(void)
{
    ui_list_select_all(&fa_m->list);
    for (uint32_t i = fa_m->folder.n; i < fa_m->list.n; i++)
        fa_m->items[i].selected = false;
}

/* A Files menu opened (D5): the item's or the empty space's. */
static void fa_open_menu(bool item, int32_t x, int32_t y, bool by_key)
{
    struct files_ops_state st = fa_ops_state();
    bool trash = fa_m->view.trash;
    enum ui_menu_kind k = trash ? (item ? UI_MENU_TRASH_ITEM : UI_MENU_TRASH_SPACE)
                                : (item ? UI_MENU_FILES_ITEM : UI_MENU_FILES_SPACE);
    enum files_ops_menu om = trash ? (item ? FILES_OPS_MENU_TRASH_ITEM : FILES_OPS_MENU_TRASH_SPACE)
                                   : (item ? FILES_OPS_MENU_ITEM : FILES_OPS_MENU_SPACE);
    uint8_t ticked = 0;
    if (!item && fa_m->folder.show_hidden)
        ticked = (uint8_t)(1u << (trash ? 2 : 3));      /* Show hidden files */
    fa_changed();
    if (fa_env.menu)
        fa_env.menu(k, x, y, by_key, files_ops_greyed(om, &st), ticked);
}

/* ---- the Text Viewer ---- */

static void fa_vlog(void)
{
    if (fa_env.logging && fa_v && fa_env.viewer->open)
        kserialf("viewer: state top=%u cut=%u sb=%u len=%u reading=%u\n", fa_v->v.top, fa_v->v.cut ? 1u : 0u,
                 (uint32_t)fa_v->v.sb, fa_v->v.len, fa_vreading ? 1u : 0u);
}

static void fa_vchanged(void)
{
    fa_damage(fa_env.viewer);
    fa_vlog();
}

static void fa_open_viewer(const char *path, const char *name, uint64_t size)
{
    struct ui_window *w = fa_env.viewer;
    if (!fa_v) {
        uint32_t tables = 0;
        const char *why = 0;
        uint32_t before = pmm_free_pages();
        if (!fa_area(VM_VIEWER_BASE, FA_VPAGES, false, &tables, &why)) {
            klogf("Text Viewer: not enough memory (%s): not opened\n", why);
            fa_status("Not enough memory to open this file.", true);
            return;
        }
        fa_vpages_mapped = FA_VPAGES;
        fa_v = (struct fa_vmem *)(uintptr_t)VM_VIEWER_BASE;
        klogf("Text Viewer: opened at %s: %u pages of free RAM at 0x%016llx, zeroed; %u page tables added (free "
              "pages %u -> %u)\n", path, FA_VPAGES, (ull)VM_VIEWER_BASE, tables, before, pmm_free_pages());
    }
    fa_copy(fa_v->path, sizeof(fa_v->path), path);
    ui_name_shown(name, fa_v->title, sizeof(fa_v->title));
    fa_v->v.buf = fa_v->buf;
    fa_v->v.len = 0;
    fa_v->v.top = 0;
    fa_v->v.sb = UI_SB_NORMAL;
    fa_v->v.cut = size > UI_VIEWER_MAX;
    fa_vreading = true;
    fa_vread_t0 = fa_now;               /* 1.5 stage 14b: the busy pointer after FA_BUSY_TICKS */
    fa_voff = 0;
    fa_vsize = size;
    fa_vholds = 0;
    w->name = "Text Viewer";
    w->title = fa_v->title;
    if (w->open) {
        fa_vchanged();
        return;
    }
    int32_t s = (int32_t)fa_env.scale;
    w->w = ui_px(UI_VIEWER_W_DP, (uint32_t)s);
    w->h = ui_px(UI_VIEWER_H_DP, (uint32_t)s);
    if (w->w > fa_env.area_w)
        w->w = fa_env.area_w;
    if (w->h > fa_env.area_h)
        w->h = fa_env.area_h;
    w->min_w = ui_px(UI_VIEWER_MIN_W_DP, (uint32_t)s);
    w->min_h = ui_px(UI_VIEWER_MIN_H_DP, (uint32_t)s);
    if (w->min_w > w->w)
        w->min_w = w->w;
    if (w->min_h > w->h)
        w->min_h = w->h;
    if (!fa_env.open(w)) {
        files_app_closed(w);
        fa_status("Not enough memory to open this file.", true);
        return;
    }
    fa_vlog();
}

/* One hold of the Text Viewer's reading: at most FA_CHUNK bytes. */
static void fa_vread_hold(void)
{
    struct vfs *v = vfs_root();
    struct vnode *n = 0;
    fd_vfs_acquire();
    int r = fa_lookup_locked(fa_v->path, &n);
    int64_t got = 0;
    if (r == 0 && (n->type != VNODE_FILE || vnode_is_link(n)))
        r = -FS_EINVAL;                     /* never a folder, never a link (rule SL) */
    if (r == 0) {
        uint64_t want = UI_VIEWER_MAX - fa_voff;
        if (want > FA_CHUNK)
            want = FA_CHUNK;
        got = want ? vfs_read(v, n, fa_voff, fa_v->buf + fa_voff, want) : 0;
        if (fa_voff == 0)
            fa_vsize = n->size;
    }
    fd_vfs_release();
    fa_vholds++;
    if (r < 0 || got < 0) {
        fa_vreading = false;
        klogf("Text Viewer: read %s: stopped (error %d) after %llu bytes\n", fa_v->path,
              r < 0 ? -r : (int)-got, (ull)fa_voff);
    } else {
        fa_voff += (uint64_t)got;
        fa_v->v.len = (uint32_t)fa_voff;
        if (got == 0 || fa_voff >= UI_VIEWER_MAX) {
            fa_vreading = false;
            fa_v->v.cut = fa_vsize > UI_VIEWER_MAX;
            klogf("Text Viewer: read %s: %llu bytes in %u holds of at most %u%s\n", fa_v->path, (ull)fa_voff,
                  fa_vholds, FA_CHUNK, fa_v->v.cut ? " (only the first 64 KiB)" : "");
        }
    }
    fa_vchanged();
}

/* ---- the desktop's calls ---- */

void files_app_options(bool nomem, bool fill)
{
    fa_opt_nomem = nomem;
    fa_opt_fill = fill;
}

void files_app_setup(const struct files_app_env *env)
{
    /* Field by field: no struct copy the compiler could turn into a
     * library call (the kernel has no memcpy). */
    fa_env.files = env->files;
    fa_env.viewer = env->viewer;
    fa_env.scale = env->scale;
    fa_env.fonts = env->fonts;
    fa_env.area_w = env->area_w;
    fa_env.area_h = env->area_h;
    fa_env.damage = env->damage;
    fa_env.open = env->open;
    fa_env.logging = env->logging;
    fa_env.menu = env->menu;            /* 1.5 stage 13: the Files menus (D5) */
    struct ui_window *w = fa_env.files;
    w->name = "Files";
    w->title = "Files";
    w->icon = UI_ICON_FOLDER;
    w->paint = files_app_paint;
    w->grows = true;
    struct ui_window *t = fa_env.viewer;
    t->name = "Text Viewer";
    t->title = "Text Viewer";
    t->icon = UI_ICON_TEXTFILE;
    t->paint = files_app_viewer_paint;
    t->esc_closes = true;
    t->buttons = UI_WIN_MINIMISE | UI_WIN_MAXIMISE;
    t->resizable = true;
    t->grows = true;
}

void files_app_live_home(void)
{
    uint32_t made = 0;
    fd_vfs_acquire();
    int r = vfs_live_home(&made);
    fd_vfs_release();
    if (r < 0) {
        klogf("home: Live mode: the home folders not made (error %d)\n", -r);
        return;
    }
    klogf("home: Live mode: /home/user and its 6 folders made in memory (Desktop, Documents, Downloads, Pictures, "
          "Music, Videos)\n");
    (void)made;
    if (!fa_opt_fill)
        return;
    int fr = fa_create(FILES_HOME "/Many", true);
    uint32_t files = 0;
    char path[48];
    for (uint32_t i = 1; fr == 0 && i <= 1005; i++) {
        ksnprintf(path, sizeof(path), FILES_HOME "/Many/item-%04u.txt", i);
        fr = fa_create(path, false);
        if (fr == 0)
            files++;
    }
    klogf("Files fill test: /home/user/Many with %u empty files (test option filesfill)%s\n", files,
          fr == 0 ? "" : ": stopped by an error");
}

void files_app_open(void)
{
    struct ui_window *w = fa_env.files;
    uint32_t s = fa_env.scale;
    uint32_t tables = 0, before = pmm_free_pages();
    const char *why = 0;
    fa_nomem_view = !fa_area(VM_FILES_BASE, FA_PAGES, fa_opt_nomem, &tables, &why);
    if (fa_nomem_view) {
        klogf("Files: not enough memory (%s): only the message shown\n", why);
        fa_m = 0;
        w->w = ui_px(480, s);
        w->h = ui_px(120, s);
        w->buttons = 0;
        w->resizable = false;
        w->esc_closes = false;
    } else {
        fa_pages_mapped = FA_PAGES;
        fa_m = (struct fa_mem *)(uintptr_t)VM_FILES_BASE;
        w->w = ui_px(UI_FILES_W_DP, s);
        w->h = ui_px(UI_FILES_H_DP, s);
        if (w->w > fa_env.area_w)
            w->w = fa_env.area_w;
        if (w->h > fa_env.area_h)
            w->h = fa_env.area_h;
        w->min_w = ui_px(UI_FILES_MIN_W_DP, s);
        w->min_h = ui_px(UI_FILES_MIN_H_DP, s);
        if (w->min_w > w->w)
            w->min_w = w->w;
        if (w->min_h > w->h)
            w->min_h = w->h;
        w->buttons = UI_WIN_MINIMISE | UI_WIN_MAXIMISE;
        w->resizable = true;
        w->esc_closes = false;
        files_history_init(&fa_m->hist, fa_m->hist_store);
        files_clip_init(&fa_m->clip, fa_m->clip_store);     /* 1.5 stage 13 (Q8) */
        fa_job = FA_JOB_NONE;
        fa_step = FA_ST_ITEM;
        fa_ask = FA_ASK_NONE;
        fa_tinfo_on = fa_keep_status = fa_jinfo_made = false;
        fa_menu_item = -1;
        files_folder_init(&fa_m->folder, fa_m->entries, FILES_MAX_ENTRIES, fa_m->names, sizeof(fa_m->names), false);
        ui_list_init(&fa_m->list, UI_LIST_DETAILS, fa_m->items, 0, ui_files_panel_labels, (struct rect){ 0, 0, 0, 0 },
                     s);
        fa_m->view.mode = UI_FILES_LIST;
        fa_m->view.bar = !ext2_disk_mounted() ? UI_FILES_BAR_LIVE : ext2_disk_rw() ? UI_FILES_BAR_NONE
                                                                                    : UI_FILES_BAR_RO;
        fa_m->view.hover = fa_m->view.pressed = fa_m->view.tip = UF_NONE;
        fa_m->view.panel_icon = UI_ICON_NONE;
        fa_m->status[0] = '\0';
        fa_reading = fa_rename_after = false;
        fa_press_path = -1;
        fa_sb_drag = false;
        fa_tip_part = UF_NONE;
        fa_press = FA_PRESS_NONE;       /* 1.5 stage 14b: no press, band or drag yet */
        fa_band = fa_drag = fa_jdrag = false;
        fa_badge_r = (struct rect){ 0, 0, 0, 0 };
        fa_drop_item = fa_drop_place = -1;
        fa_m->view.card_sel = fa_m->view.card_hover = -1;
        const char *start = fa_is_folder(0, FILES_HOME) ? FILES_HOME : "/home";
        klogf("Files: opened at %s: %u pages of free RAM at 0x%016llx, zeroed; %u page tables added (free pages %u "
              "-> %u)\n", start, FA_PAGES, (ull)VM_FILES_BASE, tables, before, pmm_free_pages());
        fa_go(FILES_VIEW_FOLDER, start);
    }
    if (!fa_env.open(w)) {
        klogf("Files: not opened (%u windows are open already)\n", (uint32_t)UI_WINDOWS_MAX);
        files_app_closed(w);
        return;
    }
    fa_changed();
    if (fa_nomem_view)
        fa_log_state();
}

static bool fa_gesture_cancel(const char *why);    /* 1.5 stage 14b: defined with the gestures below */

void files_app_closed(struct ui_window *w)
{
    if (w == fa_env.files) {
        files_app_stop("the window was closed");    /* 1.5 stage 13 (Q9) */
        if (fa_m)
            fa_gesture_cancel("the window was closed");     /* 1.5 stage 14b */
        fa_press = FA_PRESS_NONE;
        fa_band = fa_drag = false;
        fa_badge_r = (struct rect){ 0, 0, 0, 0 };
        uint32_t before = pmm_free_pages(), n = 0;
        bool had = fa_m != 0;
        fa_m = 0;                           /* no pointer into the pages left */
        fa_reading = false;
        if (had)
            n = fa_unarea(VM_FILES_BASE, fa_pages_mapped);
        fa_pages_mapped = 0;
        fa_nomem_view = false;
        if (had)
            klogf("Files: closed: %u pages unmapped (TLB invalidated) and returned; free pages %u -> %u\n", n,
                  before, pmm_free_pages());
        else
            klogf("Files: closed (no memory was mapped)\n");
    } else if (w == fa_env.viewer) {
        uint32_t before = pmm_free_pages(), n = 0;
        bool had = fa_v != 0;
        fa_v = 0;
        fa_vreading = false;
        w->title = "Text Viewer";
        if (had)
            n = fa_unarea(VM_VIEWER_BASE, fa_vpages_mapped);
        fa_vpages_mapped = 0;
        if (had)
            klogf("Text Viewer: closed: %u pages unmapped (TLB invalidated) and returned; free pages %u -> %u\n", n,
                  before, pmm_free_pages());
    }
}

void files_app_stop(const char *why)
{
    if (!fa_m || fa_job == FA_JOB_NONE)
        return;
    bool partial = fa_job_clean();
    if (partial)
        klogf("Files: action stopped (%s): the partial copy removed\n", why);
    else
        klogf("Files: action stopped (%s)\n", why);
    fa_job = FA_JOB_NONE;
    fa_step = FA_ST_ITEM;
    fa_ask = FA_ASK_NONE;
    fa_jdrag = false;                   /* 1.5 stage 14b */
    if (fa_m->view.mode == UI_FILES_ASK)
        fa_m->view.mode = UI_FILES_LIST;
}

void files_app_right(struct ui_window *top, int32_t x, int32_t y)
{
    if (!fa_m || top != fa_env.files || fa_env.files->minimised)
        return;
    struct ui_files_view *v = &fa_m->view;
    if (v->mode != UI_FILES_LIST || v->renaming || fa_reading || fa_job != FA_JOB_NONE)
        return;
    if (v->thispc || fa_drag || fa_band)    /* 1.5 stage 14b: no menu in This PC or during a drag */
        return;
    struct ui_files_layout lay;
    int32_t index = -1;
    ui_files_layout(fa_env.files->r, fa_env.scale, fa_env.fonts->regular, v->bar != UI_FILES_BAR_NONE, &lay);
    fa_m->list.r = lay.content;
    if (ui_files_hit(&lay, v, fa_env.scale, fa_env.fonts->regular, x, y, &index) != UF_CONTENT)
        return;
    struct ui_list *l = &fa_m->list;
    int32_t i = ui_list_item_at(l, x, y);
    if (i >= (int32_t)fa_m->folder.n)
        i = -1;                         /* the limit's note: as empty space */
    if (ui_scrollbar_hit(ui_list_sb_rect(l), ui_list_lines(l), ui_list_page(l), l->top, fa_env.scale, x, y) !=
        UI_SB_HIT_NONE)
        return;
    if (i >= 0) {
        if (!l->items[i].selected)
            ui_list_click(l, i, 0);     /* selected alone, unless part of the selection */
        l->cursor = (uint32_t)i;
        fa_menu_item = fa_m->folder.e[i].kind == FILES_K_DIR && fa_nsel() == 1 ? i : -1;
    } else {
        ui_list_click(l, -1, 0);        /* the selection cleared */
        fa_menu_item = -1;
    }
    fa_count_status();
    fa_open_menu(i >= 0, x, y, false);
}

void files_app_menu(enum ui_menu_action a)
{
    if (!fa_m || fa_m->view.mode != UI_FILES_LIST || fa_reading || fa_job != FA_JOB_NONE)
        return;
    fa_update_view();
    fa_count_status();
    switch (a) {
    case UI_MENU_F_OPEN:
        if (fa_target() >= 0)
            fa_open_item((uint32_t)fa_target());
        break;
    case UI_MENU_F_CUT:
    case UI_MENU_F_COPY:
        fa_clip_set(a == UI_MENU_F_CUT);
        break;
    case UI_MENU_F_PASTE:
        fa_paste(fa_menu_item);
        break;
    case UI_MENU_F_RENAME:
        fa_rename_start();
        break;
    case UI_MENU_F_TRASH:
    case UI_MENU_F_DELETE:
        fa_delete(false, false);
        break;
    case UI_MENU_F_PROPERTIES:
        if (fa_target() >= 0)
            fa_properties((uint32_t)fa_target());
        break;
    case UI_MENU_F_NEW_FOLDER:
    case UI_MENU_F_NEW_FILE:
        fa_new(a == UI_MENU_F_NEW_FOLDER);
        break;
    case UI_MENU_F_HIDDEN:
        fa_button(UF_HIDDEN);
        break;
    case UI_MENU_F_RESTORE:
    case UI_MENU_F_RESTORE_ALL:
        fa_restore(a == UI_MENU_F_RESTORE_ALL);
        break;
    case UI_MENU_F_EMPTY:
        fa_empty(false);
        break;
    default:
        break;
    }
    fa_menu_item = -1;
    fa_changed();
}

static void fa_auto_scroll(uint64_t now);  /* 1.5 stage 14b: defined with the gestures below */

void files_app_poll(uint64_t now)
{
    fa_now = now;                       /* 1.5 stage 14b: the readings' start times */
    fa_auto_scroll(now);                /* 1.5 stage 14b: the band or a drag at an edge */
    if (fa_m && fa_reading) {
        if (fa_tinfo_on)
            fa_tinfo_step();            /* 1.5 stage 13: the Trash's info files */
        else
            fa_read_hold();
        if (!fa_reading)
            fa_changed();
    } else if (fa_m && fa_job != FA_JOB_NONE && fa_step != FA_ST_ASKING) {
        fa_job_step();                  /* 1.5 stage 13: one step of an action */
        if (fa_m->view.mode == UI_FILES_ASK || fa_reading)
            fa_changed();
    }
    if (fa_v && fa_vreading)
        fa_vread_hold();
    if (fa_m && fa_tip_part != UF_NONE && fa_m->view.tip == UF_NONE && now - fa_tip_since >= UI_FILES_TIP_TICKS &&
        fa_m->view.hover == fa_tip_part && fa_m->view.enabled[fa_tip_part]) {
        fa_m->view.tip = fa_tip_part;
        fa_changed();
    }
}

/* ---- 1.5 stage 14b: box selection, drag and drop (notes (14), (15)) ---- */

static uint8_t fa_mods;                 /* Shift and Ctrl now (the pointer's packets and the keys) */
static bool fa_drag_on;                 /* the pointer over the Files window at the last move */

/* The i-th selected item's name (files_ops_drop's names). */
static const char *fa_drag_name(void *ctx, uint32_t i, uint32_t *len)
{
    (void)ctx;
    uint32_t k = 0;
    for (uint32_t j = 0; j < fa_m->folder.n && j < fa_m->list.n; j++)
        if (fa_m->items[j].selected && k++ == i) {
            *len = fa_m->folder.e[j].name_len;
            return files_entry_name(&fa_m->folder, j);
        }
    *len = 0;
    return 0;
}

/* The drag's target folder (fa_m->drop_path), in one hold: a real folder
 * (rule SL: a link is not one), on the folder shown's filesystem or not,
 * able to change or not. False if it is not a folder (no target). */
static bool fa_drop_fs(void)
{
    struct vnode *a = 0, *b = 0;
    fd_vfs_acquire();
    bool ok = fa_lookup_locked(fa_m->path, &a) == 0 && fa_lookup_locked(fa_m->drop_path, &b) == 0 &&
              b->type == VNODE_DIR && !vnode_is_link(b);
    if (ok) {
        fa_drop_same = a->fs_id == b->fs_id;
        fa_drop_writable = vfs_writable(vfs_root(), b) == 0;
    }
    fd_vfs_release();
    return ok;
}

/* What a release at (x, y) would do (files_ops_drop); logged when it
 * changes; true if it changed. `on`: the pointer is over the Files window. */
static bool fa_drag_target(bool on, int32_t x, int32_t y)
{
    struct ui_files_view *v = &fa_m->view;
    enum files_drop_target t = FILES_DT_NONE;
    int32_t item = -1, place = -1;
    bool path = false;
    fa_drag_on = on;
    if (on) {
        struct ui_files_layout lay;
        int32_t index = -1;
        ui_files_layout(fa_env.files->r, fa_env.scale, fa_env.fonts->regular, v->bar != UI_FILES_BAR_NONE, &lay);
        fa_m->list.r = lay.content;
        enum ui_files_part part = ui_files_hit(&lay, v, fa_env.scale, fa_env.fonts->regular, x, y, &index);
        if (part == UF_CONTENT && !v->thispc && v->mode == UI_FILES_LIST) {
            int32_t i = ui_list_item_at(&fa_m->list, x, y);
            if (i >= 0 && i < (int32_t)fa_m->folder.n) {
                item = i;
                path = fa_m->folder.e[i].kind == FILES_K_DIR &&
                       fa_join(fa_m->drop_path, fa_m->path, files_entry_name(&fa_m->folder, (uint32_t)i));
                t = path ? FILES_DT_FOLDER : FILES_DT_FILE;
            }
        } else if (part == UF_PLACE && index >= 0 && index < UI_FILES_PLACES) {
            place = index;
            if (index < 7) {
                path = fa_copy(fa_m->drop_path, sizeof(fa_m->drop_path), fa_place_paths[index]);
                t = path ? FILES_DT_FOLDER : FILES_DT_NONE;
            } else if (index == 7) {
                t = FILES_DT_TRASH;
            }
        }
    }
    bool moved = t != fa_drop_kind || item != fa_drop_item || place != fa_drop_place;
    if (moved) {
        fa_drop_kind = t;
        fa_drop_item = item;
        fa_drop_place = place;
        if (path && !fa_drop_fs())
            fa_drop_kind = FILES_DT_NONE;   /* not a real folder: no target */
    }
    struct files_drop_in in = { fa_drop_kind, fa_m->path, fa_len(fa_m->path), fa_m->drop_path,
                                fa_len(fa_m->drop_path), fa_drag_n, fa_drag_name, 0, fa_drag_ctrl, fa_drop_same,
                                fa_writable, fa_drop_writable, v->trash };
    enum files_drop_why why = FILES_DW_OK;
    enum files_drop d = files_ops_drop(&in, &why);
    if (!moved && d == fa_drop && why == fa_drop_why)
        return false;
    fa_drop = d;
    fa_drop_why = why;
    if (fa_env.logging) {
        const char *what = fa_drop_kind == FILES_DT_FOLDER ? fa_m->drop_path : fa_drop_kind == FILES_DT_TRASH ? "Trash"
                           : fa_drop_kind == FILES_DT_FILE ? files_entry_name(&fa_m->folder, (uint32_t)item) : "-";
        kserialf("files: drag over %s %s%s%s%s\n", what, files_ops_drop_word(d), why != FILES_DW_OK ? " (" : "",
                 why != FILES_DW_OK ? files_ops_drop_why_word(why) : "", why != FILES_DW_OK ? ")" : "");
    }
    return true;
}

/* The badge (the item's icon and name, or "<n> items") below and right of
 * the pointer; its rectangle, empty without a drag. */
static struct rect fa_badge_rect(void)
{
    if (!fa_m || !fa_drag || fa_px < 0)
        return (struct rect){ 0, 0, 0, 0 };
    return ui_files_badge_rect(fa_px, fa_py, fa_env.scale, fa_env.fonts->regular, fa_badge_text);
}

/* The badge follows the pointer: the old place and the new one redrawn. */
static void fa_badge_move(void)
{
    struct rect n = fa_badge_rect();
    if (n.x == fa_badge_r.x && n.y == fa_badge_r.y && n.w == fa_badge_r.w && n.h == fa_badge_r.h)
        return;
    if (!ui_rect_empty(fa_badge_r))
        fa_env.damage(fa_badge_r);
    fa_badge_r = n;
    if (!ui_rect_empty(n))
        fa_env.damage(n);
}

void files_app_badge_paint(const struct ui_canvas *c)
{
    if (!fa_m || !fa_drag || ui_rect_empty(fa_badge_r))
        return;
    ui_files_badge_draw(c, fa_badge_r, fa_env.scale, fa_env.fonts->regular, fa_badge_icon, fa_badge_text);
}

static void fa_drag_stop(void)
{
    fa_drag = false;
    if (!ui_rect_empty(fa_badge_r))
        fa_env.damage(fa_badge_r);
    fa_badge_r = (struct rect){ 0, 0, 0, 0 };
    fa_drop = FILES_DROP_NO;
    fa_drop_why = FILES_DW_NOWHERE;
    fa_drop_kind = FILES_DT_NONE;
    fa_drop_item = fa_drop_place = -1;
}

/* A press on an item moved past the threshold: the selection is dragged
 * (at most FILES_DRAG_MAX items). */
static void fa_drag_start(bool on, int32_t x, int32_t y)
{
    uint32_t n = fa_nsel();
    if (n == 0)
        return;
    if (n > FILES_DRAG_MAX) {
        fa_status("Only 64 items can be dragged at once.", true);
        klogf("Files: drag refused: %u items selected (at most %u)\n", n, FILES_DRAG_MAX);
        if (fa_env.logging)
            kserialf("files: drag refused %u\n", n);
        return;
    }
    fa_drag = true;
    fa_drag_n = n;
    fa_drag_ctrl = (fa_mods & KEY_CTRL) != 0;
    fa_drop = FILES_DROP_NO;
    fa_drop_why = FILES_DW_NOWHERE;
    fa_drop_kind = FILES_DT_NONE;
    fa_drop_item = fa_drop_place = -2;  /* the first target always computed */
    int32_t first = -1;
    for (uint32_t i = 0; i < fa_m->folder.n && i < fa_m->list.n && first < 0; i++)
        if (fa_m->items[i].selected)
            first = (int32_t)i;
    fa_badge_icon = fa_m->items[first].icon;
    if (n == 1)
        fa_copy(fa_badge_text, sizeof(fa_badge_text), files_entry_name(&fa_m->folder, (uint32_t)first));
    else
        ksnprintf(fa_badge_text, sizeof(fa_badge_text), "%u items", n);
    klogf("Files: dragging %u item%s from %s\n", n, n == 1 ? "" : "s", fa_m->path);
    if (fa_env.logging)
        kserialf("files: drag start %u %s\n", n, fa_badge_text);
    fa_drag_target(on, x, y);
    fa_badge_move();
}

/* The release: the drop (the Paste job over the selection, or the Delete
 * key's Move to Trash), or nothing (logged with why). */
static void fa_drop_go(void)
{
    enum files_drop d = fa_drop;
    enum files_drop_why why = fa_drop_why;
    uint32_t n = fa_drag_n;
    fa_drag_stop();
    if (d == FILES_DROP_NO) {
        klogf("Files: drop refused (%s): nothing changed\n", files_ops_drop_why_word(why));
        if (fa_env.logging)
            kserialf("files: drop no (%s)\n", files_ops_drop_why_word(why));
        return;
    }
    if (d == FILES_DROP_TRASH) {
        klogf("Files: %u item%s dropped on the Trash\n", n, n == 1 ? "" : "s");
        if (fa_env.logging)
            kserialf("files: drop trash %u\n", n);
        fa_count_status();
        fa_delete(false, false);
        return;
    }
    fa_copy(fa_m->jdir, sizeof(fa_m->jdir), fa_m->drop_path);
    if (!fa_jdir_ok())
        return;
    klogf("Files: %u item%s dropped into %s (%s)\n", n, n == 1 ? "" : "s", fa_m->jdir, files_ops_drop_word(d));
    if (fa_env.logging)
        kserialf("files: drop %s %u %s\n", files_ops_drop_word(d), n, fa_m->jdir);
    fa_jmove = d == FILES_DROP_MOVE;
    fa_keep_status = false;
    fa_job_begin(FA_JOB_PASTE, false);
    fa_jdrag = true;
}

/* Box selection: the band applied to the list (never the limit's note). */
static void fa_band_apply(void)
{
    struct ui_list *l = &fa_m->list;
    ui_list_band(l, fa_band_ax, fa_band_ay, fa_band_bx, fa_band_by, fa_m->band_base, fa_band_add);
    if (fa_m->folder.n < l->n)
        l->items[fa_m->folder.n].selected = false;
}

static void fa_band_end(bool cancel)
{
    struct ui_list *l = &fa_m->list;
    fa_band = false;
    if (cancel)
        for (uint32_t i = 0; i < l->n && i <= FILES_MAX_ENTRIES; i++)
            l->items[i].selected = fa_m->band_base[i] != 0;
    uint32_t n = fa_nsel();
    for (uint32_t i = 0; i < fa_m->folder.n && i < l->n; i++)
        if (l->items[i].selected) {
            l->cursor = l->anchor = i;  /* the focus on the first item selected */
            break;
        }
    klogf("Files: box selection %s: %u items selected\n", cancel ? "cancelled" : "done", n);
    if (fa_env.logging)
        kserialf("files: band %s %u\n", cancel ? "cancelled" : "done", n);
}

/* A left press in the content that may become a drag (on an item) or a
 * band (on empty space); the band keeps the selection it started from. */
static void fa_press_start(enum fa_press kind, int32_t i, int32_t x, int32_t y, uint8_t mods, bool defer)
{
    struct ui_list *l = &fa_m->list;
    fa_press = kind;
    fa_press_i = i;
    fa_press_x = x;
    fa_press_y = y;
    fa_press_mods = mods;
    fa_press_defer = defer;
    fa_press_cy = ui_list_content_y(l, y);
    for (uint32_t k = 0; k < l->n && k <= FILES_MAX_ENTRIES; k++)
        fa_m->band_base[k] = l->items[k].selected ? 1u : 0u;
}

/* The pointer moved with the button held since the press: past d(4) the
 * press becomes a drag or a band; true if it did. */
static bool fa_press_move(bool on, int32_t x, int32_t y)
{
    int32_t th = ui_px(4, fa_env.scale), dx = x - fa_press_x, dy = y - fa_press_y;
    if (dx < th && dx > -th && dy < th && dy > -th)
        return false;
    enum fa_press kind = fa_press;
    fa_press = FA_PRESS_NONE;
    if (kind == FA_PRESS_ITEM) {
        fa_drag_start(on, x, y);
        return true;
    }
    fa_band = true;
    fa_band_add = (fa_press_mods & KEY_CTRL) != 0;
    fa_band_ax = fa_press_x;
    fa_band_ay = fa_press_cy;
    fa_band_bx = x;
    fa_band_by = ui_list_content_y(&fa_m->list, y);
    fa_band_apply();
    return true;
}

/* Esc, the window closed: a press, a band or a drag ends, nothing done
 * (the band's selection put back); true if one was running. */
static bool fa_gesture_cancel(const char *why)
{
    bool any = fa_drag || fa_band || fa_press != FA_PRESS_NONE;
    if (fa_drag) {
        fa_drag_stop();
        klogf("Files: drag cancelled (%s): nothing changed\n", why);
        if (fa_env.logging)
            kserialf("files: drag cancelled (%s)\n", why);
    }
    if (fa_band)
        fa_band_end(true);
    fa_press = FA_PRESS_NONE;
    return any;
}

/* The band or a drag at the list's top or bottom edge: a line every
 * FA_AUTO_TICKS (a drag only with the pointer across the list's body). */
static void fa_auto_scroll(uint64_t now)
{
    if (!fa_m || !(fa_band || fa_drag) || now - fa_auto_at < FA_AUTO_TICKS || fa_px < 0)
        return;
    struct ui_list *l = &fa_m->list;
    struct rect b = ui_list_body(l);
    if (fa_drag && (!fa_drag_on || fa_px < b.x || fa_px >= (int64_t)b.x + b.w))
        return;
    int32_t dir = fa_py < b.y ? -1 : fa_py >= (int64_t)b.y + b.h ? 1 : 0;
    uint32_t t0 = l->top;
    if (!dir)
        return;
    ui_list_scroll(l, dir);
    if (l->top == t0)
        return;
    fa_auto_at = now;
    if (fa_band) {
        fa_band_by = ui_list_content_y(l, fa_py);
        fa_band_apply();
    }
    if (fa_drag)
        fa_drag_target(fa_drag_on, fa_px, fa_py);
    fa_changed();
}

/* This PC: a press on card k (-1: none): selected; a double click opens it. */
static void fa_pc_press(int32_t k, int32_t x, int32_t y, uint64_t now)
{
    fa_m->view.card_sel = k;
    if (ui_list_double(&fa_m->clicks, now * 10u, x, y, k, fa_env.scale) && k >= 0)
        fa_pc_open(k);
}

static void fa_files_pointer(bool on, int32_t x, int32_t y, bool down, bool up, uint8_t mods, uint64_t now)
{
    struct ui_files_view *v = &fa_m->view;
    struct ui_files_layout lay;
    uint32_t s = fa_env.scale;
    const struct font *f = fa_env.fonts->regular;
    ui_files_layout(fa_env.files->r, s, f, v->bar != UI_FILES_BAR_NONE, &lay);
    fa_m->list.r = lay.content;
    int32_t index = -1;
    enum ui_files_part part = on ? ui_files_hit(&lay, v, s, f, x, y, &index) : UF_NONE;
    bool changed = false;
    fa_mods = mods;                     /* 1.5 stage 14b: Ctrl copies a drop */
    /* The scroll bar's drag goes on wherever the pointer is. */
    if (fa_sb_drag) {
        uint32_t t = ui_scrollbar_drag(ui_list_sb_rect(&fa_m->list), ui_list_lines(&fa_m->list),
                                       ui_list_page(&fa_m->list), s, y);
        if (t != fa_m->list.top) {
            fa_m->list.top = t;
            changed = true;
        }
        if (up) {
            fa_sb_drag = false;
            fa_m->list.sb = UI_SB_NORMAL;
            changed = true;
        }
        if (changed)
            fa_changed();
        return;
    }
    /* 1.5 stage 14b: no button hover while dragging or drawing a band. */
    enum ui_files_part hover = part > UF_NONE && part < UF_BUTTONS && !fa_drag && !fa_band ? part : UF_NONE;
    if (hover != v->hover) {
        v->hover = hover;
        changed = true;
    }
    if (v->thispc) {                    /* 1.5 stage 14b: the card under the pointer */
        int32_t ch = part == UF_CONTENT ? index : -1;
        if (ch != v->card_hover) {
            v->card_hover = ch;
            changed = true;
        }
    }
    if (x != fa_px || y != fa_py) {         /* the tooltip waits for the pointer to rest */
        fa_px = x;
        fa_py = y;
        fa_tip_part = ui_files_square(hover) ? hover : UF_NONE;    /* 1.5 stage 13: every square */
        fa_tip_since = now;
        if (v->tip != UF_NONE) {
            v->tip = UF_NONE;
            changed = true;
        }
    }
    if (down) {
        if (v->tip != UF_NONE) {
            v->tip = UF_NONE;
            changed = true;
        }
        fa_tip_part = UF_NONE;
        if (v->renaming && part != UF_CONTENT) {
            fa_rename_submit();
            changed = true;
        }
        if (part > UF_NONE && part < UF_BUTTONS) {
            if (v->enabled[part]) {
                v->pressed = part;
                changed = true;
            }
        } else if (part == UF_PATH && !fa_reading && !v->renaming && v->mode == UI_FILES_LIST && fa_job == FA_JOB_NONE) {
            fa_press_path = index;
        } else if (part == UF_PLACE && index >= 0 && index < UI_FILES_PLACES && !fa_reading && !v->renaming &&
                   v->mode != UI_FILES_ASK && fa_job == FA_JOB_NONE) {
            fa_count_status();
            if (index == 8) {           /* 1.5 stage 14b: This PC */
                if (!v->thispc || v->mode != UI_FILES_LIST)
                    fa_go(FILES_VIEW_THISPC, "/");
            } else if (index == 7) {    /* 1.5 stage 13: the Trash view */
                if (!v->trash || v->mode != UI_FILES_LIST)
                    fa_go(FILES_VIEW_TRASH, FA_TFILES);
            } else if (!fa_same(fa_m->path, fa_place_paths[index]) || v->mode != UI_FILES_LIST || v->trash ||
                       v->thispc) {
                fa_go(FILES_VIEW_FOLDER, fa_place_paths[index]);
            }
            changed = true;
        } else if (part == UF_CONTENT && v->mode == UI_FILES_LIST && !fa_reading && fa_job == FA_JOB_NONE) {
            struct ui_list *l = &fa_m->list;
            enum ui_sb_hit sh = ui_scrollbar_hit(ui_list_sb_rect(l), ui_list_lines(l), ui_list_page(l), l->top, s, x, y);
            if (sh == UI_SB_HIT_THUMB) {
                fa_sb_drag = true;
                l->sb = UI_SB_DRAG;
            } else if (sh == UI_SB_HIT_ABOVE || sh == UI_SB_HIT_BELOW) {
                ui_list_scroll(l, sh == UI_SB_HIT_ABOVE ? -(int32_t)ui_list_page(l) : (int32_t)ui_list_page(l));
            } else if (!v->renaming || !ui_rect_contains(ui_files_field_rect(l, l->cursor), x, y)) {
                if (v->renaming) {
                    fa_rename_submit();
                } else if (v->thispc) {         /* 1.5 stage 14b: This PC's cards */
                    fa_pc_press(index, x, y, now);
                } else {
                    int32_t i = ui_list_item_at(l, x, y);
                    if (i >= (int32_t)fa_m->folder.n)
                        i = -1;
                    /* 1.5 stage 14b: a press on an item already selected
                     * (no Shift) leaves the selection until the release,
                     * so the selection can be dragged; the click is made
                     * then if no drag started. */
                    bool defer = i >= 0 && l->items[i].selected && !(mods & KEY_SHIFT);
                    if (!defer)
                        ui_list_click(l, i, mods);
                    if (ui_list_double(&fa_m->clicks, now * 10u, x, y, i, s) && i >= 0 && !v->trash) {
                        fa_count_status();
                        fa_open_item((uint32_t)i);
                    } else {
                        fa_press_start(i >= 0 ? FA_PRESS_ITEM : FA_PRESS_SPACE, i, x, y, mods, defer);
                    }
                }
            } else {
                ui_textfield_click(&fa_m->field, ui_files_field_rect(l, l->cursor), s, f, x);
            }
            changed = true;
        }
    }
    /* 1.5 stage 14b: with the button held, a press becomes a drag or a
     * band past d(4); a drag follows the pointer (its target, the badge);
     * a band follows it (the selection). */
    if (!down) {
        if (fa_press != FA_PRESS_NONE) {
            if (fa_press_move(on, x, y))
                changed = true;
        } else if (fa_drag) {
            if (fa_drag_target(on, x, y))
                changed = true;
            fa_badge_move();
        } else if (fa_band) {
            int64_t by = ui_list_content_y(&fa_m->list, y);
            if (x != fa_band_bx || by != fa_band_by) {
                fa_band_bx = x;
                fa_band_by = by;
                fa_band_apply();
                changed = true;
            }
        }
    }
    if (up) {
        if (fa_drag) {                  /* 1.5 stage 14b: the drop */
            fa_drop_go();
            changed = true;
        } else if (fa_band) {
            fa_band_end(false);
            changed = true;
        } else if (fa_press == FA_PRESS_ITEM && fa_press_defer && fa_press_i >= 0 &&
                   (uint32_t)fa_press_i < fa_m->list.n) {
            ui_list_click(&fa_m->list, fa_press_i, fa_press_mods);     /* the click left at the press */
            changed = true;
        }
        fa_press = FA_PRESS_NONE;
        if (v->pressed != UF_NONE) {
            enum ui_files_part p = v->pressed;
            v->pressed = UF_NONE;
            if (part == p)
                fa_button(p);
            changed = true;
        }
        if (fa_press_path >= 0) {
            int32_t k = fa_press_path;
            fa_press_path = -1;
            if (part == UF_PATH && index == k && k < (int32_t)fa_m->pb.n && k < (int32_t)FILES_PB_MAX &&
                fa_m->pb.part[k].target > 0) {
                uint32_t t = fa_m->pb.part[k].target;
                enum files_view hv;
                const char *p;
                uint32_t pl;
                if (files_history_current(&fa_m->hist, &hv, &p, &pl) && t <= pl && t <= FILES_PATH_MAX) {
                    for (uint32_t i = 0; i < t; i++)
                        fa_m->scratch[i] = p[i];
                    fa_m->scratch[t] = '\0';
                    fa_count_status();
                    if (!fa_same(fa_m->scratch, fa_m->path))
                        fa_go(FILES_VIEW_FOLDER, fa_m->scratch);
                    changed = true;
                }
            }
        }
    }
    if (changed)
        fa_changed();
}

static void fa_viewer_pointer(bool on, int32_t x, int32_t y, bool down, bool up)
{
    struct ui_viewer *vv = &fa_v->v;
    struct rect r = fa_env.viewer->r;
    uint32_t s = fa_env.scale;
    const struct font *f = fa_env.fonts->regular;
    uint32_t lines = ui_viewer_lines(vv->buf, vv->len, ui_viewer_cols(vv, r, s, f)), page = ui_viewer_page(vv, r, s, f);
    struct rect sb = ui_viewer_sb(vv, r, s);
    if (fa_vsb_drag) {
        uint32_t t = ui_scrollbar_drag(sb, lines, page, s, y);
        bool changed = t != vv->top;
        vv->top = t;
        if (up) {
            fa_vsb_drag = false;
            vv->sb = UI_SB_NORMAL;
            changed = true;
        }
        if (changed)
            fa_vchanged();
        return;
    }
    if (!down || !on)
        return;
    enum ui_sb_hit h = ui_scrollbar_hit(sb, lines, page, vv->top, s, x, y);
    if (h == UI_SB_HIT_THUMB) {
        fa_vsb_drag = true;
        vv->sb = UI_SB_DRAG;
    } else if (h == UI_SB_HIT_ABOVE || h == UI_SB_HIT_BELOW) {
        ui_viewer_scroll(vv, r, s, f, h == UI_SB_HIT_ABOVE ? -(int32_t)page : (int32_t)page);
    } else {
        return;
    }
    fa_vchanged();
}

int files_app_pointer_kind(struct ui_window *top, int32_t x, int32_t y, uint64_t now, bool *dragging)
{
    *dragging = false;
    if (fa_m && fa_drag) {
        *dragging = true;
        return fa_drop == FILES_DROP_COPY ? POINTER_DRAG_COPY : fa_drop == FILES_DROP_NO ? POINTER_NO_DROP
                                                                                          : POINTER_DRAG_MOVE;
    }
    if (top && top == fa_env.viewer && fa_v && fa_vreading && now - fa_vread_t0 >= FA_BUSY_TICKS)
        return POINTER_BUSY;
    if (!top || top != fa_env.files || !fa_m || fa_env.files->minimised)
        return -1;
    if ((fa_job != FA_JOB_NONE && fa_step != FA_ST_ASKING) || (fa_reading && now - fa_read_t0 >= FA_BUSY_TICKS))
        return POINTER_BUSY;
    struct ui_files_view *v = &fa_m->view;
    if (v->mode != UI_FILES_LIST || fa_reading)
        return -1;
    struct ui_files_layout lay;
    int32_t index = -1;
    uint32_t s = fa_env.scale;
    const struct font *f = fa_env.fonts->regular;
    ui_files_layout(fa_env.files->r, s, f, v->bar != UI_FILES_BAR_NONE, &lay);
    fa_m->list.r = lay.content;
    enum ui_files_part part = ui_files_hit(&lay, v, s, f, x, y, &index);
    struct ui_list *l = &fa_m->list;
    if (v->renaming && part == UF_CONTENT && l->cursor < l->n &&
        ui_rect_contains(ui_rect_intersect(ui_files_field_rect(l, l->cursor), ui_list_body(l)), x, y))
        return POINTER_IBEAM;
    if (part == UF_PATH && index >= 0 && index < (int32_t)fa_m->pb.n && index < (int32_t)FILES_PB_MAX &&
        fa_m->pb.part[index].target > 0 && !v->renaming && fa_job == FA_JOB_NONE)
        return POINTER_HAND;
    return -1;
}

void files_app_mods(uint8_t mods)
{
    fa_mods = mods;
    if (!fa_m || !fa_drag || fa_drag_ctrl == ((mods & KEY_CTRL) != 0))
        return;
    fa_drag_ctrl = (mods & KEY_CTRL) != 0;  /* Ctrl pressed or released during a drag */
    if (fa_drag_target(fa_drag_on, fa_px, fa_py))
        fa_changed();
}

void files_app_pointer(struct ui_window *top, int32_t x, int32_t y, bool down, bool up, uint8_t mods,
                       uint64_t now)
{
    fa_now = now;                       /* 1.5 stage 14b */
    if (fa_m && fa_env.files->open && !fa_env.files->minimised)
        fa_files_pointer(top == fa_env.files, x, y, down && top == fa_env.files, up, mods, now);
    if (fa_v && fa_env.viewer->open && !fa_env.viewer->minimised)
        fa_viewer_pointer(top == fa_env.viewer, x, y, down && top == fa_env.viewer, up);
}

static bool fa_files_key(const struct key_event *e)
{
    struct ui_files_view *v = &fa_m->view;
    bool pressed = (e->flags & KEY_PRESSED) != 0, alt = (e->flags & KEY_ALT) && !(e->flags & KEY_CTRL);
    /* 1.5 stage 14b: Esc ends a drag or a band with nothing done; other
     * keys wait until it ends. */
    if (fa_drag || fa_band || fa_press != FA_PRESS_NONE) {
        if (pressed && e->keycode == KEY_ESC) {
            fa_gesture_cancel("Esc");
            fa_changed();
        }
        return true;
    }
    /* Esc (the owner's condition, S12; ui_files_esc): the rename field
     * cancels the rename, else the info panel closes, else the window
     * closes (false: desktop.c closes it). */
    if (pressed && e->keycode == KEY_ESC) {
        switch (ui_files_esc(v)) {
        case UF_ESC_CANCEL_RENAME:
            v->renaming = false;
            fa_count_status();
            fa_changed();
            return true;
        case UF_ESC_CLOSE_PANEL:
            v->mode = UI_FILES_LIST;
            fa_changed();
            return true;
        case UF_ESC_CANCEL_ASK:         /* 1.5 stage 13: the question's Cancel */
            fa_ask_answer(false);
            fa_changed();
            return true;
        default:
            return false;
        }
    }
    if (v->mode == UI_FILES_ASK) {      /* 1.5 stage 13: Tab moves the focus, Enter presses it */
        if (pressed && e->keycode == KEY_TAB) {
            v->ask_focus ^= 1;
            v->ask_ring = true;
            fa_changed();
        } else if (pressed && (e->keycode == KEY_ENTER || e->keycode == KEY_KP_ENTER)) {
            fa_ask_answer(v->ask_focus == 1);
            fa_changed();
        }
        return true;
    }
    if (v->renaming) {
        enum ui_tf_result r = ui_textfield_key(&fa_m->field, e);
        if (r == UI_TF_SUBMIT)
            fa_rename_submit();
        else if (r == UI_TF_CANCEL)
            v->renaming = false;
        if (r != UI_TF_NONE && r != UI_TF_PASS) {
            if (r != UI_TF_SUBMIT && r != UI_TF_REFUSED && !v->status_error)
                fa_count_status();
            struct rect fr = ui_files_field_rect(&fa_m->list, fa_m->list.cursor);
            ui_textfield_fit(&fa_m->field, ui_textfield_cols(fr, fa_env.scale, fa_env.fonts->regular));
            fa_changed();
        }
        return true;
    }
    if (!pressed)
        return true;
    if (v->mode == UI_FILES_PANEL) {
        if (e->keycode == KEY_ESC) {
            v->mode = UI_FILES_LIST;
            fa_changed();
            return true;
        }
        if (!(alt && (e->keycode == KEY_LEFT || e->keycode == KEY_RIGHT || e->keycode == KEY_UP)) &&
            e->keycode != KEY_BACKSPACE)
            return true;
        v->mode = UI_FILES_LIST;
    }
    if (fa_reading || fa_job != FA_JOB_NONE)    /* 1.5 stage 13: and while an action runs */
        return e->keycode != KEY_ESC;
    if (v->thispc && !alt && e->keycode != KEY_BACKSPACE && e->keycode != KEY_ESC) {
        /* 1.5 stage 14b: This PC: Up, Down, Home and End choose a card,
         * Enter opens it; nothing else. */
        int32_t n = (int32_t)fa_ncards, k = v->card_sel;
        if (e->flags & (KEY_CTRL | KEY_ALT) || n == 0)
            return true;
        if (e->keycode == KEY_DOWN)
            k = k < 0 ? 0 : k + 1 < n ? k + 1 : k;
        else if (e->keycode == KEY_UP)
            k = k <= 0 ? 0 : k - 1;
        else if (e->keycode == KEY_HOME)
            k = 0;
        else if (e->keycode == KEY_END)
            k = n - 1;
        else if (e->keycode == KEY_ENTER || e->keycode == KEY_KP_ENTER) {
            fa_pc_open(k);
            fa_changed();
            return true;
        } else {
            return true;
        }
        v->card_sel = k;
        fa_changed();
        return true;
    }
    if (alt && (e->keycode == KEY_LEFT || e->keycode == KEY_RIGHT || e->keycode == KEY_UP)) {
        enum ui_files_part p = e->keycode == KEY_LEFT ? UF_BACK : e->keycode == KEY_RIGHT ? UF_FORWARD : UF_UP;
        fa_update_view();
        if (v->enabled[p])
            fa_button(p);
        fa_changed();
        return true;
    }
    if (e->keycode == KEY_BACKSPACE && !(e->flags & (KEY_CTRL | KEY_ALT))) {
        fa_update_view();
        if (v->enabled[UF_UP])
            fa_button(UF_UP);
        fa_changed();
        return true;
    }
    if (e->keycode == KEY_F2 && !(e->flags & (KEY_CTRL | KEY_ALT))) {
        fa_update_view();
        if (v->enabled[UF_RENAME])
            fa_button(UF_RENAME);
        fa_changed();
        return true;
    }
    /* 1.5 stage 13: Ctrl+X, Ctrl+C, Ctrl+V, Ctrl+A (item (6)); Delete and
     * Shift+Delete; Alt+Enter (Properties); the Menu key and Shift+F10. */
    bool ctrl = (e->flags & KEY_CTRL) && !(e->flags & KEY_ALT), shift = (e->flags & KEY_SHIFT) != 0;
    if (ctrl && e->keycode == KEY_CHAR) {
        char c = e->ascii;
        if (c >= 1 && c <= 26)
            c = (char)(c + 'a' - 1);    /* a control code, as conskeys.c reads it */
        if (c >= 'A' && c <= 'Z')
            c = (char)(c + 'a' - 'A');
        fa_update_view();
        if (c == 'x' && v->enabled[UF_CUT])
            fa_clip_set(true);
        else if (c == 'c' && v->enabled[UF_COPY])
            fa_clip_set(false);
        else if (c == 'v' && v->enabled[UF_PASTE])
            fa_paste(-1);
        else if (c == 'a')
            fa_select_all();
        fa_changed();
        return true;
    }
    if (e->keycode == KEY_DELETE && !(e->flags & (KEY_CTRL | KEY_ALT))) {
        fa_update_view();
        if (v->trash ? v->enabled[UF_TDELETE] : (v->enabled[UF_TRASH] || (shift && fa_nsel() && fa_writable)))
            fa_delete(shift, true);
        fa_changed();
        return true;
    }
    if (alt && (e->keycode == KEY_ENTER || e->keycode == KEY_KP_ENTER)) {
        if (fa_target() >= 0)
            fa_properties((uint32_t)fa_target());
        fa_changed();
        return true;
    }
    if ((e->keycode == KEY_MENU && !(e->flags & (KEY_CTRL | KEY_ALT))) ||
        (e->keycode == KEY_F10 && shift && !(e->flags & (KEY_CTRL | KEY_ALT)))) {
        struct ui_list *l = &fa_m->list;
        bool item = fa_target() >= 0;
        int32_t mx, my;
        if (item) {                     /* at the focused item: its name's left, the row's bottom */
            struct rect ir = ui_list_item_rect(l, l->cursor);
            mx = l->view == UI_LIST_GRID ? ir.x + ir.w / 2 : ir.x + ui_px(36, fa_env.scale);
            my = ir.y + ir.h;
            fa_menu_item = fa_m->folder.e[l->cursor].kind == FILES_K_DIR && fa_nsel() == 1 ? (int32_t)l->cursor : -1;
        } else {                        /* the content's top left */
            mx = l->r.x + ui_px(8, fa_env.scale);
            my = l->r.y + ui_px(8, fa_env.scale);
            fa_menu_item = -1;
        }
        fa_open_menu(item, mx, my, true);
        return true;
    }
    if (e->keycode == KEY_ESC)
        return false;
    enum ui_list_result r = ui_list_key(&fa_m->list, e);
    if (r == UI_LIST_OPEN) {
        if (fa_m->list.cursor < fa_m->folder.n && !v->trash) {     /* 1.5 stage 13: nothing opens in the Trash */
            fa_count_status();
            fa_open_item(fa_m->list.cursor);
        }
        fa_changed();
    } else if (r == UI_LIST_MOVED) {
        if (fa_m->list.cursor >= fa_m->folder.n && fa_m->folder.n > 0) {
            fa_m->items[fa_m->list.cursor].selected = false;        /* never the limit's note */
            fa_m->list.cursor = fa_m->folder.n - 1u;
            fa_m->items[fa_m->list.cursor].selected = true;
        }
        fa_changed();
    }
    return true;
}

static bool fa_viewer_key(const struct key_event *e)
{
    if (!(e->flags & KEY_PRESSED))
        return true;
    struct ui_viewer *vv = &fa_v->v;
    struct rect r = fa_env.viewer->r;
    uint32_t s = fa_env.scale;
    const struct font *f = fa_env.fonts->regular;
    int32_t page = (int32_t)ui_viewer_page(vv, r, s, f), step = 0;
    switch (e->keycode) {
    case KEY_UP:
        step = -1;
        break;
    case KEY_DOWN:
        step = 1;
        break;
    case KEY_PGUP:
        step = -page;
        break;
    case KEY_PGDN:
        step = page;
        break;
    case KEY_HOME:
        step = -(int32_t)UI_VIEWER_MAX;
        break;
    case KEY_END:
        step = (int32_t)UI_VIEWER_MAX;
        break;
    case KEY_ESC:
        return false;
    default:
        return true;
    }
    uint32_t before = vv->top;
    ui_viewer_scroll(vv, r, s, f, step);
    if (vv->top != before)
        fa_vchanged();
    return true;
}

bool files_app_key(struct ui_window *w, const struct key_event *e)
{
    if (w == fa_env.files)
        return fa_m ? fa_files_key(e) : e->keycode != KEY_ESC;
    if (w == fa_env.viewer)
        return fa_v ? fa_viewer_key(e) : e->keycode != KEY_ESC;
    return true;
}

void files_app_wheel(struct ui_window *w, int32_t x, int32_t y, int32_t notches)
{
    (void)x;
    (void)y;
    if (w == fa_env.files && fa_m && fa_m->view.mode == UI_FILES_LIST && !fa_reading) {
        uint32_t before = fa_m->list.top;
        ui_list_wheel(&fa_m->list, notches);
        if (fa_m->list.top != before)
            fa_changed();
    } else if (w == fa_env.viewer && fa_v) {
        uint32_t before = fa_v->v.top;
        ui_viewer_scroll(&fa_v->v, fa_env.viewer->r, fa_env.scale, fa_env.fonts->regular, 3 * notches);
        if (fa_v->v.top != before)
            fa_vchanged();
    }
}

void files_app_paint(const struct ui_canvas *c, struct rect r, void *ctx)
{
    (void)ctx;
    if (fa_nomem_view || !fa_m) {
        ui_files_draw(c, r, fa_env.scale, fa_env.fonts, &fa_nomem);
        return;
    }
    struct ui_files_layout lay;
    ui_files_layout(r, fa_env.scale, fa_env.fonts->regular, fa_m->view.bar != UI_FILES_BAR_NONE, &lay);
    fa_m->list.r = lay.content;
    if (!fa_reading && fa_m->list.top > ui_list_max_top(&fa_m->list))
        fa_m->list.top = ui_list_max_top(&fa_m->list);
    /* The path bar for the window's present width (moved, resized,
     * maximised since the last change). */
    enum files_view hv = FILES_VIEW_FOLDER;
    const char *p = 0;
    uint32_t pl = 0;
    if (files_history_current(&fa_m->hist, &hv, &p, &pl))
        files_pathbar(hv, p, pl, ui_files_path_cells(&lay, fa_env.scale, fa_env.fonts->regular), &fa_m->pb);
    ui_files_draw(c, r, fa_env.scale, fa_env.fonts, &fa_m->view);
}

void files_app_viewer_paint(const struct ui_canvas *c, struct rect r, void *ctx)
{
    (void)ctx;
    if (!fa_v)
        return;
    ui_viewer_draw(c, &fa_v->v, r, fa_env.scale, fa_env.fonts);
}
