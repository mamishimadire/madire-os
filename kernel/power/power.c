/* Shut down (1.5 stage 6): see power.h. Every static is power_-prefixed. */
#include "kernel/power/power.h"
#include "drivers/vga/vga_text.h"
#include "kernel/display/fbcon.h"
#include "kernel/fs/vfs.h"
#include "kernel/kprintf.h"
#include "kernel/power/acpi.h"
#include "kernel/proc/disklock.h"
#include "kernel/proc/fd.h"
#include "kernel/proc/thread.h"

typedef unsigned long long ull;

/* ---- 1.5 stage 7: test=poweroffwait (power.h) ---- */

static bool power_t_on;
static int32_t power_t_tid = -1;
static volatile uint32_t power_t_began, power_t_done;
static uint32_t power_t_began0, power_t_done0;     /* when Shut down began */
static uint64_t power_t_yields0;                    /* other threads' disk-lock yields then */

static int32_t power_t_waiter(void *arg)
{
    (void)arg;
    for (;;) {
        power_t_began++;
        fd_vfs_acquire();               /* Disk mode: the disk lock first (fd.h) */
        struct vnode *n = 0;
        struct vfs *v = vfs_root();
        (void)vfs_lookup(v, v->root, "/", &n);
        fd_vfs_release();
        power_t_done++;
        thread_sleep(1);
    }
    return 0;
}

void power_test_start(void)
{
    if (!disklock_on()) {
        klogf("Power test (test=poweroffwait): FAILED (no disk mounted)\n");
        return;
    }
    acpi_test_stay_on();
    power_t_tid = thread_create("pw-waiter", power_t_waiter, 0, true);
    power_t_on = power_t_tid >= 0;
    klogf("Power test (test=poweroffwait): the waiter thread %s (thread %d): a file call every tick; the ACPI "
          "power-off will not write PM1a_CNT or PM1b_CNT\n", power_t_on ? "started" : "NOT started", (int)power_t_tid);
}

static uint64_t power_t_other_yields(void)
{
    struct disklock_figs d;
    disklock_figures(&d);
    return d.yields - d.yields0;
}

static const char *power_t_state(enum sched_state s)
{
    switch (s) {
    case SCHED_FREE:    return "gone";
    case SCHED_NEW:     return "not started";
    case SCHED_READY:   return "ready";
    case SCHED_RUNNING: return "running";
    case SCHED_BLOCKED: return "blocked";
    case SCHED_ZOMBIE:  return "exited";
    }
    return "?";
}

/* The line before the fallback screen (thread 0 still holds the disk lock). */
static void power_t_report(void)
{
    uint32_t began = power_t_began, done = power_t_done;
    enum sched_state s = thread_state(power_t_tid);
    bool waits = began == done + 1u && (s == SCHED_READY || s == SCHED_RUNNING);
    klogf("Power test (test=poweroffwait): since Shut down began the waiter (thread %d) began %u file calls and "
          "finished %u; other threads' waits for the disk lock: %llu yields; the waiter waits for it now: %s "
          "(thread state %s)\n", (int)power_t_tid, began - power_t_began0, done - power_t_done0,
          (ull)(power_t_other_yields() - power_t_yields0), waits ? "yes" : "no", power_t_state(s));
}

enum ext2_disk_save power_save(uint64_t clicked)
{
    klogf("Power: shutting down (%s)\n", ext2_disk_mounted() ? "Disk mode" : "Live mode");
    if (power_t_on) {
        power_t_began0 = power_t_began;
        power_t_done0 = power_t_done;
        power_t_yields0 = power_t_other_yields();
    }
    return ext2_disk_poweroff_save(clicked);
}

/* C3 step 6: the fallback screen, then a halt with interrupts off. The
 * console leaves its window or the desktop's hold first (fbcon.h), so the
 * two lines show on the whole screen; kprintf puts them in the cell grid
 * and on serial. */
static void __attribute__((noreturn)) power_fallback(enum acpi_why why)
{
    __asm__ volatile ("cli");
    fbcon_full_screen();
    vga_cursor_show(false);
    vga_set_color(VGA_WHITE, VGA_BLACK);
    vga_clear();
    kprintf("\n" POWER_SAFE_LINE "\n\nMadire OS could not power off by itself (%s).\n", acpi_why_text(why));
    for (;;)
        __asm__ volatile ("cli; hlt");
}

void power_off(void)
{
    enum acpi_why why = acpi_power_off();
    if (power_t_on)
        power_t_report();               /* 1.5 stage 7: test=poweroffwait */
    power_fallback(why);
}
