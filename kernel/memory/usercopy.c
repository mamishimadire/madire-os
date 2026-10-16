/* Copying from and to user memory (see usercopy.h). */
#include "kernel/arch/x86/cpuprot.h"
#include "kernel/fs/vfs.h"
#include "kernel/memory/paging.h"
#include "kernel/memory/pte.h"
#include "kernel/memory/usercopy.h"
#include "kernel/memory/usercopy_core.h"
#include "kernel/memory/uvm_core.h"
#include "kernel/panic.h"
#include "kernel/proc/klock.h"
#include "kernel/proc/thread.h"

static volatile uint64_t lock_checks;

static bool lookup(void *ctx, uint64_t va, uint64_t *flags_out)
{
    return uvm_translate(ctx, va, 0, flags_out);
}

/* The checked part of a copy: the address space loaded in CR3. */
static bool user_range(uint64_t addr, uint64_t len, bool write)
{
    uint64_t cr3;
    __asm__ volatile ("mov %%cr3, %0" : "=r"(cr3));
    struct uvm u;
    uvm_init(&u, paging_recursive_ops(), cr3 & PTE_ADDR_MASK, paging_nx_flag() != 0);
    return ucopy_check(addr, len, write, lookup, &u);
}

bool user_readable(uint64_t addr, uint64_t len)
{
    return user_range(addr, len, false);
}

bool user_writable(uint64_t addr, uint64_t len)
{
    return user_range(addr, len, true);
}

/* 1.0 stage 4: never under the VFS lock (usercopy.h). */
static void no_vfs_lock(const char *what)
{
    lock_checks++;
    if (klock_held(vfs_lock())) {
        int32_t me = thread_current();
        panicf("%s: thread %d '%s' holds the VFS lock:\n user memory is never touched "
               "under it.", what, (int)me, thread_name(me));
    }
}

uint64_t usercopy_lock_checks(void)
{
    return lock_checks;
}

bool copy_from_user(void *dst, uint64_t src, uint64_t len)
{
    no_vfs_lock("copy_from_user");
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

bool copy_to_user(uint64_t dst, const void *src, uint64_t len)
{
    no_vfs_lock("copy_to_user");
    if (!user_writable(dst, len))
        return false;
    bool smap = cpuprot_smap_active;
    if (smap)
        __asm__ volatile ("stac" ::: "memory", "cc");
    volatile uint8_t *d = (volatile uint8_t *)(uintptr_t)dst;
    const uint8_t *s = src;
    for (uint64_t i = 0; i < len; i++)
        d[i] = s[i];
    if (smap)
        __asm__ volatile ("clac" ::: "memory", "cc");
    return true;
}
