/* Scheduler core (see sched_core.h). */
#include "kernel/proc/sched_core.h"

static bool valid_tid(int32_t tid)
{
    return tid >= 0 && tid < SCHED_MAX_THREADS;
}

static void clear_thread(struct sched_thread *t)
{
    t->state = SCHED_FREE;
    t->block = SCHED_BLOCK_NONE;
    t->detached = false;
    t->joiner = SCHED_NONE;
    t->join_tid = SCHED_NONE;
    t->wake_tick = 0;
    t->exit_code = 0;
    t->slice_left = 0;
    t->ticks_run = 0;
    t->next = SCHED_NONE;
}

static void enqueue(struct sched *s, int32_t tid)
{
    s->t[tid].state = SCHED_READY;
    s->t[tid].next = SCHED_NONE;
    if (s->tail == SCHED_NONE)
        s->head = tid;
    else
        s->t[s->tail].next = tid;
    s->tail = tid;
}

static int32_t dequeue(struct sched *s)
{
    int32_t tid = s->head;
    if (tid == SCHED_NONE)
        return SCHED_NONE;
    s->head = s->t[tid].next;
    if (s->head == SCHED_NONE)
        s->tail = SCHED_NONE;
    s->t[tid].next = SCHED_NONE;
    return tid;
}

/* The current thread has left the CPU (requeued, blocked or exited): the
 * head of the run queue becomes current. The queue is never empty here:
 * thread 0 never blocks or exits, so it is either the thread that was just
 * requeued or already waiting in the queue. */
static int32_t run_next(struct sched *s)
{
    int32_t prev = s->current;
    int32_t n = dequeue(s);
    if (n == SCHED_NONE)
        n = 0;                              /* unreachable; see above */
    s->t[n].state = SCHED_RUNNING;
    s->t[n].slice_left = SCHED_SLICE_TICKS;
    s->current = n;
    s->need_resched = false;
    if (n != prev)
        s->switches++;
    return n;
}

static void free_slot(struct sched *s, int32_t tid)
{
    clear_thread(&s->t[tid]);
    s->live--;
}

void sched_init(struct sched *s)
{
    for (int32_t i = 0; i < SCHED_MAX_THREADS; i++)
        clear_thread(&s->t[i]);
    s->t[0].state = SCHED_RUNNING;
    s->t[0].slice_left = SCHED_SLICE_TICKS;
    s->current = 0;
    s->head = SCHED_NONE;
    s->tail = SCHED_NONE;
    s->need_resched = false;
    s->switches = 0;
    s->live = 1;
}

enum sched_result sched_alloc(struct sched *s, bool detached, int32_t *tid)
{
    for (int32_t i = 1; i < SCHED_MAX_THREADS; i++) {
        if (s->t[i].state != SCHED_FREE)
            continue;
        clear_thread(&s->t[i]);
        s->t[i].state = SCHED_NEW;
        s->t[i].detached = detached;
        s->live++;
        *tid = i;
        return SCHED_OK;
    }
    return SCHED_ERR_FULL;
}

enum sched_result sched_start(struct sched *s, int32_t tid)
{
    if (!valid_tid(tid) || s->t[tid].state == SCHED_FREE)
        return SCHED_ERR_NO_THREAD;
    if (s->t[tid].state != SCHED_NEW)
        return SCHED_ERR_STATE;
    enqueue(s, tid);
    return SCHED_OK;
}

enum sched_result sched_abandon(struct sched *s, int32_t tid)
{
    if (!valid_tid(tid) || s->t[tid].state == SCHED_FREE)
        return SCHED_ERR_NO_THREAD;
    if (s->t[tid].state != SCHED_NEW)
        return SCHED_ERR_STATE;
    free_slot(s, tid);
    return SCHED_OK;
}

enum sched_result sched_yield(struct sched *s, uint32_t preempt_count, int32_t *next)
{
    if (preempt_count > 0)
        return SCHED_ERR_PREEMPT_OFF;
    enqueue(s, s->current);
    *next = run_next(s);
    return SCHED_OK;
}

bool sched_preempt(struct sched *s, uint32_t preempt_count, int32_t *next)
{
    int32_t prev = s->current;
    *next = prev;
    if (!s->need_resched || preempt_count > 0)
        return false;                       /* need_resched stays set */
    if (s->head == SCHED_NONE) {
        s->need_resched = false;            /* nobody else to run */
        return false;
    }
    enqueue(s, prev);
    *next = run_next(s);
    return *next != prev;
}

enum sched_result sched_sleep(struct sched *s, uint32_t preempt_count, uint64_t wake,
                              int32_t *next)
{
    if (preempt_count > 0)
        return SCHED_ERR_PREEMPT_OFF;
    if (s->current == 0)
        return SCHED_WOULD_BLOCK;
    struct sched_thread *t = &s->t[s->current];
    t->state = SCHED_BLOCKED;
    t->block = SCHED_BLOCK_SLEEP;
    t->wake_tick = wake;
    *next = run_next(s);
    return SCHED_BLOCKED_NOW;
}

enum sched_result sched_wake(struct sched *s, int32_t tid)
{
    if (!valid_tid(tid) || s->t[tid].state == SCHED_FREE)
        return SCHED_ERR_NO_THREAD;
    struct sched_thread *t = &s->t[tid];
    if (t->state != SCHED_BLOCKED || t->block != SCHED_BLOCK_SLEEP)
        return SCHED_ERR_STATE;
    t->block = SCHED_BLOCK_NONE;
    enqueue(s, tid);
    s->need_resched = true;                 /* as for a sleeper whose tick came */
    return SCHED_OK;
}

enum sched_result sched_join(struct sched *s, uint32_t preempt_count, int32_t tid,
                             int32_t *code, int32_t *next)
{
    int32_t cur = s->current;
    *next = cur;
    if (preempt_count > 0)
        return SCHED_ERR_PREEMPT_OFF;
    if (!valid_tid(tid) || s->t[tid].state == SCHED_FREE)
        return SCHED_ERR_NO_THREAD;
    if (tid == cur)
        return SCHED_ERR_SELF;
    if (tid == 0)
        return SCHED_ERR_THREAD0;           /* thread 0 never exits */
    struct sched_thread *target = &s->t[tid];
    if (target->detached)
        return SCHED_ERR_DETACHED;
    if (target->joiner != SCHED_NONE && target->joiner != cur)
        return SCHED_ERR_JOINED;

    if (target->state == SCHED_ZOMBIE) {
        *code = target->exit_code;
        free_slot(s, tid);
        s->t[cur].block = SCHED_BLOCK_NONE;
        s->t[cur].join_tid = SCHED_NONE;
        return SCHED_OK;
    }
    target->joiner = cur;
    if (cur == 0)
        return SCHED_WOULD_BLOCK;           /* thread 0 polls (and idles) */
    s->t[cur].state = SCHED_BLOCKED;
    s->t[cur].block = SCHED_BLOCK_JOIN;
    s->t[cur].join_tid = tid;
    *next = run_next(s);
    return SCHED_BLOCKED_NOW;
}

enum sched_result sched_exit(struct sched *s, int32_t code, int32_t *next)
{
    int32_t cur = s->current;
    *next = cur;
    if (cur == 0)
        return SCHED_ERR_THREAD0;
    struct sched_thread *t = &s->t[cur];
    t->state = SCHED_ZOMBIE;
    t->exit_code = code;
    int32_t j = t->joiner;
    if (j != SCHED_NONE && s->t[j].state == SCHED_BLOCKED &&
        s->t[j].block == SCHED_BLOCK_JOIN && s->t[j].join_tid == cur) {
        s->t[j].block = SCHED_BLOCK_NONE;
        enqueue(s, j);
    }
    *next = run_next(s);
    return SCHED_OK;
}

enum sched_result sched_reap_detached(struct sched *s, int32_t *tid)
{
    for (int32_t i = 1; i < SCHED_MAX_THREADS; i++) {
        if (s->t[i].state == SCHED_ZOMBIE && s->t[i].detached) {
            free_slot(s, i);
            *tid = i;
            return SCHED_OK;
        }
    }
    return SCHED_ERR_NO_THREAD;
}

/* Due when now has reached wake; the signed difference also handles a tick
 * counter that wraps. */
static bool due(uint64_t now, uint64_t wake)
{
    return (int64_t)(now - wake) >= 0;
}

void sched_tick(struct sched *s, uint64_t now)
{
    struct sched_thread *cur = &s->t[s->current];
    cur->ticks_run++;
    if (cur->slice_left > 0)
        cur->slice_left--;

    /* Wake every sleeper whose tick has come, earliest wake tick first
     * (then lowest tid), each to the tail of the run queue. */
    bool woke = false;
    for (;;) {
        int32_t pick = SCHED_NONE;
        for (int32_t i = 1; i < SCHED_MAX_THREADS; i++) {
            const struct sched_thread *t = &s->t[i];
            if (t->state != SCHED_BLOCKED || t->block != SCHED_BLOCK_SLEEP ||
                !due(now, t->wake_tick))
                continue;
            if (pick == SCHED_NONE || (int64_t)(t->wake_tick - s->t[pick].wake_tick) < 0)
                pick = i;
        }
        if (pick == SCHED_NONE)
            break;
        s->t[pick].block = SCHED_BLOCK_NONE;
        enqueue(s, pick);
        woke = true;
    }

    /* A woken sleeper should run soon; otherwise switch only when the
     * slice is used up and someone else is waiting. */
    if (woke || (cur->slice_left == 0 && s->head != SCHED_NONE))
        s->need_resched = true;
}

enum sched_order sched_order_check(const struct sched_turn *log, uint32_t n,
                                   const int32_t *cycle, uint32_t k, uint32_t rounds,
                                   uint64_t pre_start, uint32_t *breaks)
{
    *breaks = 0;
    if (k == 0 || n != k * rounds)
        return SCHED_ORDER_BAD_COUNT;
    /* Every turn belongs to the cycle, and every thread has `rounds`. */
    for (uint32_t j = 0; j < k; j++) {
        uint32_t count = 0;
        for (uint32_t i = 0; i < n; i++)
            if (log[i].tid == cycle[j])
                count++;
        if (count != rounds)
            return SCHED_ORDER_BAD_COUNT;
    }
    for (uint32_t i = 0; i < n; i++) {
        bool known = false;
        for (uint32_t j = 0; j < k && !known; j++)
            known = log[i].tid == cycle[j];
        if (!known)
            return SCHED_ORDER_BAD_COUNT;
    }
    /* Breaks, each of which needs a preemption within the previous k turns
     * (0.10; 0.9 looked at the previous turn only). A thread preempted after
     * logging its turn but before yielding makes the wrong turn appear only
     * after every other thread of the cycle has logged once more, and those
     * turns already carry the raised count. */
    for (uint32_t i = 0; i < n; i++) {
        int32_t want = cycle[0];
        if (i > 0) {
            uint32_t j = 0;
            while (cycle[j] != log[i - 1].tid)
                j++;
            want = cycle[(j + 1) % k];
        }
        uint64_t before = i >= k ? log[i - k].preemptions : pre_start;
        if (log[i].tid == want)
            continue;
        if (log[i].preemptions <= before)
            return SCHED_ORDER_BAD_ORDER;       /* out of turn, no preemption */
        (*breaks)++;
    }
    return *breaks ? SCHED_ORDER_RELAXED : SCHED_ORDER_STRICT;
}

const char *sched_order_str(enum sched_order o)
{
    switch (o) {
    case SCHED_ORDER_STRICT:    return "strict order";
    case SCHED_ORDER_RELAXED:   return "order differs only right after preemptions";
    case SCHED_ORDER_BAD_COUNT: return "wrong number of turns";
    case SCHED_ORDER_BAD_ORDER: return "out of order without a preemption";
    }
    return "?";
}

bool sched_queue_empty(const struct sched *s)
{
    return s->head == SCHED_NONE;
}

uint32_t sched_ready_count(const struct sched *s)
{
    uint32_t n = 0;
    for (int32_t i = s->head; i != SCHED_NONE; i = s->t[i].next)
        n++;
    return n;
}

const char *sched_state_str(enum sched_state st)
{
    switch (st) {
    case SCHED_FREE:    return "free";
    case SCHED_NEW:     return "new";
    case SCHED_READY:   return "ready";
    case SCHED_RUNNING: return "running";
    case SCHED_BLOCKED: return "blocked";
    case SCHED_ZOMBIE:  return "zombie";
    }
    return "?";
}

const char *sched_result_str(enum sched_result r)
{
    switch (r) {
    case SCHED_OK:              return "ok";
    case SCHED_BLOCKED_NOW:     return "blocked";
    case SCHED_WOULD_BLOCK:     return "would block (thread 0 polls)";
    case SCHED_ERR_FULL:        return "no free thread slot";
    case SCHED_ERR_NO_THREAD:   return "no such thread";
    case SCHED_ERR_STATE:       return "thread in the wrong state";
    case SCHED_ERR_SELF:        return "a thread cannot join itself";
    case SCHED_ERR_DETACHED:    return "detached threads cannot be joined";
    case SCHED_ERR_JOINED:      return "another thread is already joining it";
    case SCHED_ERR_THREAD0:     return "thread 0 may not block or exit";
    case SCHED_ERR_PREEMPT_OFF: return "called with preemption disabled";
    }
    return "?";
}

/* ---- kernel thread stacks ---- */

void kstack_init(struct kstack_pool *p)
{
    p->used = 0;
}

int kstack_alloc(struct kstack_pool *p)
{
    for (int i = 0; i < KSTACK_SLOTS; i++) {
        uint64_t bit = 1ull << i;
        if (!(p->used & bit)) {
            p->used |= bit;
            return i;
        }
    }
    return -1;
}

bool kstack_free(struct kstack_pool *p, int slot)
{
    if (slot < 0 || slot >= KSTACK_SLOTS)
        return false;
    uint64_t bit = 1ull << slot;
    if (!(p->used & bit))
        return false;
    p->used &= ~bit;
    return true;
}

uint32_t kstack_in_use(const struct kstack_pool *p)
{
    uint32_t n = 0;
    for (int i = 0; i < KSTACK_SLOTS; i++)
        if (p->used & (1ull << i))
            n++;
    return n;
}

uint64_t kstack_slot_base(int slot)
{
    return VM_KSTACK_BASE + (uint64_t)slot * KSTACK_SLOT_SIZE;
}

uint64_t kstack_bottom(int slot)
{
    return kstack_slot_base(slot) + KSTACK_GUARD_SIZE;
}

uint64_t kstack_top(int slot)
{
    return kstack_slot_base(slot) + KSTACK_SLOT_SIZE;
}

/* Slot index and offset within the slot, for an address in the area. */
static int slot_of(uint64_t addr, uint64_t *within)
{
    if (addr < VM_KSTACK_BASE)
        return -1;
    uint64_t off = addr - VM_KSTACK_BASE;
    if (off >= (uint64_t)KSTACK_SLOTS * KSTACK_SLOT_SIZE)
        return -1;
    *within = off % KSTACK_SLOT_SIZE;
    return (int)(off / KSTACK_SLOT_SIZE);
}

int kstack_guard_slot(uint64_t addr)
{
    uint64_t within;
    int slot = slot_of(addr, &within);
    return (slot >= 0 && within < KSTACK_GUARD_SIZE) ? slot : -1;
}

int kstack_stack_slot(uint64_t addr)
{
    uint64_t within;
    int slot = slot_of(addr, &within);
    return (slot >= 0 && within >= KSTACK_GUARD_SIZE) ? slot : -1;
}

/* ---- the longest turn and its owner (0.11) ---- */

void sched_longest_reset(struct sched_longest *l)
{
    l->ticks = 0;
    l->tid = -1;
    l->pid = 0;
    l->own_space = false;
    l->name[0] = '\0';
}

bool sched_longest_note(struct sched_longest *l, uint64_t ticks, int32_t tid, int32_t pid,
                        bool own_space, const char *name)
{
    if (ticks <= l->ticks)
        return false;
    l->ticks = ticks;
    l->tid = tid;
    l->pid = pid > 0 ? pid : 0;
    l->own_space = own_space;
    unsigned i = 0;
    for (; name && name[i] && i < SCHED_LONGEST_NAME_MAX - 1; i++)
        l->name[i] = name[i];
    l->name[i] = '\0';
    return true;
}

/* ---- policy (b): switch at preempt_enable (0.11) ---- */

bool sched_switch_on_enable(uint32_t count, bool pending, bool interrupts_on, bool ready,
                            bool panicking)
{
    return count == 0 && pending && interrupts_on && ready && !panicking;
}
