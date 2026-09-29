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
#include "kernel/arch/x86/gdt.h"
#include "kernel/arch/x86/idt.h"
#include "kernel/arch/x86/interrupts.h"
#include "kernel/arch/x86/irq.h"
#include "kernel/arch/x86/pic.h"
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
#include "kernel/memory/vm_layout.h"
#include "kernel/memory/vmtest.h"
#include "kernel/multiboot.h"
#include "kernel/panic.h"
#include "kernel/timer.h"

/* The only IRQ lines 0.4 may unmask. */
#define IRQ_ALLOWED_MASK ((1u << IRQ_TIMER) | (1u << IRQ_KEYBOARD))

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

/* Checks every Multiboot module after start-up (memory, paging and heap are
 * set up by now): a test module (kernel/bootmod.h) must still be intact,
 * which proves nothing the kernel set up was placed on top of it. Serial
 * log only; boot continues either way. */
static void check_boot_modules(const struct multiboot_info *mbi)
{
    if (!(mbi->flags & MULTIBOOT_INFO_MODS) || mbi->mods_count == 0) {
        klogf("Boot modules: none\n");
        return;
    }
    const struct multiboot_module *mods =
        (const struct multiboot_module *)(uintptr_t)mbi->mods_addr;
    for (uint32_t i = 0; i < mbi->mods_count; i++) {
        uint32_t size = mods[i].mod_end - mods[i].mod_start;
        struct bootmod_result r;
        enum bootmod_status s =
            bootmod_check((const uint8_t *)(uintptr_t)mods[i].mod_start, size, &r);
        if (s == BOOTMOD_NO_HEADER)
            klogf("Boot module %u: 0x%08x-0x%08x (%u bytes): %s\n", i + 1, mods[i].mod_start,
                  mods[i].mod_end, size, bootmod_status_str(s));
        else
            klogf("Boot module %u: 0x%08x-0x%08x (%u bytes): %s (FNV-1a 0x%08x, expected 0x%08x)\n",
                  i + 1, mods[i].mod_start, mods[i].mod_end, size, bootmod_status_str(s),
                  r.actual, r.expected);
    }
}

/* Diagnostics: GRUB's "Diagnostics" menu (or QEMU -append) passes test=<name>
 * to deliberately trigger a CPU exception or run a memory test. */
static void run_diagnostics(void)
{
    if (cmdline_has("test=timer")) {
        devtest_timer();
    } else if (cmdline_has("test=keyboard")) {
        devtest_keyboard();                         /* does not return */
    } else if (cmdline_has("test=kbdflood")) {
        devtest_kbd_flood();
    } else if (cmdline_has("test=irqunexpected")) {
        devtest_irq_unexpected();
    } else if (cmdline_has("test=memexhaust")) {
        bootscreen_message("Memory test: allocating every free page...");
        /* Frames other owners hold before the test (the test leaves them
         * alone and must count exactly this many). */
        uint32_t tables = paging_enabled() ? paging_stats()->page_tables : 0;
        uint32_t dirs = paging_enabled() ? 1 : 0;
        uint32_t heap_pages = kheap_stats()->arena_bytes >> 12;
        klogf("Memory exhaustion test: frames in use before the test: %u page tables + %u directory"
              " + %u heap pages = %u\n", tables, dirs, heap_pages, tables + dirs + heap_pages);
        char line[80];
        ksnprintf(line, sizeof(line), "Memory exhaustion test: PASS (%u pages)", memtest_exhaust());
        bootscreen_message(line);
    } else if (cmdline_has("test=doublefree")) {
        bootscreen_message("Memory test: double free");
        memtest_double_free();
    } else if (cmdline_has("test=freereserved")) {
        bootscreen_message("Memory test: free a kernel page");
        memtest_free_reserved();
    } else if (cmdline_has("test=heapdouble")) {
        bootscreen_message("Heap test: double free");
        vmtest_heap_double();
    } else if (cmdline_has("test=heapinvalid")) {
        bootscreen_message("Heap test: free a stack address");
        vmtest_heap_invalid();
    } else if (cmdline_has("test=heapcorrupt")) {
        bootscreen_message("Heap test: corrupted block header");
        vmtest_heap_corrupt();
    } else if (cmdline_has("test=heapexhaust")) {
        bootscreen_message("Heap test: allocating until the heap is full...");
        bootscreen_message(vmtest_heap_exhaust() ? "Heap exhaustion test: PASS"
                                                 : "Heap exhaustion test: FAIL (see serial log)");
    } else if (cmdline_has("test=pfnull")) {
        bootscreen_message("Page-fault test: read from NULL");
        vmtest_pf_null();
    } else if (cmdline_has("test=pfunmapped")) {
        bootscreen_message("Page-fault test: read from unmapped 0x40000000");
        vmtest_pf_unmapped();
    } else if (cmdline_has("test=pfnx")) {
        bootscreen_message("Page-fault test: execute from a no-execute data page");
        vmtest_pf_nx();                             /* returns only without NX */
        bootscreen_message("NX test: NOT APPLICABLE (NX not supported by this CPU)");
    } else if (cmdline_has("test=stackoverflow")) {
        bootscreen_message("Crash test: kernel stack overflow into the guard page");
        vmtest_stack_overflow();                    /* returns only without paging */
        bootscreen_message("Stack test: NOT APPLICABLE (paging is off, no guard page)");
    } else if (cmdline_has("test=pfreadonly")) {
        bootscreen_message("Page-fault test: write to read-only kernel code");
        vmtest_pf_readonly();
    } else if (cmdline_has("test=divzero")) {
        bootscreen_message("Crash test: divide by zero");
        __asm__ volatile ("mov $10, %%eax\n\t"
                          "xor %%edx, %%edx\n\t"
                          "xor %%ecx, %%ecx\n\t"
                          "div %%ecx" ::: "eax", "ecx", "edx", "cc");
    } else if (cmdline_has("test=ud")) {
        bootscreen_message("Crash test: invalid opcode");
        __asm__ volatile ("ud2");
    } else if (cmdline_has("test=gpf")) {
        bootscreen_message("Crash test: general protection fault");
        /* Selector 0x28 = GDT entry 5, beyond the end of our 3-entry GDT. */
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
            devtest_log_key(&e);
            if (console_active()) {
                console_handle_key(&e);
            } else if (e.flags & KEY_PRESSED) {
                bootscreen_last_key(&e);
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
                bootscreen_uptime(seconds);
            if (seconds && seconds % 5 == 0)
                klogf("uptime: %u s\n", seconds);
        }

        __asm__ volatile ("hlt");
    }
}

void kernel_main(uint32_t magic, uint32_t mb_info)
{
    serial_init();
    bool display_ok = vga_init();

    if (magic != MULTIBOOT_BOOTLOADER_MAGIC)
        panic("Not loaded by a Multiboot bootloader.");

    if (mb_info == 0)
        panic("No Multiboot information from the bootloader.");
    const struct multiboot_info *mbi = (const struct multiboot_info *)(uintptr_t)mb_info;
    /* Only the pointer is kept here; memory_init() validates and reserves
     * the string before anything reads it. */
    if (mbi->flags & MULTIBOOT_INFO_CMDLINE)
        cmdline = (const char *)(uintptr_t)mbi->cmdline;

    bootscreen_draw_header();

    bootscreen_status("GDT loaded:", gdt_init() ? BOOT_OK : BOOT_FAILED,
                      "64-bit code 0x08, data 0x10, TSS 0x18 (IST1 double-fault stack)");
    bootscreen_status("IDT loaded (256 entries):", idt_init() ? BOOT_OK : BOOT_FAILED, NULL);
    log_cpu_mode();
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
    memory_init(mbi, &mem);
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

    run_diagnostics();

    bool console_offered = console_ok && keyboard_ok;
    bootscreen_ready(console_offered ? "Press any key to open the console."
                     : !keyboard_ok ? "Keyboard unavailable: console disabled."
                     : "Console self-test failed: console disabled.");
    idle_loop(&mem, console_offered);
}
