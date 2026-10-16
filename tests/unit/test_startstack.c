/* Unit tests for the 1.0 stage 7 System V start block (kernel/proc/loadplan.c):
 * Linux's layout of argc, argv, envp, the auxiliary vector and the strings
 * in the user stack's top page, its 16-byte alignment, the 4096-byte limit
 * (E2BIG), and AT_PHDR for ELF files. */
#include <stdint.h>
#include <string.h>
#include "kernel/memory/vm_layout.h"
#include "kernel/proc/loadplan.h"
#include "tests/unit/unit.h"

static struct start_block b;

static uint64_t word(uint64_t addr)
{
    uint64_t off = addr - STARTBLOCK_BASE, v = 0;
    for (int i = 0; i < 8; i++)
        v |= (uint64_t)b.page[off + i] << (8 * i);
    return v;
}

static const char *str(uint64_t addr)
{
    return (const char *)&b.page[addr - STARTBLOCK_BASE];
}

static const struct start_aux aux = { 0x400010, 0, 0, 0x178BFBFF };

/* The auxiliary vector at `at`: Linux's order, the values given. */
static int auxv_ok(uint64_t at, const struct start_aux *a)
{
    const uint64_t want[STARTBLOCK_AUXC][2] = {
        { AT_HWCAP, a->hwcap }, { AT_PAGESZ, 4096 }, { AT_CLKTCK, 100 }, { AT_PHDR, a->phdr },
        { AT_PHENT, 56 }, { AT_PHNUM, a->phnum }, { AT_BASE, 0 }, { AT_FLAGS, 0 },
        { AT_ENTRY, a->entry }, { AT_UID, 0 }, { AT_EUID, 0 }, { AT_GID, 0 }, { AT_EGID, 0 },
        { AT_SECURE, 0 }, { AT_HWCAP2, 0 }, { AT_EXECFN, 0 }, { AT_PLATFORM, 0 }, { AT_NULL, 0 },
    };
    for (unsigned i = 0; i < STARTBLOCK_AUXC; i++) {
        if (word(at + 16 * i) != want[i][0])
            return 0;
        uint64_t v = word(at + 16 * i + 8);
        if (want[i][0] == AT_EXECFN || want[i][0] == AT_PLATFORM)
            continue;                                   /* pointers: checked by the caller */
        if (v != want[i][1])
            return 0;
    }
    return 1;
}

static void test_spin_default(void)
{
    /* spin, as the 0.10 ring-3 tests start it: argv { "spin" }. */
    CHECK_EQ(startblock_default_rsp("spin"), 0x00007FFFFFFFEE90ull);
    const char *argv[1] = { "spin" };
    CHECK_EQ(startblock_strings(&b, "spin", 1, argv, 0, 0, 0), 0);
    startblock_finish(&b, &aux);
    uint64_t rsp = startblock_rsp(&b);
    CHECK_EQ(rsp, 0x00007FFFFFFFEE90ull);
    CHECK_EQ(startblock_bytes(&b), 0x170);
    CHECK_EQ(word(rsp), 1);                                         /* argc */
    CHECK_EQ(word(rsp + 8), 0x00007FFFFFFFEFEEull);                 /* argv[0] */
    CHECK(strcmp(str(word(rsp + 8)), "spin") == 0);
    CHECK_EQ(word(rsp + 16), 0);                                    /* argv's NULL */
    CHECK_EQ(word(rsp + 24), 0);                                    /* envp's NULL */
    CHECK(auxv_ok(rsp + 32, &aux));
    CHECK_EQ(word(rsp + 32 + 16 * 15 + 8), 0x00007FFFFFFFEFF3ull);  /* AT_EXECFN */
    CHECK(strcmp(str(0x00007FFFFFFFEFF3ull), "spin") == 0);
    CHECK_EQ(word(rsp + 32 + 16 * 16 + 8), 0x00007FFFFFFFEFD9ull);  /* AT_PLATFORM */
    CHECK(strcmp(str(0x00007FFFFFFFEFD9ull), "x86_64") == 0);
    CHECK_EQ(word(VM_USER_STACK_TOP - 8), 0);                       /* the end marker */
    /* Padding between the auxiliary vector and "x86_64" is zero. */
    int zero = 1;
    for (uint64_t a = rsp + 32 + 16 * STARTBLOCK_AUXC; a < 0x00007FFFFFFFEFD9ull; a++)
        zero = zero && b.page[a - STARTBLOCK_BASE] == 0;
    CHECK(zero);
}

/* Every argc and envc parity: the layout rules hold. */
static void test_layout_rules(void)
{
    static const char *const words[] = { "a", "bb", "", "dddd", "eeeee", "ffffff" };
    static const char *const names[] = { "HOME", "X", "LONG_NAME_16CHRS" };
    static const char *const values[] = { "/home", "", "v" };
    int all = 1;
    for (uint32_t argc = 0; argc <= 6; argc++) {
        for (uint32_t envc = 0; envc <= 3; envc++) {
            int ok = startblock_strings(&b, "prog", argc, words, envc, names, values) == 0;
            startblock_finish(&b, &aux);
            uint64_t rsp = startblock_rsp(&b);
            ok = ok && (rsp & 15) == 0 && rsp >= STARTBLOCK_BASE && word(rsp) == argc;
            /* The strings: argv in order, then envp, then the name, then the end. */
            uint64_t s = argc ? word(rsp + 8) : 0;
            uint64_t first = s;
            for (uint32_t i = 0; ok && i < argc; i++) {
                ok = word(rsp + 8 + 8 * i) == s && strcmp(str(s), words[i]) == 0;
                s += strlen(words[i]) + 1;
            }
            ok = ok && word(rsp + 8 + 8 * argc) == 0;
            uint64_t envp = rsp + 8 + 8 * argc + 8;
            if (!argc && envc)
                s = first = word(envp);
            for (uint32_t i = 0; ok && i < envc; i++) {
                char want[64];
                strcpy(want, names[i]);
                strcat(want, "=");
                strcat(want, values[i]);
                ok = word(envp + 8 * i) == s && strcmp(str(s), want) == 0;
                s += strlen(want) + 1;
            }
            ok = ok && word(envp + 8 * envc) == 0;
            uint64_t auxv = envp + 8 * envc + 8;
            ok = ok && auxv_ok(auxv, &aux);
            uint64_t execfn = word(auxv + 16 * 15 + 8), plat = word(auxv + 16 * 16 + 8);
            if (!argc && !envc)
                s = first = execfn;
            ok = ok && execfn == s && strcmp(str(execfn), "prog") == 0 &&
                 s + 5 == VM_USER_STACK_TOP - 8 && word(VM_USER_STACK_TOP - 8) == 0;
            uint64_t want_plat = (first & ~15ull) - 7;
            uint64_t nwords = 1 + argc + 1 + envc + 1 + 2 * STARTBLOCK_AUXC;
            ok = ok && plat == want_plat && strcmp(str(plat), "x86_64") == 0 &&
                 auxv + 16 * STARTBLOCK_AUXC <= plat && rsp == ((plat - 8 * nwords) & ~15ull);
            if (!ok)
                printf("    layout wrong for argc %u, envc %u\n", argc, envc);
            all = all && ok;
        }
    }
    CHECK(all);
}

/* The limit: the largest block is exactly 4096 bytes; one byte more of
 * strings is E2BIG. */
static void test_limit(void)
{
    static char big[STARTBLOCK_SIZE + 1];
    const char *argv[2] = { "args", big };
    uint32_t best = 0;
    for (uint32_t n = 1; n < STARTBLOCK_SIZE; n++) {
        memset(big, 'b', n);
        big[n] = '\0';
        if (startblock_strings(&b, "args", 2, argv, 0, 0, 0) != 0)
            break;
        best = n;
    }
    CHECK(best > 3500);
    memset(big, 'b', best);
    big[best] = '\0';
    CHECK_EQ(startblock_strings(&b, "args", 2, argv, 0, 0, 0), 0);
    CHECK_EQ(startblock_bytes(&b), STARTBLOCK_SIZE);
    CHECK_EQ(b.rsp, 0);
    startblock_finish(&b, &aux);
    CHECK_EQ(word(STARTBLOCK_BASE), 2);
    CHECK(strcmp(str(word(STARTBLOCK_BASE + 16)), big) == 0);
    big[best] = 'b';
    big[best + 1] = '\0';
    CHECK_EQ(startblock_strings(&b, "args", 2, argv, 0, 0, 0), -STARTBLOCK_E2BIG);
    CHECK_EQ(STARTBLOCK_E2BIG, 7);                      /* Linux's E2BIG */
    /* Many empty arguments: the pointers count too. */
    static const char *many[600];
    for (int i = 0; i < 600; i++)
        many[i] = "";
    CHECK_EQ(startblock_strings(&b, "args", 600, many, 0, 0, 0), -STARTBLOCK_E2BIG);
    CHECK_EQ(startblock_strings(&b, "args", 400, many, 0, 0, 0), 0);
    CHECK(startblock_bytes(&b) <= STARTBLOCK_SIZE);
}

/* AT_PHDR as Linux computes it: the program headers' address if a loaded
 * segment's file bytes hold them, else 0; AT_PHNUM is e_phnum. */
static void test_phdr(void)
{
    static uint8_t file[512];
    memset(file, 0, sizeof(file));
    file[32] = 64;                                      /* e_phoff */
    file[56] = 3;                                       /* e_phnum */
    struct elf_plan plan;
    memset(&plan, 0, sizeof(plan));
    plan.count = 2;
    plan.seg[0] = (struct elf_segment){ 0x500000, 0x1000, 0x10, 0x20, true, false };
    plan.seg[1] = (struct elf_segment){ 0x400000, 0, 0x300, 0x300, false, true };
    uint64_t phdr = 1, phnum = 1;
    startblock_elf_phdr(file, &plan, &phdr, &phnum);
    CHECK_EQ(phdr, 0x400040);
    CHECK_EQ(phnum, 3);
    plan.seg[1].offset = 0x1000;                        /* no segment holds the headers */
    startblock_elf_phdr(file, &plan, &phdr, &phnum);
    CHECK_EQ(phdr, 0);
    CHECK_EQ(phnum, 3);
}

int main(void)
{
    RUN(test_spin_default);
    RUN(test_layout_rules);
    RUN(test_limit);
    RUN(test_phdr);
    return unit_report("startstack");
}
