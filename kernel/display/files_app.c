/*
 * The Files app and the Text Viewer, the kernel side (1.5 stage 12); see
 * files_app.h.
 */
#include "kernel/display/files_app.h"
#include "kernel/display/ui_files.h"
#include "kernel/display/ui_icon.h"
#include "kernel/display/ui_list.h"
#include "kernel/display/ui_textfield.h"
#include "kernel/display/ui_viewer.h"
#include "kernel/files/files_core.h"
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

typedef unsigned long long ull;

#define FA_STATUS_MAX 320u
#define FA_HOLD_ENTRIES 16u
#define FA_CHUNK 4096u

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
        why = "the disk is full";
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
    bool idle = !fa_reading && !v->renaming && v->mode == UI_FILES_LIST;
    v->enabled[UF_BACK] = idle && files_history_can_back(&fa_m->hist);
    v->enabled[UF_FORWARD] = idle && files_history_can_forward(&fa_m->hist);
    v->enabled[UF_UP] = idle && files_history_can_up(&fa_m->hist);
    v->enabled[UF_LIST] = v->enabled[UF_GRID] = v->enabled[UF_HIDDEN] = idle;
    v->enabled[UF_NEWFOLDER] = v->enabled[UF_NEWFILE] = idle && fa_writable;
    v->enabled[UF_OPEN] = idle && fa_target() >= 0;
    v->enabled[UF_RENAME] = idle && fa_writable && fa_target() >= 0;
    v->enabled[UF_CLOSE] = v->mode == UI_FILES_PANEL;
    v->chosen[UF_LIST] = fa_m->list.view == UI_LIST_DETAILS;
    v->chosen[UF_GRID] = fa_m->list.view == UI_LIST_GRID;
    v->chosen[UF_HIDDEN] = fa_m->folder.show_hidden;
    v->place = -1;
    for (int32_t k = 0; k < 7; k++)
        if (fa_same(fa_m->path, fa_place_paths[k]))
            v->place = k;
    struct ui_files_layout lay;
    ui_files_layout(fa_env.files->r, fa_env.scale, fa_env.fonts->regular, v->bar != UI_FILES_BAR_NONE, &lay);
    files_pathbar(hv, p, pl, ui_files_path_cells(&lay, fa_env.scale, fa_env.fonts->regular), &fa_m->pb);
    v->pb = &fa_m->pb;
    v->list = &fa_m->list;
    v->field = &fa_m->field;
    v->status = fa_m->status;
    /* The list's rectangle follows the window (moved, resized, maximised). */
    fa_m->list.r = lay.content;
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
    if (v->renaming)
        kserialf("files: field %u %u %s\n", (uint32_t)fa_m->field.pos, (uint32_t)fa_m->field.first, fa_m->field.buf);
    kserialf("files: status %u %s\n", v->status_error ? 1u : 0u, fa_m->status);
    if (v->mode == UI_FILES_PANEL && l->cursor < fa_m->folder.n) {
        const struct files_entry *e = &fa_m->folder.e[l->cursor];
        kserialf("files: panel %c %llu %lld %s\n", e->kind == FILES_K_DIR ? 'D' : e->kind == FILES_K_LINK ? 'L' : 'F',
                 (ull)e->size, (long long)e->mtime, files_entry_name(&fa_m->folder, l->cursor));
    }
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
    kserialf("files: state mode=%s view=%s bar=%s place=%d en=%s chosen=%s hover=%s pressed=%s tip=%s top=%u "
             "cursor=%u ring=%u sb=%u rename=%u reading=%u sel=%s\n",
             v->mode == UI_FILES_PANEL ? "panel" : "list", l->view == UI_LIST_GRID ? "grid" : "list",
             v->bar == UI_FILES_BAR_LIVE ? "live" : v->bar == UI_FILES_BAR_RO ? "ro" : "none", v->place, en, ch,
             fa_part_name(v->hover), fa_part_name(v->pressed), fa_part_name(v->tip), l->top, l->cursor,
             l->ring ? 1u : 0u, (uint32_t)l->sb, v->renaming ? 1u : 0u, fa_reading ? 1u : 0u, sel);
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
    fa_cookie = 0;
    fa_seen = fa_holds = 0;
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
    fa_count_status();
    if (fa_m->select_name[0]) {
        for (uint32_t i = 0; i < f->n; i++)
            if (fa_same(files_entry_name(f, i), fa_m->select_name)) {
                fa_m->list.cursor = fa_m->list.anchor = i;
                fa_m->items[i].selected = true;
                ui_list_reveal(&fa_m->list);
                if (fa_rename_after) {
                    ui_textfield_set(&fa_m->field, fa_m->select_name);
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
    }
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
    if (r == 0) {
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

/* ---- actions ---- */

static void fa_go(enum files_view view, const char *path)
{
    if (!files_history_go(&fa_m->hist, view, path, fa_len(path))) {
        fa_status("This path is too long.", true);
        return;
    }
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
        if (fa_target() >= 0)
            fa_open_item((uint32_t)fa_target());
        break;
    case UF_RENAME:
        fa_rename_start();
        break;
    case UF_CLOSE:
        fa_m->view.mode = UI_FILES_LIST;
        break;
    default:
        break;
    }
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

void files_app_closed(struct ui_window *w)
{
    if (w == fa_env.files) {
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

void files_app_poll(uint64_t now)
{
    if (fa_m && fa_reading) {
        fa_read_hold();
        if (!fa_reading)
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
    enum ui_files_part hover = part > UF_NONE && part < UF_BUTTONS ? part : UF_NONE;
    if (hover != v->hover) {
        v->hover = hover;
        changed = true;
    }
    if (x != fa_px || y != fa_py) {         /* the tooltip waits for the pointer to rest */
        fa_px = x;
        fa_py = y;
        fa_tip_part = hover >= UF_BACK && hover <= UF_UP ? hover : UF_NONE;
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
        } else if (part == UF_PATH && !fa_reading && !v->renaming && v->mode == UI_FILES_LIST) {
            fa_press_path = index;
        } else if (part == UF_PLACE && index >= 0 && index < 7 && !fa_reading && !v->renaming) {
            fa_count_status();
            if (!fa_same(fa_m->path, fa_place_paths[index]) || v->mode != UI_FILES_LIST)
                fa_go(FILES_VIEW_FOLDER, fa_place_paths[index]);
            changed = true;
        } else if (part == UF_CONTENT && v->mode == UI_FILES_LIST && !fa_reading) {
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
                } else {
                    int32_t i = ui_list_item_at(l, x, y);
                    if (i >= (int32_t)fa_m->folder.n)
                        i = -1;
                    ui_list_click(l, i, mods);
                    if (ui_list_double(&fa_m->clicks, now * 10u, x, y, i, s) && i >= 0) {
                        fa_count_status();
                        fa_open_item((uint32_t)i);
                    }
                }
            } else {
                ui_textfield_click(&fa_m->field, ui_files_field_rect(l, l->cursor), s, f, x);
            }
            changed = true;
        }
    }
    if (up) {
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

void files_app_pointer(struct ui_window *top, int32_t x, int32_t y, bool down, bool up, uint8_t mods,
                       uint64_t now)
{
    if (fa_m && fa_env.files->open && !fa_env.files->minimised)
        fa_files_pointer(top == fa_env.files, x, y, down && top == fa_env.files, up, mods, now);
    if (fa_v && fa_env.viewer->open && !fa_env.viewer->minimised)
        fa_viewer_pointer(top == fa_env.viewer, x, y, down && top == fa_env.viewer, up);
}

static bool fa_files_key(const struct key_event *e)
{
    struct ui_files_view *v = &fa_m->view;
    bool pressed = (e->flags & KEY_PRESSED) != 0, alt = (e->flags & KEY_ALT) && !(e->flags & KEY_CTRL);
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
        default:
            return false;
        }
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
    if (fa_reading)
        return e->keycode != KEY_ESC;
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
    if (e->keycode == KEY_ESC)
        return false;
    enum ui_list_result r = ui_list_key(&fa_m->list, e);
    if (r == UI_LIST_OPEN) {
        if (fa_m->list.cursor < fa_m->folder.n) {
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
