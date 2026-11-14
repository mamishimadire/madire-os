/* The ACPI S5 power-off, the kernel side (1.5 stage 6): see acpi.h. Every
 * static is acpi_k_-prefixed. */
#include "kernel/power/acpi.h"
#include "kernel/arch/x86/io.h"
#include "kernel/kprintf.h"
#include "kernel/memory/paging.h"
#include "kernel/proc/diagwait.h"
#include "kernel/proc/thread.h"
#include "kernel/timer.h"

typedef unsigned long long ull;

#define ACPI_K_PAGE     4096u
#define ACPI_K_LIMIT    0x100000000ull  /* tables are read below 4 GiB only */
#define ACPI_K_BDA_EBDA 0x40Eu          /* the BIOS data area's EBDA segment */
#define ACPI_K_EBDA_MIN 0x500u
#define ACPI_K_EBDA_END 0xA0000u
#define ACPI_K_ENABLE_TICKS   (1u * TIMER_HZ)     /* C3 step 4: at most 1 s */
#define ACPI_K_POWEROFF_TICKS (2u * TIMER_HZ)     /* C3 step 6: still running 2 s later */

static bool acpi_k_stay_on;             /* 1.5 stage 7: test=poweroffwait */

void acpi_test_stay_on(void)
{
    acpi_k_stay_on = true;
}

/* acpi_core.h's read: through the physical-access window, a page at a
 * time, below 4 GiB only (a table elsewhere cannot be read: its check
 * fails, never a fault). */
static int acpi_k_read(void *ctx, uint64_t phys, uint32_t len, uint8_t *buf)
{
    (void)ctx;
    if (!paging_enabled() || len == 0 || phys >= ACPI_K_LIMIT || len > ACPI_K_LIMIT - phys)
        return -1;
    while (len) {
        uint64_t page = phys & ~(uint64_t)(ACPI_K_PAGE - 1u);
        uint32_t off = (uint32_t)(phys - page);
        uint32_t n = ACPI_K_PAGE - off < len ? ACPI_K_PAGE - off : len;
        volatile const uint8_t *p = (volatile const uint8_t *)vmm_window_map(page);
        for (uint32_t i = 0; i < n; i++)
            buf[i] = p[off + i];
        vmm_window_unmap();
        buf += n;
        phys += n;
        len -= n;
    }
    return 0;
}

static const struct acpi_mem acpi_k_mem = { acpi_k_read, 0 };

/* The EBDA's address from the BIOS data area, or 0 if it is not plausible
 * (then only 0xE0000-0xFFFFF is searched). */
static uint64_t acpi_k_ebda(void)
{
    uint8_t b[2];
    if (acpi_k_read(0, ACPI_K_BDA_EBDA, 2, b))
        return 0;
    uint64_t a = (uint64_t)((uint32_t)b[0] | (uint32_t)b[1] << 8) << 4;
    return a >= ACPI_K_EBDA_MIN && a + 1024u <= ACPI_K_EBDA_END ? a : 0;
}

/* SCI_EN set in PM1a_CNT within `ticks` (bounded, yielding: diagwait.h). */
static bool acpi_k_wait_sci(uint16_t port, uint32_t ticks)
{
    struct diag_site s;
    diag_wait_begin(&s, ticks);
    for (;;) {
        if (inw(port) & ACPI_SCI_EN)
            return true;
        if (!diag_wait_poll(&s))
            return (inw(port) & ACPI_SCI_EN) != 0;
        thread_yield();
    }
}

/* The tables found so far, for the ACPI: line. */
static size_t acpi_k_tables(char *b, size_t size, const struct acpi_info *in)
{
    size_t n = ksnprintf(b, size, "ACPI: RSDP at 0x%llx (revision %u), %s at 0x%llx", (ull)in->rsdp,
                         (unsigned)in->revision, in->xsdt ? "XSDT" : "RSDT", (ull)in->root);
    if (in->entries && n < size)
        n += ksnprintf(b + n, size - n, " (%u entries)", in->entries);
    if (in->fadt && n < size)
        n += ksnprintf(b + n, size - n, ", FADT at 0x%llx", (ull)in->fadt);
    if (in->fadt_len && n < size)
        n += ksnprintf(b + n, size - n, " (%u bytes)", in->fadt_len);
    if (in->dsdt && n < size)
        n += ksnprintf(b + n, size - n, ", DSDT at 0x%llx", (ull)in->dsdt);
    if (in->dsdt_len && n < size)
        n += ksnprintf(b + n, size - n, " (%u bytes)", in->dsdt_len);
    return n;
}

enum acpi_why acpi_power_off(void)
{
    struct acpi_info in;
    char line[480];
    enum acpi_why w = acpi_find(&acpi_k_mem, acpi_k_ebda(), &in);
    if (w == ACPI_NO_TABLES) {
        klogf("ACPI: %s (%s)\n", acpi_why_text(w), in.detail ? in.detail : "?");
        return w;
    }
    size_t n = acpi_k_tables(line, sizeof(line), &in);
    if (w != ACPI_OK) {
        klogf("%s; %s (%s)\n", line, acpi_why_text(w), in.detail ? in.detail : "?");
        return w;
    }
    if (n < sizeof(line))
        n += ksnprintf(line + n, sizeof(line) - n, "; _S5 SLP_TYPa %u, SLP_TYPb %u; PM1a_CNT 0x%x, ",
                       (unsigned)in.slp_typa, (unsigned)in.slp_typb, (unsigned)in.pm1a_cnt);
    if (n < sizeof(line)) {
        if (in.pm1b_cnt)
            n += ksnprintf(line + n, sizeof(line) - n, "PM1b_CNT 0x%x; ", (unsigned)in.pm1b_cnt);
        else
            n += ksnprintf(line + n, sizeof(line) - n, "PM1b_CNT none; ");
    }
    /* C3 step 4: ACPI mode, only when SCI_EN is clear. */
    if (inw(in.pm1a_cnt) & ACPI_SCI_EN) {
        klogf("%sSCI_EN set\n", line);
    } else if (in.smi_cmd && in.acpi_enable) {
        outb((uint16_t)in.smi_cmd, in.acpi_enable);
        if (!acpi_k_wait_sci(in.pm1a_cnt, ACPI_K_ENABLE_TICKS)) {
            klogf("%sSCI_EN still clear 1 s after ACPI_ENABLE 0x%x to SMI_CMD 0x%x; %s\n", line,
                  (unsigned)in.acpi_enable, (unsigned)in.smi_cmd, acpi_why_text(ACPI_NO_ENABLE));
            return ACPI_NO_ENABLE;
        }
        klogf("%sSCI_EN set after ACPI_ENABLE 0x%x to SMI_CMD 0x%x\n", line, (unsigned)in.acpi_enable,
              (unsigned)in.smi_cmd);
    } else {
        klogf("%sSCI_EN clear (no SMI_CMD or ACPI_ENABLE: not changed)\n", line);
    }
    /* C3 step 5: the read-modify-write of PM1a_CNT, then PM1b_CNT (1.5
     * stage 7: not under test=poweroffwait, so the wait below runs). */
    if (acpi_k_stay_on) {
        klogf("ACPI: test=poweroffwait: PM1a_CNT and PM1b_CNT not written (the computer stays on for the 2 s wait)\n");
    } else {
        outw(in.pm1a_cnt, acpi_pm1_value(inw(in.pm1a_cnt), in.slp_typa));
        if (in.pm1b_cnt)
            outw(in.pm1b_cnt, acpi_pm1_value(inw(in.pm1b_cnt), in.slp_typb));
    }
    /* C3 step 6: still running 2 s later. */
    (void)diag_sleep(ACPI_K_POWEROFF_TICKS);
    return ACPI_NO_POWEROFF;
}
