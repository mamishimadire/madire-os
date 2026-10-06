/*
 * Kernel crash screen.
 *
 * Called when the kernel cannot continue: an unexpected CPU exception or a
 * fatal start-up error. Everything is also written to the serial port so a
 * crash can be diagnosed from the log without seeing the screen.
 */
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include "drivers/vga/vga_text.h"
#include "kernel/arch/x86/cpuid.h"
#include "kernel/arch/x86/pagefault.h"
#include "kernel/kprintf.h"
#include "kernel/panic.h"
#include "kernel/proc/sched_core.h"
#include "kernel/proc/thread.h"

static const char *const exception_names[32] = {
    "Divide Error", "Debug", "Non-Maskable Interrupt", "Breakpoint",
    "Overflow", "Bound Range Exceeded", "Invalid Opcode", "Device Not Available",
    "Double Fault", "Coprocessor Segment Overrun", "Invalid TSS", "Segment Not Present",
    "Stack-Segment Fault", "General Protection Fault", "Page Fault", "Reserved",
    "x87 Floating-Point Error", "Alignment Check", "Machine Check", "SIMD Floating-Point Error",
    "Virtualization Exception", "Control Protection Exception", "Reserved", "Reserved",
    "Reserved", "Reserved", "Reserved", "Reserved",
    "Hypervisor Injection Exception", "VMM Communication Exception", "Security Exception", "Reserved",
};

static const char *const exception_mnemonics[32] = {
    "#DE", "#DB", "NMI", "#BP", "#OF", "#BR", "#UD", "#NM",
    "#DF", "-", "#TS", "#NP", "#SS", "#GP", "#PF", "-",
    "#MF", "#AC", "#MC", "#XM", "#VE", "#CP", "-", "-",
    "-", "-", "-", "-", "#HV", "#VC", "#SX", "-",
};

volatile bool panic_active;

static void panic_begin(void)
{
    __asm__ volatile ("cli");
    panic_active = true;            /* 0.9: exempt from the thread-0 screen rule */
    vga_set_color(VGA_WHITE, VGA_RED);
    vga_clear();
    kputs("\n *** MADIRE OS - KERNEL PANIC ***\n\n");
}

static void __attribute__((noreturn)) panic_halt(void)
{
    kputs("\n System halted. Power off the machine.\n");
    for (;;)
        __asm__ volatile ("cli; hlt");
}

void panic(const char *message)
{
    panic_begin();
    kprintf(" %s\n", message);
    panic_halt();
}

void panicf(const char *fmt, ...)
{
    va_list ap;
    panic_begin();
    kputc(' ');
    va_start(ap, fmt);
    kvprintf(fmt, ap);
    va_end(ap);
    kputc('\n');
    panic_halt();
}

/* Exception 14: the error code decoded, and the faulting address (CR2).
 * Every page fault is fatal: no page is ever brought in on demand. Split
 * over up to three lines so none reaches the 80th column (an 80-character
 * line would leave a blank line: vga_putc moves on after column 79). */
static void print_page_fault(uint32_t error_code, uint64_t cr2)
{
    kprintf(" Page fault: %s, %s, %s\n", pf_cause_str(error_code), pf_access_str(error_code),
            pf_mode_str(error_code));
    /* The optional parts each start with ", "; drop the first one. */
    const char *reserved = pf_reserved_str(error_code), *fetch = pf_fetch_str(error_code);
    if (reserved[0])
        kprintf(" Details: %s%s\n", reserved + 2, fetch);
    else if (fetch[0])
        kprintf(" Details: %s\n", fetch + 2);
    kprintf(" Address: 0x%016llx\n", (unsigned long long)cr2);
}

typedef unsigned long long ull;

/* The stack guard pages (boot/boot.asm .stacks; never mapped). */
extern char kstack_guard[], ist1_guard[];

static bool in_page(uint64_t addr, const char *page)
{
    uint64_t start = (uint64_t)(uintptr_t)page;
    return addr >= start && addr < start + 4096;
}

/* Exception 8 runs on the IST1 stack, so it is reported even when the
 * kernel stack is gone. A kernel stack overflow is the case the guard
 * page exists for: the stack reaches it, the #PF cannot be pushed, and
 * the CPU raises #DF instead. Anything else is said as it is. */
static void print_double_fault(uint64_t rsp, uint64_t cr2)
{
    uint64_t g = (uint64_t)(uintptr_t)kstack_guard;
    if (in_page(rsp, kstack_guard)) {
        kputs(" Double fault: kernel stack overflow (RSP is in the stack guard page)\n");
        kprintf(" RSP=0x%016llx  guard page 0x%016llx-0x%016llx\n", (ull)rsp, (ull)g,
                (ull)(g + 4095));
    } else if (in_page(cr2, kstack_guard)) {
        kputs(" Double fault: kernel stack overflow (the stack reached its guard page)\n");
        kprintf(" CR2=0x%016llx  guard page 0x%016llx-0x%016llx\n", (ull)cr2, (ull)g,
                (ull)(g + 4095));
    } else if (kstack_guard_slot(rsp) >= 0 || kstack_guard_slot(cr2) >= 0) {
        /* 0.9: a kernel thread's stack (slot 507) ran into its guard. */
        bool by_rsp = kstack_guard_slot(rsp) >= 0;
        int slot = by_rsp ? kstack_guard_slot(rsp) : kstack_guard_slot(cr2);
        int32_t tid = thread_slot_owner(slot);
        kprintf(" Double fault: kernel thread stack overflow (thread %d '%s')\n", (int)tid,
                tid >= 0 ? thread_name(tid) : "?");
        kprintf(" %s=0x%016llx  guard 0x%016llx-0x%016llx\n", by_rsp ? "RSP" : "CR2",
                (ull)(by_rsp ? rsp : cr2), (ull)kstack_slot_base(slot),
                (ull)(kstack_bottom(slot) - 1));
    } else if (in_page(rsp, ist1_guard) || in_page(cr2, ist1_guard)) {
        kputs(" Double fault: IST1 stack overflow (it reached its guard page)\n");
    } else {
        kputs(" Double fault: handled on its own stack (IST1); RSP not in the stack guard page\n");
    }
}

/* x86-64 register dump; every line fits the 80-column screen. The CPU
 * always pushes SS:RSP in long mode, so RSP is the interrupted stack. */
void panic_exception(const struct interrupt_frame *f)
{
    uint64_t cr0, cr2, cr3, cr4;
    uint16_t ds, es, fs, gs;
    __asm__ volatile ("mov %%cr0, %0" : "=r"(cr0));
    __asm__ volatile ("mov %%cr2, %0" : "=r"(cr2));
    __asm__ volatile ("mov %%cr3, %0" : "=r"(cr3));
    __asm__ volatile ("mov %%cr4, %0" : "=r"(cr4));
    __asm__ volatile ("mov %%ds, %0" : "=r"(ds));
    __asm__ volatile ("mov %%es, %0" : "=r"(es));
    __asm__ volatile ("mov %%fs, %0" : "=r"(fs));
    __asm__ volatile ("mov %%gs, %0" : "=r"(gs));
    uint64_t efer = cpu_read_efer();

    panic_begin();
    kprintf(" CPU exception %u: %s (%s)\n", (uint32_t)f->vector,
            exception_names[f->vector & 31], exception_mnemonics[f->vector & 31]);
    kprintf(" Error code: 0x%08x\n", (uint32_t)f->error_code);
    if (f->vector == 14)
        print_page_fault((uint32_t)f->error_code, cr2);
    if (f->vector == 8)
        print_double_fault(f->rsp, cr2);
    kputc('\n');
    kprintf(" RIP=0x%016llx  CS=0x%04x  RFLAGS=0x%016llx\n",
            (ull)f->rip, (uint32_t)(f->cs & 0xFFFF), (ull)f->rflags);
    kprintf(" RAX=0x%016llx  RBX=0x%016llx  RCX=0x%016llx\n", (ull)f->rax, (ull)f->rbx, (ull)f->rcx);
    kprintf(" RDX=0x%016llx  RSI=0x%016llx  RDI=0x%016llx\n", (ull)f->rdx, (ull)f->rsi, (ull)f->rdi);
    kprintf(" RBP=0x%016llx  RSP=0x%016llx  R8 =0x%016llx\n", (ull)f->rbp, (ull)f->rsp, (ull)f->r8);
    kprintf(" R9 =0x%016llx  R10=0x%016llx  R11=0x%016llx\n", (ull)f->r9, (ull)f->r10, (ull)f->r11);
    kprintf(" R12=0x%016llx  R13=0x%016llx  R14=0x%016llx\n", (ull)f->r12, (ull)f->r13, (ull)f->r14);
    kprintf(" R15=0x%016llx  SS=0x%04x  DS=0x%04x  ES=0x%04x  FS=0x%04x  GS=0x%04x\n",
            (ull)f->r15, (uint32_t)(f->ss & 0xFFFF), (uint32_t)ds, (uint32_t)es, (uint32_t)fs,
            (uint32_t)gs);
    kprintf(" CR0=0x%016llx  CR2=0x%016llx  CR3=0x%016llx\n", (ull)cr0, (ull)cr2, (ull)cr3);
    kprintf(" CR4=0x%016llx  EFER=0x%016llx\n", (ull)cr4, (ull)efer);
    panic_halt();
}
