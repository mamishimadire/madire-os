/* Validation of user buffers (see usercopy_core.h). */
#include "kernel/memory/pte.h"
#include "kernel/memory/usercopy_core.h"
#include "kernel/memory/vm_layout.h"

bool ucopy_range_ok(uint64_t addr, uint64_t len)
{
    if (len == 0)
        return true;
    if (addr >= VM_USER_TOP || len > VM_USER_TOP - addr)
        return false;                       /* also catches a wrap-around */
    return true;
}

bool ucopy_check(uint64_t addr, uint64_t len, bool write, ucopy_lookup_fn lookup, void *ctx)
{
    if (!ucopy_range_ok(addr, len))
        return false;
    if (len == 0)
        return true;
    uint64_t need = PTE_PRESENT | PTE_USER | (write ? PTE_WRITE : 0);
    uint64_t end = addr + len;              /* <= VM_USER_TOP: no overflow */
    for (uint64_t page = vm_floor(addr); page < end; page += VM_PAGE_SIZE) {
        uint64_t flags = 0;
        if (!lookup(ctx, page, &flags) || (flags & need) != need)
            return false;
    }
    return true;
}
