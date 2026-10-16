/* The System V start-up diagnostic (see starttest.h). */
#include <stddef.h>
#include <stdint.h>
#include "kernel/kprintf.h"
#include "kernel/memory/kmalloc.h"
#include "kernel/proc/diagwait.h"
#include "kernel/proc/loadplan.h"
#include "kernel/proc/preempt.h"
#include "kernel/proc/process.h"
#include "kernel/proc/starttest.h"
#include "kernel/proc/thread.h"
#include "kernel/timer.h"

#define ST_WAIT   300u                  /* ticks allowed for any one wait */
#define ST_KILLS  20u
#define LONG_ARG  255u

static const char *const tag = "Startup test";
static unsigned bad_lines;
static uint64_t wait_longest;

static bool line(bool ok, const char *text)
{
    klogf("%s: %s: %s\n", tag, text, ok ? "OK" : "MISMATCH");
    if (!ok)
        bad_lines++;
    return ok;
}

/* Thread 0 yields until process `pid` has ended, at most ST_WAIT ticks;
 * the longest wait is reported with its margin. Then collects it. */
static struct proc_result wait_end(int32_t pid, bool *ok)
{
    struct proc_result res = { PROC_END_FAULT, -1 };
    *ok = false;
    if (pid <= 0)
        return res;
    uint64_t t0 = timer_ticks();
    bool in_time = true;
    while (!process_ended(pid)) {
        if (timer_ticks() - t0 > ST_WAIT) {
            in_time = false;
            break;
        }
        thread_yield();
    }
    uint64_t w = timer_ticks() - t0;
    if (w > wait_longest)
        wait_longest = w;
    *ok = in_time && process_wait(pid, &res) == PROC_OK;
    return res;
}

struct heap_snap {
    uint32_t used, blocks;
};

static void snap(struct heap_snap *s)
{
    while (thread_reap())
        ;
    const struct heap_stats *h = kheap_stats();
    s->used = h->used_bytes;
    s->blocks = h->live_blocks;
}

/* One run of args: its result line (exit status 0: nothing wrong). */
static bool run_case(const char *what, bool module, uint32_t argc, const char *const *argv,
                     uint32_t envc, const char *const *names, const char *const *values)
{
    int32_t pid = process_start_argv("args", module, argc, argv, envc, names, values);
    bool ok;
    struct proc_result res = wait_end(pid, &ok);
    char text[200];
    ksnprintf(text, sizeof(text), "case %s: process %d %s, status %d (expected 0)", what,
              (int)pid, proc_end_str(res.end), (int)res.code);
    return line(ok && res.end == PROC_END_EXIT && res.code == 0, text);
}

/* True if argv { "args", <n bytes of big> } fits the start block. big
 * holds STARTBLOCK_SIZE 'b's; its byte n becomes the end for the test. */
static bool fits_with(struct start_block *b, char *big, uint32_t n)
{
    const char *argv[2] = { "args", big };
    big[n] = '\0';
    bool ok = startblock_strings(b, "args", 2, argv, 0, 0, 0) == 0;
    big[n] = 'b';
    return ok;
}

/* The longest single argument with which argv { "args", <it> } still fits
 * the 4096-byte start block (its block is then exactly 4096 bytes): the
 * fit only gets worse as the argument grows, so a binary search. */
static uint32_t largest_arg(struct start_block *b, char *big)
{
    for (uint32_t i = 0; i < STARTBLOCK_SIZE; i++)
        big[i] = 'b';
    uint32_t lo = 0, hi = STARTBLOCK_SIZE;          /* fits(lo), not fits(hi) */
    while (hi - lo > 1) {
        uint32_t mid = lo + (hi - lo) / 2;
        if (fits_with(b, big, mid))
            lo = mid;
        else
            hi = mid;
    }
    return lo;
}

bool starttest_startup(void)
{
    bad_lines = 0;
    wait_longest = 0;
    struct heap_snap h0, h1;
    snap(&h0);
    uint32_t live0 = process_live(), blocks0 = process_start_blocks_live();

    static const char *const four[] = { "args", "one", "two", "three" };
    static const char *const names[] = { "HOME", "X" };
    static const char *const values[] = { "/home", "1" };
    run_case("1 (built-in, argv 'args' 'one' 'two' 'three', envp 'HOME=/home' 'X=1')", false, 4,
             four, 2, names, values);
    run_case("2 (module, argv 'args' 'one' 'two' 'three', envp 'HOME=/home' 'X=1')", true, 4,
             four, 2, names, values);
    static const char *const one[] = { "args" };
    run_case("3 (built-in, argv 'args', no environment)", false, 1, one, 0, 0, 0);

    /* An empty argument and one of 255 bytes ('a' to 'z' over and over). */
    char *big = kmalloc(STARTBLOCK_SIZE + 1);
    struct start_block *b = kmalloc(sizeof(*b));
    if (!big || !b) {
        line(false, "no memory for the test's own buffers");
        kfree(big);
        kfree(b);
        klogf("%s: FAIL\n", tag);
        return false;
    }
    for (uint32_t i = 0; i < LONG_ARG; i++)
        big[i] = (char)('a' + i % 26);
    big[LONG_ARG] = '\0';
    const char *edge[3] = { "args", "", big };
    run_case("4 (built-in, argv 'args' '' and 255 bytes)", false, 3, edge, 0, 0, 0);

    /* The largest start block: exactly 4096 bytes; then one byte more. */
    uint32_t n = largest_arg(b, big);
    const char *argv[2] = { "args", big };
    big[n] = '\0';
    bool fits = n > 0 && startblock_strings(b, "args", 2, argv, 0, 0, 0) == 0 &&
                startblock_bytes(b) == STARTBLOCK_SIZE;
    int32_t pid = process_start_argv("args", false, 2, argv, 0, 0, 0);
    bool ok;
    struct proc_result res = wait_end(pid, &ok);
    char text[260];
    ksnprintf(text, sizeof(text), "a start block of exactly %u bytes (argv 'args' and %u bytes): "
              "process %d %s, status %d (expected 0)", STARTBLOCK_SIZE, n, (int)pid,
              proc_end_str(res.end), (int)res.code);
    line(fits && ok && res.end == PROC_END_EXIT && res.code == 0, text);

    struct heap_snap e0, e1;
    snap(&e0);
    uint32_t live_e0 = process_live();
    big[n] = 'b';
    big[n + 1] = '\0';
    int32_t r = process_start_argv("args", false, 2, argv, 0, 0, 0);
    snap(&e1);
    uint32_t live_e1 = process_live();
    ksnprintf(text, sizeof(text), "one byte more (%u): refused with %d (E2BIG), processes %u -> "
              "%u, heap %u -> %u bytes, %u -> %u blocks, start blocks live %u", n + 1, (int)r,
              live_e0, live_e1, e0.used, e1.used, e0.blocks, e1.blocks,
              process_start_blocks_live());
    line(r == PROCESS_START_E2BIG && live_e1 == live_e0 && e1.used == e0.used &&
         e1.blocks == e0.blocks && process_start_blocks_live() == blocks0, text);
    kfree(big);
    kfree(b);

    /* A numeric start: argv { "args", "7" }. */
    pid = process_start("args", 7);
    res = wait_end(pid, &ok);
    ksnprintf(text, sizeof(text), "case 5 (numeric start, process_start 'args' 7): process %d %s, "
              "status %d (expected 0)", (int)pid, proc_end_str(res.end), (int)res.code);
    line(ok && res.end == PROC_END_EXIT && res.code == 0, text);

    /* Killed before ring 3: the kill is requested before the new thread
     * first runs, so the process ends where it checks, after loading and
     * before enter_user (its start block already copied and freed). */
    uint32_t killed = 0, no_returns = 0;
    for (uint32_t i = 0; i < ST_KILLS; i++) {
        preempt_disable();
        pid = process_start("args", 0);
        bool asked = pid > 0 && process_kill(pid) == PROC_OK;
        preempt_enable();
        res = wait_end(pid, &ok);
        if (asked && ok && res.end == PROC_END_KILLED)
            killed++;
        if (pid > 0 && process_user_returns(pid) == 0)
            no_returns++;
    }
    snap(&h1);
    ksnprintf(text, sizeof(text), "%u starts killed before ring 3: %u killed, %u with no return "
              "to ring 3; start blocks live %u -> %u; heap %u -> %u bytes, %u -> %u blocks; "
              "processes %u -> %u", ST_KILLS, killed, no_returns, blocks0,
              process_start_blocks_live(), h0.used, h1.used, h0.blocks, h1.blocks, live0,
              process_live());
    line(killed == ST_KILLS && no_returns == ST_KILLS && process_start_blocks_live() == blocks0 &&
         h1.used == h0.used && h1.blocks == h0.blocks && process_live() == live0, text);

    uint64_t margin = wait_longest < ST_WAIT ? (ST_WAIT - wait_longest) * 100 / ST_WAIT : 0;
    klogf("%s: waits: the longest bounded wait %llu ticks of its %u-tick limit (margin %llu%%)\n",
          tag, (unsigned long long)wait_longest, ST_WAIT, (unsigned long long)margin);
    bool pass = bad_lines == 0 && diag_wait_failures() == 0;
    klogf("%s: %s\n", tag, pass ? "PASS" : "FAIL");
    return pass;
}
