/* The kernel lock, the pure part (see klock_core.h). */
#include "kernel/proc/klock_core.h"

static bool valid_tid(int32_t tid)
{
    return tid >= 0 && tid < SCHED_MAX_THREADS;
}

void klock_core_init(struct klock *l)
{
    l->owner = KLOCK_FREE;
    l->since = 0;
    l->takes = l->contended = l->yields = l->yields0 = 0;
    l->hold_longest = l->wait_longest = l->wait_longest0 = 0;
    l->release_pc_max = 0;
}

enum klock_result klock_core_try(struct klock *l, int32_t tid, uint64_t now, uint64_t first_try,
                                 uint32_t yields)
{
    if (!valid_tid(tid))
        return KLOCK_ERR_BAD_TID;
    if (l->owner == tid)
        return KLOCK_ERR_RECURSIVE;
    if (l->owner != KLOCK_FREE)
        return KLOCK_BUSY;
    l->owner = tid;
    l->since = now;
    l->takes++;
    if (yields)
        l->contended++;
    uint64_t waited = now >= first_try ? now - first_try : 0;
    if (waited > l->wait_longest)
        l->wait_longest = waited;
    if (tid == 0 && waited > l->wait_longest0)
        l->wait_longest0 = waited;
    return KLOCK_TAKEN;
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
    l->owner = KLOCK_FREE;
    return KLOCK_RELEASED;
}

void klock_core_waited(struct klock *l, int32_t tid)
{
    l->yields++;
    if (tid == 0)
        l->yields0++;
}

enum klock_wait klock_core_wait_method(int32_t tid)
{
    (void)tid;                          /* no sleep queue in 1.0: everyone yields */
    return KLOCK_WAIT_YIELD;
}

bool klock_core_owner_ok(enum sched_state owner_state)
{
    return owner_state == SCHED_READY;
}

bool klock_core_held_by(const struct klock *l, int32_t tid)
{
    return l->owner != KLOCK_FREE && l->owner == tid;
}

const char *klock_result_str(enum klock_result r)
{
    switch (r) {
    case KLOCK_TAKEN:         return "taken";
    case KLOCK_BUSY:          return "busy";
    case KLOCK_RELEASED:      return "released";
    case KLOCK_ERR_RECURSIVE: return "taken twice by the same thread";
    case KLOCK_ERR_NOT_OWNER: return "released by a thread that does not hold it";
    case KLOCK_ERR_BAD_TID:   return "not a thread id";
    }
    return "?";
}
