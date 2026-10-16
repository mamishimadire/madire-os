/* The kernel lock, the pure part: a FIFO ticket lock (see klock_core.h). */
#include "kernel/proc/klock_core.h"

static bool valid_tid(int32_t tid)
{
    return tid >= 0 && tid < SCHED_MAX_THREADS;
}

void klock_core_init(struct klock *l)
{
    l->owner = KLOCK_FREE;
    l->reserved = KLOCK_FREE;
    l->next = l->serving = 0;
    for (uint32_t i = 0; i < KLOCK_TICKETS; i++)
        l->ticket_tid[i] = KLOCK_FREE;
    l->since = 0;
    l->takes = l->contended = l->yields = l->yields0 = 0;
    l->hold_longest = l->wait_longest = l->wait_longest0 = 0;
    l->ahead_max0 = 0;
    l->release_pc_max = 0;
    l->release_pc_min = UINT32_MAX;
    l->reserves = 0;
}

uint32_t klock_core_ahead(const struct klock *l, uint32_t ticket)
{
    return ticket - l->serving;
}

enum klock_result klock_core_ticket(struct klock *l, int32_t tid, uint32_t *ticket)
{
    if (!valid_tid(tid))
        return KLOCK_ERR_BAD_TID;
    if (l->owner == tid || l->reserved == tid)
        return KLOCK_ERR_RECURSIVE;
    if (l->next - l->serving >= KLOCK_TICKETS)
        return KLOCK_ERR_FULL;
    uint32_t t = l->next++;
    l->ticket_tid[t % KLOCK_TICKETS] = tid;
    uint32_t ahead = klock_core_ahead(l, t);
    if (tid == 0 && ahead > l->ahead_max0)
        l->ahead_max0 = ahead;
    *ticket = t;
    return KLOCK_QUEUED;
}

/* The checks of a try; KLOCK_TAKEN when `ticket` is served now. */
static enum klock_result try_turn(const struct klock *l, int32_t tid, uint32_t ticket)
{
    if (!valid_tid(tid))
        return KLOCK_ERR_BAD_TID;
    if (l->owner == tid || l->reserved == tid)
        return KLOCK_ERR_RECURSIVE;
    if (l->ticket_tid[ticket % KLOCK_TICKETS] != tid || klock_core_ahead(l, ticket) >= KLOCK_TICKETS)
        return KLOCK_ERR_NOT_TICKET;
    if (l->serving != ticket || l->owner != KLOCK_FREE || l->reserved != KLOCK_FREE)
        return KLOCK_BUSY;
    return KLOCK_TAKEN;
}

/* A served turn's figures: its start and its wait. */
static void served(struct klock *l, int32_t tid, uint64_t now, uint64_t first_try, uint32_t yields)
{
    l->since = now;
    if (yields)
        l->contended++;
    uint64_t waited = now >= first_try ? now - first_try : 0;
    if (waited > l->wait_longest)
        l->wait_longest = waited;
    if (tid == 0 && waited > l->wait_longest0)
        l->wait_longest0 = waited;
}

enum klock_result klock_core_try(struct klock *l, int32_t tid, uint32_t ticket, uint64_t now,
                                 uint64_t first_try, uint32_t yields)
{
    enum klock_result r = try_turn(l, tid, ticket);
    if (r != KLOCK_TAKEN)
        return r;
    l->owner = tid;
    l->takes++;
    served(l, tid, now, first_try, yields);
    return KLOCK_TAKEN;
}

enum klock_result klock_core_reserve_try(struct klock *l, int32_t tid, uint32_t ticket,
                                         uint64_t now, uint64_t first_try, uint32_t yields,
                                         bool diag)
{
    if (!diag)
        return KLOCK_ERR_NOT_DIAG;
    enum klock_result r = try_turn(l, tid, ticket);
    if (r != KLOCK_TAKEN)
        return r;
    l->reserved = tid;
    l->reserves++;
    served(l, tid, now, first_try, yields);
    return KLOCK_RESERVED;
}

enum klock_result klock_core_take_reserved(struct klock *l, int32_t tid, bool diag)
{
    if (!diag)
        return KLOCK_ERR_NOT_DIAG;
    if (!valid_tid(tid))
        return KLOCK_ERR_BAD_TID;
    if (l->reserved != tid || l->owner != KLOCK_FREE)
        return KLOCK_ERR_NOT_RESERVED;
    l->reserved = KLOCK_FREE;
    l->owner = tid;
    l->takes++;
    return KLOCK_TAKEN;                 /* the hold's time began at the reservation */
}

enum klock_result klock_core_release(struct klock *l, int32_t tid, uint64_t now, uint32_t pc)
{
    if (!valid_tid(tid))
        return KLOCK_ERR_BAD_TID;
    if (l->owner != tid)
        return KLOCK_ERR_NOT_OWNER;
    uint64_t held = now >= l->since ? now - l->since : 0;
    if (held > l->hold_longest)
        l->hold_longest = held;
    if (pc > l->release_pc_max)
        l->release_pc_max = pc;
    if (pc < l->release_pc_min)
        l->release_pc_min = pc;
    l->ticket_tid[l->serving % KLOCK_TICKETS] = KLOCK_FREE;
    l->serving++;
    l->owner = KLOCK_FREE;
    return KLOCK_RELEASED;
}

enum klock_result klock_core_requeue(struct klock *l, int32_t tid, uint64_t now, uint32_t pc,
                                     uint32_t *ticket)
{
    if (!valid_tid(tid))
        return KLOCK_ERR_BAD_TID;
    if (l->owner != tid)
        return KLOCK_ERR_NOT_OWNER;
    if (pc != 1)                        /* stage 7b: only the hold's own count */
        return KLOCK_ERR_COUNT;
    enum klock_result r = klock_core_release(l, tid, now, pc);
    if (r != KLOCK_RELEASED)
        return r;
    return klock_core_ticket(l, tid, ticket);
}

void klock_core_waited(struct klock *l, int32_t tid)
{
    l->yields++;
    if (tid == 0)
        l->yields0++;
}

int32_t klock_core_ahead_bad(const struct klock *l, uint32_t ticket,
                             enum sched_state (*state)(int32_t tid), enum sched_state *bad_state)
{
    uint32_t n = 0;
    for (uint32_t t = l->serving; t != ticket && n < KLOCK_TICKETS; t++, n++) {
        int32_t tid = l->ticket_tid[t % KLOCK_TICKETS];
        enum sched_state s = valid_tid(tid) ? state(tid) : SCHED_FREE;
        if (!klock_core_owner_ok(s)) {
            *bad_state = s;
            return valid_tid(tid) ? tid : KLOCK_FREE - 1;
        }
    }
    return KLOCK_FREE;
}

enum klock_wait klock_core_wait_method(int32_t tid)
{
    (void)tid;                          /* no sleep queue in 1.0: everyone yields */
    return KLOCK_WAIT_YIELD;
}

bool klock_core_owner_ok(enum sched_state state)
{
    return state == SCHED_READY;
}

bool klock_core_held_by(const struct klock *l, int32_t tid)
{
    return l->owner != KLOCK_FREE && l->owner == tid;
}

const char *klock_result_str(enum klock_result r)
{
    switch (r) {
    case KLOCK_TAKEN:          return "taken";
    case KLOCK_BUSY:           return "busy";
    case KLOCK_QUEUED:         return "queued";
    case KLOCK_RELEASED:       return "released";
    case KLOCK_ERR_RECURSIVE:  return "taken twice by the same thread";
    case KLOCK_ERR_NOT_OWNER:  return "released by a thread that does not hold it";
    case KLOCK_ERR_BAD_TID:    return "not a thread id";
    case KLOCK_ERR_FULL:       return "more tickets out than threads";
    case KLOCK_ERR_NOT_TICKET: return "a try with a ticket that is not the caller's";
    case KLOCK_RESERVED:       return "reserved";
    case KLOCK_ERR_COUNT:      return "preemption count not the hold's own (1)";
    case KLOCK_ERR_NOT_DIAG:   return "a reserved turn outside a test= diagnostic";
    case KLOCK_ERR_NOT_RESERVED: return "a take without a reserved turn";
    }
    return "?";
}
