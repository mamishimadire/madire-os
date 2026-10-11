#ifndef MADIRE_PROC_USERTEST_H
#define MADIRE_PROC_USERTEST_H

/*
 * User-mode diagnostics (0.10). Stage 2 (CPU set-up, before any program
 * runs):
 *   test=rsp0  threads yield and are preempted; after every switch each one
 *              checks that the TSS's RSP0 is its own kernel stack top;
 *   test=clac  with RFLAGS.AC set, interrupts are taken: with SMAP on, the
 *              handler must see AC=0 (isr.asm's clac) and the clac count
 *              must grow; with SMAP off, clac must never run.
 * Stage 3 (address spaces, no ring 3):
 *   test=uaspace  heap growth under a process CR3 (seen under the kernel's),
 *                 then 500 address spaces built, entered, checked through
 *                 the recursive mapping and torn down by two threads that
 *                 are switched (preempted and yielding) in the middle of
 *                 builds and teardowns; free pages exactly restored.
 * Stage 4a (the first program in ring 3):
 *   test=userspin  a thread loads the built-in program spin into its own
 *                  address space and enters ring 3 with IRETQ; every timer
 *                  interrupt taken in ring 3 is checked (CS 0x3B, SS 0x33,
 *                  RIP in spin's code, frame on the thread's RSP0 stack),
 *                  and spin must be preempted from ring 3 and resumed
 *                  there. spin then keeps running (exit and kill come
 *                  later).
 * Stage 4b (the first process teardown):
 *   test=userkill  spin processes are started and killed: each ends at
 *                  its next return to ring 3 (or before entering it), and
 *                  its own thread tears its address space down; another
 *                  process keeps running meanwhile; free pages, page
 *                  tables, stack slots and threads exactly restored.
 * Stage 4c (fault containment, part 1):
 *   test=userfault  programs that read address 0, execute an invalid
 *                   instruction, run past their stack, write to their own
 *                   code and execute their own data (with NX): each ends
 *                   with its exception named in one log line, while a
 *                   spin process keeps running; everything restored.
 * Stage 4d (fault containment, part 2):
 *   test=userpriv  programs that try what ring 3 may not: nine privileged
 *                  instructions (#GP), INT to ring-0 gates (#GP with the
 *                  gate in the error code), x87/MMX (pxor)/SSE with CR0.EM set
 *                  (#NM, #UD); checked as for test=userfault.
 * Stage 4e (fault containment, part 3):
 *   test=userkmem  sti and int 0x0e (#GP), then kread and kwrite against
 *                  kernel text, kernel data (the heap) and the PML4
 *                  through the recursive slot: each a page fault
 *                  (protection violation, user mode) at that address; a
 *                  checksum of kernel text, read-only data, the kernel
 *                  PML4 and the heap's first block header is unchanged.
 * Stage 5a (the first system calls, no pointer arguments):
 *   test=usersys  exitcode (42, 43, 263 & 0xFF = 7, 44 from main), getpid,
 *                 yield, enosys and sysregs, while a spin process keeps
 *                 running: each exit status as expected, the exact number
 *                 of calls of each kind, cleanup exact.
 * Stage 5b (system calls with pointer arguments):
 *   test=userio  hello (a line; control characters and a fake kernel
 *                line; 5000 bytes, 4096 taken), sleep (100 ms = 10 ticks;
 *                argument errors), badptr (every bad pointer -EFAULT,
 *                nothing logged) and two isolate processes at once, while
 *                a spin process keeps running; cleanup exact.
 * Stage 6a:
 *   usertest_boot  at every boot (not a diagnostic): hello, exitcode and
 *                  nullptr as processes; fatal on failure (kernel.c);
 *                  deterministic: only results are checked, nothing timed;
 *   test=userpreempt  five spin processes (two pure loops, getpid,
 *                  sched_yield and nanosleep loops) and a kernel thread
 *                  allocating frames: each user process's longest turn at
 *                  most 6 ticks;
 *   test=userchurn  500 hello and 200 nullptr one after another, then 10
 *                  batches of 30 at once; everything exactly restored.
 * Stage 6b (crash tests; each returns only if the CPU lacks the feature):
 *   test=smep  the kernel calls a present, executable user page: with SMEP
 *              on, #PF (instruction fetch, kernel mode) and the crash
 *              screen naming SMEP;
 *   test=smap  with RFLAGS.AC clear, the kernel reads a present user page:
 *              with SMAP on, #PF (read, kernel mode) and the crash screen
 *              naming SMAP.
 * 0.11 stage 3 (ELF programs from boot modules):
 *   test=elfrun  each kept module with a built-in twin (hello, exitcode,
 *                nullptr, getpid, sleep, spin) gives the same result as
 *                the twin; two isolate instances at once; every refused
 *                file refused again at load (no address space built);
 *                no user frame inside a boot module; module checksums
 *                unchanged; cleanup exact. What it runs depends on the
 *                modules the boot was given.
 * Each returns true on PASS and logs the details.
 */

#include <stdbool.h>
#include <stdint.h>

bool usertest_rsp0(void);
bool usertest_clac(void);
bool usertest_uaspace(void);
bool usertest_userspin(void);
bool usertest_userkill(void);
bool usertest_userfault(void);
bool usertest_userpriv(void);
bool usertest_userkmem(void);
bool usertest_usersys(void);
bool usertest_userio(void);
bool usertest_boot(void);
bool usertest_userpreempt(void);
bool usertest_userchurn(void);
void usertest_smep(void);
void usertest_smap(void);
bool usertest_elfrun(void);
/* 0.12 stage 2b: each must panic (test=selfcheckcr3, selfcheckswitch,
 * selfcheckpick, selfcheckring3, selfcheckring3cr3; see usertest.c). */
void usertest_selfcheck_cr3(void);
void usertest_selfcheck_switch(void);
void usertest_selfcheck_pick(void);
void usertest_selfcheck_ring3(void);
void usertest_selfcheck_ring3cr3(void);
/* 0.12 stage 2b: test=killrace (only in the stress part). 0.12 stage 3:
 * with seeded (boot option seed=N), each kill comes after a pseudo-random
 * amount of extra work derived from seed; without, the delays are fixed. */
bool usertest_killrace(bool seeded, uint32_t seed);

#endif
