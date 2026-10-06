/* User virtual memory core (see uvm_core.h). */
#include "kernel/memory/pte.h"
#include "kernel/memory/uvm_core.h"

#define FLAG_BITS   0x8000000000000FFFull   /* NX + bits 0-11 */
#define LOWER_SLOTS (VM_ENTRIES / 2)        /* PML4 entries 0-255: the lower half */

void uvm_init(struct uvm *u, const struct vmm_ops *ops, uint64_t root, bool nx)
{
    u->ops = ops;
    u->root = root;
    u->nx = nx;
    u->pages = 0;
    u->tables = 0;
}

static bool user_page(uint64_t va)
{
    return vm_aligned(va) && va >= VM_USER_MIN && va < VM_USER_TOP;
}

/* The table at level `level` (3 = PDPT, 2 = PD, 1 = PT) covering va, or 0
 * if an entry on the way is missing. Large pages are never made here; one
 * found on the way counts as missing. */
static uint64_t *table_at(const struct uvm *u, uint64_t va, int level)
{
    const struct vmm_ops *o = u->ops;
    uint64_t *t = o->table(o->ctx, 4, va, u->root, false);
    for (int l = 4; l > level; l--) {
        uint64_t e = t[vm_index(va, l)];
        if (!pte_present(e) || (e & PTE_LARGE))
            return 0;
        t = o->table(o->ctx, l - 1, va, pte_frame(e), false);
    }
    return t;
}

enum uvm_result uvm_map(struct uvm *u, uint64_t va, uint64_t phys, uint64_t flags)
{
    if (!user_page(va))
        return UVM_ERR_RANGE;
    if (!vm_aligned(phys) || (phys & ~PTE_ADDR_MASK) || phys == 0)
        return UVM_ERR_PHYS;
    if ((flags & ~(PTE_WRITE | PTE_NX)) || ((flags & PTE_NX) && !u->nx))
        return UVM_ERR_FLAGS;
    if (u->nx && (flags & PTE_WRITE) && !(flags & PTE_NX))
        return UVM_ERR_WX;

    const struct vmm_ops *o = u->ops;
    uint64_t *pt = table_at(u, va, 1);
    if (pt && pte_present(pt[vm_index(va, 1)]))
        return UVM_ERR_EXISTS;

    if (!pt) {
        /* Find the deepest table present, then take every missing frame
         * first, so a failure changes nothing. */
        uint64_t *t = o->table(o->ctx, 4, va, u->root, false);
        int level = 4;
        while (level > 1) {
            uint64_t e = t[vm_index(va, level)];
            if (!pte_present(e))
                break;
            if (e & PTE_LARGE)
                return UVM_ERR_EXISTS;
            t = o->table(o->ctx, level - 1, va, pte_frame(e), false);
            level--;
        }
        uint64_t frames[3];
        int need = level - 1;
        for (int i = 0; i < need; i++) {
            frames[i] = o->alloc_frame(o->ctx);
            if (frames[i] == 0 || !vm_aligned(frames[i]) || (frames[i] & ~PTE_ADDR_MASK)) {
                if (frames[i])
                    o->free_frame(o->ctx, frames[i]);
                while (i--)
                    o->free_frame(o->ctx, frames[i]);
                return UVM_ERR_NO_FRAME;
            }
        }
        for (int i = 0; i < need; i++, level--) {
            t[vm_index(va, level)] = pte_make(frames[i], PTE_PRESENT | PTE_WRITE | PTE_USER);
            t = o->table(o->ctx, level - 1, va, frames[i], true);
            for (unsigned k = 0; k < VM_ENTRIES; k++)
                t[k] = 0;
            u->tables++;
        }
        pt = t;
    }
    /* A not-present entry is never cached by the TLB: no flush needed. */
    pt[vm_index(va, 1)] = pte_make(phys, PTE_PRESENT | PTE_USER | flags);
    u->pages++;
    return UVM_OK;
}

enum uvm_result uvm_unmap(struct uvm *u, uint64_t va, uint64_t *phys_out)
{
    if (!user_page(va))
        return UVM_ERR_RANGE;
    uint64_t *pt = table_at(u, va, 1);
    if (!pt || !pte_present(pt[vm_index(va, 1)]))
        return UVM_ERR_NOT_MAPPED;
    uint64_t entry = pt[vm_index(va, 1)];
    pt[vm_index(va, 1)] = 0;
    u->ops->flush(u->ops->ctx, va);
    if (u->pages)
        u->pages--;
    if (phys_out)
        *phys_out = pte_frame(entry);
    return UVM_OK;
}

bool uvm_translate(const struct uvm *u, uint64_t va, uint64_t *phys_out, uint64_t *flags_out)
{
    if (va < VM_USER_MIN || va >= VM_USER_TOP)
        return false;
    uint64_t *pt = table_at(u, va, 1);
    if (!pt)
        return false;
    uint64_t e = pt[vm_index(va, 1)];
    if (!pte_present(e))
        return false;
    if (phys_out)
        *phys_out = pte_frame(e) | (va & (VM_PAGE_SIZE - 1));
    if (flags_out)
        *flags_out = e & FLAG_BITS;
    return true;
}

/* Lowest virtual address covered by entry i of the table at `level` whose
 * own range starts at base. */
static uint64_t entry_base(uint64_t base, int level, unsigned i)
{
    return base + ((uint64_t)i << (12 + 9 * (level - 1)));
}

/* Frees what the entries of the table at `level` (range starting at base)
 * point to: user pages at level 1, deeper tables (and what they hold)
 * above. Clears each entry. */
static void free_below(struct uvm *u, uint64_t *t, int level, uint64_t base,
                       struct uvm_teardown_stats *st)
{
    const struct vmm_ops *o = u->ops;
    for (unsigned i = 0; i < VM_ENTRIES; i++) {
        uint64_t e = t[i];
        if (!pte_present(e))
            continue;
        if (level > 1 && (e & PTE_LARGE)) {
            st->bad_entries++;
            continue;
        }
        uint64_t va = entry_base(base, level, i);
        if (level == 1) {
            t[i] = 0;
            o->free_frame(o->ctx, pte_frame(e));
            st->pages++;
            continue;
        }
        uint64_t *child = o->table(o->ctx, level - 1, va, pte_frame(e), false);
        free_below(u, child, level - 1, va, st);
        t[i] = 0;
        o->free_frame(o->ctx, pte_frame(e));
        st->tables++;
    }
}

void uvm_teardown(struct uvm *u, struct uvm_teardown_stats *out)
{
    struct uvm_teardown_stats st = { 0, 0, 0 };
    const struct vmm_ops *o = u->ops;
    uint64_t *pml4 = o->table(o->ctx, 4, 0, u->root, false);
    for (unsigned i = 0; i < LOWER_SLOTS; i++) {
        uint64_t e = pml4[i];
        if (!pte_present(e))
            continue;
        if (e & PTE_LARGE) {
            st.bad_entries++;
            continue;
        }
        uint64_t va = entry_base(0, 4, i);
        uint64_t *pdpt = o->table(o->ctx, 3, va, pte_frame(e), false);
        free_below(u, pdpt, 3, va, &st);
        pml4[i] = 0;
        o->free_frame(o->ctx, pte_frame(e));
        st.tables++;
    }
    u->pages = 0;
    u->tables = 0;
    if (out)
        *out = st;
}

static uint32_t count_user(const struct vmm_ops *o, uint64_t *t, int level, uint64_t base)
{
    uint32_t n = 0;
    for (unsigned i = 0; i < VM_ENTRIES; i++) {
        uint64_t e = t[i];
        if (!pte_present(e))
            continue;
        if (e & PTE_USER)
            n++;
        if (level == 1 || (e & PTE_LARGE))
            continue;
        uint64_t va = entry_base(base, level, i);
        n += count_user(o, o->table(o->ctx, level - 1, va, pte_frame(e), false), level - 1, va);
    }
    return n;
}

uint32_t uvm_upper_user_entries(const struct vmm_ops *ops, uint64_t root)
{
    uint64_t *pml4 = ops->table(ops->ctx, 4, 0, root, false);
    uint32_t n = 0;
    for (unsigned i = LOWER_SLOTS; i < VM_ENTRIES; i++) {
        uint64_t e = pml4[i];
        if (!pte_present(e) || i == VM_RECURSIVE_INDEX)
            continue;
        if (e & PTE_USER)
            n++;
        if (e & PTE_LARGE)
            continue;
        uint64_t va = vm_sign_extend(entry_base(0, 4, i));
        n += count_user(ops, ops->table(ops->ctx, 3, va, pte_frame(e), false), 3, va);
    }
    return n;
}

const char *uvm_result_str(enum uvm_result result)
{
    switch (result) {
    case UVM_OK:             return "ok";
    case UVM_ERR_RANGE:      return "not a user page address";
    case UVM_ERR_EXISTS:     return "address already mapped";
    case UVM_ERR_NOT_MAPPED: return "address not mapped";
    case UVM_ERR_NO_FRAME:   return "no free frame for a page table";
    case UVM_ERR_FLAGS:      return "unsupported mapping flags";
    case UVM_ERR_WX:         return "writable and executable";
    case UVM_ERR_PHYS:       return "frame not page-aligned (or beyond 52 bits)";
    }
    return "unknown uvm error";
}
