#ifndef MADIRE_PROC_ELF_CORE_H
#define MADIRE_PROC_ELF_CORE_H

/*
 * ELF64 program files (0.11), pure: tests/unit/test_elf.c, and the host
 * tool tests/elfcheck.c for the fixture files (tests/elf-fixtures.py).
 *
 * A file is untrusted: every field is read through bounds-checked
 * little-endian readers (no structure casts, no unaligned access), every
 * sum is checked for overflow, and the first rule a file breaks is the
 * result. A file that passes yields a load plan: the segments to copy into
 * fresh pages (never the module's own pages) and the entry point.
 *
 * Accepted: a static x86-64 executable (ET_EXEC), 64-bit little-endian,
 * System V ABI, at most 1 MiB, with
 *   - 1 to 16 program headers, all inside the file;
 *   - program-header types from an allowlist: PT_LOAD, PT_NOTE,
 *     PT_GNU_PROPERTY, PT_NULL (ignored) and PT_GNU_STACK (which must not
 *     be executable). Anything else is refused, among them PT_INTERP,
 *     PT_DYNAMIC, PT_TLS and PT_PHDR (dynamic linking and thread-local
 *     storage are not supported);
 *   - 1 to 4 PT_LOAD segments, each: readable; never writable and
 *     executable at once (W^X); p_filesz <= p_memsz, p_memsz > 0; its
 *     file bytes inside the file; p_align 0x1000 with p_vaddr and
 *     p_offset equal modulo 0x1000; inside [ELF_VA_MIN, ELF_VA_END);
 *     after the previous segment's last page (ascending, no overlap, no
 *     page shared by two segments);
 *   - at most 1 MiB of memory in all, and at least one executable segment;
 *   - the entry point inside an executable segment's file bytes.
 * The plan's segment ranges are the segments as written (bytes past
 * p_filesz up to p_memsz are zero, the rest of their pages too).
 */

#include <stdbool.h>
#include <stdint.h>

#define ELF_FILE_MAX   (1024u * 1024)   /* bytes in the file */
#define ELF_MEM_MAX    (1024u * 1024)   /* bytes of memory in all segments (whole pages) */
#define ELF_PHNUM_MAX  16u
#define ELF_LOAD_MAX   4u
#define ELF_PAGE       0x1000ull
#define ELF_VA_MIN     0x0000000000400000ull    /* = VM_USER_TEXT: nothing below 4 MiB */
#define ELF_VA_END     0x0000000040000000ull    /* 1 GiB: far below the stack */

struct elf_segment {
    uint64_t vaddr, offset, filesz, memsz;
    bool write, exec;
};

struct elf_plan {
    uint64_t entry;
    uint32_t count;                 /* PT_LOAD segments */
    uint32_t pages;                 /* pages they need in all */
    struct elf_segment seg[ELF_LOAD_MAX];
};

enum elf_result {
    ELF_OK,
    ELF_ERR_EMPTY,                  /* no bytes */
    ELF_ERR_TOO_BIG,                /* more than ELF_FILE_MAX bytes */
    ELF_ERR_SHORT,                  /* shorter than the 64-byte header */
    ELF_ERR_MAGIC,                  /* not an ELF file */
    ELF_ERR_CLASS,                  /* not 64-bit */
    ELF_ERR_DATA,                   /* not little-endian */
    ELF_ERR_VERSION,                /* e_ident or e_version not 1 */
    ELF_ERR_OSABI,                  /* not the System V ABI */
    ELF_ERR_TYPE,                   /* not ET_EXEC */
    ELF_ERR_MACHINE,                /* not x86-64 */
    ELF_ERR_HEADER_SIZE,            /* e_ehsize not 64 or e_phentsize not 56 */
    ELF_ERR_PHNUM,                  /* no program header, or more than 16 */
    ELF_ERR_PHDRS,                  /* program headers outside the file */
    ELF_ERR_SEGMENT_TYPE,           /* a type outside the allowlist */
    ELF_ERR_STACK_EXEC,             /* PT_GNU_STACK asks for an executable stack */
    ELF_ERR_TOO_MANY_LOADS,         /* more than ELF_LOAD_MAX PT_LOAD */
    ELF_ERR_SIZES,                  /* p_filesz > p_memsz, or p_memsz 0 */
    ELF_ERR_FILE_RANGE,             /* segment bytes outside the file */
    ELF_ERR_ALIGN,                  /* p_align not 0x1000, or vaddr/offset not congruent */
    ELF_ERR_RANGE,                  /* outside [ELF_VA_MIN, ELF_VA_END) */
    ELF_ERR_PERMISSIONS,            /* not readable, or unknown flag bits */
    ELF_ERR_WX,                     /* writable and executable */
    ELF_ERR_ORDER,                  /* not ascending, overlapping, or sharing a page */
    ELF_ERR_MEM_TOO_BIG,            /* more than ELF_MEM_MAX of memory */
    ELF_ERR_NO_CODE,                /* no executable segment */
    ELF_ERR_ENTRY,                  /* entry point outside the executable bytes */
};

/* Checks the file and fills *plan (when ELF_OK; plan may be NULL). */
enum elf_result elf_check(const uint8_t *file, uint64_t len, struct elf_plan *plan);

/* A short reason, for log lines ("not a 64-bit ELF file", ...). */
const char *elf_result_str(enum elf_result r);

#endif
