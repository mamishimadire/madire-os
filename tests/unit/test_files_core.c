/* Unit tests for the Files app's pure part, kernel/files/files_core.c (1.5
 * stage 11; docs/plans/1.5.md, C5, C6c rule SL, the S11 row): the named
 * cases files_history_back_forward_up, files_history_limit_64,
 * files_history_missing_folder, files_pathbar_parts_and_clip and the five
 * link cases (files_copy_symlink_not_followed,
 * files_delete_symlink_not_followed, files_empty_trash_symlink_not_followed,
 * files_paste_target_through_symlink, files_move_symlink_as_link), and the
 * folder, its limit and sorting, the column texts (the date as the
 * console's shell_date), the clipboard, the names for Paste and Restore,
 * and the recursive steps (chunks, depth 64, long paths, a failed copy).
 * File access through tests/files_fakefs.h. Each literal was checked by
 * hand (T4) and against tests/files_ref.py. */
#include <stdint.h>
#include <string.h>
#include "kernel/files/files_core.c"
#include "kernel/shell/shell.h"
#include "tests/files_fakefs.h"
#include "tests/unit/unit.h"

static struct files_entry entries[FILES_MAX_ENTRIES];
static char names[FILES_MAX_ENTRIES * (FILES_NAME_MAX + 1)];
static char hstore[FILES_HISTORY_BYTES];
static char cstore[FILES_CLIP_BYTES];
static struct files_history hist;
static struct files_walk walk;
static char scratch[FILES_PATH_MAX + 1];

static int is_cur(enum files_view v, const char *path)
{
    enum files_view cv;
    const char *cp;
    uint32_t cl;
    if (!files_history_current(&hist, &cv, &cp, &cl))
        return 0;
    return cv == v && cl == strlen(path) && memcmp(cp, path, cl) == 0;
}

static int go(const char *path)
{
    return files_history_go(&hist, FILES_VIEW_FOLDER, path, (uint32_t)strlen(path));
}

/* ---- The folder ---- */

static enum files_add add(struct files_folder *f, const char *name, enum files_kind k)
{
    return files_folder_add(f, name, (uint32_t)strlen(name), k, 0, 0);
}

static void files_folder_add_and_limit(void)
{
    struct files_folder f;
    char name[300];
    CHECK_EQ(files_folder_bytes(FILES_MAX_ENTRIES), 280000);
    CHECK_EQ(sizeof(entries) + sizeof(names), 280000);
    files_folder_init(&f, entries, FILES_MAX_ENTRIES, names, sizeof(names), false);
    CHECK_EQ(add(&f, ".", FILES_K_DIR), FILES_ADD_SKIPPED);
    CHECK_EQ(add(&f, "..", FILES_K_DIR), FILES_ADD_SKIPPED);
    CHECK_EQ(add(&f, ".hidden", FILES_K_FILE), FILES_ADD_SKIPPED);
    CHECK_EQ(add(&f, "a/b", FILES_K_FILE), FILES_ADD_BAD);
    CHECK_EQ(add(&f, "", FILES_K_FILE), FILES_ADD_BAD);
    CHECK_EQ(add(&f, "x", FILES_K_NONE), FILES_ADD_BAD);
    CHECK_EQ(add(&f, "x", FILES_K_ERROR), FILES_ADD_BAD);
    memset(name, 'n', 256);
    name[256] = 0;
    CHECK_EQ(add(&f, name, FILES_K_FILE), FILES_ADD_BAD);
    CHECK_EQ(files_folder_add(&f, "a\0b", 3, FILES_K_FILE, 0, 0), FILES_ADD_BAD);
    CHECK_EQ(f.n, 0);
    CHECK_EQ(files_folder_add(&f, "caf\xc3\xa9", 5, FILES_K_FILE, 1234, 99), FILES_ADD_OK);
    CHECK_EQ(f.e[0].flags, FILES_F_ODD);
    CHECK_EQ(f.e[0].size, 1234);
    CHECK_EQ(f.e[0].mtime, 99);
    CHECK(strcmp(files_entry_name(&f, 0), "caf\xc3\xa9") == 0);
    CHECK(files_entry_name(&f, 1) == 0);
    for (int i = 1; i < 1000; i++) {
        sprintf(name, "f%04d", i);
        CHECK_EQ(add(&f, name, FILES_K_FILE), FILES_ADD_OK);
    }
    CHECK_EQ(f.n, 1000);
    CHECK(!f.truncated);
    CHECK_EQ(add(&f, ".late", FILES_K_FILE), FILES_ADD_SKIPPED);    /* hidden: never counted */
    CHECK(!f.truncated);
    CHECK_EQ(add(&f, "f1000", FILES_K_FILE), FILES_ADD_FULL);
    CHECK(f.truncated);
    CHECK_EQ(f.n, 1000);
    /* shown hidden files count */
    files_folder_init(&f, entries, 2, names, sizeof(names), true);
    CHECK_EQ(add(&f, ".a", FILES_K_FILE), FILES_ADD_OK);
    CHECK_EQ(f.e[0].flags, FILES_F_HIDDEN);
    CHECK_EQ(add(&f, "b", FILES_K_LINK), FILES_ADD_OK);
    CHECK_EQ(add(&f, ".c", FILES_K_FILE), FILES_ADD_FULL);
    /* a names store too small also ends the folder */
    files_folder_init(&f, entries, 10, names, 4, true);
    CHECK_EQ(add(&f, "abc", FILES_K_FILE), FILES_ADD_OK);
    CHECK_EQ(add(&f, "d", FILES_K_FILE), FILES_ADD_FULL);
}

static void files_sort_folders_first(void)
{
    static const char *const in[] = { "b", "A", "a", "B", "_x", "Z", "a.txt", "ln", "Readme", "README" };
    static const enum files_kind kinds[] = { FILES_K_FILE, FILES_K_DIR, FILES_K_FILE, FILES_K_DIR, FILES_K_FILE,
                                             FILES_K_FILE, FILES_K_FILE, FILES_K_LINK, FILES_K_FILE, FILES_K_FILE };
    static const char *const want[] = { "A", "B", "_x", "a", "a.txt", "b", "ln", "README", "Readme", "Z" };
    struct files_folder f;
    files_folder_init(&f, entries, FILES_MAX_ENTRIES, names, sizeof(names), true);
    for (int i = 0; i < 10; i++)
        add(&f, in[i], kinds[i]);
    files_folder_sort(&f);
    for (int i = 0; i < 10; i++)
        CHECK(strcmp(files_entry_name(&f, (uint32_t)i), want[i]) == 0);
    CHECK_EQ(f.e[0].kind, FILES_K_DIR);
    CHECK_EQ(f.e[6].kind, FILES_K_LINK);
    CHECK(files_name_compare("abc", 3, "abd", 3) < 0);
    CHECK(files_name_compare("ab", 2, "abc", 3) < 0);
    CHECK(files_name_compare("ABC", 3, "abc", 3) < 0);
    CHECK(files_name_compare("abc", 3, "abc", 3) == 0);
    CHECK(files_name_compare("\xff", 1, "z", 1) > 0);
    /* 1,000 entries in reverse order come out in order */
    files_folder_init(&f, entries, FILES_MAX_ENTRIES, names, sizeof(names), true);
    for (int i = 999; i >= 0; i--) {
        char name[8];
        sprintf(name, "%04d", i);
        add(&f, name, (i % 3) ? FILES_K_FILE : FILES_K_DIR);
    }
    files_folder_sort(&f);
    int ok = 1;
    for (uint32_t i = 1; i < f.n; i++)
        if (fc_entry_cmp(&f, i - 1, i) >= 0)
            ok = 0;
    CHECK(ok);
    CHECK(strcmp(files_entry_name(&f, 0), "0000") == 0);
    CHECK(strcmp(files_entry_name(&f, 334), "0001") == 0);          /* 334 folders first */
}

/* ---- The column texts ---- */

static void files_column_texts(void)
{
    char s[FILES_SIZE_TEXT], d[FILES_DATE_TEXT], sh[20];
    CHECK(strcmp(files_type_text(FILES_K_DIR, "a.txt", 5), "Folder") == 0);
    CHECK(strcmp(files_type_text(FILES_K_LINK, "a.txt", 5), "Link") == 0);
    CHECK(strcmp(files_type_text(FILES_K_FILE, "notes.TXT", 9), "Text file") == 0);
    CHECK(strcmp(files_type_text(FILES_K_FILE, "a.c", 3), "Text file") == 0);
    CHECK(strcmp(files_type_text(FILES_K_FILE, "x.json", 6), "Text file") == 0);
    CHECK(strcmp(files_type_text(FILES_K_FILE, "run.Sh", 6), "Text file") == 0);
    CHECK(strcmp(files_type_text(FILES_K_FILE, ".txt", 4), "File") == 0);
    CHECK(strcmp(files_type_text(FILES_K_FILE, "archive.tar", 11), "File") == 0);
    CHECK(strcmp(files_type_text(FILES_K_FILE, "x.", 2), "File") == 0);
    CHECK(strcmp(files_type_text(FILES_K_FILE, "Makefile", 8), "File") == 0);
    CHECK(strcmp(files_type_text(FILES_K_FILE, "a.cc", 4), "File") == 0);
    CHECK(strcmp(files_type_text(FILES_K_FILE, "a.txt.gz", 8), "File") == 0);
    static const struct { uint64_t b; const char *t; } sizes[] = {
        { 0, "0 B" }, { 1023, "1023 B" }, { 1024, "1.0 KiB" }, { 1536, "1.5 KiB" }, { 1048575, "1023.9 KiB" },
        { 1048576, "1.0 MiB" }, { 5905580032ull, "5.5 GiB" }, { UINT64_MAX, "17179869183.9 GiB" },
    };
    for (unsigned i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
        files_size_text(sizes[i].b, s);
        CHECK(strcmp(s, sizes[i].t) == 0);
    }
    static const struct { int64_t t; const char *text; } dates[] = {
        { 0, "1970-01-01 00:00" }, { -5, "1970-01-01 00:00" }, { 951782400, "2000-02-29 00:00" },
        { 1767225599, "2025-12-31 23:59" }, { 4102444800ll, "2100-01-01 00:00" },
        { FILES_DATE_MAX + 100, "9999-12-31 23:59" },
    };
    for (unsigned i = 0; i < sizeof(dates) / sizeof(dates[0]); i++) {
        files_date_text(dates[i].t, d);
        CHECK(strcmp(d, dates[i].text) == 0);
    }
    /* the same day and minute as the console's shell_date */
    static const int64_t same[] = { 0, 59, 86399, 951782400, 1767225599, 1795168830, 4102444800ll, 7258118400ll };
    for (unsigned i = 0; i < sizeof(same) / sizeof(same[0]); i++) {
        files_date_text(same[i], d);
        shell_date(same[i], sh);
        CHECK(memcmp(d, sh, 16) == 0);
    }
}

/* ---- The history ---- */

static char gone[4][64];

static bool exists(void *ctx, const char *path)
{
    (void)ctx;
    for (int i = 0; i < 4; i++)
        if (strcmp(gone[i], path) == 0)
            return false;
    return true;
}

static void files_history_back_forward_up(void)
{
    uint32_t removed;
    memset(gone, 0, sizeof(gone));
    files_history_init(&hist, hstore);
    CHECK(!files_history_can_back(&hist));
    CHECK(!files_history_can_forward(&hist));
    CHECK(!files_history_can_up(&hist));
    CHECK(!files_history_back(&hist, exists, 0, &removed));
    CHECK(go("/home/user"));
    CHECK(go("/home/user/Documents"));
    CHECK(go("/home/user/Documents/Work"));
    CHECK(go("/home/user/Documents/Work"));        /* the current place again: nothing added */
    CHECK_EQ(hist.count, 3);
    CHECK(files_history_can_back(&hist));
    CHECK(!files_history_can_forward(&hist));
    CHECK(files_history_back(&hist, exists, 0, &removed));
    CHECK(is_cur(FILES_VIEW_FOLDER, "/home/user/Documents"));
    CHECK(files_history_can_forward(&hist));
    CHECK(files_history_forward(&hist, exists, 0, &removed));
    CHECK(is_cur(FILES_VIEW_FOLDER, "/home/user/Documents/Work"));
    CHECK(!files_history_forward(&hist, exists, 0, &removed));
    CHECK(files_history_back(&hist, exists, 0, &removed));
    CHECK(files_history_back(&hist, exists, 0, &removed));
    CHECK(is_cur(FILES_VIEW_FOLDER, "/home/user"));
    CHECK(!files_history_can_back(&hist));
    /* a new move after Back drops the forward entries */
    CHECK(go("/tmp"));
    CHECK_EQ(hist.count, 2);
    CHECK(!files_history_can_forward(&hist));
    /* Up: the parent, then "/", where it is greyed */
    CHECK(files_history_up(&hist));
    CHECK(is_cur(FILES_VIEW_FOLDER, "/"));
    CHECK(!files_history_can_up(&hist));
    CHECK(!files_history_up(&hist));
    CHECK(go("/home/user/Documents/Work"));
    CHECK(files_history_up(&hist));
    CHECK(is_cur(FILES_VIEW_FOLDER, "/home/user/Documents"));
    /* Trash and This PC: no Up */
    CHECK(files_history_go(&hist, FILES_VIEW_TRASH, 0, 0));
    CHECK(is_cur(FILES_VIEW_TRASH, ""));
    CHECK(!files_history_can_up(&hist));
    CHECK(files_history_go(&hist, FILES_VIEW_THISPC, "ignored", 7));
    CHECK(is_cur(FILES_VIEW_THISPC, ""));
    CHECK(!files_history_can_up(&hist));
    CHECK(files_history_back(&hist, exists, 0, &removed));
    CHECK(is_cur(FILES_VIEW_TRASH, ""));
    /* refused moves change nothing */
    uint32_t count = hist.count;
    CHECK(!files_history_go(&hist, FILES_VIEW_FOLDER, "", 0));
    CHECK(!files_history_go(&hist, FILES_VIEW_FOLDER, scratch, FILES_PATH_MAX + 1));
    CHECK_EQ(hist.count, count);
}

static void files_history_limit_64(void)
{
    uint32_t removed;
    char p[32];
    memset(gone, 0, sizeof(gone));
    files_history_init(&hist, hstore);
    for (int i = 0; i < 65; i++) {
        sprintf(p, "/home/user/d%d", i);
        CHECK(go(p));
    }
    CHECK_EQ(hist.count, 64);
    CHECK_EQ(hist.pos, 63);
    CHECK(is_cur(FILES_VIEW_FOLDER, "/home/user/d64"));
    int backs = 0;
    while (files_history_back(&hist, exists, 0, &removed))
        backs++;
    CHECK_EQ(backs, 63);
    CHECK(is_cur(FILES_VIEW_FOLDER, "/home/user/d1"));      /* d0, the oldest, was dropped */
    /* the ring keeps working after it wrapped */
    for (int i = 0; i < 63; i++)
        CHECK(files_history_forward(&hist, exists, 0, &removed));
    CHECK(is_cur(FILES_VIEW_FOLDER, "/home/user/d64"));
    CHECK(go("/home/user/x"));
    CHECK_EQ(hist.count, 64);
    CHECK(files_history_back(&hist, exists, 0, &removed));
    CHECK(is_cur(FILES_VIEW_FOLDER, "/home/user/d64"));
    /* a path of 4,096 bytes fits */
    memset(scratch, 'p', FILES_PATH_MAX);
    scratch[0] = '/';
    CHECK(files_history_go(&hist, FILES_VIEW_FOLDER, scratch, FILES_PATH_MAX));
    enum files_view v;
    const char *cp;
    uint32_t cl;
    CHECK(files_history_current(&hist, &v, &cp, &cl));
    CHECK_EQ(cl, FILES_PATH_MAX);
    CHECK_EQ(cp[FILES_PATH_MAX], 0);
}

static void files_history_missing_folder(void)
{
    uint32_t removed;
    memset(gone, 0, sizeof(gone));
    files_history_init(&hist, hstore);
    go("/home/user/A");
    go("/home/user/B");
    go("/home/user/C");
    go("/home/user/D");
    strcpy(gone[0], "/home/user/B");
    strcpy(gone[1], "/home/user/C");
    CHECK(files_history_back(&hist, exists, 0, &removed));
    CHECK_EQ(removed, 2);
    CHECK(is_cur(FILES_VIEW_FOLDER, "/home/user/A"));
    CHECK_EQ(hist.count, 2);
    CHECK(files_history_forward(&hist, exists, 0, &removed));
    CHECK_EQ(removed, 0);
    CHECK(is_cur(FILES_VIEW_FOLDER, "/home/user/D"));
    /* forward skips too */
    memset(gone, 0, sizeof(gone));
    files_history_init(&hist, hstore);
    go("/home/user/A");
    go("/home/user/B");
    go("/home/user/C");
    files_history_back(&hist, exists, 0, &removed);
    files_history_back(&hist, exists, 0, &removed);
    strcpy(gone[0], "/home/user/B");
    CHECK(files_history_forward(&hist, exists, 0, &removed));
    CHECK_EQ(removed, 1);
    CHECK(is_cur(FILES_VIEW_FOLDER, "/home/user/C"));
    /* every entry behind gone: Back fails and the current entry stays */
    strcpy(gone[1], "/home/user/A");
    CHECK(!files_history_back(&hist, exists, 0, &removed));
    CHECK_EQ(removed, 1);
    CHECK(is_cur(FILES_VIEW_FOLDER, "/home/user/C"));
    CHECK_EQ(hist.count, 1);
    CHECK(!files_history_can_back(&hist));
    /* Trash and This PC always exist */
    files_history_go(&hist, FILES_VIEW_TRASH, 0, 0);
    go("/home/user/A");
    CHECK(files_history_back(&hist, exists, 0, &removed));
    CHECK_EQ(removed, 0);
    CHECK(is_cur(FILES_VIEW_TRASH, ""));
}

/* ---- The path bar ---- */

static int part_is(const struct files_pathbar *pb, uint32_t i, const char *text, uint32_t x, uint32_t target)
{
    return i < pb->n && pb->part[i].len == strlen(text) && memcmp(pb->part[i].text, text, pb->part[i].len) == 0 &&
           pb->part[i].x == x && pb->part[i].target == target;
}

static void files_pathbar_parts_and_clip(void)
{
    static struct files_pathbar pb;
    const char *p = "/home/user/Documents/Work";
    files_pathbar(FILES_VIEW_FOLDER, p, 25, 100, &pb);
    CHECK_EQ(pb.n, 3);
    CHECK(!pb.clipped);
    CHECK_EQ(pb.cells, 23);
    CHECK(part_is(&pb, 0, "Home", 0, 10));
    CHECK(part_is(&pb, 1, "Documents", 7, 20));
    CHECK(part_is(&pb, 2, "Work", 19, 25));
    files_pathbar(FILES_VIEW_FOLDER, p, 25, 23, &pb);       /* exactly fits */
    CHECK_EQ(pb.n, 3);
    CHECK(!pb.clipped);
    files_pathbar(FILES_VIEW_FOLDER, p, 25, 22, &pb);       /* "... > Documents > Work" */
    CHECK_EQ(pb.n, 2);
    CHECK(pb.clipped);
    CHECK_EQ(pb.cells, 22);
    CHECK(part_is(&pb, 0, "Documents", 6, 20));
    CHECK(part_is(&pb, 1, "Work", 18, 25));
    files_pathbar(FILES_VIEW_FOLDER, p, 25, 16, &pb);       /* "... > Work" */
    CHECK_EQ(pb.n, 1);
    CHECK(pb.clipped);
    CHECK_EQ(pb.cells, 10);
    CHECK(part_is(&pb, 0, "Work", 6, 25));
    files_pathbar(FILES_VIEW_FOLDER, p, 25, 5, &pb);        /* "...rk" */
    CHECK_EQ(pb.n, 1);
    CHECK_EQ(pb.cells, 5);
    CHECK(part_is(&pb, 0, "rk", 3, 25));
    files_pathbar(FILES_VIEW_FOLDER, p, 25, 3, &pb);
    CHECK_EQ(pb.n, 0);
    CHECK(pb.clipped);
    files_pathbar(FILES_VIEW_FOLDER, "/tmp/x", 6, 100, &pb);
    CHECK_EQ(pb.n, 3);
    CHECK_EQ(pb.cells, 11);
    CHECK(part_is(&pb, 0, "/", 0, 1));
    CHECK(part_is(&pb, 1, "tmp", 4, 4));
    CHECK(part_is(&pb, 2, "x", 10, 6));
    files_pathbar(FILES_VIEW_FOLDER, "/home/user", 10, 100, &pb);
    CHECK_EQ(pb.n, 1);
    CHECK(part_is(&pb, 0, "Home", 0, 10));
    files_pathbar(FILES_VIEW_FOLDER, "/home/username", 14, 100, &pb);
    CHECK_EQ(pb.n, 3);
    CHECK(part_is(&pb, 0, "/", 0, 1));
    CHECK(part_is(&pb, 2, "username", 11, 14));
    files_pathbar(FILES_VIEW_FOLDER, "/", 1, 100, &pb);
    CHECK_EQ(pb.n, 1);
    CHECK(part_is(&pb, 0, "/", 0, 1));
    files_pathbar(FILES_VIEW_TRASH, 0, 0, 100, &pb);
    CHECK_EQ(pb.n, 1);
    CHECK(part_is(&pb, 0, "Trash", 0, 0));
    files_pathbar(FILES_VIEW_THISPC, 0, 0, 5, &pb);
    CHECK_EQ(pb.n, 1);
    CHECK(pb.clipped);
    CHECK(part_is(&pb, 0, "PC", 3, 0));
    /* 40 parts: never more than FILES_PB_MAX shown, the first one dropped */
    char deep[200] = "/home/user";
    for (int i = 0; i < 40; i++)
        strcat(deep, "/d");
    files_pathbar(FILES_VIEW_FOLDER, deep, (uint32_t)strlen(deep), 1000, &pb);
    CHECK(pb.clipped);
    CHECK_EQ(pb.n, FILES_PB_MAX);
    CHECK(part_is(&pb, FILES_PB_MAX - 1, "d", 6 + 31 * 4, 90));
}

/* ---- The clipboard and the names ---- */

static void files_clipboard(void)
{
    struct files_clip c;
    char p[32];
    uint32_t len;
    files_clip_init(&c, cstore);
    CHECK(files_clip_empty(&c));
    files_clip_begin(&c, true);
    CHECK(c.cut);
    for (int i = 0; i < 64; i++) {
        sprintf(p, "/home/user/f%d", i);
        CHECK_EQ(files_clip_add(&c, p, (uint32_t)strlen(p)), FILES_CLIP_OK);
    }
    CHECK_EQ(files_clip_add(&c, "/home/user/g", 12), FILES_CLIP_FULL);
    CHECK(!files_clip_empty(&c));
    CHECK(strcmp(files_clip_path(&c, 63, &len), "/home/user/f63") == 0);
    CHECK_EQ(len, 14);
    CHECK(files_clip_path(&c, 64, &len) == 0);
    files_clip_begin(&c, false);
    CHECK(files_clip_empty(&c));
    CHECK(!c.cut);
    CHECK_EQ(files_clip_add(&c, scratch, FILES_PATH_MAX + 1), FILES_CLIP_TOO_LONG);
    CHECK_EQ(files_clip_add(&c, scratch, 0), FILES_CLIP_TOO_LONG);
    memset(scratch, 'x', FILES_PATH_MAX);
    for (int i = 0; i < 64; i++)            /* 64 paths of 4,096 bytes fill the store exactly */
        CHECK_EQ(files_clip_add(&c, scratch, FILES_PATH_MAX), FILES_CLIP_OK);
    CHECK_EQ(c.used, FILES_CLIP_BYTES);
    files_clip_clear(&c);
    CHECK(files_clip_empty(&c));
}

static const char *const taken_names[] = { "a.txt", "a 2.txt", "Folder", "a (restored).txt", 0 };

static bool taken(void *ctx, const char *name)
{
    (void)ctx;
    for (int i = 0; taken_names[i]; i++)
        if (strcmp(taken_names[i], name) == 0)
            return true;
    return false;
}

static bool all_taken(void *ctx, const char *name)
{
    (void)ctx;
    (void)name;
    return true;
}

static void files_names_for_paste_and_restore(void)
{
    char out[FILES_NAME_MAX + 1], name[300];
    CHECK_EQ(files_unique_name("a.txt", 5, false, FILES_VARIANT_NUMBER, taken, 0, out), 7);
    CHECK(strcmp(out, "a 3.txt") == 0);
    CHECK_EQ(files_unique_name("b.txt", 5, false, FILES_VARIANT_NUMBER, taken, 0, out), 5);
    CHECK(strcmp(out, "b.txt") == 0);
    CHECK_EQ(files_unique_name("Folder", 6, true, FILES_VARIANT_NUMBER, taken, 0, out), 8);
    CHECK(strcmp(out, "Folder 2") == 0);
    CHECK_EQ(files_unique_name("a.txt", 5, false, FILES_VARIANT_RESTORED, taken, 0, out), 18);
    CHECK(strcmp(out, "a (restored 2).txt") == 0);
    CHECK_EQ(files_unique_name("a.txt", 5, false, FILES_VARIANT_NUMBER, all_taken, 0, out), 0);
    CHECK_EQ(files_name_variant("a.b", 3, true, FILES_VARIANT_NUMBER, 2, out), 5);
    CHECK(strcmp(out, "a.b 2") == 0);
    CHECK_EQ(files_name_variant(".bashrc", 7, false, FILES_VARIANT_NUMBER, 2, out), 9);
    CHECK(strcmp(out, ".bashrc 2") == 0);
    CHECK_EQ(files_name_variant("a.txt", 5, false, FILES_VARIANT_RESTORED, 1, out), 16);
    CHECK(strcmp(out, "a (restored).txt") == 0);
    CHECK_EQ(files_name_variant("a.txt", 5, false, FILES_VARIANT_NUMBER, 999, out), 9);
    CHECK(strcmp(out, "a 999.txt") == 0);
    CHECK_EQ(files_name_variant("a.txt", 5, false, FILES_VARIANT_NUMBER, 1000, out), 0);
    CHECK_EQ(files_name_variant("a.txt", 5, false, FILES_VARIANT_NUMBER, 0, out), 0);
    memset(name, 'y', 255);
    CHECK_EQ(files_name_variant(name, 255, false, FILES_VARIANT_NUMBER, 2, out), 255);  /* the base cut */
    CHECK(memcmp(out + 251, "yy 2", 4) == 0 || memcmp(out + 251, "y 2", 3) == 0);
    CHECK(memcmp(out + 253, " 2", 2) == 0);
    memset(name, 'x', 250);
    memcpy(name + 250, ".txt", 4);
    CHECK_EQ(files_name_variant(name, 254, false, FILES_VARIANT_NUMBER, 999, out), 255);
    CHECK(memcmp(out + 245, "xx 999.txt", 10) == 0);
    memset(name, 'e', 255);              /* an extension too long for any suffix */
    name[1] = '.';
    CHECK_EQ(files_name_variant(name, 255, false, FILES_VARIANT_NUMBER, 2, out), 0);
}

/* ---- The recursive steps ---- */

static enum files_walk_result run_walk(enum files_walk_op op, const char *src, const char *dst)
{
    enum files_walk_result r = FILES_WALK_MORE;
    int steps = 0;
    files_walk_start(&walk, &ffs_fs, op, src, dst);
    while (steps++ < 1000000 && (r = files_walk_step(&walk)) == FILES_WALK_MORE)
        ;
    return r;
}

static int node(const char *path)
{
    int parent;
    return ffs_lookup(path, &parent);
}

static void files_walk_copy_file_chunks(void)
{
    ffs_reset();
    ffs_make("/src/a.bin", FILES_K_FILE, 10000, 9);
    ffs_make("/dst/x", FILES_K_DIR, 0, 0);
    files_walk_start(&walk, &ffs_fs, FILES_WALK_COPY, "/src/a.bin", "/dst/a.bin");
    CHECK_EQ(files_walk_step(&walk), FILES_WALK_MORE);     /* the start */
    CHECK_EQ(files_walk_step(&walk), FILES_WALK_MORE);     /* 4,096 */
    CHECK_EQ(files_walk_step(&walk), FILES_WALK_MORE);     /* 4,096 */
    CHECK_EQ(files_walk_step(&walk), FILES_WALK_MORE);     /* 1,808 */
    CHECK_EQ(walk.offset, 10000);
    CHECK_EQ(files_walk_step(&walk), FILES_WALK_DONE);     /* the end */
    CHECK_EQ(files_walk_step(&walk), FILES_WALK_DONE);     /* and again */
    CHECK_EQ(walk.done, 1);
    int n = node("/dst/a.bin");
    CHECK(n > 0 && ffs_node[n].size == 10000 && ffs_node[n].seed == 9);
    /* a folder tree */
    ffs_make("/src/t/e.txt", FILES_K_FILE, 0, 4);
    ffs_make("/src/t/u/v/w.txt", FILES_K_FILE, 5, 5);
    CHECK_EQ(run_walk(FILES_WALK_COPY, "/src/t", "/dst/t2"), FILES_WALK_DONE);
    CHECK_EQ(walk.done, 5);            /* e.txt, w.txt, v, u, t */
    CHECK(node("/dst/t2/e.txt") > 0 && ffs_node[node("/dst/t2/e.txt")].size == 0);
    CHECK(node("/dst/t2/u/v/w.txt") > 0 && ffs_node[node("/dst/t2/u/v/w.txt")].size == 5);
    CHECK(node("/src/t/u/v/w.txt") > 0);
    /* an existing target is refused by the filesystem */
    CHECK_EQ(run_walk(FILES_WALK_COPY, "/src/t", "/dst/t2"), FILES_WALK_ERROR);
    CHECK_EQ(walk.err, FFS_EEXIST);
}

static void files_walk_failed_copy_removed(void)
{
    ffs_reset();
    ffs_make("/src/a.bin", FILES_K_FILE, 10000, 9);
    ffs_make("/dst/x", FILES_K_DIR, 0, 0);
    ffs_fail_at = 3;                    /* the third chunk fails */
    CHECK_EQ(run_walk(FILES_WALK_COPY, "/src/a.bin", "/dst/a.bin"), FILES_WALK_ERROR);
    CHECK_EQ(walk.err, FFS_ENOMEM);
    CHECK_EQ(node("/dst/a.bin"), FFS_ENOENT);           /* the partial copy removed */
    CHECK_EQ(ffs_calls, 4);
    ffs_reset();
    ffs_make("/src/d/f", FILES_K_FILE, 1, 1);
    ffs_fail_at = 2;                    /* delete: the folder's removal fails */
    CHECK_EQ(run_walk(FILES_WALK_DELETE, "/src/d", 0), FILES_WALK_ERROR);
    CHECK_EQ(walk.err, FFS_ENOMEM);
    CHECK_EQ(walk.done, 1);
    CHECK(node("/src/d") > 0);
    CHECK_EQ(node("/src/d/f"), FFS_ENOENT);
}

static void files_walk_depth_64(void)
{
    char p[FILES_PATH_MAX + 1];
    /* a folder 64 below the start: allowed */
    ffs_reset();
    strcpy(p, "/src");
    for (int i = 0; i < 64; i++)
        sprintf(p + strlen(p), "/n%d", i);
    strcat(p, "/f.txt");
    ffs_make(p, FILES_K_FILE, 3, 3);
    CHECK_EQ(run_walk(FILES_WALK_COPY, "/src", "/copy"), FILES_WALK_DONE);
    CHECK_EQ(walk.done, 66);            /* 65 folders and the file */
    CHECK_EQ(run_walk(FILES_WALK_DELETE, "/src", 0), FILES_WALK_DONE);
    CHECK_EQ(walk.done, 66);
    CHECK_EQ(node("/src"), FFS_ENOENT);
    /* 65 below: stopped before anything is done in it */
    ffs_reset();
    strcpy(p, "/src");
    for (int i = 0; i < 65; i++)
        sprintf(p + strlen(p), "/n%d", i);
    strcat(p, "/f.txt");
    ffs_make(p, FILES_K_FILE, 3, 3);
    ffs_make("/src/a.txt", FILES_K_FILE, 3, 3);
    int before = ffs_count;
    CHECK_EQ(run_walk(FILES_WALK_DELETE, "/src", 0), FILES_WALK_DEEP);
    CHECK_EQ(walk.done, 0);
    CHECK(node(p) > 0);                 /* nothing removed: the walk goes down first */
    CHECK_EQ(walk.depth, 65);
    CHECK_EQ(run_walk(FILES_WALK_COPY, "/src", "/copy"), FILES_WALK_DEEP);
    CHECK_EQ(walk.done, 0);
    CHECK_EQ(ffs_count, before + 65);   /* /copy and n0 to n63 made (n63 is 64 below), nothing in n63's copy */
    CHECK_EQ(node("/copy/a.txt"), FFS_ENOENT);
}

static void files_walk_too_long(void)
{
    char p[FILES_PATH_MAX + 300], name[260];
    ffs_reset();
    memset(name, 'L', 250);
    name[250] = 0;
    strcpy(p, "/s");
    for (int i = 0; i < 16; i++) {      /* 2 + 16 x 251 = 4,018 bytes */
        strcat(p, "/");
        strcat(p, name);
    }
    ffs_make(p, FILES_K_DIR, 0, 0);
    strcat(p, "/");
    strcat(p, name);                    /* 4,269: only through the walk */
    int last = ffs_add(node("/s") >= 0 ? ffs_count - 1 : 0, FILES_K_FILE, name, 1, 1);
    CHECK(last > 0);
    CHECK_EQ(run_walk(FILES_WALK_DELETE, "/s", 0), FILES_WALK_TOO_LONG);
    CHECK_EQ(walk.done, 0);
    CHECK_EQ(run_walk(FILES_WALK_COPY, "/s", "/a-longer-copy-name"), FILES_WALK_TOO_LONG);
    CHECK_EQ(files_walk_start(&walk, &ffs_fs, FILES_WALK_DELETE, p, 0), false);
    CHECK_EQ(walk.result, FILES_WALK_TOO_LONG);
    CHECK_EQ(files_walk_start(&walk, &ffs_fs, FILES_WALK_DELETE, "/", 0), false);
    CHECK_EQ(walk.result, FILES_WALK_ERROR);
    CHECK_EQ(files_walk_start(&walk, &ffs_fs, FILES_WALK_COPY, "/s", "rel"), false);
    CHECK_EQ(run_walk(FILES_WALK_DELETE, "/missing", 0), FILES_WALK_ERROR);
}

/* The link cases (rule SL). By convention each link "points" at /outside,
 * which holds a folder and a file that must never change. */
static void make_outside(void)
{
    ffs_make("/outside/secret/keep.txt", FILES_K_FILE, 100, 7);
}

static int outside_intact(void)
{
    int n = node("/outside/secret/keep.txt");
    return node("/outside/secret") > 0 && n > 0 && ffs_node[n].size == 100;
}

static void files_copy_symlink_not_followed(void)
{
    ffs_reset();
    make_outside();
    ffs_make("/src/f", FILES_K_FILE, 5000, 9);
    ffs_make("/src/ln", FILES_K_LINK, 0, 0);
    ffs_make("/src/sub/ln2", FILES_K_LINK, 0, 0);
    ffs_make("/dst/x", FILES_K_DIR, 0, 0);
    int before = ffs_count;
    CHECK_EQ(run_walk(FILES_WALK_COPY, "/src/ln", "/dst/ln"), FILES_WALK_LINK);
    CHECK(strcmp(walk.link_name, "ln") == 0);
    CHECK_EQ(walk.done, 0);
    CHECK_EQ(ffs_count, before);        /* nothing made */
    CHECK_EQ(run_walk(FILES_WALK_COPY, "/src", "/dst/copy"), FILES_WALK_DONE);
    CHECK_EQ(walk.links_skipped, 2);
    CHECK(strcmp(walk.link_name, "ln") == 0);
    CHECK_EQ(walk.done, 3);             /* f, sub, src */
    CHECK(node("/dst/copy/f") > 0 && ffs_node[node("/dst/copy/f")].size == 5000);
    CHECK_EQ(node("/dst/copy/ln"), FFS_ENOENT);
    CHECK_EQ(node("/dst/copy/sub/ln2"), FFS_ENOENT);
    CHECK_EQ(ffs_lstat(0, "/src/ln"), FILES_K_LINK);    /* the links left where they are */
    CHECK(outside_intact());
    CHECK_EQ(files_move_how(FILES_K_LINK, false), FILES_MOVE_REFUSE_LINK);
}

static void files_delete_symlink_not_followed(void)
{
    ffs_reset();
    make_outside();
    ffs_make("/a/ln", FILES_K_LINK, 0, 0);
    ffs_make("/a/b/ln", FILES_K_LINK, 0, 0);
    ffs_make("/a/b/file", FILES_K_FILE, 3, 3);
    ffs_make("/b/ln2", FILES_K_LINK, 0, 0);
    CHECK_EQ(run_walk(FILES_WALK_DELETE, "/b/ln2", 0), FILES_WALK_DONE);
    CHECK_EQ(walk.done, 1);
    CHECK_EQ(node("/b/ln2"), FFS_ENOENT);
    CHECK(node("/b") > 0);
    CHECK_EQ(run_walk(FILES_WALK_DELETE, "/a", 0), FILES_WALK_DONE);
    CHECK_EQ(walk.done, 5);
    CHECK_EQ(node("/a"), FFS_ENOENT);
    CHECK(outside_intact());
    /* a path through a link answers an error and is never walked */
    CHECK_EQ(run_walk(FILES_WALK_DELETE, "/c/ln/secret", 0), FILES_WALK_ERROR);
    ffs_make("/c/ln", FILES_K_LINK, 0, 0);
    CHECK_EQ(run_walk(FILES_WALK_DELETE, "/c/ln/secret", 0), FILES_WALK_ERROR);
    CHECK(outside_intact());
}

static void files_empty_trash_symlink_not_followed(void)
{
    static const char *const tf = "/home/user/.local/share/Trash/files";
    char path[FILES_PATH_MAX + 1], name[FILES_NAME_MAX + 1];
    ffs_reset();
    make_outside();
    ffs_make("/home/user/.local/share/Trash/files/x/ln", FILES_K_LINK, 0, 0);
    ffs_make("/home/user/.local/share/Trash/files/x/y.txt", FILES_K_FILE, 9, 9);
    ffs_make("/home/user/.local/share/Trash/files/ln2", FILES_K_LINK, 0, 0);
    ffs_make("/home/user/.local/share/Trash/info/x.trashinfo", FILES_K_FILE, 40, 1);
    /* Empty Trash: each entry of files/ deleted as a walk of its own */
    int walks = 0;
    for (;;) {
        uint64_t cookie = 0;
        if (ffs_read_dir(0, tf, &cookie, name) != 1)
            break;
        snprintf(path, sizeof(path), "%s/%s", tf, name);
        CHECK_EQ(run_walk(FILES_WALK_DELETE, path, 0), FILES_WALK_DONE);
        if (++walks > 4)
            break;
    }
    CHECK_EQ(walks, 2);
    CHECK(node(tf) > 0);
    CHECK(outside_intact());
    CHECK(node("/home/user/.local/share/Trash/info/x.trashinfo") > 0);
}

static void files_paste_target_through_symlink(void)
{
    uint32_t at;
    ffs_reset();
    make_outside();
    ffs_make("/home/user/docs/a.txt", FILES_K_FILE, 1, 1);
    ffs_make("/home/user/ln", FILES_K_LINK, 0, 0);
    CHECK_EQ(files_check_target(&ffs_fs, "/home/user/ln/sub", scratch, &at), FILES_TARGET_NOT_FOLDER);
    CHECK_EQ(at, 13);
    CHECK_EQ(files_check_target(&ffs_fs, "/home/user/ln", scratch, &at), FILES_TARGET_NOT_FOLDER);
    CHECK_EQ(at, 13);
    CHECK_EQ(files_check_target(&ffs_fs, "/home/user/docs", scratch, &at), FILES_TARGET_OK);
    CHECK_EQ(at, 15);
    CHECK_EQ(files_check_target(&ffs_fs, "/home/user/docs/a.txt", scratch, &at), FILES_TARGET_NOT_FOLDER);
    CHECK_EQ(at, 21);
    CHECK_EQ(files_check_target(&ffs_fs, "/home/user/nope", scratch, &at), FILES_TARGET_MISSING);
    CHECK_EQ(at, 15);
    CHECK_EQ(files_check_target(&ffs_fs, "/", scratch, &at), FILES_TARGET_OK);
    CHECK_EQ(files_check_target(&ffs_fs, "rel", scratch, &at), FILES_TARGET_ERROR);
    CHECK(outside_intact());
}

static void files_move_symlink_as_link(void)
{
    CHECK_EQ(files_move_how(FILES_K_LINK, true), FILES_MOVE_RENAME);   /* the link itself renamed */
    CHECK_EQ(files_move_how(FILES_K_LINK, false), FILES_MOVE_REFUSE_LINK);
    CHECK_EQ(files_move_how(FILES_K_FILE, false), FILES_MOVE_COPY_DELETE);
    CHECK_EQ(files_move_how(FILES_K_DIR, false), FILES_MOVE_COPY_DELETE);
    CHECK_EQ(files_move_how(FILES_K_DIR, true), FILES_MOVE_RENAME);
}

int main(void)
{
    printf("files_core unit tests\n");
    RUN(files_folder_add_and_limit);
    RUN(files_sort_folders_first);
    RUN(files_column_texts);
    RUN(files_history_back_forward_up);
    RUN(files_history_limit_64);
    RUN(files_history_missing_folder);
    RUN(files_pathbar_parts_and_clip);
    RUN(files_clipboard);
    RUN(files_names_for_paste_and_restore);
    RUN(files_walk_copy_file_chunks);
    RUN(files_walk_failed_copy_removed);
    RUN(files_walk_depth_64);
    RUN(files_walk_too_long);
    RUN(files_copy_symlink_not_followed);
    RUN(files_delete_symlink_not_followed);
    RUN(files_empty_trash_symlink_not_followed);
    RUN(files_paste_target_through_symlink);
    RUN(files_move_symlink_as_link);
    return unit_report("files_core");
}
