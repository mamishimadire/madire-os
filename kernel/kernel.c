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
#include "kernel/arch/x86/gdt.h"
#include "kernel/arch/x86/idt.h"
#include "kernel/arch/x86/interrupts.h"
#include "kernel/arch/x86/irq.h"
#include "kernel/arch/x86/pic.h"
#include "kernel/bootscreen.h"
#include "kernel/console/console.h"
#include "kernel/devtest.h"
#include "kernel/kernel.h"
#include "kernel/kprintf.h"
#include "kernel/memory/memory.h"
#include "kernel/memory/memtest.h"
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
        char line[80];
        ksnprintf(line, sizeof(line), "Memory exhaustion test: PASS (%u pages)", memtest_exhaust());
        bootscreen_message(line);
    } else if (cmdline_has("test=doublefree")) {
        bootscreen_message("Memory test: double free");
        memtest_double_free();
    } else if (cmdline_has("test=freereserved")) {
        bootscreen_message("Memory test: free a kernel page");
        memtest_free_reserved();
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

    bootscreen_status("GDT loaded:", gdt_init() ? BOOT_OK : BOOT_FAILED, NULL);
    bootscreen_status("IDT loaded (256 entries):", idt_init() ? BOOT_OK : BOOT_FAILED, NULL);
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
