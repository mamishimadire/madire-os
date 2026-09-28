/* Virtual memory core (see vmm_core.h). */
#include "kernel/memory/pte.h"
#include "kernel/memory/vm_layout.h"
#include "kernel/memory/vmm_core.h"

void vmm_init(struct vmm *vm, const struct vmm_ops *ops)
{
    vm->ops = ops;
    vm->mapped_pages = 0;
    vm->page_tables = 0;
}

static bool reserved_va(uint32_t va)
{
    return va < VM_NULL_GUARD_END || pd_index(va) == VM_RECURSIVE_INDEX;
}

/* Existing table for va, or NULL. */
static uint32_t *find_table(const struct vmm *vm, uint32_t va)
{
    const struct vmm_ops *o = vm->ops;
    uint32_t pde = o->directory(o->ctx)[pd_index(va)];
    if (!pte_present(pde))
        return 0;
    return o->table(o->ctx, pd_index(va), pte_frame(pde), false);
}

enum vmm_result vmm_ensure_table(struct vmm *vm, uint32_t va)
{
    const struct vmm_ops *o = vm->ops;
    if (reserved_va(va))
        return VMM_ERR_RESERVED_VA;
    uint32_t *dir = o->directory(o->ctx);
    uint32_t pdi = pd_index(va);
    if (pte_present(dir[pdi]))
        return VMM_OK;

    uint32_t frame = o->alloc_frame(o->ctx);
    if (frame == 0 || !page_aligned(frame))
        return VMM_ERR_NO_FRAME;
    dir[pdi] = pte_make(frame, PTE_PRESENT | PTE_WRITE);
    uint32_t *table = o->table(o->ctx, pdi, frame, true);
    for (uint32_t i = 0; i < VM_ENTRIES; i++)
        table[i] = 0;
    vm->page_tables++;
    return VMM_OK;
}

enum vmm_result vmm_map(struct vmm *vm, uint32_t va, uint32_t phys, uint32_t flags)
{
    if (!page_aligned(va) || !page_aligned(phys))
        return VMM_ERR_UNALIGNED;
    if (reserved_va(va))
        return VMM_ERR_RESERVED_VA;
    if (flags & ~PTE_WRITE)
        return VMM_ERR_FLAGS;

    /* Check first so a refused map never creates a table. */
    uint32_t *table = find_table(vm, va);
    if (table && pte_present(table[pt_index(va)]))
        return VMM_ERR_EXISTS;
    if (!table) {
        enum vmm_result r = vmm_ensure_table(vm, va);
        if (r != VMM_OK)
            return r;
        table = find_table(vm, va);
    }

    /* A not-present entry is never cached by the TLB: no flush needed. */
    table[pt_index(va)] = pte_make(phys, PTE_PRESENT | flags);
    vm->mapped_pages++;
    return VMM_OK;
}

enum vmm_result vmm_unmap(struct vmm *vm, uint32_t va, uint32_t *phys_out)
{
    if (!page_aligned(va))
        return VMM_ERR_UNALIGNED;
    if (reserved_va(va))
        return VMM_ERR_RESERVED_VA;
    uint32_t *table = find_table(vm, va);
    if (!table || !pte_present(table[pt_index(va)]))
        return VMM_ERR_NOT_MAPPED;

    uint32_t entry = table[pt_index(va)];
    table[pt_index(va)] = 0;
    vm->ops->flush(vm->ops->ctx, va);
    if (vm->mapped_pages)
        vm->mapped_pages--;
    if (phys_out)
        *phys_out = pte_frame(entry);
    return VMM_OK;
}

bool vmm_translate(const struct vmm *vm, uint32_t va, uint32_t *phys_out, uint32_t *flags_out)
{
    if (reserved_va(va))
        return false;
    uint32_t *table = find_table(vm, va);
    if (!table)
        return false;
    uint32_t entry = table[pt_index(va)];
    if (!pte_present(entry))
        return false;
    if (phys_out)
        *phys_out = pte_frame(entry) | (va & 0xFFFu);
    if (flags_out)
        *flags_out = entry & 0xFFFu;
    return true;
}

const char *vmm_result_str(enum vmm_result result)
{
    switch (result) {
    case VMM_OK:              return "ok";
    case VMM_ERR_UNALIGNED:   return "address not page-aligned";
    case VMM_ERR_EXISTS:      return "address already mapped";
    case VMM_ERR_NOT_MAPPED:  return "address not mapped";
    case VMM_ERR_NO_FRAME:    return "no free frame for a page table";
    case VMM_ERR_RESERVED_VA: return "reserved virtual address";
    case VMM_ERR_FLAGS:       return "unsupported mapping flags";
    }
    return "unknown vmm error";
}
