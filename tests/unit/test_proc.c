/* Unit tests for kernel/proc/proc_core.c (the process table, 0.10). */
#include <stdint.h>
#include <string.h>
#include "kernel/proc/proc_core.h"
#include "tests/unit/unit.h"

static struct proc_table t;

static void test_alloc_and_pids(void)
{
    proc_init(&t);
    int s = -1;
    int32_t pid = 0;
    CHECK_EQ(proc_live(&t), 0);
    CHECK_EQ(proc_alloc(&t, "hello", &s, &pid), PROC_OK);
    CHECK_EQ(s, 0);
    CHECK_EQ(pid, 1);                               /* PIDs start at 1 */
    CHECK(strcmp(t.e[s].name, "hello") == 0);
    CHECK_EQ(t.e[s].state, PROC_ALIVE);
    CHECK_EQ(t.e[s].tid, -1);
    CHECK_EQ(proc_alloc(&t, "exitcode", &s, &pid), PROC_OK);
    CHECK_EQ(s, 1);
    CHECK_EQ(pid, 2);
    CHECK_EQ(proc_live(&t), 2);
    CHECK_EQ(proc_find(&t, 1), 0);
    CHECK_EQ(proc_find(&t, 2), 1);
    CHECK_EQ(proc_find(&t, 3), -1);
    CHECK_EQ(proc_find(&t, 0), -1);
    CHECK_EQ(proc_find(&t, -1), -1);
    proc_set_tid(&t, 1, 7);
    CHECK_EQ(t.e[1].tid, 7);
}

static void test_name_cut(void)
{
    proc_init(&t);
    int s;
    int32_t pid;
    CHECK_EQ(proc_alloc(&t, "a_very_long_program_name", &s, &pid), PROC_OK);
    CHECK_EQ(strlen(t.e[s].name), PROC_NAME_MAX - 1);
    CHECK(strcmp(t.e[s].name, "a_very_long_pro") == 0);
    CHECK_EQ(proc_alloc(&t, 0, &s, &pid), PROC_OK);
    CHECK_EQ(t.e[s].name[0], 0);
}

static void test_exit_reap_and_no_reuse(void)
{
    proc_init(&t);
    int s;
    int32_t pid;
    CHECK_EQ(proc_alloc(&t, "exitcode", &s, &pid), PROC_OK);
    struct proc_result r = { PROC_END_KILLED, -5 };
    CHECK_EQ(proc_reap(&t, pid, &r), PROC_ERR_NOT_EXITED);      /* still running */
    CHECK(proc_exit(&t, s, PROC_END_EXIT, 42));
    CHECK(!proc_exit(&t, s, PROC_END_EXIT, 1));                 /* only once */
    CHECK_EQ(t.e[s].state, PROC_ZOMBIE);
    CHECK_EQ(proc_live(&t), 1);                                 /* a zombie still counts */
    CHECK_EQ(proc_request_kill(&t, pid), PROC_ERR_NOT_ALIVE);
    CHECK_EQ(proc_reap(&t, pid, &r), PROC_OK);
    CHECK_EQ(r.end, PROC_END_EXIT);
    CHECK_EQ(r.code, 42);
    CHECK_EQ(proc_live(&t), 0);
    CHECK_EQ(t.e[s].state, PROC_FREE);
    CHECK_EQ(proc_reap(&t, pid, &r), PROC_ERR_NO_SUCH);         /* only once */
    CHECK_EQ(proc_find(&t, pid), -1);
    int s2;
    int32_t pid2;
    CHECK_EQ(proc_alloc(&t, "again", &s2, &pid2), PROC_OK);
    CHECK_EQ(s2, s);                                            /* the slot is reused */
    CHECK_EQ(pid2, pid + 1);                                    /* the PID is not */
}

static void test_fault_and_kill_results(void)
{
    proc_init(&t);
    int a, b;
    int32_t pa, pb;
    CHECK_EQ(proc_alloc(&t, "nullptr", &a, &pa), PROC_OK);
    CHECK_EQ(proc_alloc(&t, "spin", &b, &pb), PROC_OK);
    CHECK(proc_exit(&t, a, PROC_END_FAULT, 14));
    CHECK(!proc_kill_pending(&t, b));
    CHECK_EQ(proc_request_kill(&t, pb), PROC_OK);
    CHECK(proc_kill_pending(&t, b));
    CHECK_EQ(proc_request_kill(&t, pb), PROC_OK);               /* asking twice is harmless */
    CHECK_EQ(proc_request_kill(&t, 99), PROC_ERR_NO_SUCH);
    CHECK(proc_exit(&t, b, PROC_END_KILLED, 0));
    CHECK(!proc_kill_pending(&t, b));                           /* cleared once it has ended */
    struct proc_result r;
    CHECK_EQ(proc_reap(&t, pa, &r), PROC_OK);
    CHECK_EQ(r.end, PROC_END_FAULT);
    CHECK_EQ(r.code, 14);
    CHECK_EQ(proc_reap(&t, pb, &r), PROC_OK);
    CHECK_EQ(r.end, PROC_END_KILLED);
    CHECK_EQ(r.code, 0);
    CHECK(strcmp(proc_end_str(PROC_END_FAULT), "killed by a CPU exception") == 0);
}

static void test_table_full(void)
{
    proc_init(&t);
    int s;
    int32_t pid;
    for (int i = 0; i < PROC_MAX; i++)
        CHECK_EQ(proc_alloc(&t, "p", &s, &pid), PROC_OK);
    CHECK_EQ(proc_live(&t), PROC_MAX);
    CHECK_EQ(proc_alloc(&t, "one too many", &s, &pid), PROC_ERR_FULL);
    CHECK(proc_exit(&t, 10, PROC_END_EXIT, 0));
    CHECK_EQ(proc_alloc(&t, "still full", &s, &pid), PROC_ERR_FULL);  /* a zombie holds its slot */
    CHECK_EQ(proc_reap(&t, t.e[10].pid, 0), PROC_OK);
    CHECK_EQ(proc_alloc(&t, "fits", &s, &pid), PROC_OK);
    CHECK_EQ(s, 10);
    CHECK_EQ(pid, PROC_MAX + 1);
}

static void test_hundreds_of_processes(void)
{
    proc_init(&t);
    int s;
    int32_t pid = 0;
    struct proc_result r;
    for (int i = 0; i < 1000; i++) {
        CHECK_EQ(proc_alloc(&t, "hello", &s, &pid), PROC_OK);
        CHECK_EQ(s, 0);
        CHECK(proc_exit(&t, s, PROC_END_EXIT, i & 0xFF));
        CHECK_EQ(proc_reap(&t, pid, &r), PROC_OK);
        CHECK_EQ(r.code, i & 0xFF);
    }
    CHECK_EQ(pid, 1000);
    CHECK_EQ(proc_live(&t), 0);
}

static void test_pids_used_up(void)
{
    proc_init(&t);
    t.next_pid = INT32_MAX;
    int s;
    int32_t pid;
    CHECK_EQ(proc_alloc(&t, "last", &s, &pid), PROC_OK);
    CHECK_EQ(pid, INT32_MAX);
    CHECK_EQ(proc_alloc(&t, "none left", &s, &pid), PROC_ERR_PIDS);
    CHECK_EQ(proc_live(&t), 1);
}

int main(void)
{
    RUN(test_alloc_and_pids);
    RUN(test_name_cut);
    RUN(test_exit_reap_and_no_reuse);
    RUN(test_fault_and_kill_results);
    RUN(test_table_full);
    RUN(test_hundreds_of_processes);
    RUN(test_pids_used_up);
    return unit_report("proc");
}
