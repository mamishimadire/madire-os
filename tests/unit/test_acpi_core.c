/* Unit tests for the ACPI tables' pure part, kernel/power/acpi_core.c
 * (1.5 stage 6; docs/plans/1.5.md, C3 with addition b, risk 64): built
 * tables in a fake physical memory (the low 1.1 MiB), read through the
 * same call the kernel gives the window. Each named case of the plan:
 * good tables; a bad checksum; a bad length; a truncated _S5_ package;
 * _S5_ at the DSDT's end; an RSDT entry pointing outside; an XSDT; no
 * PM1b; and acpi_pm1_rmw_keeps_bits. A "forbidden" range right after the
 * DSDT catches any read past a table's stated length; every read is
 * checked to be at most ACPI_CHUNK bytes. */
#include <stdint.h>
#include "kernel/power/acpi_core.c"
#include "tests/unit/unit.h"

#define MEM_SIZE 0x110000u
static uint8_t mem[MEM_SIZE];
static uint64_t forbid_lo, forbid_hi;   /* [lo, hi): a read touching it fails and is counted */
static int forbidden_reads, big_reads, reads;

static int fake_read(void *ctx, uint64_t phys, uint32_t len, uint8_t *buf)
{
    (void)ctx;
    reads++;
    if (len > ACPI_CHUNK)
        big_reads++;
    if (phys < forbid_hi && phys + len > forbid_lo) {
        forbidden_reads++;
        return -1;
    }
    if (phys >= MEM_SIZE || len > MEM_SIZE - phys)
        return -1;
    for (uint32_t i = 0; i < len; i++)
        buf[i] = mem[phys + i];
    return 0;
}

static const struct acpi_mem fake = { fake_read, 0 };

static void p8(uint64_t a, uint8_t v) { mem[a] = v; }
static void p32(uint64_t a, uint32_t v)
{
    for (int i = 0; i < 4; i++)
        mem[a + i] = (uint8_t)(v >> (8 * i));
}
static void p64(uint64_t a, uint64_t v)
{
    p32(a, (uint32_t)v);
    p32(a + 4, (uint32_t)(v >> 32));
}
static void pstr(uint64_t a, const char *s)
{
    while (*s)
        mem[a++] = (uint8_t)*s++;
}
static void pbytes(uint64_t a, const uint8_t *b, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++)
        mem[a + i] = b[i];
}

/* The byte at `cks` set so the bytes [a, a + len) sum to 0. */
static void fix_sum(uint64_t a, uint32_t len, uint64_t cks)
{
    uint8_t s = 0;
    mem[cks] = 0;
    for (uint32_t i = 0; i < len; i++)
        s = (uint8_t)(s + mem[a + i]);
    mem[cks] = (uint8_t)(0x100u - s);
}

/* A table header at a: signature, length; its checksum fixed. */
static void table(uint64_t a, const char *sig, uint32_t len)
{
    pstr(a, sig);
    p32(a + 4, len);
    p8(a + 8, 1);
    pstr(a + 10, "MADIRE");
    fix_sum(a, len, a + 9);
}

#define RSDP_AT  0xE0010u
#define EBDA     0x9FC00u
#define RSDT_AT  0x100000u
#define APIC_AT  0x100100u
#define FADT_AT  0x100200u
#define XSDT_AT  0x100400u
#define DSDT_AT  0x101000u
#define FADT_LEN 244u

static void rsdp(uint64_t a, uint8_t rev, uint32_t rsdt, uint64_t xsdt)
{
    for (uint32_t i = 0; i < 36; i++)
        mem[a + i] = 0;
    pstr(a, "RSD PTR ");
    pstr(a + 9, "MADIRE");
    p8(a + 15, rev);
    p32(a + 16, rsdt);
    if (rev >= 2) {
        p32(a + 20, 36);
        p64(a + 24, xsdt);
    }
    fix_sum(a, 20, a + 8);
    if (rev >= 2)
        fix_sum(a, 36, a + 32);
}

/* The FADT: DSDT, SMI_CMD 0xB2, ACPI_ENABLE 0xF1, PM1a 0x604, PM1b, PM1_CNT_LEN 2. */
static void fadt(uint32_t len, uint32_t pm1b)
{
    for (uint32_t i = 0; i < FADT_LEN; i++)
        mem[FADT_AT + i] = 0;
    p32(FADT_AT + 40, DSDT_AT);
    p32(FADT_AT + 48, 0xB2);
    p8(FADT_AT + 52, 0xF1);
    p32(FADT_AT + 64, 0x604);
    p32(FADT_AT + 68, pm1b);
    p8(FADT_AT + 89, 2);
    table(FADT_AT, "FACP", len);
}

/* The DSDT: some AML, a "_S5_" that is not a name (a reference after
 * StoreOp), then `s5` (n bytes); its header and checksum. Returns its length. */
static uint32_t dsdt(const uint8_t *s5, uint32_t n)
{
    static const uint8_t before[] = { 0x10, 0x05, 0x5C, 0x2F, 0x00, 0x70, '_', 'S', '5', '_', 0x60, 0xA3 };
    uint32_t len = 36u + (uint32_t)sizeof(before) + n;
    for (uint32_t i = 0; i < 0x2000u; i++)
        mem[DSDT_AT + i] = 0;
    pbytes(DSDT_AT + 36, before, sizeof(before));
    pbytes(DSDT_AT + 36 + sizeof(before), s5, n);
    table(DSDT_AT, "DSDT", len);
    return len;
}

static const uint8_t s5_good[] = { 0x08, '_', 'S', '5', '_', 0x12, 0x08, 0x04, 0x0A, 0x05, 0x0A, 0x07, 0x00, 0x00 };

/* The good tables: RSDP (revision 0) at 0xE0010, an RSDT with an "APIC"
 * table and the FADT, the FADT with PM1b 0x0B04, the DSDT with s5_good. */
static void build_good(void)
{
    for (uint32_t i = 0; i < MEM_SIZE; i++)
        mem[i] = 0;
    forbid_lo = forbid_hi = 0;
    forbidden_reads = big_reads = reads = 0;
    rsdp(RSDP_AT, 0, RSDT_AT, 0);
    p32(RSDT_AT + 36, APIC_AT);
    p32(RSDT_AT + 40, FADT_AT);
    table(RSDT_AT, "RSDT", 44);
    table(APIC_AT, "APIC", 44);
    fadt(FADT_LEN, 0x0B04);
    uint32_t len = dsdt(s5_good, sizeof(s5_good));
    forbid_lo = DSDT_AT + len;
    forbid_hi = forbid_lo + 64;
}

static bool same(const char *a, const char *b)
{
    if (!a)
        return false;                   /* no detail: a failed check, not a crash */
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return *a == 0 && *b == 0;
}

static void clean_reads(void)
{
    CHECK_EQ(forbidden_reads, 0);
    CHECK_EQ(big_reads, 0);
}

static void test_good_tables(void)
{
    struct acpi_info in;
    build_good();
    CHECK_EQ(acpi_find(&fake, 0, &in), ACPI_OK);
    CHECK_EQ(in.rsdp, RSDP_AT);
    CHECK_EQ(in.revision, 0);
    CHECK(!in.xsdt);
    CHECK_EQ(in.root, RSDT_AT);
    CHECK_EQ(in.entries, 2);
    CHECK_EQ(in.fadt, FADT_AT);
    CHECK_EQ(in.fadt_len, FADT_LEN);
    CHECK_EQ(in.dsdt, DSDT_AT);
    CHECK_EQ(in.smi_cmd, 0xB2);
    CHECK_EQ(in.acpi_enable, 0xF1);
    CHECK_EQ(in.pm1a_cnt, 0x604);
    CHECK_EQ(in.pm1b_cnt, 0x0B04);
    CHECK_EQ(in.slp_typa, 5);
    CHECK_EQ(in.slp_typb, 7);
    CHECK(in.detail == 0);
    clean_reads();
    /* The EBDA's first KiB is searched first. */
    rsdp(EBDA + 0x40, 0, RSDT_AT, 0);
    CHECK_EQ(acpi_find(&fake, EBDA, &in), ACPI_OK);
    CHECK_EQ(in.rsdp, EBDA + 0x40);
    /* The root prefix '\' before the name, Zero and One as values, a
     * two-byte PkgLength. */
    static const uint8_t s5_root[] = { 0x08, 0x5C, '_', 'S', '5', '_', 0x12, 0x46, 0x00, 0x04, 0x00, 0x01, 0x00, 0x00,
                                       0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                                       0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                                       0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
    uint32_t len = dsdt(s5_root, sizeof(s5_root));
    forbid_lo = DSDT_AT + len;
    forbid_hi = forbid_lo + 64;
    CHECK_EQ(acpi_find(&fake, 0, &in), ACPI_OK);
    CHECK_EQ(in.slp_typa, 0);
    CHECK_EQ(in.slp_typb, 1);
    clean_reads();
}

static void test_no_rsdp(void)
{
    struct acpi_info in;
    build_good();
    mem[RSDP_AT] = 'X';
    CHECK_EQ(acpi_find(&fake, EBDA, &in), ACPI_NO_TABLES);
    CHECK(in.rsdp == 0 && in.detail != 0);
}

static void test_bad_checksum(void)
{
    struct acpi_info in;
    build_good();
    mem[FADT_AT + 200]++;               /* the FADT's sum no longer 0 */
    CHECK_EQ(acpi_find(&fake, 0, &in), ACPI_BAD_TABLE);
    CHECK(same(in.detail, "the FADT has a bad checksum"));
    clean_reads();
    build_good();
    mem[RSDP_AT + 16]++;                /* the RSDP's 20-byte checksum: not an RSDP */
    CHECK_EQ(acpi_find(&fake, 0, &in), ACPI_NO_TABLES);
    build_good();
    mem[DSDT_AT + 40]++;
    CHECK_EQ(acpi_find(&fake, 0, &in), ACPI_BAD_TABLE);
    CHECK(same(in.detail, "the DSDT has a bad checksum"));
    clean_reads();
}

static void test_bad_length(void)
{
    struct acpi_info in;
    build_good();
    fadt(100, 0x0B04);                  /* below the 116-byte FADT */
    CHECK_EQ(acpi_find(&fake, 0, &in), ACPI_BAD_TABLE);
    CHECK(same(in.detail, "the FADT has a bad length"));
    build_good();
    p32(DSDT_AT + 4, ACPI_DSDT_MAX + 1u);   /* above 1 MiB: refused before any read of it */
    CHECK_EQ(acpi_find(&fake, 0, &in), ACPI_BAD_TABLE);
    CHECK(same(in.detail, "the DSDT has a bad length"));
    clean_reads();
    build_good();
    p32(RSDT_AT + 4, 42);               /* not a whole number of entries */
    fix_sum(RSDT_AT, 42, RSDT_AT + 9);
    CHECK_EQ(acpi_find(&fake, 0, &in), ACPI_BAD_TABLE);
    CHECK(same(in.detail, "the RSDT or XSDT has a bad length"));
}

static void test_truncated_s5(void)
{
    struct acpi_info in;
    /* The package's PkgLength goes past the DSDT's end. */
    static const uint8_t s5_long[] = { 0x08, '_', 'S', '5', '_', 0x12, 0x3F, 0x04, 0x0A, 0x05, 0x0A, 0x05 };
    build_good();
    uint32_t len = dsdt(s5_long, sizeof(s5_long));
    forbid_lo = DSDT_AT + len;
    forbid_hi = forbid_lo + 64;
    CHECK_EQ(acpi_find(&fake, 0, &in), ACPI_NO_S5);
    CHECK(same(in.detail, "the _S5_ package goes past the DSDT's end"));
    clean_reads();
    /* Two values announced, one inside the package. */
    static const uint8_t s5_one[] = { 0x08, '_', 'S', '5', '_', 0x12, 0x04, 0x02, 0x0A, 0x05, 0x0A, 0x05, 0x00 };
    len = dsdt(s5_one, sizeof(s5_one));
    forbid_lo = DSDT_AT + len;
    forbid_hi = forbid_lo + 64;
    CHECK_EQ(acpi_find(&fake, 0, &in), ACPI_NO_S5);
    CHECK(same(in.detail, "the _S5_ package's values cannot be decoded"));
    clean_reads();
    /* A value above 7, and a value of another kind (WordPrefix). */
    static const uint8_t s5_big[] = { 0x08, '_', 'S', '5', '_', 0x12, 0x06, 0x02, 0x0A, 0x08, 0x0A, 0x05 };
    dsdt(s5_big, sizeof(s5_big));
    forbid_lo = forbid_hi = 0;
    CHECK_EQ(acpi_find(&fake, 0, &in), ACPI_NO_S5);
    static const uint8_t s5_word[] = { 0x08, '_', 'S', '5', '_', 0x12, 0x07, 0x02, 0x0B, 0x05, 0x00, 0x00 };
    dsdt(s5_word, sizeof(s5_word));
    CHECK_EQ(acpi_find(&fake, 0, &in), ACPI_NO_S5);
}

static void test_s5_at_dsdt_end(void)
{
    struct acpi_info in;
    static const uint8_t s5_end[] = { 0x08, '_', 'S', '5', '_' };
    build_good();
    uint32_t len = dsdt(s5_end, sizeof(s5_end));
    forbid_lo = DSDT_AT + len;          /* the bytes after the DSDT may not be read */
    forbid_hi = forbid_lo + 64;
    mem[forbid_lo] = 0x12;              /* (a package that would decode if they were) */
    mem[forbid_lo + 1] = 0x04;
    mem[forbid_lo + 2] = 0x02;
    CHECK_EQ(acpi_find(&fake, 0, &in), ACPI_NO_S5);
    CHECK(same(in.detail, "_S5_ is not followed by a package"));
    clean_reads();
    /* Only a reference, never a name: no _S5. */
    static const uint8_t s5_none[] = { 0xA4, '_', 'S', '5', '_' };
    len = dsdt(s5_none, sizeof(s5_none));
    forbid_lo = DSDT_AT + len;
    forbid_hi = forbid_lo + 64;
    CHECK_EQ(acpi_find(&fake, 0, &in), ACPI_NO_S5);
    CHECK(same(in.detail, "no _S5_ name in the DSDT"));
    clean_reads();
}

static void test_rsdt_entry_outside(void)
{
    struct acpi_info in;
    build_good();
    p32(RSDT_AT + 40, 0x7FFFF000u);     /* outside memory */
    fix_sum(RSDT_AT, 44, RSDT_AT + 9);
    CHECK_EQ(acpi_find(&fake, 0, &in), ACPI_BAD_TABLE);
    CHECK(same(in.detail, "an RSDT or XSDT entry points outside memory"));
    CHECK(in.fadt == 0);
    /* The RSDT itself outside memory. */
    build_good();
    rsdp(RSDP_AT, 0, 0x7FFFF000u, 0);
    CHECK_EQ(acpi_find(&fake, 0, &in), ACPI_BAD_TABLE);
    CHECK(same(in.detail, "the RSDT or XSDT cannot be read"));
}

static void test_xsdt(void)
{
    struct acpi_info in;
    build_good();
    p64(XSDT_AT + 36, FADT_AT);
    table(XSDT_AT, "XSDT", 44);
    rsdp(RSDP_AT, 2, RSDT_AT, XSDT_AT);
    mem[RSDT_AT] = 'X';                 /* the RSDT broken: only the XSDT may be used */
    CHECK_EQ(acpi_find(&fake, 0, &in), ACPI_OK);
    CHECK(in.xsdt);
    CHECK_EQ(in.revision, 2);
    CHECK_EQ(in.root, XSDT_AT);
    CHECK_EQ(in.entries, 1);
    CHECK_EQ(in.fadt, FADT_AT);
    CHECK_EQ(in.slp_typa, 5);
    clean_reads();
    /* Revision 2 with a bad extended checksum: not an RSDP. */
    mem[RSDP_AT + 33]++;
    CHECK_EQ(acpi_find(&fake, 0, &in), ACPI_NO_TABLES);
}

static void test_no_pm1b(void)
{
    struct acpi_info in;
    build_good();
    fadt(FADT_LEN, 0);
    CHECK_EQ(acpi_find(&fake, 0, &in), ACPI_OK);
    CHECK_EQ(in.pm1a_cnt, 0x604);
    CHECK_EQ(in.pm1b_cnt, 0);
    /* No PM1a: the FADT fails its checks. */
    build_good();
    p32(FADT_AT + 64, 0);
    fix_sum(FADT_AT, FADT_LEN, FADT_AT + 9);
    CHECK_EQ(acpi_find(&fake, 0, &in), ACPI_BAD_TABLE);
    CHECK(same(in.detail, "the FADT gives no PM1a control port"));
}

/* From every 16-bit value read and every SLP_TYP, the value written differs
 * from the value read only in bits 10-13: SLP_TYP there, SLP_EN set. */
static void test_acpi_pm1_rmw_keeps_bits(void)
{
    uint32_t bad = 0;
    for (uint32_t v = 0; v <= 0xFFFFu; v++)
        for (uint32_t t = 0; t < 8; t++) {
            uint16_t w = acpi_pm1_value((uint16_t)v, (uint8_t)t);
            if (((w ^ v) & ~0x3C00u) || ((w >> 10) & 7u) != t || !(w & ACPI_SLP_EN) ||
                (w & ACPI_SCI_EN) != (v & ACPI_SCI_EN))
                bad++;
        }
    CHECK_EQ(bad, 0);
    CHECK_EQ(acpi_pm1_value(0x0001, 5), 0x3401);    /* SCI_EN kept */
    CHECK_EQ(acpi_pm1_value(0xFFFF, 0), 0xE3FF);
    CHECK_EQ(acpi_pm1_value(0x1C00, 0x0F), 0x3C00); /* only SLP_TYP's 3 bits */
}

static void test_texts(void)
{
    CHECK(same(acpi_why_text(ACPI_NO_TABLES), "no ACPI tables found"));
    CHECK(same(acpi_why_text(ACPI_BAD_TABLE), "an ACPI table failed its checks"));
    CHECK(same(acpi_why_text(ACPI_NO_S5), "no _S5 sleep state"));
    CHECK(same(acpi_why_text(ACPI_NO_ENABLE), "ACPI mode could not be enabled"));
    CHECK(same(acpi_why_text(ACPI_NO_POWEROFF), "the computer did not power off"));
}

int main(void)
{
    printf("acpi_core unit tests\n");
    RUN(test_good_tables);
    RUN(test_no_rsdp);
    RUN(test_bad_checksum);
    RUN(test_bad_length);
    RUN(test_truncated_s5);
    RUN(test_s5_at_dsdt_end);
    RUN(test_rsdt_entry_outside);
    RUN(test_xsdt);
    RUN(test_no_pm1b);
    RUN(test_acpi_pm1_rmw_keeps_bits);
    RUN(test_texts);
    return unit_report("acpi_core");
}
