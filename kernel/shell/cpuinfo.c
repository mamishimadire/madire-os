/* CPUID decoding (see cpuinfo.h). */
#include "kernel/shell/cpuinfo.h"

#define EAX 0
#define EBX 1
#define ECX 2
#define EDX 3

/* Copies the 4 bytes of a register (little-endian) as characters;
 * non-printable bytes become '?' so odd firmware can't corrupt output. */
static void put_reg(char *dst, uint32_t reg)
{
    for (int i = 0; i < 4; i++) {
        char c = (char)((reg >> (8 * i)) & 0xFF);
        dst[i] = (c >= 0x20 && c <= 0x7E) ? c : (c == 0 ? ' ' : '?');
    }
}

/* Removes leading and trailing spaces in place. */
static void trim(char *s)
{
    int start = 0, end = 0, n = 0;
    while (s[start] == ' ')
        start++;
    for (int i = start; s[i]; i++)
        if (s[i] != ' ')
            end = i + 1;
    for (int i = start; i < end; i++)
        s[n++] = s[i];
    s[n] = '\0';
}

void cpuinfo_decode(const struct cpuid_raw *raw, struct cpu_info *out)
{
    out->available = raw->available;
    out->has_signature = false;
    out->family = out->model = out->stepping = 0;
    out->edx_features = out->ecx_features = 0;
    out->vendor[0] = out->brand[0] = out->hypervisor[0] = '\0';
    if (!raw->available)
        return;

    /* Vendor: leaf 0 ebx, edx, ecx (in that order). */
    put_reg(out->vendor, raw->leaf0[EBX]);
    put_reg(out->vendor + 4, raw->leaf0[EDX]);
    put_reg(out->vendor + 8, raw->leaf0[ECX]);
    out->vendor[12] = '\0';
    trim(out->vendor);

    if (raw->has_leaf1) {
        uint32_t sig = raw->leaf1[EAX];
        uint32_t base_family = (sig >> 8) & 0xF;
        uint32_t base_model = (sig >> 4) & 0xF;
        out->stepping = sig & 0xF;
        out->family = base_family;
        out->model = base_model;
        if (base_family == 0xF)
            out->family += (sig >> 20) & 0xFF;              /* extended family */
        if (base_family == 0x6 || base_family == 0xF)
            out->model += ((sig >> 16) & 0xF) << 4;         /* extended model */
        out->edx_features = raw->leaf1[EDX];
        out->ecx_features = raw->leaf1[ECX];
        out->has_signature = true;
    }

    if (raw->has_brand) {
        for (int i = 0; i < 12; i++)
            put_reg(out->brand + 4 * i, raw->brand[i]);
        out->brand[48] = '\0';
        trim(out->brand);
    }

    if (raw->has_hv) {
        put_reg(out->hypervisor, raw->hv[EBX]);
        put_reg(out->hypervisor + 4, raw->hv[ECX]);
        put_reg(out->hypervisor + 8, raw->hv[EDX]);
        out->hypervisor[12] = '\0';
        trim(out->hypervisor);
    }
}

struct feature {
    bool in_ecx;
    uint8_t bit;
    const char *name;
};

static const struct feature features[] = {
    { false, 0, "fpu" },   { false, 4, "tsc" },   { false, 5, "msr" },
    { false, 6, "pae" },   { false, 9, "apic" },  { false, 15, "cmov" },
    { false, 23, "mmx" },  { false, 25, "sse" },  { false, 26, "sse2" },
    { false, 28, "htt" },
    { true, 0, "sse3" },   { true, 9, "ssse3" },  { true, 19, "sse4.1" },
    { true, 20, "sse4.2" }, { true, 23, "popcnt" }, { true, 28, "avx" },
    { true, 31, "hypervisor" },
};

size_t cpuinfo_features(const struct cpu_info *c, char *buf, size_t size)
{
    size_t len = 0;
    if (size == 0)
        return 0;
    for (size_t i = 0; i < sizeof(features) / sizeof(features[0]); i++) {
        uint32_t reg = features[i].in_ecx ? c->ecx_features : c->edx_features;
        if (!((reg >> features[i].bit) & 1))
            continue;
        if (len && len + 1 < size)
            buf[len++] = ' ';
        for (const char *p = features[i].name; *p && len + 1 < size; p++)
            buf[len++] = *p;
    }
    if (len == 0)
        for (const char *p = "none"; *p && len + 1 < size; p++)
            buf[len++] = *p;
    buf[len] = '\0';
    return len;
}
