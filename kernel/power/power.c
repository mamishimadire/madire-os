/* Shut down (1.5 stage 6): see power.h. Every static is power_-prefixed. */
#include "kernel/power/power.h"
#include "drivers/vga/vga_text.h"
#include "kernel/display/fbcon.h"
#include "kernel/kprintf.h"
#include "kernel/power/acpi.h"

enum ext2_disk_save power_save(uint64_t clicked)
{
    klogf("Power: shutting down (%s)\n", ext2_disk_mounted() ? "Disk mode" : "Live mode");
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
    power_fallback(acpi_power_off());
}
