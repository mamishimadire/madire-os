/* Process table (see proc_core.h). */
#include "kernel/proc/proc_core.h"

void proc_init(struct proc_table *t)
{
    for (int i = 0; i < PROC_MAX; i++) {
        t->e[i].state = PROC_FREE;
        t->e[i].pid = 0;
        t->e[i].tid = -1;
        t->e[i].name[0] = 0;
        t->e[i].kill_pending = false;
        t->e[i].result.end = PROC_END_EXIT;
        t->e[i].result.code = 0;
        fd_table_init(&t->e[i].fds);
    }
    t->next_pid = 1;
    t->live = 0;
}

enum proc_status proc_alloc(struct proc_table *t, const char *name, int *slot, int32_t *pid)
{
    if (t->next_pid <= 0)
        return PROC_ERR_PIDS;               /* wrapped past INT32_MAX */
    for (int i = 0; i < PROC_MAX; i++) {
        struct proc_entry *p = &t->e[i];
        if (p->state != PROC_FREE)
            continue;
        p->state = PROC_ALIVE;
        p->pid = t->next_pid;
        p->tid = -1;
        unsigned n = 0;
        while (name && name[n] && n < PROC_NAME_MAX - 1) {
            p->name[n] = name[n];
            n++;
        }
        p->name[n] = 0;
        p->kill_pending = false;
        p->result.end = PROC_END_EXIT;
        p->result.code = 0;
        fd_table_init(&p->fds);
        t->next_pid = t->next_pid == INT32_MAX ? 0 : t->next_pid + 1;
        t->live++;
        *slot = i;
        *pid = p->pid;
        return PROC_OK;
    }
    return PROC_ERR_FULL;
}

int proc_find(const struct proc_table *t, int32_t pid)
{
    if (pid <= 0)
        return -1;
    for (int i = 0; i < PROC_MAX; i++)
        if (t->e[i].state != PROC_FREE && t->e[i].pid == pid)
            return i;
    return -1;
}

void proc_set_tid(struct proc_table *t, int slot, int32_t tid)
{
    t->e[slot].tid = tid;
}

enum proc_status proc_request_kill(struct proc_table *t, int32_t pid)
{
    int s = proc_find(t, pid);
    if (s < 0)
        return PROC_ERR_NO_SUCH;
    if (t->e[s].state != PROC_ALIVE)
        return PROC_ERR_NOT_ALIVE;
    t->e[s].kill_pending = true;
    return PROC_OK;
}

bool proc_kill_pending(const struct proc_table *t, int slot)
{
    return t->e[slot].state == PROC_ALIVE && t->e[slot].kill_pending;
}

bool proc_exit(struct proc_table *t, int slot, enum proc_end end, int32_t code)
{
    struct proc_entry *p = &t->e[slot];
    if (p->state != PROC_ALIVE)
        return false;
    p->state = PROC_ZOMBIE;
    p->kill_pending = false;
    p->result.end = end;
    p->result.code = code;
    return true;
}

enum proc_status proc_reap(struct proc_table *t, int32_t pid, struct proc_result *out)
{
    int s = proc_find(t, pid);
    if (s < 0)
        return PROC_ERR_NO_SUCH;
    struct proc_entry *p = &t->e[s];
    if (p->state != PROC_ZOMBIE)
        return PROC_ERR_NOT_EXITED;
    if (out)
        *out = p->result;
    p->state = PROC_FREE;
    p->pid = 0;
    p->tid = -1;
    p->name[0] = 0;
    t->live--;
    return PROC_OK;
}

uint32_t proc_live(const struct proc_table *t)
{
    return t->live;
}

const char *proc_end_str(enum proc_end end)
{
    switch (end) {
    case PROC_END_EXIT:   return "exited";
    case PROC_END_FAULT:  return "killed by a CPU exception";
    case PROC_END_KILLED: return "killed";
    case PROC_END_LOAD_FAILED: return "load failed";
    }
    return "unknown";
}
