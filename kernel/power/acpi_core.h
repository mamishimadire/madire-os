#ifndef MADIRE_POWER_ACPI_CORE_H
#define MADIRE_POWER_ACPI_CORE_H

/*
 * The ACPI tables for the power-off, the pure part (1.5 stage 6;
 * docs/plans/1.5.md, C3 with addition b, risks 47 and 64). PURE: no
 * hardware access, allocation or printing; every byte comes through the
 * caller's read call (struct acpi_mem), so the host unit suite
 * (tests/unit/test_acpi_core.c) runs it on built tables. The kernel side
 * (acpi.c) gives it the physical-access window, one page at a time.
 *
 * No AML interpreter: the path finds the RSDP, the RSDT (or the XSDT), the
 * FADT and the DSDT, and decodes the `_S5_` package's first two values
 * (SLP_TYPa, SLP_TYPb) by a fixed shortcut.
 *
 * The tables are untrusted firmware data. Every check below leads to a
 * reason (enum acpi_why), never to a read outside what a table states:
 *   - the RSDP: searched 16 bytes apart in the EBDA's first KiB (when the
 *     caller gives an EBDA) and in 0xE0000-0xFFFFF; a candidate counts only
 *     if its 20-byte checksum is right and, from revision 2, its length is
 *     36 to ACPI_RSDP_MAX bytes and its extended checksum (over that
 *     length) is right; none: ACPI_NO_TABLES;
 *   - the RSDT, or the XSDT when the RSDP's revision is 2 or more and it
 *     gives an XSDT address: signature, length (at least the 36-byte
 *     header, at most ACPI_TABLE_MAX, the entries a whole number), the
 *     checksum over the stated length; each entry is read only inside that
 *     length; each entry's table must be readable (an entry pointing
 *     outside memory fails the checks);
 *   - the FADT (the first entry signed "FACP") and the DSDT: signature,
 *     length (the FADT at least 116 bytes, its 1.0 size, and at most
 *     ACPI_TABLE_MAX; the DSDT at most ACPI_DSDT_MAX), the checksum over
 *     the stated length; the FADT's fields are read only inside its stated
 *     length (X_DSDT only if the FADT is at least 148 bytes); PM1a_CNT must
 *     be a port (1 to 0xFFFF) with PM1_CNT_LEN at least 2, PM1b_CNT 0 (none)
 *     or a port, SMI_CMD 0 or a port. Any failure: ACPI_BAD_TABLE;
 *   - `_S5_`: searched only inside the DSDT's stated length, after its
 *     header; only a name definition counts (NameOp 0x08 right before it,
 *     or NameOp and the root prefix '\'); its package (PackageOp 0x12) is
 *     decoded with every byte bounded: PkgLength of 1 to 4 bytes, the
 *     package's end inside the DSDT, NumElements at least 2, each of the
 *     first two values BytePrefix (0x0A) and a byte, Zero (0x00) or One
 *     (0x01), each at most 7 (SLP_TYP is 3 bits), every byte inside the
 *     package. None, or not decodable: ACPI_NO_S5.
 * Every read is at most ACPI_CHUNK bytes; a table's address plus its length
 * never wraps.
 *
 * The PM1 control registers (C3 step 5): acpi_pm1_value() is the
 * read-modify-write: the value read with only SLP_TYP (bits 10-12)
 * replaced and SLP_EN (bit 13) set; every other bit (SCI_EN included) as
 * read.
 *
 * Facts from the ACPI specification (UEFI Forum; references.md; the owner's
 * Adopter Membership Agreement, docs/plans/1.5.md risk 65): the structures'
 * offsets and the AML opcodes named above. Written from scratch from those
 * facts; no other project's code was read for it.
 */

#include <stdbool.h>
#include <stdint.h>

#define ACPI_CHUNK      256u            /* bytes per read at most */
#define ACPI_TABLE_MAX  (64u * 1024u)   /* the RSDT, XSDT and FADT */
#define ACPI_DSDT_MAX   (1024u * 1024u)
#define ACPI_RSDP_MAX   1024u
#define ACPI_FADT_MIN   116u            /* the ACPI 1.0 FADT */
#define ACPI_SCI_EN     0x0001u         /* PM1 control, bit 0 */
#define ACPI_SLP_TYP    0x1C00u         /* PM1 control, bits 10-12 */
#define ACPI_SLP_EN     0x2000u         /* PM1 control, bit 13 */

enum acpi_why {
    ACPI_OK,
    ACPI_NO_TABLES,                     /* "no ACPI tables found" */
    ACPI_BAD_TABLE,                     /* "an ACPI table failed its checks" */
    ACPI_NO_S5,                         /* "no _S5 sleep state" */
    ACPI_NO_ENABLE,                     /* "ACPI mode could not be enabled" (acpi.c) */
    ACPI_NO_POWEROFF,                   /* "the computer did not power off" (acpi.c) */
};

/* The fallback screen's reason for w (docs/plans/1.5.md, C6b). */
const char *acpi_why_text(enum acpi_why w);

struct acpi_mem {
    /* Reads `len` (1 to ACPI_CHUNK) bytes at physical address `phys` into
     * buf: 0, or not 0 if they cannot be read. */
    int (*read)(void *ctx, uint64_t phys, uint32_t len, uint8_t *buf);
    void *ctx;
};

/* What acpi_find() found (each field 0 until found). */
struct acpi_info {
    uint64_t rsdp;
    uint8_t revision;
    uint64_t root;                      /* the RSDT or the XSDT */
    bool xsdt;
    uint32_t entries;
    uint64_t fadt;
    uint32_t fadt_len;
    uint64_t dsdt;
    uint32_t dsdt_len;
    uint32_t smi_cmd;                   /* 0: none */
    uint8_t acpi_enable;
    uint16_t pm1a_cnt;
    uint16_t pm1b_cnt;                  /* 0: none */
    uint8_t slp_typa, slp_typb;
    const char *detail;                 /* the check that failed (for the log), or 0 */
};

/* The RSDP, the RSDT or XSDT, the FADT, the DSDT and `_S5_` (the rules
 * above); `ebda` is the EBDA's address, or 0 to search 0xE0000-0xFFFFF
 * only. ACPI_OK with every field of *in set, or the reason with the fields
 * found so far and `detail`. */
enum acpi_why acpi_find(const struct acpi_mem *m, uint64_t ebda, struct acpi_info *in);

/* The PM1 read-modify-write (C3 step 5): `read` with bits 10-12 set to
 * slp_typ (its low 3 bits) and bit 13 set; every other bit unchanged. */
uint16_t acpi_pm1_value(uint16_t read, uint8_t slp_typ);

#endif
