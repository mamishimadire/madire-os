#ifndef MADIRE_DISPLAY_FILES_APP_H
#define MADIRE_DISPLAY_FILES_APP_H

/*
 * The Files app and the Text Viewer, the kernel side (1.5 stage 12;
 * docs/plans/1.5.md, C5, C6b, C6c and the S12 row with the owner's
 * approvals). On thread 0 with the desktop (desktop.c routes its input
 * here); the picture is ui_files.h's and ui_viewer.h's, drawn on exposure
 * (no surface). One Files window and one Text Viewer at once.
 *
 * Memory (the owner's rule): the Files window's state, its folder (1,000
 * entries and their names), its list items and texts and its history (64
 * paths) live in pages of free RAM mapped at VM_FILES_BASE when the window
 * opens, each page ZEROED through its new mapping before use; the Text
 * Viewer's (its 64 KiB) at VM_VIEWER_BASE when it opens. A window opens
 * only if at least FILES_APP_SPARE_PAGES pages stay free after it; a
 * window that cannot get its memory shows only "Not enough memory to open
 * Files." (Files) and its close button. At the close the pointers into the
 * pages are cleared first, then every page is unmapped (paging_unmap
 * invalidates its TLB entry) before its frame is given back. The page
 * tables the first open added stay (paging_unmap frees none).
 *
 * The folder is read in holds of at most 16 entries, one hold of the VFS
 * lock per pass of the desktop's loop (files_app_poll); Files keeps names
 * and figures only, never a node (risk 43). The file calls follow the lock
 * rules (C1): fd_vfs_acquire around each one; a change answering -FS_EAGAIN
 * is tried again after a save (ext2_disk_retry). Links are never followed
 * (rule SL): a link opens the info panel with the type Link.
 */

#include <stdbool.h>
#include <stdint.h>
#include "drivers/keyboard/scancode.h"
#include "kernel/display/ui_draw.h"
#include "kernel/display/ui_font.h"
#include "kernel/display/ui_window.h"

#define FILES_APP_SPARE_PAGES 512u

/* What the desktop gives: the windows (desktop.c's), the scale, the fonts,
 * the screen's area above the taskbar, damage and the window manager's
 * open (false if the window could not open), and whether desktoplog is on
 * (the "files:" state lines for tests/qmp_desktop.py). */
struct files_app_env {
    struct ui_window *files, *viewer;
    uint32_t scale;
    const struct ui_fonts *fonts;
    int32_t area_w, area_h;
    void (*damage)(struct rect r);
    bool (*open)(struct ui_window *w);
    bool logging;
};

/* The test options: filesnomem (the last page's allocation fails on
 * purpose), filesfill (the Live home gains /home/user/Many with 1,005 empty
 * files). */
void files_app_options(bool nomem, bool fill);
void files_app_setup(const struct files_app_env *env);

/* Live mode's home folders (vfs_live_home), logged; with filesfill, the
 * 1,005 files. Called once when the desktop starts. */
void files_app_live_home(void);

/* Super+E (and later the menus): prepares the Files window (its memory, or
 * the message) and opens it through env->open; if it is open already, the
 * caller activates it instead. */
void files_app_open(void);

/* The window w (Files or the Text Viewer) was closed by the window
 * manager: its memory goes back. */
void files_app_closed(struct ui_window *w);

/* One pass of the desktop's loop: one hold of a folder or file read, the
 * tooltip's time. */
void files_app_poll(uint64_t now);

/* The pointer at (x, y) (screen), the left button pressed or released
 * there; `top` the window at (x, y) (or none); mods KEY_SHIFT, KEY_CTRL. */
void files_app_pointer(struct ui_window *top, int32_t x, int32_t y, bool down, bool up, uint8_t mods,
                       uint64_t now);

/* A key for the focused window w (Files or the Text Viewer): true if it
 * was used (false: Esc that nothing in the window took). */
bool files_app_key(struct ui_window *w, const struct key_event *e);

/* The wheel over window w. */
void files_app_wheel(struct ui_window *w, int32_t x, int32_t y, int32_t notches);

/* The windows' clients (struct ui_window's paint). */
void files_app_paint(const struct ui_canvas *c, struct rect r, void *ctx);
void files_app_viewer_paint(const struct ui_canvas *c, struct rect r, void *ctx);

#endif
