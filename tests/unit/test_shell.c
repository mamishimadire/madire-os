/* Unit tests for kernel/shell/shell.c (command shell core). */
#include <stdint.h>
#include <string.h>
#include "kernel/shell/shell.h"
#include "tests/unit/unit.h"

static char out[4096];
static int errors;
static struct memory_stats mem;
static struct cpu_info cpu;
static struct shell_context ctx;

static void capture(void *c, const char *text, enum shell_text kind)
{
    (void)c;
    strncat(out, text, sizeof(out) - strlen(out) - 1);
    if (kind == SHELL_ERROR)
        errors++;
}

static const struct shell_output sink = { capture, NULL };

static enum shell_result run(const char *text)
{
    static char line[128];
    strcpy(line, text);
    out[0] = '\0';
    errors = 0;
    return shell_execute(line, &ctx, &sink);
}

static void reset_context(void)
{
    memset(&mem, 0, sizeof(mem));
    mem.ram_bytes = 4294573056ull;                  /* MadireOS-Test figures */
    mem.usable_bytes = 917391ull * 4096;
    mem.above_4g_bytes = 512ull << 20;
    mem.usable_pages = 917391;
    mem.reservations = 6;

    memset(&cpu, 0, sizeof(cpu));
    cpu.available = true;
    strcpy(cpu.vendor, "GenuineIntel");
    strcpy(cpu.brand, "Madire Test CPU @ 1.30GHz");
    cpu.has_signature = true;
    cpu.family = 6;
    cpu.model = 186;
    cpu.stepping = 3;
    cpu.edx_features = (1u << 0) | (1u << 4) | (1u << 26);
    cpu.ecx_features = 1u << 31;
    strcpy(cpu.hypervisor, "VBoxVBoxVBox");

    ctx = (struct shell_context){
        .version = "0.5", .timer_ok = true, .timer_hz = 100,
        .uptime_seconds = 151, .ticks = 15123,
        .mem = &mem, .free_pages_now = 917152, .page_size = 4096,
        .cpu = &cpu, .cr0 = 0x00000011,
    };
}

static void test_help_lists_all_commands(void)
{
    reset_context();
    CHECK_EQ(run("help"), SHELL_OK);
    /* 1.0 stage 10 (D2): the names, several per line; purposes: help <command>. */
    CHECK(strcmp(out,
        "Available commands:\n"
        "  help  clear  version  uptime  mem  cpu  echo\n") == 0);
    CHECK_EQ(errors, 0);
}

static void test_version(void)
{
    reset_context();
    CHECK_EQ(run("version"), SHELL_OK);
    CHECK(strcmp(out, "Madire OS v0.5 (32-bit x86, Multiboot 1)\n") == 0);
}

static void check_uptime(uint32_t seconds, uint64_t ticks, const char *expected)
{
    reset_context();
    ctx.uptime_seconds = seconds;
    ctx.ticks = ticks;
    CHECK_EQ(run("uptime"), SHELL_OK);
    CHECK(strcmp(out, expected) == 0);
    if (strcmp(out, expected) != 0)
        printf("      got: %s", out);
}

static void test_uptime_formatting(void)
{
    check_uptime(0, 0, "Uptime: 00:00:00 (0 ticks at 100 Hz)\n");
    check_uptime(59, 5900, "Uptime: 00:00:59 (5900 ticks at 100 Hz)\n");
    check_uptime(60, 6000, "Uptime: 00:01:00 (6000 ticks at 100 Hz)\n");
    check_uptime(151, 15123, "Uptime: 00:02:31 (15123 ticks at 100 Hz)\n");
    check_uptime(3599, 359900, "Uptime: 00:59:59 (359900 ticks at 100 Hz)\n");
    check_uptime(3600, 360000, "Uptime: 01:00:00 (360000 ticks at 100 Hz)\n");
    check_uptime(86399, 8639900, "Uptime: 23:59:59 (8639900 ticks at 100 Hz)\n");
    check_uptime(97384, 9738400, "Uptime: 27:03:04 (9738400 ticks at 100 Hz)\n");
    /* ticks no longer fit in 32 bits (after ~497 days): ticks omitted */
    check_uptime(42949673, 4294967300ull, "Uptime: 11930:27:53\n");
    check_uptime(4294967295u, 0xFFFFFFFFFFull, "Uptime: 1193046:28:15\n");
}

static void test_uptime_without_timer(void)
{
    reset_context();
    ctx.timer_ok = false;
    CHECK_EQ(run("uptime"), SHELL_OK);
    CHECK(strcmp(out, "Uptime: unavailable (timer not running)\n") == 0);
}

static void test_mem(void)
{
    reset_context();
    CHECK_EQ(run("mem"), SHELL_OK);
    CHECK(strcmp(out,
        "Physical RAM detected:   4096 MB\n"
        "Usable RAM (<4 GiB):     3584 MB\n"
        "RAM above 4 GiB:          512 MB\n"
        "Page size:               4096 bytes\n"
        "Usable pages:          917391\n"
        "Free pages (now):      917152\n"
        "Reserved ranges:            6\n") == 0);
    if (errors || strstr(out, "4096 MB") == NULL)
        printf("      got:\n%s", out);
}

static void test_cpu_full(void)
{
    reset_context();
    CHECK_EQ(run("cpu"), SHELL_OK);
    CHECK(strcmp(out,
        "CPUID:        supported\n"
        "Vendor:       GenuineIntel\n"
        "Brand:        Madire Test CPU @ 1.30GHz\n"
        "Family/model: 6 / 186, stepping 3\n"
        "Features:     fpu tsc sse2 hypervisor\n"
        "Hypervisor:   VBoxVBoxVBox\n"
        "Mode:         32-bit protected mode, paging off, 1 CPU in use\n") == 0);
}

static void test_cpu_minimal_and_unavailable(void)
{
    reset_context();
    cpu.brand[0] = '\0';
    cpu.hypervisor[0] = '\0';
    cpu.has_signature = false;
    run("cpu");
    CHECK(strcmp(out,
        "CPUID:        supported\n"
        "Vendor:       GenuineIntel\n"
        "Mode:         32-bit protected mode, paging off, 1 CPU in use\n") == 0);

    reset_context();
    cpu.available = false;
    run("cpu");
    CHECK(strcmp(out,
        "CPUID:        not supported (pre-586 processor)\n"
        "Mode:         32-bit protected mode, paging off, 1 CPU in use\n") == 0);

    reset_context();
    ctx.cr0 = 0x80000011u;
    run("cpu");
    CHECK(strstr(out, "paging on") != NULL);
}

static void test_echo(void)
{
    reset_context();
    CHECK_EQ(run("echo hello"), SHELL_OK);
    CHECK(strcmp(out, "hello\n") == 0);
    run("echo   Hello   World!");
    CHECK(strcmp(out, "Hello World!\n") == 0);
    run("echo");
    CHECK(strcmp(out, "\n") == 0);
    run("  echo\ta\t\tb  ");
    CHECK(strcmp(out, "a b\n") == 0);
    run("echo 1 2 3 4 5 6 7");                          /* 8 words: the maximum */
    CHECK(strcmp(out, "1 2 3 4 5 6 7\n") == 0);
}

static void test_unknown_and_case(void)
{
    reset_context();
    CHECK_EQ(run("foo"), SHELL_UNKNOWN);
    CHECK(strcmp(out, "Unknown command: foo. Type 'help' for a list of commands.\n") == 0);
    CHECK_EQ(errors, 1);
    CHECK_EQ(run("HELP"), SHELL_UNKNOWN);               /* case-sensitive */
    CHECK_EQ(run("ECHO ABC"), SHELL_UNKNOWN);
    CHECK(strcmp(out, "Unknown command: ECHO. Type 'help' for a list of commands.\n") == 0);
    CHECK_EQ(run("helpx"), SHELL_UNKNOWN);              /* no prefix matching */
    CHECK_EQ(run("hel"), SHELL_UNKNOWN);
}

static void test_argument_errors(void)
{
    reset_context();
    const char *cmds[] = { "help x", "clear x", "version x", "uptime 5", "mem all", "cpu -v" };
    const char *usage[] = { "Usage: help\n", "Usage: clear\n", "Usage: version\n",
                            "Usage: uptime\n", "Usage: mem\n", "Usage: cpu\n" };
    for (int i = 0; i < 6; i++) {
        CHECK_EQ(run(cmds[i]), SHELL_BAD_ARGS);
        CHECK(strcmp(out, usage[i]) == 0);
        CHECK_EQ(errors, 1);
    }
    CHECK_EQ(run("echo 1 2 3 4 5 6 7 8"), SHELL_TOO_MANY);
    CHECK(strcmp(out, "Too many arguments (max 8).\n") == 0);
}

static void test_empty_and_clear(void)
{
    reset_context();
    CHECK_EQ(run(""), SHELL_EMPTY);
    CHECK_EQ(out[0], 0);
    CHECK_EQ(run("   \t "), SHELL_EMPTY);
    CHECK_EQ(out[0], 0);
    CHECK_EQ(run("clear"), SHELL_CLEAR);
    CHECK_EQ(out[0], 0);
    CHECK_EQ(run("  clear  "), SHELL_CLEAR);
}

static void test_repeated_commands(void)
{
    char first[512];
    reset_context();
    run("mem");
    strcpy(first, out);
    for (int i = 0; i < 5; i++) {
        run("mem");
        CHECK(strcmp(out, first) == 0);
    }
}

int main(void)
{
    printf("shell unit tests\n");
    RUN(test_help_lists_all_commands);
    RUN(test_version);
    RUN(test_uptime_formatting);
    RUN(test_uptime_without_timer);
    RUN(test_mem);
    RUN(test_cpu_full);
    RUN(test_cpu_minimal_and_unavailable);
    RUN(test_echo);
    RUN(test_unknown_and_case);
    RUN(test_argument_errors);
    RUN(test_empty_and_clear);
    RUN(test_repeated_commands);
    return unit_report("shell");
}
