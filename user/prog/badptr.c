/* badptr (0.10): system calls with bad pointers and descriptors must fail
 * cleanly (-EFAULT, -EBADF; write and nanosleep), never crash the kernel,
 * and never write or log anything. Exits with the number of wrong results. */
#include "user/ulib.h"

static const char ok[] = "badptr: valid write\n";    /* read-only data: readable */
extern char __bss_end[];                              /* user/link.ld */
static volatile char rw[64] = { 1 };                  /* data: at least one data page exists */
#define USER_STACK (32u * 1024)                         /* VM_USER_STACK_SIZE */

int main(long arg)
{
    (void)arg;
    int bad = 0;
    char buf[8] = { 'x', 'x', 'x', 'x', 'x', 'x', 'x', '\n' };
    bad += u_expect("badptr: NULL: ", sys_write(1, 0, 5), -EFAULT);
    bad += u_expect("badptr: kernel text: ", sys_write(1, (const void *)K_TEXT, 5), -EFAULT);
    bad += u_expect("badptr: kernel PML4: ", sys_write(1, (const void *)K_PML4, 5), -EFAULT);
    bad += u_expect("badptr: kernel heap: ", sys_write(1, (const void *)K_HEAP, 5), -EFAULT);
    bad += u_expect("badptr: unmapped: ", sys_write(1, (const void *)0x10000000ull, 5), -EFAULT);
    bad += u_expect("badptr: below 4 MiB: ", sys_write(1, (const void *)0x1000ull, 5), -EFAULT);
    bad += u_expect("badptr: across the top: ", sys_write(1, (const void *)(USER_TOP - 2), 5), -EFAULT);
    bad += u_expect("badptr: non-canonical: ",
                    sys_write(1, (const void *)0x0000800000000000ull, 5), -EFAULT);
    bad += u_expect("badptr: wraps: ", sys_write(1, (const void *)(USER_TOP - 16), UINT64_MAX), -EFAULT);
    /* From the last data/bss page into the unmapped hole after it, and from
     * the hole below the stack into the stack: mapped and unmapped mixed. */
    rw[0]++;
    uint64_t data_end = ((uint64_t)(uintptr_t)__bss_end + 4095) & ~4095ull;
    bad += u_expect("badptr: data page into the hole: ", sys_write(1, (const void *)(data_end - 4), 8), -EFAULT);
    bad += u_expect("badptr: hole into the stack: ",
                    sys_write(1, (const void *)(USER_TOP - USER_STACK - 4), 8), -EFAULT);
    bad += u_expect("badptr: nanosleep unmapped: ",
                    sys_nanosleep((const struct timespec *)0x10000000ull, 0), -EFAULT);
    bad += u_expect("badptr: fd 0: ", sys_write(0, buf, 8), -EBADF);
    bad += u_expect("badptr: fd 3: ", sys_write(3, buf, 8), -EBADF);
    bad += u_expect("badptr: fd -1: ", sys_write((uint64_t)-1, buf, 8), -EBADF);
    bad += u_expect("badptr: fd 2^32+1: ", sys_write((1ull << 32) + 1, buf, 8), -EBADF);
    bad += u_expect("badptr: count 0, NULL: ", sys_write(1, 0, 0), 0);
    bad += u_expect("badptr: count 0, kernel: ", sys_write(1, (const void *)K_TEXT, 0), 0);
    bad += u_expect("badptr: read-only data: ", sys_write(1, ok, sizeof(ok) - 1), (long)(sizeof(ok) - 1));
    bad += u_expect("badptr: stack: ", sys_write(2, buf, 8), 8);
    u_put_num("badptr: wrong results: ", bad);
    return bad;
}
