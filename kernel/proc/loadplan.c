/* Load plans (see loadplan.h). */
#include "kernel/memory/pte.h"
#include "kernel/memory/vm_layout.h"
#include "kernel/proc/loadplan.h"

bool loadplan_from_userimg(const struct userimg *img, struct load_plan *out)
{
    if (userimg_check(img) != USERIMG_OK)
        return false;
    out->entry = img->entry;
    out->count = 1;
    out->seg[0] = (struct load_segment){ VM_USER_TEXT, img->text_size, img->text_size,
                                         img->text, false, true };
    if (img->data_mem) {
        out->seg[1] = (struct load_segment){ VM_USER_DATA, img->data_size, img->data_mem,
                                             img->data, true, false };
        out->count = 2;
    }
    return true;
}

void loadplan_from_elf(const uint8_t *file, const struct elf_plan *plan, struct load_plan *out)
{
    out->entry = plan->entry;
    out->count = plan->count;
    for (uint32_t i = 0; i < plan->count && i < LOADPLAN_MAX; i++) {
        const struct elf_segment *e = &plan->seg[i];
        out->seg[i] = (struct load_segment){ e->vaddr, e->filesz, e->memsz, file + e->offset,
                                             e->write, e->exec };
    }
}

uint64_t loadplan_first_page(const struct load_segment *s)
{
    return s->vaddr & ~(VM_PAGE_SIZE - 1);
}

uint32_t loadplan_pages(const struct load_segment *s)
{
    uint64_t end = (s->vaddr + s->memsz + VM_PAGE_SIZE - 1) & ~(VM_PAGE_SIZE - 1);
    return (uint32_t)((end - loadplan_first_page(s)) / VM_PAGE_SIZE);
}

void loadplan_page(const struct load_segment *s, uint64_t page, uint64_t *src_off, uint32_t *at,
                   uint32_t *len)
{
    /* The file bytes are [vaddr, vaddr + filesz); this page is
     * [page, page + VM_PAGE_SIZE). */
    uint64_t lo = s->vaddr > page ? s->vaddr : page;
    uint64_t file_end = s->vaddr + s->filesz, page_end = page + VM_PAGE_SIZE;
    uint64_t hi = file_end < page_end ? file_end : page_end;
    if (hi <= lo) {                 /* only zeros in this page */
        *src_off = 0;
        *at = 0;
        *len = 0;
        return;
    }
    *at = (uint32_t)(lo - page);
    *src_off = lo - s->vaddr;
    *len = (uint32_t)(hi - lo);
}

/* ---- 1.0 stage 7: the System V start block ---- */

#define END_MARKER 8u
#define WORD       8u

static const char platform_str[] = "x86_64";

static uint64_t str_size(const char *s)
{
    uint64_t n = 0;
    while (s[n])
        n++;
    return n + 1;
}

/* The offsets for strings of `bytes` bytes in all and argc + envc
 * pointers; false if the block does not fit the page. */
static bool layout(uint64_t bytes, uint64_t argc, uint64_t envc, uint32_t *strings,
                   uint32_t *platform, uint32_t *rsp)
{
    uint64_t top = STARTBLOCK_SIZE - END_MARKER;
    if (bytes > top)
        return false;
    uint64_t s = top - bytes;
    uint64_t p = s & ~15ull;
    if (p < sizeof(platform_str))
        return false;
    p -= sizeof(platform_str);
    uint64_t words = 1 + argc + 1 + envc + 1 + 2 * (uint64_t)STARTBLOCK_AUXC;
    if (argc > STARTBLOCK_SIZE || envc > STARTBLOCK_SIZE || p < words * WORD)
        return false;
    *strings = (uint32_t)s;
    *platform = (uint32_t)p;
    *rsp = (uint32_t)((p - words * WORD) & ~15ull);
    return true;
}

static uint32_t put_str(uint8_t *page, uint32_t at, const char *s)
{
    while (*s)
        page[at++] = (uint8_t)*s++;
    return at;
}

int startblock_strings(struct start_block *b, const char *execfn, uint32_t argc,
                       const char *const *argv, uint32_t envc, const char *const *env_names,
                       const char *const *env_values)
{
    uint64_t bytes = str_size(execfn);
    for (uint32_t i = 0; i < argc && bytes <= STARTBLOCK_SIZE; i++)
        bytes += str_size(argv[i]);
    for (uint32_t i = 0; i < envc && bytes <= STARTBLOCK_SIZE; i++)
        bytes += str_size(env_names[i]) + str_size(env_values[i]);   /* '=' takes one NUL's place */
    if (!layout(bytes, argc, envc, &b->strings, &b->platform, &b->rsp))
        return -STARTBLOCK_E2BIG;
    for (uint32_t i = 0; i < STARTBLOCK_SIZE; i++)
        b->page[i] = 0;
    b->argc = argc;
    b->envc = envc;
    uint32_t at = b->strings;
    for (uint32_t i = 0; i < argc; i++)
        at = put_str(b->page, at, argv[i]) + 1;
    for (uint32_t i = 0; i < envc; i++) {
        at = put_str(b->page, at, env_names[i]);
        b->page[at++] = '=';
        at = put_str(b->page, at, env_values[i]) + 1;
    }
    b->execfn = at;
    put_str(b->page, at, execfn);
    put_str(b->page, b->platform, platform_str);
    return 0;
}

uint64_t startblock_addr(uint32_t off)
{
    return STARTBLOCK_BASE + off;
}

uint64_t startblock_rsp(const struct start_block *b)
{
    return startblock_addr(b->rsp);
}

uint32_t startblock_bytes(const struct start_block *b)
{
    return STARTBLOCK_SIZE - b->rsp;
}

static uint32_t put_word(uint8_t *page, uint32_t at, uint64_t v)
{
    for (uint32_t i = 0; i < WORD; i++)
        page[at + i] = (uint8_t)(v >> (8 * i));
    return at + WORD;
}

static uint32_t put_aux(uint8_t *page, uint32_t at, uint64_t type, uint64_t value)
{
    return put_word(page, put_word(page, at, type), value);
}

void startblock_finish(struct start_block *b, const struct start_aux *aux)
{
    uint32_t at = put_word(b->page, b->rsp, b->argc);
    uint32_t s = b->strings;
    for (uint32_t i = 0; i < b->argc + b->envc; i++) {
        if (i == b->argc)
            at = put_word(b->page, at, 0);              /* argv's NULL */
        at = put_word(b->page, at, startblock_addr(s));
        while (b->page[s])
            s++;
        s++;
    }
    if (b->envc == 0)
        at = put_word(b->page, at, 0);                  /* argv's NULL */
    at = put_word(b->page, at, 0);                      /* envp's NULL */
    at = put_aux(b->page, at, AT_HWCAP, aux->hwcap);
    at = put_aux(b->page, at, AT_PAGESZ, VM_PAGE_SIZE);
    at = put_aux(b->page, at, AT_CLKTCK, 100);
    at = put_aux(b->page, at, AT_PHDR, aux->phdr);
    at = put_aux(b->page, at, AT_PHENT, 56);
    at = put_aux(b->page, at, AT_PHNUM, aux->phnum);
    at = put_aux(b->page, at, AT_BASE, 0);
    at = put_aux(b->page, at, AT_FLAGS, 0);
    at = put_aux(b->page, at, AT_ENTRY, aux->entry);
    at = put_aux(b->page, at, AT_UID, 0);
    at = put_aux(b->page, at, AT_EUID, 0);
    at = put_aux(b->page, at, AT_GID, 0);
    at = put_aux(b->page, at, AT_EGID, 0);
    at = put_aux(b->page, at, AT_SECURE, 0);
    at = put_aux(b->page, at, AT_HWCAP2, 0);
    at = put_aux(b->page, at, AT_EXECFN, startblock_addr(b->execfn));
    at = put_aux(b->page, at, AT_PLATFORM, startblock_addr(b->platform));
    put_aux(b->page, at, AT_NULL, 0);
}

static uint64_t get_le(const uint8_t *p, uint32_t n)
{
    uint64_t v = 0;
    for (uint32_t i = 0; i < n; i++)
        v |= (uint64_t)p[i] << (8 * i);
    return v;
}

void startblock_elf_phdr(const uint8_t *file, const struct elf_plan *plan, uint64_t *phdr,
                         uint64_t *phnum)
{
    uint64_t phoff = get_le(file + 32, 8);
    *phnum = get_le(file + 56, 2);
    *phdr = 0;
    for (uint32_t i = 0; i < plan->count && i < ELF_LOAD_MAX; i++) {
        const struct elf_segment *s = &plan->seg[i];
        if (s->offset <= phoff && phoff - s->offset < s->filesz) {
            *phdr = s->vaddr + (phoff - s->offset);
            return;
        }
    }
}

uint64_t startblock_default_rsp(const char *name)
{
    uint32_t strings, platform, rsp;
    if (!layout(2 * str_size(name), 1, 0, &strings, &platform, &rsp))
        return 0;
    return startblock_addr(rsp);
}
