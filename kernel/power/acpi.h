#ifndef MADIRE_POWER_ACPI_H
#define MADIRE_POWER_ACPI_H

/*
 * The ACPI S5 power-off, the kernel side (1.5 stage 6; docs/plans/1.5.md,
 * C3, risks 47-49 and 64). The tables are read only now, when the power-off
 * was chosen (never at boot), through the physical-access window, a page at
 * a time, and checked by the pure acpi_core.c; the outcome is logged in one
 * line:
 *   "ACPI: RSDP at 0x... (revision R), RSDT|XSDT at 0x... (N entries), FADT
 *    at 0x... (L bytes), DSDT at 0x... (L bytes); _S5 SLP_TYPa A, SLP_TYPb B;
 *    PM1a_CNT 0x..., PM1b_CNT 0x...|none; SCI_EN set|<how it was enabled>"
 * or, when a step failed, the tables found so far and "; <reason> (<the
 * check>)".
 *
 * Then (C3 steps 4 and 5): if PM1a_CNT's SCI_EN is clear and the FADT gives
 * SMI_CMD and ACPI_ENABLE, ACPI_ENABLE is written to SMI_CMD and SCI_EN is
 * waited for, at most 1 s (bounded even if the timer stops: diagwait.h);
 * then PM1a_CNT, and PM1b_CNT if it exists, are each read, given SLP_TYP and
 * SLP_EN by acpi_pm1_value() (every other bit as read) and written back.
 * If the machine is still running 2 s later, the reason is "the computer
 * did not power off".
 *
 * Thread 0 only (the power-off path, power.c), with preemption on and no
 * VFS lock held (it yields while it waits; in Disk mode it holds the disk
 * lock, which allows that, L2).
 */

#include "kernel/power/acpi_core.h"

/* The power-off. Returns only if the machine did not power off, with the
 * reason (never ACPI_OK). */
enum acpi_why acpi_power_off(void);

/* 1.5 stage 7, test=poweroffwait (a test option, power.h): PM1a_CNT and
 * PM1b_CNT are not written, so a machine with good tables stays on and
 * the 2 s wait runs ("ACPI: test=poweroffwait: ..." logged after the
 * ACPI: line); everything before the write is as without it. */
void acpi_test_stay_on(void);

#endif
