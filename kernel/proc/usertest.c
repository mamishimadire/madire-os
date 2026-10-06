/* User-mode diagnostics (see usertest.h). */
#include <stdint.h>
#include "kernel/arch/x86/cpuprot.h"
#include "kernel/arch/x86/gdt.h"
#include "kernel/arch/x86/interrupts.h"
#include "kernel/kprintf.h"
#include "kernel/proc/thread.h"
#include "kernel/proc/usertest.h"
#include "kernel/timer.h"

/* ---- test=rsp0 ---- */

#define RSP0_WORKERS 3
#define RSP0_TICKS   40             /* long enough for several preemptions each */

static volatile uint32_t rsp0_checks, rsp0_mismatches;
static volatile bool rsp0_stop;
static uint64_t rsp0_seen[RSP0_WORKERS + 1];

/* Checks RSP0 against the running thread's own stack top; records the
 * value per thread (index 0 = thread 0). */
static void rsp0_check(int index)
{
    uint64_t want = thread_kstack_top(thread_current());
    uint64_t got = gdt_rsp0();
    rsp0_checks++;
    if (got != want || want == 0)
        rsp0_mismatches++;
    rsp0_seen[index] = got;
}

static int32_t rsp0_worker(void *arg)
{
    int index = (int)(intptr_t)arg;
    uint32_t round = 0;
    while (!rsp0_stop) {
        rsp0_check(index);
        volatile uint32_t work = 0;         /* real work: TCG delivers interrupts between blocks */
        for (uint32_t i = 0; i < 4000; i++)
            work += i;
        /* Worker 1 never yields: it runs out its slice and is preempted
         * (switches from an interrupt). Workers 2 and 3 also yield (the
         * cooperative path). */
        if (index != 1 && ++round % 8 == 0)
            thread_yield();
    }
    return 0;
}

bool usertest_rsp0(void)
{
    rsp0_checks = rsp0_mismatches = 0;
    rsp0_stop = false;
    uint64_t switches0 = thread_switches(), preempt0 = thread_preemptions();
    int32_t tid[RSP0_WORKERS];
    for (int i = 0; i < RSP0_WORKERS; i++) {
        tid[i] = thread_create("rsp0", rsp0_worker, (void *)(intptr_t)(i + 1), false);
        if (tid[i] < 0) {
            klogf("RSP0 test: FAIL (could not create worker %d)\n", i + 1);
            return false;
        }
    }
    uint64_t start = timer_ticks();
    while (timer_ticks() - start < RSP0_TICKS) {
        rsp0_check(0);
        thread_yield();
    }
    rsp0_stop = true;
    for (int i = 0; i < RSP0_WORKERS; i++)
        thread_join(tid[i]);
    rsp0_check(0);

    unsigned distinct = 0;
    for (int i = 0; i <= RSP0_WORKERS; i++) {
        bool seen_before = false;
        for (int j = 0; j < i; j++)
            if (rsp0_seen[j] == rsp0_seen[i])
                seen_before = true;
        if (!seen_before && rsp0_seen[i])
            distinct++;
    }
    uint64_t switches = thread_switches() - switches0, preempted = thread_preemptions() - preempt0;
    bool ok = rsp0_mismatches == 0 && rsp0_checks > 0 && preempted >= 3 && distinct == RSP0_WORKERS + 1;
    klogf("RSP0 test: %u checks in %u threads after %llu switches (%llu preemptions), "
          "%u distinct RSP0 values, %u mismatches\n", (unsigned)rsp0_checks, RSP0_WORKERS + 1,
          (unsigned long long)switches, (unsigned long long)preempted, distinct,
          (unsigned)rsp0_mismatches);
    klogf("RSP0 test: %s\n", ok ? "PASS" : "FAIL");
    return ok;
}

/* ---- test=clac ---- */

#define RFLAGS_AC 0x40000ull
#define CLAC_TICKS 10

static inline void set_ac(bool on)
{
    uint64_t f;
    __asm__ volatile ("pushfq; pop %0" : "=r"(f));
    f = on ? (f | RFLAGS_AC) : (f & ~RFLAGS_AC);
    __asm__ volatile ("push %0; popfq" : : "r"(f) : "memory", "cc");
}

bool usertest_clac(void)
{
    const struct cpuprot_status *st = cpuprot_status();
    uint64_t clac0 = cpuprot_clac_count;
    interrupts_ac_probe_start();
    set_ac(true);
    uint64_t start = timer_ticks();
    while (timer_ticks() - start < CLAC_TICKS) {
        volatile uint32_t work = 0;
        for (uint32_t i = 0; i < 4000; i++)
            work += i;
    }
    set_ac(false);
    uint32_t with_ac, without_ac;
    interrupts_ac_probe_stop(&with_ac, &without_ac);
    uint64_t clacs = cpuprot_clac_count - clac0;
    uint32_t total = with_ac + without_ac;
    bool ok = st->smap_on ? (total >= CLAC_TICKS && with_ac == 0 && clacs >= total)
                          : (total >= CLAC_TICKS && clacs == 0 && cpuprot_clac_count == 0);
    klogf("clac test: SMAP %s; %u interrupts taken with AC=1 in the interrupted code; "
          "AC=1 seen by the handler %u times; clac executed %llu times\n",
          st->smap_on ? "on" : "off", total, with_ac, (unsigned long long)clacs);
    klogf("clac test: %s\n", ok ? "PASS" : "FAIL");
    return ok;
}
