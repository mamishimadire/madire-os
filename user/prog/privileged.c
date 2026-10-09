/* privileged (0.10): one privileged instruction, chosen by arg; each must
 * raise #GP in ring 3 and kill this process.
 *   0 cli   1 hlt   2 out   3 in   4 mov from cr3   5 lgdt   6 wrmsr
 *   7 rdmsr   8 invlpg   9 sti */
#include "user/ulib.h"

int main(long arg)
{
    static const struct { uint16_t limit; uint64_t base; } __attribute__((packed)) gdtr = { 0, 0 };
    uint64_t v = 0;
    switch (arg) {
    case 0: __asm__ volatile ("cli"); break;
    case 1: __asm__ volatile ("hlt"); break;
    case 2: __asm__ volatile ("outb %%al, $0x80" : : "a"(0)); break;
    case 3: __asm__ volatile ("inb $0x60, %%al" : "=a"(v)); break;
    case 4: __asm__ volatile ("mov %%cr3, %0" : "=r"(v)); break;
    case 5: __asm__ volatile ("lgdt %0" : : "m"(gdtr)); break;
    case 6: __asm__ volatile ("wrmsr" : : "c"(0xC0000082u), "a"(0), "d"(0)); break;
    case 7: __asm__ volatile ("rdmsr" : "=a"(v) : "c"(0xC0000082u) : "rdx"); break;
    case 8: __asm__ volatile ("invlpg (%0)" : : "r"(0x400000ull) : "memory"); break;
    case 9: __asm__ volatile ("sti"); break;
    default: return 2;
    }
    (void)v;
    u_put_num("privileged: INSTRUCTION ALLOWED IN RING 3: ", arg);
    return 1;
}
