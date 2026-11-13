/* The ACPI tables for the power-off, the pure part (1.5 stage 6): see
 * acpi_core.h. Every static is acpi_c_-prefixed. */
#include "kernel/power/acpi_core.h"

#define ACPI_C_HDR      36u             /* a table's header; also the RSDP's revision-2 size */
#define ACPI_C_RSDP1    20u             /* the RSDP's revision-0 part (its first checksum) */
#define ACPI_C_FADT_X   148u            /* the FADT reaches X_DSDT (offset 140, 8 bytes) */
#define ACPI_C_EBDA_LEN 1024u
#define ACPI_C_BIOS     0xE0000u
#define ACPI_C_BIOS_LEN 0x20000u

/* The AML bytes of the _S5_ shortcut. */
#define ACPI_C_NAMEOP   0x08u
#define ACPI_C_ROOT     0x5Cu           /* '\' */
#define ACPI_C_PKGOP    0x12u
#define ACPI_C_BYTEPFX  0x0Au
#define ACPI_C_ZERO     0x00u
#define ACPI_C_ONE      0x01u
#define ACPI_C_S5_WIN   24u             /* bytes read from "_S5_" on, at most */

static uint32_t acpi_c_le32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static uint64_t acpi_c_le64(const uint8_t *p)
{
    return (uint64_t)acpi_c_le32(p) | (uint64_t)acpi_c_le32(p + 4) << 32;
}

static bool acpi_c_same(const uint8_t *p, const char *s, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++)
        if (p[i] != (uint8_t)s[i])
            return false;
    return true;
}

const char *acpi_why_text(enum acpi_why w)
{
    switch (w) {
    case ACPI_OK:          return "found";
    case ACPI_NO_TABLES:   return "no ACPI tables found";
    case ACPI_BAD_TABLE:   return "an ACPI table failed its checks";
    case ACPI_NO_S5:       return "no _S5 sleep state";
    case ACPI_NO_ENABLE:   return "ACPI mode could not be enabled";
    case ACPI_NO_POWEROFF: return "the computer did not power off";
    }
    return "unknown";
}

/* One read through the caller: 1 to ACPI_CHUNK bytes, no address wrap. */
static int acpi_c_read(const struct acpi_mem *m, uint64_t phys, uint32_t len, uint8_t *buf)
{
    if (len == 0 || len > ACPI_CHUNK || phys > UINT64_MAX - len)
        return -1;
    return m->read(m->ctx, phys, len, buf) ? -1 : 0;
}

/* The sum of the bytes [phys, phys + len) modulo 256, or -1 if any cannot
 * be read. */
static int acpi_c_sum(const struct acpi_mem *m, uint64_t phys, uint32_t len)
{
    uint8_t buf[ACPI_CHUNK];
    uint8_t sum = 0;
    if (phys > UINT64_MAX - len)
        return -1;
    while (len) {
        uint32_t n = len < ACPI_CHUNK ? len : ACPI_CHUNK;
        if (acpi_c_read(m, phys, n, buf))
            return -1;
        for (uint32_t i = 0; i < n; i++)
            sum = (uint8_t)(sum + buf[i]);
        phys += n;
        len -= n;
    }
    return sum;
}

static enum acpi_why acpi_c_fail(struct acpi_info *in, enum acpi_why w, const char *detail)
{
    in->detail = detail;
    return w;
}

/* ---- the RSDP ---- */

/* True if a valid RSDP starts at phys (the rules in acpi_core.h); *in
 * then holds its address, revision and root table. */
static bool acpi_c_rsdp_at(const struct acpi_mem *m, uint64_t phys, struct acpi_info *in)
{
    uint8_t b[ACPI_C_HDR];
    if (acpi_c_read(m, phys, ACPI_C_RSDP1, b) || !acpi_c_same(b, "RSD PTR ", 8) ||
        acpi_c_sum(m, phys, ACPI_C_RSDP1) != 0)
        return false;
    uint8_t rev = b[15];
    uint64_t root = acpi_c_le32(b + 16);
    bool x = false;
    if (rev >= 2) {
        /* Its length (offset 20) first; the rest only inside that length. */
        if (acpi_c_read(m, phys, 24, b))
            return false;
        uint32_t len = acpi_c_le32(b + 20);
        if (len < ACPI_C_HDR || len > ACPI_RSDP_MAX || acpi_c_read(m, phys, ACPI_C_HDR, b) ||
            acpi_c_sum(m, phys, len) != 0)
            return false;
        uint64_t xa = acpi_c_le64(b + 24);
        if (xa) {
            root = xa;
            x = true;
        }
    }
    in->rsdp = phys;
    in->revision = rev;
    in->root = root;
    in->xsdt = x;
    return true;
}

/* The RSDP searched 16 bytes apart in [base, base + size). */
static bool acpi_c_scan(const struct acpi_mem *m, uint64_t base, uint32_t size, struct acpi_info *in)
{
    uint8_t buf[ACPI_CHUNK];
    for (uint32_t off = 0; off < size; off += ACPI_CHUNK) {
        uint32_t n = size - off < ACPI_CHUNK ? size - off : ACPI_CHUNK;
        if (acpi_c_read(m, base + off, n, buf))
            continue;                   /* nothing readable here */
        for (uint32_t p = 0; p + 8u <= n; p += 16u)
            if (acpi_c_same(buf + p, "RSD PTR ", 8) && acpi_c_rsdp_at(m, base + off + p, in))
                return true;
    }
    return false;
}

/* ---- the tables ---- */

/* The table at phys signed `sig`: readable, its length from `min` to `max`
 * (not wrapping), its checksum over that length right. ACPI_OK with *len,
 * or ACPI_BAD_TABLE with the detail. */
static enum acpi_why acpi_c_table(const struct acpi_mem *m, uint64_t phys, const char *sig, uint32_t min,
                                  uint32_t max, uint32_t *len, struct acpi_info *in, const char *const *what)
{
    uint8_t h[ACPI_C_HDR];
    if (phys == 0 || acpi_c_read(m, phys, ACPI_C_HDR, h))
        return acpi_c_fail(in, ACPI_BAD_TABLE, what[0]);
    if (!acpi_c_same(h, sig, 4))
        return acpi_c_fail(in, ACPI_BAD_TABLE, what[1]);
    uint32_t n = acpi_c_le32(h + 4);
    if (n < min || n > max || phys > UINT64_MAX - n)
        return acpi_c_fail(in, ACPI_BAD_TABLE, what[2]);
    if (acpi_c_sum(m, phys, n) != 0)
        return acpi_c_fail(in, ACPI_BAD_TABLE, what[3]);
    *len = n;
    return ACPI_OK;
}

static const char *const acpi_c_root_what[4] = { "the RSDT or XSDT cannot be read",
                                                 "the RSDT or XSDT has a wrong signature",
                                                 "the RSDT or XSDT has a bad length",
                                                 "the RSDT or XSDT has a bad checksum" };
static const char *const acpi_c_fadt_what[4] = { "the FADT cannot be read", "the FADT has a wrong signature",
                                                 "the FADT has a bad length", "the FADT has a bad checksum" };
static const char *const acpi_c_dsdt_what[4] = { "the DSDT cannot be read", "the DSDT has a wrong signature",
                                                 "the DSDT has a bad length", "the DSDT has a bad checksum" };

/* The FADT: the first entry of the RSDT or XSDT whose table is signed
 * "FACP"; every entry read inside the root's length, every entry's table
 * readable. */
static enum acpi_why acpi_c_find_fadt(const struct acpi_mem *m, uint32_t root_len, struct acpi_info *in)
{
    uint32_t esz = in->xsdt ? 8u : 4u;
    if ((root_len - ACPI_C_HDR) % esz)
        return acpi_c_fail(in, ACPI_BAD_TABLE, "the RSDT or XSDT has a bad length");
    in->entries = (root_len - ACPI_C_HDR) / esz;
    for (uint32_t i = 0; i < in->entries; i++) {
        uint8_t e[8], sig[4];
        if (acpi_c_read(m, in->root + ACPI_C_HDR + (uint64_t)i * esz, esz, e))
            return acpi_c_fail(in, ACPI_BAD_TABLE, "the RSDT or XSDT cannot be read");
        uint64_t a = esz == 8u ? acpi_c_le64(e) : acpi_c_le32(e);
        if (a == 0 || acpi_c_read(m, a, 4, sig))
            return acpi_c_fail(in, ACPI_BAD_TABLE, "an RSDT or XSDT entry points outside memory");
        if (acpi_c_same(sig, "FACP", 4)) {
            in->fadt = a;
            return ACPI_OK;
        }
    }
    return acpi_c_fail(in, ACPI_BAD_TABLE, "no FADT");
}

/* The FADT's fields (inside its stated length) and its checks. */
static enum acpi_why acpi_c_fadt(const struct acpi_mem *m, struct acpi_info *in)
{
    uint32_t len = 0;
    enum acpi_why w = acpi_c_table(m, in->fadt, "FACP", ACPI_FADT_MIN, ACPI_TABLE_MAX, &len, in, acpi_c_fadt_what);
    if (w != ACPI_OK)
        return w;
    in->fadt_len = len;
    uint8_t f[ACPI_C_FADT_X];
    uint32_t n = len < ACPI_C_FADT_X ? len : ACPI_C_FADT_X;
    if (acpi_c_read(m, in->fadt, n, f))
        return acpi_c_fail(in, ACPI_BAD_TABLE, "the FADT cannot be read");
    uint64_t dsdt = acpi_c_le32(f + 40);
    if (n >= ACPI_C_FADT_X && acpi_c_le64(f + 140))
        dsdt = acpi_c_le64(f + 140);    /* X_DSDT */
    uint32_t smi = acpi_c_le32(f + 48), pm1a = acpi_c_le32(f + 64), pm1b = acpi_c_le32(f + 68);
    if (pm1a == 0 || pm1a > 0xFFFFu || f[89] < 2u)
        return acpi_c_fail(in, ACPI_BAD_TABLE, "the FADT gives no PM1a control port");
    if (pm1b > 0xFFFFu || smi > 0xFFFFu)
        return acpi_c_fail(in, ACPI_BAD_TABLE, "the FADT gives a port above 0xffff");
    in->dsdt = dsdt;
    in->smi_cmd = smi;
    in->acpi_enable = f[52];
    in->pm1a_cnt = (uint16_t)pm1a;
    in->pm1b_cnt = (uint16_t)pm1b;
    return ACPI_OK;
}

/* ---- _S5_ ---- */

/* One value of the package at b[*i] (below `end`): BytePrefix and a byte,
 * Zero or One; at most 7. */
static bool acpi_c_value(const uint8_t *b, uint32_t *i, uint32_t end, uint8_t *v)
{
    if (*i >= end)
        return false;
    uint8_t op = b[*i];
    uint32_t val;
    if (op == ACPI_C_ZERO || op == ACPI_C_ONE) {
        val = op;
        *i += 1;
    } else if (op == ACPI_C_BYTEPFX) {
        if (*i + 1 >= end)
            return false;
        val = b[*i + 1];
        *i += 2;
    } else {
        return false;
    }
    if (val > 7u)
        return false;
    *v = (uint8_t)val;
    return true;
}

/* The package after the "_S5_" at DSDT offset `at` (the name checked by
 * the caller), every byte inside the DSDT's `len`. */
static enum acpi_why acpi_c_s5_package(const struct acpi_mem *m, uint32_t at, struct acpi_info *in)
{
    uint8_t b[ACPI_C_S5_WIN];
    uint32_t len = in->dsdt_len;
    uint32_t n = len - at < ACPI_C_S5_WIN ? len - at : ACPI_C_S5_WIN;
    if (acpi_c_read(m, in->dsdt + at, n, b))
        return acpi_c_fail(in, ACPI_BAD_TABLE, "the DSDT cannot be read");
    uint32_t i = 4;                     /* after "_S5_" */
    if (i + 1 >= n || b[i] != ACPI_C_PKGOP)
        return acpi_c_fail(in, ACPI_NO_S5, "_S5_ is not followed by a package");
    i++;                                /* the PkgLength's lead byte */
    uint32_t lead = b[i], more = lead >> 6, plen = more ? (lead & 0x0Fu) : (lead & 0x3Fu);
    if (i + more >= n)
        return acpi_c_fail(in, ACPI_NO_S5, "the _S5_ package is cut off");
    for (uint32_t k = 1; k <= more; k++)
        plen |= (uint32_t)b[i + k] << (4u + 8u * (k - 1u));
    /* The package ends `plen` bytes after the lead byte (which it counts),
     * inside the DSDT. */
    if (plen > len - (at + i))
        return acpi_c_fail(in, ACPI_NO_S5, "the _S5_ package goes past the DSDT's end");
    uint32_t end = i + plen;            /* in b's offsets */
    if (end > n)
        end = n;                        /* the values needed lie well inside the window */
    i += 1 + more;                      /* NumElements */
    if (i >= end)
        return acpi_c_fail(in, ACPI_NO_S5, "the _S5_ package is cut off");
    uint32_t count = b[i++];
    if (count < 2u)
        return acpi_c_fail(in, ACPI_NO_S5, "the _S5_ package has fewer than two values");
    if (!acpi_c_value(b, &i, end, &in->slp_typa) || !acpi_c_value(b, &i, end, &in->slp_typb))
        return acpi_c_fail(in, ACPI_NO_S5, "the _S5_ package's values cannot be decoded");
    return ACPI_OK;
}

/* True if the "_S5_" at DSDT offset `at` is a name definition: NameOp
 * right before it, or NameOp and '\' (both after the header). */
static bool acpi_c_s5_named(const struct acpi_mem *m, uint32_t at, struct acpi_info *in)
{
    uint8_t p[2] = { 0, 0 };
    if (at < ACPI_C_HDR + 1u || acpi_c_read(m, in->dsdt + at - 1u, 1, p + 1))
        return false;
    if (p[1] == ACPI_C_NAMEOP)
        return true;
    if (p[1] != ACPI_C_ROOT || at < ACPI_C_HDR + 2u || acpi_c_read(m, in->dsdt + at - 2u, 1, p))
        return false;
    return p[0] == ACPI_C_NAMEOP;
}

/* "_S5_" searched in the DSDT's AML (after its header, inside its stated
 * length), chunk by chunk with 3 bytes of overlap; the first name
 * definition is decoded. */
static enum acpi_why acpi_c_s5(const struct acpi_mem *m, struct acpi_info *in)
{
    uint8_t buf[ACPI_CHUNK];
    uint32_t len = in->dsdt_len;
    for (uint32_t off = ACPI_C_HDR; off + 4u <= len;) {
        uint32_t n = len - off < ACPI_CHUNK ? len - off : ACPI_CHUNK;
        if (acpi_c_read(m, in->dsdt + off, n, buf))
            return acpi_c_fail(in, ACPI_BAD_TABLE, "the DSDT cannot be read");
        for (uint32_t p = 0; p + 4u <= n; p++)
            if (acpi_c_same(buf + p, "_S5_", 4) && acpi_c_s5_named(m, off + p, in))
                return acpi_c_s5_package(m, off + p, in);
        off += n - 3u;                  /* n >= 4 here */
    }
    return acpi_c_fail(in, ACPI_NO_S5, "no _S5_ name in the DSDT");
}

/* ---- the whole path ---- */

enum acpi_why acpi_find(const struct acpi_mem *m, uint64_t ebda, struct acpi_info *in)
{
    uint8_t *z = (uint8_t *)in;
    for (uint32_t i = 0; i < sizeof *in; i++)
        z[i] = 0;
    if (!(ebda && acpi_c_scan(m, ebda, ACPI_C_EBDA_LEN, in)) && !acpi_c_scan(m, ACPI_C_BIOS, ACPI_C_BIOS_LEN, in))
        return acpi_c_fail(in, ACPI_NO_TABLES, "no RSDP in the EBDA or 0xE0000-0xFFFFF");
    uint32_t root_len = 0;
    enum acpi_why w = acpi_c_table(m, in->root, in->xsdt ? "XSDT" : "RSDT", ACPI_C_HDR, ACPI_TABLE_MAX, &root_len, in,
                                   acpi_c_root_what);
    if (w == ACPI_OK)
        w = acpi_c_find_fadt(m, root_len, in);
    if (w == ACPI_OK)
        w = acpi_c_fadt(m, in);
    if (w == ACPI_OK)
        w = acpi_c_table(m, in->dsdt, "DSDT", ACPI_C_HDR, ACPI_DSDT_MAX, &in->dsdt_len, in, acpi_c_dsdt_what);
    if (w == ACPI_OK)
        w = acpi_c_s5(m, in);
    return w;
}

uint16_t acpi_pm1_value(uint16_t read, uint8_t slp_typ)
{
    return (uint16_t)((read & ~ACPI_SLP_TYP) | ((uint32_t)(slp_typ & 7u) << 10) | ACPI_SLP_EN);
}
