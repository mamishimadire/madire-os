/* Unit tests for the 0.6 "mem" sections (paging and kernel heap) of
 * kernel/shell/shell.c. The 0.5 output itself is pinned by test_shell.c. */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "kernel/shell/shell.h"
#include "tests/unit/unit.h"

static char out[4096];
static struct memory_stats mem;
static struct cpu_info cpu;
static struct paging_stats paging;
static struct heap_stats heap;
static struct shell_context ctx;

static void capture(void *c, const char *text, enum shell_text kind)
{
    (void)c;
    (void)kind;
    strncat(out, text, sizeof(out) - strlen(out) - 1);
}

static const struct shell_output sink = { capture, NULL };

static void run_mem(void)
{
    static char line[8];
    strcpy(line, "mem");
    out[0] = '\0';
    CHECK_EQ(shell_execute(line, &ctx, &sink), SHELL_OK);
}

static void reset(void)
{
    memset(&mem, 0, sizeof(mem));
    mem.ram_bytes = 4294573056ull;
    mem.usable_bytes = 917391ull * 4096;
    mem.above_4g_bytes = 512ull << 20;
    mem.usable_pages = 917391;
    mem.reservations = 5;
    memset(&cpu, 0, sizeof(cpu));
    paging = (struct paging_stats){ .enabled = true, .cr3 = 0x158000, .mapped_pages = 343,
                                    .page_tables = 3, .kernel_ro_pages = 15 };
    heap = (struct heap_stats){ .arena_bytes = 176 * 1024, .used_bytes = 0,
                                .free_bytes = 176 * 1024, .largest_free = 176 * 1024 - 16,
                                .live_blocks = 0 };
    ctx = (struct shell_context){
        .version = "0.6", .timer_ok = true, .timer_hz = 100,
        .mem = &mem, .free_pages_now = 917097, .page_size = 4096,
        .cpu = &cpu, .cr0 = 0x80010011,
    };
}

/* "label" padded to column 23, then text: the shell's row format. */
static void row(char *buf, size_t size, const char *label, const char *text)
{
    snprintf(buf + strlen(buf), size - strlen(buf), "%-23s%s\n", label, text);
}

static void test_without_pointers_unchanged(void)
{
    char base[2048];
    reset();
    run_mem();
    strcpy(base, out);
    CHECK(strstr(base, "Paging") == NULL);
    CHECK(strstr(base, "heap") == NULL && strstr(base, "Heap") == NULL);
    CHECK(strstr(base, "Reserved ranges:            5\n") != NULL);

    /* With both sections, the 0.5 lines come first, exactly the same. */
    ctx.paging = &paging;
    ctx.heap = &heap;
    run_mem();
    CHECK(strncmp(out, base, strlen(base)) == 0);
    CHECK(strlen(out) > strlen(base));
}

static void test_exact_extra_lines(void)
{
    char base[2048], expected[4096];
    reset();
    run_mem();
    strcpy(base, out);
    ctx.paging = &paging;
    ctx.heap = &heap;
    run_mem();

    strcpy(expected, base);
    row(expected, sizeof(expected), "Paging:", "enabled (4-level, 4 KiB, higher-half, NX off)");
    row(expected, sizeof(expected), "Mapped pages:", "   343");
    row(expected, sizeof(expected), "Page tables:", "     3");
    row(expected, sizeof(expected), "NX:", "not supported by this CPU");
    row(expected, sizeof(expected), "Kernel heap:", "   176 KiB mapped, max 16384 KiB");
    row(expected, sizeof(expected), "Heap in use:", "     0 KiB in 0 blocks");
    row(expected, sizeof(expected), "Heap free:", "   176 KiB, largest block 175 KiB");
    CHECK(strcmp(out, expected) == 0);
    if (strcmp(out, expected) != 0)
        printf("--- got:\n%s--- expected:\n%s", out, expected);
    /* One line spelled out in full, so the format itself is pinned too. */
    CHECK(strstr(out, "\nMapped pages:             343\n") != NULL);
    /* 0.7: with no-execute bits in use the paging line says so, and the NX
     * row tells supported / enabled apart. */
    paging.nx_supported = paging.nx_enabled = paging.nx = true;
    run_mem();
    char want[160] = "\n";
    row(want, sizeof(want), "Paging:", "enabled (4-level, 4 KiB, higher-half, NX on)");
    CHECK(strstr(out, want) != NULL);
    strcpy(want, "\n");
    row(want, sizeof(want), "NX:", "supported, enabled");
    CHECK(strstr(out, want) != NULL);
    paging.nx_enabled = paging.nx = false;          /* supported, but EFER.NXE not set */
    run_mem();
    strcpy(want, "\n");
    row(want, sizeof(want), "NX:", "supported, not enabled");
    CHECK(strstr(out, want) != NULL);
    strcpy(want, "\n");
    row(want, sizeof(want), "Paging:", "enabled (4-level, 4 KiB, higher-half, NX off)");
    CHECK(strstr(out, want) != NULL);
}

static void test_paging_disabled(void)
{
    reset();
    paging.enabled = false;
    ctx.paging = &paging;
    run_mem();
    CHECK(strstr(out, "\nPaging:                disabled\n") != NULL);
    CHECK(strstr(out, "Mapped pages:") == NULL);
    CHECK(strstr(out, "Page tables:") == NULL);
    CHECK(strstr(out, "Kernel heap:") == NULL);          /* heap pointer not set */
}

static void test_heap_disabled(void)
{
    reset();
    memset(&heap, 0, sizeof(heap));                     /* arena_bytes == 0 */
    ctx.paging = &paging;
    ctx.heap = &heap;
    run_mem();
    CHECK(strstr(out, "\nKernel heap:           disabled\n") != NULL);
    CHECK(strstr(out, "Heap in use:") == NULL);
    CHECK(strstr(out, "Heap free:") == NULL);
    CHECK(strstr(out, "Paging:                enabled") != NULL);
}

static void test_heap_in_use_and_singular(void)
{
    reset();
    heap.used_bytes = 4096 + 16;
    heap.free_bytes = heap.arena_bytes - heap.used_bytes;
    heap.largest_free = heap.free_bytes - 16;
    heap.live_blocks = 1;
    ctx.heap = &heap;
    run_mem();
    char want[256] = "\n";
    row(want, sizeof(want), "Heap in use:", "     4 KiB in 1 block");
    CHECK(strstr(out, want) != NULL);
    strcpy(want, "\n");
    row(want, sizeof(want), "Heap free:", "   171 KiB, largest block 171 KiB");
    CHECK(strstr(out, want) != NULL);
    heap.arena_bytes = 16u * 1024 * 1024;               /* at the cap */
    run_mem();
    strcpy(want, "\n");
    row(want, sizeof(want), "Kernel heap:", " 16384 KiB mapped, max 16384 KiB");
    CHECK(strstr(out, want) != NULL);
    if (!strstr(out, want))
        printf("--- got:\n%s", out);
}

int main(void)
{
    printf("shell mem (0.6) unit tests\n");
    RUN(test_without_pointers_unchanged);
    RUN(test_exact_extra_lines);
    RUN(test_paging_disabled);
    RUN(test_heap_disabled);
    RUN(test_heap_in_use_and_singular);
    return unit_report("shell_mem06");
}
