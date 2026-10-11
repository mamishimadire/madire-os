#ifndef MADIRE_PROC_SCHED_CORE_H
#define MADIRE_PROC_SCHED_CORE_H

/*
 * Scheduler core (0.9): the bookkeeping of kernel threads, with no hardware
 * access, printing or panics, so it is unit-tested on the host
 * (tests/unit/test_sched.c, tests/unit/test_kstack.c). kernel/proc/thread.c
 * does the context switches and turns the errors below into panics.
 *
 * Threads are slots 0 .. SCHED_MAX_THREADS-1; a thread id is its slot.
 * Thread 0 is kernel_main's flow (the console and idle loop): it exists from
 * sched_init() on, is never on a stack slot, and never blocks or exits, so
 * there is always a thread that can run (the others may all sleep).
 *
 * States:
 *   FREE     slot unused
 *   NEW      allocated, not started (its stack is being set up)
 *   READY    on the run queue
 *   RUNNING  on the CPU (exactly one thread: `current`)
 *   BLOCKED  sleeping until a tick, or waiting for another thread to exit
 *   ZOMBIE   exited; its exit code is kept until it is reaped
 *
 * Policy: round robin. A thread runs for at most SCHED_SLICE_TICKS timer
 * ticks while another thread is ready; then `need_resched` is set and the
 * kernel switches at the next safe point (end of an IRQ, preemption count
 * 0). A thread that yields, sleeps, joins or exits goes off the CPU at once.
 * A sleeper that wakes goes to the tail of the run queue and also sets
 * `need_resched`, so it is not left waiting for the rest of the current
 * slice when the CPU is otherwise only spinning.
 *
 * Every operation that takes the current thread off the CPU stores the
 * thread to switch to in `*next`; the caller then switches (if it differs).
 * Yield, sleep and join refuse to run while preemption is disabled
 * (SCHED_ERR_PREEMPT_OFF, a kernel bug the caller turns into a panic).
 */

#include <stdbool.h>
#include <stdint.h>
#include "kernel/memory/vm_layout.h"

#define SCHED_MAX_THREADS  64
#define SCHED_SLICE_TICKS  5u      /* 50 ms at the 100 Hz system tick */
#define SCHED_NONE         (-1)

enum sched_state {
    SCHED_FREE = 0,
    SCHED_NEW,
    SCHED_READY,
    SCHED_RUNNING,
    SCHED_BLOCKED,
    SCHED_ZOMBIE,
};

enum sched_block {
    SCHED_BLOCK_NONE = 0,
    SCHED_BLOCK_SLEEP,          /* until wake_tick */
    SCHED_BLOCK_JOIN,           /* until thread join_tid exits */
};

enum sched_result {
    SCHED_OK = 0,
    SCHED_BLOCKED_NOW,          /* the current thread blocked: switch to *next */
    SCHED_WOULD_BLOCK,          /* thread 0 must wait by polling, never block */
    SCHED_ERR_FULL,             /* no free thread slot */
    SCHED_ERR_NO_THREAD,        /* tid out of range, or not a live thread */
    SCHED_ERR_STATE,            /* the thread is not in the state required */
    SCHED_ERR_SELF,             /* a thread cannot join itself */
    SCHED_ERR_DETACHED,         /* detached threads cannot be joined */
    SCHED_ERR_JOINED,           /* another thread is already joining it */
    SCHED_ERR_THREAD0,          /* thread 0 may not block or exit */
    SCHED_ERR_PREEMPT_OFF,      /* yield/sleep/join with preemption disabled */
};

struct sched_thread {
    enum sched_state state;
    enum sched_block block;
    bool detached;              /* reaped by thread 0, never joined */
    int32_t joiner;             /* thread blocked joining this one, or NONE */
    int32_t join_tid;           /* SCHED_BLOCK_JOIN: the thread waited for */
    uint64_t wake_tick;         /* SCHED_BLOCK_SLEEP: first tick it may run */
    int32_t exit_code;          /* SCHED_ZOMBIE */
    uint32_t slice_left;        /* ticks left in the current slice */
    uint64_t ticks_run;         /* ticks this thread was on the CPU */
    int32_t next;               /* run-queue link */
};

struct sched {
    struct sched_thread t[SCHED_MAX_THREADS];
    int32_t current;
    int32_t head, tail;         /* run queue (READY threads, FIFO) */
    bool need_resched;
    uint64_t switches;          /* times `current` changed */
    uint32_t live;              /* threads not FREE (thread 0 included) */
    /* 0.12 stage 2b (self-check): a thread taken off the run queue that was
     * not READY; thread.c panics before switching to it. Appended, so the
     * offsets above (read by tests/qemu_capture.py) do not move. */
    uint32_t bad_picks;
    int32_t bad_pick_tid;
    enum sched_state bad_pick_state;
};

void sched_init(struct sched *s);

/* Allocates a slot (lowest free tid >= 1) in state NEW. */
enum sched_result sched_alloc(struct sched *s, bool detached, int32_t *tid);
/* NEW -> READY (tail of the run queue). */
enum sched_result sched_start(struct sched *s, int32_t tid);
/* Gives a NEW thread's slot back (its set-up failed). */
enum sched_result sched_abandon(struct sched *s, int32_t tid);

/* The current thread gives up the CPU (back to the tail of the queue). */
enum sched_result sched_yield(struct sched *s, uint32_t preempt_count, int32_t *next);
/* Switch decision at the end of an IRQ: if need_resched and preemption is
 * enabled, the current thread is requeued like a yield and *next is set;
 * returns true only if a different thread should run. */
bool sched_preempt(struct sched *s, uint32_t preempt_count, int32_t *next);
/* Sleep until tick `wake` (>= now + 1). Thread 0: SCHED_WOULD_BLOCK. */
enum sched_result sched_sleep(struct sched *s, uint32_t preempt_count, uint64_t wake,
                              int32_t *next);
/* 0.11: wakes thread `tid` early if it is sleeping (SCHED_BLOCK_SLEEP): to
 * the tail of the run queue, need_resched set, as when its tick comes. A
 * kill (process_kill, Ctrl+C) uses it so a sleeping process ends at once.
 * SCHED_ERR_STATE: it is not sleeping (running, ready, joining, zombie);
 * SCHED_ERR_NO_THREAD: no such thread. tests/unit/test_wake011.c. */
enum sched_result sched_wake(struct sched *s, int32_t tid);
/* Join `tid`: if it is a zombie, reap it (*code, SCHED_OK); otherwise block
 * (SCHED_BLOCKED_NOW; call again once woken) or, for thread 0,
 * SCHED_WOULD_BLOCK. */
enum sched_result sched_join(struct sched *s, uint32_t preempt_count, int32_t tid,
                             int32_t *code, int32_t *next);
/* The current thread exits: ZOMBIE, its joiner (if any) woken. */
enum sched_result sched_exit(struct sched *s, int32_t code, int32_t *next);
/* Thread 0's reaper: the lowest detached zombie, made FREE (*tid), or
 * SCHED_ERR_NO_THREAD if there is none. */
enum sched_result sched_reap_detached(struct sched *s, int32_t *tid);

/* One timer tick (IRQ context: bookkeeping only): charges the current
 * thread, wakes sleepers whose tick has come, sets need_resched. */
void sched_tick(struct sched *s, uint64_t now);

/*
 * Turn-order check for the cooperative tests (0.9). Threads that only
 * yield to each other take turns in a fixed cycle; each logged turn
 * records the thread and the preemption count at that moment. A timer
 * preemption may legally reorder the turns (VirtualBox, for instance,
 * can deliver several timer ticks at once after the host delayed the VM,
 * so a slice can run out within microseconds). The rules:
 *   - every thread of the cycle has exactly `rounds` turns, nothing else;
 *   - a turn that is not the expected one (the thread after the previous
 *     turn's thread in the cycle, or cycle[0] first) is a "break"; a break
 *     is allowed only where the preemption count rose within the previous
 *     k turns, k = the cycle's length (or since `pre_start` for the first k
 *     turns). 0.10: 0.9 compared with the previous turn only, but a thread
 *     preempted after logging its turn and before yielding shows the wrong
 *     turn only once every other thread has logged again (with the count
 *     already raised): the boot self-test failed on a correct kernel. For
 *     the ping-pong (k = 2) that is the previous two turns, nothing looser;
 *   - SCHED_ORDER_STRICT: no break; SCHED_ORDER_RELAXED: every break
 *     explained by a preemption; otherwise BAD_COUNT or BAD_ORDER.
 */
struct sched_turn {
    int32_t tid;
    uint64_t preemptions;
};

enum sched_order {
    SCHED_ORDER_STRICT = 0,
    SCHED_ORDER_RELAXED,
    SCHED_ORDER_BAD_COUNT,
    SCHED_ORDER_BAD_ORDER,
};

enum sched_order sched_order_check(const struct sched_turn *log, uint32_t n,
                                   const int32_t *cycle, uint32_t k, uint32_t rounds,
                                   uint64_t pre_start, uint32_t *breaks);
const char *sched_order_str(enum sched_order o);

bool sched_queue_empty(const struct sched *s);
uint32_t sched_ready_count(const struct sched *s);
const char *sched_state_str(enum sched_state st);
const char *sched_result_str(enum sched_result r);

/*
 * Kernel thread stacks (0.9), in PML4 slot 507 (vm_layout.h). Slot i is
 * KSTACK_SLOT_SIZE bytes at VM_KSTACK_BASE + i * KSTACK_SLOT_SIZE: the lower
 * KSTACK_GUARD_SIZE bytes are never mapped (the guard), the top KSTACK_SIZE
 * bytes are the stack, which grows down towards the guard.
 */
#define KSTACK_SLOTS       64
#define KSTACK_SLOT_SIZE   (32u * 1024)
#define KSTACK_SIZE        (16u * 1024)
#define KSTACK_GUARD_SIZE  (KSTACK_SLOT_SIZE - KSTACK_SIZE)
#define KSTACK_AREA_SIZE   ((uint64_t)KSTACK_SLOTS * KSTACK_SLOT_SIZE)    /* 2 MiB */

struct kstack_pool {
    uint64_t used;              /* bit i: slot i is in use */
};

void kstack_init(struct kstack_pool *p);
/* The lowest free slot, or -1 when all are in use. */
int kstack_alloc(struct kstack_pool *p);
/* false (and nothing changes) for a slot out of range or not in use. */
bool kstack_free(struct kstack_pool *p, int slot);
uint32_t kstack_in_use(const struct kstack_pool *p);

uint64_t kstack_slot_base(int slot);    /* = the guard's lowest address */
uint64_t kstack_bottom(int slot);       /* lowest stack byte (above the guard) */
uint64_t kstack_top(int slot);          /* one past the highest stack byte */
/* The slot whose guard contains addr, or -1. */
int kstack_guard_slot(uint64_t addr);
/* The slot whose stack contains addr, or -1. */
int kstack_stack_slot(uint64_t addr);

/*
 * 0.11: the longest turn together with the thread that took it
 * (tests/unit/test_longest011.c). A 0.10 watch item: one VirtualBox boot
 * measured a 10-tick turn, and the record did not say whose it was. The
 * owner is captured when the turn is noted, because by the time a test
 * logs it the thread slot may be reused and the process gone. pid > 0:
 * the thread was a user process's; pid 0 with own_space: a thread running
 * in its own address space without a process entry (test=userspin's);
 * otherwise a kernel thread.
 */
/*
 * 0.11, policy (b) (an approved deviation from the 0.9 policy (a)): when
 * preempt_enable() brings the count to 0 and a switch is due, it switches
 * at once, provided interrupts are on (so never inside an interrupt
 * handler: interrupt gates clear IF), the scheduler is ready and no panic
 * is in progress. Wherever this holds, a timer interrupt could already
 * have switched, so no new interleaving appears; the switch only stops
 * waiting for the next interrupt. tests/unit/test_preemptb.c.
 */
bool sched_switch_on_enable(uint32_t count, bool pending, bool interrupts_on, bool ready,
                            bool panicking);

#define SCHED_LONGEST_NAME_MAX 16       /* including the terminating 0 */

struct sched_longest {
    uint64_t ticks;
    int32_t tid;                        /* -1: no turn noted yet */
    int32_t pid;
    bool own_space;
    char name[SCHED_LONGEST_NAME_MAX];
};

void sched_longest_reset(struct sched_longest *l);
/* Records the turn if it is longer than the one held (a tie keeps the
 * first). name may be NULL; it is cut to SCHED_LONGEST_NAME_MAX - 1
 * characters. Returns true if the turn was recorded. */
bool sched_longest_note(struct sched_longest *l, uint64_t ticks, int32_t tid, int32_t pid,
                        bool own_space, const char *name);

#endif
