/*
 * Kernel crash screen.
 *
 * Called when the kernel cannot continue: an unexpected CPU exception or a
 * fatal start-up error. Everything is also written to the serial port so a
 * crash can be diagnosed from the log without seeing the screen.
 */
#include <stdarg.h>
#include <stdint.h>
#include "drivers/vga/vga_text.h"
#include "kernel/kprintf.h"
#include "kernel/panic.h"

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

static void panic_begin(void)
{
    __asm__ volatile ("cli");
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

void panic_exception(const struct interrupt_frame *f)
{
    uint32_t cr0, cr2, cr3, ss;
    __asm__ volatile ("mov %%cr0, %0" : "=r"(cr0));
    __asm__ volatile ("mov %%cr2, %0" : "=r"(cr2));
    __asm__ volatile ("mov %%cr3, %0" : "=r"(cr3));
    __asm__ volatile ("mov %%ss, %0" : "=r"(ss));

    /* Same privilege level: the CPU pushed no ESP, so the interrupted stack
     * pointer is just above the frame. */
    uint32_t esp = (uint32_t)(uintptr_t)(&f->eflags + 1);

    panic_begin();
    kprintf(" CPU exception %u: %s (%s)\n", f->vector,
            exception_names[f->vector & 31], exception_mnemonics[f->vector & 31]);
    kprintf(" Error code: 0x%08x\n\n", f->error_code);
    kprintf(" EIP=0x%08x  CS=0x%04x  EFLAGS=0x%08x\n", f->eip, f->cs & 0xFFFF, f->eflags);
    kprintf(" EAX=0x%08x  EBX=0x%08x  ECX=0x%08x  EDX=0x%08x\n", f->eax, f->ebx, f->ecx, f->edx);
    kprintf(" ESI=0x%08x  EDI=0x%08x  EBP=0x%08x  ESP=0x%08x\n", f->esi, f->edi, f->ebp, esp);
    kprintf(" DS=0x%04x  ES=0x%04x  FS=0x%04x  GS=0x%04x  SS=0x%04x\n",
            f->ds & 0xFFFF, f->es & 0xFFFF, f->fs & 0xFFFF, f->gs & 0xFFFF, ss & 0xFFFF);
    kprintf(" CR0=0x%08x  CR2=0x%08x  CR3=0x%08x\n", cr0, cr2, cr3);
    panic_halt();
}
