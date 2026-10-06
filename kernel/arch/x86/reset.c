/* Restarting the machine (see reset.h). */
#include <stdint.h>
#include "kernel/arch/x86/io.h"
#include "kernel/arch/x86/reset.h"
#include "kernel/kprintf.h"

#define KBC_STATUS          0x64
#define KBC_COMMAND         0x64
#define KBC_INPUT_FULL      0x02    /* status bit 1: controller still busy */
#define KBC_PULSE_RESET     0xFE    /* pulse output line 0: the CPU reset */
#define KBC_WAIT_LIMIT      100000u
#define RESET_WAIT_IO       100000u /* io_wait() calls, about 1 us each */

struct idt_pointer {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

static void keyboard_controller_reset(void)
{
    uint32_t i = 0;
    while (i < KBC_WAIT_LIMIT && (inb(KBC_STATUS) & KBC_INPUT_FULL))
        i++;
    klogf("Restart: keyboard controller reset (0xfe to port 0x64)%s\n",
          i < KBC_WAIT_LIMIT ? "" : ", although the controller stayed busy");
    outb(KBC_COMMAND, KBC_PULSE_RESET);
    for (uint32_t n = 0; n < RESET_WAIT_IO; n++)
        io_wait();
    klogf("Restart: the keyboard controller did not reset the machine\n");
}

void reset_machine(bool keyboard_controller)
{
    __asm__ volatile ("cli");
    if (keyboard_controller)
        keyboard_controller_reset();
    klogf("Restart: forcing a triple fault (empty interrupt table, then int3)\n");
    static const struct idt_pointer empty = { 0, 0 };
    __asm__ volatile ("lidt %0\n\t"
                      "int3" : : "m"(empty) : "memory");
    for (;;)
        __asm__ volatile ("cli; hlt");
}
