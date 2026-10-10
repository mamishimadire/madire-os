#ifndef MADIRE_PROC_USERENTRY_H
#define MADIRE_PROC_USERENTRY_H

/*
 * Entering ring 3 (0.10): the IRETQ frame a thread uses to start a program
 * in user mode, built and checked here (pure: tests/unit/test_userentry.c),
 * then loaded by enter_user (kernel/proc/userenter.asm).
 *
 * The frame is exactly: RIP = the image's entry point (inside its code),
 * CS = 0x3B (user code, RPL 3), RFLAGS = 0x202 (IF=1 and the reserved bit;
 * IOPL 0, and TF, DF, AC, NT clear), RSP = VM_USER_STACK_TOP (16-byte
 * aligned) and SS = 0x33 (user data, RPL 3). Anything else is refused.
 */

#include <stdbool.h>
#include <stdint.h>
#include "kernel/arch/x86/gdt_desc.h"
#include "kernel/memory/vm_layout.h"
#include "kernel/proc/userimg.h"

/* The order IRETQ pops (lowest address first). */
struct user_iret {
    uint64_t rip, cs, rflags, rsp, ss;
};

#define USER_RFLAGS_IF       0x200ull
#define USER_RFLAGS_RESERVED 0x2ull         /* bit 1, always 1 */
#define USER_RFLAGS_IOPL     0x3000ull
#define USER_RFLAGS_START    (USER_RFLAGS_IF | USER_RFLAGS_RESERVED)

/* 0.11: the frame for any entry point (a load plan's; elf_core has checked
 * it lies in executable bytes). */
static inline void user_iret_build_entry(struct user_iret *f, uint64_t entry)
{
    f->rip = entry;
    f->cs = GDT_USER_CODE_SEL;
    f->rflags = USER_RFLAGS_START;
    f->rsp = VM_USER_STACK_TOP;
    f->ss = GDT_USER_DATA_SEL;
}

static inline void user_iret_build(struct user_iret *f, const struct userimg *img)
{
    user_iret_build_entry(f, img->entry);
}

/* True if RIP lies inside the image's code: [VM_USER_TEXT, + text_size). */
static inline bool user_rip_in_code(uint64_t rip, const struct userimg *img)
{
    return rip >= VM_USER_TEXT && rip - VM_USER_TEXT < img->text_size;
}

/* True if RSP lies inside the user stack: [top - size, top]. */
static inline bool user_rsp_in_stack(uint64_t rsp)
{
    return rsp >= VM_USER_STACK_TOP - VM_USER_STACK_SIZE && rsp <= VM_USER_STACK_TOP;
}

static inline bool user_iret_ok(const struct user_iret *f, const struct userimg *img)
{
    return f->cs == GDT_USER_CODE_SEL && f->ss == GDT_USER_DATA_SEL &&
           f->rflags == USER_RFLAGS_START && user_rip_in_code(f->rip, img) &&
           f->rsp == VM_USER_STACK_TOP && (f->rsp & 15) == 0;
}

#endif
