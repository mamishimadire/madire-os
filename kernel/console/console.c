/* Interactive console (see console.h). */
#include "drivers/serial/serial.h"
#include "drivers/vga/vga_text.h"
#include "kernel/arch/x86/cmos.h"
#include "kernel/arch/x86/cpuid.h"
#include "kernel/arch/x86/reset.h"
#include "kernel/console/console.h"
#include "kernel/console/history.h"
#include "kernel/console/lineedit.h"
#include "kernel/console/outq.h"
#include "kernel/console/scrollback.h"
#include "kernel/console/textbuf.h"
#include "kernel/kernel.h"
#include "kernel/kprintf.h"
#include "kernel/logbuf.h"
#include "kernel/rtc_time.h"
#include "kernel/memory/kmalloc.h"
#include "kernel/memory/pmm.h"
#include "kernel/panic.h"
#include "kernel/proc/preempt.h"
#include "kernel/proc/process.h"
#include "kernel/proc/progmod.h"
#include "kernel/proc/thread.h"
#include "kernel/shell/parser.h"
#include "kernel/shell/shell.h"
#include "kernel/timer.h"

#define PROMPT       "madire> "
#define PROMPT_LEN   8
#define CONFIRM_PROMPT "yes/no> "   /* 0.9.1: answering "reboot"; also 8 wide */

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
static struct history hist;             /* 0.9.1 */
static bool confirming;                 /* 0.9.1: the next line answers "reboot" */
static const char *prompt = PROMPT;
static struct scrollback sb;            /* 0.9.2: 1000 lines, about 156 KiB */
static int32_t fg_pid;                  /* 0.11: the foreground program (0: none) */
static int32_t kill_pid;                /* 0.11: the process "kill" asked about (0: none) */
static bool ctrl_c_sent;                /* 0.11: Ctrl+C sent to the foreground program */
static uint64_t ctrl_c_tick;
static int32_t bg_list[PROC_MAX];       /* 0.11: background programs to collect */
static uint32_t bg_count;
static struct outq program_outq;          /* 0.11 stage 5: foreground output */
static int32_t outq_wait_logged_pid;
static uint8_t outq_drain[256];

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

/* ---- Scrollback (0.9.2) ---- */

/* Every character the console writes to its grid goes through here: a
 * line about to scroll off the top is copied first and, if it did scroll
 * off, kept in the scrollback (textbuf.c itself is unchanged). Only a
 * character written on the bottom row can scroll, at most one line. */
static void grid_putc(char c)
{
    static uint16_t top_row[TB_COLS];
    if (screen.row == TB_ROWS - 1)
        for (uint32_t i = 0; i < TB_COLS; i++)
            top_row[i] = screen.cells[0][i];
    uint32_t before = screen.scrolls;
    textbuf_putc(&screen, c);
    if (screen.scrolls != before)
        sb_push(&sb, top_row);
}

/* While scrolled back: rows 0-23 from the scrollback and the live screen,
 * row 24 the indicator (reverse colours), the hardware cursor hidden. */
static void show_view(void)
{
    char top[SB_TEXT_MAX + 1], ind[TB_COLS + 1];
    for (uint32_t r = 0; r < SB_VIEW_ROWS; r++) {
        struct sb_source src = sb_view_row(&sb, r);
        const uint16_t *cells = src.from_screen ? screen.cells[src.index]
                                                : sb_line(&sb, src.index);
        for (uint32_t c = 0; c < TB_COLS; c++)
            vga_put_cell(r, c, (uint8_t)(cells[c] & 0xFF), (uint8_t)((cells[c] >> 8) & 0x0F),
                         (uint8_t)(cells[c] >> 12));
        if (r == 0)
            sb_row_text(cells, top, SB_TEXT_MAX);
    }
    uint32_t n = sb_indicator(&sb, ind);
    for (uint32_t c = 0; c < TB_COLS; c++)
        vga_put_cell(TB_ROWS - 1, c, (uint8_t)(c < n ? ind[c] : ' '), SB_INDICATOR_ATTR & 0x0F,
                     SB_INDICATOR_ATTR >> 4);
    vga_cursor_show(false);
    klogf("console: scrolled back: line %u of %u, top '%s'\n", sb_first_shown(&sb),
          sb_total(&sb), top);
}

static void show_bottom(void)
{
    render_all();
    vga_cursor_show(true);
    place_cursor();
    klogf("console: back at the bottom\n");
}

/* Any other key, and any new output, returns to the live screen. */
static void leave_scrollback(void)
{
    if (sb_to_bottom(&sb))
        show_bottom();
}

/* "clear" keeps the screen's used rows in the scrollback first. */
static void keep_screen_rows(void)
{
    int32_t last = sb_last_used_row(screen.cells);
    for (int32_t r = 0; r <= last; r++)
        sb_push(&sb, screen.cells[r]);
}

static void write_screen(const char *text, uint8_t attr)
{
    leave_scrollback();
    textbuf_set_attr(&screen, attr);
    while (*text)
        grid_putc(*text++);
}

/* Program output is queued by its process, but only thread 0 draws it. */
static void drain_program_output(void)
{
    preempt_disable();
    size_t n = outq_read(&program_outq, outq_drain, sizeof(outq_drain));
    preempt_enable();
    if (n == 0)
        return;

    leave_scrollback();
    textbuf_set_attr(&screen, ATTR_TEXT);
    for (size_t i = 0; i < n; i++) {
        char c = (char)outq_drain[i];
        if (c == '\n') {
            grid_putc('\n');
        } else {
            if (screen.col >= 79)
                grid_putc('\n');
            grid_putc(c);
        }
    }
    render_all();
    place_cursor();
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
        grid_putc('\n');
    prompt = confirming ? CONFIRM_PROMPT : PROMPT;
    write_screen(prompt, ATTR_PROMPT);
    prompt_row = screen.row;
    lineedit_reset(&input);
    draw_input();
}

/* ---- Shell glue ---- */

/* The shell hands over one complete line per call. Its serial copy runs
 * with preemption off (0.9), so another thread's klogf line never lands in
 * the middle of it. The screen belongs to thread 0 (the console). */
static void shell_sink(void *ctx, const char *text, enum shell_text kind)
{
    (void)ctx;
    write_screen(text, kind == SHELL_ERROR ? ATTR_ERROR : ATTR_TEXT);
    preempt_disable();
    serial_write(text);
    preempt_enable();
}

static const struct shell_output sink = { shell_sink, 0 };

/* ---- Programs (0.11) ---- */

/* shell_procs.start: a program module, by name. */
static int32_t start_program(void *ctx, const char *name, uint64_t arg, bool background)
{
    (void)ctx;
    if (!progmod_find(progmod_table(), name))
        return SHELL_RUN_NO_PROGRAM;
    if (background && bg_count >= PROC_MAX)
        return SHELL_RUN_FAILED;
    if (!background)
        preempt_disable();
    int32_t pid = process_start_program(name, arg);
    if (pid <= 0) {
        if (!background)
            preempt_enable();
        return SHELL_RUN_FAILED;
    }
    klogf("console: %s process %d '%s', argument %llu\n",
          background ? "started in the background:" : "running", (int)pid, name,
          (unsigned long long)arg);
    if (background)
        bg_list[bg_count++] = pid;
    else
        fg_pid = pid;
    if (!background)
        preempt_enable();
    return pid;
}

/* shell_procs.ask_kill: the next line answers "End process N?". */
static void ask_kill(void *ctx, int32_t pid)
{
    (void)ctx;
    kill_pid = pid;
}

/* Collects an ended process: its result and its end line. */
static void collect(int32_t pid, char *line)
{
    struct process_info rows[PROC_MAX];
    uint32_t n = process_list(rows, PROC_MAX);
    const char *name = "?";
    for (uint32_t i = 0; i < n; i++)
        if (rows[i].pid == pid)
            name = rows[i].name;
    char kept[PROC_NAME_MAX];
    uint32_t k = 0;
    for (; k + 1 < PROC_NAME_MAX && name[k]; k++)
        kept[k] = name[k];
    kept[k] = '\0';
    uint64_t address = process_fault_address(pid);
    struct proc_result res = { PROC_END_EXIT, 0 };
    process_wait(pid, &res);
    shell_end_line(line, pid, kept, (enum shell_end)res.end, res.code,
                   res.end == PROC_END_FAULT && res.code >= 0
                       ? exception_mnemonic((unsigned)res.code) : 0, address);
}

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
    c->efer = cpu_read_efer();          /* LMA: the CPU really is in long mode */
    c->paging = paging_stats();
    c->heap = kheap_stats();            /* runs a full heap_check first */
    c->stack_size = kernel_stack_size();
    c->stack_peak = kernel_stack_peak();

    /* 0.9: the threads for "ps" (a snapshot; the console is thread 0). */
    static struct shell_thread rows[SCHED_MAX_THREADS];
    uint32_t n = 0;
    for (int32_t tid = 0; tid < SCHED_MAX_THREADS; tid++) {
        enum sched_state st = thread_state(tid);
        if (st == SCHED_FREE)
            continue;
        rows[n].tid = tid;
        rows[n].state = sched_state_str(st);
        rows[n].ticks = thread_ticks_run(tid);
        rows[n].stack_peak = thread_stack_peak(tid);
        rows[n].stack_size = thread_stack_size(tid);
        rows[n].name = thread_name(tid);
        n++;
    }
    c->threads = rows;
    c->thread_count = n;

    /* 0.11: the processes for "ps", and "run". */
    static struct process_info infos[PROC_MAX];
    static struct shell_process prows[PROC_MAX];
    static struct shell_procs procs;
    uint32_t np = process_list(infos, PROC_MAX);
    for (uint32_t i = 0; i < np; i++) {
        prows[i].pid = infos[i].pid;
        prows[i].state = infos[i].ended ? "ended" : "running";
        prows[i].tid = infos[i].tid;
        prows[i].name = infos[i].name;
    }
    procs.rows = prows;
    procs.count = np;
    procs.start = start_program;
    procs.ask_kill = ask_kill;
    procs.ctx = 0;
    c->procs = &procs;

    /* 0.9.1: the extra commands' data. The kernel log is copied with
     * preemption off, so no other thread's klogf changes it mid-copy; the
     * copy ends with this command's own "madire> ..." line. */
    static struct shell_extras extras;
    static char log_copy[LOGBUF_KERNEL_SIZE];
    extras.hostname = MADIRE_HOSTNAME;
    struct rtc_raw raw;
    struct rtc_time now;
    extras.rtc_ok = cmos_rtc_read(&raw) && rtc_decode(&raw, &now);
    if (extras.rtc_ok)
        rtc_format(&now, extras.date_text);
    preempt_disable();
    extras.log_len = logbuf_copy(klog_buffer(), log_copy, sizeof(log_copy));
    extras.log_wrapped = logbuf_wrapped(klog_buffer());
    preempt_enable();
    extras.log = log_copy;
    extras.history = &hist;
    c->extras = &extras;
}

/* 0.9.1: the answer to "reboot". Only exactly "yes" restarts. */
static void answer_reboot(const char *answer)
{
    if (shell_reboot_confirmed(answer)) {
        shell_sink(0, SHELL_REBOOT_STARTING "\n", SHELL_TEXT);
        render_all();
        place_cursor();
        klogf("console: restart confirmed\n");
        reset_machine(true);                /* does not return */
    }
    shell_sink(0, SHELL_REBOOT_CANCELLED "\n", SHELL_TEXT);
}

/* 0.11: the answer to "kill". Only exactly "yes" ends the process; the
 * console then waits for it like a foreground program (its end line, then
 * the prompt). Returns true while it waits. */
static bool answer_kill(const char *answer)
{
    int32_t pid = kill_pid;
    kill_pid = 0;
    if (!shell_reboot_confirmed(answer)) {      /* the same exact "yes" rule */
        shell_sink(0, SHELL_KILL_CANCELLED "\n", SHELL_TEXT);
        return false;
    }
    if (process_kill(pid) != PROC_OK) {         /* it ended meanwhile */
        char t[64];
        ksnprintf(t, sizeof(t), "kill: process %d has already ended.\n", (int)pid);
        shell_sink(0, t, SHELL_ERROR);
        return false;
    }
    klogf("console: kill confirmed for process %d\n", (int)pid);
    for (uint32_t i = 0; i < bg_count; i++) {
        if (bg_list[i] == pid) {
            bg_list[i] = bg_list[--bg_count];
            break;
        }
    }
    fg_pid = pid;
    return true;
}

static void submit(void)
{
    uint32_t scrolls_before = screen.scrolls;
    char line[LINE_MAX + 1];
    for (uint8_t i = 0; i <= input.len; i++)
        line[i] = input.buf[i];

    klogf("%s%s\n", prompt, input.buf);
    textbuf_set_cursor(&screen, prompt_row, (uint8_t)(PROMPT_LEN + input.len));
    grid_putc('\n');

    enum shell_result r = SHELL_OK;
    if (confirming) {
        confirming = false;                 /* not a command: not in the history */
        if (kill_pid) {
            if (answer_kill(line))
                r = SHELL_RUN;              /* wait for its end, as for a program */
        } else {
            answer_reboot(line);
        }
    } else {
        history_add(&hist, line);           /* before it runs: "history" lists itself */
        struct shell_context ctx;
        build_context(&ctx);
        r = shell_execute(line, &ctx, &sink);
        confirming = r == SHELL_REBOOT || r == SHELL_KILL;
    }
    if (r == SHELL_RUN) {                   /* 0.11: no prompt until it ends */
        render_all();
        place_cursor();
        return;
    }
    if (r == SHELL_CLEAR) {
        keep_screen_rows();                 /* 0.9.2: clear never loses lines */
        textbuf_clear(&screen);
        klogf("console: cleared\n");
    } else if (screen.scrolls != scrolls_before) {
        klogf("console: scrolled %u lines\n", screen.scrolls - scrolls_before);
    }

    show_prompt();
    render_all();
    place_cursor();
}

/* 0.9.1: Up/Down recall a line from the history into the input line,
 * through the line editor (lineedit.c itself still ignores these keys). */
static void recall(const char *text)
{
    lineedit_reset(&input);
    for (const char *p = text; *p; p++) {
        struct key_event e = { *p == '\t' ? KEY_TAB : KEY_CHAR, *p, KEY_PRESSED, 0 };
        lineedit_key(&input, &e);
    }
    draw_input();
}

/* ---- Public ---- */

void console_start(const struct memory_stats *mem)
{
    struct cpuid_raw raw;
    boot_mem = mem;
    cpuid_read_all(&raw);
    cpuinfo_decode(&raw, &cpu);

    textbuf_init(&screen, ATTR_TEXT);
    sb_init(&sb);
    preempt_disable();
    outq_init(&program_outq);
    preempt_enable();
    outq_wait_logged_pid = 0;
    write_screen("M A D I R E   O S\n", ATTR_TITLE);
    write_screen(MADIRE_OS_NAME " v" MADIRE_OS_VERSION "\n\n", ATTR_PROMPT);
    write_screen("Type 'help' for a list of commands.\n\n", ATTR_TEXT);

    vga_cursor_show(true);
    history_init(&hist);
    confirming = false;
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
    if (fg_pid) {
        /* 0.11: a program runs in the foreground; only Ctrl+C reaches it
         * (it ends the program). The key is never logged. */
        if ((e->flags & KEY_PRESSED) && (e->flags & KEY_CTRL) && e->keycode == KEY_CHAR &&
            (e->ascii == 'c' || e->ascii == 'C' || e->ascii == 3) && !ctrl_c_sent) {
            ctrl_c_sent = true;
            ctrl_c_tick = timer_ticks();
            if (process_kill(fg_pid) == PROC_OK)
                klogf("console: Ctrl+C: kill requested for process %d\n", (int)fg_pid);
        }
        return;
    }

    /* 0.9.2: Shift+PgUp / Shift+PgDn page through the scrollback; while
     * scrolled back, any other key press first returns to the live screen
     * and is then handled as usual. Not scrolled back, plain PgUp/PgDn
     * still do nothing (the line editor ignores them). */
    enum sb_key k = sb_classify_key(e);
    if (k == SB_KEY_PAGE_UP || k == SB_KEY_PAGE_DOWN) {
        enum sb_move m = k == SB_KEY_PAGE_UP ? sb_page_up(&sb) : sb_page_down(&sb);
        if (m == SB_EMPTY)
            klogf("console: scrollback is empty\n");
        else if (m == SB_MOVED && sb_scrolled(&sb))
            show_view();
        else if (m == SB_MOVED)
            show_bottom();                  /* Shift+PgDn reached the live screen */
        return;
    }
    if (k == SB_KEY_OTHER)
        leave_scrollback();

    if ((e->keycode == KEY_UP || e->keycode == KEY_DOWN) && (e->flags & KEY_PRESSED)
        && !(e->flags & (KEY_CTRL | KEY_ALT)) && !confirming) {
        const char *text = e->keycode == KEY_UP ? history_up(&hist) : history_down(&hist);
        if (text)
            recall(text);
        return;
    }
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

void console_poll(void)
{
    if (!active)
        return;
    drain_program_output();
    char line[SHELL_END_LINE_MAX];
    for (uint32_t i = 0; i < bg_count; ) {
        if (!process_ended(bg_list[i])) {
            i++;
            continue;
        }
        collect(bg_list[i], line);
        klogf("%s\n", line);                /* background: serial and dmesg only */
        bg_list[i] = bg_list[--bg_count];
    }
    if (fg_pid && process_ended(fg_pid)) {
        preempt_disable();
        size_t queued = outq_used(&program_outq);
        preempt_enable();
        if (queued != 0)
            return;
        collect(fg_pid, line);
        if (ctrl_c_sent)                    /* 0.11: how soon Ctrl+C took effect */
            klogf("console: process %d collected %u ticks after Ctrl+C\n", (int)fg_pid,
                  (unsigned)(timer_ticks() - ctrl_c_tick));
        ctrl_c_sent = false;
        fg_pid = 0;
        uint32_t len = 0;
        while (line[len])
            len++;
        line[len] = '\n';
        line[len + 1] = '\0';
        if (screen.col != 0)
            grid_putc('\n');
        shell_sink(0, line, SHELL_TEXT);
        show_prompt();
        render_all();
        place_cursor();
    }
}

/* Called only by sys_write from a process thread. Background output remains
 * serial-only. A full queue yields until thread 0 drains it; a pending kill
 * returns a short write promptly, so no bytes disappear without a count. */
size_t console_program_write(int32_t pid, const uint8_t *text, size_t len)
{
    preempt_disable();
    bool foreground = pid == fg_pid;
    preempt_enable();
    if (!foreground)
        return len;

    size_t accepted = 0;
    while (accepted < len) {
        if (process_current_kill_pending())
            break;

        preempt_disable();
        size_t n = outq_write(&program_outq, text + accepted, len - accepted);
        bool full = n < len - accepted;
        preempt_enable();
        accepted += n;

        if (full && outq_wait_logged_pid != pid) {
            outq_wait_logged_pid = pid;
            klogf("console: process %d waiting for output queue\n", (int)pid);
        }
        if (accepted < len)
            thread_yield();
    }
    return accepted;
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
