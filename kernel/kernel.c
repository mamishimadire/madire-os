/*
 * Madire OS kernel entry.
 *
 * Runs in 32-bit protected mode, entered from boot/boot.asm. Brings up the
 * display, replaces GRUB's CPU tables with our own (GDT, IDT), makes the
 * interrupt controller safe, proves exception handling works, sets up and
 * tests physical memory, enables the timer (IRQ0) and keyboard (IRQ1),
 * self-tests the console, then runs the idle loop, which opens the console
 * and shell on the first key press.
 */
#include <stdbool.h>
#include <stddef.h>
#include "drivers/keyboard/ps2kbd.h"
#include "drivers/serial/serial.h"
#include "drivers/vga/vga_text.h"
#include "kernel/arch/x86/cpuid.h"
#include "kernel/arch/x86/cpuprot.h"
#include "kernel/arch/x86/gdt.h"
#include "kernel/arch/x86/gdt_desc.h"
#include "kernel/arch/x86/idt.h"
#include "kernel/arch/x86/interrupts.h"
#include "kernel/arch/x86/irq.h"
#include "kernel/arch/x86/pic.h"
#include "kernel/arch/x86/reset.h"
#include "kernel/bootmod.h"
#include "kernel/bootscreen.h"
#include "kernel/console/console.h"
#include "kernel/devtest.h"
#include "kernel/kernel.h"
#include "kernel/kprintf.h"
#include "kernel/memory/kmalloc.h"
#include "kernel/memory/memory.h"
#include "kernel/memory/memtest.h"
#include "kernel/memory/paging.h"
#include "kernel/memory/pte.h"
#include "kernel/memory/vm_layout.h"
#include "kernel/memory/vmtest.h"
#include "kernel/multiboot.h"
#include "kernel/panic.h"
#include "kernel/proc/preempt.h"
#include "kernel/proc/schedtest.h"
#include "kernel/proc/thread.h"
#include "kernel/proc/syscall.h"
#include "kernel/proc/usertest.h"
#include "kernel/stackpeak.h"
#include "kernel/timer.h"

/* The only IRQ lines 0.4 may unmask. */
#define IRQ_ALLOWED_MASK ((1u << IRQ_TIMER) | (1u << IRQ_KEYBOARD))

/* Kernel stack high-water mark (0.8). At the very start of kernel_main the
 * unused part of the stack is painted: from its bottom up to STACK_PAINT_
 * MARGIN bytes below the current RSP. The margin keeps the paint away from
 * the live frames (kernel_main's own is already allocated; stack_paint is
 * a leaf call that uses only its 8-byte return address below RSP), and
 * interrupts are still off (boot.asm: cli, then RFLAGS = 0; the first sti
 * is interrupts_enable() further down), so no interrupt frame can land in
 * the painted area. Bytes in the margin count as used. */
#define STACK_PAINT_MARGIN 256u

extern char stack_bottom[], stack_top[];               /* boot/boot.asm */
static uint8_t *paint_lo;
static size_t paint_len;
static uint64_t paint_rsp;

uint32_t kernel_stack_size(void)
{
    return (uint32_t)(stack_top - stack_bottom);
}

uint32_t kernel_stack_peak(void)
{
    size_t untouched = paint_len - stack_peak(paint_lo, paint_len);
    return kernel_stack_size() - (uint32_t)untouched;
}

static const char *cmdline = "";

/* True if `word` appears in the boot command line (e.g. "test=divzero"). */
static int cmdline_has(const char *word)
{
    for (const char *p = cmdline; *p; p++) {
        size_t i = 0;
        while (word[i] && p[i] == word[i])
            i++;
        if (!word[i])
            return 1;
    }
    return 0;
}

/*
 * 0.9.2: true if `word` is a whole word of the command line (separated by
 * spaces or tabs), so "nokeylog" or "keylog2" don't count. Used for the
 * security switch "keylog"; cmdline_has() keeps its substring match for
 * the test= options.
 */
static int cmdline_word(const char *word)
{
    const char *p = cmdline;
    while (*p) {
        while (*p == ' ' || *p == '\t')
            p++;
        const char *start = p;
        while (*p && *p != ' ' && *p != '\t')
            p++;
        size_t len = (size_t)(p - start), i = 0;
        while (i < len && word[i] && start[i] == word[i])
            i++;
        if (len > 0 && i == len && !word[i])
            return 1;
    }
    return 0;
}

/*
 * 0.9.2: key events are logged (serial, and so dmesg) only for tests and
 * diagnostics: with the boot option "keylog", or during the keyboard
 * diagnostics, which show key codes by design (devtest.c logs those
 * itself). Off by default, so typed text never reaches the logs. No secret
 * prompt exists yet; one must add secret-input mode (AGENTS.md).
 */
static bool key_logging;

static void key_logging_init(void)
{
    if (cmdline_word("keylog")) {
        key_logging = true;
        klogf("Key events: logged (boot option 'keylog')\n");
    } else if (cmdline_has("test=keyboard") || cmdline_has("test=kbdflood")) {
        key_logging = true;
        klogf("Key events: logged (keyboard diagnostic)\n");
    } else {
        key_logging = false;
        klogf("Key events: not logged (boot option 'keylog' logs them; for tests only)\n");
    }
}

/*
 * The boot screen while other threads may exist (0.9). bootscreen.c has no
 * protection of its own (and is protected from change), so every use that
 * can happen once threads exist goes through BOOTSCREEN_SAFE(call): it
 * panics unless the caller is thread 0, and runs the call with preemption
 * off. Only the boot sequence (kernel_main, start_devices,
 * check_boot_modules) and devtest.c call the boot screen directly;
 * scripts/check-bootscreen.sh fails the build otherwise.
 */
static void screen_guard_enter(const char *call)
{
    if (thread_current() != 0)
        panicf("Boot screen used from thread %d '%s' (%s): only thread 0 may.",
               (int)thread_current(), thread_name(thread_current()), call);
    preempt_disable();
}

#define BOOTSCREEN_SAFE(call)          \
    do {                               \
        screen_guard_enter(#call);     \
        call;                          \
        preempt_enable();              \
    } while (0)

static void diag_message(const char *text)
{
    BOOTSCREEN_SAFE(bootscreen_message(text));
}

/*
 * Long-mode proof, read from the CPU itself (serial log): EFER (LME set by
 * the stub, LMA set by the CPU once paging was switched on), CR0, CR4, the
 * code segment's descriptor (L bit), the task register and the GDTR. Plus
 * 64-bit arithmetic on values above 2^32, without libgcc.
 */
static void log_cpu_mode(void)
{
    struct __attribute__((packed)) { uint16_t limit; uint64_t base; } gdtr;
    uint16_t cs, tr;
    __asm__ volatile ("sgdt %0" : "=m"(gdtr));
    __asm__ volatile ("mov %%cs, %0" : "=r"(cs));
    __asm__ volatile ("str %0" : "=r"(tr));
    uint64_t efer = cpu_read_efer(), cr4 = cpu_read_cr4(), desc = gdt_descriptor(cs);
    uint32_t cr0 = cpu_read_cr0();
    unsigned lme = !!(efer & EFER_LME), lma = !!(efer & EFER_LMA), nxe = !!(efer & EFER_NXE);
    unsigned l_bit = (desc >> 53) & 1, d_bit = (desc >> 54) & 1;

    klogf("CPU mode: EFER=0x%016llx (LME=%u LMA=%u NXE=%u) CR0=0x%08x (PE=%u PG=%u WP=%u) "
          "CR4=0x%016llx (PAE=%u)\n", (unsigned long long)efer, lme, lma, nxe, cr0,
          !!(cr0 & CR0_PE), !!(cr0 & CR0_PG), !!(cr0 & CR0_WP), (unsigned long long)cr4,
          !!(cr4 & CR4_PAE));
    klogf("CPU mode: CS=0x%04x descriptor=0x%016llx (L=%u D=%u) TR=0x%04x "
          "GDTR base=0x%016llx limit=0x%04x\n", cs, (unsigned long long)desc, l_bit, d_bit,
          tr, (unsigned long long)gdtr.base, gdtr.limit);

    volatile uint64_t a = 0x123456789ABCDEF0ull, b = 0x1234567ull;
    uint64_t q = a / b, r = a % b;
    bool arith = q == 0x1000000790ull && r == 0x400ull && q * b + r == a;
    bool long_mode = lme && lma && (cr0 & CR0_PG) && (cr4 & CR4_PAE) && l_bit && !d_bit &&
                     sizeof(void *) == 8;
    klogf("CPU mode: %s; 64-bit arithmetic %s\n",
          long_mode ? "64-bit long mode active" : "long mode NOT active",
          arith ? "PASS" : "WRONG");
}

/*
 * 0.10: the user-mode parts of the GDT, IDT and TSS, read back from the
 * tables the CPU is using (sgdt / sidt), so the tests check what is loaded.
 */
static void log_user_segments(void)
{
    struct __attribute__((packed)) { uint16_t limit; uint64_t base; } gdtr;
    __asm__ volatile ("sgdt %0" : "=m"(gdtr));
    klogf("GDT: %u entries (limit 0x%04x); 0x28 placeholder=0x%016llx; "
          "user data 0x30=0x%016llx; user code 0x38=0x%016llx\n",
          (unsigned)(gdtr.limit + 1) / 8, gdtr.limit,
          (unsigned long long)gdt_descriptor(GDT_USER32_PLACEHOLDER),
          (unsigned long long)gdt_descriptor(GDT_USER_DATA),
          (unsigned long long)gdt_descriptor(GDT_USER_CODE));

    char ist1[32], ist2[32];
    size_t n1 = 0, n2 = 0;
    unsigned other = 0;
    ist1[0] = ist2[0] = 0;
    for (int v = 0; v < IDT_HANDLED_VECTORS; v++) {
        int ist = idt_gate_ist(v);
        if (ist == 1 && n1 < sizeof(ist1) - 8)
            n1 += ksnprintf(ist1 + n1, sizeof(ist1) - n1, "%s%d", n1 ? ", " : "", v);
        else if (ist == 2 && n2 < sizeof(ist2) - 8)
            n2 += ksnprintf(ist2 + n2, sizeof(ist2) - n2, "%s%d", n2 ? ", " : "", v);
        else if (ist != 0)
            other++;
    }
    klogf("IDT: IST1 on vector %s; IST2 on vectors %s; %u other vectors on an IST; "
          "the rest on the current stack\n", ist1, ist2, other);
    klogf("TSS: RSP0=0x%016llx (thread 0's stack top), IST1=0x%016llx, IST2=0x%016llx\n",
          (unsigned long long)gdt_rsp0(), (unsigned long long)gdt_ist(1),
          (unsigned long long)gdt_ist(2));
}

/*
 * 0.10: CPU protections for user mode (SMEP, SMAP, CR0.EM) and the freeze of
 * the kernel half, once paging and the thread stacks' area exist, before
 * any process. Serial log only (the boot screen's rows are fixed).
 */
static void user_mode_setup(bool paging_on)
{
    if (!paging_on) {
        klogf("User mode: NOT APPLICABLE (paging is off)\n");
        return;
    }
    struct cpuprot_status cp;
    cpuprot_init(&cp);
    klogf("SMEP: %s\n", cp.smep_on ? "enabled (CR4.SMEP=1): the kernel cannot execute user pages"
                       : cp.smep_supported ? "FAILED (supported, but CR4.SMEP is not set)"
                       : "NOT APPLICABLE (not supported by this CPU)");
    klogf("SMAP: %s\n", cp.smap_on ? "enabled (CR4.SMAP=1): clac on every kernel entry"
                       : cp.smap_supported ? "FAILED (supported, but CR4.SMAP is not set)"
                       : "NOT APPLICABLE (not supported by this CPU)");
    klogf("CR0.EM: %s\n", cp.em_on ? "set: x87, MMX and SSE instructions fault (the kernel uses none)"
                         : "NOT SET");
    klogf("User-mode CPU set-up: CR0=0x%08x CR4=0x%016llx\n", cpu_read_cr0(),
          (unsigned long long)cpu_read_cr4());
    if ((cp.smep_supported && !cp.smep_on) || (cp.smap_supported && !cp.smap_on) || !cp.em_on)
        panic("User-mode CPU set-up failed (details on the serial log).");
    if (!syscall_init())            /* 0.10: SYSCALL entry (EFER.SCE, STAR, LSTAR, SFMASK) */
        panic("System-call set-up failed (details on the serial log).");
    paging_freeze_kernel_half();
}

/* Checks every Multiboot module after start-up (memory, paging and heap are
 * set up by now): a test module (kernel/bootmod.h) must still be intact,
 * which proves nothing the kernel set up was placed on top of it. Serial
 * log only; boot continues either way. Everything is read through the
 * physical view (the module table and the modules are boot data). */
static void check_boot_modules(const struct multiboot_info *mbi)
{
    if (!(mbi->flags & MULTIBOOT_INFO_MODS) || mbi->mods_count == 0) {
        klogf("Boot modules: none\n");
        return;
    }
    const struct multiboot_module *mods = phys_view(mbi->mods_addr);
    for (uint32_t i = 0; i < mbi->mods_count; i++) {
        uint32_t size = mods[i].mod_end - mods[i].mod_start;
        struct bootmod_result r;
        enum bootmod_status s =
            bootmod_check((const uint8_t *)phys_view(mods[i].mod_start), size, &r);
        if (s == BOOTMOD_NO_HEADER)
            klogf("Boot module %u: 0x%08x-0x%08x (%u bytes): %s\n", i + 1, mods[i].mod_start,
                  mods[i].mod_end, size, bootmod_status_str(s));
        else
            klogf("Boot module %u: 0x%08x-0x%08x (%u bytes): %s (FNV-1a 0x%08x, expected 0x%08x)\n",
                  i + 1, mods[i].mod_start, mods[i].mod_end, size, bootmod_status_str(s),
                  r.actual, r.expected);
        /* 0.8: also on the screen's message line, for machines without a
         * serial port (only when a module was loaded). */
        char msg[80];
        ksnprintf(msg, sizeof(msg), "Boot module %u: %s (%u bytes)", i + 1,
                  bootmod_status_str(s), size);
        bootscreen_message(msg);
    }
}

/* Diagnostics: GRUB's "Diagnostics" menu (or QEMU -append) passes test=<name>
 * to deliberately trigger a CPU exception or run a memory test. */
static void run_diagnostics(void)
{
    /* 0.9 thread diagnostics. The longer names come first: cmdline_has
     * matches prefixes, and "test=preempt" (stage 4) is one of these. */
    if (cmdline_has("test=gpflimit")) {           /* before test=gpf: a prefix of it */
        diag_message("Crash test: general protection fault (selector past the GDT's end)");
        /* 0.10: selector 0x40 = GDT entry 8, the first one past the 8-entry
         * GDT (limit 0x3F): #GP(0x40), from the limit check. */
        __asm__ volatile ("mov $0x40, %%ax\n\t"
                          "mov %%ax, %%ds" ::: "eax");
    } else if (cmdline_has("test=freezeviolation")) {
        diag_message("Crash test: new kernel-half PML4 entry after the freeze (must panic)");
        /* 0.10: PML4 slot 257 is empty in the kernel half; creating it after
         * the freeze panics inside paging_map. */
        uint32_t frame = memory_alloc_page();
        paging_map(0xFFFF808000000000ull, frame, PTE_WRITE | paging_nx_flag());
        diag_message("Freeze test: FAIL (the new kernel-half entry was created)");
    } else if (cmdline_has("test=smep")) {
        diag_message("Crash test: the kernel executes a user page (SMEP must refuse it)");
        usertest_smep();                            /* returns only without SMEP */
        diag_message("SMEP test: NOT APPLICABLE (not supported by this CPU)");
    } else if (cmdline_has("test=smap")) {
        diag_message("Crash test: the kernel reads a user page (SMAP must refuse it)");
        usertest_smap();                            /* returns only without SMAP */
        diag_message("SMAP test: NOT APPLICABLE (not supported by this CPU)");
    } else if (cmdline_has("test=rsp0")) {
        diag_message("RSP0 test: 4 threads check the TSS's RSP0 after every switch...");
        diag_message(usertest_rsp0() ? "RSP0 test: PASS" : "RSP0 test: FAIL (see serial log)");
    } else if (cmdline_has("test=uaspace")) {
        diag_message("Address-space test: 500 address spaces built, entered and torn down...");
        diag_message(usertest_uaspace() ? "Address-space test: PASS"
                                        : "Address-space test: FAIL (see serial log)");
    } else if (cmdline_has("test=userpreempt")) {
        diag_message("User preemption test: 5 processes and a frame thread for 3 seconds...");
        diag_message(usertest_userpreempt() ? "User preemption test: PASS"
                                            : "User preemption test: FAIL (see serial log)");
    } else if (cmdline_has("test=userchurn")) {
        diag_message("User churn test: 1000 processes started and ended...");
        diag_message(usertest_userchurn() ? "User churn test: PASS"
                                          : "User churn test: FAIL (see serial log)");
    } else if (cmdline_has("test=userio")) {
        diag_message("User I/O test: write, nanosleep and bad pointers...");
        diag_message(usertest_userio() ? "User I/O test: PASS"
                                       : "User I/O test: FAIL (see serial log)");
    } else if (cmdline_has("test=usersys")) {
        diag_message("User syscall test: 8 programs make system calls...");
        diag_message(usertest_usersys() ? "User syscall test: PASS"
                                        : "User syscall test: FAIL (see serial log)");
    } else if (cmdline_has("test=userkmem")) {
        diag_message("User kernel-memory test: 8 programs reach for the kernel...");
        diag_message(usertest_userkmem() ? "User kernel-memory test: PASS"
                                         : "User kernel-memory test: FAIL (see serial log)");
    } else if (cmdline_has("test=userpriv")) {
        diag_message("User privilege test: 15 programs try what ring 3 may not...");
        diag_message(usertest_userpriv() ? "User privilege test: PASS"
                                         : "User privilege test: FAIL (see serial log)");
    } else if (cmdline_has("test=userfault")) {
        diag_message("User fault test: 5 programs make a mistake; each must end alone...");
        diag_message(usertest_userfault() ? "User fault test: PASS"
                                          : "User fault test: FAIL (see serial log)");
    } else if (cmdline_has("test=userkill")) {
        diag_message("User kill test: 60 spin processes started and killed...");
        diag_message(usertest_userkill() ? "User kill test: PASS"
                                         : "User kill test: FAIL (see serial log)");
    } else if (cmdline_has("test=userspin")) {
        diag_message("User-mode test: the program spin runs in ring 3 and is preempted...");
        diag_message(usertest_userspin() ? "User-mode test: PASS"
                                         : "User-mode test: FAIL (see serial log)");
    } else if (cmdline_has("test=clac")) {
        diag_message("clac test: interrupts taken with RFLAGS.AC set...");
        diag_message(usertest_clac() ? "clac test: PASS" : "clac test: FAIL (see serial log)");
    } else if (cmdline_has("test=reboottriple")) {
        /* 0.9.1, QEMU only: the reboot fallback, without the keyboard
         * controller. QEMU (-no-reboot) exits when the machine resets. */
        diag_message("Restart test: forcing a triple fault (no keyboard controller)");
        reset_machine(false);
    } else if (cmdline_has("test=preemptyield")) {
        diag_message("Thread test: yield with preemption disabled (must panic)");
        schedtest_preempt_yield();
    } else if (cmdline_has("test=preemptjoin")) {
        diag_message("Thread test: join with preemption disabled (must panic)");
        schedtest_preempt_join();
    } else if (cmdline_has("test=preemptsleep")) {
        diag_message("Thread test: sleep with preemption disabled (must panic)");
        schedtest_preempt_sleep();
    } else if (cmdline_has("test=preemptoff")) {
        diag_message("Preemptoff test: switch deferred while preemption is disabled...");
        diag_message(schedtest_preemptoff() ? "Preemptoff test: PASS"
                                            : "Preemptoff test: FAIL (see serial log)");
    } else if (cmdline_has("test=preempt")) {
        diag_message("Preempt test: 3 threads that never yield, for 3 seconds...");
        diag_message(schedtest_preempt() ? "Preempt test: PASS" : "Preempt test: FAIL (see serial log)");
    } else if (cmdline_has("test=sleep")) {
        diag_message("Sleep test: 1, 10 and 100 ticks next to a spinning thread...");
        diag_message(schedtest_sleep() ? "Sleep test: PASS" : "Sleep test: FAIL (see serial log)");
    } else if (cmdline_has("test=heapthreads")) {
        diag_message("Heap threads test: 4 threads using the heap for 2 seconds...");
        diag_message(schedtest_heapthreads() ? "Heap threads test: PASS"
                                             : "Heap threads test: FAIL (see serial log)");
    } else if (cmdline_has("test=framethreads")) {
        diag_message("Frame threads test: 4 threads allocating frames for 2 seconds...");
        diag_message(schedtest_framethreads() ? "Frame threads test: PASS"
                                              : "Frame threads test: FAIL (see serial log)");
    } else if (cmdline_has("test=pingpongpreempt")) {
        diag_message("Ping-pong preemption test: one turn runs past its slice...");
        diag_message(schedtest_pingpong_preempt() ? "Ping-pong preemption test: PASS"
                                                  : "Ping-pong preemption test: FAIL (see serial log)");
    } else if (cmdline_has("test=pingpongbad")) {
        diag_message("Ping-pong order test: a skipped and a repeated turn...");
        diag_message(schedtest_pingpong_bad() ? "Ping-pong order test: PASS"
                                              : "Ping-pong order test: FAIL (see serial log)");
    } else if (cmdline_has("test=threadleak")) {
        diag_message("Thread churn test: 2000 threads created and freed...");
        diag_message(schedtest_threadleak() ? "Thread churn test: PASS"
                                            : "Thread churn test: FAIL (see serial log)");
    } else if (cmdline_has("test=threadoverflow")) {
        diag_message("Crash test: kernel thread stack overflow into its guard");
        schedtest_threadoverflow();
    } else if (cmdline_has("test=threads")) {
        diag_message("Thread test: 8 threads x 100 yields...");
        diag_message(schedtest_threads() ? "Thread test: PASS" : "Thread test: FAIL (see serial log)");
    } else if (cmdline_has("test=join")) {
        diag_message("Join test: exit codes, a blocked joiner, a detached thread...");
        diag_message(schedtest_join() ? "Join test: PASS" : "Join test: FAIL (see serial log)");
    } else if (cmdline_has("test=ifstate")) {
        diag_message("IF-state test: interrupt flag across thread switches...");
        diag_message(schedtest_ifstate() ? "IF-state test: PASS"
                                         : "IF-state test: FAIL (see serial log)");
    } else if (cmdline_has("test=timer")) {
        devtest_timer();
    } else if (cmdline_has("test=keyboard")) {
        devtest_keyboard();                         /* does not return */
    } else if (cmdline_has("test=kbdflood")) {
        devtest_kbd_flood();
    } else if (cmdline_has("test=irqunexpected")) {
        devtest_irq_unexpected();
    } else if (cmdline_has("test=memexhaust")) {
        diag_message("Memory test: allocating every free page...");
        /* Frames other owners hold before the test (the test leaves them
         * alone and must count exactly this many). */
        uint32_t tables = paging_enabled() ? paging_stats()->page_tables : 0;
        uint32_t dirs = paging_enabled() ? 1 : 0;
        uint32_t heap_pages = kheap_stats()->arena_bytes >> 12;
        klogf("Memory exhaustion test: frames in use before the test: %u page tables + %u directory"
              " + %u heap pages = %u\n", tables, dirs, heap_pages, tables + dirs + heap_pages);
        char line[80];
        ksnprintf(line, sizeof(line), "Memory exhaustion test: PASS (%u pages)", memtest_exhaust());
        diag_message(line);
    } else if (cmdline_has("test=doublefree")) {
        diag_message("Memory test: double free");
        memtest_double_free();
    } else if (cmdline_has("test=freereserved")) {
        diag_message("Memory test: free a kernel page");
        memtest_free_reserved();
    } else if (cmdline_has("test=heapdouble")) {
        diag_message("Heap test: double free");
        vmtest_heap_double();
    } else if (cmdline_has("test=heapinvalid")) {
        diag_message("Heap test: free a stack address");
        vmtest_heap_invalid();
    } else if (cmdline_has("test=heapcorrupt")) {
        diag_message("Heap test: corrupted block header");
        vmtest_heap_corrupt();
    } else if (cmdline_has("test=heapexhaust")) {
        diag_message("Heap test: allocating until the heap is full...");
        diag_message(vmtest_heap_exhaust() ? "Heap exhaustion test: PASS"
                                                 : "Heap exhaustion test: FAIL (see serial log)");
    } else if (cmdline_has("test=pfnull")) {
        diag_message("Page-fault test: read from NULL");
        vmtest_pf_null();
    } else if (cmdline_has("test=pfunmapped")) {
        diag_message("Page-fault test: read from unmapped 0x40000000");
        vmtest_pf_unmapped();
    } else if (cmdline_has("test=pfnx")) {
        diag_message("Page-fault test: execute from a no-execute data page");
        vmtest_pf_nx();                             /* returns only without NX */
        diag_message("NX test: NOT APPLICABLE (NX not supported by this CPU)");
    } else if (cmdline_has("test=pflow")) {
        diag_message("Page-fault test: read the kernel's old low address 0x100000");
        vmtest_pf_low();
    } else if (cmdline_has("test=stackoverflow")) {
        diag_message("Crash test: kernel stack overflow into the guard page");
        vmtest_stack_overflow();                    /* returns only without paging */
        diag_message("Stack test: NOT APPLICABLE (paging is off, no guard page)");
    } else if (cmdline_has("test=pfreadonly")) {
        diag_message("Page-fault test: write to read-only kernel code");
        vmtest_pf_readonly();
    } else if (cmdline_has("test=divzero")) {
        diag_message("Crash test: divide by zero");
        __asm__ volatile ("mov $10, %%eax\n\t"
                          "xor %%edx, %%edx\n\t"
                          "xor %%ecx, %%ecx\n\t"
                          "div %%ecx" ::: "eax", "ecx", "edx", "cc");
    } else if (cmdline_has("test=ud")) {
        diag_message("Crash test: invalid opcode");
        __asm__ volatile ("ud2");
    } else if (cmdline_has("test=gpf")) {
        diag_message("Crash test: general protection fault");
        /* Selector 0x28 = GDT entry 5: since 0.10 the all-zero placeholder
         * (gdt_desc.h), inside the GDT's limit but not a data or readable
         * code segment, so the type check raises #GP(0x28) (until 0.10 it
         * was past the end of a 5-entry GDT; test=gpflimit covers that). */
        __asm__ volatile ("mov $0x28, %%ax\n\t"
                          "mov %%ax, %%ds" ::: "eax");
    }
}

/*
 * Enables IRQ0 (timer) and IRQ1 (keyboard). For each line the driver does:
 * register handler -> set up device -> clear pending data -> unmask ->
 * verify mask. A self-test then proves the interrupt is really delivered.
 * A device failure shows FAILED and boot continues; a wrong PIC mask is a
 * kernel bug and panics.
 */
static bool start_devices(void)
{
    char note[80];

    struct timer_selftest ts = { 0 };
    bool timer_started = timer_init();
    bool timer_ok = timer_started && timer_selftest(&ts);
    if (timer_ok)
        ksnprintf(note, sizeof(note), "PIT counting, IRQ0 delivered %u ticks, divisor %u",
                  ts.ticks_seen, ts.divisor);
    else
        ksnprintf(note, sizeof(note), "%s",
                  !timer_started ? "IRQ0 could not be enabled"
                  : !ts.pit_counting ? "PIT is not counting"
                  : "PIT counting but IRQ0 never delivered");
    klogf("Timer self-test: %s (%s)\n", timer_ok ? "PASS" : "FAIL", note);
    bootscreen_status("Timer (PIT, 100 Hz):", timer_ok ? BOOT_OK : BOOT_FAILED, note);

    struct kbd_selftest ks = { 0 };
    enum kbd_status kst = keyboard_init();
    bool kbd_ok = kst == KBD_OK && keyboard_selftest(&ks);
    if (kbd_ok)
        ksnprintf(note, sizeof(note), "controller set up, echo via IRQ1, decoder and queue checked");
    else
        ksnprintf(note, sizeof(note), "%s",
                  kst != KBD_OK ? kbd_status_str(kst)
                  : !ks.irq_delivered ? "IRQ1 never delivered the echo reply"
                  : !ks.decoder_ok ? "scancode decoder self-test failed"
                  : "key queue self-test failed");
    klogf("Keyboard self-test: %s (%s)\n", kbd_ok ? "PASS" : "FAIL", note);
    bootscreen_status("Keyboard (PS/2):", kbd_ok ? BOOT_OK : BOOT_FAILED, note);

    /* Exactly the lines that were enabled, and never anything beyond IRQ0/1. */
    uint16_t enabled = (uint16_t)((timer_started ? 1u << IRQ_TIMER : 0)
                                  | (kst == KBD_OK ? 1u << IRQ_KEYBOARD : 0));
    uint16_t expected = (uint16_t)~enabled;
    uint16_t mask = pic_get_mask();
    klogf("IRQ mask: 0x%04x (expected 0x%04x)\n", mask, expected);
    if (mask != expected || (uint16_t)~mask & (uint16_t)~IRQ_ALLOWED_MASK)
        panicf("PIC mask is 0x%04x, expected 0x%04x: only IRQ0/IRQ1 may be unmasked",
               mask, expected);
    return kbd_ok;
}

/*
 * Idle event loop (not a scheduler): sleep with hlt until any interrupt,
 * then handle what the interrupt handlers queued or counted. All screen and
 * serial output happens here, never in interrupt context.
 *
 * While the boot screen is showing, the first key press opens the console
 * (if offered); that key is not typed. After that, every key event goes to
 * the console, which runs shell commands here, in normal context.
 */
static void __attribute__((noreturn)) idle_loop(const struct memory_stats *mem,
                                                 bool console_offered)
{
    uint32_t shown_seconds = 0xFFFFFFFFu;
    uint32_t reported_dropped = 0, reported_unexpected = 0;

    for (;;) {
        struct key_event e;
        while (keyboard_get_event(&e)) {
            if (key_logging)                /* 0.9.2: off by default */
                devtest_log_key(&e);
            if (console_active()) {
                console_handle_key(&e);
            } else if (e.flags & KEY_PRESSED) {
                BOOTSCREEN_SAFE(bootscreen_last_key(&e));
                if (console_offered)
                    console_start(mem);
            }
        }

        uint32_t dropped = keyboard_dropped();
        if (dropped != reported_dropped) {
            klogf("Keyboard: %u events dropped (queue full)\n", dropped - reported_dropped);
            reported_dropped = dropped;
        }
        uint32_t unexpected = irq_unexpected_count();
        if (unexpected != reported_unexpected) {
            klogf("Unexpected IRQ %u (total %u)\n", irq_last_unexpected(), unexpected);
            reported_unexpected = unexpected;
        }

        uint32_t seconds = timer_uptime_seconds();
        if (seconds != shown_seconds) {
            shown_seconds = seconds;
            if (!console_active())
                BOOTSCREEN_SAFE(bootscreen_uptime(seconds));
            if (seconds && seconds % 5 == 0)
                klogf("uptime: %u s\n", seconds);
        }

        /* 0.9: free exited detached threads, then let another thread run
         * or wait for the next interrupt (hlt, as before 0.9). */
        thread_idle();
    }
}

void kernel_main(uint32_t magic, uint32_t mb_info)
{
    /* First, before anything else uses the stack (interrupts are off). */
    __asm__ volatile ("mov %%rsp, %0" : "=r"(paint_rsp));
    paint_lo = (uint8_t *)stack_bottom;
    paint_len = (size_t)(paint_rsp - STACK_PAINT_MARGIN - (uint64_t)(uintptr_t)stack_bottom);
    stack_paint(paint_lo, paint_len);

    serial_init();
    bool display_ok = vga_init();
    klogf("Kernel stack: 0x%016llx-0x%016llx; painted 0x%016llx-0x%016llx, %u bytes below "
          "RSP 0x%016llx (live frames untouched)\n",
          (unsigned long long)(uintptr_t)stack_bottom, (unsigned long long)(uintptr_t)stack_top,
          (unsigned long long)(uintptr_t)paint_lo,
          (unsigned long long)((uintptr_t)paint_lo + paint_len), STACK_PAINT_MARGIN,
          (unsigned long long)paint_rsp);

    if (magic != MULTIBOOT_BOOTLOADER_MAGIC)
        panic("Not loaded by a Multiboot bootloader.");

    if (mb_info == 0)
        panic("No Multiboot information from the bootloader.");
    /* Boot data is read through the physical view (vm_layout.h), which
     * stays mapped for it once the low identity mapping is gone. Only
     * memory_init() gets the physical pointer itself: reserve.c records
     * the info block's own (physical) address, and it runs before the
     * kernel's page tables are loaded, while the boot tables still map
     * 0-4 GiB at 0. */
    const struct multiboot_info *mbi = phys_view(mb_info);
    const struct multiboot_info *mbi_phys = (const struct multiboot_info *)(uintptr_t)mb_info;
    /* Only the pointer is kept here; memory_init() validates and reserves
     * the string before anything reads it. */
    if (mbi->flags & MULTIBOOT_INFO_CMDLINE)
        cmdline = phys_view(mbi->cmdline);

    bootscreen_draw_header();

    bootscreen_status("GDT loaded:", gdt_init() ? BOOT_OK : BOOT_FAILED,
                      "64-bit code 0x08, data 0x10, TSS 0x18 (IST1, IST2), user data 0x33, code 0x3B");
    bootscreen_status("IDT loaded (256 entries):", idt_init() ? BOOT_OK : BOOT_FAILED, NULL);
    log_cpu_mode();
    log_user_segments();
    bootscreen_status("Interrupt controller:", pic_init() ? BOOT_OK : BOOT_FAILED,
                      "remapped, IRQs masked");

    interrupts_enable();
    __asm__ volatile ("int3");
    bootscreen_status("Exception handling:",
                      interrupts_breakpoint_count() == 1 ? BOOT_OK : BOOT_FAILED,
                      "int3 self-test");

    /* After the IDT, so a fault while reading boot data shows the crash
     * screen instead of resetting the machine. */
    static struct memory_stats mem;
    memory_init(mbi_phys, &mem);
    uint32_t mem_checks = memtest_boot();

    /* Only reached if the map was parsed, usable memory identified, the
     * kernel and boot data reserved, the allocator set up and the self-test
     * passed; any failure on the way ends on the crash screen instead. */
    char note[64];
    ksnprintf(note, sizeof(note), "%u reserved ranges, self-test PASS, %u checks",
              mem.reservations, mem_checks);
    bootscreen_status("Memory initialization:", BOOT_OK, note);
    bootscreen_status("Display initialization:", display_ok ? BOOT_OK : BOOT_FAILED, NULL);
    bootscreen_memory(&mem);

    /* Paging: build, walk-check, enable; before any IRQ is unmasked. A
     * failed check leaves paging off; a failed self-test once it is on is
     * fatal. Then the kernel heap, only with paging on: a failure disables
     * it (kmalloc returns NULL) and boot continues. */
    bool paging_on = paging_init();
    if (paging_on && !paging_selftest())
        panic("Paging self-test failed (details on the serial log).");
    bool heap_ok = paging_on && kheap_init() && kheap_selftest();

    const struct paging_stats *ps = paging_stats();
    char paging_note[112], heap_note[80];
    if (paging_on)
        ksnprintf(paging_note, sizeof(paging_note),
                  "CR0=0x%08x CR3=0x%016llx, %u page tables, %u pages mapped, self-test PASS",
                  cpu_read_cr0(), (unsigned long long)ps->cr3, ps->page_tables, ps->mapped_pages);
    else
        ksnprintf(paging_note, sizeof(paging_note),
                  "kernel page tables not loaded; still on the boot page tables (see above)");
    if (heap_ok)
        ksnprintf(heap_note, sizeof(heap_note), "%u KiB at 0x%016llx, max %u KiB, self-test PASS",
                  kheap_stats()->arena_bytes / 1024, (unsigned long long)VM_HEAP_BASE,
                  VM_HEAP_MAX / 1024);
    else
        ksnprintf(heap_note, sizeof(heap_note), "%s; kmalloc returns NULL",
                  paging_on ? "set-up or self-test failed" : "needs paging, which is off");
    bootscreen_status_pair("Paging & kernel heap:",
                           "Paging enabled:", "paging", paging_on ? BOOT_OK : BOOT_FAILED, paging_note,
                           "Kernel heap:", "heap", heap_ok ? BOOT_OK : BOOT_FAILED, heap_note);

    /* 0.9: kernel_main becomes thread 0 ("kernel"). Other threads get their
     * stacks from slot 507, so they exist only with paging on. */
    thread_init();
    user_mode_setup(paging_on);

    check_boot_modules(mbi);

    bool keyboard_ok = start_devices();
    bootscreen_uptime(timer_uptime_seconds());
    bootscreen_last_key(NULL);

    /* A test of the pure console/shell code only (line editor, parser,
     * shell, text grid). Keyboard and VGA hardware are proven separately,
     * by the keyboard self-test and the QEMU / VirtualBox console tests. */
    bool console_ok = console_selftest();
    bootscreen_status("Console & shell:", console_ok ? BOOT_OK : BOOT_FAILED,
                      "pure self-test of line editor, parser, shell, text grid; not a hardware test");

    /* 0.9: scheduler self-test (serial only; the boot screen's rows are
     * fixed). A failure is fatal, as for the paging self-test. */
    if (paging_on) {
        if (!schedtest_boot())
            panic("Scheduler self-test failed (details on the serial log).");
        /* 0.10: three programs as processes (hello, exitcode, nullptr);
         * deterministic (no timing assumptions), fatal on failure. */
        if (!usertest_boot())
            panic("User-mode self-test failed (details on the serial log).");
    } else {
        klogf("Scheduler self-test: NOT APPLICABLE (paging is off: no thread stacks)\n");
    }

    key_logging_init();                     /* 0.9.2: before any diagnostic reads keys */
    run_diagnostics();

    bool console_offered = console_ok && keyboard_ok;
    bootscreen_ready(console_offered ? "Press any key to open the console."
                     : !keyboard_ok ? "Keyboard unavailable: console disabled."
                     : "Console self-test failed: console disabled.");
    klogf("Kernel stack: %u bytes at peak, of %u\n", kernel_stack_peak(), kernel_stack_size());
    idle_loop(&mem, console_offered);
}
