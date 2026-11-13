#ifndef MADIRE_POWER_POWER_H
#define MADIRE_POWER_POWER_H

/*
 * Shut down (1.5 stage 6; docs/plans/1.5.md, C3 and C6b): the path the power
 * dialog's Shut down (desktop.c) and "poweroff" (console.c) take, on
 * thread 0 with no lock held, in two calls so the console can print its
 * "Saving files..." line between them:
 *
 *   power_save(clicked)   logs "Power: shutting down (Live mode)" or
 *                         "(Disk mode)"; in Disk mode the save before
 *                         power-off (ext2_disk_poweroff_save: the automatic
 *                         save stopped, the disk lock taken and KEPT from
 *                         then on, the final full save, the clean mark and
 *                         its save, within 10 s of `clicked`, timer ticks);
 *                         Live mode saves nothing and takes no lock (L6).
 *   power_off()           the ACPI S5 power-off (acpi.h); if the machine is
 *                         still running, the fallback screen (full screen,
 *                         on the framebuffer or in VGA text, also on serial):
 *                           "It is now safe to turn off the computer."
 *                           "Madire OS could not power off by itself (<reason>)."
 *                         then a halt with interrupts off. The disk lock is
 *                         never given back.
 *
 * Restart is not here: its path is unchanged (reset.c, ext2_disk_restart_save).
 */

#include <stdint.h>
#include "kernel/fs/ext2_disk.h"

#define POWER_SAFE_LINE "It is now safe to turn off the computer."

enum ext2_disk_save power_save(uint64_t clicked);
void power_off(void) __attribute__((noreturn));

#endif
