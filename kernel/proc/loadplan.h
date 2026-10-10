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

#endif
