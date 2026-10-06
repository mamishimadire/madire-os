/* x86-64 4-level virtual memory core (see vmm_core.h). */
#include "kernel/memory/pte.h"
#include "kernel/memory/vmm_core.h"

#define FLAG_BITS 0x8000000000000FFFull     /* NX + bits 0-11 */

void vmm_init(struct vmm *vm, const struct vmm_ops *ops, uint64_t root)
{
    vm->ops = ops;
    vm->root = root;
    vm->mapped_pages = 0;
    vm->page_tables = 0;
    vm->upper_frozen = false;
}

void vmm_freeze_upper(struct vmm *vm)
{
    vm->upper_frozen = true;
}

static enum vmm_result check_va(uint64_t va)
{
    if (!vm_canonical(va))
        return VMM_ERR_NONCANONICAL;
    if (va < VM_NULL_GUARD_END || vm_index(va, 4) == VM_RECURSIVE_INDEX)
        return VMM_ERR_RESERVED_VA;
    return VMM_OK;
}

/*
 * Walks from the PML4 down towards `stop` (1 = to the PT). Returns the table
 * at the deepest level reached (*level) and fills path[] with the table
 * pointers from level 4 down. Stops early at a missing entry or a large
 * page (*large set).
 */
static uint64_t *walk(const struct vmm *vm, uint64_t va, int stop, int *level, bool *large)
{
    const struct vmm_ops *o = vm->ops;
    uint64_t *t = o->table(o->ctx, 4, va, vm->root, false);
    int l = 4;
    *large = false;
    while (l > stop) {
        uint64_t e = t[vm_index(va, l)];
        if (!pte_present(e))
            break;
        if (e & PTE_LARGE) {
            *large = true;
            break;
        }
        t = o->table(o->ctx, l - 1, va, pte_frame(e), false);
        l--;
    }
    *level = l;
    return t;
}

enum vmm_result vmm_ensure_table(struct vmm *vm, uint64_t va)
{
    enum vmm_result r = check_va(va);
    if (r != VMM_OK)
        return r;
    const struct vmm_ops *o = vm->ops;
    int level;
    bool large;
    uint64_t *t = walk(vm, va, 1, &level, &large);
    if (large)
        return VMM_ERR_EXISTS;
    if (level == 1)
        return VMM_OK;
    if (level == 4 && vm->upper_frozen && vm_index(va, 4) >= VM_ENTRIES / 2)
        return VMM_ERR_FROZEN;              /* nothing taken, nothing changed */

    /* Levels level-1 .. 1 are missing: take every frame first. */
    uint64_t frames[3];
    int need = level - 1;
    for (int i = 0; i < need; i++) {
        frames[i] = o->alloc_frame(o->ctx);
        if (frames[i] == 0 || !vm_aligned(frames[i]) || (frames[i] & ~PTE_ADDR_MASK)) {
            if (frames[i])
                o->free_frame(o->ctx, frames[i]);
            while (i--)
                o->free_frame(o->ctx, frames[i]);
            return VMM_ERR_NO_FRAME;
        }
    }
    for (int i = 0; i < need; i++, level--) {
        t[vm_index(va, level)] = pte_make(frames[i], PTE_PRESENT | PTE_WRITE);
        t = o->table(o->ctx, level - 1, va, frames[i], true);
        for (unsigned k = 0; k < VM_ENTRIES; k++)
            t[k] = 0;
        vm->page_tables++;
    }
    return VMM_OK;
}

enum vmm_result vmm_map(struct vmm *vm, uint64_t va, uint64_t phys, uint64_t flags)
{
    enum vmm_result r = check_va(va);
    if (r != VMM_OK)
        return r;
    if (!vm_aligned(va) || !vm_aligned(phys) || (phys & ~PTE_ADDR_MASK))
        return VMM_ERR_UNALIGNED;
    if (flags & ~(PTE_WRITE | PTE_NX))
        return VMM_ERR_FLAGS;

    /* Check first, so a refused map never creates a table. */
    int level;
    bool large;
    uint64_t *t = walk(vm, va, 1, &level, &large);
    if (large || (level == 1 && pte_present(t[vm_index(va, 1)])))
        return VMM_ERR_EXISTS;
    if (level != 1) {
        r = vmm_ensure_table(vm, va);
        if (r != VMM_OK)
            return r;
        t = walk(vm, va, 1, &level, &large);
    }
    /* A not-present entry is never cached by the TLB: no flush needed. */
    t[vm_index(va, 1)] = pte_make(phys, PTE_PRESENT | flags);
    vm->mapped_pages++;
    return VMM_OK;
}

enum vmm_result vmm_unmap(struct vmm *vm, uint64_t va, uint64_t *phys_out)
{
    enum vmm_result r = check_va(va);
    if (r != VMM_OK)
        return r;
    if (!vm_aligned(va))
        return VMM_ERR_UNALIGNED;
    int level;
    bool large;
    uint64_t *t = walk(vm, va, 1, &level, &large);
    if (large || level != 1 || !pte_present(t[vm_index(va, 1)]))
        return VMM_ERR_NOT_MAPPED;          /* large pages are not unmapped here */

    uint64_t entry = t[vm_index(va, 1)];
    t[vm_index(va, 1)] = 0;
    vm->ops->flush(vm->ops->ctx, va);
    if (vm->mapped_pages)
        vm->mapped_pages--;
    if (phys_out)
        *phys_out = pte_frame(entry);
    return VMM_OK;
}

bool vmm_translate(const struct vmm *vm, uint64_t va, uint64_t *phys_out, uint64_t *flags_out)
{
    if (check_va(va) != VMM_OK)
        return false;
    int level;
    bool large;
    uint64_t *t = walk(vm, va, 1, &level, &large);
    uint64_t e = t[vm_index(va, level)];
    if (!pte_present(e) || (!large && level != 1))
        return false;
    /* A leaf at level L covers 4 KiB << 9 * (L - 1). */
    uint64_t span = VM_PAGE_SIZE << (9 * (level - 1));
    if (phys_out)
        *phys_out = (pte_frame(e) & ~(span - 1)) | (va & (span - 1));
    if (flags_out)
        *flags_out = e & FLAG_BITS;
    return true;
}

const char *vmm_result_str(enum vmm_result result)
{
    switch (result) {
    case VMM_OK:               return "ok";
    case VMM_ERR_UNALIGNED:    return "address not page-aligned (or beyond 52 bits)";
    case VMM_ERR_EXISTS:       return "address already mapped";
    case VMM_ERR_NOT_MAPPED:   return "address not mapped";
    case VMM_ERR_NO_FRAME:     return "no free frame for a page table";
    case VMM_ERR_RESERVED_VA:  return "reserved virtual address";
    case VMM_ERR_FLAGS:        return "unsupported mapping flags";
    case VMM_ERR_NONCANONICAL: return "non-canonical address";
    case VMM_ERR_FROZEN:       return "new kernel-half PML4 entry after the freeze";
    }
    return "unknown vmm error";
}
