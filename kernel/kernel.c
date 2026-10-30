/*
 * Madire OS kernel entry.
 *
 * Runs in 32-bit protected mode, entered from boot/boot.asm. Brings up the
 * display, replaces the boot loader's CPU tables with our own (GDT, IDT), makes the
 * interrupt controller safe, proves exception handling works, sets up and
 * tests physical memory, enables the timer (IRQ0) and keyboard (IRQ1),
 * self-tests the console, then runs the idle loop, which opens the console
 * and shell on the first key press.
 */
#include <stdbool.h>
#include <stddef.h>
#include "drivers/keyboard/ps2kbd.h"
#include "drivers/mouse/ps2mouse.h"
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
#include "kernel/display/desktop.h"
#include "kernel/display/display.h"
#include "kernel/display/fbcon.h"
#include "kernel/display/pointer.h"
#include "kernel/display/splash.h"
#include "kernel/fs/dirtest.h"
#include "kernel/fs/fsiotest.h"
#include "kernel/fs/fstest.h"
#include "kernel/fs/vfs.h"
#include "kernel/proc/fd.h"
#include "kernel/proc/fdtest.h"
#include "kernel/proc/klock.h"
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
#include "kernel/proc/diagwait.h"
#include "kernel/proc/preempt.h"
#include "kernel/proc/progmod.h"
#include "kernel/proc/schedtest.h"
#include "kernel/proc/starttest.h"
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
 * 1.3 stage 4a: the boot loader's name (Multiboot's boot_loader_name,
 * flags bit 9), at most 63 bytes, every byte outside printable ASCII
 * shown as '?'; "-" if the loader gave none. For the About window only.
 */
#define MB_INFO_BOOT_LOADER_NAME (1u << 9)
static char loader_name[64] = "-";

static void copy_loader_name(const struct multiboot_info *mbi)
{
    if (!(mbi->flags & MB_INFO_BOOT_LOADER_NAME) || !mbi->boot_loader_name)
        return;
    const char *src = phys_view(mbi->boot_loader_name);
    size_t n = 0;
    for (; n < sizeof(loader_name) - 1 && src[n]; n++)
        loader_name[n] = src[n] >= 0x20 && src[n] < 0x7F ? src[n] : '?';
    loader_name[n] = 0;
    if (!n) {
        loader_name[0] = '-';
        loader_name[1] = 0;
    }
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
 * 0.12 stage 3: the value of a whole word "<name>N" (name with its "=",
 * N decimal, 0 to 4294967295; for example "seed=42"). False if no such
 * word, or if N is empty, not all digits or too large.
 */
static bool cmdline_u32(const char *name, uint32_t *out)
{
    const char *p = cmdline;
    while (*p) {
        while (*p == ' ' || *p == '\t')
            p++;
        const char *start = p;
        while (*p && *p != ' ' && *p != '\t')
            p++;
        size_t len = (size_t)(p - start), i = 0;
        while (i < len && name[i] && start[i] == name[i])
            i++;
        if (name[i] || i == len)
            continue;
        uint64_t v = 0;
        for (; i < len; i++) {
            if (start[i] < '0' || start[i] > '9')
                return false;
            v = v * 10 + (uint64_t)(start[i] - '0');
            if (v > 0xFFFFFFFFull)
                return false;
        }
        *out = (uint32_t)v;
        return true;
    }
    return false;
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
        /* 0.11: anything that is not a test module is a program candidate:
         * checked and named by progmod (serial and dmesg only, never the
         * screen, never a "Boot module" line). */
        if (s == BOOTMOD_NO_HEADER) {
            progmod_register(i + 1, mods[i].string ? phys_view(mods[i].string) : NULL,
                             mods[i].mod_start, (const uint8_t *)phys_view(mods[i].mod_start),
                             size);
            continue;
        }
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
    progmod_summary();
}

/* Diagnostics: the boot menu's "Diagnostics" directory (or QEMU -append) passes test=<name>
 * to deliberately trigger a CPU exception or run a memory test. */
static void run_diagnostics(void)
{
    /* 1.0 stage 7b: reserved VFS-lock turns (test code only) are allowed
     * while a test= diagnostic runs; never on a normal boot. */
    if (cmdline_has("test="))
        klock_test_allow();
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
    } else if (cmdline_has("test=elfrun")) {
        diag_message("ELF run test: module programs against their built-in twins...");
        diag_message(usertest_elfrun() ? "ELF run test: PASS" : "ELF run test: FAIL (see serial log)");
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
    } else if (cmdline_has("test=waitstall")) {
        diag_message("Wait-bound test: bounded waits with the timer stopped...");
        diag_message(schedtest_waitstall() ? "Wait-bound test: PASS"
                                           : "Wait-bound test: FAIL (see serial log)");
    } else if (cmdline_has("test=killrace")) {
        diag_message("Kill race test: 400 processes killed at 8 points of their start...");
        uint32_t seed = 0;
        bool seeded = cmdline_u32("seed=", &seed);  /* 0.12 stage 3 */
        diag_message(usertest_killrace(seeded, seed) ? "Kill race test: PASS"
                                                     : "Kill race test: FAIL (see serial log)");
    } else if (cmdline_has("test=selfcheckcopylock")) {   /* 1.0 stage 4 */
        diag_message("Self-check test: a user copy under the VFS lock (must panic)");
        fdtest_selfcheck_copylock();
    } else if (cmdline_has("test=selfcheckyieldlock")) {  /* 1.0 stage 7b */
        diag_message("Self-check test: a yield under the VFS lock (must panic)");
        fdtest_selfcheck_yieldlock();
    } else if (cmdline_has("test=selfcheckcr3")) {
        /* 0.12 stage 2b: each self-check test must panic, naming the state.
         * cmdline_has matches substrings: ring3cr3 before ring3. */
        diag_message("Self-check test: loading a destroyed address space (must panic)");
        usertest_selfcheck_cr3();
    } else if (cmdline_has("test=selfcheckswitch")) {
        diag_message("Self-check test: switching to a destroyed address space (must panic)");
        usertest_selfcheck_switch();
    } else if (cmdline_has("test=selfcheckpick")) {
        diag_message("Self-check test: running a thread that is not ready (must panic)");
        usertest_selfcheck_pick();
    } else if (cmdline_has("test=selfcheckring3cr3")) {
        diag_message("Self-check test: ring 3 without a live address space (must panic)");
        usertest_selfcheck_ring3cr3();
    } else if (cmdline_has("test=selfcheckring3")) {
        diag_message("Self-check test: ring 3 after the process ended (must panic)");
        usertest_selfcheck_ring3();
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
    } else if (cmdline_has("test=preemptlong")) {
        diag_message("Preemptlong test: 4 threads inside guarded sections for 3 seconds...");
        diag_message(schedtest_preemptlong() ? "Preemptlong test: PASS"
                                             : "Preemptlong test: FAIL (see serial log)");
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
    } else if (cmdline_has("test=splashpanic")) {                               /* 1.2 stage 8 */
        klogf("Splash fault test: a panic while the splash is shown\n");
        panic("Splash test: a panic while the splash is shown (on purpose).");
    } else if (cmdline_has("test=splashfail")) {                                /* 1.2 stage 8 */
        klogf("Splash fault test: the Console & shell line was reported FAILED on purpose (above)\n");
    } else if (cmdline_has("test=splash")) {                                    /* 1.2 stage 8 */
        splash_test();
    } else if (cmdline_has("test=fbpattern") || cmdline_has("test=fbfail")) {   /* 1.2 stage 5b */
        display_fbpattern();
    } else if (cmdline_has("test=fbcon")) {                                     /* 1.2 stage 6 */
        fbcon_test();
    } else if (cmdline_has("test=mousefail")) {                                 /* 1.2 stage 7 */
        klogf("Mouse fault test: start-up step %u failed on purpose (the Mouse: lines above); "
              "the keyboard and the console go on\n", mouse_fail_step());
    } else if (cmdline_has("test=mouse")) {                                     /* 1.2 stage 7 */
        mouse_test();
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
    } else if (cmdline_has("test=fslimits")) {    /* 1.0 stage 2 */
        diag_message("Filesystem limits test: each limit hit once...");
        diag_message(fstest_limits() ? "Filesystem limits test: done"
                                     : "Filesystem limits test: FAIL (see serial log)");
    } else if (cmdline_has("test=fsfill")) {      /* 1.0 stage 2 */
        diag_message("Filesystem fill test: files filled to the budget...");
        diag_message(fstest_fill() ? "Filesystem fill test: done"
                                   : "Filesystem fill test: FAIL (see serial log)");
    } else if (cmdline_has("test=klock")) {       /* 1.0 stage 3 */
        diag_message("Filesystem lock test: 6 threads and thread 0 for 3000 ticks...");
        diag_message(fstest_klock() ? "Filesystem lock test: done"
                                    : "Filesystem lock test: FAIL (see serial log)");
    } else if (cmdline_has("test=fdsys")) {       /* 1.0 stage 4 */
        diag_message("Descriptor test: read, write, close, dup, dup2 from fdops...");
        diag_message(fdtest_sys() ? "Descriptor test: done"
                                  : "Descriptor test: FAIL (see serial log)");
    } else if (cmdline_has("test=fdkill")) {      /* 1.0 stage 4 */
        diag_message("Descriptor kill test: programs ending with descriptors open...");
        diag_message(fdtest_kill() ? "Descriptor kill test: done"
                                   : "Descriptor kill test: FAIL (see serial log)");
    } else if (cmdline_has("test=fileio")) {      /* 1.0 stage 5 */
        diag_message("File test: open, stat, lseek, ftruncate from fileio and fileerr...");
        diag_message(fsiotest_fileio() ? "File test: done" : "File test: FAIL (see serial log)");
    } else if (cmdline_has("test=bigio")) {       /* 1.0 stage 5 */
        diag_message("Big I/O test: large reads and writes from bigio...");
        diag_message(fsiotest_bigio() ? "Big I/O test: done"
                                      : "Big I/O test: FAIL (see serial log)");
    } else if (cmdline_has("test=filecontend")) { /* 1.0 stage 5 */
        diag_message("File contention test: the busy mark and the VFS lock under load...");
        diag_message(fsiotest_contend() ? "File contention test: done"
                                        : "File contention test: FAIL (see serial log)");
    } else if (cmdline_has("test=dirops")) {      /* 1.0 stage 6 */
        diag_message("Dir test: getcwd, chdir, mkdir, rmdir, unlink, rename, getdents64...");
        diag_message(dirtest_dirops() ? "Dir test: done" : "Dir test: FAIL (see serial log)");
    } else if (cmdline_has("test=fsunlink")) {    /* 1.0 stage 6 */
        diag_message("Unlink test: open files and folders unlinked, renamed over, removed...");
        diag_message(dirtest_fsunlink() ? "Unlink test: done"
                                        : "Unlink test: FAIL (see serial log)");
    } else if (cmdline_has("test=startup")) {     /* 1.0 stage 7 */
        diag_message("Startup test: argc, argv, envp and auxv from args...");
        diag_message(starttest_startup() ? "Startup test: done"
                                         : "Startup test: FAIL (see serial log)");
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
 * 1.1: one line per light setting, on the serial port and in dmesg (never
 * on the screen): the state as acknowledged, or the failure, after which
 * the driver leaves the lights off until restart.
 */
static void log_lights(const struct kbd_lights *l)
{
    if (l->ok) {
        klogf("Keyboard lights: Num Lock %s, Caps Lock %s, Scroll Lock %s (acknowledged)\n",
              (l->state & 0x02) ? "on" : "off", (l->state & 0x04) ? "on" : "off",
              (l->state & 0x01) ? "on" : "off");
        return;
    }
    char why[24];
    if (l->timed_out)
        ksnprintf(why, sizeof(why), "no reply");
    else
        ksnprintf(why, sizeof(why), "reply 0x%02x", l->reply);
    klogf("Keyboard lights: not acknowledged (%s: %s); lights off until restart\n",
          l->failed_byte == 0xED ? "0xED" : "state byte", why);
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
    splash_step(!timer_ok);
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
    const struct kbd_lights *lights = keyboard_lights();
    if (lights)                                 /* 1.1: always logged (K8) */
        log_lights(lights);
    bootscreen_status("Keyboard (PS/2):", kbd_ok ? BOOT_OK : BOOT_FAILED, note);
    splash_step(!kbd_ok);

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
    /* 1.3 stage 8 (D1): the default path's desktop is decided at the first
     * pass, after the keys queued by then: a first key press that is Esc
     * keeps 1.2's path (the splash closes below, nothing opens); any other
     * starts the desktop, which then takes that key (desktop.h). */
    bool desk_undecided = true;

    for (;;) {
        struct key_event e;
        while (keyboard_get_event(&e)) {
            if (key_logging)                /* 0.9.2: off by default */
                devtest_log_key(&e);
            if (desk_undecided && (e.flags & KEY_PRESSED)) {
                desk_undecided = false;
                if (e.keycode != KEY_ESC)
                    desktop_start_default(console_offered);
            }
            if (desktop_showing()) {
                /* 1.3 stage 4b: on a test=desktop boot the desktop routes
                 * every key (desktop.h): with no window focused a key
                 * opens the Console window and is not typed (the first
                 * one starts the console); with the Console window
                 * focused the console takes it; with the About window
                 * focused Esc closes it. 1.3 stage 5: the menus, the
                 * Restart dialog and the Windows key read it first. */
                enum desktop_key_route r = desktop_key(e.keycode, e.flags & KEY_PRESSED);
                if (r == DESKTOP_KEY_OPENED && !console_active()) {
                    BOOTSCREEN_SAFE(bootscreen_last_key(&e));
                    console_start(mem);
                } else if (r == DESKTOP_KEY_CONSOLE) {
                    console_handle_key(&e);
                }
                continue;
            }
            if (console_active()) {
                console_handle_key(&e);
            } else if (e.flags & KEY_PRESSED) {
                BOOTSCREEN_SAFE(bootscreen_last_key(&e));
                /* 1.2 stage 8 (decision 17): a key closes the splash; Esc
                 * shows the text boot screen and opens nothing. */
                bool esc = splash_showing() && e.keycode == KEY_ESC;
                if (splash_showing())
                    splash_close_key(esc);
                if (console_offered && !esc)
                    console_start(mem);
            }
        }
        if (desk_undecided) {               /* 1.3 stage 8: no key press queued */
            desk_undecided = false;
            desktop_start_default(console_offered);
        }

        splash_tick();                      /* 1.2 stage 8: the dots, every 400 ms */
        pointer_poll();                     /* 1.2 stage 7: nothing without a mouse */
        /* 1.3 stage 5: a menu row chosen with the pointer opened the Console
         * window: start the console, as a key that opens it does. */
        if (desktop_take_console() && !console_active())
            console_start(mem);
        desktop_poll();                     /* 1.3 stage 3: the clock, then one composition */

        /* 1.1: the lights follow the lock keys; a change is logged only
         * with keylog (K8), a failure always. */
        struct kbd_lights lights;
        enum kbd_sync sync = keyboard_lights_sync(&lights);
        if (sync == KBD_SYNC_FAILED || (sync == KBD_SYNC_DONE && key_logging))
            log_lights(&lights);

        console_poll();                     /* 0.11: programs that have ended */

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
            if (seconds && seconds % 5 == 0)    /* 0.12: serial only, not in dmesg */
                kserialf("uptime: %u s\n", seconds);
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
    /* 1.2: what the loader gave for the display (the "Display:" line);
     * before memory_init(), see kernel/display/display.h. */
    display_probe(mb_info);
    /* 1.2 stage 8: the splash (decision 17) and its boot options: not with
     * `verbose`, nor with a test= option other than the splash's own; its
     * lines are logged only with `still` or those. Every status line below
     * is a boot step for it (splash_step). */
    bool splash_opt = cmdline_has("test=splash");
    splash_options(cmdline_word("verbose"), cmdline_word("still"), !cmdline_has("test=") || splash_opt,
                   cmdline_word("still") || splash_opt);
    /* 1.3 stage 3: the desktop, on test=desktop boots only (desktop.h).
     * Stage 4a: desktopnosurface and desktopabout too; stage 4b: desktoppanic.
     * Stage 8: also on the default path (no test= option; desktop.h). */
    desktop_options(cmdline_has("test=desktop"), !cmdline_has("test="), cmdline_word("desktoplog"),
                    cmdline_word("desktopnocache"), cmdline_word("desktopnosurface"), cmdline_word("desktopabout"),
                    cmdline_word("desktoppanic"));                                  /* 1.3 stage 4b */

    bootscreen_draw_header();

    bool ok = gdt_init();
    bootscreen_status("GDT loaded:", ok ? BOOT_OK : BOOT_FAILED,
                      "64-bit code 0x08, data 0x10, TSS 0x18 (IST1, IST2), user data 0x33, code 0x3B");
    splash_step(!ok);
    ok = idt_init();
    bootscreen_status("IDT loaded (256 entries):", ok ? BOOT_OK : BOOT_FAILED, NULL);
    splash_step(!ok);
    log_cpu_mode();
    log_user_segments();
    ok = pic_init();
    bootscreen_status("Interrupt controller:", ok ? BOOT_OK : BOOT_FAILED,
                      "remapped, IRQs masked");
    splash_step(!ok);

    interrupts_enable();
    __asm__ volatile ("int3");
    ok = interrupts_breakpoint_count() == 1;
    bootscreen_status("Exception handling:", ok ? BOOT_OK : BOOT_FAILED, "int3 self-test");
    splash_step(!ok);

    /* After the IDT, so a fault while reading boot data shows the crash
     * screen instead of resetting the machine. */
    static struct memory_stats mem;
    /* 1.2 stage 10: test=earlypanic (a test option): a panic before
     * paging and the framebuffer console, for the early crash screen. */
    if (cmdline_has("test=earlypanic")) {
        klogf("Early panic test: a panic before paging and the framebuffer console\n");
        panic("Early panic test: a panic before the framebuffer console (on purpose).");
    }
    /* 1.3 stage 4a: the boot loader's name for the About window, copied
     * now: memory_init() does not reserve it, and only the boot tables map
     * it (after the IDT, as the other boot data). */
    copy_loader_name(mbi);
    memory_init(mbi_phys, &mem);
    desktop_about_info(loader_name, memory_bytes_to_mb(mem.usable_bytes));     /* 1.3 stage 4a */
    uint32_t mem_checks = memtest_boot();

    /* Only reached if the map was parsed, usable memory identified, the
     * kernel and boot data reserved, the allocator set up and the self-test
     * passed; any failure on the way ends on the crash screen instead. */
    char note[64];
    ksnprintf(note, sizeof(note), "%u reserved ranges, self-test PASS, %u checks",
              mem.reservations, mem_checks);
    bootscreen_status("Memory initialization:", BOOT_OK, note);
    splash_step(false);

    /* 1.2 stage 5b: slot 506 for a usable framebuffer, planned now that
     * the allocator knows the free pages; paging_init() builds it. */
    display_plan_slot(mbi);
    uint32_t fbfail;
    if (cmdline_has("test=fbfail") && cmdline_u32("test=fbfail=", &fbfail))
        display_test_fail_at(fbfail);           /* a test option: the undo path */
    /* 1.2 stage 6: on a framebuffer boot VGA memory does not read back
     * (the screen is in a graphics mode); the display works if slot 506 is
     * planned, for the framebuffer console. */
    ok = display_ok || display_slot_plan();
    bootscreen_status("Display initialization:", ok ? BOOT_OK : BOOT_FAILED, NULL);
    splash_step(!ok);
    bootscreen_memory(&mem);

    /* Paging: build, walk-check, enable; before any IRQ is unmasked. A
     * failed check leaves paging off; a failed self-test once it is on is
     * fatal. Then the kernel heap, only with paging on: a failure disables
     * it (kmalloc returns NULL) and boot continues. */
    bool paging_on = paging_init();
    if (paging_on && !paging_selftest())
        panic("Paging self-test failed (details on the serial log).");
    bool heap_ok = paging_on && kheap_init() && kheap_selftest();
    /* 1.2 stage 6: the console on the framebuffer, once slot 506 is live
     * (test=fbpattern keeps the framebuffer to itself, as in stage 5b).
     * 1.2 stage 8: held behind the splash, which starts from the screen
     * Limine handed over (black), a cut. */
    if (display_active()) {
        if (cmdline_has("test=fbpattern")) {
            klogf("Display: console kept in VGA text memory (test=fbpattern draws on the framebuffer)\n");
        } else {
            bool splash = splash_wanted();
            fbcon_start(splash);
            if (splash)
                splash_start();
        }
    }

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
    splash_step(!paging_on || !heap_ok);

    /* 0.9: kernel_main becomes thread 0 ("kernel"). Other threads get their
     * stacks from slot 507, so they exist only with paging on. */
    thread_init();
    user_mode_setup(paging_on);

    check_boot_modules(mbi);

    bool keyboard_ok = start_devices();
    /* 1.2 stage 7: the mouse only on framebuffer boots and for test=mouse
     * (decision 10), after the keyboard and its IRQ mask line; a missing
     * or failing aux port is one log line, and the boot goes on. The
     * substring "test=mouse" also matches test=mousefail=N (a test
     * option): step N of the start fails, so the give-up path runs. */
    if (display_active() || cmdline_has("test=mouse")) {
        uint32_t mousefail;
        if (cmdline_has("test=mousefail") && cmdline_u32("test=mousefail=", &mousefail))
            mouse_fail_at(mousefail);
        mouse_init();
        /* 1.2 stage 10: no pointer on the console but with the test
         * option `pointer` or test=mouse. */
        pointer_start(cmdline_word("mouselog") || cmdline_has("test=mouse"),
                      cmdline_word("pointer") || cmdline_has("test=mouse") ||
                      cmdline_has("test=desktop"));     /* 1.3 stage 3: on the desktop */
    }
    bootscreen_uptime(timer_uptime_seconds());
    bootscreen_last_key(NULL);

    /* A test of the pure console/shell code only (line editor, parser,
     * shell, text grid). Keyboard and VGA hardware are proven separately,
     * by the keyboard self-test and the QEMU / VirtualBox console tests. */
    bool console_ok = console_selftest();
    /* 1.2 stage 8: test=splashfail (a test option) reports this line
     * FAILED on purpose, so a FAILED status closes the splash; the
     * console is then disabled, as for a real failure. */
    bool forced = cmdline_has("test=splashfail");
    if (forced)
        klogf("Console & shell self-test: %s; reported FAILED on purpose (test=splashfail)\n",
              console_ok ? "PASS" : "FAIL");
    console_ok = console_ok && !forced;
    bootscreen_status("Console & shell:", console_ok ? BOOT_OK : BOOT_FAILED,
                      "pure self-test of line editor, parser, shell, text grid; not a hardware test");
    splash_step(!console_ok);

    /* 0.12 stage 3: the stall bound of the diagnostic waits, measured while
     * the timer works and thread 0 is alone (before the self-test's threads). */
    diag_wait_calibrate();

    /* 0.9: scheduler self-test (serial only; the boot screen's rows are
     * fixed). A failure is fatal, as for the paging self-test. */
    if (paging_on) {
        if (!schedtest_boot())
            panic("Scheduler self-test failed (details on the serial log).");
        splash_tick();                      /* 1.2 stage 8: the dots, between the long steps */
        /* 0.10: three programs as processes (hello, exitcode, nullptr);
         * deterministic (no timing assumptions), fatal on failure. */
        if (!usertest_boot())
            panic("User-mode self-test failed (details on the serial log).");
        splash_tick();
    } else {
        klogf("Scheduler self-test: NOT APPLICABLE (paging is off: no thread stacks)\n");
    }

    /* 1.0: the live RAM filesystem (/, /home, /tmp; node records in the
     * heap) and its self-test, before any diagnostic that may use it. A
     * failure is fatal, as for the other boot self-tests. */
    if (heap_ok) {
        if (!vfs_boot())
            panic("Filesystem self-test failed (details on the serial log).");
    } else {
        klogf("Filesystem: NOT AVAILABLE (the kernel heap is off)\n");
    }
    splash_tick();

    key_logging_init();                     /* 0.9.2: before any diagnostic reads keys */
    run_diagnostics();
    /* 1.0 stage 4: every process so far (the boot self-test's, a
     * diagnostic's) has closed its descriptors. */
    klogf("Descriptors: %u per process, %u open files system-wide; %u open files now\n",
          FD_MAX, FILE_MAX, fd_open_files());

    bool console_offered = console_ok && keyboard_ok;
    splash_ready(console_offered);          /* 1.2 stage 10: drawn before "System ready." is logged */
    bootscreen_ready(console_offered ? "Press any key to open the console."
                     : !keyboard_ok ? "Keyboard unavailable: console disabled."
                     : "Console self-test failed: console disabled.");
    klogf("Kernel stack: %u bytes at peak, of %u\n", kernel_stack_peak(), kernel_stack_size());
    desktop_start(console_offered);         /* 1.3 stage 3: test=desktop boots only */
    idle_loop(&mem, console_offered);
}
