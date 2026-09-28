/* Interactive console (see console.h). */
#include "drivers/serial/serial.h"
#include "drivers/vga/vga_text.h"
#include "kernel/arch/x86/cpuid.h"
#include "kernel/console/console.h"
#include "kernel/console/lineedit.h"
#include "kernel/console/textbuf.h"
#include "kernel/kernel.h"
#include "kernel/kprintf.h"
#include "kernel/memory/pmm.h"
#include "kernel/shell/parser.h"
#include "kernel/shell/shell.h"
#include "kernel/timer.h"

#define PROMPT       "madire> "
#define PROMPT_LEN   8

/* Colours: only VGA slots the boot logo does not reprogram. */
#define ATTR_TEXT    0x07       /* light grey */
#define ATTR_INPUT   0x0F       /* white */
#define ATTR_PROMPT  0x0B       /* light cyan */
#define ATTR_ERROR   0x0C       /* light red */
#define ATTR_TITLE   0x0F

static struct textbuf screen;
static struct lineedit input;
static bool active;
static uint8_t prompt_row;
static const struct memory_stats *boot_mem;
static struct cpu_info cpu;

/* ---- Drawing (grid -> VGA) ---- */

static void render_row(uint8_t row)
{
    for (uint8_t col = 0; col < TB_COLS; col++) {
        uint8_t attr = textbuf_attr_at(&screen, row, col);
        vga_put_cell(row, col, (uint8_t)textbuf_char_at(&screen, row, col),
                     attr & 0x0F, attr >> 4);
    }
}

static void render_all(void)
{
    for (uint8_t row = 0; row < TB_ROWS; row++)
        render_row(row);
}

static void place_cursor(void)
{
    vga_cursor_move(screen.row, screen.col < TB_COLS ? screen.col : TB_COLS - 1);
}

static void write_screen(const char *text, uint8_t attr)
{
    textbuf_set_attr(&screen, attr);
    textbuf_puts(&screen, text);
}

/* ---- Input line ---- */

static void draw_input(void)
{
    for (uint8_t i = 0; i < LINE_MAX; i++) {
        char c = i < input.len ? input.buf[i] : ' ';
        textbuf_put_at(&screen, prompt_row, (uint8_t)(PROMPT_LEN + i),
                       c == '\t' ? ' ' : c, ATTR_INPUT);
    }
    textbuf_set_cursor(&screen, prompt_row, (uint8_t)(PROMPT_LEN + input.pos));
    render_row(prompt_row);
    place_cursor();
}

static void show_prompt(void)
{
    if (screen.col != 0)
        textbuf_putc(&screen, '\n');
    write_screen(PROMPT, ATTR_PROMPT);
    prompt_row = screen.row;
    lineedit_reset(&input);
    draw_input();
}

/* ---- Shell glue ---- */

static void shell_sink(void *ctx, const char *text, enum shell_text kind)
{
    (void)ctx;
    write_screen(text, kind == SHELL_ERROR ? ATTR_ERROR : ATTR_TEXT);
    serial_write(text);
}

static const struct shell_output sink = { shell_sink, 0 };

static void build_context(struct shell_context *c)
{
    c->version = MADIRE_OS_VERSION;
    c->timer_ok = timer_verified();
    c->timer_hz = TIMER_HZ;
    c->uptime_seconds = timer_uptime_seconds();
    c->ticks = timer_ticks();
    c->mem = boot_mem;
    c->free_pages_now = pmm_free_pages();
    c->page_size = PAGE_SIZE;
    c->cpu = &cpu;
    c->cr0 = cpu_read_cr0();
}

static void submit(void)
{
    uint32_t scrolls_before = screen.scrolls;
    char line[LINE_MAX + 1];
    for (uint8_t i = 0; i <= input.len; i++)
        line[i] = input.buf[i];

    klogf("%s%s\n", PROMPT, input.buf);
    textbuf_set_cursor(&screen, prompt_row, (uint8_t)(PROMPT_LEN + input.len));
    textbuf_putc(&screen, '\n');

    struct shell_context ctx;
    build_context(&ctx);
    if (shell_execute(line, &ctx, &sink) == SHELL_CLEAR) {
        textbuf_clear(&screen);
        klogf("console: cleared\n");
    } else if (screen.scrolls != scrolls_before) {
        klogf("console: scrolled %u lines\n", screen.scrolls - scrolls_before);
    }

    show_prompt();
    render_all();
    place_cursor();
}

/* ---- Public ---- */

void console_start(const struct memory_stats *mem)
{
    struct cpuid_raw raw;
    boot_mem = mem;
    cpuid_read_all(&raw);
    cpuinfo_decode(&raw, &cpu);

    textbuf_init(&screen, ATTR_TEXT);
    write_screen("M A D I R E   O S\n", ATTR_TITLE);
    write_screen(MADIRE_OS_NAME " v" MADIRE_OS_VERSION "\n\n", ATTR_PROMPT);
    write_screen("Type 'help' for a list of commands.\n\n", ATTR_TEXT);

    vga_cursor_show(true);
    active = true;
    show_prompt();
    render_all();
    place_cursor();
    klogf("console: started\n");
}

bool console_active(void)
{
    return active;
}

void console_handle_key(const struct key_event *e)
{
    if (!active)
        return;
    switch (lineedit_key(&input, e)) {
    case LE_CHANGED:
        draw_input();
        break;
    case LE_REFUSED:
        klogf("console: input line full (%u characters)\n", (unsigned)LINE_MAX);
        break;
    case LE_SUBMIT:
        submit();
        break;
    case LE_NONE:
        break;
    }
}

/* ---- Self-test (pure modules only; no hardware) ---- */

static bool str_eq(const char *a, const char *b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

static char captured[32];
static uint32_t captured_len;

static void capture(void *ctx, const char *text, enum shell_text kind)
{
    (void)ctx;
    (void)kind;
    while (*text && captured_len + 1 < sizeof(captured))
        captured[captured_len++] = *text++;
    captured[captured_len] = '\0';
}

static bool selftest_lineedit(void)
{
    static const struct key_event keys[] = {
        { KEY_CHAR, 'h', KEY_PRESSED, 0 }, { KEY_CHAR, 'e', KEY_PRESSED, 0 },
        { KEY_CHAR, 'l', KEY_PRESSED, 0 }, { KEY_CHAR, 'o', KEY_PRESSED, 0 },
        { KEY_LEFT, 0, KEY_PRESSED, 0 },   { KEY_CHAR, 'l', KEY_PRESSED, 0 },
        { KEY_CHAR, 'x', KEY_PRESSED | KEY_CTRL, 0 },          /* ignored */
        { KEY_CHAR, 'y', 0, 0 },                               /* release: ignored */
        { KEY_END, 0, KEY_PRESSED, 0 },
    };
    struct lineedit le;
    lineedit_reset(&le);
    for (uint32_t i = 0; i < sizeof(keys) / sizeof(keys[0]); i++)
        lineedit_key(&le, &keys[i]);
    struct key_event enter = { KEY_ENTER, '\n', KEY_PRESSED, 0 };
    return lineedit_key(&le, &enter) == LE_SUBMIT && str_eq(le.buf, "hello") && le.pos == 5;
}

static bool selftest_parser(void)
{
    char line[] = "  echo \t a  b ";
    char *argv[SHELL_MAX_ARGS];
    int argc;
    return shell_parse(line, &argc, argv) == PARSE_OK && argc == 3
        && str_eq(argv[0], "echo") && str_eq(argv[1], "a") && str_eq(argv[2], "b");
}

static bool selftest_shell(void)
{
    static const struct memory_stats no_mem;
    static const struct cpu_info no_cpu;
    struct shell_context ctx = { .version = MADIRE_OS_VERSION, .mem = &no_mem, .cpu = &no_cpu };
    struct shell_output out = { capture, 0 };
    char line[] = "echo ok";
    captured_len = 0;
    captured[0] = '\0';
    return shell_execute(line, &ctx, &out) == SHELL_OK && str_eq(captured, "ok\n");
}

static bool selftest_textbuf(void)
{
    static struct textbuf t;
    textbuf_init(&t, ATTR_TEXT);
    for (int i = 0; i < TB_ROWS + 1; i++) {
        textbuf_putc(&t, (char)('A' + i));
        textbuf_putc(&t, '\n');
    }
    /* 26 lines: two scrolled off, so the top row now holds 'C'. */
    return t.scrolls == 2 && textbuf_char_at(&t, 0, 0) == 'C' && t.row == TB_ROWS - 1;
}

bool console_selftest(void)
{
    bool le = selftest_lineedit(), pa = selftest_parser();
    bool sh = selftest_shell(), tb = selftest_textbuf();
    klogf("Console self-test: %s (line editor %s, parser %s, shell %s, text grid %s)\n",
          le && pa && sh && tb ? "PASS" : "FAIL", le ? "ok" : "FAIL", pa ? "ok" : "FAIL",
          sh ? "ok" : "FAIL", tb ? "ok" : "FAIL");
    return le && pa && sh && tb;
}
