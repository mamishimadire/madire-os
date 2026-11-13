/* The per-disk I/O lock, the pure part: a FIFO ticket lock with the
 * order check (see disklock_core.h). */
#include "kernel/proc/disklock_core.h"

static bool disklock_valid_tid(int32_t tid)
{
    return tid >= 0 && tid < SCHED_MAX_THREADS;
}

void disklock_core_init(struct disklock *l)
{
    l->on = false;
    l->owner = DISKLOCK_FREE;
    l->next = l->serving = 0;
    for (uint32_t i = 0; i < DISKLOCK_TICKETS; i++)
        l->ticket_tid[i] = DISKLOCK_FREE;
    l->since = 0;
    l->takes = l->contended = l->yields = l->yields0 = 0;
    l->hold_longest = l->wait_longest = l->wait_longest0 = 0;
}

uint32_t disklock_core_ahead(const struct disklock *l, uint32_t ticket)
{
    return ticket - l->serving;
}

/* True if thread `tid` holds a ticket not yet served (it waits). */
static bool disklock_has_ticket(const struct disklock *l, int32_t tid)
{
    uint32_t n = 0;
    for (uint32_t t = l->serving; t != l->next && n < DISKLOCK_TICKETS; t++, n++)
        if (l->ticket_tid[t % DISKLOCK_TICKETS] == tid && l->owner != tid)
            return true;
    return false;
}

enum disklock_result disklock_core_check(const struct disklock *l, int32_t tid, bool vfs_held, bool irq_on)
{
    if (!disklock_valid_tid(tid))
        return DISKLOCK_ERR_BAD_TID;
    if (!irq_on)
        return DISKLOCK_ERR_IRQ;                /* L4 */
    if (vfs_held)
        return DISKLOCK_ERR_ORDER;              /* L1 */
    if (l->owner == tid || disklock_has_ticket(l, tid))
        return DISKLOCK_ERR_RECURSIVE;          /* L3 */
    if (!l->on)
        return DISKLOCK_ERR_LIVE;               /* L6 */
    return DISKLOCK_OK;
}

enum disklock_result disklock_core_ticket(struct disklock *l, int32_t tid, uint32_t *ticket)
{
    if (!disklock_valid_tid(tid))
        return DISKLOCK_ERR_BAD_TID;
    if (l->owner == tid || disklock_has_ticket(l, tid))
        return DISKLOCK_ERR_RECURSIVE;
    if (l->next - l->serving >= DISKLOCK_TICKETS)
        return DISKLOCK_ERR_FULL;
    uint32_t t = l->next++;
    l->ticket_tid[t % DISKLOCK_TICKETS] = tid;
    *ticket = t;
    return DISKLOCK_QUEUED;
}

enum disklock_result disklock_core_try(struct disklock *l, int32_t tid, uint32_t ticket, uint64_t now,
                                       uint64_t first_try, uint32_t yields)
{
    if (!disklock_valid_tid(tid))
        return DISKLOCK_ERR_BAD_TID;
    if (l->owner == tid)
        return DISKLOCK_ERR_RECURSIVE;
    if (l->ticket_tid[ticket % DISKLOCK_TICKETS] != tid || disklock_core_ahead(l, ticket) >= DISKLOCK_TICKETS)
        return DISKLOCK_ERR_NOT_TICKET;
    if (l->serving != ticket || l->owner != DISKLOCK_FREE)
        return DISKLOCK_BUSY;
    l->owner = tid;
    l->takes++;
    l->since = now;
    if (yields)
        l->contended++;
    uint64_t waited = now >= first_try ? now - first_try : 0;
    if (waited > l->wait_longest)
        l->wait_longest = waited;
    if (tid == 0 && waited > l->wait_longest0)
        l->wait_longest0 = waited;
    return DISKLOCK_TAKEN;
}

enum disklock_result disklock_core_release(struct disklock *l, int32_t tid, uint64_t now)
{
    if (!disklock_valid_tid(tid))
        return DISKLOCK_ERR_BAD_TID;
    if (l->owner != tid)
        return DISKLOCK_ERR_NOT_OWNER;
    uint64_t held = now >= l->since ? now - l->since : 0;
    if (held > l->hold_longest)
        l->hold_longest = held;
    l->ticket_tid[l->serving % DISKLOCK_TICKETS] = DISKLOCK_FREE;
    l->serving++;
    l->owner = DISKLOCK_FREE;
    return DISKLOCK_RELEASED;
}

void disklock_core_waited(struct disklock *l, int32_t tid)
{
    l->yields++;
    if (tid == 0)
        l->yields0++;
}

int32_t disklock_core_ahead_bad(const struct disklock *l, uint32_t ticket,
                                enum sched_state (*state)(int32_t tid), enum sched_state *bad_state)
{
    uint32_t n = 0;
    for (uint32_t t = l->serving; t != ticket && n < DISKLOCK_TICKETS; t++, n++) {
        int32_t tid = l->ticket_tid[t % DISKLOCK_TICKETS];
        enum sched_state s = disklock_valid_tid(tid) ? state(tid) : SCHED_FREE;
        if (s != SCHED_READY) {
            *bad_state = s;
            return disklock_valid_tid(tid) ? tid : DISKLOCK_NO_THREAD;
        }
    }
    return DISKLOCK_FREE;
}

bool disklock_core_held_by(const struct disklock *l, int32_t tid)
{
    return l->owner != DISKLOCK_FREE && l->owner == tid;
}

bool disklock_core_involved(const struct disklock *l, int32_t tid)
{
    return disklock_valid_tid(tid) && (disklock_core_held_by(l, tid) || disklock_has_ticket(l, tid));
}

bool disklock_core_io_ok(const struct disklock *l, int32_t tid)
{
    return !l->on || disklock_core_held_by(l, tid);
}

bool disklock_core_may_yield(const struct disklock *l, int32_t tid, uint32_t pc, bool irq_on)
{
    return l->on && disklock_core_held_by(l, tid) && pc == 0 && irq_on;
}

const char *disklock_result_str(enum disklock_result r)
{
    switch (r) {
    case DISKLOCK_OK:             return "ok";
    case DISKLOCK_TAKEN:          return "taken";
    case DISKLOCK_BUSY:           return "busy";
    case DISKLOCK_QUEUED:         return "queued";
    case DISKLOCK_RELEASED:       return "released";
    case DISKLOCK_ERR_IRQ:        return "disk lock taken in an interrupt handler (L4)";
    case DISKLOCK_ERR_ORDER:      return "disk lock taken while the VFS lock is held (L1)";
    case DISKLOCK_ERR_RECURSIVE:  return "disk lock taken again by its owner (L3)";
    case DISKLOCK_ERR_LIVE:       return "disk lock taken in Live mode (L6)";
    case DISKLOCK_ERR_NOT_OWNER:  return "released by a thread that does not hold it";
    case DISKLOCK_ERR_BAD_TID:    return "not a thread id";
    case DISKLOCK_ERR_FULL:       return "more tickets out than threads";
    case DISKLOCK_ERR_NOT_TICKET: return "a try with a ticket that is not the caller's";
    }
    return "?";
}
