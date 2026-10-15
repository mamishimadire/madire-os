/* System calls, the kernel side (see syscall.h). */
#include "kernel/arch/x86/cpuid.h"
#include "kernel/arch/x86/gdt_desc.h"
#include "kernel/arch/x86/irq.h"
#include "kernel/kprintf.h"
#include "kernel/console/console.h"
#include "kernel/memory/usercopy.h"
#include "kernel/proc/fd.h"
#include "kernel/proc/preempt.h"
#include "kernel/proc/process.h"
#include "kernel/proc/syscall.h"
#include "kernel/proc/thread.h"
#include "kernel/timer.h"

extern void syscall_entry(void);

static struct syscall_counts counts;
static struct syscall_sleep_stats sleep_stats;

/* ---- write (stage 5b) ---- */

static void emit_line(void *ctx, const char *line, size_t len)
{
    (void)ctx;
    char out[24 + SYSCALL_LINE_MAX + 1];
    for (size_t i = 0; i < len; i++)
        out[i] = line[i];
    out[len] = 0;
    klogf("%s\n", out);             /* serial and dmesg only, never the screen */
}

/* The whole buffer is checked before any of it is used: a bad buffer is
 * refused (-EFAULT) with nothing logged. 1.0 stage 4: the descriptor
 * table decides (not open, or not for writing: -EBADF); the console keeps
 * this path, a file goes through the chunk loop (kernel/proc/fd.c). */
static int64_t __attribute__((noinline)) console_write(uint64_t buf, uint64_t count);

static int64_t sys_write(uint64_t fd, uint64_t buf, uint64_t count)
{
    uint32_t file = 0;
    uint8_t kind = 0;
    int e = fd_resolve(process_current_fds(), fd, OF_WRITE, &file, &kind);
    if (e)
        return e;
    if (kind == OFILE_FILE)
        return fd_file_write(file, buf, count);
    return console_write(buf, count);
}

/* Its own frame (noinline): a file write never carries this 4 KiB buffer
 * on the thread's stack next to the chunk loop's. */
static int64_t console_write(uint64_t buf, uint64_t count)
{
    uint64_t n = syscall_console_count(count);
    if (n == 0)
        return 0;                   /* nothing touched, whatever buf is */
    if (!user_readable(buf, n))
        return -MADIRE_EFAULT;
    uint8_t write_buf[SYSCALL_WRITE_MAX];
    preempt_disable();
    bool ok = copy_from_user(write_buf, buf, n);
    preempt_enable();
    if (!ok)
        return -MADIRE_EFAULT;

    /* Keep newlines as line breaks; every other control or non-ASCII byte
     * is shown as '?', both on screen and in the existing serial record. */
    for (uint64_t i = 0; i < n; i++)
        if (write_buf[i] != '\n' && (write_buf[i] < 0x20 || write_buf[i] > 0x7E))
            write_buf[i] = '?';

    int32_t pid = process_current_pid();
    size_t written = console_program_write(pid, write_buf, (size_t)n);
    preempt_disable();
    syscall_write_lines(pid, write_buf, written, emit_line, 0);
    preempt_enable();
    return (int64_t)written;
}

/* ---- read (1.0 stage 4) ---- */

/* fd 0 at start is the console's input: no keyboard input reaches
 * programs yet, so it returns 0 (plan B1); a file goes through the chunk
 * loop. */
static int64_t sys_read(uint64_t fd, uint64_t buf, uint64_t count)
{
    uint32_t file = 0;
    uint8_t kind = 0;
    int e = fd_resolve(process_current_fds(), fd, OF_READ, &file, &kind);
    if (e)
        return e;
    if (kind != OFILE_FILE)
        return 0;
    return fd_file_read(file, buf, count);
}

/* ---- nanosleep (stage 5b) ---- */

struct user_timespec {
    int64_t tv_sec, tv_nsec;
};

static int64_t sys_nanosleep(uint64_t req)
{
    struct user_timespec ts;
    if (!copy_from_user(&ts, req, sizeof(ts)))
        return -MADIRE_EFAULT;
    uint32_t ticks = 0;
    int64_t e = syscall_timespec_ticks(ts.tv_sec, ts.tv_nsec, TIMER_HZ, &ticks);
    if (e)
        return e;
    uint64_t t0 = timer_ticks();
    if (ticks)
        thread_sleep(ticks);
    else
        thread_yield();
    uint64_t slept = timer_ticks() - t0;
    uint32_t f = irq_save();
    sleep_stats.calls++;
    sleep_stats.last_ticks = ticks;
    sleep_stats.last_slept = slept;
    irq_restore(f);
    return 0;                       /* rem is never written: only a kill cuts a sleep short
                                     * (0.11), and the process then ends on the way out */
}

static uint64_t rdmsr(uint32_t msr)
{
    uint32_t lo, hi;
    __asm__ volatile ("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return ((uint64_t)hi << 32) | lo;
}

static void wrmsr(uint32_t msr, uint64_t v)
{
    __asm__ volatile ("wrmsr" : : "c"(msr), "a"((uint32_t)v), "d"((uint32_t)(v >> 32)));
}

bool syscall_init(void)
{
    uint64_t star = gdt_star(), lstar = (uint64_t)(uintptr_t)syscall_entry;
    wrmsr(MSR_STAR, star);
    wrmsr(MSR_LSTAR, lstar);
    wrmsr(MSR_SFMASK, SYSCALL_SFMASK);
    wrmsr(MSR_EFER, rdmsr(MSR_EFER) | EFER_SCE);
    uint64_t efer = rdmsr(MSR_EFER), star1 = rdmsr(MSR_STAR), lstar1 = rdmsr(MSR_LSTAR);
    uint64_t sfmask1 = rdmsr(MSR_SFMASK);
    bool ok = (efer & EFER_SCE) && star1 == star && lstar1 == lstar && sfmask1 == SYSCALL_SFMASK;
    klogf("SYSCALL: EFER=0x%016llx (SCE=%u) STAR=0x%016llx LSTAR=0x%016llx SFMASK=0x%016llx, "
          "read back: %s\n", (unsigned long long)efer, (unsigned)(efer & EFER_SCE),
          (unsigned long long)star1, (unsigned long long)lstar1, (unsigned long long)sfmask1,
          ok ? "OK" : "MISMATCH");
    return ok;
}

/* Interrupts are off on entry (SFMASK); the call runs with them on, like
 * any kernel code of the thread, and they are off again for the return,
 * which first ends the process if a kill is pending. */
void syscall_dispatch(struct interrupt_frame *frame)
{
    interrupts_enable();
    enum syscall_id id = syscall_lookup(frame->rax);
    int64_t r;
    switch (id) {
    case SC_WRITE:
        r = sys_write(frame->rdi, frame->rsi, frame->rdx);
        break;
    case SC_READ:
        r = sys_read(frame->rdi, frame->rsi, frame->rdx);
        break;
    case SC_CLOSE:
        r = fd_sys_close(process_current_fds(), frame->rdi);
        break;
    case SC_DUP:
        r = fd_sys_dup(process_current_fds(), frame->rdi);
        break;
    case SC_DUP2:
        r = fd_sys_dup2(process_current_fds(), frame->rdi, frame->rsi);
        break;
    case SC_NANOSLEEP:
        r = sys_nanosleep(frame->rdi);
        break;
    case SC_GETPID:
        r = process_current_pid();
        break;
    case SC_SCHED_YIELD:
        thread_yield();
        r = 0;
        break;
    case SC_EXIT:
    case SC_EXIT_GROUP: {
        uint32_t f = irq_save();
        counts.by_id[id]++;
        irq_restore(f);
        process_exit_current(syscall_exit_status(frame->rdi));
    }
    default:
        r = -MADIRE_ENOSYS;
        break;
    }
    fd_busy_check("syscall return");   /* 1.0 stage 4: no busy mark back to ring 3 */
    __asm__ volatile ("cli");
    counts.by_id[id]++;
    if (r == -MADIRE_ENOSYS)
        counts.enosys++;
    frame->rax = (uint64_t)r;
    frame->rflags = syscall_user_rflags(frame->rflags);
    frame->rcx = frame->rip;        /* as SYSRET leaves them */
    frame->r11 = frame->rflags;
    process_return_to_user(frame);
}

void syscall_counts(struct syscall_counts *out)
{
    uint32_t f = irq_save();
    *out = counts;
    irq_restore(f);
}

void syscall_sleep_stats(struct syscall_sleep_stats *out)
{
    uint32_t f = irq_save();
    *out = sleep_stats;
    irq_restore(f);
}
