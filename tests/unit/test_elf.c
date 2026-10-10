/* Unit tests for the 0.11 ELF64 checks (kernel/proc/elf_core.c). The base
 * image is shaped like the real build/user/hello.elf: code at 0x400000 from
 * file offset 0x1000, a bss-only data segment at 0x500000, PT_GNU_STACK RW.
 * Each case changes one thing and expects one exact result. */
#include <stdint.h>
#include <string.h>
#include "kernel/proc/elf_core.h"
#include "tests/unit/unit.h"

#define TEXT_SIZE 0x200u
#define FILE_LEN  (0x1000u + TEXT_SIZE)
#define PHOFF     64u
#define PH(i)     (PHOFF + 56u * (i))

static uint8_t img[ELF_FILE_MAX + 16];
static uint32_t len;

static void w16(uint32_t at, uint16_t v) { img[at] = (uint8_t)v; img[at + 1] = (uint8_t)(v >> 8); }
static void w32(uint32_t at, uint32_t v) { for (int i = 0; i < 4; i++) img[at + i] = (uint8_t)(v >> (8 * i)); }
static void w64(uint32_t at, uint64_t v) { for (int i = 0; i < 8; i++) img[at + i] = (uint8_t)(v >> (8 * i)); }

static void phdr(unsigned i, uint32_t type, uint32_t flags, uint64_t off, uint64_t va,
                 uint64_t filesz, uint64_t memsz, uint64_t align)
{
    w32(PH(i), type);
    w32(PH(i) + 4, flags);
    w64(PH(i) + 8, off);
    w64(PH(i) + 16, va);
    w64(PH(i) + 24, va);            /* p_paddr: ignored */
    w64(PH(i) + 32, filesz);
    w64(PH(i) + 40, memsz);
    w64(PH(i) + 48, align);
}

/* The good image: 3 program headers. Nothing is ever written past FILE_LEN,
 * so the rest of img stays zero (the ELF_FILE_MAX case relies on that). */
static void base(void)
{
    memset(img, 0, FILE_LEN);
    len = FILE_LEN;
    img[0] = 0x7F; img[1] = 'E'; img[2] = 'L'; img[3] = 'F';
    img[4] = 2;                     /* ELFCLASS64 */
    img[5] = 1;                     /* little-endian */
    img[6] = 1;                     /* EV_CURRENT */
    img[7] = 0;                     /* System V */
    w16(16, 2);                     /* ET_EXEC */
    w16(18, 62);                    /* x86-64 */
    w32(20, 1);
    w64(24, 0x400000);              /* entry */
    w64(32, PHOFF);
    w16(52, 64);
    w16(54, 56);
    w16(56, 3);
    phdr(0, 1, 5, 0x1000, 0x400000, TEXT_SIZE, TEXT_SIZE, 0x1000);      /* LOAD R+X */
    phdr(1, 1, 6, 0, 0x500000, 0, 0x1388, 0x1000);                      /* LOAD R+W, bss only */
    phdr(2, 0x6474E551, 6, 0, 0, 0, 0, 0x10);                           /* GNU_STACK RW */
    for (uint32_t i = 0; i < TEXT_SIZE; i++)
        img[0x1000 + i] = (uint8_t)(0x90 + i);
}

static enum elf_result run(void)
{
    return elf_check(img, len, NULL);
}

#define EXPECT(r) CHECK_EQ(run(), (r))

static void test_good_image_and_plan(void)
{
    base();
    struct elf_plan p;
    memset(&p, 0xA5, sizeof(p));
    CHECK_EQ(elf_check(img, len, &p), ELF_OK);
    CHECK_EQ(p.entry, 0x400000);
    CHECK_EQ(p.count, 2);
    CHECK_EQ(p.pages, 3);                                   /* 1 code page, 2 data pages */
    CHECK_EQ(p.seg[0].vaddr, 0x400000);
    CHECK_EQ(p.seg[0].offset, 0x1000);
    CHECK_EQ(p.seg[0].filesz, TEXT_SIZE);
    CHECK_EQ(p.seg[0].memsz, TEXT_SIZE);
    CHECK(p.seg[0].exec && !p.seg[0].write);
    CHECK_EQ(p.seg[1].vaddr, 0x500000);
    CHECK_EQ(p.seg[1].filesz, 0);
    CHECK_EQ(p.seg[1].memsz, 0x1388);
    CHECK(p.seg[1].write && !p.seg[1].exec);
    CHECK_EQ(elf_check(img, len, NULL), ELF_OK);            /* the plan is optional */
    CHECK(strcmp(elf_result_str(ELF_OK), "OK") == 0);
}

static void test_file_size(void)
{
    base(); len = 0;                EXPECT(ELF_ERR_EMPTY);
    base(); len = ELF_FILE_MAX + 1; EXPECT(ELF_ERR_TOO_BIG);
    base(); len = 63;               EXPECT(ELF_ERR_SHORT);
    base(); len = 1;                EXPECT(ELF_ERR_SHORT);
    /* Every shorter length is refused, and none reads past the end. */
    unsigned refused = 0;
    for (uint32_t l = 0; l < FILE_LEN; l++) {
        base(); len = l;
        refused += run() != ELF_OK;
    }
    CHECK_EQ(refused, FILE_LEN);
    /* Exactly at the cap is fine (zero padding after the code). */
    base(); len = ELF_FILE_MAX;     EXPECT(ELF_OK);
}

static void test_identification(void)
{
    base(); img[0] = 0x7E;          EXPECT(ELF_ERR_MAGIC);
    base(); img[3] = 'f';           EXPECT(ELF_ERR_MAGIC);
    base(); img[4] = 1;             EXPECT(ELF_ERR_CLASS);      /* 32-bit */
    base(); img[4] = 0;             EXPECT(ELF_ERR_CLASS);
    base(); img[5] = 2;             EXPECT(ELF_ERR_DATA);       /* big-endian */
    base(); img[6] = 0;             EXPECT(ELF_ERR_VERSION);
    base(); w32(20, 2);             EXPECT(ELF_ERR_VERSION);
    base(); img[7] = 3;             EXPECT(ELF_ERR_OSABI);      /* GNU/Linux */
    base(); w16(16, 3);             EXPECT(ELF_ERR_TYPE);       /* ET_DYN */
    base(); w16(16, 1);             EXPECT(ELF_ERR_TYPE);       /* ET_REL */
    base(); w16(16, 4);             EXPECT(ELF_ERR_TYPE);       /* ET_CORE */
    base(); w16(18, 3);             EXPECT(ELF_ERR_MACHINE);    /* i386 */
    base(); w16(18, 183);           EXPECT(ELF_ERR_MACHINE);    /* AArch64 */
    base(); w16(52, 52);            EXPECT(ELF_ERR_HEADER_SIZE);
    base(); w16(54, 32);            EXPECT(ELF_ERR_HEADER_SIZE);
    base(); w16(54, 64);            EXPECT(ELF_ERR_HEADER_SIZE);
}

static void test_program_header_table(void)
{
    base(); w16(56, 0);             EXPECT(ELF_ERR_PHNUM);
    base(); w16(56, 17);            EXPECT(ELF_ERR_PHNUM);
    base(); w16(56, 0xFFFF);        EXPECT(ELF_ERR_PHNUM);
    base(); w64(32, FILE_LEN);      EXPECT(ELF_ERR_PHDRS);      /* starts at the end */
    base(); w64(32, FILE_LEN - 3 * 56 + 1); EXPECT(ELF_ERR_PHDRS);   /* one byte past */
    base(); w64(32, 0xFFFFFFFFFFFFFFF0ull); EXPECT(ELF_ERR_PHDRS);   /* overflow */
    /* 16 headers are allowed (13 of them PT_NULL). */
    base(); w16(56, 16);            EXPECT(ELF_OK);
}

static void test_segment_types(void)
{
    static const uint32_t refused[] = { 2, 3, 5, 6, 7, 0x6474E550, 0x6474E552, 0x12345, 0x60000000 };
    for (unsigned i = 0; i < sizeof(refused) / sizeof(refused[0]); i++) {
        base(); w32(PH(2), refused[i]);
        EXPECT(ELF_ERR_SEGMENT_TYPE);           /* INTERP, DYNAMIC, TLS, PHDR, ... */
    }
    base(); w32(PH(2) + 4, 7);      EXPECT(ELF_ERR_STACK_EXEC);
    base(); w32(PH(2) + 4, 1);      EXPECT(ELF_ERR_STACK_EXEC);
    base(); w32(PH(2), 4);          EXPECT(ELF_OK);             /* PT_NOTE ignored */
    base(); w32(PH(2), 0x6474E553); EXPECT(ELF_OK);             /* PT_GNU_PROPERTY ignored */
    base(); w32(PH(2), 0);          EXPECT(ELF_OK);             /* PT_NULL ignored */
    base(); w16(56, 2);             EXPECT(ELF_OK);             /* no PT_GNU_STACK at all */
}

static void test_load_count(void)
{
    /* Code and data, plus two more PT_LOAD (replacing PT_GNU_STACK): 4, fine. */
    base();
    w16(56, 4);
    phdr(2, 1, 4, 0, 0x600000, 0, 0x1000, 0x1000);
    phdr(3, 1, 4, 0, 0x700000, 0, 0x1000, 0x1000);
    EXPECT(ELF_OK);
    struct elf_plan p;
    CHECK_EQ(elf_check(img, len, &p), ELF_OK);
    CHECK_EQ(p.count, 4);
    CHECK_EQ(p.pages, 5);
    /* One more: 5 PT_LOAD, refused. */
    base();
    w16(56, 5);
    phdr(2, 1, 4, 0, 0x600000, 0, 0x1000, 0x1000);
    phdr(3, 1, 4, 0, 0x700000, 0, 0x1000, 0x1000);
    phdr(4, 1, 4, 0, 0x800000, 0, 0x1000, 0x1000);
    EXPECT(ELF_ERR_TOO_MANY_LOADS);
}

static void test_permissions(void)
{
    base(); w32(PH(0) + 4, 1);      EXPECT(ELF_ERR_PERMISSIONS);   /* X only: not readable */
    base(); w32(PH(1) + 4, 0);      EXPECT(ELF_ERR_PERMISSIONS);
    base(); w32(PH(1) + 4, 6 | 8);  EXPECT(ELF_ERR_PERMISSIONS);   /* an unknown flag bit */
    base(); w32(PH(0) + 4, 7);      EXPECT(ELF_ERR_WX);            /* RWX code */
    base(); w32(PH(1) + 4, 7);      EXPECT(ELF_ERR_WX);            /* RWX data */
    base(); w32(PH(0) + 4, 4);      EXPECT(ELF_ERR_NO_CODE);       /* no executable segment */
}

static void test_sizes_and_file_range(void)
{
    base(); w64(PH(1) + 40, 0);                 EXPECT(ELF_ERR_SIZES);    /* empty segment */
    base(); w64(PH(0) + 40, TEXT_SIZE - 1);     EXPECT(ELF_ERR_SIZES);    /* filesz > memsz */
    base(); w64(PH(0) + 32, TEXT_SIZE + 1); w64(PH(0) + 40, TEXT_SIZE + 1);
                                                EXPECT(ELF_ERR_FILE_RANGE);
    base(); w64(PH(0) + 8, 0xFFFFFFFFFFFFF000ull); EXPECT(ELF_ERR_FILE_RANGE);   /* overflow */
    base(); w64(PH(1) + 32, 0x2000); w64(PH(1) + 40, 0x2000);
                                                EXPECT(ELF_ERR_FILE_RANGE);
}

static void test_alignment(void)
{
    base(); w64(PH(0) + 48, 0x200000);          EXPECT(ELF_ERR_ALIGN);
    base(); w64(PH(0) + 48, 0);                 EXPECT(ELF_ERR_ALIGN);
    base(); w64(PH(0) + 16, 0x400010); w64(24, 0x400010);
                                                EXPECT(ELF_ERR_ALIGN);    /* not congruent */
    /* Congruent but not page-aligned: fine (offset 0x1010 at 0x400010). */
    base(); w64(PH(0) + 8, 0x1010); w64(PH(0) + 16, 0x400010); w64(PH(0) + 32, 0x100);
            w64(PH(0) + 40, 0x100); w64(24, 0x400010);
                                                EXPECT(ELF_OK);
}

static void test_ranges(void)
{
    base(); w64(PH(0) + 16, 0x3FF000); w64(24, 0x3FF000);           EXPECT(ELF_ERR_RANGE);
    base(); w64(PH(1) + 16, 0x40000000);                             EXPECT(ELF_ERR_RANGE);
    base(); w64(PH(1) + 16, 0x3FFFF000); w64(PH(1) + 40, 0x2000);    EXPECT(ELF_ERR_RANGE);
    base(); w64(PH(1) + 16, 0xFFFFFFFF80100000ull);                  EXPECT(ELF_ERR_RANGE);
    base(); w64(PH(1) + 16, 0x00007FFFFFFFE000ull);                  EXPECT(ELF_ERR_RANGE);
    base(); w64(PH(1) + 40, 0xFFFFFFFFFFFFFFF0ull);                  EXPECT(ELF_ERR_RANGE);
    base(); w64(PH(0) + 16, 0); w64(24, 0);                           EXPECT(ELF_ERR_RANGE);
    /* The last page below ELF_VA_END is fine. */
    base(); w64(PH(1) + 16, 0x3FFFF000); w64(PH(1) + 40, 0x1000);    EXPECT(ELF_OK);
}

static void test_order(void)
{
    base(); w64(PH(1) + 16, 0x400000);                               EXPECT(ELF_ERR_ORDER);
    /* Data first, then code below it. */
    base();
    phdr(0, 1, 6, 0, 0x500000, 0, 0x1388, 0x1000);
    phdr(1, 1, 5, 0x1000, 0x400000, TEXT_SIZE, TEXT_SIZE, 0x1000);
    EXPECT(ELF_ERR_ORDER);
    /* Data in the code's page (congruent offset): a shared page. */
    base(); w64(PH(1) + 8, 0x800); w64(PH(1) + 16, 0x400800);       EXPECT(ELF_ERR_ORDER);
    /* Data in the very next page: fine. */
    base(); w64(PH(1) + 16, 0x401000);                               EXPECT(ELF_OK);
}

static void test_memory_cap(void)
{
    /* 1 code page + 255 data pages = 1 MiB: fine; one more page: refused. */
    base(); w64(PH(1) + 40, 255 * 0x1000);                           EXPECT(ELF_OK);
    base(); w64(PH(1) + 40, 255 * 0x1000 + 1);                       EXPECT(ELF_ERR_MEM_TOO_BIG);
    base(); w64(PH(1) + 40, 0x100000);                               EXPECT(ELF_ERR_MEM_TOO_BIG);
}

static void test_entry(void)
{
    base(); w64(24, 0x500000);                  EXPECT(ELF_ERR_ENTRY);    /* in data */
    base(); w64(24, 0x400000 + TEXT_SIZE);      EXPECT(ELF_ERR_ENTRY);    /* one past the code */
    base(); w64(24, 0x3FFFFF);                  EXPECT(ELF_ERR_ENTRY);
    base(); w64(24, 0);                         EXPECT(ELF_ERR_ENTRY);
    base(); w64(24, 0x400000 + TEXT_SIZE - 1);  EXPECT(ELF_OK);           /* the last code byte */
    /* Past filesz but inside memsz (zero bytes): refused. */
    base(); w64(PH(0) + 40, 0x800); w64(24, 0x400400);                    EXPECT(ELF_ERR_ENTRY);
}

/* Random byte changes in the headers: never a crash, and every accepted
 * file obeys every rule in its plan. */
static uint32_t rng = 12345;
static uint32_t next_rand(void)
{
    rng = rng * 1103515245u + 12345u;
    return rng >> 8;
}

static void test_mutations(void)
{
    unsigned accepted = 0, bad_plans = 0;
    for (unsigned n = 0; n < 200000; n++) {
        base();
        unsigned changes = 1 + next_rand() % 4;
        for (unsigned c = 0; c < changes; c++)
            img[next_rand() % (PH(3))] = (uint8_t)next_rand();
        struct elf_plan p;
        if (elf_check(img, len, &p) != ELF_OK)
            continue;
        accepted++;
        bool ok = p.count >= 1 && p.count <= ELF_LOAD_MAX && p.pages * ELF_PAGE <= ELF_MEM_MAX;
        bool entry_in_code = false;
        uint64_t prev_end = 0;
        for (uint32_t i = 0; i < p.count && i < ELF_LOAD_MAX; i++) {
            const struct elf_segment *s = &p.seg[i];
            ok = ok && !(s->write && s->exec) && s->filesz <= s->memsz && s->memsz > 0;
            ok = ok && s->offset + s->filesz <= len && s->vaddr >= ELF_VA_MIN;
            ok = ok && s->vaddr + s->memsz <= ELF_VA_END && (s->vaddr & ~0xFFFull) >= prev_end;
            prev_end = (s->vaddr + s->memsz + 0xFFF) & ~0xFFFull;
            if (s->exec && p.entry >= s->vaddr && p.entry < s->vaddr + s->filesz)
                entry_in_code = true;
        }
        if (!ok || !entry_in_code)
            bad_plans++;
    }
    CHECK(accepted > 0);            /* some changes are harmless (paddr, padding) */
    CHECK_EQ(bad_plans, 0);
}

static void test_result_strings(void)
{
    for (int r = ELF_OK; r <= ELF_ERR_ENTRY; r++) {
        const char *s = elf_result_str((enum elf_result)r);
        CHECK(s && strlen(s) > 0 && strlen(s) <= 60);
        CHECK(strcmp(s, "unknown result") != 0);
    }
    CHECK(strcmp(elf_result_str(ELF_ERR_CLASS), "not a 64-bit ELF file") == 0);
    CHECK(strcmp(elf_result_str((enum elf_result)999), "unknown result") == 0);
}

int main(void)
{
    RUN(test_good_image_and_plan);
    RUN(test_file_size);
    RUN(test_identification);
    RUN(test_program_header_table);
    RUN(test_segment_types);
    RUN(test_load_count);
    RUN(test_permissions);
    RUN(test_sizes_and_file_range);
    RUN(test_alignment);
    RUN(test_ranges);
    RUN(test_order);
    RUN(test_memory_cap);
    RUN(test_entry);
    RUN(test_mutations);
    RUN(test_result_strings);
    return unit_report("elf");
}
