#ifndef MADIRE_FILES_TRASH_CORE_H
#define MADIRE_FILES_TRASH_CORE_H

/*
 * The Trash's pure part (1.5 stage 11; docs/plans/1.5.md, C6 with addition
 * a, C6c rule SL, risks 55, 63 and 66). PURE: no file access, allocation,
 * printing or static data; the host unit suite (tests/unit/
 * test_trash_core.c) and the host tool (tests/filesdump.c, against
 * tests/files_ref.py) run it as the kernel will (the Files app, S13).
 *
 * A `.trashinfo` file is untrusted input. trash_info_parse() reads it with
 * every length bounded, in this order:
 *   - more than TRASH_INFO_MAX (8 KiB) bytes: TI_FILE_TOO_LARGE (refused
 *     unread);
 *   - lines end at '\n' (or the buffer's end); any line longer than
 *     TRASH_LINE_MAX (4,096 + 64) bytes, wherever it is: TI_LINE_TOO_LONG;
 *   - the first non-empty line must be exactly "[Trash Info]":
 *     TI_BAD_HEADER;
 *   - after it, the first line starting "Path=" and the first starting
 *     "DeletionDate=" count; later repeats and every other line are ignored
 *     (as the specification says); no Path=: TI_NO_PATH;
 *   - Path's value is strictly percent-decoded: every '%' must be followed
 *     by two hexadecimal digits (TI_BAD_ESCAPE); no NUL byte, raw or
 *     escaped (TI_NUL); no "%2F" (TI_ESCAPED_SLASH); at most TRASH_PATH_MAX
 *     (4,096) bytes decoded (TI_PATH_TOO_LONG);
 *   - the decoded path must be absolute (TI_RELATIVE), must not end with
 *     '/' (TI_TRAILING_SLASH), every component non-empty (TI_EMPTY_
 *     COMPONENT), not "." or ".." (TI_DOT_COMPONENT), at most 255 bytes
 *     (TI_COMPONENT_TOO_LONG), checked from the left; it must lie under
 *     TRASH_HOME, "/home/user/" (TI_OUTSIDE_HOME), and must not be
 *     TRASH_DIR or anything inside it (TI_INSIDE_TRASH).
 * A DeletionDate that is missing or not exactly YYYY-MM-DDThh:mm:ss with
 * valid ranges (year 0001-9999, the month's days with leap years, hour
 * 0-23, minute and second 0-59) only clears date_ok: the item stays
 * restorable and shows "(unknown date)".
 *
 * Writing: trash_path_encode() keeps the letters, digits, "-_.!~*'()" and
 * '/', and writes every other byte as %XX (upper-case hex; RFC 2396 section
 * 2); trash_info_build() makes the whole file and refuses (0) anything
 * trash_info_parse() would refuse, so every file Madire writes can be read
 * back. trash_slot_name() names the item in files/ (and its info file):
 * "<name>" for the first try, "<name>.<n>" for n = 2 to 999, the name cut
 * (by bytes) so that "<slot>.trashinfo" fits 255 bytes.
 *
 * Restore (rule SL): trash_restore_check() looks at each existing component
 * of the original folder from the root, by the caller's lstat (a
 * component's own type, never its target's): a link or a file there refuses
 * the restore (TR_PARENT_NOT_FOLDER, with the prefix's length for the
 * message "<folder> is not a folder"); a missing component means the
 * folders from there on are made again (TR_MAKE_PARENTS); the item's own
 * name already used is TR_EXISTS ("Keep both").
 *
 * Facts from the FreeDesktop Trash specification 1.0 (references.md);
 * written from scratch.
 */

#include <stdbool.h>
#include <stdint.h>

#define TRASH_INFO_MAX   8192u
#define TRASH_LINE_MAX   (4096u + 64u)
#define TRASH_PATH_MAX   4096u
#define TRASH_NAME_MAX   255u
#define TRASH_SLOT_MAX   999u
#define TRASH_HOME       "/home/user/"
#define TRASH_DIR        "/home/user/.local/share/Trash"
#define TRASH_INFO_EXT   ".trashinfo"
#define TRASH_DATE_LEN   19u     /* YYYY-MM-DDThh:mm:ss */

enum trash_info_result {
    TI_OK,
    TI_FILE_TOO_LARGE,
    TI_LINE_TOO_LONG,
    TI_BAD_HEADER,
    TI_NO_PATH,
    TI_BAD_ESCAPE,
    TI_NUL,
    TI_ESCAPED_SLASH,
    TI_PATH_TOO_LONG,
    TI_RELATIVE,
    TI_TRAILING_SLASH,
    TI_EMPTY_COMPONENT,
    TI_DOT_COMPONENT,
    TI_COMPONENT_TOO_LONG,
    TI_OUTSIDE_HOME,
    TI_INSIDE_TRASH,
    TI_RESULTS
};

struct trash_date {
    uint16_t year;
    uint8_t month, day, hour, minute, second;
};

struct trash_info {
    char path[TRASH_PATH_MAX + 1];      /* decoded, NUL-terminated */
    uint32_t path_len;
    bool date_ok;
    struct trash_date date;
};

/* Parses len bytes; out is filled only as far as the result says (path on
 * TI_OK; date_ok and date whenever the header was found). */
enum trash_info_result trash_info_parse(const uint8_t *buf, uint32_t len, struct trash_info *out);

/* A short name for a result (logs and the host tool; not shown on screen). */
const char *trash_info_reason(enum trash_info_result r);

/* Encodes path (len bytes) into out; the encoded length, or 0 if it (and its
 * NUL) does not fit out_size. */
uint32_t trash_path_encode(const char *path, uint32_t len, char *out, uint32_t out_size);

/* Decodes text (len bytes) by the rules above (escapes, NUL, %2F, length):
 * TI_OK and *out_len, or the first rule broken. out holds TRASH_PATH_MAX + 1. */
enum trash_info_result trash_path_decode(const char *text, uint32_t len, char *out, uint32_t *out_len);

/* Checks a decoded path (len bytes) by the path rules above: TI_OK or the
 * first rule broken (TI_RELATIVE ... TI_INSIDE_TRASH). */
enum trash_info_result trash_path_check(const char *path, uint32_t len);

/* Whether d is a valid date by the rules above. */
bool trash_date_valid(const struct trash_date *d);

/* The whole .trashinfo file for path (decoded, len bytes) and d into out:
 * its length, or 0 if path or d would be refused on reading, or if it does
 * not fit out_size or TRASH_INFO_MAX. */
uint32_t trash_info_build(const char *path, uint32_t len, const struct trash_date *d, char *out,
                          uint32_t out_size);

/* The slot name for try n (1 to TRASH_SLOT_MAX) of name (1 to 255 bytes,
 * no '/') into out (at least TRASH_NAME_MAX + 1 bytes): its length, or 0
 * for a bad n or name. The info file is the slot name and TRASH_INFO_EXT. */
uint32_t trash_slot_name(const char *name, uint32_t len, uint32_t n, char *out);

/* ---- Restore (rule SL) ---- */

enum trash_kind { TRASH_K_NONE, TRASH_K_DIR, TRASH_K_FILE, TRASH_K_LINK, TRASH_K_ERROR };

/* The caller's lstat: the kind of path (NUL-terminated) itself; a link is
 * TRASH_K_LINK, never its target; TRASH_K_ERROR for any other failure. */
typedef enum trash_kind (*trash_lstat_fn)(void *ctx, const char *path);

enum trash_restore {
    TR_OK,                  /* every folder there, the name free */
    TR_MAKE_PARENTS,        /* folders from *at on are missing: made again */
    TR_PARENT_NOT_FOLDER,   /* the prefix of *at bytes is a link or a file */
    TR_EXISTS,              /* the name is used: Keep both or Cancel */
    TR_ERROR                /* the lstat failed (*at: the prefix) */
};

/* path: a decoded path that passed trash_path_check (len bytes); scratch:
 * TRASH_PATH_MAX + 1 bytes for the prefixes. *at is a prefix length of path
 * (the first missing folder, the folder that is not one, or the failing
 * prefix); path's length for TR_OK and TR_EXISTS. */
enum trash_restore trash_restore_check(const char *path, uint32_t len, trash_lstat_fn lstat, void *ctx,
                                       char *scratch, uint32_t *at);

#endif
