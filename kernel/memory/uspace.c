/* Address spaces (see uspace.h). */
#include "kernel/arch/x86/irq.h"
#include "kernel/memory/memory.h"
#include "kernel/memory/paging.h"
#include "kernel/memory/pte.h"
#include "kernel/memory/uspace.h"
#include "kernel/memory/vm_layout.h"
#include "kernel/panic.h"
#include "kernel/proc/thread.h"

static uint64_t read_cr3(void)
{
    uint64_t v;
    __asm__ volatile ("mov %%cr3, %0" : "=r"(v));
    return v;
}

/* 0.12 stage 2b (self-check): every user address space's PML4 frame from
 * uspace_create until uspace_destroy frees it. Global, so the stall capture
 * (tests/qemu_capture.py) can read it. Changed with interrupts off. */
uint64_t uspace_live_pml4[USPACE_LIVE_MAX];
uint32_t uspace_live_count;

static bool live_add(uint64_t pml4)
{
    uint32_t f = irq_save();
    bool ok = uspace_live_count < USPACE_LIVE_MAX;
    if (ok)
        uspace_live_pml4[uspace_live_count++] = pml4;
    irq_restore(f);
    return ok;
}

static void live_remove(uint64_t pml4)
{
    uint32_t f = irq_save();
    for (uint32_t i = 0; i < uspace_live_count; i++) {
        if (uspace_live_pml4[i] == pml4) {
            uspace_live_pml4[i] = uspace_live_pml4[--uspace_live_count];
            uspace_live_pml4[uspace_live_count] = 0;
            irq_restore(f);
            return;
        }
    }
    irq_restore(f);
    panicf("uspace_destroy: PML4 0x%016llx is not a live\n address space.",
           (unsigned long long)pml4);
}

bool uspace_live(uint64_t pml4)
{
    pml4 &= PTE_ADDR_MASK;
    for (uint32_t i = 0; i < uspace_live_count; i++)
        if (uspace_live_pml4[i] == pml4)
            return true;
    return false;
}

void uspace_test_forget(uint64_t pml4)
{
    uint32_t f = irq_save();
    for (uint32_t i = 0; i < uspace_live_count; i++)
        if (uspace_live_pml4[i] == pml4)
            uspace_live_pml4[i] = uspace_live_pml4[--uspace_live_count];
    irq_restore(f);
}

bool uspace_is_current(const struct uspace *as)
{
    return as->pml4 && (read_cr3() & PTE_ADDR_MASK) == as->pml4;
}

bool uspace_create(struct uspace *as)
{
    as->pml4 = 0;
    uint32_t frame = memory_alloc_page();
    if (!frame)
        return false;
    if (!live_add(frame)) {                 /* 0.12 stage 2b: never expected */
        memory_free_page(frame);
        return false;
    }
    /* The kernel half as the kernel's PML4 has it. Read through the
     * recursive mapping of whatever PML4 is loaded: every PML4 has the same
     * kernel half (the freeze), except entry 510, which is replaced. */
    uint64_t upper[VM_ENTRIES / 2];
    const volatile uint64_t *cur = (const volatile uint64_t *)(uintptr_t)VM_PML4;
    for (unsigned i = 0; i < VM_ENTRIES / 2; i++)
        upper[i] = cur[VM_ENTRIES / 2 + i];
    volatile uint64_t *w = vmm_window_map(frame);
    for (unsigned i = 0; i < VM_ENTRIES / 2; i++)
        w[i] = 0;
    for (unsigned i = 0; i < VM_ENTRIES / 2; i++)
        w[VM_ENTRIES / 2 + i] = upper[i];
    w[VM_RECURSIVE_INDEX] = pte_make(frame, PTE_PRESENT | PTE_WRITE | paging_nx_flag());
    vmm_window_unmap();
    as->pml4 = frame;
    uvm_init(&as->uvm, paging_recursive_ops(), frame, paging_nx_flag() != 0);
    return true;
}

void uspace_enter(struct uspace *as)
{
    thread_set_cr3(as->pml4);
}

void uspace_leave(void)
{
    thread_set_cr3(0);
}

enum uvm_result uspace_map_page(struct uspace *as, uint64_t va, const void *src, size_t len,
                                uint64_t flags, uint64_t *frame_out)
{
    return uspace_map_page_at(as, va, src, len, 0, flags, frame_out);
}

enum uvm_result uspace_map_page_at(struct uspace *as, uint64_t va, const void *src, size_t len,
                                   size_t at, uint64_t flags, uint64_t *frame_out)
{
    if (!uspace_is_current(as))
        panicf("uspace_map_page(0x%016llx): the address space is not loaded.",
               (unsigned long long)va);
    if (at > VM_PAGE_SIZE || len > VM_PAGE_SIZE - at)
        panicf("uspace_map_page(0x%016llx): %llu bytes at %llu do not fit the page.",
               (unsigned long long)va, (unsigned long long)len, (unsigned long long)at);
    uint32_t frame = memory_alloc_page();
    if (!frame)
        return UVM_ERR_NO_FRAME;
    volatile uint8_t *w = vmm_window_map(frame);
    const uint8_t *s = src;
    for (size_t i = 0; i < VM_PAGE_SIZE; i++)
        w[i] = (s && i >= at && i - at < len) ? s[i - at] : 0;
    vmm_window_unmap();
    enum uvm_result r = uvm_map(&as->uvm, va, frame, flags);
    if (r != UVM_OK) {
        memory_free_page(frame);
        return r;
    }
    if (frame_out)
        *frame_out = frame;
    return UVM_OK;
}

void uspace_destroy(struct uspace *as, struct uvm_teardown_stats *out)
{
    if (!uspace_is_current(as))
        panic("uspace_destroy: the address space is not loaded.");
    uvm_teardown(&as->uvm, out);
    uspace_leave();                 /* CR3 = the kernel's: nothing uses this PML4 now */
    live_remove(as->pml4);          /* 0.12 stage 2b: before the frame is free */
    memory_free_page((uint32_t)as->pml4);
    as->pml4 = 0;
}
