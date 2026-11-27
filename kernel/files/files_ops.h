#ifndef MADIRE_FILES_FILES_OPS_H
#define MADIRE_FILES_FILES_OPS_H

/*
 * The Files app's actions, their pure part (1.5 stage 13; docs/plans/
 * 1.5.md, C5, C6, C6b, C6c rule SL, notes (13) and the S13 decisions
 * D1-D5 and Q1-Q9; host unit tests in tests/unit/test_files_ops.c). PURE:
 * no file access, allocation, printing or static data. files_app.c makes
 * the file calls; this decides what they may be and what the user reads.
 *
 *   - files_ops_trashable: only an item under /home/user/ (not /home/user
 *     itself) and not the Trash folder or anything in it may go to the
 *     Trash (Q7: the .trashinfo parser accepts no other Path, so anything
 *     else could never be restored); anything else is asked about ("It
 *     can't go to the Trash.").
 *   - files_ops_into_itself: a folder pasted into itself or into a folder
 *     inside it ("Can't paste '<name>' into itself.").
 *   - files_ops_parent_len, files_ops_name_off: a path's folder and name.
 *   - files_ops_location: the Trash view's Original location column: the
 *     folder written as the path bar writes it ("Home > Documents" under
 *     /home/user, "/ > tmp" elsewhere), clipped at the start with "..." to
 *     the cells given (files_core.h's files_pathbar); bytes outside
 *     printable ASCII as '?'.
 *   - files_ops_date: local seconds since 1970 as a trash_date (the
 *     DeletionDate written; before 1970 as 1970-01-01 00:00:00, after 9999
 *     as 9999-12-31 23:59:59).
 *   - files_ops_question: the question panel's two lines and its action
 *     button's label, word for word as approved (C6b and the S13 report);
 *     a name over FILES_OPS_NAME_SHOWN bytes is cut to its first
 *     FILES_OPS_NAME_SHOWN - 3 bytes and "...", bytes outside printable
 *     ASCII as '?'.
 *   - files_ops_greyed: each Files menu's greyed rows (ui_menu.h's row
 *     order; row k is bit k) from what is selected and writable:
 *       Files item:  Open and Properties need exactly one item selected;
 *                    Cut and Move to Trash at least one and a writable
 *                    folder; Copy at least one; Paste a clipboard and a
 *                    writable target; Rename exactly one and a writable
 *                    folder;
 *       Files space: New Folder and New File a writable folder; Paste a
 *                    clipboard and a writable folder; Show hidden files
 *                    never greyed;
 *       Trash item:  Restore at least one restorable item selected and a
 *                    writable /home; Delete permanently at least one and
 *                    a writable /home; Properties exactly one;
 *       Trash space: Restore all and Empty Trash at least one item in the
 *                    Trash and a writable /home; Show hidden files never.
 */

#include <stdbool.h>
#include <stdint.h>
#include "kernel/files/trash_core.h"

#define FILES_OPS_NAME_SHOWN 40u        /* a name in a question, at most */
#define FILES_OPS_LINE_MAX   128u       /* a question line and its NUL fit */
#define FILES_OPS_LABEL_MAX  16u        /* an action label and its NUL fit */

bool files_ops_trashable(const char *path, uint32_t len);
bool files_ops_into_itself(const char *src, uint32_t src_len, const char *dst, uint32_t dst_len);

/* "/a/b" -> 2 ("/a"); "/a" -> 1 ("/"); "/" or "" -> 0. */
uint32_t files_ops_parent_len(const char *path, uint32_t len);
/* The offset of the last component's first byte (after the last '/'). */
uint32_t files_ops_name_off(const char *path, uint32_t len);

/* The location text of folder (len bytes) in at most cells cells into out
 * (size bytes, at least cells + 1 for the whole text); its length. */
uint32_t files_ops_location(const char *folder, uint32_t len, uint32_t cells, char *out, uint32_t size);

void files_ops_date(int64_t seconds, struct trash_date *d);

enum files_question {
    FILES_Q_DELETE_ONE,         /* "Delete '<name>' permanently?" (Shift+Delete; Delete in the Trash) */
    FILES_Q_DELETE_MANY,        /* "Delete <n> items permanently?" */
    FILES_Q_NOTRASH_ONE,        /* "Delete '<name>' permanently?" / "It can't go to the Trash." */
    FILES_Q_NOTRASH_MANY,       /* "Delete <n> items permanently?" / "They can't go to the Trash." */
    FILES_Q_EMPTY,              /* "Empty the Trash?" / "<n> items will be deleted permanently." */
    FILES_Q_EXISTS              /* "'<name>' already exists in <folder>." (Keep both) */
};

/* The question q about name (or n items; folder for FILES_Q_EXISTS) into
 * line1 and line2 (FILES_OPS_LINE_MAX bytes each; line2 may be empty) and
 * the action button's label (FILES_OPS_LABEL_MAX bytes). */
void files_ops_question(enum files_question q, const char *name, uint32_t n, const char *folder, char *line1,
                        char *line2, char *label);

/* The depth limit's message (plan C5: the items already done stay done and
 * the message says how many): "This folder is nested too deeply (more than
 * 64 levels) (<n> items done)." ("1 item done" for one) into out (at least
 * FILES_OPS_LINE_MAX bytes). */
void files_ops_deep_text(uint32_t done, char *out);

enum files_ops_menu { FILES_OPS_MENU_ITEM, FILES_OPS_MENU_SPACE, FILES_OPS_MENU_TRASH_ITEM,
                      FILES_OPS_MENU_TRASH_SPACE };

struct files_ops_state {
    uint32_t selected;          /* items selected */
    uint32_t restorable;        /* of them, with a known original location (the Trash) */
    uint32_t in_trash;          /* items the Trash holds (the Trash view) */
    bool writable;              /* the folder shown (the Trash: /home) can change */
    bool clip;                  /* the clipboard holds something */
    bool paste_writable;        /* Paste's target can change (a folder item, or the folder shown) */
};

uint8_t files_ops_greyed(enum files_ops_menu menu, const struct files_ops_state *st);

/*
 * 1.5 stage 14b: drag and drop inside Files (docs/plans/1.5.md, notes (14)
 * and (15)): what a drop of the n items of folder `dir` would do, in this
 * order of checks:
 *   - more than FILES_DRAG_MAX items: nothing (FILES_DW_TOO_MANY; Files
 *     does not start such a drag: "Only 64 items can be dragged at once.");
 *     none: nothing (FILES_DW_NOWHERE);
 *   - items of the Trash view: nothing (FILES_DW_FROM_TRASH: no drag out of
 *     the Trash in 1.5; Restore is in the menu);
 *   - no target (empty space, outside the window, This PC): nothing
 *     (FILES_DW_NOWHERE); a file or a link: nothing (FILES_DW_NOT_FOLDER);
 *   - the Trash: to the Trash (FILES_DROP_TRASH: the Delete key's action,
 *     with its question for items that can't go there), if the items'
 *     folder can change (else FILES_DW_READONLY);
 *   - a folder: nothing if it is the items' own folder (FILES_DW_HERE), or
 *     one of the items or inside one (FILES_DW_ITSELF), or cannot change
 *     (FILES_DW_READONLY); a copy if Ctrl is held or it is on another
 *     filesystem (FILES_DROP_COPY); a move if the items' folder can change
 *     (FILES_DROP_MOVE), else nothing (FILES_DW_SOURCE_RO).
 * The items' names come from in->name (i < n; their length in *len).
 */
#define FILES_DRAG_MAX 64u

enum files_drop_target { FILES_DT_NONE, FILES_DT_FOLDER, FILES_DT_FILE, FILES_DT_TRASH };
enum files_drop { FILES_DROP_NO, FILES_DROP_MOVE, FILES_DROP_COPY, FILES_DROP_TRASH };
enum files_drop_why {
    FILES_DW_OK, FILES_DW_NOWHERE, FILES_DW_NOT_FOLDER, FILES_DW_HERE, FILES_DW_ITSELF, FILES_DW_READONLY,
    FILES_DW_SOURCE_RO, FILES_DW_FROM_TRASH, FILES_DW_TOO_MANY
};

struct files_drop_in {
    enum files_drop_target target;
    const char *dir;                    /* the folder the items are in */
    uint32_t dir_len;
    const char *to;                     /* the target folder (FILES_DT_FOLDER) */
    uint32_t to_len;
    uint32_t n;                         /* the items */
    const char *(*name)(void *ctx, uint32_t i, uint32_t *len);
    void *ctx;
    bool ctrl;                          /* Ctrl held */
    bool same_fs;                       /* the target is on the items' filesystem */
    bool src_writable, dst_writable;    /* the items' folder, the target folder can change */
    bool from_trash;                    /* the items are the Trash view's */
};

enum files_drop files_ops_drop(const struct files_drop_in *in, enum files_drop_why *why);

/* The words logged ("files: drag over ..."): "move", "copy", "trash", "no";
 * the reasons "ok", "nowhere", "not a folder", "here", "into itself",
 * "read-only", "source read-only", "from the Trash", "too many". */
const char *files_ops_drop_word(enum files_drop d);
const char *files_ops_drop_why_word(enum files_drop_why w);

#endif
