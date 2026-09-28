/* Device diagnostics (see devtest.h). */
#include <stdbool.h>
#include <stdint.h>
#include "drivers/keyboard/ps2kbd.h"
#include "kernel/arch/x86/cmos.h"
#include "kernel/arch/x86/irq.h"
#include "kernel/bootscreen.h"
#include "kernel/devtest.h"
#include "kernel/input/keyqueue.h"
#include "kernel/kprintf.h"
#include "kernel/timer.h"

#define RATE_SECONDS      3u
#define RATE_EXPECTED     (RATE_SECONDS * TIMER_HZ)
#define RATE_TOLERANCE    (RATE_EXPECTED / 10)      /* +-10% */
#define EDGE_TIMEOUT      (2 * TIMER_HZ)            /* 2 s per RTC second */
#define FLOOD_TICKS       (3 * TIMER_HZ)
#define UNEXPECTED_VECTOR 34                         /* IRQ2: masked, no handler */

/* Result on the message line (bootscreen_message also logs it to serial). */
static void report(const char *line)
{
    bootscreen_message(line);
}

/* Busy-waits (bounded by timer ticks) until the RTC seconds value changes.
 * Busy rather than hlt so the edge is seen as soon as it happens. */
static bool wait_rtc_edge(uint8_t *seconds)
{
    uint64_t deadline = timer_ticks() + EDGE_TIMEOUT;
    uint8_t now;
    while (timer_ticks() < deadline) {
        if (!cmos_rtc_seconds(&now))
            return false;
        if (now != *seconds) {
            *seconds = now;
            return true;
        }
    }
    return false;
}

void devtest_timer(void)
{
    char line[96];
    uint8_t sec;

    if (!timer_verified()) {
        report("Timer diagnostic: FAIL (timer is not running)");
        return;
    }
    if (!cmos_rtc_seconds(&sec)) {
        report("Timer diagnostic: FAIL (RTC not readable)");
        return;
    }
    bootscreen_message("Timer diagnostic: measuring against the RTC for 3 seconds...");

    /* Start on an RTC second boundary, then count ticks to the 3rd next one. */
    if (!wait_rtc_edge(&sec)) {
        report("Timer diagnostic: FAIL (RTC seconds did not change)");
        return;
    }
    uint64_t start = timer_ticks();
    for (uint32_t i = 0; i < RATE_SECONDS; i++) {
        if (!wait_rtc_edge(&sec)) {
            report("Timer diagnostic: FAIL (RTC seconds stopped changing)");
            return;
        }
    }
    uint32_t measured = (uint32_t)(timer_ticks() - start);

    bool pass = measured >= RATE_EXPECTED - RATE_TOLERANCE
             && measured <= RATE_EXPECTED + RATE_TOLERANCE;
    ksnprintf(line, sizeof(line), "Timer diagnostic: %s (%u ticks in %u RTC seconds, expected %u-%u)",
              pass ? "PASS" : "FAIL", measured, RATE_SECONDS,
              RATE_EXPECTED - RATE_TOLERANCE, RATE_EXPECTED + RATE_TOLERANCE);
    report(line);
}

void devtest_keyboard(void)
{
    char line[80];
    uint32_t count = 0, reported_dropped = 0;

    bootscreen_diag_begin();
    bootscreen_message("Keyboard test: press keys, codes appear above (reboot to exit)");
    klogf("Keyboard test: ready\n");

    for (;;) {
        struct key_event e;
        while (keyboard_get_event(&e)) {
            devtest_describe_key(line, sizeof(line), &e);
            klogf("%s\n", line);
            bootscreen_diag_line(count++, line);
        }
        uint32_t dropped = keyboard_dropped();
        if (dropped != reported_dropped) {
            klogf("Keyboard: %u events dropped (queue full)\n", dropped - reported_dropped);
            reported_dropped = dropped;
        }
        __asm__ volatile ("hlt");
    }
}

void devtest_kbd_flood(void)
{
    char line[96];
    struct key_event e;

    if (!timer_verified()) {
        report("Keyboard flood test: FAIL (timer is not running)");
        return;
    }
    uint32_t dropped_before = keyboard_dropped();
    bootscreen_message("Keyboard flood test: not reading keys for 3 seconds...");
    klogf("Keyboard flood test: ready\n");
    timer_wait_ticks(FLOOD_TICKS);

    /* Snapshot at the end of the window, then drain exactly that many:
     * events arriving while draining are "late" and left to the idle loop,
     * so they are not miscounted as kept. */
    uint32_t flags = irq_save();
    uint32_t kept = keyboard_queued();
    uint32_t dropped = keyboard_dropped() - dropped_before;
    irq_restore(flags);
    for (uint32_t i = 0; i < kept && keyboard_get_event(&e); i++)
        devtest_log_key(&e);
    ksnprintf(line, sizeof(line), "Keyboard flood test: kept=%u dropped=%u (queue holds %u)",
              kept, dropped, KEYQUEUE_SIZE);
    report(line);
}

void devtest_irq_unexpected(void)
{
    char line[96];
    uint32_t before = irq_unexpected_count();
    __asm__ volatile ("int %0" : : "i"(UNEXPECTED_VECTOR));
    uint32_t after = irq_unexpected_count();
    bool pass = after == before + 1 && irq_last_unexpected() == UNEXPECTED_VECTOR - 32;
    ksnprintf(line, sizeof(line), "Unexpected IRQ test: %s (IRQ %u counted and acknowledged, total %u)",
              pass ? "PASS" : "FAIL", irq_last_unexpected(), after);
    report(line);
}

void devtest_describe_key(char *buf, size_t size, const struct key_event *e)
{
    char ch[8];
    if (e->ascii >= 0x21 && e->ascii <= 0x7E)
        ksnprintf(ch, sizeof(ch), "'%c'", e->ascii);
    else
        ksnprintf(ch, sizeof(ch), "none");
    ksnprintf(buf, size, "key: %s sc=0x%02x key=%s ascii=%s flags=0x%02x",
              (e->flags & KEY_PRESSED) ? "press" : "release", e->scancode,
              scancode_key_name(e->keycode), ch, e->flags);
}

void devtest_log_key(const struct key_event *e)
{
    char line[80];
    devtest_describe_key(line, sizeof(line), e);
    klogf("%s\n", line);
}
