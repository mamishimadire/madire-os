/* ELF64 program files: the checks and the load plan (see elf_core.h). */
#include "kernel/proc/elf_core.h"

/* e_ident and header fields (System V gABI, x86-64 psABI). */
#define EI_CLASS       4
#define EI_DATA        5
#define EI_VERSION     6
#define EI_OSABI       7
#define ELFCLASS64     2
#define ELFDATA2LSB    1
#define EV_CURRENT     1
#define ELFOSABI_SYSV  0
#define ET_EXEC        2
#define EM_X86_64      62
#define EHDR_SIZE      64u
#define PHDR_SIZE      56u

#define PT_NULL         0u
#define PT_LOAD         1u
#define PT_NOTE         4u
#define PT_GNU_STACK    0x6474E551u
#define PT_GNU_PROPERTY 0x6474E553u

#define PF_X 1u
#define PF_W 2u
#define PF_R 4u

/* Little-endian reads; the caller has checked that the bytes exist. */
static uint16_t rd16(const uint8_t *p)
{
    return (uint16_t)(p[0] | (uint16_t)p[1] << 8);
}

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static uint64_t rd64(const uint8_t *p)
{
    return (uint64_t)rd32(p) | (uint64_t)rd32(p + 4) << 32;
}

static uint64_t page_down(uint64_t a)
{
    return a & ~(ELF_PAGE - 1);
}

/* a is below ELF_VA_END here, so this cannot overflow. */
static uint64_t page_up(uint64_t a)
{
    return (a + ELF_PAGE - 1) & ~(ELF_PAGE - 1);
}

static enum elf_result check_header(const uint8_t *f, uint64_t len)
{
    if (len == 0)
        return ELF_ERR_EMPTY;
    if (len > ELF_FILE_MAX)
        return ELF_ERR_TOO_BIG;
    if (len < EHDR_SIZE)
        return ELF_ERR_SHORT;
    if (f[0] != 0x7F || f[1] != 'E' || f[2] != 'L' || f[3] != 'F')
        return ELF_ERR_MAGIC;
    if (f[EI_CLASS] != ELFCLASS64)
        return ELF_ERR_CLASS;
    if (f[EI_DATA] != ELFDATA2LSB)
        return ELF_ERR_DATA;
    if (f[EI_VERSION] != EV_CURRENT || rd32(f + 20) != EV_CURRENT)
        return ELF_ERR_VERSION;
    if (f[EI_OSABI] != ELFOSABI_SYSV)
        return ELF_ERR_OSABI;
    if (rd16(f + 16) != ET_EXEC)
        return ELF_ERR_TYPE;
    if (rd16(f + 18) != EM_X86_64)
        return ELF_ERR_MACHINE;
    if (rd16(f + 52) != EHDR_SIZE || rd16(f + 54) != PHDR_SIZE)
        return ELF_ERR_HEADER_SIZE;
    uint16_t phnum = rd16(f + 56);
    if (phnum == 0 || phnum > ELF_PHNUM_MAX)
        return ELF_ERR_PHNUM;
    uint64_t phoff = rd64(f + 32);
    if (phoff > len || (uint64_t)phnum * PHDR_SIZE > len - phoff)
        return ELF_ERR_PHDRS;
    return ELF_OK;
}

/* One PT_LOAD: its rules, then its place after the previous segment. */
static enum elf_result check_load(const uint8_t *ph, uint64_t len, uint64_t *prev_end,
                                  struct elf_segment *s)
{
    uint32_t flags = rd32(ph + 4);
    uint64_t offset = rd64(ph + 8), vaddr = rd64(ph + 16);
    uint64_t filesz = rd64(ph + 32), memsz = rd64(ph + 40), align = rd64(ph + 48);

    if ((flags & ~(PF_R | PF_W | PF_X)) || !(flags & PF_R))
        return ELF_ERR_PERMISSIONS;
    if ((flags & PF_W) && (flags & PF_X))
        return ELF_ERR_WX;
    if (memsz == 0 || filesz > memsz)
        return ELF_ERR_SIZES;
    if (filesz > len || offset > len - filesz)
        return ELF_ERR_FILE_RANGE;
    if (align != ELF_PAGE || (vaddr & (ELF_PAGE - 1)) != (offset & (ELF_PAGE - 1)))
        return ELF_ERR_ALIGN;
    if (vaddr < ELF_VA_MIN || vaddr >= ELF_VA_END || memsz > ELF_VA_END - vaddr)
        return ELF_ERR_RANGE;
    if (page_down(vaddr) < *prev_end)
        return ELF_ERR_ORDER;
    *prev_end = page_up(vaddr + memsz);

    s->vaddr = vaddr;
    s->offset = offset;
    s->filesz = filesz;
    s->memsz = memsz;
    s->write = (flags & PF_W) != 0;
    s->exec = (flags & PF_X) != 0;
    return ELF_OK;
}

enum elf_result elf_check(const uint8_t *file, uint64_t len, struct elf_plan *plan)
{
    enum elf_result r = check_header(file, len);
    if (r != ELF_OK)
        return r;

    struct elf_plan p = { .entry = rd64(file + 24) };
    uint64_t phoff = rd64(file + 32), prev_end = 0, pages = 0;
    uint16_t phnum = rd16(file + 56);
    for (uint16_t i = 0; i < phnum; i++) {
        const uint8_t *ph = file + phoff + (uint64_t)i * PHDR_SIZE;
        uint32_t type = rd32(ph);
        if (type == PT_NULL || type == PT_NOTE || type == PT_GNU_PROPERTY)
            continue;
        if (type == PT_GNU_STACK) {
            if (rd32(ph + 4) & PF_X)
                return ELF_ERR_STACK_EXEC;
            continue;
        }
        if (type != PT_LOAD)
            return ELF_ERR_SEGMENT_TYPE;
        if (p.count == ELF_LOAD_MAX)
            return ELF_ERR_TOO_MANY_LOADS;
        struct elf_segment *s = &p.seg[p.count];
        r = check_load(ph, len, &prev_end, s);
        if (r != ELF_OK)
            return r;
        pages += (page_up(s->vaddr + s->memsz) - page_down(s->vaddr)) / ELF_PAGE;
        if (pages * ELF_PAGE > ELF_MEM_MAX)
            return ELF_ERR_MEM_TOO_BIG;
        p.count++;
    }

    bool code = false, entry_ok = false;
    for (uint32_t i = 0; i < p.count; i++) {
        if (!p.seg[i].exec)
            continue;
        code = true;
        if (p.entry >= p.seg[i].vaddr && p.entry - p.seg[i].vaddr < p.seg[i].filesz)
            entry_ok = true;
    }
    if (!code)
        return ELF_ERR_NO_CODE;
    if (!entry_ok)
        return ELF_ERR_ENTRY;

    p.pages = (uint32_t)pages;
    if (plan)
        *plan = p;
    return ELF_OK;
}

const char *elf_result_str(enum elf_result r)
{
    switch (r) {
    case ELF_OK:                 return "OK";
    case ELF_ERR_EMPTY:          return "empty file";
    case ELF_ERR_TOO_BIG:        return "file larger than 1 MiB";
    case ELF_ERR_SHORT:          return "shorter than an ELF header";
    case ELF_ERR_MAGIC:          return "not an ELF file";
    case ELF_ERR_CLASS:          return "not a 64-bit ELF file";
    case ELF_ERR_DATA:           return "not little-endian";
    case ELF_ERR_VERSION:        return "unknown ELF version";
    case ELF_ERR_OSABI:          return "not the System V ABI";
    case ELF_ERR_TYPE:           return "not a static executable (ET_EXEC)";
    case ELF_ERR_MACHINE:        return "not an x86-64 program";
    case ELF_ERR_HEADER_SIZE:    return "wrong header or program-header size";
    case ELF_ERR_PHNUM:          return "no program header, or more than 16";
    case ELF_ERR_PHDRS:          return "program headers outside the file";
    case ELF_ERR_SEGMENT_TYPE:   return "unsupported segment type";
    case ELF_ERR_STACK_EXEC:     return "asks for an executable stack";
    case ELF_ERR_TOO_MANY_LOADS: return "more than 4 loadable segments";
    case ELF_ERR_SIZES:          return "segment empty, or file size above memory size";
    case ELF_ERR_FILE_RANGE:     return "segment outside the file";
    case ELF_ERR_ALIGN:          return "segment not page-aligned";
    case ELF_ERR_RANGE:          return "segment outside the program area";
    case ELF_ERR_PERMISSIONS:    return "segment not readable, or unknown flags";
    case ELF_ERR_WX:             return "segment both writable and executable";
    case ELF_ERR_ORDER:          return "segments out of order, overlapping or sharing a page";
    case ELF_ERR_MEM_TOO_BIG:    return "more than 1 MiB of memory";
    case ELF_ERR_NO_CODE:        return "no executable segment";
    case ELF_ERR_ENTRY:          return "entry point outside the code";
    }
    return "unknown result";
}
