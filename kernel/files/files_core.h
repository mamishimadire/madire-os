#ifndef MADIRE_FILES_FILES_CORE_H
#define MADIRE_FILES_FILES_CORE_H

/*
 * The Files app's pure part (1.5 stage 11; docs/plans/1.5.md, C5, C6c rule
 * SL, the S11 row and its approvals; risks 56, 57 and 66). PURE: no file
 * access, allocation, printing or static data. Every buffer is the
 * caller's: the Files window allocates them when it opens and frees them
 * when it closes (S12; the owner's memory rule); the host unit suite
 * (tests/unit/test_files_core.c) and the host tool (tests/filesdump.c,
 * against tests/files_ref.py) give their own. File access goes through the
 * caller's calls (struct files_fs, files_exists_fn), each one bounded: S12
 * and S13 make each one VFS call under the lock rules (C1, L1-L6).
 *
 * The folder (struct files_folder): entries added in the folder's own
 * order; "." and ".." skipped; names starting with '.' skipped unless
 * show_hidden is set (they never count towards the limit; turning the
 * setting on or off reads the folder again); at most cap (FILES_MAX_ENTRIES,
 * 1,000) entries: the next shown entry sets truncated and is refused
 * (FILES_ADD_FULL: the reading stops; the list ends with "Showing the first
 * 1000 items of this folder."), so the entries kept are the first 1,000 in
 * the folder's own order. Each name keeps its own bytes (open, delete and
 * move use them); a name with a byte outside printable ASCII is marked
 * FILES_F_ODD and shown with '?' for each such byte (ui_name_shown).
 *
 * Sorting (the plan, approved again in S11): folders first, then by name:
 * byte order with ASCII letters folded to lower case; names equal after
 * folding by their raw bytes. Links sort with the files (never followed).
 * In place (heap sort): no recursion, no extra memory.
 *
 * The texts (approved in S11): the Type column "Folder", "Link", "Text
 * file" (the extensions txt, md, log, conf, cfg, ini, csv, json, sh, c, h,
 * in any case; the extension starts at the last '.' that is not the name's
 * first byte) or "File"; the Size column "<n> B" under 1 KiB, else "<w>.<t>
 * KiB", "MiB" or "GiB" (the tenth cut, never rounded up); the Modified
 * column "YYYY-MM-DD hh:mm" (the clock's local seconds since 1970; before
 * 1970 shown as 1970-01-01 00:00, after 9999 as 9999-12-31 23:59; the same
 * day and time as the console's shell_date).
 *
 * The history (per Files window, FILES_HISTORY_MAX = 64 entries, each a
 * view and a path of at most FILES_PATH_MAX bytes; the caller's store of
 * FILES_HISTORY_BYTES): a move adds an entry after the current one (the
 * forward entries are dropped first, as browsers do; a move to the current
 * place adds nothing); the 65th drops the oldest. Back and Forward skip
 * and remove every entry whose folder the caller's exists call refuses
 * (missing or answering an error: "That folder no longer exists."). Up
 * goes to the parent folder; it is greyed at "/" and in Trash and This PC.
 *
 * The path bar: the parts joined by " > ": under /home/user it starts at
 * "Home" (the folder /home/user), elsewhere at "/"; Trash and This PC are
 * one part each. Each part jumps to its folder (the path's first `target`
 * bytes). If the parts need more than max_cells cells (one cell per byte),
 * or more than FILES_PB_MAX parts, the start is clipped: "..." and " > "
 * and the largest number of last parts that fit (the first part never);
 * if not even the last part fits that way, "..." is followed directly by
 * the last part's last max_cells - 3 bytes (nothing if max_cells < 4).
 *
 * The clipboard: up to FILES_CLIP_MAX = 64 paths, cut or copy, in the
 * caller's store of FILES_CLIP_BYTES; Paste is greyed while it is empty.
 *
 * Names for Paste and Restore: files_unique_name() tries the name itself,
 * then "<base> 2<ext>" ... "<base> 999<ext>" (FILES_VARIANT_NUMBER; as the
 * default names of 1.5 stage 10), or "<base> (restored)<ext>", "<base>
 * (restored 2)<ext>" ... (FILES_VARIANT_RESTORED); a folder has no
 * extension; the base is cut (by bytes) so that the name fits 255 bytes.
 * None free: 0 ("Can't paste '<name>': too many items with this name.").
 *
 * The recursive steps (struct files_walk): delete (Move to Trash's
 * "delete permanently", Delete permanently, Empty Trash) and copy (Copy and
 * Paste; a move across filesystems is a copy, then, only if it finished
 * with no link skipped, a delete), one bounded action per files_walk_step()
 * call (one entry, one folder made or removed, one chunk of a file), so the
 * desktop keeps running between them. Rule SL: each entry's kind is the
 * caller's lstat of the entry itself; a link is never followed: deleted as
 * an entry, never descended; copying a link is refused (the copy's start a
 * link: FILES_WALK_LINK, nothing done; a link inside a folder: skipped and
 * counted, the first one's name kept for "Can't copy '<name>': it is a
 * link, and links can't be copied yet."). Depth: the start is depth 0; a
 * folder more than FILES_DEPTH_MAX (64) folders below it stops the walk
 * before anything is done in it (FILES_WALK_DEEP: "This folder is nested
 * too deeply (more than 64 levels)."; `done` items stay done). A path over
 * FILES_PATH_MAX bytes: FILES_WALK_TOO_LONG ("This path is too long.").
 * A failed call: FILES_WALK_ERROR with its value (-ENOMEM: "Too many files
 * are in use: close some windows and try again."); a file copy that fails
 * part way removes its partial copy. files_check_target() checks a Paste
 * target folder (rule SL): every component a real folder.
 */

#include <stdbool.h>
#include <stdint.h>

#define FILES_MAX_ENTRIES 1000u
#define FILES_NAME_MAX    255u
#define FILES_PATH_MAX    4096u
#define FILES_DEPTH_MAX   64u
#define FILES_HISTORY_MAX 64u
#define FILES_CLIP_MAX    64u
#define FILES_PB_MAX      32u
#define FILES_VARIANT_MAX 999u
#define FILES_HOME        "/home/user"
#define FILES_SIZE_TEXT   24u       /* "17179869183.9 GiB" and its NUL fit */
#define FILES_DATE_TEXT   17u       /* "YYYY-MM-DD hh:mm" and its NUL */
#define FILES_DATE_MAX    253402300799ll    /* 9999-12-31 23:59:59 */

/* The bytes of the stores the caller allocates. */
#define FILES_HISTORY_BYTES (FILES_HISTORY_MAX * (FILES_PATH_MAX + 1u))
#define FILES_CLIP_BYTES    (FILES_CLIP_MAX * (FILES_PATH_MAX + 1u))

enum files_kind { FILES_K_NONE, FILES_K_DIR, FILES_K_FILE, FILES_K_LINK, FILES_K_ERROR };

/* ---- The folder ---- */

#define FILES_F_HIDDEN 1u           /* the name starts with '.' */
#define FILES_F_ODD    2u           /* a byte outside printable ASCII */

struct files_entry {
    uint32_t name_off;              /* in the names store, NUL-terminated */
    uint16_t name_len;
    uint8_t kind;                   /* FILES_K_DIR, FILES_K_FILE or FILES_K_LINK */
    uint8_t flags;
    uint64_t size;
    int64_t mtime;
};

struct files_folder {
    struct files_entry *e;
    uint32_t cap, n;
    char *names;
    uint32_t names_size, names_used;
    bool show_hidden, truncated;
};

enum files_add { FILES_ADD_OK, FILES_ADD_SKIPPED, FILES_ADD_FULL, FILES_ADD_BAD };

/* The bytes a folder of cap entries needs: the entries and a names store
 * that holds cap names of FILES_NAME_MAX bytes with their NULs. */
uint32_t files_folder_bytes(uint32_t cap);
void files_folder_init(struct files_folder *f, struct files_entry *e, uint32_t cap, char *names,
                       uint32_t names_size, bool show_hidden);
/* FILES_ADD_SKIPPED: "." or "..", or hidden while show_hidden is off;
 * FILES_ADD_BAD: an empty name, more than 255 bytes, a '/' or NUL byte, or a
 * kind that is not a folder, a file or a link; FILES_ADD_FULL: the limit
 * (or the names store) reached: truncated is set and the reading stops. */
enum files_add files_folder_add(struct files_folder *f, const char *name, uint32_t len, enum files_kind kind,
                                uint64_t size, int64_t mtime);
const char *files_entry_name(const struct files_folder *f, uint32_t i);
void files_folder_sort(struct files_folder *f);

/* <0, 0 or >0: name a before name b (folders not considered). */
int files_name_compare(const char *a, uint32_t la, const char *b, uint32_t lb);

/* ---- The column texts ---- */

const char *files_type_text(enum files_kind kind, const char *name, uint32_t len);
void files_size_text(uint64_t bytes, char out[FILES_SIZE_TEXT]);
void files_date_text(int64_t seconds, char out[FILES_DATE_TEXT]);

/* ---- The history ---- */

enum files_view { FILES_VIEW_FOLDER, FILES_VIEW_TRASH, FILES_VIEW_THISPC };

struct files_history {
    char *store;                    /* FILES_HISTORY_BYTES */
    uint16_t len[FILES_HISTORY_MAX];
    uint8_t view[FILES_HISTORY_MAX];
    uint32_t first, count, pos;     /* a ring: entry i at slot (first + i) % 64 */
};

/* The caller's folder test (path NUL-terminated): false if the folder is
 * missing or answers an error. Also the name test of files_unique_name. */
typedef bool (*files_exists_fn)(void *ctx, const char *path);

void files_history_init(struct files_history *h, char *store);
/* Adds a move to view (and path, len bytes, for a folder); false (nothing
 * changed) for a folder path that is empty or over FILES_PATH_MAX bytes. */
bool files_history_go(struct files_history *h, enum files_view view, const char *path, uint32_t len);
/* The current entry; false while the history is empty. */
bool files_history_current(const struct files_history *h, enum files_view *view, const char **path,
                           uint32_t *len);
bool files_history_can_back(const struct files_history *h);
bool files_history_can_forward(const struct files_history *h);
bool files_history_can_up(const struct files_history *h);
/* Back or Forward to the nearest entry that exists (*removed: how many were
 * skipped and removed); false if none (the current entry stays). */
bool files_history_back(struct files_history *h, files_exists_fn exists, void *ctx, uint32_t *removed);
bool files_history_forward(struct files_history *h, files_exists_fn exists, void *ctx, uint32_t *removed);
/* Up: a move to the current folder's parent; false if greyed. */
bool files_history_up(struct files_history *h);

/* ---- The path bar ---- */

struct files_pb_part {
    const char *text;               /* into the path, or a constant */
    uint16_t len;                   /* the bytes shown */
    uint16_t x;                     /* its first cell */
    uint16_t target;                /* the folder it jumps to: the path's first `target` bytes; 0 for Trash, This PC */
};

struct files_pathbar {
    struct files_pb_part part[FILES_PB_MAX];
    uint32_t n;
    bool clipped;                   /* "..." at cell 0 */
    uint32_t cells;                 /* every cell used, "..." and " > " included */
};

void files_pathbar(enum files_view view, const char *path, uint32_t len, uint32_t max_cells,
                   struct files_pathbar *pb);

/* ---- The clipboard ---- */

struct files_clip {
    char *store;                    /* FILES_CLIP_BYTES */
    uint32_t off[FILES_CLIP_MAX];
    uint16_t len[FILES_CLIP_MAX];
    uint32_t n, used;
    bool cut;
};

enum files_clip_add { FILES_CLIP_OK, FILES_CLIP_FULL, FILES_CLIP_TOO_LONG };

void files_clip_init(struct files_clip *c, char *store);
/* Empties it and starts a new set (cut or copy). */
void files_clip_begin(struct files_clip *c, bool cut);
enum files_clip_add files_clip_add(struct files_clip *c, const char *path, uint32_t len);
bool files_clip_empty(const struct files_clip *c);     /* Paste greyed */
const char *files_clip_path(const struct files_clip *c, uint32_t i, uint32_t *len);
void files_clip_clear(struct files_clip *c);

/* ---- Names for Paste and Restore ---- */

enum files_variant { FILES_VARIANT_NUMBER, FILES_VARIANT_RESTORED };

/* Try n of name (1 to 255 bytes) into out (FILES_NAME_MAX + 1 bytes): its
 * length, or 0 if n is out of range or the name cannot be cut to fit.
 * FILES_VARIANT_NUMBER: n 1 is the name itself, 2-999 "<base> <n><ext>";
 * FILES_VARIANT_RESTORED: n 1 "<base> (restored)<ext>", 2-999
 * "<base> (restored <n>)<ext>". */
uint32_t files_name_variant(const char *name, uint32_t len, bool folder, enum files_variant v, uint32_t n,
                            char *out);
/* The first try that exists() refuses (in order from n 1); 0 if none. */
uint32_t files_unique_name(const char *name, uint32_t len, bool folder, enum files_variant v,
                           files_exists_fn exists, void *ctx, char *out);

/* ---- The recursive steps (rule SL) ---- */

struct files_fs {
    void *ctx;
    /* The kind of path itself (a link is FILES_K_LINK, never its target). */
    enum files_kind (*lstat)(void *ctx, const char *path);
    /* The next entry of folder path after *cookie (0: from its start): 1 and
     * its name (NUL-terminated, 1 to 255 bytes) in name (FILES_NAME_MAX + 1
     * bytes), 0 at the end, negative on an error; never "." or "..". */
    int (*read_dir)(void *ctx, const char *path, uint64_t *cookie, char *name);
    int (*mkdir)(void *ctx, const char *path);
    int (*unlink)(void *ctx, const char *path);
    int (*rmdir)(void *ctx, const char *path);
    /* Copies the next chunk of file src at offset into dst (made, empty and
     * exclusively, when offset is 0): the bytes copied (more than 0), 0 when
     * src ends at offset (dst complete), negative on an error. */
    int64_t (*copy_chunk)(void *ctx, const char *src, const char *dst, uint64_t offset);
};

enum files_walk_op { FILES_WALK_DELETE, FILES_WALK_COPY };

enum files_walk_result {
    FILES_WALK_MORE,
    FILES_WALK_DONE,
    FILES_WALK_DEEP,
    FILES_WALK_TOO_LONG,
    FILES_WALK_LINK,
    FILES_WALK_ERROR
};

struct files_walk_level {
    uint64_t cookie;
    uint16_t src_len, dst_len;
};

struct files_walk {
    const struct files_fs *fs;
    enum files_walk_op op;
    char src[FILES_PATH_MAX + 1], dst[FILES_PATH_MAX + 1];
    char name[FILES_NAME_MAX + 1];
    char link_name[FILES_NAME_MAX + 1];     /* the first link skipped (or the start, FILES_WALK_LINK) */
    uint32_t src_len, dst_len;
    struct files_walk_level lv[FILES_DEPTH_MAX + 1];
    uint32_t depth;                 /* folders open: 1 for the start */
    bool started, copying, finished;
    uint64_t offset;
    uint32_t done, links_skipped;
    int64_t err;
    enum files_walk_result result;
};

/* src: the item to delete or copy; dst: the copy's full path (copy only).
 * Absolute, not "/", at most FILES_PATH_MAX bytes: else false (and
 * FILES_WALK_TOO_LONG for a long one, FILES_WALK_ERROR otherwise). */
bool files_walk_start(struct files_walk *w, const struct files_fs *fs, enum files_walk_op op, const char *src,
                      const char *dst);
/* One action; FILES_WALK_MORE until the walk ends; then its result again. */
enum files_walk_result files_walk_step(struct files_walk *w);

enum files_target { FILES_TARGET_OK, FILES_TARGET_NOT_FOLDER, FILES_TARGET_MISSING, FILES_TARGET_ERROR };

/* Paste's target folder (absolute, NUL-terminated, at most FILES_PATH_MAX
 * bytes): every component and the folder itself a real folder by lstat.
 * *at: the prefix's length where it failed ("<folder> is not a folder").
 * scratch: FILES_PATH_MAX + 1 bytes. */
enum files_target files_check_target(const struct files_fs *fs, const char *path, char *scratch, uint32_t *at);

/* How a move goes: a rename on the same filesystem (a link moved as a link);
 * across filesystems a copy and a delete, refused for a link (it cannot be
 * copied). */
enum files_move { FILES_MOVE_RENAME, FILES_MOVE_COPY_DELETE, FILES_MOVE_REFUSE_LINK };
enum files_move files_move_how(enum files_kind kind, bool same_fs);

#endif
