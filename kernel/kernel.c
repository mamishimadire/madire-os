/*
 * Madire OS kernel entry - milestone 0.2
 *
 * Runs in 32-bit protected mode, entered from boot/boot.asm. Brings up the
 * display, replaces GRUB's CPU tables with our own (GDT, IDT), makes the
 * interrupt controller safe, proves exception handling works, then idles.
 */
#include <stdbool.h>
#include <stddef.h>
#include "drivers/serial/serial.h"
#include "drivers/vga/vga_text.h"
#include "kernel/arch/x86/gdt.h"
#include "kernel/arch/x86/idt.h"
#include "kernel/arch/x86/interrupts.h"
#include "kernel/arch/x86/pic.h"
#include "kernel/bootscreen.h"
#include "kernel/kernel.h"
#include "kernel/panic.h"

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

/* Diagnostics: GRUB's "Diagnostics" menu (or QEMU -append) passes test=<fault>
 * to deliberately trigger a CPU exception and show the crash screen. */
static void run_crash_test(void)
{
    if (cmdline_has("test=divzero")) {
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

void kernel_main(uint32_t magic, uint32_t mb_info)
{
    serial_init();
    bool display_ok = vga_init();

    if (magic != MULTIBOOT_BOOTLOADER_MAGIC)
        panic("Not loaded by a Multiboot bootloader.");

    const struct multiboot_info *mbi = (const struct multiboot_info *)(uintptr_t)mb_info;
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

    bootscreen_status("Memory initialization:", BOOT_PENDING, "milestone 0.3");
    bootscreen_status("Display initialization:", display_ok ? BOOT_OK : BOOT_FAILED, NULL);

    run_crash_test();

    bootscreen_ready();

    for (;;)
        __asm__ volatile ("hlt");
}
