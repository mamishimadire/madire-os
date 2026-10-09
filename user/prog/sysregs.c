/* sysregs (0.10): registers and RFLAGS across a system call. Every
 * general register gets a known value and RFLAGS.NT is set, then getpid
 * runs. Afterwards only RAX (the result), RCX (the return address) and
 * R11 (RFLAGS) may differ, as with SYSRET; RFLAGS must have NT clear, IF
 * set and IOPL 0, and R11 must hold it. Exit status:
 *   0 all right   1-12 that register changed (rbx rbp rdx rsi rdi r8 r9
 *   r10 r12 r13 r14 r15)   20 RAX not a process ID   21 RCX not the return
 *   address   22 R11 not RFLAGS   23 NT still set   24 IF clear   25 IOPL */
#include "user/ulib.h"

uint64_t sr_out[17];

int main(long arg)
{
    (void)arg;
    __asm__ volatile (
        "push %%rbp\n\t"
        "pushfq\n\t"
        "orq $0x4000, (%%rsp)\n\t"          /* NT */
        "popfq\n\t"
        "movabs $0x5A5A5A5A00000001, %%rbx\n\t"
        "movabs $0x5A5A5A5A00000002, %%rbp\n\t"
        "movabs $0x5A5A5A5A00000003, %%rdx\n\t"
        "movabs $0x5A5A5A5A00000004, %%rsi\n\t"
        "movabs $0x5A5A5A5A00000005, %%rdi\n\t"
        "movabs $0x5A5A5A5A00000006, %%r8\n\t"
        "movabs $0x5A5A5A5A00000007, %%r9\n\t"
        "movabs $0x5A5A5A5A00000008, %%r10\n\t"
        "movabs $0x5A5A5A5A00000009, %%r12\n\t"
        "movabs $0x5A5A5A5A0000000A, %%r13\n\t"
        "movabs $0x5A5A5A5A0000000B, %%r14\n\t"
        "movabs $0x5A5A5A5A0000000C, %%r15\n\t"
        "mov $39, %%eax\n\t"                 /* getpid */
        "syscall\n"
        "1:\n\t"
        "mov %%rbx, sr_out+0(%%rip)\n\t"
        "mov %%rbp, sr_out+8(%%rip)\n\t"
        "mov %%rdx, sr_out+16(%%rip)\n\t"
        "mov %%rsi, sr_out+24(%%rip)\n\t"
        "mov %%rdi, sr_out+32(%%rip)\n\t"
        "mov %%r8, sr_out+40(%%rip)\n\t"
        "mov %%r9, sr_out+48(%%rip)\n\t"
        "mov %%r10, sr_out+56(%%rip)\n\t"
        "mov %%r12, sr_out+64(%%rip)\n\t"
        "mov %%r13, sr_out+72(%%rip)\n\t"
        "mov %%r14, sr_out+80(%%rip)\n\t"
        "mov %%r15, sr_out+88(%%rip)\n\t"
        "mov %%rax, sr_out+96(%%rip)\n\t"
        "mov %%rcx, sr_out+104(%%rip)\n\t"
        "mov %%r11, sr_out+112(%%rip)\n\t"
        "pushfq\n\t"
        "popq sr_out+120(%%rip)\n\t"
        "lea 1b(%%rip), %%rax\n\t"
        "mov %%rax, sr_out+128(%%rip)\n\t"
        "pop %%rbp\n\t"
        : : : "rax", "rbx", "rcx", "rdx", "rsi", "rdi", "r8", "r9", "r10", "r11",
              "r12", "r13", "r14", "r15", "memory", "cc");
    for (int i = 0; i < 12; i++)
        if (sr_out[i] != 0x5A5A5A5A00000000ull + (uint64_t)(i + 1))
            return i + 1;
    uint64_t flags = sr_out[15];
    if ((int64_t)sr_out[12] <= 0)
        return 20;
    if (sr_out[13] != sr_out[16])
        return 21;
    if (sr_out[14] != flags)
        return 22;
    if (flags & 0x4000)
        return 23;
    if (!(flags & 0x200))
        return 24;
    if (flags & 0x3000)
        return 25;
    return 0;
}
