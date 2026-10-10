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
