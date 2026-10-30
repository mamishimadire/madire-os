/* Unit tests for kernel/console/conskeys.c (the console keys, 1.3 stage
 * 7b, decision 16): which key an event is (presses with Ctrl only, either
 * case, not with Alt, never Ctrl+Y, Ctrl+D or Ctrl+C); what each key does
 * at the prompt, scrolled back, at a yes/no question and while a program
 * runs; every edit at the start, the middle and the end of the line and on
 * an empty line; Ctrl+W after several spaces, at the start and on a single
 * word. The line is typed through the line editor (lineedit.c, unchanged),
 * which still ignores these keys. The module is included directly. */
#include <string.h>
#include "tests/unit/unit.h"
#include "kernel/console/conskeys.c"

static struct key_event ctrl(char c)
{
    return (struct key_event){ KEY_CHAR, c, KEY_PRESSED | KEY_CTRL, 0 };
}

/* The line `text` typed, then the cursor moved to `pos`. */
static void typed(struct lineedit *le, const char *text, uint8_t pos)
{
    lineedit_reset(le);
    for (const char *p = text; *p; p++) {
        struct key_event e = { *p == ' ' ? KEY_SPACE : KEY_CHAR, *p, KEY_PRESSED, 0 };
        lineedit_key(le, &e);
    }
    struct key_event left = { KEY_LEFT, 0, KEY_PRESSED, 0 };
    while (le->pos > pos)
        lineedit_key(le, &left);
}

/* Key k on `text` with the cursor at `pos`: the change, the line and the cursor. */
static int edits(const char *text, uint8_t pos, enum conskey k, bool changed, const char *want, uint8_t want_pos)
{
    struct lineedit le = { 0 };
    typed(&le, text, pos);
    bool got = conskeys_edit(&le, k);
    if (got != changed || strcmp(le.buf, want) != 0 || le.pos != want_pos || (size_t)le.len != strlen(want)) {
        printf("    '%s' at %u, key %d: got %d '%s' at %u (len %u), expected %d '%s' at %u\n", text, pos, (int)k,
               got, le.buf, le.pos, le.len, changed, want, want_pos);
        return 0;
    }
    return 1;
}

static void test_classify(void)
{
    struct key_event e = ctrl('l');
    CHECK_EQ(conskeys_classify(&e), CONSKEY_CLEAR);
    e = ctrl('a');
    CHECK_EQ(conskeys_classify(&e), CONSKEY_START);
    e = ctrl('e');
    CHECK_EQ(conskeys_classify(&e), CONSKEY_END);
    e = ctrl('k');
    CHECK_EQ(conskeys_classify(&e), CONSKEY_KILL_END);
    e = ctrl('u');
    CHECK_EQ(conskeys_classify(&e), CONSKEY_KILL_START);
    e = ctrl('w');
    CHECK_EQ(conskeys_classify(&e), CONSKEY_WORD);
    e = ctrl('W');                                  /* Shift or Caps Lock */
    CHECK_EQ(conskeys_classify(&e), CONSKEY_WORD);
    e = ctrl(12);                                   /* a control code (as Ctrl+C's 3) */
    CHECK_EQ(conskeys_classify(&e), CONSKEY_CLEAR);
    e = ctrl('y');                                  /* no Ctrl+Y */
    CHECK_EQ(conskeys_classify(&e), CONSKEY_NONE);
    e = ctrl('d');                                  /* Ctrl+D deferred */
    CHECK_EQ(conskeys_classify(&e), CONSKEY_NONE);
    e = ctrl('c');                                  /* Ctrl+C is the console's own */
    CHECK_EQ(conskeys_classify(&e), CONSKEY_NONE);
    e = ctrl('l');
    e.flags = KEY_CTRL;                             /* a release */
    CHECK_EQ(conskeys_classify(&e), CONSKEY_NONE);
    e = ctrl('l');
    e.flags |= KEY_ALT;                             /* Ctrl+Alt */
    CHECK_EQ(conskeys_classify(&e), CONSKEY_NONE);
    e = (struct key_event){ KEY_CHAR, 'l', KEY_PRESSED, 0 };       /* no Ctrl: typed */
    CHECK_EQ(conskeys_classify(&e), CONSKEY_NONE);
    e = (struct key_event){ KEY_HOME, 0, KEY_PRESSED | KEY_CTRL, 0 };
    CHECK_EQ(conskeys_classify(&e), CONSKEY_NONE);
    /* The line editor still ignores them: nothing typed. */
    struct lineedit le = { 0 };
    typed(&le, "ab", 1);
    e = ctrl('k');
    CHECK_EQ(lineedit_key(&le, &e), LE_NONE);
    CHECK(strcmp(le.buf, "ab") == 0 && le.pos == 1);
}

static void test_route(void)
{
    for (int k = CONSKEY_CLEAR; k <= CONSKEY_WORD; k++) {
        CHECK_EQ(conskeys_route((enum conskey)k, false, false, false), CONSKEYS_ACT);
        CHECK_EQ(conskeys_route((enum conskey)k, true, false, false), CONSKEYS_LEAVE_ACT);     /* scrollback */
        CHECK_EQ(conskeys_route((enum conskey)k, false, true, false), CONSKEYS_IGNORE);        /* yes/no */
        CHECK_EQ(conskeys_route((enum conskey)k, true, true, false), CONSKEYS_LEAVE);          /* both */
        CHECK_EQ(conskeys_route((enum conskey)k, false, false, true), CONSKEYS_IGNORE);        /* a program */
        CHECK_EQ(conskeys_route((enum conskey)k, true, false, true), CONSKEYS_IGNORE);
        CHECK_EQ(conskeys_route((enum conskey)k, true, true, true), CONSKEYS_IGNORE);
    }
    CHECK_EQ(conskeys_route(CONSKEY_NONE, false, false, false), CONSKEYS_IGNORE);
    CHECK_EQ(conskeys_route(CONSKEY_NONE, true, false, false), CONSKEYS_IGNORE);
}

static void test_start_end(void)
{
    CHECK(edits("echo hi", 7, CONSKEY_START, true, "echo hi", 0));      /* end */
    CHECK(edits("echo hi", 3, CONSKEY_START, true, "echo hi", 0));      /* middle */
    CHECK(edits("echo hi", 0, CONSKEY_START, false, "echo hi", 0));     /* start */
    CHECK(edits("echo hi", 0, CONSKEY_END, true, "echo hi", 7));
    CHECK(edits("echo hi", 4, CONSKEY_END, true, "echo hi", 7));
    CHECK(edits("echo hi", 7, CONSKEY_END, false, "echo hi", 7));
    CHECK(edits("", 0, CONSKEY_START, false, "", 0));                   /* empty */
    CHECK(edits("", 0, CONSKEY_END, false, "", 0));
}

static void test_kill(void)
{
    CHECK(edits("echo hi there", 7, CONSKEY_KILL_END, true, "echo hi", 7));
    CHECK(edits("echo hi there", 0, CONSKEY_KILL_END, true, "", 0));
    CHECK(edits("echo hi there", 13, CONSKEY_KILL_END, false, "echo hi there", 13));
    CHECK(edits("echo hi there", 8, CONSKEY_KILL_START, true, "there", 0));
    CHECK(edits("echo hi there", 13, CONSKEY_KILL_START, true, "", 0));
    CHECK(edits("echo hi there", 0, CONSKEY_KILL_START, false, "echo hi there", 0));
    CHECK(edits("", 0, CONSKEY_KILL_END, false, "", 0));
    CHECK(edits("", 0, CONSKEY_KILL_START, false, "", 0));
    /* A full line (71 characters). */
    char full[LINE_MAX + 1] = { 0 };
    memset(full, 'x', LINE_MAX);
    full[LINE_MAX] = '\0';
    CHECK(edits(full, 70, CONSKEY_KILL_START, true, "x", 0));
    CHECK(edits(full, 1, CONSKEY_KILL_END, true, "x", 1));
}

static void test_word(void)
{
    CHECK(edits("echo one two", 12, CONSKEY_WORD, true, "echo one ", 9));         /* end */
    CHECK(edits("echo one two", 9, CONSKEY_WORD, true, "echo two", 5));           /* after a space */
    CHECK(edits("echo one two", 7, CONSKEY_WORD, true, "echo e two", 5));         /* inside a word */
    CHECK(edits("echo one   two", 11, CONSKEY_WORD, true, "echo two", 5));        /* several spaces */
    CHECK(edits("echo one   ", 11, CONSKEY_WORD, true, "echo ", 5));              /* only spaces after it */
    CHECK(edits("echo one two", 0, CONSKEY_WORD, false, "echo one two", 0));      /* the start */
    CHECK(edits("word", 4, CONSKEY_WORD, true, "", 0));                           /* a single word */
    CHECK(edits("word", 2, CONSKEY_WORD, true, "rd", 0));
    CHECK(edits("   word", 3, CONSKEY_WORD, true, "word", 0));                    /* spaces alone */
    CHECK(edits("", 0, CONSKEY_WORD, false, "", 0));                              /* empty */
    /* Twice in a row. */
    struct lineedit le = { 0 };
    typed(&le, "echo a b", 8);
    CHECK(conskeys_edit(&le, CONSKEY_WORD) && conskeys_edit(&le, CONSKEY_WORD));
    CHECK(strcmp(le.buf, "echo ") == 0 && le.pos == 5 && le.len == 5);
}

static void test_no_edit(void)
{
    /* Ctrl+L is the console's (no edit); CONSKEY_NONE changes nothing. */
    CHECK(edits("echo hi", 3, CONSKEY_CLEAR, false, "echo hi", 3));
    CHECK(edits("echo hi", 3, CONSKEY_NONE, false, "echo hi", 3));
    /* After an edit the line editor goes on as usual. */
    struct lineedit le = { 0 };
    typed(&le, "cho x", 0);
    struct key_event e = { KEY_CHAR, 'e', KEY_PRESSED, 0 };
    CHECK(conskeys_edit(&le, CONSKEY_END));
    CHECK(conskeys_edit(&le, CONSKEY_START));
    CHECK_EQ(lineedit_key(&le, &e), LE_CHANGED);
    CHECK(strcmp(le.buf, "echo x") == 0 && le.pos == 1);
    struct key_event enter = { KEY_ENTER, '\n', KEY_PRESSED, 0 };
    CHECK_EQ(lineedit_key(&le, &enter), LE_SUBMIT);
}

int main(void)
{
    printf("test_conskeys13\n");
    RUN(test_classify);
    RUN(test_route);
    RUN(test_start_end);
    RUN(test_kill);
    RUN(test_word);
    RUN(test_no_edit);
    return unit_report("test_conskeys13");
}
