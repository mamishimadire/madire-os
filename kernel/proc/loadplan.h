#ifndef MADIRE_PROC_LOADPLAN_H
#define MADIRE_PROC_LOADPLAN_H

/*
 * Load plans (0.11), pure: tests/unit/test_loadplan.c. One loading path
 * for every program: a built-in image (userimg.h) and an ELF file
 * (elf_core.h) both become a list of segments, and process_load maps the
 * pages of each segment, filling every fresh frame from the segment's
 * bytes (never mapping the source's own pages).
 *
 * A segment covers [vaddr, vaddr + memsz); its first filesz bytes come
 * from src, the rest are zero, and so is the rest of its first and last
 * page. loadplan_page tells, for one page of a segment, which bytes of src
 * go where in that page.
 */

#include <stdbool.h>
#include <stdint.h>
#include "kernel/memory/vm_layout.h"
#include "kernel/proc/elf_core.h"
#include "kernel/proc/userimg.h"

#define LOADPLAN_MAX ELF_LOAD_MAX

struct load_segment {
    uint64_t vaddr, filesz, memsz;
    const uint8_t *src;             /* filesz bytes */
    bool write, exec;
};

struct load_plan {
    uint64_t entry;
    uint32_t count;
    struct load_segment seg[LOADPLAN_MAX];
};

/* A built-in image: code at VM_USER_TEXT (read + execute), then data + bss
 * at VM_USER_DATA (read + write) if it has any. false: the image fails
 * userimg_check. */
bool loadplan_from_userimg(const struct userimg *img, struct load_plan *out);

/* An ELF file already checked by elf_check (plan): the segments' bytes are
 * taken from file. */
void loadplan_from_elf(const uint8_t *file, const struct elf_plan *plan, struct load_plan *out);

/* The page-aligned range a segment occupies, and its page count. */
uint64_t loadplan_first_page(const struct load_segment *s);
uint32_t loadplan_pages(const struct load_segment *s);

/* For the page at `page` (page-aligned, inside the segment's range): the
 * bytes src[*src_off .. *src_off + *len) go at offset *at in the page; *len
 * may be 0 (a page of zeros). */
void loadplan_page(const struct load_segment *s, uint64_t page, uint64_t *src_off, uint32_t *at,
                   uint32_t *len);

/*
 * 1.0 stage 7: the System V start block (x86-64 psABI), in Linux's layout,
 * built here as the image of the user stack's top page (4096 bytes ending
 * at VM_USER_STACK_TOP), which process_load_plan fills its fresh frame from.
 * From the top down:
 *   8 zero bytes (the end marker);
 *   the strings, in rising addresses: argv[0..argc-1], then envp's
 *   "NAME=value" strings, then the program's name (AT_EXECFN);
 *   down to a multiple of 16, then "x86_64" (AT_PLATFORM);
 *   then, from RSP (a multiple of 16, rounded down) upwards: argc, argv[]
 *   and NULL, envp[] and NULL, the auxiliary vector (STARTBLOCK_AUXC pairs,
 *   AT_NULL last), then padding up to "x86_64".
 * No AT_RANDOM (no entropy source yet; musl falls back without it), no
 * AT_SYSINFO_EHDR (no vDSO), no AT_MINSIGSTKSZ (no signals), no AT_RSEQ_*.
 * A block that does not fit the page is refused with E2BIG (Linux's errno
 * for execve's argument list), before any process exists.
 *
 * Built in two steps: startblock_strings when the start is requested (the
 * strings and the size check), startblock_finish once the program is
 * loaded (the pointers and the auxiliary vector need its entry point and
 * program headers).
 */
#define STARTBLOCK_SIZE  4096u
#define STARTBLOCK_E2BIG 7
#define STARTBLOCK_AUXC  18u            /* auxiliary vector entries, AT_NULL included */
#define STARTBLOCK_BASE  (VM_USER_STACK_TOP - STARTBLOCK_SIZE)

#define AT_NULL     0
#define AT_PHDR     3
#define AT_PHENT    4
#define AT_PHNUM    5
#define AT_PAGESZ   6
#define AT_BASE     7
#define AT_FLAGS    8
#define AT_ENTRY    9
#define AT_UID      11
#define AT_EUID     12
#define AT_GID      13
#define AT_EGID     14
#define AT_PLATFORM 15
#define AT_HWCAP    16
#define AT_CLKTCK   17
#define AT_SECURE   23
#define AT_HWCAP2   26
#define AT_EXECFN   31

struct start_block {
    uint8_t page[STARTBLOCK_SIZE];  /* the top stack page, as the program sees it */
    uint32_t argc, envc;
    uint32_t strings;               /* offset in page of argv[0]'s first byte */
    uint32_t execfn, platform;      /* offsets of the name and of "x86_64" */
    uint32_t rsp;                   /* offset of argc: RSP at entry */
};

/* What startblock_finish needs from the loaded program. */
struct start_aux {
    uint64_t entry;                 /* AT_ENTRY */
    uint64_t phdr, phnum;           /* AT_PHDR, AT_PHNUM (0, 0: none mapped) */
    uint64_t hwcap;                 /* AT_HWCAP: CPUID leaf 1 EDX */
};

/* The strings and the layout: envp's strings are env_names[i] '='
 * env_values[i]. 0, or -STARTBLOCK_E2BIG (b is then not usable). */
int startblock_strings(struct start_block *b, const char *execfn, uint32_t argc,
                       const char *const *argv, uint32_t envc, const char *const *env_names,
                       const char *const *env_values);

/* The pointers and the auxiliary vector, below the strings. */
void startblock_finish(struct start_block *b, const struct start_aux *aux);

/* The user address of offset `off` in the block, and RSP at entry. */
uint64_t startblock_addr(uint32_t off);
uint64_t startblock_rsp(const struct start_block *b);

/* The block's size from RSP to the stack top (what the limit counts). */
uint32_t startblock_bytes(const struct start_block *b);

/* AT_PHDR and AT_PHNUM for an ELF file checked by elf_check (plan), as
 * Linux computes them: the address of the program headers if a loaded
 * segment's file bytes hold them, else 0 (they are then not mapped). */
void startblock_elf_phdr(const uint8_t *file, const struct elf_plan *plan, uint64_t *phdr,
                         uint64_t *phnum);

/* RSP at entry for the default block of a built-in image started without
 * arguments by name (argv = { name }, no environment): what process_load
 * builds (the 0.10 ring-3 tests). */
uint64_t startblock_default_rsp(const char *name);

#endif
