/* User programs (see process.h). */
#include "kernel/arch/x86/irq.h"
#include "kernel/arch/x86/pagefault.h"
#include "kernel/kprintf.h"
#include "kernel/memory/memory.h"
#include "kernel/memory/paging.h"
#include "kernel/memory/pte.h"
#include "kernel/memory/usercopy.h"
#include "kernel/memory/vm_layout.h"
#include "kernel/panic.h"
#include "kernel/proc/loadplan.h"
#include "kernel/proc/process.h"
#include "kernel/proc/progmod.h"
#include "kernel/proc/thread.h"

/* 0.11: what every load added up to (test=elfrun). */
static struct process_load_totals load_totals;

static bool same_str(const char *a, const char *b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

/* True if the frame lies inside a boot module (boot data): a program's
 * pages are always fresh frames, never the module's own. */
static bool frame_in_module(uint64_t frame)
{
    unsigned count;
    const struct phys_range *r = memory_reservations(&count);
    for (unsigned i = 0; i < count; i++)
        if (r[i].what && same_str(r[i].what, "boot module") && frame + VM_PAGE_SIZE > r[i].start &&
            frame < r[i].end)
            return true;
    return false;
}

/* Maps one segment page by page, each page a fresh frame filled from the
 * segment's bytes (loadplan_page) and zeros. */
static bool map_segment(struct uspace *as, const struct load_segment *s, uint64_t flags,
                        uint32_t *pages, struct process_load_stats *st)
{
    uint64_t page = loadplan_first_page(s);
    for (uint32_t i = 0; i < loadplan_pages(s); i++, page += VM_PAGE_SIZE) {
        uint64_t src_off, frame = 0;
        uint32_t at, len;
        loadplan_page(s, page, &src_off, &at, &len);
        if (uspace_map_page_at(as, page, len ? s->src + src_off : 0, len, at, flags, &frame) !=
            UVM_OK)
            return false;
        if (frame_in_module(frame))
            st->in_modules++;
        (*pages)++;
    }
    return true;
}

bool process_load_plan(struct uspace *as, const struct load_plan *plan,
                       struct process_load_stats *st)
{
    st->text = st->data = st->stack = st->in_modules = 0;
    uint64_t nx = paging_nx_flag();
    bool ok = true;
    for (uint32_t i = 0; ok && i < plan->count; i++) {
        const struct load_segment *s = &plan->seg[i];
        uint64_t flags = s->exec ? 0 : (s->write ? PTE_WRITE | nx : nx);
        ok = map_segment(as, s, flags, s->exec ? &st->text : &st->data, st);
    }
    const struct load_segment stack = { VM_USER_STACK_TOP - VM_USER_STACK_SIZE, 0,
                                        VM_USER_STACK_SIZE, 0, true, false };
    ok = ok && map_segment(as, &stack, PTE_WRITE | nx, &st->stack, st);
    uint32_t f = irq_save();
    load_totals.loads++;
    load_totals.pages += st->text + st->data + st->stack;
    load_totals.in_modules += st->in_modules;
    irq_restore(f);
    return ok;
}

bool process_load(struct uspace *as, const struct userimg *img, struct process_load_stats *st)
{
    struct load_plan plan;
    st->text = st->data = st->stack = st->in_modules = 0;
    if (!loadplan_from_userimg(img, &plan))
        return false;
    return process_load_plan(as, &plan, st);
}

void process_load_totals(struct process_load_totals *out)
{
    uint32_t f = irq_save();
    *out = load_totals;
    irq_restore(f);
}

/* ---- processes (stage 4b) ---- */

/* The table and everything per slot change only with interrupts off
 * (process_return_to_user reads them from interrupt context). */
static struct proc_table table;
static bool table_ready;
static struct uspace spaces[PROC_MAX];
static const struct userimg *images[PROC_MAX];     /* a built-in image, or */
static const uint8_t *files[PROC_MAX];             /* 0.11: an ELF file (checked again at load) */
static uint64_t file_sizes[PROC_MAX];
static uint64_t args[PROC_MAX];
static volatile uint64_t user_returns[PROC_MAX];
static uint64_t fault_addresses[PROC_MAX];          /* 0.11: "run" */
static uint8_t slot_of_tid[SCHED_MAX_THREADS];  /* slot + 1; 0 = not a process */
static struct process_teardown_totals totals;

static void table_init(void)
{
    if (!table_ready) {
        proc_init(&table);
        table_ready = true;
    }
}

/* The process's own thread: tears its address space down (if it has one;
 * CR3 is back to the kernel's afterwards), records the result and exits. */
static void process_finish(int slot, enum proc_end end, int32_t code) __attribute__((noreturn));
static void process_finish(int slot, enum proc_end end, int32_t code)
{
    if (spaces[slot].pml4) {
        struct uvm_teardown_stats st = { 0, 0, 0 };
        uspace_destroy(&spaces[slot], &st);
        uint32_t f = irq_save();
        totals.processes++;
        totals.pages += st.pages;
        totals.tables += st.tables;
        totals.bad_entries += st.bad_entries;
        irq_restore(f);
    }
    uint32_t f = irq_save();
    if (!proc_exit(&table, slot, end, code))
        panicf("process_finish: slot %d was not alive.", slot);
    slot_of_tid[thread_current()] = 0;
    irq_restore(f);
    thread_exit(code);
}

static int32_t process_thread(void *arg)
{
    int slot = (int)(intptr_t)arg;
    uint32_t f = irq_save();
    slot_of_tid[thread_current()] = (uint8_t)(slot + 1);
    irq_restore(f);
    thread_set_pid(table.e[slot].pid);     /* 0.11: for the longest-turn record */

    /* One loading path (0.11): a built-in image or an ELF file becomes a
     * load plan. The ELF file is checked again here: the check at boot is
     * not trusted alone. A refused file never gets an address space. */
    struct load_plan plan;
    bool planned;
    if (images[slot]) {
        planned = loadplan_from_userimg(images[slot], &plan);
    } else {
        struct elf_plan ep;
        enum elf_result er = elf_check(files[slot], file_sizes[slot], &ep);
        if (er != ELF_OK) {
            klogf("Process %d '%s': load failed: %s; it never ran\n", (int)table.e[slot].pid,
                  table.e[slot].name, elf_result_str(er));
            process_finish(slot, PROC_END_LOAD_FAILED, (int32_t)er);
        }
        loadplan_from_elf(files[slot], &ep, &plan);
        planned = true;
    }
    struct process_load_stats st;
    if (!planned || !uspace_create(&spaces[slot]))
        process_finish(slot, PROC_END_FAULT, -1);
    uspace_enter(&spaces[slot]);
    if (!process_load_plan(&spaces[slot], &plan, &st))
        process_finish(slot, PROC_END_FAULT, -1);
    struct user_iret frame;
    user_iret_build_entry(&frame, plan.entry);

    /* A kill requested before ring 3 ends the process here; one requested
     * from now on is seen at its first return to ring 3. */
    f = irq_save();
    bool kill = proc_kill_pending(&table, slot);
    irq_restore(f);
    if (kill)
        process_finish(slot, PROC_END_KILLED, 0);
    enter_user(&frame, args[slot]);
}

/* A process for a built-in image (img) or an ELF file (file, size). */
static int32_t start(const char *name, const struct userimg *img, const uint8_t *file,
                     uint64_t size, uint64_t arg)
{
    int slot = -1;
    int32_t pid = -1;
    uint32_t f = irq_save();
    table_init();
    enum proc_status r = proc_alloc(&table, name, &slot, &pid);
    if (r == PROC_OK) {
        images[slot] = img;
        files[slot] = file;
        file_sizes[slot] = size;
        args[slot] = arg;
        user_returns[slot] = 0;
        fault_addresses[slot] = 0;
        spaces[slot].pml4 = 0;
    }
    irq_restore(f);
    if (r != PROC_OK)
        return -1;
    int32_t tid = thread_create(name, process_thread, (void *)(intptr_t)slot, false);
    f = irq_save();
    if (tid < 0) {
        struct proc_result res;
        proc_exit(&table, slot, PROC_END_FAULT, -1);
        proc_reap(&table, pid, &res);
        pid = -1;
    } else {
        proc_set_tid(&table, slot, tid);
    }
    irq_restore(f);
    return pid;
}

int32_t process_start(const char *name, uint64_t arg)
{
    const struct userimg *img = userimg_find(user_images, user_images_count, name);
    if (!img || userimg_check(img) != USERIMG_OK)
        return -1;
    return start(img->name, img, 0, 0, arg);
}

int32_t process_start_file(const char *name, const uint8_t *file, uint64_t size, uint64_t arg)
{
    if (!name || !file)
        return -1;
    return start(name, 0, file, size, arg);
}

int32_t process_start_program(const char *name, uint64_t arg)
{
    const struct progmod *m = progmod_find(progmod_table(), name);
    if (!m)
        return -1;
    return process_start_file(m->name, (const uint8_t *)phys_view(m->phys), m->size, arg);
}

enum proc_status process_kill(int32_t pid)
{
    uint32_t f = irq_save();
    table_init();
    enum proc_status r = proc_request_kill(&table, pid);
    int slot = r == PROC_OK ? proc_find(&table, pid) : -1;
    int32_t tid = slot >= 0 ? table.e[slot].tid : -1;
    irq_restore(f);
    /* 0.11 (Q7): a process asleep in nanosleep wakes now, and ends when
     * its system call returns, instead of after the whole sleep. */
    if (tid > 0 && thread_wake(tid))
        klogf("Process %d: kill requested while asleep; woken early\n", (int)pid);
    return r;
}

enum proc_status process_wait(int32_t pid, struct proc_result *out)
{
    uint32_t f = irq_save();
    table_init();
    int slot = proc_find(&table, pid);
    int32_t tid = slot >= 0 ? table.e[slot].tid : -1;
    irq_restore(f);
    if (slot < 0)
        return PROC_ERR_NO_SUCH;
    if (tid >= 0)
        thread_join(tid);
    f = irq_save();
    enum proc_status r = proc_reap(&table, pid, out);
    irq_restore(f);
    return r;
}

/* Interrupts are off; the EOI is sent and the preemption point is passed,
 * so this thread is running as itself on its own kernel stack. With a kill
 * pending it turns interrupts back on and ends the process like any
 * thread would; the interrupt frame is abandoned with the stack. */
void process_return_to_user(const struct interrupt_frame *frame)
{
    (void)frame;
    int slot = (int)slot_of_tid[thread_current()] - 1;
    if (slot < 0)
        return;
    user_returns[slot]++;
    if (!proc_kill_pending(&table, slot))
        return;
    interrupts_enable();
    process_finish(slot, PROC_END_KILLED, 0);
}

uint32_t process_live(void)
{
    uint32_t f = irq_save();
    uint32_t n = table_ready ? proc_live(&table) : 0;
    irq_restore(f);
    return n;
}

uint32_t process_list(struct process_info *out, uint32_t max)
{
    uint32_t n = 0;
    uint32_t f = irq_save();
    for (int i = 0; table_ready && i < PROC_MAX && n < max; i++) {
        const struct proc_entry *e = &table.e[i];
        if (e->state == PROC_FREE)
            continue;
        out[n].pid = e->pid;
        out[n].tid = e->tid;
        out[n].ended = e->state == PROC_ZOMBIE;
        for (int k = 0; k < PROC_NAME_MAX; k++)
            out[n].name[k] = e->name[k];
        n++;
    }
    irq_restore(f);
    return n;
}

bool process_ended(int32_t pid)
{
    uint32_t f = irq_save();
    int slot = table_ready ? proc_find(&table, pid) : -1;
    bool ended = slot >= 0 && table.e[slot].state == PROC_ZOMBIE;
    irq_restore(f);
    return ended;
}

uint64_t process_fault_address(int32_t pid)
{
    uint32_t f = irq_save();
    int slot = table_ready ? proc_find(&table, pid) : -1;
    uint64_t a = slot >= 0 && table.e[slot].state == PROC_ZOMBIE &&
                 table.e[slot].result.end == PROC_END_FAULT ? fault_addresses[slot] : 0;
    irq_restore(f);
    return a;
}

void process_teardown_totals(struct process_teardown_totals *out)
{
    uint32_t f = irq_save();
    *out = totals;
    irq_restore(f);
}

uint64_t process_user_returns(int32_t pid)
{
    uint32_t f = irq_save();
    int slot = table_ready ? proc_find(&table, pid) : -1;
    uint64_t n = slot >= 0 ? user_returns[slot] : 0;
    irq_restore(f);
    return n;
}

/* ---- faults (stage 4c) ---- */

static struct process_fault last_fault;

/* Interrupts are off (interrupt gate), on the thread's own kernel stack
 * (the exception came from ring 3, so the CPU switched to RSP0). */
void process_fault(const struct interrupt_frame *frame)
{
    unsigned v = (unsigned)frame->vector;
    if (v == 2 || v == 8 || v == 18)        /* NMI, double fault, machine check */
        return;
    int slot = (int)slot_of_tid[thread_current()] - 1;
    if (slot < 0)
        return;
    uint64_t cr2 = 0;
    if (v == 14)
        __asm__ volatile ("mov %%cr2, %0" : "=r"(cr2));
    struct process_fault f = { table.e[slot].pid, v, frame->error_code, frame->rip, cr2, { 0, { 0 } } };
    /* The instruction bytes at RIP, through the checked user copy (its
     * address space is still loaded); not past RIP's page, so a mapped
     * page followed by an unmapped one still gives the bytes it has. */
    uint64_t room = VM_PAGE_SIZE - (frame->rip & (VM_PAGE_SIZE - 1));
    uint8_t n = room < FAULTOP_MAX ? (uint8_t)room : (uint8_t)FAULTOP_MAX;
    if (copy_from_user(f.op.b, frame->rip, n))
        f.op.n = n;
    last_fault = f;
    fault_addresses[slot] = v == 14 ? cr2 : frame->rip;
    interrupts_enable();                    /* as for a kill: the thread ends itself */
    if (v == 14) {
        uint32_t e = (uint32_t)frame->error_code;
        klogf("Process %d '%s' ended by CPU exception %u (%s %s) at RIP 0x%016llx: address "
              "0x%016llx, %s, %s, %s%s; the kernel continues\n", (int)f.pid, table.e[slot].name, v,
              exception_mnemonic(v), exception_name(v), (unsigned long long)f.rip,
              (unsigned long long)cr2, pf_cause_str(e), pf_access_str(e), pf_mode_str(e),
              pf_fetch_str(e));
    } else {
        klogf("Process %d '%s' ended by CPU exception %u (%s %s) at RIP 0x%016llx, error code "
              "0x%llx; the kernel continues\n", (int)f.pid, table.e[slot].name, v,
              exception_mnemonic(v), exception_name(v), (unsigned long long)f.rip,
              (unsigned long long)f.error_code);
    }
    process_finish(slot, PROC_END_FAULT, (int32_t)v);
}

void process_last_fault(struct process_fault *out)
{
    uint32_t f = irq_save();
    *out = last_fault;
    irq_restore(f);
}

/* ---- system calls (stage 5a) ---- */

static int current_slot(const char *what)
{
    int slot = (int)slot_of_tid[thread_current()] - 1;
    if (slot < 0)
        panicf("%s from thread %d, which is not a process.", what, (int)thread_current());
    return slot;
}

int32_t process_current_pid(void)
{
    return table.e[current_slot("getpid")].pid;
}

bool process_current_kill_pending(void)
{
    uint32_t f = irq_save();
    bool pending = proc_kill_pending(&table, current_slot("kill check"));
    irq_restore(f);
    return pending;
}

void process_exit_current(int32_t status)
{
    process_finish(current_slot("exit"), PROC_END_EXIT, status);
}

int32_t process_tid(int32_t pid)
{
    uint32_t f = irq_save();
    int slot = table_ready ? proc_find(&table, pid) : -1;
    int32_t tid = slot >= 0 ? table.e[slot].tid : -1;
    irq_restore(f);
    return tid;
}
