/* Unit tests for the 0.10 SMEP / SMAP page-fault cause (pf_smx_str,
 * kernel/arch/x86/pagefault.h). */
#include <stdint.h>
#include <string.h>
#include "kernel/arch/x86/pagefault.h"
#include "tests/unit/unit.h"

#define SMEP_MSG "SMEP violation (the kernel executed a user page)"
#define SMAP_RD  "SMAP violation (the kernel read a user page, AC=0)"
#define SMAP_WR  "SMAP violation (the kernel wrote a user page, AC=0)"

#define BOTH     ((uint64_t)CR4_SMEP | CR4_SMAP)
#define AC       PF_RFLAGS_AC
#define USER_VA  0x0000000000400000ull
#define KERN_VA  0xFFFFFFFF80100000ull

static int is(const char *got, const char *want)
{
    return got && strcmp(got, want) == 0;
}

static void test_smep(void)
{
    uint32_t fetch = PF_PRESENT | PF_FETCH;                     /* 0x11, as the CPU reports it */
    CHECK(is(pf_smx_str(fetch, USER_VA, BOTH, 0x2), SMEP_MSG));
    CHECK(is(pf_smx_str(fetch, USER_VA, CR4_SMEP, 0x2), SMEP_MSG));
    CHECK(is(pf_smx_str(fetch, USER_VA, BOTH, 0x2 | AC), SMEP_MSG)); /* AC never lifts SMEP */
    CHECK(pf_smx_str(fetch, USER_VA, CR4_SMAP, 0x2) == NULL);    /* SMEP off: not named */
    CHECK(pf_smx_str(fetch, USER_VA, 0, 0x2) == NULL);
    CHECK(pf_smx_str(fetch, KERN_VA, BOTH, 0x2) == NULL);        /* kernel half: NX, as before */
    CHECK(pf_smx_str(PF_FETCH, USER_VA, BOTH, 0x2) == NULL);     /* not present */
    CHECK(pf_smx_str(fetch | PF_USER, USER_VA, BOTH, 0x2) == NULL); /* the program's own fault */
    CHECK(pf_smx_str(fetch | PF_RESERVED, USER_VA, BOTH, 0x2) == NULL);
    /* The NX decoding itself is unchanged (test_nx.c). */
    CHECK(strcmp(pf_fetch_str(fetch), ", instruction fetch (NX violation)") == 0);
}

static void test_smap(void)
{
    uint32_t rd = PF_PRESENT, wr = PF_PRESENT | PF_WRITE;
    CHECK(is(pf_smx_str(rd, USER_VA, BOTH, 0x2), SMAP_RD));
    CHECK(is(pf_smx_str(wr, USER_VA, BOTH, 0x2), SMAP_WR));
    CHECK(is(pf_smx_str(rd, USER_VA, CR4_SMAP, 0x2), SMAP_RD));
    CHECK(pf_smx_str(rd, USER_VA, BOTH, 0x2 | AC) == NULL);      /* AC=1: SMAP lifted */
    CHECK(pf_smx_str(wr, USER_VA, BOTH, 0x2 | AC) == NULL);
    CHECK(pf_smx_str(rd, USER_VA, CR4_SMEP, 0x2) == NULL);       /* SMAP off */
    CHECK(pf_smx_str(rd, KERN_VA, BOTH, 0x2) == NULL);
    CHECK(pf_smx_str(0, USER_VA, BOTH, 0x2) == NULL);            /* not present */
    CHECK(pf_smx_str(rd | PF_USER, USER_VA, BOTH, 0x2) == NULL);
    CHECK(pf_smx_str(wr | PF_USER, USER_VA, BOTH, 0x2) == NULL);
    CHECK(pf_smx_str(rd | PF_RESERVED, USER_VA, BOTH, 0x2) == NULL);
}

static void test_boundary(void)
{
    CHECK(is(pf_smx_str(PF_PRESENT, 0x00007FFFFFFFFFFFull, BOTH, 0), SMAP_RD));
    CHECK(pf_smx_str(PF_PRESENT, 0x0000800000000000ull, BOTH, 0) == NULL);
    CHECK(pf_smx_str(PF_PRESENT, 0xFFFF800000000000ull, BOTH, 0) == NULL);
    CHECK(is(pf_smx_str(PF_PRESENT, 0, BOTH, 0), SMAP_RD));
    CHECK_EQ(PF_USER_LIMIT, 0x0000800000000000ull);
    CHECK_EQ(PF_RFLAGS_AC, 1ull << 18);
    /* Every message fits the crash screen after " Cause: " (79 columns). */
    CHECK(8 + strlen(SMEP_MSG) <= 79);
    CHECK(8 + strlen(SMAP_RD) <= 79);
    CHECK(8 + strlen(SMAP_WR) <= 79);
}

int main(void)
{
    RUN(test_smep);
    RUN(test_smap);
    RUN(test_boundary);
    return unit_report("pfcause010");
}
