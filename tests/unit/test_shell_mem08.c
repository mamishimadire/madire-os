/* Unit tests for the 0.8 "mem" row (kernel stack high-water mark) of
 * kernel/shell/shell.c. The 0.5 and 0.6 output is pinned by test_shell.c
 * and test_shell_mem06.c; without the stack fields it is unchanged. */
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
    mem.usable_pages = 917391;
    mem.reservations = 5;
    memset(&cpu, 0, sizeof(cpu));
    paging = (struct paging_stats){ .enabled = true, .nx_supported = true, .nx_enabled = true,
                                    .nx = true, .mapped_pages = 361, .page_tables = 9 };
    heap = (struct heap_stats){ .arena_bytes = 176 * 1024, .free_bytes = 176 * 1024,
                                .largest_free = 176 * 1024 - 16 };
    ctx = (struct shell_context){
        .version = "0.8", .timer_ok = true, .timer_hz = 100,
        .mem = &mem, .free_pages_now = 917070, .page_size = 4096,
        .cpu = &cpu, .cr0 = 0x80010011, .paging = &paging, .heap = &heap,
    };
}

static void test_no_stack_fields_no_row(void)
{
    reset();
    run_mem();
    CHECK(strstr(out, "Kernel stack") == NULL);
    CHECK(strstr(out, "Heap free:") != NULL);
}

static void test_stack_row_exact_and_last(void)
{
    reset();
    ctx.stack_size = 16384;
    ctx.stack_peak = 5120;
    run_mem();
    const char *want = "Kernel stack:            5120 bytes at peak, of 16384\n";
    const char *at = strstr(out, want);
    CHECK(at != NULL);
    CHECK(at != NULL && strcmp(at, want) == 0);         /* the last row */
    CHECK(strlen(want) - 1 <= 79);                      /* fits the screen */
}

static void test_stack_row_edges(void)
{
    reset();
    ctx.stack_size = 16384;
    ctx.stack_peak = 0;
    run_mem();
    CHECK(strstr(out, "Kernel stack:               0 bytes at peak, of 16384\n") != NULL);
    ctx.stack_peak = 16384;
    run_mem();
    CHECK(strstr(out, "Kernel stack:           16384 bytes at peak, of 16384\n") != NULL);
    ctx.stack_peak = 123456;                            /* wider than 6 digits */
    run_mem();
    CHECK(strstr(out, "Kernel stack:          123456 bytes at peak, of 16384\n") != NULL);
}

static void test_every_mem_line_fits(void)
{
    reset();
    ctx.stack_size = 16384;
    ctx.stack_peak = 16384;
    run_mem();
    unsigned longest = 0, n = 0;
    for (const char *p = out; *p; p++) {
        if (*p == '\n') {
            if (n > longest)
                longest = n;
            n = 0;
        } else {
            n++;
        }
    }
    CHECK(longest <= 79);
}

int main(void)
{
    printf("shell mem (0.8) unit tests\n");
    RUN(test_no_stack_fields_no_row);
    RUN(test_stack_row_exact_and_last);
    RUN(test_stack_row_edges);
    RUN(test_every_mem_line_fits);
    return unit_report("shell_mem08");
}
