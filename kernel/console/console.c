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
#include "kernel/fs/fs_errno.h"
#include "kernel/fs/fsys_core.h"
#include "kernel/fs/ramfs_core.h"
#include "kernel/fs/vfs.h"
#include "kernel/kernel.h"
#include "kernel/kprintf.h"
#include "kernel/logbuf.h"
#include "kernel/rtc_time.h"
#include "kernel/memory/kmalloc.h"
#include "kernel/memory/pmm.h"
#include "kernel/panic.h"
#include "kernel/proc/fd.h"
#include "kernel/proc/preempt.h"
#include "kernel/proc/process.h"
#include "kernel/proc/progmod.h"
#include "kernel/proc/thread.h"
#include "kernel/shell/parser.h"
#include "kernel/shell/shell.h"
#include "kernel/timer.h"

#define PROMPT_LEN   SHELL_PROMPT_WIDTH /* 0.12: the prompt is in the shell state */
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
static struct shell_state shell_st;     /* 0.12: the prompt and the aliases */
static const char *prompt = shell_st.prompt;
static struct scrollback sb;            /* 0.9.2: 1000 lines, about 156 KiB */
static int32_t fg_pid;                  /* 0.11: the foreground program (0: none) */
static int32_t kill_pid;                /* 0.11: the process "kill" asked about (0: none) */
static bool ctrl_c_sent;                /* 0.11: Ctrl+C sent to the foreground program */
static uint64_t ctrl_c_tick;
static int32_t bg_list[PROC_MAX];       /* 0.11: background programs to collect */
static uint32_t bg_count;
static struct shell_session session;    /* 0.12 stage 2: variables, waits, time */
static bool sleeping;                   /* 0.12 stage 2: "sleep" waits */
static uint64_t sleep_start, sleep_until;
static bool awaiting_key;               /* 0.12 stage 2: "presskey" waits */
static bool timing;                     /* 0.12 stage 2: "time": a line is due */
static uint64_t timing_start;
static struct outq program_outq;          /* 0.11 stage 5: foreground output */
static int32_t outq_wait_logged_pid;
static uint8_t outq_drain[256];
/* 1.0 stage 8: the shell's own current folder (NULL is the root, with no
 * hold, as in a process's descriptor table; any other folder is held),
 * the folder "ls" lists and the file "cat" prints (both held meanwhile). */
static struct vnode *sh_cwd;
static struct vnode *ls_dir;
static struct vnode *cat_node;
static bool catting;
static uint64_t cat_off, cat_start;
static struct shell_cat cat_text;
static uint8_t cat_chunk[4096];

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
    prompt = confirming ? CONFIRM_PROMPT : shell_st.prompt;
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

/* ---- Files (1.0 stage 8) ---- */

/*
 * The shell's file commands reach the filesystem only here, on thread 0.
 * Every call is one hold of the VFS lock (fd_vfs_acquire, with the lock's
 * figures) around bounded work into kernel buffers: one path lookup, one
 * batch of at most SHELL_LS_BATCH folder entries, or one chunk of at most
 * 4096 bytes of a file. Nothing is printed, and nothing yields, sleeps or
 * waits, while the lock is held (the klock self-checks would panic);
 * thread 0 waits for the lock only by yielding (klock.h). Nodes are given
 * back with fd_node_drop, outside the lock.
 */

static bool fs_up(void)
{
    return vfs_root()->root != 0;           /* the filesystem booted */
}

static struct vnode *cwd_node(void)
{
    return sh_cwd ? sh_cwd : vfs_root()->root;
}

/* A new hold of the shell's current folder for a program (NULL: the root). */
static struct vnode *cwd_hold(void)
{
    struct vnode *n = sh_cwd;
    if (!n)
        return 0;
    fd_vfs_acquire();
    vfs_hold(n);
    fd_vfs_release();
    return n;
}

static int64_t fs_cwd_path(void *ctx, char *buf, uint32_t size)
{
    (void)ctx;
    fd_vfs_acquire();
    int64_t r = fsys_cwd_path(cwd_node(), vfs_root()->root, buf, size);
    fd_vfs_release();
    return r;
}

static bool fs_cwd_removed(void *ctx)
{
    (void)ctx;
    fd_vfs_acquire();
    bool gone = cwd_node()->parent == 0;    /* a removed folder has no parent */
    fd_vfs_release();
    return gone;
}

static int fs_chdir(void *ctx, const char *path)
{
    (void)ctx;
    struct vfs *v = vfs_root();
    struct vnode *n = 0;
    fd_vfs_acquire();
    int r = vfs_lookup(v, cwd_node(), path, &n);
    if (r == 0 && n->type != VNODE_DIR)
        r = -FS_ENOTDIR;
    if (r == 0) {
        struct vnode *old = sh_cwd;
        if (n == v->root)
            n = 0;                          /* the root needs no hold */
        else
            vfs_hold(n);                    /* the new one first: it may be the old */
        sh_cwd = n;
        if (old)
            vfs_drop(v, old);               /* a folder has no pages to give back */
    }
    fd_vfs_release();
    return r;
}

/* A node's facts (the VFS lock held). */
static void fs_facts(const struct vnode *n, struct shell_finfo *f)
{
    uint32_t subdirs = 0;
    if (n->type == VNODE_DIR)
        for (const struct vnode *e = ramfs_first(n); e; e = ramfs_next(e))
            if (e->type == VNODE_DIR)
                subdirs++;
    f->type = n->type == VNODE_DIR ? SHELL_FT_DIR : SHELL_FT_FILE;
    f->pages = ramfs_file_pages(n);
    f->nlink = n->nlink == 0 ? 0 : n->type == VNODE_DIR ? 2 + subdirs : 1;
    f->mode = n->mode;
    f->size = n->type == VNODE_DIR ? 0 : n->size;
    f->ino = n->ino;
    f->atime = n->atime;
    f->mtime = n->mtime;
    f->ctime = n->ctime;
    f->name[0] = '\0';
}

static int fs_stat(void *ctx, const char *path, struct shell_finfo *out)
{
    (void)ctx;
    struct vnode *n = 0;
    fd_vfs_acquire();
    int r = vfs_lookup(vfs_root(), cwd_node(), path, &n);
    if (r == 0)
        fs_facts(n, out);
    fd_vfs_release();
    return r;
}

static int fs_mkdir(void *ctx, const char *path)
{
    (void)ctx;
    fd_vfs_acquire();
    int r = vfs_create(vfs_root(), cwd_node(), path, VNODE_DIR, 0755, 0);
    fd_vfs_release();
    return r;
}

static int fs_rmdir(void *ctx, const char *path)
{
    (void)ctx;
    fd_vfs_acquire();
    int r = vfs_rmdir(vfs_root(), cwd_node(), path);
    fd_vfs_release();
    return r;
}

/* An empty file (mode 0644), or an existing file's or folder's access,
 * modify and change times set to now. */
static int fs_touch(void *ctx, const char *path)
{
    (void)ctx;
    struct vfs *v = vfs_root();
    fd_vfs_acquire();
    int r = vfs_create(v, cwd_node(), path, VNODE_FILE, 0644, 0);
    if (r == -FS_EEXIST || r == -FS_EISDIR) {
        struct vnode *n = 0;
        r = vfs_lookup(v, cwd_node(), path, &n);
        if (r == 0)
            n->atime = n->mtime = n->ctime = v->now();
    }
    fd_vfs_release();
    return r;
}

static int fs_list_open(void *ctx, const char *path)
{
    (void)ctx;
    struct vnode *n = 0;
    fd_vfs_acquire();
    int r = vfs_lookup(vfs_root(), cwd_node(), path, &n);
    if (r == 0 && n->type != VNODE_DIR)
        r = -FS_ENOTDIR;
    if (r == 0) {
        vfs_hold(n);
        ls_dir = n;
    }
    fd_vfs_release();
    return r;
}

static int fs_list_next(void *ctx, uint64_t *cursor, struct shell_finfo *rows, uint32_t max)
{
    (void)ctx;
    int r = 0;
    fd_vfs_acquire();
    if (ls_dir != vfs_root()->root && !ls_dir->parent) {
        r = -FS_ENOENT;                     /* removed meanwhile (as getdents64, S6-3) */
    } else {
        for (struct vnode *e = ramfs_entry_from(ls_dir, *cursor); e && (uint32_t)r < max;
             e = ramfs_next(e)) {
            size_t len = 0;
            const char *s = ramfs_name(e, &len);
            fs_facts(e, &rows[r]);
            for (size_t i = 0; i < len && i < SHELL_NAME_MAX; i++)
                rows[r].name[i] = s[i];
            rows[r].name[len < SHELL_NAME_MAX ? len : SHELL_NAME_MAX] = '\0';
            *cursor = (uint64_t)ramfs_seq(e) + 1;
            r++;
        }
    }
    fd_vfs_release();
    return r;
}

static void fs_list_close(void *ctx)
{
    (void)ctx;
    if (ls_dir)
        fd_node_drop(ls_dir);
    ls_dir = 0;
}

static int fs_cat_open(void *ctx, const char *path)
{
    (void)ctx;
    struct vnode *n = 0;
    fd_vfs_acquire();
    int r = vfs_lookup(vfs_root(), cwd_node(), path, &n);
    if (r == 0 && n->type == VNODE_DIR)
        r = -FS_EISDIR;
    if (r == 0) {
        vfs_hold(n);
        cat_node = n;
        cat_off = 0;
    }
    fd_vfs_release();
    return r;
}

static const struct shell_files files_ops = {
    fs_cwd_path, fs_cwd_removed, fs_chdir, fs_stat, fs_mkdir, fs_rmdir, fs_touch,
    fs_list_open, fs_list_next, fs_list_close, fs_cat_open, 0,
};

/* ---- Programs (0.11) ---- */

/* shell_procs.start: a program module, by name (argv[0]); 1.0 stage 7:
 * with its arguments and environment (System V start-up). */
static int32_t start_program(void *ctx, int argc, char **argv, uint32_t envc,
                             const char *const *env_names, const char *const *env_values,
                             bool background)
{
    (void)ctx;
    const char *name = argv[0];
    if (!progmod_find(progmod_table(), name))
        return SHELL_RUN_NO_PROGRAM;
    if (background && bg_count >= PROC_MAX)
        return SHELL_RUN_FAILED;
    /* 1.0 stage 8 (S6-5): the program starts in the shell's current
     * folder. Its hold is taken here, under the VFS lock and before
     * preemption is turned off (the lock is taken at count 0 only); the
     * process takes it over if it starts, else it is dropped below. */
    struct vnode *cwd = cwd_hold();
    if (!background)
        preempt_disable();
    int32_t pid = process_start_argv_in(name, true, (uint32_t)argc, (const char *const *)argv,
                                        envc, env_names, env_values, cwd);
    if (pid <= 0) {
        if (!background)
            preempt_enable();
        if (cwd)
            fd_node_drop(cwd);
        return pid == PROCESS_START_E2BIG ? SHELL_RUN_TOO_BIG : SHELL_RUN_FAILED;
    }
    klogf("console: %s process %d '%s', argc %d\n",
          background ? "started in the background:" : "running", (int)pid, name, argc);
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

    /* 0.12: prompt, alias and unalias change this state. */
    c->state = &shell_st;
    /* 0.12 stage 2: variables, sleep, presskey, time and kill -y. */
    c->session = &session;
    /* 1.0 stage 8: the file commands, once the filesystem has booted. */
    c->files = fs_up() ? &files_ops : 0;
}

/* 0.12 stage 2: Ctrl held with c (as for a foreground program). */
static bool is_ctrl_c(const struct key_event *e)
{
    return (e->flags & KEY_PRESSED) && (e->flags & KEY_CTRL) && e->keycode == KEY_CHAR &&
           (e->ascii == 'c' || e->ascii == 'C' || e->ascii == 3);
}

/* 0.12 stage 2: the next prompt, after "time"'s line if one is due (not
 * while a question waits for its answer: the time then covers the answer). */
static void next_prompt(void)
{
    if (timing && !confirming) {
        char t[SHELL_TIME_LINE_MAX + 1];
        uint32_t n = 0;
        timing = false;
        shell_time_line(t, timer_ticks() - timing_start, TIMER_HZ);
        while (t[n])
            n++;
        t[n] = '\n';
        t[n + 1] = '\0';
        if (screen.col != 0)
            grid_putc('\n');
        shell_sink(0, t, SHELL_TEXT);
    }
    show_prompt();
    render_all();
    place_cursor();
}

/* 0.12 stage 2: a "sleep" or "presskey" wait ends (`text`: why, or 0). */
static void end_wait(const char *text)
{
    sleeping = false;
    awaiting_key = false;
    if (text)
        shell_sink(0, text, SHELL_TEXT);
    next_prompt();
}

/* 1.0 stage 8: "cat" ends (why: the cancel line, or 0 at the end of the
 * file): its last line, the file given back, the next prompt. */
static void cat_finish(const char *why)
{
    shell_cat_end(&cat_text, &sink);
    catting = false;
    fd_node_drop(cat_node);
    cat_node = 0;
    uint64_t ticks = timer_ticks() - cat_start;
    if (why) {
        klogf("console: Ctrl+C: cat cancelled after %llu bytes (%llu ticks)\n",
              (unsigned long long)cat_off, (unsigned long long)ticks);
        shell_sink(0, why, SHELL_TEXT);
    } else {
        klogf("console: cat: %llu bytes in %llu ticks\n", (unsigned long long)cat_off,
              (unsigned long long)ticks);
    }
    next_prompt();
}

/* 1.0 stage 8: one chunk of the file per pass of the idle loop: read under
 * the VFS lock into a kernel buffer, then shown after the release, so keys
 * (Ctrl+C) are handled between chunks and other threads run. */
static void cat_step(void)
{
    fd_vfs_acquire();
    int64_t n = ramfs_read(vfs_ramfs(), cat_node, cat_off, cat_chunk, sizeof(cat_chunk));
    fd_vfs_release();
    if (n <= 0) {
        cat_finish(0);
        return;
    }
    shell_cat_put(&cat_text, cat_chunk, (uint32_t)n, &sink);
    cat_off += (uint64_t)n;
    render_all();
    place_cursor();
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

static bool end_process(int32_t pid, const char *how);

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
    return end_process(pid, "kill confirmed");
}

/* Ends `pid` ("kill" answered "yes", or "kill -y"); the console then waits
 * for it like a foreground program. Returns true while it waits. */
static bool end_process(int32_t pid, const char *how)
{
    if (process_kill(pid) != PROC_OK) {         /* it ended meanwhile */
        char t[64];
        ksnprintf(t, sizeof(t), "kill: process %d has already ended.\n", (int)pid);
        shell_sink(0, t, SHELL_ERROR);
        return false;
    }
    klogf("console: %s for process %d\n", how, (int)pid);
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
        uint64_t started = timer_ticks();
        r = shell_execute(line, &ctx, &sink);
        confirming = r == SHELL_REBOOT || r == SHELL_KILL;
        if (session.timed) {                /* 0.12 stage 2: "time <command>" */
            timing = true;
            timing_start = started;
        }
        if (r == SHELL_KILL_NOW) {          /* 0.12 stage 2: "kill -y" */
            int32_t pid = kill_pid;
            kill_pid = 0;
            r = end_process(pid, "kill -y") ? SHELL_RUN : SHELL_OK;
        } else if (r == SHELL_SLEEP) {
            sleeping = true;
            sleep_start = timer_ticks();
            sleep_until = sleep_start + (uint64_t)session.sleep_seconds * TIMER_HZ;
            klogf("console: sleep %u s\n", (unsigned)session.sleep_seconds);
        } else if (r == SHELL_PRESSKEY) {
            awaiting_key = true;
            klogf("console: presskey: waiting for a key\n");
        } else if (r == SHELL_CAT) {        /* 1.0 stage 8: cat_step prints it */
            catting = true;
            cat_start = timer_ticks();
            shell_cat_init(&cat_text);
        }
    }
    if (r == SHELL_RUN || r == SHELL_SLEEP || r == SHELL_PRESSKEY || r == SHELL_CAT) {
        render_all();                       /* 0.11: no prompt until it ends */
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

    next_prompt();
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
    if (fs_up()) {
        /* 1.0 stage 8: the Live label (D6), on the screen and the serial
         * port (not in the kernel log); the shell starts in /home, where
         * "cd" alone goes (S8-13). */
        static const char *const live[] = { SHELL_LIVE_LINE1 "\n", SHELL_LIVE_LINE2 "\n" };
        for (uint32_t i = 0; i < 2; i++) {
            write_screen(live[i], ATTR_TITLE);
            preempt_disable();
            serial_write(live[i]);
            preempt_enable();
        }
        write_screen("\n", ATTR_TEXT);
        struct vnode *home = 0;
        fd_vfs_acquire();
        if (vfs_lookup(vfs_root(), vfs_root()->root, "/home", &home) == 0 &&
            home->type == VNODE_DIR) {
            vfs_hold(home);
            sh_cwd = home;
        }
        fd_vfs_release();
    }
    write_screen("Type 'help' for a list of commands.\n\n", ATTR_TEXT);

    vga_cursor_show(true);
    history_init(&hist);
    shell_state_init(&shell_st);
    shell_session_init(&session);
    sleeping = awaiting_key = timing = false;
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
    if (sleeping) {
        /* 0.12 stage 2: only Ctrl+C reaches "sleep" (it ends the wait). */
        if (is_ctrl_c(e)) {
            klogf("console: Ctrl+C: sleep cancelled after %u ticks\n",
                  (unsigned)(timer_ticks() - sleep_start));
            end_wait(SHELL_SLEEP_CANCELLED "\n");
        }
        return;
    }
    if (catting) {
        /* 1.0 stage 8: only Ctrl+C reaches "cat" (it stops it); other keys
         * are not typed. */
        if (is_ctrl_c(e))
            cat_finish(SHELL_CAT_CANCELLED "\n");
        return;
    }
    if (awaiting_key) {
        /* 0.12 stage 2: "presskey" ends at the next key press, which is not
         * typed; Ctrl+C cancels it. Releases and Shift, Ctrl, Alt, the lock
         * keys and the Windows keys alone are ignored. The key itself is
         * never logged. */
        if (!(e->flags & KEY_PRESSED))
            return;
        if (is_ctrl_c(e)) {
            klogf("console: Ctrl+C: presskey cancelled\n");
            end_wait(SHELL_PRESSKEY_CANCELLED "\n");
            return;
        }
        switch (e->keycode) {
        case KEY_NONE: case KEY_LSHIFT: case KEY_RSHIFT: case KEY_LCTRL: case KEY_RCTRL:
        case KEY_LALT: case KEY_RALT: case KEY_CAPSLOCK: case KEY_NUMLOCK:
        case KEY_SCROLLLOCK: case KEY_LGUI: case KEY_RGUI:
            return;
        default:
            break;
        }
        if (e->flags & KEY_CTRL)            /* Ctrl held with another key */
            return;
        klogf("console: presskey: key pressed\n");
        end_wait(0);
        return;
    }
    if (fg_pid) {
        /* 0.11: a program runs in the foreground; only Ctrl+C reaches it
         * (it ends the program). The key is never logged. */
        if (is_ctrl_c(e) && !ctrl_c_sent) {
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
    if (sleeping && timer_ticks() >= sleep_until) {     /* 0.12 stage 2 */
        klogf("console: sleep done after %u ticks\n", (unsigned)(timer_ticks() - sleep_start));
        end_wait(0);
    }
    if (catting)                            /* 1.0 stage 8: one chunk per pass */
        cat_step();
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
        next_prompt();
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
