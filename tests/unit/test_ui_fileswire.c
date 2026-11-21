/* The Files app's wiring (1.5 stage 13, the owner's condition after the
 * first sharded run: files_app_setup did not copy env->menu, so no Files
 * menu ever opened). kernel/display/files_app.c itself is included and set
 * up as kernel/display/desktop.c sets it up: files_app_options, every
 * field of struct files_app_env, files_app_setup; its open hook calls the
 * window manager's ui_wm_open and its menu hook calls ui_menus_open_at,
 * as desk_files_open and desk_files_menu do. Under it the real VFS (vfs_core
 * over ramfs_core) holds /home/user and a Trash with one item; the kernel
 * calls Files makes beside those (the page allocator, the page tables, the
 * VFS lock, the disk, the log) are stubs here, and Files' memory area
 * (VM_FILES_BASE) is a static buffer. Checked: every hook of the
 * environment is the one passed in; a right press on an item opens the
 * Files item menu at the press, on empty space the Files space menu; the
 * Menu key opens the item menu at the focused item, by the keyboard; in the
 * Trash view a right press on its item opens the Trash item menu, on empty
 * space the Trash space menu; each time the menu is really open in the
 * desktop's menus (ui_menus) with the greyed rows Files asked for. The
 * toolkit's modules are included directly; surface_core.c, wallpaper_core.c
 * and coverage_core.c are linked (the Makefile). */
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "tests/unit/unit.h"
#include "kernel/display/tokens.h"
#include "kernel/display/ui_rect.c"
#include "kernel/display/ui_draw.c"
#include "kernel/display/icons.c"
#include "kernel/display/ui_icon.c"
#include "kernel/display/cicons.c"
#include "kernel/display/ui_cicon.c"
#include "kernel/display/ui_about.c"
#include "kernel/display/ui_widget.c"
#include "kernel/display/ui_window.c"
#include "kernel/display/ui_menu.c"
#include "kernel/display/ui_dialog.c"
#include "kernel/display/ui_menus.c"
#include "kernel/display/ui_textfield.c"
#include "kernel/display/ui_list.c"
#include "kernel/display/ui_files.c"
#include "kernel/display/ui_viewer.c"
#include "kernel/files/files_core.c"
#include "kernel/files/trash_core.c"
#include "kernel/files/files_ops.c"
#include "kernel/fs/path_core.c"
#include "kernel/fs/mount_core.c"
#include "kernel/fs/ramfs_core.c"
#include "kernel/fs/vfs_core.c"
#include "kernel/memory/vm_layout.h"

/* Files' memory area: a static buffer in place of VM_FILES_BASE's pages
 * (the stubs below map nothing; Files zeroes and uses the buffer). */
static uint8_t fw_area[VM_FILES_PAGES * 4096u] __attribute__((aligned(4096)));
#undef VM_FILES_BASE
#define VM_FILES_BASE ((uint64_t)(uintptr_t)fw_area)

#include "kernel/display/files_app.c"

/* ---- the kernel calls Files makes, as stubs ---- */

static struct ramfs fw_fs;
static struct vfs fw_vfs;
static struct paging_stats fw_pstats;
static uint32_t fw_frames;
static int fw_holds;

struct vfs *vfs_root(void)
{
    return &fw_vfs;
}

struct ramfs *vfs_ramfs(void)
{
    return &fw_fs;
}

int64_t vfs_now(void)
{
    return 1795236000;
}

int vfs_live_home(uint32_t *made)
{
    *made = 0;
    return 0;
}

uint32_t fd_vfs_acquire(void)
{
    fw_holds++;
    return 0;
}

void fd_vfs_release(void)
{
    fw_holds--;
}

bool ext2_disk_retry(int *r, uint32_t *tries)
{
    (void)r;
    (void)tries;
    return false;
}

bool ext2_disk_mounted(void)
{
    return false;
}

bool ext2_disk_rw(void)
{
    return false;
}

bool ext2_disk_df(struct ext2_disk_dfinfo *out)
{
    (void)out;
    return false;
}

void klogf(const char *fmt, ...)
{
    (void)fmt;
}

void kserialf(const char *fmt, ...)
{
    (void)fmt;
}

size_t ksnprintf(char *buf, size_t size, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, size, fmt, ap);
    va_end(ap);
    if (size == 0 || n < 0)
        return 0;
    return (size_t)n < size ? (size_t)n : size - 1;
}

const struct paging_stats *paging_stats(void)
{
    return &fw_pstats;
}

uint32_t pmm_free_pages(void)
{
    return 4096u;
}

uint32_t memory_alloc_page(void)
{
    return 0x100000u + 0x1000u * ++fw_frames;
}

void memory_free_page(uint32_t addr)
{
    (void)addr;
}

enum vmm_result paging_map(uint64_t va, uint64_t phys, uint64_t flags)
{
    (void)va;
    (void)phys;
    (void)flags;
    return VMM_OK;
}

enum vmm_result paging_unmap(uint64_t va, uint64_t *phys_out)
{
    (void)va;
    if (phys_out)
        *phys_out = 0x101000u;
    return VMM_OK;
}

uint64_t paging_nx_flag(void)
{
    return 0;
}

/* ---- the RAM filesystem's memory and file pages ---- */

#define FW_POOL 64u
static unsigned char fw_pool[FW_POOL][RAMFS_PAGE] __attribute__((aligned(4096)));
static unsigned char fw_used[FW_POOL];

static uint64_t fw_palloc(void)
{
    for (unsigned i = 0; i < FW_POOL; i++) {
        if (!fw_used[i]) {
            fw_used[i] = 1;
            return (uint64_t)(i + 1) * RAMFS_PAGE;
        }
    }
    return 0;
}

static void fw_pfree(uint64_t frame)
{
    fw_used[frame / RAMFS_PAGE - 1] = 0;
}

static volatile void *fw_pmap(uint64_t frame)
{
    return fw_pool[frame / RAMFS_PAGE - 1];
}

static void fw_punmap(void)
{
}

static const struct ramfs_pages fw_source = { fw_palloc, fw_pfree, fw_pmap, fw_punmap };

static void *fw_halloc(size_t bytes)
{
    return malloc(bytes);
}

static void fw_hfree(void *p)
{
    free(p);
}

static const struct ramfs_mem fw_mem = { fw_halloc, fw_hfree };

static int64_t fw_clock(void)
{
    return 1795236000;
}

static void fw_dir(const char *path)
{
    CHECK_EQ(vfs_create(&fw_vfs, fw_vfs.root, path, VNODE_DIR, 0755, NULL), 0);
}

static void fw_file(const char *path, const char *text)
{
    struct vnode *n = NULL;
    CHECK_EQ(vfs_create(&fw_vfs, fw_vfs.root, path, VNODE_FILE, 0644, &n), 0);
    if (n)
        CHECK_EQ(vfs_write(&fw_vfs, n, 0, text, strlen(text)), strlen(text));
}

/* ---- the desktop: its window manager and menus, at 100 % ---- */

static uint8_t fw_font_bits[2][256 * 16];
static struct font fw_font_store[2];
static struct ui_fonts fw_fonts;
static struct ui_damage fw_dmg;
static struct ui_wm fw_wm;
static struct ui_window fw_about, fw_console, fw_files_win, fw_viewer_win;
static struct ui_menus fw_mn;
static struct files_app_env fw_env;
static uint64_t fw_now;

/* What files_app asked the menu hook for. */
static int fw_menu_calls;
static enum ui_menu_kind fw_kind;
static int32_t fw_x, fw_y;
static bool fw_by_key;
static uint8_t fw_greyed;

static void fw_damage(struct rect r)
{
    (void)r;
}

/* As desk_files_open: the window manager opens the window. */
static bool fw_open(struct ui_window *w)
{
    return ui_wm_open(&fw_wm, w);
}

/* As desk_files_menu: the desktop's menus open the Files menu. */
static void fw_menu(enum ui_menu_kind k, int32_t x, int32_t y, bool by_key, uint8_t greyed, uint8_t ticked)
{
    fw_menu_calls++;
    fw_kind = k;
    fw_x = x;
    fw_y = y;
    fw_by_key = by_key;
    fw_greyed = greyed;
    ui_menus_open_at(&fw_mn, k, x, y, by_key, greyed, ticked);
}

static void fw_clear(void)
{
    CHECK_EQ(fw_mn.lost_ev, 0);
    CHECK_EQ(fw_wm.lost_ev, 0);
    ui_menus_clear_events(&fw_mn);
    ui_wm_clear_events(&fw_wm);
    ui_damage_clear(&fw_dmg);
}

/* Every read or action Files started finished (one hold per pass). */
static void fw_settle(void)
{
    for (int k = 0; k < 4000 && (fa_reading || fa_job != FA_JOB_NONE); k++)
        files_app_poll(++fw_now);
    CHECK(!fa_reading && fa_job == FA_JOB_NONE);
    CHECK_EQ(fw_holds, 0);
}

static void fw_setup(void)
{
    for (int b = 0; b < 2; b++) {
        for (uint32_t k = 0; k < 256u * 16; k++)
            fw_font_bits[b][k] = (uint8_t)((k * (37u + 6u * b) + (k >> 3)) & (b ? 0xB5u : 0xA5u));
        fw_font_store[b] = (struct font){ 8, 16, 1, fw_font_bits[b] };
    }
    fw_fonts = (struct ui_fonts){ &fw_font_store[0], &fw_font_store[1] };
    /* The filesystem: /home/user with a folder and a file, a hidden
     * .local holding the Trash with one item and its info file. */
    CHECK_EQ(ramfs_init(&fw_fs, &fw_mem, RAMFS_MAX_NODES), 0);
    ramfs_set_pages(&fw_fs, &fw_source, FW_POOL);
    vfs_mount(&fw_vfs, &ramfs_ops, &fw_fs, &fw_fs.root->vn, fw_clock);
    fw_dir("/home");
    fw_dir("/home/user");
    fw_dir("/home/user/Documents");
    fw_file("/home/user/a.txt", "alpha\n");
    fw_dir("/home/user/.local");
    fw_dir("/home/user/.local/share");
    fw_dir(TRASH_DIR);
    fw_dir(TRASH_DIR "/files");
    fw_dir(TRASH_DIR "/info");
    fw_file(TRASH_DIR "/files/old.txt", "old\n");
    fw_file(TRASH_DIR "/info/old.txt.trashinfo",
            "[Trash Info]\nPath=/home/user/old.txt\nDeletionDate=2026-11-20T10:00:00\n");
    /* The desktop (1024 x 768, the taskbar from 728). */
    ui_damage_init(&fw_dmg, 1024, 768);
    ui_wm_init(&fw_wm, 1024, 768, 728, 4, &fw_dmg);
    fw_about = (struct ui_window){ .name = "About", .title = "About", .icon = UI_ICON_ABOUT, .w = 352, .h = 377,
                                   .esc_closes = true };
    fw_console = (struct ui_window){ .name = "Console", .title = "Console", .icon = UI_ICON_CONSOLE, .w = 642,
                                     .h = 433 };
    ui_menus_init(&fw_mn, &fw_wm, &fw_about, &fw_console, (struct rect){ 0, 728, 40, 40 }, &fw_fonts);
    /* Files, field by field as desktop.c sets it up. */
    files_app_options(false, false);
    fw_env.files = &fw_files_win;
    fw_env.viewer = &fw_viewer_win;
    fw_env.scale = 4;
    fw_env.fonts = &fw_fonts;
    fw_env.area_w = 1024;
    fw_env.area_h = 728;
    fw_env.damage = fw_damage;
    fw_env.open = fw_open;
    fw_env.logging = false;
    fw_env.menu = fw_menu;
    files_app_setup(&fw_env);
}

/* The Files window's layout now, its list placed in it (as files_app_right
 * places it). */
static struct ui_files_layout fw_layout(void)
{
    struct ui_files_layout lay;
    ui_files_layout(fw_files_win.r, 4, fw_fonts.regular, fa_m->view.bar != UI_FILES_BAR_NONE, &lay);
    fa_m->list.r = lay.content;
    return lay;
}

/* A right press as desktop.c hands it on (no menu open before it); the
 * menu hook must have been called once. */
static void fw_right(int32_t x, int32_t y)
{
    CHECK(!fw_mn.menu_open);
    fw_menu_calls = 0;
    files_app_right(&fw_files_win, x, y);
    CHECK_EQ(fw_menu_calls, 1);
}

/* The menu opened is kind k, really open in the desktop's menus, by the
 * pointer or the keyboard, with the greyed rows Files asked for. */
static void fw_opened(enum ui_menu_kind k, bool by_key)
{
    CHECK_EQ(fw_kind, k);
    CHECK(fw_by_key == by_key);
    CHECK(fw_mn.menu_open);
    CHECK_EQ(fw_mn.menu.kind, k);
    CHECK_EQ(fw_mn.menu.greyed, fw_greyed);
    int opens = 0;
    for (uint32_t i = 0; i < fw_mn.n_ev; i++) {
        if (fw_mn.ev[i].kind != UI_MEV_MENU_OPEN)
            continue;
        opens++;
        CHECK_EQ(fw_mn.ev[i].menu, k);
        CHECK_EQ(fw_mn.ev[i].why, by_key ? UI_WHY_KEYBOARD : UI_WHY_MOUSE);
    }
    CHECK_EQ(opens, 1);
}

/* A click outside the menu closes it (the screen's top left corner). */
static void fw_close(void)
{
    ui_menus_pointer(&fw_mn, 2, 2, true, false, false);
    ui_menus_pointer(&fw_mn, 2, 2, false, true, false);
    CHECK(!fw_mn.menu_open);
    fw_clear();
}

static void test_env_copied(void)
{
    fw_setup();
    CHECK(fa_env.files == &fw_files_win);
    CHECK(fa_env.viewer == &fw_viewer_win);
    CHECK_EQ(fa_env.scale, 4);
    CHECK(fa_env.fonts == &fw_fonts);
    CHECK_EQ(fa_env.area_w, 1024);
    CHECK_EQ(fa_env.area_h, 728);
    CHECK(fa_env.damage == fw_damage);
    CHECK(fa_env.open == fw_open);
    CHECK(!fa_env.logging);
    CHECK(fa_env.menu == fw_menu);      /* the hook the first sharded run found missing */
}

static void test_files_menus(void)
{
    files_app_open();
    CHECK(fa_m != 0);
    CHECK(fw_files_win.open);
    fw_settle();
    CHECK(!fa_m->view.trash);
    CHECK_EQ(fa_m->folder.n, 2);        /* Documents, a.txt (.local hidden) */
    fw_clear();
    /* A right press on a.txt (item 1): it is selected and its menu opens
     * at the press. */
    struct ui_files_layout lay = fw_layout();
    struct rect ir = ui_list_item_rect(&fa_m->list, 1);
    int32_t x = ir.x + 40, y = ir.y + ir.h / 2;
    fw_right(x, y);
    fw_opened(UI_MENU_FILES_ITEM, false);
    CHECK(fw_x == x && fw_y == y);
    CHECK(fa_m->list.items[1].selected && fa_m->list.cursor == 1);
    fw_close();
    /* The Menu key: the item menu at the focused item, by the keyboard. */
    struct key_event e = { KEY_MENU, 0, KEY_PRESSED, 0 };
    fw_menu_calls = 0;
    CHECK(files_app_key(&fw_files_win, &e));
    CHECK_EQ(fw_menu_calls, 1);
    fw_opened(UI_MENU_FILES_ITEM, true);
    ir = ui_list_item_rect(&fa_m->list, 1);
    CHECK(fw_x == ir.x + ui_px(36, 4) && fw_y == ir.y + ir.h);
    fw_close();
    /* A right press on empty space: the selection cleared, the space
     * menu at the press. */
    x = lay.content.x + 30;
    y = lay.content.y + lay.content.h - 6;
    fw_right(x, y);
    fw_opened(UI_MENU_FILES_SPACE, false);
    CHECK(fw_x == x && fw_y == y);
    CHECK(!fa_m->list.items[0].selected && !fa_m->list.items[1].selected);
    fw_close();
}

static void test_trash_menus(void)
{
    /* The Trash place (the sidebar's eighth row) pressed: the Trash view
     * with its one item. */
    struct ui_files_layout lay = fw_layout();
    struct rect pr = ui_files_place_rect(&lay, 4, 7);
    files_app_pointer(&fw_files_win, pr.x + pr.w / 2, pr.y + pr.h / 2, true, false, 0, ++fw_now);
    files_app_pointer(&fw_files_win, pr.x + pr.w / 2, pr.y + pr.h / 2, false, true, 0, ++fw_now);
    fw_settle();
    CHECK(fa_m->view.trash);
    CHECK_EQ(fa_m->folder.n, 1);        /* old.txt */
    fw_clear();
    lay = fw_layout();
    struct rect ir = ui_list_item_rect(&fa_m->list, 0);
    int32_t x = ir.x + 40, y = ir.y + ir.h / 2;
    fw_right(x, y);
    fw_opened(UI_MENU_TRASH_ITEM, false);
    CHECK(fw_x == x && fw_y == y);
    fw_close();
    x = lay.content.x + 30;
    y = lay.content.y + lay.content.h - 6;
    fw_right(x, y);
    fw_opened(UI_MENU_TRASH_SPACE, false);
    CHECK(fw_x == x && fw_y == y);
    fw_close();
}

int main(void)
{
    printf("test_ui_fileswire\n");
    RUN(test_env_copied);
    RUN(test_files_menus);
    RUN(test_trash_menus);
    return unit_report("test_ui_fileswire");
}
