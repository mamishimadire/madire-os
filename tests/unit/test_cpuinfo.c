/* Unit tests for kernel/shell/cpuinfo.c (CPUID decoding). */
#include <stdint.h>
#include <string.h>
#include "kernel/shell/cpuinfo.h"
#include "tests/unit/unit.h"

static struct cpuid_raw raw;
static struct cpu_info info;

/* Packs 4 characters into a register (little-endian, as CPUID returns). */
static uint32_t reg(const char *s)
{
    return (uint32_t)(uint8_t)s[0] | (uint32_t)(uint8_t)s[1] << 8
         | (uint32_t)(uint8_t)s[2] << 16 | (uint32_t)(uint8_t)s[3] << 24;
}

static void base(const char *vendor)
{
    memset(&raw, 0, sizeof(raw));
    raw.available = true;
    raw.max_basic = 1;
    raw.leaf0[0] = 1;
    raw.leaf0[1] = reg(vendor);         /* ebx */
    raw.leaf0[3] = reg(vendor + 4);     /* edx */
    raw.leaf0[2] = reg(vendor + 8);     /* ecx */
}

static void set_brand(const char *brand48)
{
    for (int i = 0; i < 12; i++)
        raw.brand[i] = reg(brand48 + 4 * i);
    raw.has_brand = true;
}

static void test_cpuid_unavailable(void)
{
    memset(&raw, 0, sizeof(raw));
    memset(&info, 0x55, sizeof(info));
    cpuinfo_decode(&raw, &info);
    CHECK(!info.available);
    CHECK(!info.has_signature);
    CHECK_EQ(info.vendor[0], 0);
    CHECK_EQ(info.brand[0], 0);
    CHECK_EQ(info.hypervisor[0], 0);
}

static void test_vendor_string(void)
{
    base("GenuineIntel");
    cpuinfo_decode(&raw, &info);
    CHECK(info.available);
    CHECK(strcmp(info.vendor, "GenuineIntel") == 0);
    base("AuthenticAMD");
    cpuinfo_decode(&raw, &info);
    CHECK(strcmp(info.vendor, "AuthenticAMD") == 0);
}

static void test_family_model_stepping(void)
{
    base("GenuineIntel");
    raw.has_leaf1 = true;
    /* family 6, ext model 0xB, model 0xA, stepping 3 -> model 0xBA = 186 */
    raw.leaf1[0] = (0xBu << 16) | (0x6u << 8) | (0xAu << 4) | 0x3u;
    cpuinfo_decode(&raw, &info);
    CHECK(info.has_signature);
    CHECK_EQ(info.family, 6);
    CHECK_EQ(info.model, 186);
    CHECK_EQ(info.stepping, 3);

    /* family 0xF + ext family 0x8 = 23; model uses ext model too */
    raw.leaf1[0] = (0x08u << 20) | (0x7u << 16) | (0xFu << 8) | (0x1u << 4) | 0x0u;
    cpuinfo_decode(&raw, &info);
    CHECK_EQ(info.family, 23);
    CHECK_EQ(info.model, 0x71);

    /* family 5 (Pentium): extended fields ignored */
    raw.leaf1[0] = (0x3u << 20) | (0x9u << 16) | (0x5u << 8) | (0x2u << 4) | 0x1u;
    cpuinfo_decode(&raw, &info);
    CHECK_EQ(info.family, 5);
    CHECK_EQ(info.model, 2);
    CHECK_EQ(info.stepping, 1);
}

static void test_no_leaf1(void)
{
    base("GenuineIntel");
    raw.max_basic = 0;
    cpuinfo_decode(&raw, &info);
    CHECK(info.available);
    CHECK(!info.has_signature);
    CHECK(strcmp(info.vendor, "GenuineIntel") == 0);
}

static void test_brand_string(void)
{
    base("GenuineIntel");
    /* Intel pads the brand with leading spaces; trimmed on both sides. */
    set_brand("      Madire Test CPU @ 1.30GHz\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0");
    cpuinfo_decode(&raw, &info);
    CHECK(strcmp(info.brand, "Madire Test CPU @ 1.30GHz") == 0);

    base("GenuineIntel");
    cpuinfo_decode(&raw, &info);                        /* no brand leaves */
    CHECK_EQ(info.brand[0], 0);

    base("GenuineIntel");
    set_brand("Exactly-48-characters-long-brand-string-xxxxxxx!");
    cpuinfo_decode(&raw, &info);
    CHECK_EQ(strlen(info.brand), 48);
}

static void test_hypervisor(void)
{
    base("GenuineIntel");
    raw.has_hv = true;
    raw.hv[1] = reg("VBox");
    raw.hv[2] = reg("VBox");
    raw.hv[3] = reg("VBox");
    cpuinfo_decode(&raw, &info);
    CHECK(strcmp(info.hypervisor, "VBoxVBoxVBox") == 0);

    raw.hv[1] = reg("KVMK");
    raw.hv[2] = reg("VMKV");
    raw.hv[3] = reg("M\0\0\0");
    cpuinfo_decode(&raw, &info);
    CHECK(strcmp(info.hypervisor, "KVMKVMKVM") == 0);   /* NUL padding trimmed */

    raw.has_hv = false;
    cpuinfo_decode(&raw, &info);
    CHECK_EQ(info.hypervisor[0], 0);
}

static void test_feature_list(void)
{
    char buf[128];
    base("GenuineIntel");
    raw.has_leaf1 = true;
    raw.leaf1[3] = (1u << 0) | (1u << 4) | (1u << 6) | (1u << 25) | (1u << 26);
    raw.leaf1[2] = (1u << 0) | (1u << 31);
    cpuinfo_decode(&raw, &info);
    cpuinfo_features(&info, buf, sizeof(buf));
    CHECK(strcmp(buf, "fpu tsc pae sse sse2 sse3 hypervisor") == 0);

    raw.leaf1[3] = raw.leaf1[2] = 0;
    cpuinfo_decode(&raw, &info);
    CHECK_EQ(cpuinfo_features(&info, buf, sizeof(buf)), 4);
    CHECK(strcmp(buf, "none") == 0);

    raw.leaf1[3] = 0xFFFFFFFFu;
    raw.leaf1[2] = 0xFFFFFFFFu;
    cpuinfo_decode(&raw, &info);
    char small[10];
    cpuinfo_features(&info, small, sizeof(small));       /* truncated safely */
    CHECK_EQ(strlen(small), 9);
    CHECK(strncmp(small, "fpu tsc m", 9) == 0);
}

static void test_odd_bytes_sanitised(void)
{
    /* 12 bytes, two of them non-printable (split literals keep the hex
     * escapes from swallowing the next letters). */
    base("Genu" "\x01" "neI" "\xff" "tel");
    cpuinfo_decode(&raw, &info);
    CHECK(strcmp(info.vendor, "Genu?neI?tel") == 0);
    for (size_t i = 0; info.vendor[i]; i++)
        CHECK(info.vendor[i] >= 0x20 && info.vendor[i] <= 0x7E);
}

int main(void)
{
    printf("cpuinfo unit tests\n");
    RUN(test_cpuid_unavailable);
    RUN(test_vendor_string);
    RUN(test_family_model_stepping);
    RUN(test_no_leaf1);
    RUN(test_brand_string);
    RUN(test_hypervisor);
    RUN(test_feature_list);
    RUN(test_odd_bytes_sanitised);
    return unit_report("cpuinfo");
}
