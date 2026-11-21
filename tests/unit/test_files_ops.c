/* Unit tests for the Files actions' pure part, kernel/files/files_ops.c
 * (1.5 stage 13; docs/plans/1.5.md, C5, C6, C6b, C6c, note (13) and the S13
 * decisions D1-D5 and Q1-Q9): which items may go to the Trash (Q7), Paste
 * into itself, a path's folder and name, the Trash view's Original
 * location (the path bar's words, clipped at the start), the DeletionDate
 * of a time, the question panel's texts word for word, and each Files
 * menu's greyed rows. Each literal was worked out by hand (T4). */
#include <stdint.h>
#include <string.h>
#include "kernel/files/files_core.c"
#include "kernel/files/trash_core.c"
#include "kernel/files/files_ops.c"
#include "tests/unit/unit.h"

static bool trashable(const char *p)
{
    return files_ops_trashable(p, (uint32_t)strlen(p));
}

static bool inside(const char *src, const char *dst)
{
    return files_ops_into_itself(src, (uint32_t)strlen(src), dst, (uint32_t)strlen(dst));
}

static void fo_trashable(void)
{
    CHECK(trashable("/home/user/a.txt"));
    CHECK(trashable("/home/user/Documents/Work"));
    CHECK(!trashable("/home/user"));
    CHECK(!trashable("/home/user/"));
    CHECK(!trashable("/home/users/x"));
    CHECK(!trashable("/home"));
    CHECK(!trashable("/tmp/x"));
    CHECK(!trashable("/"));
    CHECK(!trashable(TRASH_DIR));
    CHECK(!trashable(TRASH_DIR "/files"));
    CHECK(!trashable(TRASH_DIR "/files/a.txt"));
    CHECK(!trashable(TRASH_DIR "/info/a.txt.trashinfo"));
    CHECK(!trashable("/home/user/.local"));             /* it holds the Trash */
    CHECK(!trashable("/home/user/.local/share"));
    CHECK(trashable("/home/user/.localx"));
    CHECK(trashable("/home/user/.local/sharex"));
    CHECK(trashable("/home/user/.local/share/Trashy"));
    CHECK(trashable("/home/user/.local/other"));
}

static void fo_into_itself(void)
{
    CHECK(inside("/a", "/a"));
    CHECK(inside("/a", "/a/b"));
    CHECK(inside("/a", "/a/b/c"));
    CHECK(!inside("/a", "/ab"));
    CHECK(!inside("/a/b", "/a"));
    CHECK(!inside("/home/user/Docs", "/home/user/Documents"));
    CHECK(inside("/", "/x"));
    CHECK(!inside("", "/x"));
}

static void fo_parent_and_name(void)
{
    CHECK_EQ(files_ops_parent_len("/a/b", 4), 2);
    CHECK_EQ(files_ops_parent_len("/a", 2), 1);
    CHECK_EQ(files_ops_parent_len("/", 1), 0);
    CHECK_EQ(files_ops_parent_len("", 0), 0);
    CHECK_EQ(files_ops_parent_len("/home/user/x.txt", 16), 10);
    CHECK_EQ(files_ops_name_off("/a/b", 4), 3);
    CHECK_EQ(files_ops_name_off("/a", 2), 1);
    CHECK_EQ(files_ops_name_off("/home/user/x.txt", 16), 11);
}

static void fo_location(void)
{
    char out[48];
    CHECK_EQ(files_ops_location("/home/user/Documents", 20, 22, out, sizeof(out)), 16);
    CHECK(strcmp(out, "Home > Documents") == 0);
    CHECK_EQ(files_ops_location("/home/user", 10, 22, out, sizeof(out)), 4);
    CHECK(strcmp(out, "Home") == 0);
    CHECK_EQ(files_ops_location("/tmp", 4, 22, out, sizeof(out)), 7);
    CHECK(strcmp(out, "/ > tmp") == 0);
    /* "Home > Documents > Work > Projects > 2026" is 41 cells: clipped at
     * the start to the last parts that fit 22 cells. */
    CHECK_EQ(files_ops_location("/home/user/Documents/Work/Projects/2026", 39, 22, out, sizeof(out)), 21);
    CHECK(strcmp(out, "... > Projects > 2026") == 0);
    CHECK_EQ(files_ops_location("/home/user/caf\xe9", 15, 22, out, sizeof(out)), 11);
    CHECK(strcmp(out, "Home > caf?") == 0);
    /* The output's size bounds the cells too. */
    char small[8];
    CHECK(files_ops_location("/home/user/Documents", 20, 22, small, sizeof(small)) <= 7);
    CHECK(strlen(small) <= 7);
}

static void fo_date(void)
{
    struct trash_date d;
    files_ops_date(0, &d);
    CHECK(d.year == 1970 && d.month == 1 && d.day == 1 && d.hour == 0 && d.minute == 0 && d.second == 0);
    files_ops_date(-5, &d);
    CHECK(d.year == 1970 && d.month == 1 && d.day == 1 && d.hour == 0 && d.minute == 0 && d.second == 0);
    files_ops_date(86399, &d);
    CHECK(d.year == 1970 && d.month == 1 && d.day == 1 && d.hour == 23 && d.minute == 59 && d.second == 59);
    files_ops_date(951782400, &d);                      /* 2000-02-29 00:00:00 */
    CHECK(d.year == 2000 && d.month == 2 && d.day == 29 && d.hour == 0 && d.minute == 0 && d.second == 0);
    files_ops_date(1709251199, &d);                     /* 2024-02-29 23:59:59 */
    CHECK(d.year == 2024 && d.month == 2 && d.day == 29 && d.hour == 23 && d.minute == 59 && d.second == 59);
    files_ops_date(1795196847, &d);                     /* 2026-11-20 17:47:27 */
    CHECK(d.year == 2026 && d.month == 11 && d.day == 20 && d.hour == 17 && d.minute == 47 && d.second == 27);
    CHECK(trash_date_valid(&d));
    files_ops_date(FILES_DATE_MAX, &d);
    CHECK(d.year == 9999 && d.month == 12 && d.day == 31 && d.hour == 23 && d.minute == 59 && d.second == 59);
    files_ops_date(1000000000000000ll, &d);
    CHECK(d.year == 9999 && d.month == 12 && d.day == 31 && d.hour == 23 && d.minute == 59 && d.second == 59);
}

static void fo_questions(void)
{
    char a[FILES_OPS_LINE_MAX], b[FILES_OPS_LINE_MAX], l[FILES_OPS_LABEL_MAX];
    files_ops_question(FILES_Q_DELETE_ONE, "a.txt", 1, 0, a, b, l);
    CHECK(strcmp(a, "Delete 'a.txt' permanently?") == 0 && b[0] == 0 && strcmp(l, "Delete") == 0);
    files_ops_question(FILES_Q_NOTRASH_ONE, "t.txt", 1, 0, a, b, l);
    CHECK(strcmp(a, "Delete 't.txt' permanently?") == 0);
    CHECK(strcmp(b, "It can't go to the Trash.") == 0 && strcmp(l, "Delete") == 0);
    files_ops_question(FILES_Q_DELETE_MANY, "x", 3, 0, a, b, l);
    CHECK(strcmp(a, "Delete 3 items permanently?") == 0 && b[0] == 0);
    files_ops_question(FILES_Q_NOTRASH_MANY, "x", 12, 0, a, b, l);
    CHECK(strcmp(a, "Delete 12 items permanently?") == 0 && strcmp(b, "They can't go to the Trash.") == 0);
    files_ops_question(FILES_Q_EMPTY, 0, 1, 0, a, b, l);
    CHECK(strcmp(a, "Empty the Trash?") == 0 && strcmp(b, "1 item will be deleted permanently.") == 0);
    CHECK(strcmp(l, "Empty Trash") == 0);
    files_ops_question(FILES_Q_EMPTY, 0, 5, 0, a, b, l);
    CHECK(strcmp(b, "5 items will be deleted permanently.") == 0);
    files_ops_question(FILES_Q_EXISTS, "a.txt", 0, "Home > Documents", a, b, l);
    CHECK(strcmp(a, "'a.txt' already exists in Home > Documents.") == 0 && b[0] == 0);
    CHECK(strcmp(l, "Keep both") == 0);
    /* A long name: its first 37 bytes and "..."; odd bytes as '?'. */
    char longname[64];
    memset(longname, 'x', 50);
    longname[50] = 0;
    files_ops_question(FILES_Q_DELETE_ONE, longname, 1, 0, a, b, l);
    CHECK(strcmp(a, "Delete 'xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx...' permanently?") == 0);
    files_ops_question(FILES_Q_DELETE_ONE, "caf\xe9\x01", 1, 0, a, b, l);
    CHECK(strcmp(a, "Delete 'caf?" "?' permanently?") == 0);     /* split: no trigraph sequence */
}

static void fo_deep_text(void)
{
    char t[FILES_OPS_LINE_MAX];
    files_ops_deep_text(0, t);
    CHECK(strcmp(t, "This folder is nested too deeply (more than 64 levels) (0 items done).") == 0);
    files_ops_deep_text(1, t);
    CHECK(strcmp(t, "This folder is nested too deeply (more than 64 levels) (1 item done).") == 0);
    files_ops_deep_text(65, t);
    CHECK(strcmp(t, "This folder is nested too deeply (more than 64 levels) (65 items done).") == 0);
    files_ops_deep_text(4294967295u, t);                /* the longest: it fits the line */
    CHECK(strcmp(t, "This folder is nested too deeply (more than 64 levels) (4294967295 items done).") == 0);
}

static void fo_greyed(void)
{
    struct files_ops_state st = { 1, 0, 0, true, true, true };
    CHECK_EQ(files_ops_greyed(FILES_OPS_MENU_ITEM, &st), 0);
    st.selected = 0;            /* Open, Cut, Copy, Rename, Move to Trash, Properties */
    CHECK_EQ(files_ops_greyed(FILES_OPS_MENU_ITEM, &st), 0x77);
    st.selected = 2;            /* Open, Rename, Properties */
    CHECK_EQ(files_ops_greyed(FILES_OPS_MENU_ITEM, &st), 0x51);
    st = (struct files_ops_state){ 1, 0, 0, false, true, false };   /* read-only: Cut, Paste, Rename, Trash */
    CHECK_EQ(files_ops_greyed(FILES_OPS_MENU_ITEM, &st), 0x3A);
    st = (struct files_ops_state){ 1, 0, 0, true, false, true };    /* no clipboard: Paste */
    CHECK_EQ(files_ops_greyed(FILES_OPS_MENU_ITEM, &st), 0x08);
    st = (struct files_ops_state){ 0, 0, 0, true, true, true };
    CHECK_EQ(files_ops_greyed(FILES_OPS_MENU_SPACE, &st), 0);
    st.writable = false;        /* New Folder, New File, Paste; never Show hidden files */
    CHECK_EQ(files_ops_greyed(FILES_OPS_MENU_SPACE, &st), 0x07);
    st = (struct files_ops_state){ 0, 0, 0, true, false, true };
    CHECK_EQ(files_ops_greyed(FILES_OPS_MENU_SPACE, &st), 0x04);
    st = (struct files_ops_state){ 1, 1, 3, true, false, true };
    CHECK_EQ(files_ops_greyed(FILES_OPS_MENU_TRASH_ITEM, &st), 0);
    st.restorable = 0;          /* an unknown original location: Restore */
    CHECK_EQ(files_ops_greyed(FILES_OPS_MENU_TRASH_ITEM, &st), 0x01);
    st.writable = false;        /* /home read-only: Restore, Delete permanently */
    CHECK_EQ(files_ops_greyed(FILES_OPS_MENU_TRASH_ITEM, &st), 0x03);
    st = (struct files_ops_state){ 2, 2, 3, true, false, true };     /* two: Properties */
    CHECK_EQ(files_ops_greyed(FILES_OPS_MENU_TRASH_ITEM, &st), 0x04);
    st = (struct files_ops_state){ 0, 0, 3, true, false, true };
    CHECK_EQ(files_ops_greyed(FILES_OPS_MENU_TRASH_SPACE, &st), 0);
    st.in_trash = 0;            /* an empty Trash: Restore all, Empty Trash */
    CHECK_EQ(files_ops_greyed(FILES_OPS_MENU_TRASH_SPACE, &st), 0x03);
    st = (struct files_ops_state){ 0, 0, 3, false, false, true };
    CHECK_EQ(files_ops_greyed(FILES_OPS_MENU_TRASH_SPACE, &st), 0x03);
}

int main(void)
{
    printf("files_ops unit tests\n");
    RUN(fo_trashable);
    RUN(fo_into_itself);
    RUN(fo_parent_and_name);
    RUN(fo_location);
    RUN(fo_date);
    RUN(fo_questions);
    RUN(fo_deep_text);
    RUN(fo_greyed);
    return unit_report("files_ops");
}
