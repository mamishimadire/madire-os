/*
 * Paging (see paging.h), x86-64 4-level.
 *
 * The kernel starts in long mode on the boot stub's temporary tables
 * (boot/boot.asm: 0-4 GiB at 0 and in the physical view, the kernel in the
 * higher half, 2 MiB pages, page 0 included). The kernel's own tables are
 * built while those are still in use: every frame the allocator hands out
 * lies below 4 GiB, so vmm_core reaches the new PML4 and tables through the
 * physical view. Three address spaces are mapped (vm_layout.h):
 *   kernel     slot 511: the kernel image at VM_KERNEL_VIRT_OFFSET + phys
 *   phys-view  slot 256: the low 1 MiB and the boot data (never page 0,
 *              free RAM or the kernel image) at VM_PHYS_VIEW_BASE + phys
 * Nothing is mapped in the lower half (PML4 slot 0 is empty): once these
 * tables are loaded, every address in 0-4 GiB faults, which the checks
 * below prove. Before anything is loaded, the result is walk-checked: everything the
 * kernel is running on right now must translate to the right physical page
 * with the right access, and page 0 and free RAM must not be mapped. Every
 * check is logged on serial as
 * "Paging check: <item> <range>: expected <...>, actual <...>: OK|MISMATCH".
 *
 * If the check passes, CR3 is loaded (CR0.PG, CR0.WP and CR4.PAE are
 * already set by the stub); from then on the tables are reached through
 * the recursive mapping only (their frames are free RAM, not mapped). If it
 * fails, the frames are freed and the kernel keeps running on the boot
 * tables. paging_selftest() then proves the live address space (map, read
 * back, TLB flush, window, no page leak).
 */
#include "kernel/arch/x86/cpuid.h"
#include "kernel/arch/x86/idt.h"
#include "kernel/arch/x86/interrupts.h"
#include "kernel/arch/x86/irq.h"
#include "kernel/kprintf.h"
#include "kernel/panic.h"
#include "kernel/memory/memory.h"
#include "kernel/memory/paging.h"
#include "kernel/memory/pmm.h"
#include "kernel/memory/pte.h"
#include "kernel/memory/vm_layout.h"
#include "kernel/memory/vmm_core.h"
#include "kernel/proc/preempt.h"
#include "kernel/proc/sched_core.h"

extern char __kernel_start[], __text_start[], __rodata_start[], __rodata_end[], __data_start[];
extern char __bss_start[], __bss_end[], __kernel_end[];
extern char stack_bottom[], stack_top[], ist1_stack_bottom[], ist1_stack_top[];
extern char kstack_guard[], ist1_guard[];                       /* boot.asm .stacks */
extern const uint64_t isr_stub_table[IDT_HANDLED_VECTORS];      /* isr.asm */

#define SYM(s)   ((uint64_t)(uintptr_t)(s))                     /* virtual (high) */
#define KPHYS(s) (kernel_phys(SYM(s)))                          /* its physical address */

typedef unsigned long long ull;

#define VGA_TEXT_START   0x00000000000B8000ull                  /* physical */
#define VGA_TEXT_END     (VGA_TEXT_START + 80u * 25 * 2)
#define LOW_4G           0x0000000100000000ull
#define MAX_TABLE_FRAMES 64u

/* The three spaces: virtual address = base + physical address. */
#define SPACE_KERNEL     VM_KERNEL_VIRT_OFFSET
#define SPACE_VIEW       VM_PHYS_VIEW_BASE
#define SPACE_IDENTITY   0ull           /* nothing is mapped here any more */

static uint64_t pml4_phys;
static uint64_t table_frames[MAX_TABLE_FRAMES];     /* frames taken while building */
static unsigned table_frame_count;
static struct vmm vm;
static struct paging_stats stats;
static unsigned check_items, check_failures;

/* ---- vmm_core operations while the boot tables are in use ---- */

static uint64_t phys_alloc_frame(void *ctx)
{
    (void)ctx;
    if (table_frame_count >= MAX_TABLE_FRAMES)
        return 0;
    uint32_t frame = memory_alloc_page();
    if (frame)
        table_frames[table_frame_count++] = frame;
    return frame;
}

static void phys_free_frame(void *ctx, uint64_t frame)
{
    (void)ctx;
    for (unsigned i = 0; i < table_frame_count; i++)
        if (table_frames[i] == frame) {
            table_frames[i] = table_frames[--table_frame_count];
            break;
        }
    memory_free_page((uint32_t)frame);
}

static uint64_t *phys_table(void *ctx, int level, uint64_t va, uint64_t table_phys, bool fresh)
{
    (void)ctx;
    (void)level;
    (void)va;
    (void)fresh;
    return phys_view(table_phys);   /* below 4 GiB: in the stub's physical view */
}

static void phys_flush(void *ctx, uint64_t va)
{
    (void)ctx;
    (void)va;                   /* the tables being built are not loaded: nothing to flush */
}

static const struct vmm_ops phys_ops = {
    phys_alloc_frame, phys_free_frame, phys_table, phys_flush, 0
};

/* ---- vmm_core operations once CR3 holds the kernel's PML4 ---- */

static inline void invlpg(uint64_t va)
{
    __asm__ volatile ("invlpg (%0)" :: "r"(va) : "memory");
}

static inline uint64_t read_cr3(void)
{
    uint64_t v;
    __asm__ volatile ("mov %%cr3, %0" : "=r"(v));
    return v;
}

static uint64_t rec_alloc_frame(void *ctx)
{
    (void)ctx;
    return memory_alloc_page();         /* page tables live for the whole run */
}

static void rec_free_frame(void *ctx, uint64_t frame)
{
    (void)ctx;
    memory_free_page((uint32_t)frame);
}

static uint64_t *rec_table(void *ctx, int level, uint64_t va, uint64_t table_phys, bool fresh)
{
    (void)ctx;
    (void)table_phys;
    uint64_t t = vm_recursive_table(VM_RECURSIVE_INDEX, level, va);
    if (fresh)
        invlpg(t);                      /* the entry behind t just changed */
    return (uint64_t *)(uintptr_t)t;
}

static void rec_flush(void *ctx, uint64_t va)
{
    (void)ctx;
    invlpg(va);
}

static const struct vmm_ops recursive_ops = {
    rec_alloc_frame, rec_free_frame, rec_table, rec_flush, 0
};

/* ---- building ---- */

/* The two stack guard pages (boot/boot.asm .stacks): never mapped, so a
 * stack running past its bottom faults instead of overwriting what lies
 * below it. The kernel stack's overflow then becomes a double fault, which
 * runs on the IST1 stack (idt.c) and is reported (panic.c). Compared by
 * physical page, so they are holes in every space. */
static bool is_guard_phys(uint64_t phys)
{
    uint64_t page = vm_floor(phys);
    return page == KPHYS(kstack_guard) || page == KPHYS(ist1_guard);
}

static bool in_kernel_image_phys(uint64_t phys)
{
    return phys >= KPHYS(__kernel_start) && phys < KPHYS(__kernel_end);
}

/*
 * EXPECT_RX: kernel code: read-only, executable.
 * EXPECT_RO: read-only data: read-only, no-execute (when NX is in use).
 * EXPECT_RW: everything written: read/write, no-execute (when NX is in use).
 * EXPECT_KERNEL_AWARE: RX in .text, RO in .rodata, RW elsewhere (decided by
 *                      physical address, so it works in every space).
 * Without NX no entry may carry the NX bit (it would be a reserved bit).
 */
enum expect { EXPECT_RX, EXPECT_RO, EXPECT_RW, EXPECT_KERNEL_AWARE, EXPECT_UNMAPPED };

static enum expect expect_at(enum expect e, uint64_t phys)
{
    if (e != EXPECT_KERNEL_AWARE)
        return e;
    if (phys >= KPHYS(__text_start) && phys < KPHYS(__rodata_start))
        return EXPECT_RX;
    if (phys >= KPHYS(__rodata_start) && phys < KPHYS(__data_start))
        return EXPECT_RO;
    return EXPECT_RW;
}

static uint64_t flags_for(enum expect e)
{
    uint64_t nx = pte_nx_flag(stats.nx);
    return e == EXPECT_RX ? 0 : e == EXPECT_RO ? nx : PTE_WRITE | nx;
}

/* Maps physical [start, end) at base + physical, skipping the guard pages
 * and (in the physical view) the kernel image; pages already mapped (by an
 * earlier, more specific range) are left as they are when allow_existing
 * is set. */
static bool map_range(const char *what, uint64_t base, uint64_t start, uint64_t end,
                      enum expect e, bool allow_existing)
{
    for (uint64_t phys = vm_floor(start); phys < end; phys += VM_PAGE_SIZE) {
        if (is_guard_phys(phys) || (base == SPACE_VIEW && in_kernel_image_phys(phys)))
            continue;
        enum vmm_result r = vmm_map(&vm, base + phys, phys, flags_for(expect_at(e, phys)));
        if (r == VMM_OK || (r == VMM_ERR_EXISTS && allow_existing))
            continue;
        klogf("Paging build: %s: page 0x%016llx: %s\n", what, (ull)(base + phys),
              vmm_result_str(r));
        return false;
    }
    return true;
}

/* Every boot-data reservation below 4 GiB, page 0 excluded, in one space. */
static bool map_reservations(uint64_t base, enum expect e)
{
    unsigned count;
    const struct phys_range *r = memory_reservations(&count);
    for (unsigned i = 0; i < count; i++) {
        if (r[i].start >= r[i].end || r[i].start >= LOW_4G)
            continue;
        uint64_t start = r[i].start, end = r[i].end > LOW_4G ? LOW_4G : r[i].end;
        if (start < VM_NULL_GUARD_END)
            start = VM_NULL_GUARD_END;              /* page 0 stays the NULL guard */
        if (start < end && !map_range(r[i].what, base, start, end, e, true))
            return false;
    }
    return true;
}

static bool build_tables(void)
{
    pml4_phys = memory_alloc_page();
    if (!pml4_phys) {
        klogf("Paging build: no free page for the PML4\n");
        return false;
    }
    uint64_t *pml4 = phys_view(pml4_phys);
    for (uint32_t i = 0; i < VM_ENTRIES; i++)
        pml4[i] = 0;
    /* Only kernel code (.text, which includes the boot stub and every
     * interrupt/exception handler) is executable; everything else is
     * no-execute when NX is in use (the tables themselves included). */
    uint64_t nx = pte_nx_flag(stats.nx);
    pml4[VM_RECURSIVE_INDEX] = pte_make(pml4_phys, PTE_PRESENT | PTE_WRITE | nx);
    vmm_init(&vm, &phys_ops, pml4_phys);

    /* The kernel image in the higher half: code RX, rodata RO, the rest RW
     * (guard pages left out). */
    if (!map_range("kernel image", SPACE_KERNEL, KPHYS(__kernel_start), KPHYS(__kernel_end),
                   EXPECT_KERNEL_AWARE, false))
        return false;

    /* The physical view: the low 1 MiB (VGA, BIOS area; not page 0) and the
     * boot data. Never the kernel image (no writable alias of its code) and
     * never free RAM. */
    if (!map_range("physical view: low memory", SPACE_VIEW, VM_LOW_MAP_START, VM_LOW_MAP_END,
                   EXPECT_RW, false) ||
        !map_reservations(SPACE_VIEW, EXPECT_RW))
        return false;
    /* Nothing in the lower half: PML4[0] stays empty. */

    /* The test page and the window share one page table, made now so that
     * mapping either never needs a frame (not even with memory exhausted). */
    enum vmm_result w = vmm_ensure_table(&vm, VM_WINDOW);
    if (w != VMM_OK) {
        klogf("Paging build: window page table: %s\n", vmm_result_str(w));
        return false;
    }
    /* 0.9: the kernel thread stack area (slot 507, 2 MiB) has one page
     * table, made now: creating a thread then never needs a table frame
     * (so thread churn returns every frame), and the slot exists before
     * anything could copy the kernel half of the PML4 (0.10). */
    enum vmm_result k = vmm_ensure_table(&vm, VM_KSTACK_BASE);
    if (k != VMM_OK) {
        klogf("Paging build: kernel thread stack page table: %s\n", vmm_result_str(k));
        return false;
    }

    stats.mapped_pages = vm.mapped_pages;
    stats.page_tables = vm.page_tables;
    stats.kernel_ro_pages = (uint32_t)((SYM(__data_start) - SYM(__text_start)) / VM_PAGE_SIZE);
    return true;
}

static void release_tables(void)
{
    while (table_frame_count)
        memory_free_page((uint32_t)table_frames[--table_frame_count]);
    if (pml4_phys)
        memory_free_page((uint32_t)pml4_phys);
    pml4_phys = 0;
}

/* ---- walk-check ---- */

/* "rw nx", "ro exec", ... ("exec" = the entry does not forbid execution). */
static const char *access_str(bool present, uint64_t flags)
{
    if (!present)
        return "unmapped";
    if (flags & PTE_WRITE)
        return (flags & PTE_NX) ? "rw nx" : "rw exec";
    return (flags & PTE_NX) ? "ro nx" : "ro exec";
}

/* A kernel virtual address inside the read-only part (code + rodata). */
static bool in_kernel_ro(uint64_t va)
{
    return va >= SYM(__text_start) && va < SYM(__data_start);
}

static const char *space_name(uint64_t base)
{
    return base == SPACE_KERNEL ? "kernel" : base == SPACE_VIEW ? "phys-view" : "identity";
}

static void report(const char *what, uint64_t start, uint64_t end, const char *expected,
                   const char *actual, bool ok)
{
    check_items++;
    if (!ok)
        check_failures++;
    klogf("Paging check: %s 0x%016llx-0x%016llx: expected %s, actual %s: %s\n",
          what, (ull)start, (ull)end, expected, actual, ok ? "OK" : "MISMATCH");
}

/* Every page of virtual [start, end), in the space at `base` (virtual =
 * base + physical), must match. Guard pages are holes in every space, and
 * so is the kernel image in the physical view (no alias of it there). Logs
 * one line for the range; on a mismatch the actual value shown is the
 * first bad page. */
static bool check_range(const char *what, uint64_t base, uint64_t start, uint64_t end,
                        enum expect e)
{
    const char *nx = stats.nx ? "nx" : "exec", *sp = space_name(base);
    char expected[64], actual[96];
    switch (e) {
    case EXPECT_RX:
        ksnprintf(expected, sizeof(expected), "%s ro exec", sp);
        break;
    case EXPECT_RO:
        ksnprintf(expected, sizeof(expected), "%s ro %s", sp, nx);
        break;
    case EXPECT_RW:
        ksnprintf(expected, sizeof(expected), "%s rw %s", sp, nx);
        break;
    case EXPECT_KERNEL_AWARE:
        ksnprintf(expected, sizeof(expected), "%s; code ro exec, rodata ro %s, else rw %s",
                  sp, nx, nx);
        break;
    case EXPECT_UNMAPPED:
        ksnprintf(expected, sizeof(expected), "unmapped");
        break;
    }
    uint32_t pages = 0, guards = 0, kernel_holes = 0;
    bool ok = true;
    for (uint64_t va = vm_floor(start); va < end && ok; va += VM_PAGE_SIZE) {
        uint64_t phys = 0, flags = 0, want_phys = va - base;
        bool present = vmm_translate(&vm, va, &phys, &flags);
        enum expect want = expect_at(e, want_phys);
        if (want != EXPECT_UNMAPPED && is_guard_phys(want_phys)) {
            want = EXPECT_UNMAPPED;             /* inside a writable range: must be a hole */
            guards++;
        } else if (want != EXPECT_UNMAPPED && base == SPACE_VIEW &&
                   in_kernel_image_phys(want_phys)) {
            want = EXPECT_UNMAPPED;             /* the view never aliases the kernel image */
            kernel_holes++;
        }
        bool want_rw = want == EXPECT_RW;
        bool want_nx = want != EXPECT_RX && stats.nx;
        if (want == EXPECT_UNMAPPED)
            ok = !present;
        else
            ok = present && phys == want_phys && ((flags & PTE_WRITE) != 0) == want_rw &&
                 ((flags & PTE_NX) != 0) == want_nx && !(flags & PTE_USER);
        if (!ok)
            ksnprintf(actual, sizeof(actual), "page 0x%016llx -> %s 0x%016llx", (ull)va,
                      access_str(present, flags), (ull)phys);
        pages++;
        if (va + VM_PAGE_SIZE < va)
            break;
    }
    if (ok) {
        char holes[64] = "";
        if (guards)
            ksnprintf(holes, sizeof(holes), ", %u guard page%s unmapped", guards,
                      guards == 1 ? "" : "s");
        else if (kernel_holes)
            ksnprintf(holes, sizeof(holes), ", %u kernel-image page%s absent", kernel_holes,
                      kernel_holes == 1 ? "" : "s");
        if (e == EXPECT_UNMAPPED)
            ksnprintf(actual, sizeof(actual), "unmapped (%u pages)", pages);
        else if (e == EXPECT_KERNEL_AWARE)
            ksnprintf(actual, sizeof(actual), "%s as expected (%u pages%s)", sp, pages, holes);
        else
            ksnprintf(actual, sizeof(actual), "%s (%u pages%s)", expected, pages, holes);
    }
    report(what, start, end, expected, actual, ok);
    return ok;
}

/* The same range, checked by the physical addresses it maps to. */
static bool check_phys(const char *what, uint64_t base, uint64_t phys_start, uint64_t phys_end,
                       enum expect e)
{
    return check_range(what, base, base + phys_start, base + phys_end, e);
}

/* A fact about the layout (not a translation); logged the same way. */
static void check_fact(const char *what, uint64_t start, uint64_t end, const char *expected,
                       bool ok)
{
    report(what, start, end, expected, ok ? "as expected" : "different", ok);
}

static bool starts_with(const char *s, const char *prefix)
{
    while (*prefix)
        if (*s++ != *prefix++)
            return false;
    return true;
}

/* Physical start of the reservation whose name begins with `prefix`, or
 * `fallback` if there is none. */
static uint64_t reservation_start(const char *prefix, uint64_t fallback)
{
    unsigned count;
    const struct phys_range *r = memory_reservations(&count);
    for (unsigned i = 0; i < count; i++)
        if (starts_with(r[i].what, prefix))
            return r[i].start;
    return fallback;
}

/* The lower half is empty: PML4[0] is not present (so 0-512 GiB, and all
 * of 0-4 GiB, faults), checked directly, and at the addresses the kernel
 * used before the move (0.7 identity-mapped them all). `pml4_entry0` is
 * PML4[0] as read from the tables being checked. */
static void check_low_half_empty(uint64_t pml4_entry0)
{
    check_fact("lower half empty: PML4[0] not present", 0, 0x0000007FFFFFFFFFull,
               "not present (0-512 GiB unmapped)", !pte_present(pml4_entry0));
    static const struct { const char *what; uint64_t addr; } points[] = {
        { "low address 0x0 (NULL)", 0 },
        { "low address 0x100000 (kernel's physical start)", 0x100000 },
        { "low address 0xB8000 (VGA text buffer)", 0xB8000 },
        { "low address 0xFFFFF000 (last page below 4 GiB)", 0xFFFFF000ull },
    };
    for (unsigned i = 0; i < sizeof(points) / sizeof(points[0]); i++)
        check_range(points[i].what, SPACE_IDENTITY, points[i].addr, points[i].addr + 1,
                    EXPECT_UNMAPPED);
    uint64_t mbi = reservation_start("multiboot info", 0);
    check_range("low address of the Multiboot info", SPACE_IDENTITY, mbi, mbi + 1,
                EXPECT_UNMAPPED);
}

struct __attribute__((packed)) descriptor_table {
    uint16_t limit;
    uint64_t base;
};

static bool in_kernel_rw(uint64_t start, uint64_t end)
{
    return start >= SYM(__data_start) && end <= SYM(__kernel_end) && start < end;
}

/* A page-table frame of the (not yet loaded) new tables, through the view. */
static const uint64_t *table_at(uint64_t phys)
{
    return (const uint64_t *)phys_view(phys);
}

/* Entry at `level` on va's path in the new tables; 0 if a level above it
 * is missing. */
static uint64_t phys_entry(uint64_t va, int level)
{
    uint64_t table = pml4_phys;
    for (int l = 4; l > level; l--) {
        uint64_t e = table_at(table)[vm_index(va, l)];
        if (!pte_present(e))
            return 0;
        table = pte_frame(e);
    }
    return table_at(table)[vm_index(va, level)];
}

static void walk_check(void)
{
    uint64_t rip, rsp;
    __asm__ volatile ("lea 0(%%rip), %0" : "=r"(rip));
    __asm__ volatile ("mov %%rsp, %0" : "=r"(rsp));
    struct descriptor_table gdtr, idtr;
    uint16_t tr;
    __asm__ volatile ("sgdt %0" : "=m"(gdtr));
    __asm__ volatile ("sidt %0" : "=m"(idtr));
    __asm__ volatile ("str %0" : "=r"(tr));
    uint64_t gdt_end = gdtr.base + gdtr.limit + 1, idt_end = idtr.base + idtr.limit + 1;
    /* The TSS base from its 16-byte descriptor in the live GDT. */
    const uint64_t *tss_desc = (const uint64_t *)(uintptr_t)(gdtr.base + (tr & ~7u));
    uint64_t tss_base = ((tss_desc[0] >> 16) & 0xFFFFFFull) | (((tss_desc[0] >> 56) & 0xFFull) << 24) |
                        (tss_desc[1] << 32);
    uint64_t tss_end = tss_base + (tss_desc[0] & 0xFFFFull) + 1;

    check_items = check_failures = 0;

    /* What the CPU is using at this moment: all in the kernel space. */
    check_range("RIP (running code)", SPACE_KERNEL, rip, rip + 1, EXPECT_RX);
    /* Every interrupt/exception entry stub and the C handlers stay
     * executable (a no-execute handler would turn any interrupt into a
     * fault inside the fault path). */
    check_range("interrupt entry stubs (isr.asm)", SPACE_KERNEL, isr_stub_table[0],
                isr_stub_table[IDT_HANDLED_VECTORS - 1] + 1, EXPECT_RX);
    check_range("interrupt_dispatch", SPACE_KERNEL, (uint64_t)(uintptr_t)interrupt_dispatch,
                (uint64_t)(uintptr_t)interrupt_dispatch + 1, EXPECT_RX);
    check_range("panic_exception (page-fault path)", SPACE_KERNEL,
                (uint64_t)(uintptr_t)panic_exception, (uint64_t)(uintptr_t)panic_exception + 1,
                EXPECT_RX);
    check_range("irq_dispatch (timer, keyboard)", SPACE_KERNEL, (uint64_t)(uintptr_t)irq_dispatch,
                (uint64_t)(uintptr_t)irq_dispatch + 1, EXPECT_RX);
    check_range("RSP (current stack)", SPACE_KERNEL, rsp, rsp + 1, EXPECT_RW);
    check_range("kernel stack", SPACE_KERNEL, SYM(stack_bottom), SYM(stack_top), EXPECT_RW);
    check_range("IST1 stack (double faults)", SPACE_KERNEL, SYM(ist1_stack_bottom),
                SYM(ist1_stack_top), EXPECT_RW);
    /* One unmapped page directly below each stack: an overflow faults
     * instead of silently overwriting whatever lies below. */
    check_range("kernel stack guard page", SPACE_KERNEL, SYM(kstack_guard),
                SYM(kstack_guard) + VM_PAGE_SIZE, EXPECT_UNMAPPED);
    check_range("IST1 stack guard page", SPACE_KERNEL, SYM(ist1_guard),
                SYM(ist1_guard) + VM_PAGE_SIZE, EXPECT_UNMAPPED);
    check_fact("kernel stack guard directly below the stack", SYM(kstack_guard), SYM(stack_bottom),
               "guard + 4 KiB = stack bottom, page-aligned",
               vm_aligned(SYM(kstack_guard)) &&
               SYM(kstack_guard) + VM_PAGE_SIZE == SYM(stack_bottom));
    check_fact("IST1 guard directly below the IST1 stack", SYM(ist1_guard), SYM(ist1_stack_bottom),
               "guard + 4 KiB = IST1 stack bottom, page-aligned",
               vm_aligned(SYM(ist1_guard)) &&
               SYM(ist1_guard) + VM_PAGE_SIZE == SYM(ist1_stack_bottom));
    check_range("kernel code", SPACE_KERNEL, SYM(__text_start), SYM(__rodata_start), EXPECT_RX);
    check_range("kernel rodata", SPACE_KERNEL, SYM(__rodata_start), SYM(__data_start), EXPECT_RO);
    check_range("kernel data+bss", SPACE_KERNEL, SYM(__data_start), SYM(__kernel_end), EXPECT_RW);
    check_phys("VGA text buffer", SPACE_VIEW, VGA_TEXT_START, VGA_TEXT_END, EXPECT_RW);
    check_phys("low memory pages 1-255", SPACE_VIEW, VM_LOW_MAP_START, VM_LOW_MAP_END, EXPECT_RW);
    /* The physical view never aliases the kernel image (no writable view
     * of its code) and never holds page 0. */
    check_phys("kernel image not in the physical view", SPACE_VIEW, KPHYS(__kernel_start),
               KPHYS(__kernel_end), EXPECT_UNMAPPED);
    check_phys("physical view: page 0", SPACE_VIEW, 0, VM_NULL_GUARD_END, EXPECT_UNMAPPED);

    /* The CPU's own tables, found where the CPU says they are. */
    check_range("GDT (sgdt)", SPACE_KERNEL, gdtr.base, gdt_end, EXPECT_RW);
    check_range("IDT (sidt)", SPACE_KERNEL, idtr.base, idt_end, EXPECT_RW);
    check_range("TSS (str)", SPACE_KERNEL, tss_base, tss_end, EXPECT_RW);
    check_fact("GDT inside kernel .data/.bss", gdtr.base, gdt_end, "writable kernel data",
               in_kernel_rw(gdtr.base, gdt_end));
    check_fact("IDT inside kernel .data/.bss", idtr.base, idt_end, "writable kernel data",
               in_kernel_rw(idtr.base, idt_end));
    check_fact("TSS inside kernel .data/.bss", tss_base, tss_end, "writable kernel data",
               in_kernel_rw(tss_base, tss_end));

    /* The pmm bitmaps are static in pmm.c, in .bss: covered page by page. */
    check_range("pmm bitmaps (.bss)", SPACE_KERNEL, SYM(__bss_start), SYM(__bss_end), EXPECT_RW);

    /* Every boot-data reservation, in the physical view (page 0 excluded:
     * see below). The kernel image's own reservation is the item above. */
    unsigned count;
    const struct phys_range *r = memory_reservations(&count);
    for (unsigned i = 0; i < count; i++) {
        if (r[i].start >= r[i].end || r[i].start >= LOW_4G)
            continue;
        uint64_t start = r[i].start, end = r[i].end > LOW_4G ? LOW_4G : r[i].end;
        if (start < VM_NULL_GUARD_END)
            start = VM_NULL_GUARD_END;
        if (start >= KPHYS(__kernel_start) && end <= KPHYS(__kernel_end))
            continue;
        char what[80];
        ksnprintf(what, sizeof(what), "reservation %s", r[i].what);
        check_phys(what, SPACE_VIEW, start, end, EXPECT_RW);
    }

    /* Nothing in the lower half: the 0.7 identity mapping is gone. */
    check_low_half_empty(table_at(pml4_phys)[0]);

    /* Page 0 (NULL, real-mode IVT, BIOS data area 0x400, EBDA pointer
     * 0x40E) is not mapped: the boot tables map it, these must not. */
    check_range("page 0 (NULL, IVT, BDA 0x400, EBDA ptr 0x40E)", SPACE_IDENTITY, 0,
                VM_NULL_GUARD_END, EXPECT_UNMAPPED);

    /* The two new slots. */
    check_fact("kernel slot (PML4[511])", SPACE_KERNEL, SPACE_KERNEL + KPHYS(__kernel_end),
               "present, maps the kernel image",
               pte_present(table_at(pml4_phys)[VM_KERNEL_HIGH_INDEX]));
    check_fact("physical view slot (PML4[256])", SPACE_VIEW, SPACE_VIEW + LOW_4G - 1,
               "present, maps the low 1 MiB and the boot data",
               pte_present(table_at(pml4_phys)[VM_PHYS_VIEW_INDEX]));

    /* The read-only range holds only .text/.rodata, and everything the
     * kernel writes lies outside it. */
    check_fact("section starts page-aligned", SYM(__text_start), SYM(__kernel_end),
               "text/rodata/data/bss/end 4 KiB aligned",
               vm_aligned(SYM(__text_start)) && vm_aligned(SYM(__rodata_start)) &&
               vm_aligned(SYM(__data_start)) && vm_aligned(SYM(__bss_start)) &&
               vm_aligned(SYM(__kernel_end)));
    check_fact("rodata ends before writable data", SYM(__rodata_end), SYM(__data_start),
               "__rodata_end <= __data_start", SYM(__rodata_end) <= SYM(__data_start));
    check_fact("writable objects outside read-only", SYM(__text_start), SYM(__data_start),
               "stacks, GDT, IDT, TSS, .data, .bss not read-only",
               !in_kernel_ro(SYM(stack_bottom)) && !in_kernel_ro(SYM(stack_top) - 1) &&
               !in_kernel_ro(SYM(ist1_stack_bottom)) && !in_kernel_ro(gdtr.base) &&
               !in_kernel_ro(idtr.base) && !in_kernel_ro(tss_base) &&
               !in_kernel_ro(SYM(__data_start)) && !in_kernel_ro(SYM(__bss_start)));

    /* Free RAM is not mapped: the table frames themselves came from it.
     * Not at their physical address, and not in the physical view. */
    check_range("PML4 frame (free RAM)", SPACE_IDENTITY, pml4_phys, pml4_phys + 1,
                EXPECT_UNMAPPED);
    check_phys("PML4 frame (free RAM) in the physical view", SPACE_VIEW, pml4_phys,
               pml4_phys + 1, EXPECT_UNMAPPED);
    for (unsigned i = 0; i < table_frame_count; i++) {
        check_range("page table frame (free RAM)", SPACE_IDENTITY, table_frames[i],
                    table_frames[i] + 1, EXPECT_UNMAPPED);
        check_phys("page table frame (free RAM) in the physical view", SPACE_VIEW,
                   table_frames[i], table_frames[i] + 1, EXPECT_UNMAPPED);
    }

    /* Addresses reserved for later use are empty. */
    check_range("heap area (mapped by kmalloc)", SPACE_IDENTITY, VM_HEAP_BASE,
                VM_HEAP_BASE + VM_HEAP_INITIAL, EXPECT_UNMAPPED);
    check_range("paging self-test page", SPACE_IDENTITY, VM_TEST_PAGE, VM_TEST_PAGE + 1,
                EXPECT_UNMAPPED);
    check_range("physical-access window", SPACE_IDENTITY, VM_WINDOW, VM_WINDOW + 1,
                EXPECT_UNMAPPED);

    check_fact("window page table present", VM_WINDOW, VM_WINDOW + VM_PAGE_SIZE,
               "PML4, PDPT and PD entries present", pte_present(phys_entry(VM_WINDOW, 2)));
    /* 0.9: the kernel thread stack area: its table exists, nothing in it is
     * mapped yet (each thread maps its own stack; the guards never are). */
    check_range("kernel thread stack area (PML4[507]), empty at boot", SPACE_IDENTITY,
                VM_KSTACK_BASE, VM_KSTACK_BASE + KSTACK_AREA_SIZE, EXPECT_UNMAPPED);
    check_fact("kernel thread stack page table present", VM_KSTACK_BASE,
               VM_KSTACK_BASE + KSTACK_AREA_SIZE - 1, "PML4, PDPT and PD entries present",
               pte_present(phys_entry(VM_KSTACK_BASE, 2)));
    uint64_t rec = table_at(pml4_phys)[VM_RECURSIVE_INDEX];
    check_fact("recursive entry (PML4[510])", VM_RECURSIVE_BASE, VM_PML4 + VM_PAGE_SIZE - 1,
               stats.nx ? "points to the PML4, rw nx" : "points to the PML4, rw",
               pte_present(rec) && pte_writable(rec) && pte_frame(rec) == pml4_phys &&
               !(rec & PTE_USER) && pte_nx(rec) == stats.nx);

    /* Counts, recomputed independently of vmm_core's counters. */
    uint32_t tables = 0, pages = 0;
    const uint64_t *l4 = table_at(pml4_phys);
    for (uint32_t a = 0; a < VM_ENTRIES; a++) {
        if (a == VM_RECURSIVE_INDEX || !pte_present(l4[a]))
            continue;
        tables++;
        const uint64_t *l3 = table_at(pte_frame(l4[a]));
        for (uint32_t b = 0; b < VM_ENTRIES; b++) {
            if (!pte_present(l3[b]))
                continue;
            tables++;
            const uint64_t *l2 = table_at(pte_frame(l3[b]));
            for (uint32_t c = 0; c < VM_ENTRIES; c++) {
                if (!pte_present(l2[c]))
                    continue;
                tables++;
                const uint64_t *l1 = table_at(pte_frame(l2[c]));
                for (uint32_t d = 0; d < VM_ENTRIES; d++)
                    if (pte_present(l1[d]))
                        pages++;
            }
        }
    }
    char expected[48];
    ksnprintf(expected, sizeof(expected), "%u tables, %u pages", vm.page_tables, vm.mapped_pages);
    check_fact("table walk counts", 0, 0xFFFFFFFFFFFFFFFFull, expected,
               tables == vm.page_tables && pages == vm.mapped_pages);
}

/* CR3 = the new PML4. CR0.PG/WP and CR4.PAE are already set (boot stub).
 * The kernel runs in the higher half in both the boot tables and these, so
 * execution continues at the same addresses. */
static void load(void)
{
    __asm__ volatile ("mov %0, %%cr3" :: "r"(pml4_phys) : "memory");
    vm.ops = &recursive_ops;
    stats.enabled = true;
    stats.cr3 = pml4_phys;
}

bool paging_init(void)
{
    stats.enabled = false;
    stats.nx = stats.nx_supported = stats.nx_enabled = false;
    stats.cr3 = 0;
    if (!(cpu_read_efer() & EFER_LMA)) {        /* cannot happen: the stub switched */
        klogf("Paging: long mode not active; staying on the boot page tables\n");
        return false;
    }

    /* No-execute: supported (CPUID), enabled (EFER.NXE, read back), used
     * (NX bits in the tables built below) are three separate facts. */
    stats.nx_supported = cpu_nx_supported();
    if (stats.nx_supported)
        stats.nx_enabled = cpu_enable_nx();
    stats.nx = stats.nx_enabled;
    uint64_t efer = cpu_read_efer();
    if (!stats.nx_supported)
        klogf("NX: not supported by this CPU (CPUID 0x80000001 EDX bit 20 clear); "
              "EFER.NXE left clear, no NX bits in the page tables (EFER=0x%016llx)\n",
              (ull)efer);
    else
        klogf("NX: supported by the CPU (CPUID 0x80000001 EDX bit 20); EFER.NXE %s "
              "(EFER=0x%016llx, LMA %s); %s\n",
              stats.nx_enabled ? "enabled" : "COULD NOT BE SET", (ull)efer,
              (efer & EFER_LMA) ? "still set" : "CLEAR",
              stats.nx ? "NX bits used for everything but kernel code"
                       : "no NX bits in the page tables");

    uint32_t free_before = pmm_free_pages();
    table_frame_count = 0;
    bool built = build_tables();
    if (built)
        walk_check();
    bool ok = built && check_failures == 0;
    if (built)
        klogf("Paging walk-check: %s (%u items, %u mismatches)\n", ok ? "PASS" : "FAIL",
              check_items, check_failures);
    klogf("Paging tables: %u page tables + PML4, %u mapped pages, %u read-only kernel pages\n",
          stats.page_tables, stats.mapped_pages, stats.kernel_ro_pages);

    if (!ok) {
        /* Never loaded: give the frames back and prove nothing leaked. */
        uint32_t frames = table_frame_count + (pml4_phys ? 1 : 0);
        release_tables();
        uint32_t free_after = pmm_free_pages();
        klogf("Paging: kernel tables not loaded, staying on the boot page tables; "
              "%u table frames released, free pages %u -> %u\n", frames, free_before, free_after);
        if (free_after != free_before)
            klogf("Paging: page leak: %u pages not returned\n", free_before - free_after);
        return false;
    }

    load();
    klogf("Paging: enabled; CR0=0x%08x CR3=0x%016llx CR4=0x%016llx EFER=0x%016llx\n",
          cpu_read_cr0(), (ull)read_cr3(), (ull)cpu_read_cr4(), (ull)cpu_read_efer());
    return true;
}

/* ---- after loading ---- */

static void st_line(const char *what, const char *detail, bool ok)
{
    check_items++;
    if (!ok)
        check_failures++;
    klogf("Paging self-test: %s: %s: %s\n", what, detail, ok ? "OK" : "MISMATCH");
}

static uint64_t test_pattern(uint64_t frame, uint32_t i)
{
    return frame ^ ((uint64_t)i * 0x9E3779B97F4A7C15ull);
}

/* Fills (write) or checks (!write) the page at p with seed's pattern. */
static bool pattern_page(volatile uint64_t *p, uint64_t seed, bool write)
{
    for (uint32_t i = 0; i < VM_PAGE_SIZE / 8; i++) {
        if (write)
            p[i] = test_pattern(seed, i);
        else if (p[i] != test_pattern(seed, i))
            return false;
    }
    return true;
}

/* The same on the self-test page, with frame's own pattern. */
static bool test_page(uint64_t frame, bool write)
{
    return pattern_page((volatile uint64_t *)(uintptr_t)VM_TEST_PAGE, frame, write);
}

static bool test_map(uint64_t frame)
{
    return vmm_map(&vm, VM_TEST_PAGE, frame, PTE_WRITE | pte_nx_flag(stats.nx)) == VMM_OK;
}

static bool test_unmap(uint64_t frame)
{
    uint64_t phys = 0;
    return vmm_unmap(&vm, VM_TEST_PAGE, &phys) == VMM_OK && phys == frame;
}

bool paging_selftest(void)
{
    char d[112];
    if (!stats.enabled)
        return false;
    check_items = check_failures = 0;

    uint32_t cr0 = cpu_read_cr0();
    uint64_t cr3 = read_cr3(), cr4 = cpu_read_cr4(), efer = cpu_read_efer();
    ksnprintf(d, sizeof(d), "CR0=0x%08x, PG %s, WP %s", cr0, (cr0 & CR0_PG) ? "set" : "clear",
              (cr0 & CR0_WP) ? "set" : "clear");
    st_line("control register CR0", d, (cr0 & CR0_PG) && (cr0 & CR0_WP));
    ksnprintf(d, sizeof(d), "CR3=0x%016llx, PML4 at 0x%016llx", (ull)cr3, (ull)pml4_phys);
    st_line("control register CR3", d, (cr3 & PTE_ADDR_MASK) == pml4_phys);
    ksnprintf(d, sizeof(d), "CR4=0x%016llx PAE %s, EFER=0x%016llx LMA %s", (ull)cr4,
              (cr4 & CR4_PAE) ? "set" : "clear", (ull)efer, (efer & EFER_LMA) ? "set" : "clear");
    st_line("long mode (CR4.PAE, EFER.LMA)", d, (cr4 & CR4_PAE) && (efer & EFER_LMA));
    uint64_t rec = ((const volatile uint64_t *)(uintptr_t)VM_PML4)[VM_RECURSIVE_INDEX];
    ksnprintf(d, sizeof(d), "PML4[510] read at 0x%016llx = 0x%016llx", (ull)VM_PML4, (ull)rec);
    st_line("recursive mapping", d, pte_present(rec) && pte_frame(rec) == pml4_phys);

    /* The address space still holds what the kernel runs on (read through
     * the recursive mapping now), and page 0 is not mapped. */
    uint64_t rsp;
    __asm__ volatile ("mov %%rsp, %0" : "=r"(rsp));
    check_range("kernel code", SPACE_KERNEL, SYM(__text_start), SYM(__rodata_start), EXPECT_RX);
    check_range("kernel rodata", SPACE_KERNEL, SYM(__rodata_start), SYM(__data_start), EXPECT_RO);
    check_range("kernel data+bss", SPACE_KERNEL, SYM(__data_start), SYM(__kernel_end), EXPECT_RW);
    check_range("RSP (current stack)", SPACE_KERNEL, rsp, rsp + 1, EXPECT_RW);
    check_range("kernel stack guard page", SPACE_KERNEL, SYM(kstack_guard),
                SYM(kstack_guard) + VM_PAGE_SIZE, EXPECT_UNMAPPED);
    check_range("IST1 stack guard page", SPACE_KERNEL, SYM(ist1_guard),
                SYM(ist1_guard) + VM_PAGE_SIZE, EXPECT_UNMAPPED);
    check_phys("VGA text buffer", SPACE_VIEW, VGA_TEXT_START, VGA_TEXT_END, EXPECT_RW);
    check_phys("kernel image not in the physical view", SPACE_VIEW, KPHYS(__kernel_start),
               KPHYS(__kernel_end), EXPECT_UNMAPPED);
    unsigned count;
    const struct phys_range *r = memory_reservations(&count);
    for (unsigned i = 0; i < count; i++) {
        if (r[i].start >= r[i].end || r[i].start >= LOW_4G || r[i].start < 0x100000u)
            continue;                               /* low 1 MiB: covered by the build check */
        uint64_t end = r[i].end > LOW_4G ? LOW_4G : r[i].end;
        if (r[i].start >= KPHYS(__kernel_start) && end <= KPHYS(__kernel_end))
            continue;                               /* the kernel image: the item above */
        char what[80];
        ksnprintf(what, sizeof(what), "reservation %s", r[i].what);
        check_phys(what, SPACE_VIEW, r[i].start, end, EXPECT_RW);
    }
    check_range("page 0 (NULL, IVT, BDA 0x400, EBDA ptr 0x40E)", SPACE_IDENTITY, 0,
                VM_NULL_GUARD_END, EXPECT_UNMAPPED);
    check_phys("physical view: page 0", SPACE_VIEW, 0, VM_NULL_GUARD_END, EXPECT_UNMAPPED);
    /* The live tables, read through the recursive mapping. */
    check_low_half_empty(((const volatile uint64_t *)(uintptr_t)VM_PML4)[0]);

    /* Map, write, read back, translate. */
    uint32_t free_before = pmm_free_pages();
    uint64_t a = memory_alloc_page(), b = memory_alloc_page();
    ksnprintf(d, sizeof(d), "frames A=0x%016llx B=0x%016llx", (ull)a, (ull)b);
    st_line("test frames allocated", d, a && b);
    if (a && b) {
        uint64_t phys = 0;
        bool mapped = test_map(a);
        bool ok = mapped && (test_page(a, true), test_page(a, false)) &&
                  vmm_translate(&vm, VM_TEST_PAGE + 0x123, &phys, 0) && phys == a + 0x123;
        ksnprintf(d, sizeof(d), "0x%016llx -> A, 4 KiB written and read back, translates to 0x%016llx",
                  (ull)VM_TEST_PAGE, (ull)phys);
        st_line("map and read back", d, ok);

        ok = mapped && test_unmap(a) && !vmm_translate(&vm, VM_TEST_PAGE, 0, 0);
        st_line("unmap", "test page no longer translates; frame A returned", ok);

        /* TLB flush: the same virtual page now goes to B. If the old
         * translation were still cached, B's pattern would land in A. */
        mapped = test_map(b);
        ok = mapped && (test_page(b, true), test_page(b, false)) && test_unmap(b);
        mapped = test_map(a);
        bool a_intact = mapped && test_page(a, false);
        ok = ok && a_intact && test_unmap(a);
        mapped = test_map(b);
        bool b_intact = mapped && test_page(b, false);
        ok = ok && b_intact && test_unmap(b);
        ksnprintf(d, sizeof(d), "remapped A -> B -> A -> B: A kept %s, B kept %s",
                  a_intact ? "its pattern" : "B's pattern (stale TLB)",
                  b_intact ? "its pattern" : "a wrong pattern");
        st_line("TLB flush on remap", d, ok);

        /* Physical-access window: map -> access -> unmap -> not mapped.
         * It must reach frame A itself: it sees the pattern written through
         * the test page, and what it writes is then found in A. */
        volatile uint64_t *w = vmm_window_map(a);
        uint64_t wphys = 0;
        bool w_ok = paging_translate(VM_WINDOW, &wphys, 0) && wphys == a &&
                    pattern_page(w, a, false);
        pattern_page(w, ~a, true);
        w_ok = w_ok && pattern_page(w, ~a, false);
        vmm_window_unmap();
        bool w_gone = !vmm_translate(&vm, VM_WINDOW, 0, 0);
        mapped = test_map(a);
        bool landed = mapped &&
                      pattern_page((volatile uint64_t *)(uintptr_t)VM_TEST_PAGE, ~a, false);
        ok = w_ok && w_gone && landed && test_unmap(a);
        ksnprintf(d, sizeof(d), "0x%016llx -> A read and written; after unmap %s; data in A: %s",
                  (ull)VM_WINDOW, w_gone ? "not mapped" : "STILL MAPPED", landed ? "yes" : "no");
        st_line("physical-access window", d, ok);
    }
    if (a)
        memory_free_page((uint32_t)a);
    if (b)
        memory_free_page((uint32_t)b);
    uint32_t free_after = pmm_free_pages();
    ksnprintf(d, sizeof(d), "free pages %u -> %u", free_before, free_after);
    st_line("no page leak", d, free_after == free_before && !vmm_translate(&vm, VM_TEST_PAGE, 0, 0) &&
                               !vmm_translate(&vm, VM_WINDOW, 0, 0));

    klogf("Paging self-test: %s (%u checks, %u mismatches)\n", check_failures ? "FAIL" : "PASS",
          check_items, check_failures);
    return check_failures == 0;
}

bool paging_enabled(void)
{
    return stats.enabled;
}

uint64_t paging_nx_flag(void)
{
    return pte_nx_flag(stats.enabled && stats.nx);
}

static void require_enabled(const char *what)
{
    if (!stats.enabled)
        panicf("%s called while the kernel's page tables are not loaded.", what);
}

/* 0.9: the tables (reached through the recursive mapping) and the counts in
 * `vm` and `stats` have no locking; each call runs with preemption off
 * (kernel/proc/preempt.h). */
enum vmm_result paging_map(uint64_t va, uint64_t phys, uint64_t flags)
{
    require_enabled("paging_map");
    preempt_disable();
    enum vmm_result r = vmm_map(&vm, va, phys, flags);
    stats.page_tables = vm.page_tables;
    preempt_enable();
    if (r == VMM_ERR_FROZEN)            /* 0.10: a kernel bug, never an ordinary error */
        panicf("Kernel-half PML4 entry created after the freeze: 0x%016llx (PML4 slot %u).",
               (ull)va, vm_index(va, 4));
    return r;
}

void paging_freeze_kernel_half(void)
{
    require_enabled("paging_freeze_kernel_half");
    char list[96];
    size_t n = 0;
    unsigned count = 0;
    preempt_disable();
    vmm_freeze_upper(&vm);
    const volatile uint64_t *pml4 = (const volatile uint64_t *)(uintptr_t)VM_PML4;
    for (unsigned i = VM_ENTRIES / 2; i < VM_ENTRIES; i++) {
        if (!pte_present(pml4[i]) || n >= sizeof(list) - 8)
            continue;
        n += ksnprintf(list + n, sizeof(list) - n, "%s%u", count ? ", " : "", i);
        count++;
    }
    preempt_enable();
    klogf("Kernel half frozen: PML4 slots %s (%u entries); every process shares them, "
          "a new one now panics\n", list, count);
}

enum vmm_result paging_unmap(uint64_t va, uint64_t *phys_out)
{
    require_enabled("paging_unmap");
    preempt_disable();
    enum vmm_result r = vmm_unmap(&vm, va, phys_out);
    preempt_enable();
    return r;
}

bool paging_translate(uint64_t va, uint64_t *phys_out, uint64_t *flags_out)
{
    preempt_disable();
    bool r = stats.enabled && vmm_translate(&vm, va, phys_out, flags_out);
    preempt_enable();
    return r;
}

/* The window's page table was created at build time, so mapping it never
 * needs a frame (it works even when physical memory is exhausted).
 *
 * 0.9: there is one window, so its user keeps preemption off from
 * vmm_window_map() to vmm_window_unmap(): no other thread can map it in
 * between, and yielding while holding it panics (kernel/proc/thread.c). */
static bool window_mapped;

volatile void *vmm_window_map(uint64_t phys)
{
    require_enabled("vmm_window_map");
    preempt_disable();                  /* until vmm_window_unmap() */
    if (window_mapped)
        panic("vmm_window_map: window already in use (missing vmm_window_unmap).");
    enum vmm_result r = vmm_map(&vm, VM_WINDOW, phys, PTE_WRITE | pte_nx_flag(stats.nx));
    if (r != VMM_OK)
        panicf("vmm_window_map(0x%016llx): %s.", (ull)phys, vmm_result_str(r));
    window_mapped = true;
    return (volatile void *)(uintptr_t)VM_WINDOW;
}

void vmm_window_unmap(void)
{
    require_enabled("vmm_window_unmap");
    if (!window_mapped)
        panic("vmm_window_unmap: window not mapped.");
    /* Clears the entry and invalidates the TLB entry (vmm_core flushes on
     * unmap). The frame is not freed: it belongs to the caller. */
    enum vmm_result r = vmm_unmap(&vm, VM_WINDOW, 0);
    if (r != VMM_OK || vmm_translate(&vm, VM_WINDOW, 0, 0))
        panicf("vmm_window_unmap: %s.", r != VMM_OK ? vmm_result_str(r) : "window still mapped");
    window_mapped = false;
    preempt_enable();                   /* taken in vmm_window_map() */
}

const struct paging_stats *paging_stats(void)
{
    return &stats;
}

const struct vmm_ops *paging_recursive_ops(void)
{
    return &recursive_ops;
}

uint64_t paging_kernel_cr3(void)
{
    return stats.cr3;
}
