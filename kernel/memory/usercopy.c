/* Copying from user memory (see usercopy.h). */
#include "kernel/arch/x86/cpuprot.h"
#include "kernel/memory/paging.h"
#include "kernel/memory/pte.h"
#include "kernel/memory/usercopy.h"
#include "kernel/memory/usercopy_core.h"
#include "kernel/memory/uvm_core.h"

static bool lookup(void *ctx, uint64_t va, uint64_t *flags_out)
{
    return uvm_translate(ctx, va, 0, flags_out);
}

/* The checked part of a copy: the address space loaded in CR3. */
bool user_readable(uint64_t addr, uint64_t len)
{
    uint64_t cr3;
    __asm__ volatile ("mov %%cr3, %0" : "=r"(cr3));
    struct uvm u;
    uvm_init(&u, paging_recursive_ops(), cr3 & PTE_ADDR_MASK, paging_nx_flag() != 0);
    return ucopy_check(addr, len, false, lookup, &u);
}

bool copy_from_user(void *dst, uint64_t src, uint64_t len)
{
    if (!user_readable(src, len))
        return false;
    bool smap = cpuprot_smap_active;
    if (smap)
        __asm__ volatile ("stac" ::: "memory", "cc");
    const volatile uint8_t *s = (const volatile uint8_t *)(uintptr_t)src;
    uint8_t *d = dst;
    for (uint64_t i = 0; i < len; i++)
        d[i] = s[i];
    if (smap)
        __asm__ volatile ("clac" ::: "memory", "cc");
    return true;
}
