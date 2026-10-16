/* args (1.0 stage 7): the System V start block, seen from the program.
 * Prints what it received (argc, argv, envp) and checks its own start
 * block: the auxiliary vector, the layout (kernel/proc/loadplan.h), and
 * the registers at entry. Its entry point is args_entry (the Makefile links
 * it with -e args_entry), a stub that stores every general register and
 * RFLAGS as the kernel left them, then jumps to crt0's _start, so argc,
 * argv and envp reach main exactly as for every other program.
 * Exit status: the number of wrong values (at most 255). */
#include <stdbool.h>
#include "user/ulib.h"

/* rax rbx rcx rdx rsi rdi rbp r8-r15 (15), RFLAGS, then 1 once stored. */
uint64_t args_regs[17];
extern char args_entry[];

/* Absolute addressing only (the data lies below 2 GiB): no register is
 * used as scratch, RSP is the same after pushfq/popq, and the start block
 * above RSP is not touched. */
__asm__(".section .text.start,\"ax\",@progbits\n"
        ".global args_entry\n"
        "args_entry:\n"
        "    mov %rax, args_regs+0\n"
        "    mov %rbx, args_regs+8\n"
        "    mov %rcx, args_regs+16\n"
        "    mov %rdx, args_regs+24\n"
        "    mov %rsi, args_regs+32\n"
        "    mov %rdi, args_regs+40\n"
        "    mov %rbp, args_regs+48\n"
        "    mov %r8, args_regs+56\n"
        "    mov %r9, args_regs+64\n"
        "    mov %r10, args_regs+72\n"
        "    mov %r11, args_regs+80\n"
        "    mov %r12, args_regs+88\n"
        "    mov %r13, args_regs+96\n"
        "    mov %r14, args_regs+104\n"
        "    mov %r15, args_regs+112\n"
        "    pushfq\n"
        "    popq args_regs+120\n"
        "    movq $1, args_regs+128\n"
        "    jmp _start\n"
        ".previous\n");

#define TOP       0x00007FFFFFFFF000ull     /* VM_USER_STACK_TOP */
#define BLOCK     4096u                     /* STARTBLOCK_SIZE */
#define AUXC      18                        /* entries, AT_NULL included */
#define SHOWN_MAX 255                       /* longer strings: their length only */

static int bad;

static char line[400];
static uint64_t len;

static void l_str(const char *s)
{
    while (*s && len < sizeof(line) - 2)
        line[len++] = *s++;
}

static void l_num(uint64_t v)
{
    char d[20];
    int n = 0;
    do {
        d[n++] = (char)('0' + v % 10);
        v /= 10;
    } while (v);
    while (n && len < sizeof(line) - 2)
        line[len++] = d[--n];
}

static void l_hex(uint64_t v)
{
    l_str("0x");
    for (int s = 60; s >= 0; s -= 4)
        line[len++] = "0123456789abcdef"[(v >> s) & 15];
}

static void l_end(bool ok, bool verdict)
{
    if (verdict) {
        l_str(ok ? ": OK" : ": WRONG");
        if (!ok)
            bad++;
    }
    line[len++] = '\n';
    sys_write(1, line, len);
    len = 0;
}

static bool same(const char *a, const char *b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

static void show(const char *what, uint64_t i, const char *s)
{
    l_str("args: ");
    l_str(what);
    l_str("[");
    l_num(i);
    l_str("] ");
    uint64_t n = u_strlen(s);
    if (n > SHOWN_MAX) {
        l_num(n);
        l_str(" bytes");
    } else {
        l_str("'");
        l_str(s);
        l_str("'");
    }
    l_end(true, false);
}

static uint64_t cpuid1_edx(void)
{
    uint32_t a = 1, b, c = 0, d;
    __asm__ volatile ("cpuid" : "+a"(a), "=b"(b), "+c"(c), "=d"(d));
    (void)a;
    (void)b;
    (void)c;
    return d;
}

static uint64_t le(const uint8_t *p, int n)
{
    uint64_t v = 0;
    for (int i = 0; i < n; i++)
        v |= (uint64_t)p[i] << (8 * i);
    return v;
}

static bool in_block(uint64_t p)
{
    return p >= TOP - BLOCK && p < TOP;
}

int main(int argc, char **argv, char **envp)
{
    uint64_t *rsp = (uint64_t *)(uintptr_t)argv - 1;
    l_str("args: argc ");
    l_num((uint64_t)argc);
    l_end(true, false);
    for (int i = 0; i < argc; i++)
        show("argv", (uint64_t)i, argv[i]);
    uint64_t envc = 0;
    while (envp[envc])
        envc++;
    l_str("args: envc ");
    l_num(envc);
    l_end(true, false);
    for (uint64_t i = 0; i < envc; i++)
        show("envp", i, envp[i]);

    /* The auxiliary vector: after envp's NULL, pairs up to AT_NULL. */
    uint64_t *aux = (uint64_t *)(envp + envc + 1);
    static uint64_t val[64];            /* static: zeroed without a memset call */
    static uint32_t seen[64];
    int n = 0;
    bool known = true;
    while (n < 64) {
        uint64_t type = aux[2 * n], v = aux[2 * n + 1];
        n++;
        if (type >= 64) {
            known = false;
            continue;
        }
        seen[type]++;
        val[type] = v;
        if (type == 0)
            break;
    }
    static const uint8_t types[AUXC] = { 16, 6, 17, 3, 4, 5, 7, 8, 9, 11, 12, 13, 14, 23, 26,
                                         31, 15, 0 };
    bool once = known && n == AUXC && aux[2 * (n - 1)] == 0;
    for (int i = 0; i < AUXC; i++)
        once = once && seen[types[i]] == 1;
    l_str("args: auxv: ");
    l_num((uint64_t)n);
    l_str(" entries, each type once, AT_NULL last");
    l_end(once, true);

    bool values = val[6] == 4096 && val[17] == 100 && val[9] == (uint64_t)(uintptr_t)args_entry &&
                  val[7] == 0 && val[8] == 0 && val[23] == 0 && val[11] == 0 && val[12] == 0 &&
                  val[13] == 0 && val[14] == 0 && val[26] == 0 && val[16] == cpuid1_edx();
    l_str("args: auxv values: PAGESZ 4096, CLKTCK 100, ENTRY = the entry point, BASE FLAGS "
          "SECURE UID EUID GID EGID HWCAP2 0, HWCAP = CPUID.1:EDX");
    l_end(values, true);

    /* AT_PHDR: 0, or the program headers (56 bytes each), one of them a
     * loadable segment (PT_LOAD) holding the entry point. */
    bool ph = val[4] == 56;
    if (val[3]) {
        bool found = false;
        for (uint64_t i = 0; i < val[5] && i < 16; i++) {
            const uint8_t *h = (const uint8_t *)(uintptr_t)(val[3] + 56 * i);
            uint64_t ptype = le(h, 4), va = le(h + 16, 8), mem = le(h + 40, 8);
            if (ptype == 1 && val[9] >= va && val[9] - va < mem)
                found = true;
        }
        ph = ph && found;
    }
    l_str("args: AT_PHDR ");
    l_hex(val[3]);
    l_str(", AT_PHENT 56, AT_PHNUM ");
    l_num(val[5]);
    l_end(ph, true);

    const char *platform = (const char *)(uintptr_t)val[15];
    const char *execfn = (const char *)(uintptr_t)val[31];
    bool strs = in_block(val[15]) && in_block(val[31]) && same(platform, "x86_64");
    l_str("args: AT_PLATFORM '");
    l_str(strs ? platform : "?");
    l_str("', AT_EXECFN '");
    l_str(strs ? execfn : "?");
    l_str("'");
    l_end(strs, true);

    /* The layout (loadplan.h): from RSP up argc, argv, NULL, envp, NULL,
     * auxv; the strings from argv[0] up in order, the name last, then 8
     * zero bytes at the top; "x86_64" just below argv[0]'s 16-byte line;
     * RSP the highest multiple of 16 that leaves room for the words. */
    uint64_t r = (uint64_t)(uintptr_t)rsp;
    bool lay = (r & 15) == 0 && r >= TOP - BLOCK && rsp[0] == (uint64_t)argc &&
               argv[argc] == 0 && (uint64_t *)(uintptr_t)envp == rsp + 1 + argc + 1;
    const char *s = argc ? argv[0] : (envc ? envp[0] : execfn);
    uint64_t first = (uint64_t)(uintptr_t)s;
    for (int i = 0; lay && i < argc; i++) {
        lay = argv[i] == s;
        s += u_strlen(s) + 1;
    }
    for (uint64_t i = 0; lay && i < envc; i++) {
        lay = envp[i] == s;
        s += u_strlen(s) + 1;
    }
    if (lay) {
        lay = strs && execfn == s;
        s += u_strlen(execfn) + 1;
        lay = lay && (uint64_t)(uintptr_t)s == TOP - 8 && *(const uint64_t *)(TOP - 8) == 0;
    }
    uint64_t words = 1 + (uint64_t)argc + 1 + envc + 1 + 2 * AUXC;
    uint64_t plat = (first & ~15ull) - 7;
    lay = lay && val[15] == plat && (uint64_t)(uintptr_t)(aux + 2 * AUXC) <= plat &&
          r == ((plat - 8 * words) & ~15ull);
    l_str("args: layout: RSP ");
    l_hex(r);
    l_str(" 16-byte aligned, argc/argv/envp/auxv contiguous, strings above auxv in order, "
          "8 zero bytes at the top");
    l_end(lay, true);

    bool regs = args_regs[16] == 1 && args_regs[15] == 0x202;
    for (int i = 0; i < 15; i++)
        regs = regs && args_regs[i] == 0;
    l_str("args: registers at entry: 0 except RSP, RFLAGS ");
    l_hex(args_regs[15]);
    l_end(regs, true);

    l_str("args: ");
    l_num((uint64_t)bad);
    l_str(" wrong");
    l_end(true, false);
    return bad > 255 ? 255 : bad;
}
