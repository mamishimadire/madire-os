/* Unit tests for 1.2 stage 10b of kernel/shell/shell.c: "display", read
 * only (decision 16). With the context's display set, "display" shows the
 * screen's mode and the console's scale (a framebuffer) or "VGA text
 * 80x25", takes no argument, has its help, is listed in the section
 * "system" after "reboot", completes with Tab and is a built-in name for
 * "alias". Without it, everything is exactly as before (the other suites,
 * whose contexts have no display, prove that too). */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "kernel/shell/shell.h"
#include "tests/unit/unit.h"

static char out[8192];
static int errors;
static struct memory_stats mem;
static struct cpu_info cpu;
static struct shell_context ctx;
static struct shell_extras extras;
static struct shell_procs procs;
static struct shell_state st;
static struct shell_session ss;
static struct shell_files files;
static struct shell_storage storage;
static struct shell_display disp;
static struct history hist;
static char file[8192];
static size_t file_len;

static void capture(void *c, const char *text, enum shell_text kind)
{
    (void)c;
    if (kind == SHELL_ERROR)
        errors++;
    strncat(out, text, sizeof(out) - strlen(out) - 1);
}

static const struct shell_output sink = { capture, NULL };

static enum shell_result run(const char *cmd)
{
    static char line[512];
    strncpy(line, cmd, sizeof(line) - 1);
    line[sizeof(line) - 1] = '\0';
    out[0] = '\0';
    errors = 0;
    return shell_execute(line, &ctx, &sink);
}

static int32_t fake_start(void *c, int argc, char **argv, uint32_t envc,
                          const char *const *env_names, const char *const *env_values,
                          bool background)
{
    (void)c; (void)argc; (void)argv; (void)envc; (void)env_names; (void)env_values;
    (void)background;
    return 7;
}

static void fake_ask_kill(void *c, int32_t pid)
{
    (void)c; (void)pid;
}

static bool fake_removed(void *c) { (void)c; return false; }

static int fake_out_open(void *c, const char *path, bool append)
{
    (void)c; (void)path; (void)append;
    file_len = 0;
    return 0;
}

static int64_t fake_out_write(void *c, const uint8_t *buf, uint32_t len)
{
    (void)c;
    if (file_len + len > sizeof(file))
        len = (uint32_t)(sizeof(file) - file_len);
    memcpy(file + file_len, buf, len);
    file_len += len;
    return len;
}

static void fake_out_close(void *c) { (void)c; }
static bool fake_out_is(void *c, const char *path) { (void)c; (void)path; return false; }

static const struct shell_thread one_thread[] = { { 0, "running", 5, 1480, 16384, "kernel" } };
static const struct shell_process one_process[] = { { 5, "running", 1, "spin" } };

/* The console's context (every optional part), with or without a display. */
static void reset(bool with_display)
{
    memset(&mem, 0, sizeof(mem));
    memset(&cpu, 0, sizeof(cpu));
    memset(&ctx, 0, sizeof(ctx));
    memset(&extras, 0, sizeof(extras));
    memset(&procs, 0, sizeof(procs));
    memset(&files, 0, sizeof(files));
    memset(&storage, 0, sizeof(storage));
    memset(&disp, 0, sizeof(disp));
    history_init(&hist);
    ctx.version = "1.2";
    ctx.mem = &mem;
    ctx.cpu = &cpu;
    ctx.page_size = 4096;
    ctx.threads = one_thread;
    ctx.thread_count = 1;
    extras.hostname = "madire";
    extras.history = &hist;
    ctx.extras = &extras;
    procs.rows = one_process;
    procs.count = 1;
    procs.start = fake_start;
    procs.ask_kill = fake_ask_kill;
    ctx.procs = &procs;
    shell_state_init(&st);
    ctx.state = &st;
    shell_session_init(&ss);
    ctx.session = &ss;
    files.cwd_removed = fake_removed;
    ctx.files = &files;
    storage.out_open = fake_out_open;
    storage.out_write = fake_out_write;
    storage.out_close = fake_out_close;
    storage.out_is = fake_out_is;
    ctx.storage = &storage;
    if (with_display)
        ctx.display = &disp;
}

static void framebuffer(uint32_t w, uint32_t h, uint32_t bpp, uint32_t scale, uint32_t cw, uint32_t ch)
{
    disp.framebuffer = true;
    disp.width = w;
    disp.height = h;
    disp.bpp = bpp;
    disp.scale = scale;
    disp.cell_w = cw;
    disp.cell_h = ch;
}

/* ---- without a display: no such command ---- */

static void test_without_display(void)
{
    reset(false);
    CHECK_EQ(run("display"), SHELL_UNKNOWN);
    CHECK(strncmp(out, "Unknown command: display. ", 26) == 0);
    CHECK_EQ(run("help display"), SHELL_UNKNOWN);
    CHECK(strncmp(out, "No help for 'display': unknown command.\n", 40) == 0);
    CHECK_EQ(run("help system"), SHELL_OK);
    CHECK(strstr(out, "display") == NULL);
}

/* ---- the outputs ---- */

static void test_vga_text(void)
{
    reset(true);
    CHECK_EQ(run("display"), SHELL_OK);
    CHECK(strcmp(out, "Screen:  VGA text 80x25\n") == 0);
    CHECK_EQ(errors, 0);
}

static void test_framebuffer(void)
{
    reset(true);
    framebuffer(1024, 768, 32, 100, 8, 16);
    CHECK_EQ(run("display"), SHELL_OK);
    CHECK(strcmp(out, "Screen:  framebuffer 1024x768, 32 bits per pixel\n"
                      "Scale:   100 % (console: 80x25 cells of 8x16 pixels)\n") == 0);
    CHECK_EQ(errors, 0);

    framebuffer(800, 600, 24, 100, 8, 16);
    CHECK_EQ(run("display"), SHELL_OK);
    CHECK(strcmp(out, "Screen:  framebuffer 800x600, 24 bits per pixel\n"
                      "Scale:   100 % (console: 80x25 cells of 8x16 pixels)\n") == 0);

    framebuffer(1920, 1080, 32, 150, 12, 24);
    CHECK_EQ(run("display"), SHELL_OK);
    CHECK(strcmp(out, "Screen:  framebuffer 1920x1080, 32 bits per pixel\n"
                      "Scale:   150 % (console: 80x25 cells of 12x24 pixels)\n") == 0);

    framebuffer(2560, 1440, 32, 200, 16, 32);
    CHECK_EQ(run("display"), SHELL_OK);
    CHECK(strcmp(out, "Screen:  framebuffer 2560x1440, 32 bits per pixel\n"
                      "Scale:   200 % (console: 80x25 cells of 16x32 pixels)\n") == 0);
    CHECK(strlen("Scale:   200 % (console: 80x25 cells of 16x32 pixels)") <= 79);
}

/* ---- usage, help, the help lists ---- */

static void test_usage_and_help(void)
{
    reset(true);
    CHECK_EQ(run("display 1024x768"), SHELL_BAD_ARGS);
    CHECK(strcmp(out, "Usage: display\n") == 0);
    CHECK_EQ(errors, 1);
    CHECK_EQ(run("help display"), SHELL_OK);
    CHECK(strcmp(out, "display: Show the screen's mode and scale\nUsage: display\n") == 0);
    CHECK_EQ(run("display --help"), SHELL_OK);
    CHECK(strcmp(out, "display: Show the screen's mode and scale\nUsage: display\n") == 0);

    CHECK_EQ(run("help"), SHELL_OK);
    CHECK(strstr(out, "system: Information about the system\n"
                      "  version  uptime  mem  cpu  uname  hostname  date  dmesg  reboot  display\n"
                      "programs: Programs and processes\n") != NULL);
    CHECK_EQ(run("help system"), SHELL_OK);
    CHECK(strstr(out, "  reboot   Restart the computer (asks first)\n"
                      "  display  Show the screen's mode and scale\n") != NULL);
    const char *last = strstr(out, "  display  ");
    CHECK(last != NULL && strchr(last, '\n') == out + strlen(out) - 1);
}

/* ---- Tab, alias, redirection ---- */

static void test_complete_alias_redirect(void)
{
    struct shell_completion c;
    reset(true);
    out[0] = '\0';
    shell_complete(&ctx, "di", 69, &c, &sink);
    CHECK_EQ(c.count, 1);
    CHECK(strcmp(c.add, "splay ") == 0);

    CHECK_EQ(run("alias display echo x"), SHELL_BAD_ARGS);
    CHECK(strcmp(out, "alias: 'display' is a built-in command.\n") == 0);
    CHECK_EQ(run("alias scr display"), SHELL_OK);
    CHECK_EQ(run("scr"), SHELL_OK);
    CHECK(strcmp(out, "Screen:  VGA text 80x25\n") == 0);

    framebuffer(1024, 768, 32, 100, 8, 16);
    file_len = 0;
    CHECK_EQ(run("display > d.txt"), SHELL_OK);
    CHECK(strcmp(out, "") == 0);
    CHECK_EQ(file_len, strlen("Screen:  framebuffer 1024x768, 32 bits per pixel\n"
                              "Scale:   100 % (console: 80x25 cells of 8x16 pixels)\n"));
    CHECK(memcmp(file, "Screen:  framebuffer 1024x768, 32 bits per pixel\n"
                       "Scale:   100 % (console: 80x25 cells of 8x16 pixels)\n", file_len) == 0);
}

int main(void)
{
    printf("test_shell_display12\n");
    RUN(test_without_display);
    RUN(test_vga_text);
    RUN(test_framebuffer);
    RUN(test_usage_and_help);
    RUN(test_complete_alias_redirect);
    return unit_report("shell_display12");
}
