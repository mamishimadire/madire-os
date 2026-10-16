/* Unit tests for kernel/proc/userentry.h (the ring-3 IRETQ frame, 0.10). */
#include <stdint.h>
#include "kernel/proc/userentry.h"
#include "tests/unit/unit.h"

static const uint8_t code[100] = { 0x90 };

static struct userimg image(void)
{
    struct userimg i = { "spin", code, sizeof(code), 0, 0, 0, VM_USER_TEXT + 16 };
    return i;
}

static void test_build(void)
{
    struct userimg img = image();
    struct user_iret f;
    user_iret_build(&f, &img);
    CHECK_EQ(f.rip, VM_USER_TEXT + 16);
    CHECK_EQ(f.cs, 0x3B);
    CHECK_EQ(f.rflags, 0x202);
    CHECK_EQ(f.rsp, 0x00007FFFFFFFEE90ull);         /* 1.0: spin's start block (argc 1) */
    CHECK_EQ(f.ss, 0x33);
    CHECK(user_iret_ok(&f, &img));
    CHECK(f.rflags & USER_RFLAGS_IF);
    CHECK_EQ(f.rflags & USER_RFLAGS_IOPL, 0);
}

/* Every field that would not give ring 3, interrupts on, IOPL 0, the
 * program's own code and its stack is refused. */
static void test_refused(void)
{
    struct userimg img = image();
    struct user_iret good, f;
    user_iret_build(&good, &img);
    static const uint64_t bad_cs[] = { 0x08, 0x38, 0x39, 0x33, 0x2B, 0x28 | 3, 0 };
    for (unsigned i = 0; i < sizeof(bad_cs) / sizeof(bad_cs[0]); i++) {
        f = good;
        f.cs = bad_cs[i];
        CHECK(!user_iret_ok(&f, &img));
    }
    static const uint64_t bad_ss[] = { 0x10, 0x30, 0x3B, 0x2B, 0 };
    for (unsigned i = 0; i < sizeof(bad_ss) / sizeof(bad_ss[0]); i++) {
        f = good;
        f.ss = bad_ss[i];
        CHECK(!user_iret_ok(&f, &img));
    }
    static const uint64_t bad_flags[] = {
        0x002,                      /* IF clear */
        0x1202, 0x2202, 0x3202,     /* IOPL 1, 2, 3 */
        0x302,                      /* TF */
        0x602,                      /* DF */
        0x40202,                    /* AC */
        0x4202,                     /* NT */
        0x200,                      /* reserved bit clear */
    };
    for (unsigned i = 0; i < sizeof(bad_flags) / sizeof(bad_flags[0]); i++) {
        f = good;
        f.rflags = bad_flags[i];
        CHECK(!user_iret_ok(&f, &img));
    }
    f = good;
    f.rip = VM_USER_TEXT - 1;                       /* below the code */
    CHECK(!user_iret_ok(&f, &img));
    f.rip = VM_USER_TEXT + sizeof(code);            /* just past it */
    CHECK(!user_iret_ok(&f, &img));
    f.rip = 0xFFFFFFFF80100000ull;                  /* the kernel */
    CHECK(!user_iret_ok(&f, &img));
    f.rip = VM_USER_TEXT + sizeof(code) - 1;        /* the last byte: fine */
    CHECK(user_iret_ok(&f, &img));
    f = good;
    f.rsp = VM_USER_STACK_TOP - 8;                  /* not the start block, not aligned */
    CHECK(!user_iret_ok(&f, &img));
    f.rsp = VM_USER_STACK_TOP + 0x1000;             /* above the stack */
    CHECK(!user_iret_ok(&f, &img));
    f.rsp = VM_USER_STACK_TOP;                      /* 1.0: the top (0.10's RSP) is no start block */
    CHECK(!user_iret_ok(&f, &img));
}

static void test_ranges(void)
{
    struct userimg img = image();
    CHECK(user_rip_in_code(VM_USER_TEXT, &img));
    CHECK(user_rip_in_code(VM_USER_TEXT + sizeof(code) - 1, &img));
    CHECK(!user_rip_in_code(VM_USER_TEXT + sizeof(code), &img));
    CHECK(!user_rip_in_code(VM_USER_TEXT - 1, &img));
    CHECK(!user_rip_in_code(0, &img));
    CHECK(user_rsp_in_stack(VM_USER_STACK_TOP));
    CHECK(user_rsp_in_stack(VM_USER_STACK_TOP - VM_USER_STACK_SIZE));
    CHECK(!user_rsp_in_stack(VM_USER_STACK_TOP - VM_USER_STACK_SIZE - 1));
    CHECK(!user_rsp_in_stack(VM_USER_STACK_TOP + 1));
}

int main(void)
{
    printf("test_userentry\n");
    RUN(test_build);
    RUN(test_refused);
    RUN(test_ranges);
    return unit_report("test_userentry");
}
