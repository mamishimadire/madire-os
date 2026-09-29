/* Unit tests for the 0.7 "cpu" mode line (64-bit long mode) and the 0.7
 * "version" wording of kernel/shell/shell.c. Both follow EFER.LMA. The
 * 32-bit wording (no EFER in the context) is pinned by test_shell.c. */
#include <stdint.h>
#include <string.h>
#include "kernel/shell/shell.h"
#include "tests/unit/unit.h"

static char out[4096];
static struct memory_stats mem;
static struct cpu_info cpu;
static struct shell_context ctx;

static void capture(void *c, const char *text, enum shell_text kind)
{
    (void)c;
    (void)kind;
    strncat(out, text, sizeof(out) - strlen(out) - 1);
}

static const struct shell_output sink = { capture, NULL };

static const char *mode_line(uint32_t cr0, uint64_t efer)
{
    static char line[16];
    memset(&cpu, 0, sizeof(cpu));                   /* CPUID unavailable: short output */
    memset(&mem, 0, sizeof(mem));
    ctx = (struct shell_context){ .version = "0.7", .mem = &mem, .cpu = &cpu,
                                  .cr0 = cr0, .efer = efer };
    strcpy(line, "cpu");
    out[0] = '\0';
    CHECK_EQ(shell_execute(line, &ctx, &sink), SHELL_OK);
    const char *m = strstr(out, "Mode:");
    return m ? m : "";
}

static void test_long_mode(void)
{
    /* EFER = 0xd00: LME, LMA, NXE, as measured in QEMU and VirtualBox. */
    CHECK(strcmp(mode_line(0x80010011u, 0xd00u),
                 "Mode:         64-bit long mode, paging on, 1 CPU in use\n") == 0);
    CHECK(strcmp(mode_line(0x80010011u, 0x500u),                  /* no NX */
                 "Mode:         64-bit long mode, paging on, 1 CPU in use\n") == 0);
}

static void test_lme_without_lma_is_not_long_mode(void)
{
    /* LME set but LMA clear: long mode enabled, not yet active. */
    CHECK(strcmp(mode_line(0x00000011u, 0x100u),
                 "Mode:         32-bit protected mode, paging off, 1 CPU in use\n") == 0);
}

static void test_efer_zero_keeps_32_bit_wording(void)
{
    CHECK(strcmp(mode_line(0x80000011u, 0),
                 "Mode:         32-bit protected mode, paging on, 1 CPU in use\n") == 0);
    CHECK(strcmp(mode_line(0x00000011u, 0),
                 "Mode:         32-bit protected mode, paging off, 1 CPU in use\n") == 0);
    CHECK(strcmp(mode_line(0x00000010u, 0),
                 "Mode:         real mode, paging off, 1 CPU in use\n") == 0);
}

/* Runs `command` with a 0.7 context holding `efer`; returns the output. */
static const char *run_with_efer(const char *command, uint64_t efer)
{
    static char line[16];
    memset(&cpu, 0, sizeof(cpu));
    memset(&mem, 0, sizeof(mem));
    ctx = (struct shell_context){ .version = "0.7", .mem = &mem, .cpu = &cpu,
                                  .cr0 = 0x80010011u, .efer = efer };
    strcpy(line, command);
    out[0] = '\0';
    shell_execute(line, &ctx, &sink);
    return out;
}

static void test_version_long_mode(void)
{
    CHECK(strcmp(run_with_efer("version", 0xd00u),
                 "Madire OS v0.7 (64-bit x86-64, Multiboot 1)\n") == 0);
    CHECK(strcmp(run_with_efer("version", 0x500u),                /* no NX */
                 "Madire OS v0.7 (64-bit x86-64, Multiboot 1)\n") == 0);
}

static void test_version_lme_without_lma(void)
{
    CHECK(strcmp(run_with_efer("version", 0x100u),
                 "Madire OS v0.7 (32-bit x86, Multiboot 1)\n") == 0);
}

static void test_version_efer_zero(void)
{
    CHECK(strcmp(run_with_efer("version", 0),
                 "Madire OS v0.7 (32-bit x86, Multiboot 1)\n") == 0);
}

/* "version" says 64-bit exactly when "cpu" says long mode. */
static void test_version_matches_cpu_mode(void)
{
    static const uint64_t efers[] = { 0xd00u, 0x500u, 0x100u, 0 };
    for (unsigned i = 0; i < sizeof(efers) / sizeof(efers[0]); i++) {
        bool version64 = strstr(run_with_efer("version", efers[i]), "64-bit x86-64") != NULL;
        bool cpu64 = strstr(run_with_efer("cpu", efers[i]), "64-bit long mode") != NULL;
        CHECK_EQ(version64, cpu64);
    }
}

static void test_version_usage_unchanged(void)
{
    CHECK(strcmp(run_with_efer("version x", 0xd00u), "Usage: version\n") == 0);
}

int main(void)
{
    printf("shell cpu (0.7) unit tests\n");
    RUN(test_long_mode);
    RUN(test_lme_without_lma_is_not_long_mode);
    RUN(test_efer_zero_keeps_32_bit_wording);
    RUN(test_version_long_mode);
    RUN(test_version_lme_without_lma);
    RUN(test_version_efer_zero);
    RUN(test_version_matches_cpu_mode);
    RUN(test_version_usage_unchanged);
    return unit_report("shell_cpu07");
}
